#include "ConfigLoader.h"
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <optional>  

using json = nlohmann::json;

static bool has_and_string(const json& j, const char* key) {
    return j.contains(key) && j[key].is_string();
}

std::optional<AppConfig> load_config_from_file(const std::string& path, std::string& out_err) {
    std::ifstream ifs(path);
    if (!ifs.is_open()) {
        out_err = "Cannot open config file: " + path;
        return std::nullopt;
    }

    json j;
    try {
        ifs >> j;
    } catch (const std::exception& ex) {
        out_err = std::string("Failed to parse JSON: ") + ex.what();
        return std::nullopt;
    }

    AppConfig cfg;

    try {
        if (j.contains("rotom")) {
            auto r = j["rotom"];
            if (has_and_string(r, "worker_endpoint")) cfg.rotom.worker_endpoint = r["worker_endpoint"].get<std::string>();
            if (has_and_string(r, "device_endpoint")) cfg.rotom.device_endpoint = r["device_endpoint"].get<std::string>();
            if (has_and_string(r, "secret")) cfg.rotom.secret = r["secret"].get<std::string>();
            if (r.contains("use_compression") && r["use_compression"].is_boolean()) cfg.rotom.use_compression = r["use_compression"].get<bool>();
        }

        if (j.contains("general")) {
            auto g = j["general"];
            if (has_and_string(g, "device_name")) cfg.general.device_name = g["device_name"].get<std::string>();
            if (g.contains("workers") && g["workers"].is_number_integer()) cfg.general.workers = g["workers"].get<int>();
            if (has_and_string(g, "dns_server")) cfg.general.dns_server = g["dns_server"].get<std::string>();
            if (has_and_string(g, "scan_dir")) cfg.general.scan_dir = g["scan_dir"].get<std::string>();
        }

        if (j.contains("log")) {
            auto l = j["log"];
            if (has_and_string(l, "level")) cfg.log.level = l["level"].get<std::string>();
            if (l.contains("use_colors") && l["use_colors"].is_boolean()) cfg.log.use_colors = l["use_colors"].get<bool>();
            if (l.contains("log_to_file") && l["log_to_file"].is_boolean()) cfg.log.log_to_file = l["log_to_file"].get<bool>();
            if (l.contains("max_size") && l["max_size"].is_number_integer()) cfg.log.max_size = l["max_size"].get<int>();
            if (l.contains("max_backups") && l["max_backups"].is_number_integer()) cfg.log.max_backups = l["max_backups"].get<int>();
            if (l.contains("max_age") && l["max_age"].is_number_integer()) cfg.log.max_age = l["max_age"].get<int>();
            if (l.contains("compress") && l["compress"].is_boolean()) cfg.log.compress = l["compress"].get<bool>();
            if (has_and_string(l, "file_path")) cfg.log.file_path = l["file_path"].get<std::string>();
        }

        if (j.contains("tuning")) {
            auto t = j["tuning"];
            if (t.contains("worker_spawn_delay_ms") && t["worker_spawn_delay_ms"].is_number_integer())
                cfg.tuning.worker_spawn_delay_ms = t["worker_spawn_delay_ms"].get<int>();
        }
    } catch (const std::exception& ex) {
        out_err = std::string("Error extracting config fields: ") + ex.what();
        return std::nullopt;
    }

    // Basic validations
    if (cfg.rotom.worker_endpoint.empty()) {
        out_err = "rotom.worker_endpoint is required";
        return std::nullopt;
    }
    if (cfg.rotom.device_endpoint.empty()) {
        out_err = "rotom.device_endpoint is required";
        return std::nullopt;
    }
    if (cfg.general.device_name.empty()) {
        cfg.general.device_name = "zapdos-device";
    }

    return cfg;
}
