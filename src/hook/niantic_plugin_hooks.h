// src/hook/niantic_plugin_hooks.h
#ifndef NIANTIC_PLUGIN_HOOKS_H
#define NIANTIC_PLUGIN_HOOKS_H

#include <jni.h>

bool initializeNianticPluginHooks();

// Funções para comunicação com Rotom
void sendToRotom(const std::string& data);
void handleGameData(const void* data, size_t size);

#endif // NIANTIC_PLUGIN_HOOKS_H