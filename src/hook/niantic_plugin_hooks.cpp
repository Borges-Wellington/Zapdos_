// src/hook/niantic_plugin_hooks.cpp
#include "niantic_plugin_hooks.h"
#include "../rotom/rotom_client.h"
#include "../proto/pogo_proto_parser.h"
#include "hook_manager.h"

// Funções originais
static void* (*original_NetworkManager_SendRequest)(void* manager, void* request) = nullptr;
static void* (*original_NetworkManager_HandleResponse)(void* manager, void* response) = nullptr;

// Hook para enviar requisições
void* hook_NetworkManager_SendRequest(void* manager, void* request) {
    LOGI("Hook: NetworkManager_SendRequest chamado");
    
    // Parse e processar a requisição
    if (request) {
        std::string requestData = PogoProtoParser::parseRequest(request);
        if (!requestData.empty()) {
            sendToRotom(requestData);
        }
    }
    
    // Chamar função original
    if (original_NetworkManager_SendRequest) {
        return original_NetworkManager_SendRequest(manager, request);
    }
    
    return nullptr;
}

// Hook para receber respostas
void* hook_NetworkManager_HandleResponse(void* manager, void* response) {
    LOGI("Hook: NetworkManager_HandleResponse chamado");
    
    // Parse e processar a resposta
    if (response) {
        std::string responseData = PogoProtoParser::parseResponse(response);
        if (!responseData.empty()) {
            sendToRotom(responseData);
        }
    }
    
    // Chamar função original
    if (original_NetworkManager_HandleResponse) {
        return original_NetworkManager_HandleResponse(manager, response);
    }
    
    return nullptr;
}

bool initializeNianticPluginHooks() {
    LOGI("Inicializando hooks do Niantic Plugin...");
    
    HookManager& hookManager = HookManager::getInstance();
    
    // Hook das principais funções de rede
    hookManager.addFunctionHook(
        "libNianticLabsPlugin.so",
        "_ZN17NetworkManager11SendRequestEPv",
        (void*)hook_NetworkManager_SendRequest,
        (void**)&original_NetworkManager_SendRequest
    );
    
    hookManager.addFunctionHook(
        "libNianticLabsPlugin.so", 
        "_ZN17NetworkManager13HandleResponseEPv",
        (void*)hook_NetworkManager_HandleResponse,
        (void**)&original_NetworkManager_HandleResponse
    );
    
    // Adicionar mais hooks conforme necessário
    // hookManager.addFunctionHook(...);
    
    LOGI("Hooks do Niantic Plugin inicializados");
    return true;
}

void sendToRotom(const std::string& data) {
    // Enviar dados para o Rotom
    RotomClient& rotom = RotomClient::getInstance();
    rotom.sendRawData(data);
    
    LOGD("Dados enviados para Rotom: %zu bytes", data.size());
}

void handleGameData(const void* data, size_t size) {
    if (!data || size == 0) return;
    
    // Processar dados brutos do jogo
    std::string gameData(static_cast<const char*>(data), size);
    sendToRotom(gameData);
    
    LOGD("Dados do jogo processados: %zu bytes", size);
}