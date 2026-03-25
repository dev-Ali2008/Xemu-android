#include "nv2a_vsh_emulator.h"

Nv2aVshParseResult nv2a_vsh_parse_program(Nv2aVshProgram *program,
                                         const uint32_t *code,
                                         uint32_t code_length)
{
    (void)program;
    (void)code;
    (void)code_length;
    return NV2AVPR_SUCCESS;
}

void nv2a_vsh_program_destroy(Nv2aVshProgram *program)
{
    (void)program;
}

Nv2aVshExecutionState nv2a_vsh_emu_initialize_xss_execution_state(
    Nv2aVshCPUXVSSExecutionState *linkage,
    float *constants)
{
    Nv2aVshExecutionState state;
    state.linkage = linkage;
    state.constants = constants;
    return state;
}

void nv2a_vsh_emu_execute_track_context_writes(
    Nv2aVshExecutionState *state,
    const Nv2aVshProgram *program,
    bool *constants_dirty)
{
    (void)state;
    (void)program;
    (void)constants_dirty;
}
