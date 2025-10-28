// tools/gen_sample.cpp
#include <fstream>
#include <iostream>
#include <string>
#include <filesystem>

#include "proto_gen/rotom.pb.h"

int main() {
    GOOGLE_PROTOBUF_VERIFY_VERSION;

    // Criar um MitmRequest vazio (ou preencher via reflection se preferir)
    RotomProtos::MitmRequest req;

    // Exemplo opcional: setar um subcampo via reflection (descomente/adapte se quiser)
    /*
    auto d = req.GetDescriptor();
    auto r = req.GetReflection();
    auto f = d->FindFieldByName("login_request");
    if (f) {
        google::protobuf::Message* sub = r->MutableMessage(&req, f);
        auto sd = sub->GetDescriptor();
        auto sr = sub->GetReflection();
        auto tokenF = sd->FindFieldByName("ptc_token");
        if (tokenF) {
            // Exemplo: se ptc_token.value existir e for string
            auto sub2 = sr->MutableMessage(sub, tokenF);
            auto sd2 = sub2->GetDescriptor();
            auto sr2 = sub2->GetReflection();
            auto valueF = sd2->FindFieldByName("value");
            if (valueF && valueF->cpp_type() == google::protobuf::FieldDescriptor::CPPTYPE_STRING) {
                sr2->SetString(sub2, valueF, "EXEMPLO_TOKEN");
            }
        }
    }
    */

    std::string out;
    if (!req.SerializeToString(&out)) {
        std::cerr << "Falha ao serializar MitmRequest\n";
        return 1;
    }

    std::filesystem::create_directories("samples");
    std::ofstream ofs("samples/sample_request.bin", std::ios::binary);
    ofs.write(out.data(), out.size());
    ofs.close();

    std::cout << "Escrito samples/sample_request.bin (" << out.size() << " bytes)\n";
    return 0;
}
