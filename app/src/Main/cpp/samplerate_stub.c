#include <stdlib.h>
#include <string.h>

#include "samplerate.h"

/*
 * Minimal libsamplerate replacement for Android.
 *
 * Uses linear interpolation for sample-rate conversion.  Quality is lower
 * than the sinc resampler used on desktop, but the ratio is correctly
 * applied so voices recorded at rates other than 48 kHz (e.g. 22050 Hz
 * dialogue in Halo CE) play at the right pitch instead of chipmunk-fast.
 *
 * The original stub did:  (void)ratio;  — i.e. passed samples through
 * unchanged regardless of the conversion ratio, causing every sub-48 kHz
 * voice to play back at 48000/source_rate times normal speed.
 */

struct SRC_STATE {
    src_callback_t cb;
    void          *cb_data;
    int            channels;

    /* Current input block (pointer owned by the callback, not by us). */
    float *buf;
    long   buf_len; /* frames in buf */

    /* Fractional read position within buf (advances by 1/ratio per output frame). */
    double buf_pos;
};

SRC_STATE *src_callback_new(src_callback_t cb, int converter_type, int channels,
                            int *error, void *cb_data)
{
    (void)converter_type;
    if (error) {
        *error = 0;
    }
    SRC_STATE *state = (SRC_STATE *)calloc(1, sizeof(*state));
    if (!state) {
        if (error) {
            *error = -1;
        }
        return NULL;
    }
    state->cb       = cb;
    state->cb_data  = cb_data;
    state->channels = channels;
    state->buf      = NULL;
    state->buf_len  = 0;
    state->buf_pos  = 0.0;
    return state;
}

/*
 * ratio = output_sample_rate / input_sample_rate
 *   > 1 : upsampling  (e.g. 22050 -> 48000, ratio ≈ 2.177)
 *   < 1 : downsampling
 *   = 1 : pass-through (still goes through the interpolator for simplicity)
 *
 * step = 1/ratio = input frames consumed per output frame produced.
 */
long src_callback_read(SRC_STATE *state, double ratio, long frames, float *data)
{
    if (!state || !state->cb || !data || frames <= 0 || ratio <= 0.0) {
        return 0;
    }

    const double step = 1.0 / ratio;
    long out = 0;

    while (out < frames) {
        long idx = (long)state->buf_pos;

        /* Refill the input buffer when we have consumed it. */
        if (state->buf == NULL || idx >= state->buf_len) {
            /*
             * Carry the fractional overshoot past the end of the old block
             * into the start of the new block.  This preserves continuity
             * when ratio > 1 (step < 1) and we drain the buffer gradually.
             */
            double carry = (state->buf_len > 0)
                               ? state->buf_pos - (double)state->buf_len
                               : 0.0;
            if (carry < 0.0) {
                carry = 0.0;
            }

            float *new_buf = NULL;
            long   got     = state->cb(state->cb_data, &new_buf);
            if (got <= 0 || new_buf == NULL) {
                break; /* source exhausted */
            }

            state->buf     = new_buf;
            state->buf_len = got;
            state->buf_pos = carry;
            idx            = (long)state->buf_pos;

            if (idx >= state->buf_len) {
                break; /* carry >= new block length — shouldn't happen */
            }
        }

        /* Linear interpolation between sample[idx] and sample[idx+1]. */
        float  alpha    = (float)(state->buf_pos - (double)idx);
        long   next_idx = idx + 1;

        for (int ch = 0; ch < state->channels; ch++) {
            float s0 = state->buf[idx * state->channels + ch];
            float s1 = (next_idx < state->buf_len)
                           ? state->buf[next_idx * state->channels + ch]
                           : s0; /* hold last sample at block boundary */
            data[out * state->channels + ch] = s0 + alpha * (s1 - s0);
        }

        state->buf_pos += step;
        out++;
    }

    return out;
}

int src_reset(SRC_STATE *state)
{
    if (state) {
        state->buf     = NULL;
        state->buf_len = 0;
        state->buf_pos = 0.0;
    }
    return 0;
}

const char *src_strerror(int error)
{
    (void)error;
    return "libsamplerate stub (linear)";
}

void src_float_to_short_array(const float *in, short *out, int len)
{
    if (!in || !out || len <= 0) {
        return;
    }
    for (int i = 0; i < len; ++i) {
        float v = in[i];
        if (v > 1.0f)       v = 1.0f;
        else if (v < -1.0f) v = -1.0f;
        out[i] = (short)(v * 32767.0f);
    }
}
