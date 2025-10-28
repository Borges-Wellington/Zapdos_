#include "Utils.hpp"
#include <fstream>
#include <chrono>
#include <iomanip>
#include <sstream>
#include <sys/stat.h>
#include <zlib.h>
#include <iostream>

void ensure_dir(const std::string &dir) {
    struct stat st;
    if (stat(dir.c_str(), &st) != 0) {
        mkdir(dir.c_str(), 0755);
    }
}

void save_payload_to_samples(const std::string &payload, const std::string &prefix) {
    ensure_dir("samples");
    using namespace std::chrono;
    auto t = duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
    std::ostringstream ss;
    ss << "samples/" << prefix << "_" << t << ".bin";
    std::ofstream ofs(ss.str(), std::ios::binary);
    if(!ofs) return;
    ofs.write(payload.data(), payload.size());
    std::cout << "[utils] saved " << ss.str() << " (" << payload.size() << " bytes)\n";
}

std::string zlib_compress(const std::string &in) {
    if (in.empty()) return {};
    z_stream zs{};
    if (deflateInit(&zs, Z_DEFAULT_COMPRESSION) != Z_OK) return {};
    zs.next_in = (Bytef*)in.data();
    zs.avail_in = (uInt)in.size();

    std::string out;
    out.resize(in.size() / 2 + 128);
    int ret;
    do {
        zs.next_out = (Bytef*)out.data() + zs.total_out;
        zs.avail_out = (uInt)(out.size() - zs.total_out);
        ret = deflate(&zs, Z_FINISH);
        if (ret == Z_STREAM_ERROR) { deflateEnd(&zs); return {}; }
        if (zs.avail_out == 0) out.resize(out.size() * 2);
    } while (zs.avail_out == 0);
    deflateEnd(&zs);
    out.resize(zs.total_out);
    return out;
}

std::string zlib_decompress(const std::string &in) {
    if (in.empty()) return {};
    z_stream zs{};
    if (inflateInit(&zs) != Z_OK) return {};
    zs.next_in = (Bytef*)in.data();
    zs.avail_in = (uInt)in.size();

    std::string out;
    out.resize(in.size() * 4 + 256);
    int ret;
    do {
        zs.next_out = (Bytef*)out.data() + zs.total_out;
        zs.avail_out = (uInt)(out.size() - zs.total_out);
        ret = inflate(&zs, Z_SYNC_FLUSH);
        if (ret == Z_STREAM_ERROR || ret == Z_DATA_ERROR) { inflateEnd(&zs); return {}; }
        if (zs.avail_out == 0) out.resize(out.size() * 2);
    } while (ret != Z_STREAM_END && zs.avail_in > 0);
    inflateEnd(&zs);
    out.resize(zs.total_out);
    return out;
}
