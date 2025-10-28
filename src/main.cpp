#include <iostream>
#include <thread>
#include <vector>
#include <chrono>
#include <atomic>
#include <sstream>

#include <nlohmann/json.hpp>
#include <websocketpp/config/asio_no_tls_client.hpp>
#include <websocketpp/client.hpp>
#include <google/protobuf/message.h>

#include "ConfigLoader.h"
#include "Utils.hpp"
#include "rotom.pb.h"

using json = nlohmann::json;
using client_t = websocketpp::client<websocketpp::config::asio_client>;
using message_ptr = websocketpp::config::asio_client::message_type::ptr;

void run_proto_channel(client_t& c, const std::string& workerId, const AppConfig& cfg, std::atomic<int>& active_workers);
static void attach_common_handlers(client_t& c, const std::string &name);

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    std::string cfg_err;
    auto cfg_opt = load_config_from_file("config/config.json", cfg_err);
    if (!cfg_opt) {
        std::cerr << "❌ Erro ao carregar config: " << cfg_err << std::endl;
        return 1;
    }

    AppConfig cfg = *cfg_opt;
    std::string deviceId = cfg.general.device_name;
    std::string device_origin = "Zapdos-" + deviceId;

    std::cout << "🔌 worker_endpoint: " << cfg.rotom.worker_endpoint << "\n";
    std::cout << "🔌 device_endpoint: " << cfg.rotom.device_endpoint << "\n";
    std::cout << "📱 device_name: " << deviceId 
              << " | workers=" << cfg.general.workers 
              << " | delay=" << cfg.tuning.worker_spawn_delay_ms << "ms\n";

    client_t control;
    control.init_asio();
    attach_common_handlers(control, "control");

    std::atomic<int> active_workers{0};

    // Handler para canal de controle
    control.set_open_handler([&](websocketpp::connection_hdl hdl) {
        std::cout << "[control] ✅ conectado\n";

        json intro = {
            {"deviceId", deviceId},
            {"version", 101},
            {"origin", device_origin},
            {"publicIp", "0.0.0.0"}
        };

        websocketpp::lib::error_code ec;
        control.send(hdl, intro.dump(), websocketpp::frame::opcode::text, ec);
        if (ec) {
            std::cerr << "[control] erro ao enviar intro: " << ec.message() << "\n";
            return;
        }

        std::cout << "[control] intro enviado: " << intro.dump() << "\n";

        // Enviar carga inicial (0/MAX)
        int total = cfg.general.workers;
        json load = {
            {"method", "worker_load"},
            {"body", {
                {"used", 0},
                {"max", total},
                {"deviceId", deviceId},
                {"origin", device_origin}
            }}
        };
        control.send(hdl, load.dump(), websocketpp::frame::opcode::text, ec);
        std::cout << "[control] carga inicial 0/" << total << "\n";

        // Thread para atualização periódica do load
        std::thread([hdl, &control, &cfg, deviceId, device_origin, &active_workers]() {
            int lastUsed = -1;
            const int total = cfg.general.workers;
            while (true) {
                int used = active_workers.load();
                if (used != lastUsed) {
                    lastUsed = used;
                    json loadUpd = {
                        {"method", "worker_load"},
                        {"body", {
                            {"used", used},
                            {"max", total},
                            {"deviceId", deviceId},
                            {"origin", device_origin}
                        }}
                    };
                    websocketpp::lib::error_code ec2;
                    control.send(hdl, loadUpd.dump(), websocketpp::frame::opcode::text, ec2);
                    if (!ec2)
                        std::cout << "[load] atualizado: " << used << "/" << total << "\n";
                }
                std::this_thread::sleep_for(std::chrono::seconds(3));
            }
        }).detach();
    });

    websocketpp::lib::error_code ec;
    auto con_ctrl = control.get_connection(cfg.rotom.device_endpoint, ec);
    if (ec) {
        std::cerr << "[control] erro ao conectar: " << ec.message() << "\n";
        return 2;
    }
    control.connect(con_ctrl);

    // Thread separada para o canal de controle
    std::thread t_ctrl([&](){ control.run(); });

    // ==== INICIAR WORKERS ====
    int total = cfg.general.workers;
    int delay = cfg.tuning.worker_spawn_delay_ms;
    std::vector<std::thread> worker_threads;

    for (int i = 1; i <= total; ++i) {
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
        std::string workerId = deviceId + "-w" + std::to_string(i);

        worker_threads.emplace_back([&, workerId, i]() {
            client_t proto;
            proto.init_asio();
            attach_common_handlers(proto, "proto-" + std::to_string(i));

            run_proto_channel(proto, workerId, cfg, active_workers);

            websocketpp::lib::error_code ec2;
            auto con_proto = proto.get_connection(cfg.rotom.worker_endpoint, ec2);
            if (ec2) {
                std::cerr << "[proto-" << i << "] erro get_connection: " << ec2.message() << "\n";
                return;
            }

            proto.connect(con_proto);
            std::cout << "[load] started worker " << i << "/" << total 
                      << " => id=" << workerId << "\n";

            proto.run();
        });
    }

    for (auto &t : worker_threads) {
        if (t.joinable()) t.join();
    }

    t_ctrl.join();
    google::protobuf::ShutdownProtobufLibrary();
    return 0;
}

// ===== Handlers compartilhados =====
static void attach_common_handlers(client_t& c, const std::string &name) {
    c.set_access_channels(websocketpp::log::alevel::none);
    c.set_fail_handler([name,&c](websocketpp::connection_hdl h) {
        try {
            auto con = c.get_con_from_hdl(h);
            std::cerr << "[" << name << "] FAIL: ec='" << con->get_ec().message()
                      << "' status=" << con->get_response_code()
                      << " reason='" << con->get_response_msg() << "'\n";
        } catch (...) {
            std::cerr << "[" << name << "] FAIL (sem conexão)\n";
        }
    });

    c.set_close_handler([name,&c](websocketpp::connection_hdl h) {
        try {
            auto con = c.get_con_from_hdl(h);
            std::cerr << "[" << name << "] CLOSE code=" << con->get_remote_close_code()
                      << " reason='" << con->get_remote_close_reason() << "'\n";
        } catch (...) {
            std::cerr << "[" << name << "] CLOSE (sem conexão)\n";
        }
    });
}
