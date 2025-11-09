#ifndef XANITE_NATIVE_H
#define XANITE_NATIVE_H

#include <jni.h>
#include <cstdint>
#include <string>

namespace xanite {

enum class GameStatus {
    GAME_LOADED = 0,
    EMULATION_STARTING = 1,
    EMULATION_RUNNING = 2,
    EMULATION_PAUSED = 3,
    EMULATION_STOPPED = 4,
    EMULATION_ERROR = 5
};


JNIEnv* GetJNIEnv();
void DetachJNIEnv();
// 要进行正确的仿真~ 必须了解系统的各个组成部分•••
void SendToastMessage(const std::string& message);
void SendGameStatus(GameStatus status, const std::string& message = "");

} 

#endif 
