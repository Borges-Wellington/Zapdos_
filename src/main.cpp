// src/main.cpp
#include <jni.h>
#include <string>
#include <thread>
#include <atomic>
#include <chrono>
#include <csignal>
#include <unordered_map>
#include <vector>

#include "hook/hook_manager.h"
#include "rotom/rotom_client.h"
#include "config/config_manager.h"
#include "data/data_processor.h"
#include "logging.h"
#include "utils/android_utils.h"
#include "utils/file_utils.h"
#include "version.h"

// Variáveis globais
std::atomic<bool> g_initialized{false};
std::atomic<bool> g_running{false};
std::atomic<bool> g_shutdown_requested{false};
std::thread g_monitor_thread;
std::thread g_data_processor_thread;

// Estatísticas do sistema
struct SystemStats {
    std::atomic<uint64_t> packets_captured{0};
    std::atomic<uint64_t> data_sent{0};
    std::atomic<uint64_t> errors{0};
    std::atomic<uint64_t> hooks_active{0};
};

SystemStats g_system_stats;

// Manipulador de sinais para graceful shutdown
void signal_handler(int signal) {
    LOGI("Sinal %d recebido, desligando sistema...", signal);
    g_shutdown_requested.store(true);
}

// Processador de dados em background
void data_processor_worker() {
    LOGI("Thread processadora de dados iniciada");
    
    DataProcessor& processor = DataProcessor::getInstance();
    
    while (g_running.load()) {
        try {
            processor.processPendingData();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        } catch (const std::exception& e) {
            LOGE("Erro no processador de dados: %s", e.what());
            g_system_stats.errors++;
        }
    }
    
    LOGI("Thread processadora de dados finalizada");
}

// Monitoramento do sistema
void monitor_application() {
    LOGI("Thread de monitoramento iniciada");
    
    auto last_stats_time = std::chrono::steady_clock::now();
    auto last_health_check = std::chrono::steady_clock::now();
    
    while (g_running.load() && !g_shutdown_requested.load()) {
        try {
            auto now = std::chrono::steady_clock::now();
            
            // Verificar saúde do Rotom a cada 10 segundos
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_health_check).count() >= 10) {
                RotomClient& rotom = RotomClient::getInstance();
                if (g_initialized.load() && !rotom.isConnected()) {
                    LOGW("Rotom desconectado, tentando reconectar...");
                    rotom.shutdown();
                    std::this_thread::sleep_for(std::chrono::seconds(2));
                    
                    if (rotom.initialize()) {
                        LOGI("Rotom reconectado com sucesso");
                    } else {
                        LOGE("Falha ao reconectar Rotom");
                        g_system_stats.errors++;
                    }
                }
                last_health_check = now;
            }
            
            // Log de estatísticas a cada 30 segundos
            if (std::chrono::duration_cast<std::chrono::seconds>(now - last_stats_time).count() >= 30) {
                LOGI("Estatísticas do Sistema - Pacotes: %llu, Dados: %llu bytes, Erros: %llu, Hooks: %llu",
                     g_system_stats.packets_captured.load(),
                     g_system_stats.data_sent.load(),
                     g_system_stats.errors.load(),
                     g_system_stats.hooks_active.load());
                
                // Verificar uso de recursos
                if (FileUtils::getDiskUsage() > 90) {
                    LOGW("Uso de disco elevado, considerando limpeza de cache");
                }
                
                last_stats_time = now;
            }
            
            std::this_thread::sleep_for(std::chrono::seconds(1));
            
        } catch (const std::exception& e) {
            LOGE("Erro no monitor: %s", e.what());
            g_system_stats.errors++;
            std::this_thread::sleep_for(std::chrono::seconds(5));
        }
    }
    
    LOGI("Thread de monitoramento finalizada");
}

