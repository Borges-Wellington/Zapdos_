// src/inspector.cpp
#include <iostream>
#include <fstream>
#include <string>
#include <memory>

#include <google/protobuf/descriptor.h>
#include <google/protobuf/dynamic_message.h>
#include <google/protobuf/message.h>
#include <zlib.h>

static std::string try_zlib_decompress(const std::string &in) {
    if (in.empty()) return {};
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return {};
    zs.next_in = (Bytef*)in.data();
    zs.avail_in = (uInt)in.size();

    std::string out;
    out.resize(in.size() * 4 + 1024);
    int ret;
    do {
        zs.next_out = (Bytef*)out.data() + zs.total_out;
        zs.avail_out = (uInt)(out.size() - zs.total_out);
        ret = inflate(&zs, Z_SYNC_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR) {
            inflateEnd(&zs);
            return {};
        }
        if (zs.avail_out == 0) out.resize(out.size() * 2);
    } while (ret != Z_STREAM_END && zs.avail_in > 0);

    inflateEnd(&zs);
    out.resize(zs.total_out);
    return out;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "Usage: inspector <file.bin>\n";
        return 1;
    }
    std::string path = argv[1];
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cerr << "Cannot open file: " << path << "\n";
        return 2;
    }
    std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    using namespace google::protobuf;
    const DescriptorPool* pool = DescriptorPool::generated_pool();

    // Candidate: RotomProtos.MitmRequest (ajuste se seu package for diferente)
    const std::string candidate = "RotomProtos.MitmRequest";
    const Descriptor* desc = pool->FindMessageTypeByName(candidate);

    if (!desc) {
        std::cerr << "[inspector] Descriptor '" << candidate << "' not found in generated pool.\n";
        std::cerr << "[inspector] Abra proto_gen/rotom.pb.h para verificar o nome do tipo.\n";
        return 5;
    }

    DynamicMessageFactory factory;
    const Message* prototype = factory.GetPrototype(desc);
    if (!prototype) {
        std::cerr << "[inspector] Cannot get prototype for descriptor: " << candidate << "\n";
        return 3;
    }

    std::unique_ptr<Message> msg(prototype->New());

    if (msg->ParseFromString(data)) {
        std::cout << "Parsed as " << candidate << " (raw)\n";
        std::cout << msg->DebugString() << "\n";
        return 0;
    }

    std::string dec = try_zlib_decompress(data);
    if (!dec.empty()) {
        if (msg->ParseFromString(dec)) {
            std::cout << "Parsed as " << candidate << " (after zlib decompression)\n";
            std::cout << msg->DebugString() << "\n";
            return 0;
        } else {
            std::cerr << "[inspector] Decompressed but failed to parse as " << candidate << "\n";
        }
    } else {
        std::cerr << "[inspector] zlib decompress returned empty / failed.\n";
    }

    std::cerr << "[inspector] Could not parse the file as " << candidate << ".\n";
    std::cerr << "[inspector] Data size: " << data.size() << " bytes\n";
    return 4;
}
