#pragma once
#include <string>
#include <optional>

// ==========================================================
// Estruturas de configuração completas
// ==========================================================

struct RotomConfig {
    std::string worker_endpoint;
    std::string device_endpoint;
    std::string secret;
    bool use_compression = true;
};

struct GeneralConfig {
    std::string device_name;
    int workers = 3;
    std::string dns_server;
    std::string scan_dir;
};

// 🔧 Adicione estas structs:
struct LogConfig {
    std::string level = "info";
    bool use_colors = true;
    bool log_to_file = false;
    int max_size = 10;
    int max_backups = 10;
    int max_age = 30;
    bool compress = true;
    std::string file_path = "/data/local/tmp/rotom-worker.log";
};

struct TuningConfig {
    int worker_spawn_delay_ms = 200;
};

// ==========================================================
// Estrutura principal
// ==========================================================
struct AppConfig {
    RotomConfig rotom;
    GeneralConfig general;
    LogConfig log;        
    TuningConfig tuning;  
};

// ==========================================================
// Funções
// ==========================================================
bool write_default_config_if_missing(const std::string &path);
std::optional<AppConfig> load_config_from_file(const std::string &path, std::string &err);
