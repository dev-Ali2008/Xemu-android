#ifndef SAMPLERATE_H
#define SAMPLERATE_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SRC_STATE SRC_STATE;

typedef long (*src_callback_t)(void *cb_data, float **data);

enum {
    SRC_SINC_FASTEST = 2,
    SRC_LINEAR = 4,
};

SRC_STATE *src_callback_new(src_callback_t cb, int converter_type, int channels,
                            int *error, void *cb_data);
long src_callback_read(SRC_STATE *state, double ratio, long frames, float *data);
SRC_STATE *src_delete(SRC_STATE *state);
int src_reset(SRC_STATE *state);
const char *src_strerror(int error);

void src_float_to_short_array(const float *in, short *out, int len);

#ifdef __cplusplus
}
#endif

#endif /* SAMPLERATE_H */
