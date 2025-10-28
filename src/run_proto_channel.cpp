#include <iostream>
#include <string>
#include <chrono>
#include <thread>
#include <functional>
#include <atomic>
#include <sstream>

#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>

#include "rotom.pb.h"
#include "Utils.hpp"
#include "ConfigLoader.h"

#include <google/protobuf/descriptor.h>
#include <google/protobuf/message.h>
#include <google/protobuf/reflection.h>

using client_t = websocketpp::client<websocketpp::config::asio_client>;
using message_ptr = websocketpp::config::asio_client::message_type::ptr;

// === Declarações ===
static void attach_proto_handlers(client_t &c, const std::string &workerId, const AppConfig &cfg, std::atomic<int>* active_count);
static void send_quick_login_response(client_t &c, websocketpp::connection_hdl hdl, const AppConfig &cfg, const RotomProtos::MitmRequest &req);

// === Implementação pública chamada pelo main.cpp ===
void run_proto_channel(client_t& c,
                       const std::string& workerId,
                       const AppConfig& cfg,
                       std::atomic<int>& active_workers)
{
    attach_proto_handlers(c, workerId, cfg, &active_workers);
}

// === Envia resposta rápida de login ===
static void send_quick_login_response(client_t &c,
                                      websocketpp::connection_hdl hdl,
                                      const AppConfig &cfg,
                                      const RotomProtos::MitmRequest & /*req*/) {
    try {
        RotomProtos::MitmResponse resp;

        // Constrói o login_response usando reflection
        const google::protobuf::Descriptor* rd = resp.GetDescriptor();
        const google::protobuf::FieldDescriptor* loginRespField = rd->FindFieldByName("login_response");
        if (loginRespField) {
            google::protobuf::Message* lrMsg = resp.GetReflection()->MutableMessage(&resp, loginRespField);
            const google::protobuf::Descriptor* lrDesc = lrMsg->GetDescriptor();

            // set supports_compression
            const auto* sc = lrDesc->FindFieldByName("supports_compression");
            if (sc && sc->type() == google::protobuf::FieldDescriptor::TYPE_BOOL) {
                lrMsg->GetReflection()->SetBool(lrMsg, sc, cfg.rotom.use_compression);
            }

            // tenta setar status enum (READY, OK ou fallback)
            const auto* statusFd = lrDesc->FindFieldByName("status");
            if (statusFd && statusFd->type() == google::protobuf::FieldDescriptor::TYPE_ENUM) {
                const auto* enumDesc = statusFd->enum_type();
                const auto* ev = enumDesc->FindValueByName("READY");
                if (!ev) ev = enumDesc->FindValueByName("OK");
                if (!ev && enumDesc->value_count() > 0) ev = enumDesc->value(0);
                if (ev) lrMsg->GetReflection()->SetEnum(lrMsg, statusFd, ev);
            }

            // set usado/max (se existir)
            const auto* usedFd = lrDesc->FindFieldByName("used_workers");
            const auto* maxFd  = lrDesc->FindFieldByName("max_workers");
            if (usedFd && maxFd) {
                lrMsg->GetReflection()->SetInt32(lrMsg, usedFd, 1);
                lrMsg->GetReflection()->SetInt32(lrMsg, maxFd, cfg.general.workers);
            }
        }

        std::string out;
        resp.SerializeToString(&out);
        if (cfg.rotom.use_compression && out.size() > 64)
            out = zlib_compress(out);

        websocketpp::lib::error_code ec;
        c.send(hdl, out, websocketpp::frame::opcode::binary, ec);
        if (ec)
            std::cerr << "[proto] erro ao enviar LoginResponse: " << ec.message() << std::endl;
        else
            std::cout << "[proto] quick LoginResponse sent\n";
    } catch (const std::exception &ex) {
        std::cerr << "[proto] exception in send_quick_login_response: " << ex.what() << std::endl;
    }
}