// Inicialização do sistema
bool initialize_system() {
    if (g_initialized.load()) {
        LOGW("Sistema já inicializado");
        return true;
    }
    
    LOGI("=== INICIALIZANDO SISTEMA POGO-ROTOM v%s ===", PROJECT_VERSION);
    
    // Configurar manipulador de sinais
    std::signal(SIGTERM, signal_handler);
    std::signal(SIGINT, signal_handler);
    
    try {
        // 1. Inicializar configurações
        LOGI("Passo 1: Carregando configurações...");
        ConfigManager& config = ConfigManager::getInstance();
        if (!config.loadConfig()) {
            LOGW("Usando configurações padrão");
            config.setDefaults();
        }
        
        LOGI("Configurações: Rotom=%s:%d, Timeout=%dms, Buffer=%dKB",
             config.getRotomHost().c_str(),
             config.getRotomPort(),
             config.getConnectionTimeout(),
             config.getBufferSize() / 1024);
        
        // 2. Inicializar utilitários
        LOGI("Passo 2: Inicializando utilitários...");
        if (!FileUtils::initializeWorkspace()) {
            LOGE("Falha ao inicializar workspace");
            return false;
        }
        
        // 3. Inicializar processador de dados
        LOGI("Passo 3: Inicializando processador de dados...");
        DataProcessor& processor = DataProcessor::getInstance();
        if (!processor.initialize()) {
            LOGE("Falha ao inicializar processador de dados");
            return false;
        }
        
        // 4. Inicializar cliente Rotom
        LOGI("Passo 4: Inicializando cliente Rotom...");
        RotomClient& rotom = RotomClient::getInstance();
        if (!rotom.initialize()) {
            LOGE("Falha ao inicializar Rotom client");
            return false;
        }
        
        // 5. Aguardar conexão com Rotom
        LOGI("Passo 5: Estabelecendo conexão com Rotom...");
        int connection_attempts = 0;
        const int max_attempts = config.getMaxConnectionAttempts();
        const int retry_delay = config.getRetryDelay();
        
        while (connection_attempts < max_attempts && g_running.load()) {
            if (rotom.isConnected()) {
                LOGI("Conexão com Rotom estabelecida (tentativa %d/%d)",
                     connection_attempts + 1, max_attempts);
                break;
            }
            
            if (connection_attempts > 0) {
                LOGI("Aguardando conexão com Rotom... (%d/%d)",
                     connection_attempts + 1, max_attempts);
            }
            
            std::this_thread::sleep_for(std::chrono::milliseconds(retry_delay));
            connection_attempts++;
        }
        
        if (!rotom.isConnected()) {
            LOGE("Não foi possível estabelecer conexão com Rotom após %d tentativas", max_attempts);
            rotom.shutdown();
            return false;
        }
        
        // 6. Inicializar sistema de hooks
        LOGI("Passo 6: Inicializando sistema de hooks...");
        HookManager& hookManager = HookManager::getInstance();
        if (!hookManager.initialize()) {
            LOGE("Falha ao inicializar HookManager");
            rotom.shutdown();
            return false;
        }
        
        // 7. Iniciar threads de background
        LOGI("Passo 7: Iniciando threads de background...");
        g_running.store(true);
        
        g_monitor_thread = std::thread(monitor_application);
        g_data_processor_thread = std::thread(data_processor_worker);
        
        // Configurar política de prioridade das threads (Android)
        AndroidUtils::setThreadPriority(g_monitor_thread.native_handle(), ANDROID_PRIORITY_BACKGROUND);
        AndroidUtils::setThreadPriority(g_data_processor_thread.native_handle(), ANDROID_PRIORITY_DEFAULT);
        
        g_initialized.store(true);
        g_system_stats.hooks_active.store(2); // libart + libNianticLabsPlugin
        
        LOGI("=== SISTEMA INICIALIZADO COM SUCESSO ===");
        LOGI("Componentes ativos:");
        LOGI("  - Hooks: libNianticLabsPlugin.so, libart.so");
        LOGI("  - Conexão: %s:%d", config.getRotomHost().c_str(), config.getRotomPort());
        LOGI("  - Processamento: %d threads ativas", 2); // monitor + data_processor
        LOGI("  - Buffer: %d KB", config.getBufferSize() / 1024);
        
        return true;
        
    } catch (const std::exception& e) {
        LOGE("Exceção durante inicialização: %s", e.what());
        shutdown_system();
        return false;
    }
}

