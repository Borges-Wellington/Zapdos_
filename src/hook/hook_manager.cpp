// src/hook/hook_manager.cpp
#include "hook_manager.h"
#include "frida-gum.h"
#include "niantic_plugin_hooks.h"
#include "libart_hooks.h"

HookManager& HookManager::getInstance() {
    static HookManager instance;
    return instance;
}

bool HookManager::initialize() {
    LOGI("Inicializando HookManager...");
    
    // Inicializar Frida Gum
    gum_init_embedded();
    
    // Carregar e hookar as bibliotecas
    if (!hookLibraries()) {
        LOGE("Falha ao hookar bibliotecas");
        return false;
    }
    
    LOGI("HookManager inicializado com sucesso");
    return true;
}

void HookManager::shutdown() {
    LOGI("Desligando HookManager...");
    gum_deinit_embedded();
}

bool HookManager::hookLibraries() {
    bool success = true;
    
    // Hook libNianticLabsPlugin.so
    if (!hookNianticPlugin()) {
        LOGE("Falha ao hookar libNianticLabsPlugin.so");
        success = false;
    }
    
    // Hook libart.so
    if (!hookLibArt()) {
        LOGE("Falha ao hookar libart.so");
        success = false;
    }
    
    return success;
}

bool HookManager::hookNianticPlugin() {
    LOGI("Hookando libNianticLabsPlugin.so...");
    
    // Carregar a biblioteca
    void* nianticLib = dlopen("libNianticLabsPlugin.so", RTLD_LAZY);
    if (!nianticLib) {
        LOGE("Não foi possível carregar libNianticLabsPlugin.so: %s", dlerror());
        return false;
    }
    
    loadedLibraries["libNianticLabsPlugin.so"] = nianticLib;
    
    // Inicializar hooks específicos do Niantic Plugin
    return initializeNianticPluginHooks();
}

bool HookManager::hookLibArt() {
    LOGI("Hookando libart.so...");
    
    // Carregar a biblioteca
    void* artLib = dlopen("libart.so", RTLD_LAZY);
    if (!artLib) {
        LOGE("Não foi possível carregar libart.so: %s", dlerror());
        return false;
    }
    
    loadedLibraries["libart.so"] = artLib;
    
    // Inicializar hooks específicos do ART
    return initializeLibArtHooks();
}

void HookManager::addFunctionHook(const std::string& library, const std::string& function, void* hookFunction, void** originalFunction) {
    GumInterceptor* interceptor = gum_interceptor_obtain();
    
    // Encontrar o símbolo da função
    void* target_function = dlsym(loadedLibraries[library], function.c_str());
    if (!target_function) {
        LOGE("Função %s não encontrada em %s", function.c_str(), library.c_str());
        return;
    }
    
    // Aplicar o hook
    gum_interceptor_begin_transaction(interceptor);
    gum_interceptor_replace(interceptor, target_function, hookFunction, originalFunction);
    gum_interceptor_end_transaction(interceptor);
    
    LOGI("Hook aplicado em %s::%s", library.c_str(), function.c_str());
}