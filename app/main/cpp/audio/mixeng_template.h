/*
 * QEMU Mixing engine
 *
 * Copyright (c) 2004-2005 Vassili Karpov (malc)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

/*
 * Tusen tack till Mike Nordell
 * dec++'ified by Dscho
 */

#ifndef SIGNED
#define HALF (IN_MAX >> 1)
#endif

#define ET glue (ENDIAN_CONVERSION, glue (glue (glue (_, ITYPE), BSIZE), _t))
#define IN_T glue (glue (ITYPE, BSIZE), _t)

#ifdef FLOAT_MIXENG
static inline mixeng_real glue (conv_, ET) (IN_T v)
{
    IN_T nv = ENDIAN_CONVERT (v);

#ifdef RECIPROCAL
#ifdef SIGNED
    return nv * (2.f / ((mixeng_real)IN_MAX - IN_MIN));
#else
    return (nv - HALF) * (2.f / (mixeng_real)IN_MAX);
#endif
#else  /* !RECIPROCAL */
#ifdef SIGNED
    return nv / (((mixeng_real)IN_MAX - IN_MIN) / 2.f);
#else
    return (nv - HALF) / ((mixeng_real)IN_MAX / 2.f);
#endif
#endif
}

static inline IN_T glue (clip_, ET) (mixeng_real v)
{
    if (v >= 1.f) {
        return IN_MAX;
    } else if (v < -1.f) {
        return IN_MIN;
    }

#ifdef SIGNED
    return ENDIAN_CONVERT((IN_T)(v * (((mixeng_real)IN_MAX - IN_MIN) / 2.f)));
#else
    return ENDIAN_CONVERT((IN_T)((v * ((mixeng_real)IN_MAX / 2.f)) + HALF));
#endif
}

#else  /* !FLOAT_MIXENG */

static inline int64_t glue (conv_, ET) (IN_T v)
{
    IN_T nv = ENDIAN_CONVERT (v);
#ifdef SIGNED
    return ((int64_t) nv) << (32 - SHIFT);
#else
    return ((int64_t) nv - HALF) << (32 - SHIFT);
#endif
}

static inline IN_T glue (clip_, ET) (int64_t v)
{
    if (v >= 0x7fffffffLL) {
        return IN_MAX;
    } else if (v < -2147483648LL) {
        return IN_MIN;
    }

#ifdef SIGNED
    return ENDIAN_CONVERT ((IN_T) (v >> (32 - SHIFT)));
#else
    return ENDIAN_CONVERT ((IN_T) ((v >> (32 - SHIFT)) + HALF));
#endif
}
#endif

static void glue (glue (conv_, ET), _to_stereo)
    (struct st_sample *__restrict dst, const void *__restrict src, int samples)
{
    struct st_sample *__restrict out = dst;
    IN_T *__restrict in = (IN_T *) src;
    int count = samples;

    while (count >= 4) {
        out[0].l = glue (conv_, ET) (in[0]);
        out[0].r = glue (conv_, ET) (in[1]);
        out[1].l = glue (conv_, ET) (in[2]);
        out[1].r = glue (conv_, ET) (in[3]);
        out[2].l = glue (conv_, ET) (in[4]);
        out[2].r = glue (conv_, ET) (in[5]);
        out[3].l = glue (conv_, ET) (in[6]);
        out[3].r = glue (conv_, ET) (in[7]);
        out += 4; in += 8; count -= 4;
    }
    while (count--) {
        out->l = glue (conv_, ET) (in[0]);
        out->r = glue (conv_, ET) (in[1]);
        out += 1; in += 2;
    }
}

static void glue (glue (conv_, ET), _to_mono)
    (struct st_sample *__restrict dst, const void *__restrict src, int samples)
{
    struct st_sample *__restrict out = dst;
    IN_T *__restrict in = (IN_T *) src;
    int count = samples;

    while (count--) {
        out->l = glue (conv_, ET) (*in);
        out->r = out->l;
        out += 1; in += 1;
    }
}

static void glue (glue (clip_, ET), _from_stereo)
    (void *__restrict dst, const struct st_sample *__restrict src, int samples)
{
    const struct st_sample *__restrict in = src;
    IN_T *__restrict out = (IN_T *) dst;
    int count = samples;
    while (count >= 4) {
        out[0] = glue (clip_, ET) (in[0].l);
        out[1] = glue (clip_, ET) (in[0].r);
        out[2] = glue (clip_, ET) (in[1].l);
        out[3] = glue (clip_, ET) (in[1].r);
        out[4] = glue (clip_, ET) (in[2].l);
        out[5] = glue (clip_, ET) (in[2].r);
        out[6] = glue (clip_, ET) (in[3].l);
        out[7] = glue (clip_, ET) (in[3].r);
        out += 8; in += 4; count -= 4;
    }
    while (count--) {
        out[0] = glue (clip_, ET) (in->l);
        out[1] = glue (clip_, ET) (in->r);
        out += 2; in += 1;
    }
}

static void glue (glue (clip_, ET), _from_mono)
    (void *__restrict dst, const struct st_sample *__restrict src, int samples)
{
    const struct st_sample *__restrict in = src;
    IN_T *__restrict out = (IN_T *) dst;
    int count = samples;
    while (count--) {
        *out++ = glue (clip_, ET) (in->l + in->r);
        in += 1;
    }
}

#undef ET
#undef HALF
#undef IN_T
