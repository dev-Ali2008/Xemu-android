#ifndef GAME_ACTIVITY_H_
#define GAME_ACTIVITY_H_

// Minimal stub for GameActivity
// For Android builds, this provides basic compatibility
// Full GameActivity functionality would require the actual library

#include <jni.h>
#include <android/native_activity.h>
#include <android/input.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GameActivity GameActivity;
typedef struct GameActivityKeyEvent GameActivityKeyEvent;
typedef struct GameActivityMotionEvent GameActivityMotionEvent;

struct GameActivity {
    ANativeActivity* activity;
    void* userData;
};

struct GameActivityKeyEvent {
    int32_t action;
    int32_t keyCode;
    int64_t eventTime;
};

struct GameActivityMotionEvent {
    int32_t action;
    int64_t eventTime;
    float x;
    float y;
};

#ifdef __cplusplus
}
#endif

#endif // GAME_ACTIVITY_H_
