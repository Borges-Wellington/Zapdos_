// src/ProtobufInspector.cpp
#include <iostream>
#include <fstream>
#include "rotom.pb.h"

int main_inspect(const std::string& path) {
    std::ifstream ifs(path, std::ios::binary);
    if (!ifs) {
        std::cerr << "Cannot open " << path << std::endl;
        return 1;
    }
    std::string data((std::istreambuf_iterator<char>(ifs)), std::istreambuf_iterator<char>());

    RotomProtos::MitmRequest req;
    if (!req.ParseFromString(data)) {
        std::cerr << "Failed to parse MitmRequest from " << path << std::endl;
        return 2;
    }

    std::cout << "Parsed MitmRequest:\n";
    std::cout << req.DebugString() << std::endl;
    return 0;
}

// If you want to compile this file as standalone tool, wrap a main:
#ifdef PROTOBUF_INSPECT_MAIN
int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr << "usage: inspector <sample.bin>\n";
        return 1;
    }
    return main_inspect(argv[1]);
}
#endif
