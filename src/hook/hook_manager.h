// src/hook/hook_manager.h
#ifndef HOOK_MANAGER_H
#define HOOK_MANAGER_H

#include <jni.h>
#include <string>
#include <functional>
#include <map>
#include "logging.h"

class HookManager {
public:
    static HookManager& getInstance();
    
    bool initialize();
    void shutdown();
    
    // Métodos para adicionar hooks
    void addFunctionHook(const std::string& library, const std::string& function, void* hookFunction, void** originalFunction);
    void addMethodHook(JNIEnv* env, const char* className, const char* methodName, const char* signature, void* hookFunction, void** originalFunction);
    
private:
    HookManager() = default;
    ~HookManager() = default;
    
    bool hookLibraries();
    bool hookNianticPlugin();
    bool hookLibArt();
    
    std::map<std::string, void*> loadedLibraries;
};

#endif // HOOK_MANAGER_H