// Desligamento graceful do sistema
void shutdown_system() {
    if (!g_initialized.load()) {
        LOGW("Sistema não estava inicializado");
        return;
    }
    
    LOGI("=== INICIANDO DESLIGAMENTO GRACEFUL DO SISTEMA ===");
    
    g_running.store(false);
    g_initialized.store(false);
    
    try {
        // 1. Parar threads de background
        LOGI("Parando threads de background...");
        
        if (g_data_processor_thread.joinable()) {
            g_data_processor_thread.join();
            LOGI("Thread processadora de dados parada");
        }
        
        if (g_monitor_thread.joinable()) {
            g_monitor_thread.join();
            LOGI("Thread de monitoramento parada");
        }
        
        // 2. Processar dados pendentes
        LOGI("Processando dados pendentes...");
        DataProcessor::getInstance().processPendingData();
        
        // 3. Desligar hooks
        LOGI("Desligando sistema de hooks...");
        HookManager::getInstance().shutdown();
        
        // 4. Desligar Rotom
        LOGI("Desligando cliente Rotom...");
        RotomClient::getInstance().shutdown();
        
        // 5. Desligar processador de dados
        LOGI("Desligando processador de dados...");
        DataProcessor::getInstance().shutdown();
        
        // 6. Limpar recursos
        LOGI("Limpando recursos...");
        FileUtils::cleanupTempFiles();
        
        // Resetar estatísticas
        g_system_stats = SystemStats{};
        
        LOGI("=== SISTEMA DESLIGADO COM SUCESSO ===");
        
    } catch (const std::exception& e) {
        LOGE("Erro durante shutdown: %s", e.what());
    }
}

// Funções de status e informações
std::string get_system_status() {
    RotomClient& rotom = RotomClient::getInstance();
    ConfigManager& config = ConfigManager::getInstance();
    
    char buffer[1024];
    snprintf(buffer, sizeof(buffer),
             "=== STATUS DO SISTEMA POGO-ROTOM v%s ===\n"
             "Inicializado: %s\n"
             "Rotom Conectado: %s\n"
             "Hooks Ativos: %llu\n"
             "Pacotes Capturados: %llu\n"
             "Dados Enviados: %llu bytes\n"
             "Erros: %llu\n"
             "Endereço Rotom: %s:%d\n"
             "Tempo de Atividade: %lld segundos\n"
             "Uso de Disco: %.1f%%",
             PROJECT_VERSION,
             g_initialized ? "SIM" : "NÃO",
             rotom.isConnected() ? "SIM" : "NÃO",
             g_system_stats.hooks_active.load(),
             g_system_stats.packets_captured.load(),
             g_system_stats.data_sent.load(),
             g_system_stats.errors.load(),
             config.getRotomHost().c_str(),
             config.getRotomPort(),
             static_cast<long long>(0), // TODO: Implementar tracking de tempo
             FileUtils::getDiskUsage());
    
    return std::string(buffer);
}

std::string get_detailed_stats() {
    char buffer[512];
    snprintf(buffer, sizeof(buffer),
             "=== ESTATÍSTICAS DETALHADAS ===\n"
             "Pacotes Capturados: %llu\n"
             "Dados Enviados: %llu bytes\n"
             "Erros: %llu\n"
             "Hooks Ativos: %llu\n"
             "Taxa de Sucesso: %.2f%%",
             g_system_stats.packets_captured.load(),
             g_system_stats.data_sent.load(),
             g_system_stats.errors.load(),
             g_system_stats.hooks_active.load(),
             g_system_stats.packets_captured > 0 ?
                 (100.0 - (g_system_stats.errors * 100.0 / g_system_stats.packets_captured)) : 100.0);
    
    return std::string(buffer);
}

