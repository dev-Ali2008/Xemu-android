#ifndef GAMETEXTINPUT_H_
#define GAMETEXTINPUT_H_

// Minimal stub for GameTextInput
// For Android builds, this provides basic compatibility
// Full GameTextInput functionality would require the actual library

#include <jni.h>
#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct GameTextInput GameTextInput;
typedef struct GameTextInputState GameTextInputState;

struct GameTextInput {
    void* userData;
};

struct GameTextInputState {
    char* text_UTF8;
    int32_t text_length;
    int32_t selection_start;
    int32_t selection_end;
    int32_t composing_region_start;
    int32_t composing_region_end;
};

GameTextInput* GameTextInput_init();
void GameTextInput_destroy(GameTextInput* input);
void GameTextInput_setState(GameTextInput* input, const GameTextInputState* state);
void GameTextInput_getState(GameTextInput* input, GameTextInputState* state);

#ifdef __cplusplus
}
#endif

#endif // GAMETEXTINPUT_H_
