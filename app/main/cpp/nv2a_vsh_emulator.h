#ifndef NV2A_VSH_EMULATOR_H
#define NV2A_VSH_EMULATOR_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct Nv2aVshProgram {
    uint32_t placeholder;
} Nv2aVshProgram;

typedef enum Nv2aVshParseResult {
    NV2AVPR_SUCCESS = 0,
    NV2AVPR_ERROR = 1,
} Nv2aVshParseResult;

typedef struct Nv2aVshCPUXVSSExecutionState {
    float input_regs[4];
} Nv2aVshCPUXVSSExecutionState;

typedef struct Nv2aVshExecutionState {
    Nv2aVshCPUXVSSExecutionState *linkage;
    float *constants;
} Nv2aVshExecutionState;

Nv2aVshParseResult nv2a_vsh_parse_program(Nv2aVshProgram *program,
                                          const uint32_t *code,
                                          uint32_t code_length);

void nv2a_vsh_program_destroy(Nv2aVshProgram *program);

Nv2aVshExecutionState nv2a_vsh_emu_initialize_xss_execution_state(
    Nv2aVshCPUXVSSExecutionState *linkage,
    float *constants);

void nv2a_vsh_emu_execute_track_context_writes(
    Nv2aVshExecutionState *state,
    const Nv2aVshProgram *program,
    bool *constants_dirty);

#ifdef __cplusplus
}
#endif

#endif
