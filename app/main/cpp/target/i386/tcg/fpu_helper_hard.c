/*
 * On AArch64, the hard FPU helper path currently uses native double
 * precision and does not correctly emulate x87 extended-precision
 * compare/equality semantics. Disable the hard-FPU variant there to
 * avoid equality bugs in games like Fable: The Lost Chapters on Android.
 */
#if defined(XBOX) && defined(__x86_64__)
#define USE_HARD_FPU 1
#include "fpu_helper.c"
#endif
