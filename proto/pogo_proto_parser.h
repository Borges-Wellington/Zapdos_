// src/proto/pogo_proto_parser.h
#ifndef POGO_PROTO_PARSER_H
#define POGO_PROTO_PARSER_H

#include <string>

class PogoProtoParser {
public:
    static std::string parseRequest(void* request);
    static std::string parseResponse(void* response);
    static std::string parseProtoData(const void* data, size_t size);
    
private:
    static std::string protoToString(const void* protoData);
};

#endif // POGO_PROTO_PARSER_H