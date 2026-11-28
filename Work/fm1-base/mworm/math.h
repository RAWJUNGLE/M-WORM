/* math.h shim for the fm1-base device build: the FM-1 links no libm and has no
 * <math.h>; fm1-base provides its own single-precision maths in fastmath.h.
 * This maps the standard <math.h> names the C-only DSP uses onto fm1-base's fm_*.
 * On the host (PLAT_HOST) the real <math.h> is used.
 */
#ifndef MWORM_MATH_SHIM_H
#define MWORM_MATH_SHIM_H

#ifdef PLAT_HOST
#include_next <math.h>
#else
#include "fastmath.h"

/* стандартные имена -> fm1-base fastmath (float, без libm) */
#define sinf     fm_sinf
#define cosf     fm_cosf
#define tanf     fm_tanf
#define tanhf    fm_tanhf
#define expf     fm_expf
#define exp2f    fm_exp2f
#define logf     fm_logf
#define log2f    fm_log2f
#define powf     fm_powf
#define sqrtf    fm_sqrtf
#define fabsf    fm_fabsf
#define floorf   fm_floorf

/* hypotf отсутствует в fastmath.h: sqrt(a^2+b^2) через fm_sqrtf */
static inline float hypotf(float a, float b)
{
    float x = fm_fabsf(a), y = fm_fabsf(b);
    if (x < y) { float t = x; x = y; y = t; }
    if (x == 0.0f)
        return 0.0f;
    y = y / x;
    return x * fm_sqrtf(1.0f + y * y);
}

/* log10f отсутствует в fastmath.h: через logf */
#define FM_LN10 2.302585092994046f
static inline float log10f(float x) { return fm_logf(x) / FM_LN10; }

/* lroundf: округление до long (lfo.c), без double-промоушена */
static inline long lroundf(float x) { return (long)(x >= 0.0f ? x + 0.5f : x - 0.5f); }

#endif /* PLAT_HOST */
#endif /* MWORM_MATH_SHIM_H */
