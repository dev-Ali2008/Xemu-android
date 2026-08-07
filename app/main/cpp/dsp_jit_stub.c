#include "qemu/osdep.h"
#include "hw/xbox/mcpx/apu/dsp/dsp_internal.h"

const DSPOps jit_dsp_ops = { 0 };

void dsp_jit_init(DSPState *dsp)
{
    dsp_c_init(dsp);
}