// Callback para dados capturados pelos hooks
extern "C" void on_data_captured(const char* data, size_t size, const char* source) {
    if (!g_initialized.load() || !data || size == 0) {
        return;
    }
    
    g_system_stats.packets_captured++;
    g_system_stats.data_sent += size;
    
    // Encaminhar para processamento
    DataProcessor& processor = DataProcessor::getInstance();
    processor.queueData(data, size, source);
    
    LOGD("Dados capturados de %s: %zu bytes", source, size);
}

// JNI Methods
extern "C" {

// Método principal de inicialização
JNIEXPORT jboolean JNICALL
Java_com_yourpackage_pogorotom_MainActivity_initializeSystem(
    JNIEnv* env,
    jobject thiz) {
    
    LOGI("Chamada Java: initializeSystem");
    
    if (!AndroidUtils::isTargetProcess()) {
        LOGW("Não é o processo do Pokémon GO, ignorando inicialização");
        return JNI_FALSE;
    }
    
    bool success = initialize_system();
    return success ? JNI_TRUE : JNI_FALSE;
}

// Método para desligar o sistema
JNIEXPORT void JNICALL
Java_com_yourpackage_pogorotom_MainActivity_shutdownSystem(
    JNIEnv* env,
    jobject thiz) {
    
    LOGI("Chamada Java: shutdownSystem");
    shutdown_system();
}

// Método para obter status
JNIEXPORT jstring JNICALL
Java_com_yourpackage_pogorotom_MainActivity_getSystemStatus(
    JNIEnv* env,
    jobject thiz) {
    
    std::string status = get_system_status();
    return env->NewStringUTF(status.c_str());
}

// Método para obter estatísticas detalhadas
JNIEXPORT jstring JNICALL
Java_com_yourpackage_pogorotom_MainActivity_getDetailedStats(
    JNIEnv* env,
    jobject thiz) {
    
    std::string stats = get_detailed_stats();
    return env->NewStringUTF(stats.c_str());
}

// Método para verificar se está inicializado
JNIEXPORT jboolean JNICALL
Java_com_yourpackage_pogorotom_MainActivity_isSystemInitialized(
    JNIEnv* env,
    jobject thiz) {
    
    return g_initialized.load() ? JNI_TRUE : JNI_FALSE;
}

// Método para reinicializar o sistema
JNIEXPORT jboolean JNICALL
Java_com_yourpackage_pogorotom_MainActivity_restartSystem(
    JNIEnv* env,
    jobject thiz) {
    
    LOGI("Chamada Java: restartSystem");
    
    if (g_initialized.load()) {
        shutdown_system();
        std::this_thread::sleep_for(std::chrono::seconds(2));
    }
    
    bool success = initialize_system();
    return success ? JNI_TRUE : JNI_FALSE;
}

// Método para enviar comando customizado para Rotom
JNIEXPORT jboolean JNICALL
Java_com_yourpackage_pogorotom_MainActivity_sendCommandToRotom(
    JNIEnv* env,
    jobject thiz,
    jstring command) {
    
    if (!g_initialized.load()) {
        LOGE("Sistema não inicializado");
        return JNI_FALSE;
    }
    
    const char* cmd_str = env->GetStringUTFChars(command, nullptr);
    if (!cmd_str) {
        LOGE("Comando inválido");
        return JNI_FALSE;
    }
    
    LOGI("Enviando comando para Rotom: %s", cmd_str);
    
    RotomClient& rotom = RotomClient::getInstance();
    bool success = rotom.sendCustomCommand(cmd_str);
    
    env->ReleaseStringUTFChars(command, cmd_str);
    return success ? JNI_TRUE : JNI_FALSE;
}

// Método para atualizar configurações
JNIEXPORT jboolean JNICALL
Java_com_yourpackage_pogorotom_MainActivity_updateConfig(
    JNIEnv* env,
    jobject thiz,
    jstring host,
    jint port) {
    
    const char* host_str = env->GetStringUTFChars(host, nullptr);
    if (!host_str) {
        return JNI_FALSE;
    }
    
    ConfigManager& config = ConfigManager::getInstance();
    config.setRotomHost(host_str);
    config.setRotomPort(port);
    config.saveConfig();
    
    env->ReleaseStringUTFChars(host, host_str);
    
    LOGI("Configurações atualizadas: %s:%d", host_str, port);
    return JNI_TRUE;
}

// Método nativo para debug
JNIEXPORT void JNICALL
Java_com_yourpackage_pogorotom_MainActivity_nativeDebug(
    JNIEnv* env,
    jobject thiz,
    jstring message) {
    
    const char* msg_str = env->GetStringUTFChars(message, nullptr);
    if (msg_str) {
        LOGD("[DEBUG-JAVA] %s", msg_str);
        env->ReleaseStringUTFChars(message, msg_str);
    }
}

// Método para limpar estatísticas
JNIEXPORT void JNICALL
Java_com_yourpackage_pogorotom_MainActivity_clearStats(
    JNIEnv* env,
    jobject thiz) {
    
    g_system_stats = SystemStats{};
    LOGI("Estatísticas limpas");
}

JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnLoad chamado - PogoRotom Library v%s", PROJECT_VERSION);
    
    JNIEnv* env;
    if (vm->GetEnv(reinterpret_cast<void**>(&env), JNI_VERSION_1_6) != JNI_OK) {
        return JNI_ERR;
    }
    
    // Inicializar utilitários Android
    AndroidUtils::initialize(vm);
    
    if (AndroidUtils::isTargetProcess()) {
        LOGI("Carregado no processo do Pokémon GO - Pronto para inicialização");
        
        // Inicialização automática opcional
        if (ConfigManager::getInstance().isAutoStartEnabled()) {
            std::thread([]() {
                std::this_thread::sleep_for(std::chrono::seconds(8));
                if (!g_initialized.load()) {
                    LOGI("Inicialização automática ativada...");
                    initialize_system();
                }
            }).detach();
        }
    } else {
        LOGI("Carregado em processo diferente - Modo standby");
    }
    
    return JNI_VERSION_1_6;
}

JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* vm, void* reserved) {
    LOGI("JNI_OnUnload chamado - Limpando recursos");
    
    if (g_initialized.load()) {
        LOGW("Sistema ainda inicializado durante unload, forçando shutdown");
        shutdown_system();
    }
    
    AndroidUtils::shutdown();
}

} // extern "C"

// Ponto de entrada para inicialização via código nativo
extern "C" void initialize_pogo_rotom() {
    LOGI("Inicialização via código nativo");
    
    std::thread([]() {
        std::this_thread::sleep_for(std::chrono::seconds(8));
        
        if (!g_initialized.load()) {
            LOGI("Tentando inicialização automática...");
            initialize_system();
        }
    }).detach();
}

// Função para teste direto (quando compilado como executável)
#ifndef ANDROID
int main(int argc, char** argv) {
    LOGI("Modo standalone - PogoRotom System v%s", PROJECT_VERSION);
    
    // Para testes em desktop
    ConfigManager::getInstance().setRotomHost("localhost");
    ConfigManager::getInstance().setRotomPort(8080);
    
    if (initialize_system()) {
        LOGI("Sistema inicializado - Pressione Ctrl+C para sair...");
        
        // Aguardar sinal de shutdown
        while (!g_shutdown_requested.load()) {
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
        
        shutdown_system();
    } else {
        LOGE("Falha na inicialização");
        return 1;
    }
    
    return 0;
}
#endif