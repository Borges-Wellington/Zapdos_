#pragma once
#include <string>

void save_payload_to_samples(const std::string &payload, const std::string &prefix);
std::string zlib_compress(const std::string &in);
std::string zlib_decompress(const std::string &in);