// === Handlers principais do canal proto ===
static void attach_proto_handlers(client_t &c,
                                  const std::string &workerId,
                                  const AppConfig &cfg,
                                  std::atomic<int>* active_count)
{
    // Conectou: envia WelcomeMessage e incrementa contador
    c.set_open_handler([&c, workerId, &cfg, active_count](websocketpp::connection_hdl hdl) {
        std::cout << "[proto] ✅ connected open, sending WelcomeMessage\n";

        RotomProtos::WelcomeMessage w;
        w.set_worker_id(workerId);
        w.set_version_code(101);
        w.set_version_name("Zapdos-Worker");
        w.set_origin("zapdos");
        w.set_useragent("Zapdos/1.0");
        w.set_device_id(workerId);

        std::string bin;
        w.SerializeToString(&bin);

        websocketpp::lib::error_code ec;
        c.send(hdl, bin, websocketpp::frame::opcode::binary, ec);
        if (ec) {
            std::cerr << "[proto] WelcomeMessage send error: " << ec.message() << std::endl;
            return;
        }
        std::cout << "[proto] WelcomeMessage sent for " << workerId << std::endl;

        if (active_count) active_count->fetch_add(1);

        // Envia LoginResponse fake inicial
        RotomProtos::MitmResponse resp;
        auto* loginResp = resp.mutable_login_response();
        if (loginResp) {
            loginResp->set_supports_compression(cfg.rotom.use_compression);
        }

        std::string respBin;
        resp.SerializeToString(&respBin);
        c.send(hdl, respBin, websocketpp::frame::opcode::binary, ec);
        if (!ec)
            std::cout << "[proto] sent fake LoginResponse for " << workerId << std::endl;
    });

    // Recebe mensagem (MitmRequest)
    c.set_message_handler([&c, &cfg](websocketpp::connection_hdl hdl, message_ptr msg) {
        try {
            auto payload = msg->get_payload();
            save_payload_to_samples(payload, "mitm");

            RotomProtos::MitmRequest mitmReq;
            bool parsed = mitmReq.ParseFromString(payload);
            if (!parsed) {
                std::string dec = zlib_decompress(payload);
                if (!dec.empty()) parsed = mitmReq.ParseFromString(dec);
            }

            if (!parsed) {
                std::cerr << "[proto] failed to parse MitmRequest (len=" << payload.size() << ")\n";
                return;
            }

            // Se for LOGIN → responde imediatamente
            if (mitmReq.method() == RotomProtos::MitmRequest_Method_LOGIN && mitmReq.has_login_request()) {
                std::cout << "[proto] LOGIN request received for worker\n";
                send_quick_login_response(c, hdl, cfg, mitmReq);
                return;
            }

            std::cout << "[proto] MitmRequest method=" << mitmReq.method() << std::endl;
        } catch (const std::exception &ex) {
            std::cerr << "[proto] exception in message handler: " << ex.what() << std::endl;
        }
    });

    // FAIL handler
    c.set_fail_handler([&c](websocketpp::connection_hdl h) {
        try {
            auto con = c.get_con_from_hdl(h);
            std::cerr << "[proto] FAIL: ec='" << con->get_ec().message()
                      << "' http_status=" << con->get_response_code()
                      << " reason='" << con->get_response_msg() << "'\n";
        } catch (...) {
            std::cerr << "[proto] FAIL (connection unavailable)\n";
        }
    });

    // CLOSE handler: decrementa contador
    c.set_close_handler([&c, active_count](websocketpp::connection_hdl h) {
        try {
            auto con = c.get_con_from_hdl(h);
            std::cerr << "[proto] CLOSE code=" << con->get_remote_close_code()
                      << " reason='" << con->get_remote_close_reason() << "'\n";
        } catch (...) {
            std::cerr << "[proto] CLOSE (unknown)\n";
        }

        if (active_count) {
            int prev = active_count->fetch_sub(1, std::memory_order_relaxed);
            if (prev <= 0) active_count->store(0);
        }
    });

    // PONG handler
    c.set_pong_handler([](websocketpp::connection_hdl, std::string payload) {
        std::cout << "[proto] PONG len=" << payload.size() << "\n";
    });
}
