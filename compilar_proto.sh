#!bin bash
# compile proto_gen/rotom.pb.cc junto com o inspector
g++ -std=c++17 -I./proto_gen -I./ -I/usr/include \
    proto_gen/rotom.pb.cc src/inspector.cpp \
    -lprotobuf -lpthread -lz -o inspector
