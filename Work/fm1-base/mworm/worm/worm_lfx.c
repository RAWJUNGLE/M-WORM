/* WORM-FM1 live-mode master effects: REPEAT 1/8 1/16 1/32, REVERSE, TAPE STOP, FREEZE, LPF, HPF, CRUSH, DUCK.
 * Own implementation (the idea of a PERFORM layer on the master is the one of Hortator / MONO2K by their authors; no code copied).
 *
 * Budget rules, as in worm_synth.c: nothing runs while the layer is idle (the master is then bit-identical); the effects work
 * on the master block (stereo, 44.1 kHz) after the DC block and before the limiter.
 *   - buffer effects (REPEAT / REVERSE / TAPE STOP / FREEZE) share ONE mono ring recorded at 22.05 kHz (LFX_BUF samples, int16):
 *     8 KB by default = 186 ms. A slice longer than the ring (1/8 or 1/16 at a slow tempo) is shortened to it. The last pressed wins.
 *     RAM: the ring lives in .bss (about 13 KB were free in the last build, see `size`); -DLFX_BUF=2048 halves it.
 *   - filter / crush / duck: no RAM; their amounts come from macros (worm_lfx_set) and from held keys (worm_lfx_key). */
#include <math.h>
#include <string.h>
#include "worm.h"

#ifndef LFX_BUF
#define LFX_BUF 4096u                 /* ring samples (power of two) */
#endif
#define LFX_MASK (LFX_BUF - 1u)
#define LFX_XF 48.f                   /* loop cross-fade, ring samples */

enum { M_NONE, M_LOOP, M_REV, M_TAPE, M_FRZ };

static int16_t ring[LFX_BUF];
static uint32_t wc;                   /* ring samples written (latest = wc-1) */
static float acc; static int ph2;     /* 2:1 decimation */
static int armed;                     /* live mode on: the ring records */
static unsigned held; static unsigned ordv[LFX_N]; static unsigned oseq;
static int amt[LFX_A_N];              /* macro amounts: FILT -100..100, CRUSH 0..100, DUCK 0..100 */
static int mode, want;                /* buffer effect playing / wanted by the keys */
static float mix, pos, tape_r, tape_k;
static uint32_t st0; static float slen;   /* loop: first ring sample (absolute), length */
/* filter */
static float fa, fta, fs1l, fs2l, fs1r, fs2r; static int fhp;
/* crush */
static int crc; static float hl, hr;
static const char *const NAME[LFX_N] = { "REP/8", "REP/16", "REP/32", "REVERSE", "TAPE", "FREEZE", "LPF", "HPF", "CRUSH", "DUCK" };

const char *worm_lfx_name(int fx) { return fx >= 0 && fx < LFX_N ? NAME[fx] : "?"; }
unsigned worm_lfx_held(void) { return held; }
int worm_lfx_get(int w) { return w >= 0 && w < LFX_A_N ? amt[w] : 0; }
void worm_lfx_set(int w, int v) {
    if (w < 0 || w >= LFX_A_N) return;
    const int lo = w == LFX_A_FILT ? -100 : 0; if (v < lo) v = lo; if (v > 100) v = 100; amt[w] = v;
}
void worm_lfx_arm(int on) { armed = on != 0; if (!on) { held = 0; want = M_NONE; } }
void worm_lfx_reset(void) { held = 0; want = M_NONE; mode = M_NONE; mix = 0.f; memset(amt, 0, sizeof amt); fa = 0.f; fs1l = fs2l = fs1r = fs2r = 0.f; hl = hr = 0.f; crc = 0; }
void worm_lfx_key(int fx, int on) {
    if (fx < 0 || fx >= LFX_N) return;
    if (on) { held |= 1u << fx; ordv[fx] = ++oseq; } else held &= ~(1u << fx);
}
static int buf_want(void) {            /* the last pressed buffer effect among the held */
    int b = -1; unsigned bo = 0;
    for (int i = LFX_R8; i <= LFX_FRZ; i++) if ((held >> i) & 1u && ordv[i] >= bo) { bo = ordv[i]; b = i; }
    return b;
}
static float rdb(uint32_t base, float off) {   /* ring read at base+off (off may be negative), linear; relative addressing keeps float exact */
    const float fo = floorf(off); const int32_t io = (int32_t)fo; const float f = off - fo;
    const uint32_t i = base + (uint32_t)io;
    const float a = (float)ring[i & LFX_MASK], b = (float)ring[(i + 1u) & LFX_MASK];
    return (a + (b - a) * f) * (1.f / 32768.f);
}
static float slice_len(int fx) {       /* ring samples */
    const float spb = 60.f / W.bpm * 22050.f;          /* ring samples per beat */
    float l = fx == LFX_R8 ? spb * .5f : fx == LFX_R16 ? spb * .25f : fx == LFX_R32 ? spb * .125f : fx == LFX_REV ? spb * .5f : 1024.f;
    if (l < 64.f) l = 64.f; if (l > (float)(int)(LFX_BUF - 64u)) l = (float)(int)(LFX_BUF - 64u);
    return l;
}
static int cur_fx = -1;
static uint32_t tb; static float tf;   /* tape read head: ring sample + fraction */
static void start_fx(int fx) {
    mode = fx == LFX_REV ? M_REV : fx == LFX_TAPE ? M_TAPE : fx == LFX_FRZ ? M_FRZ : M_LOOP; pos = 0.f;
    slen = slice_len(fx); st0 = wc - (uint32_t)slen;
    if (mode == M_TAPE) { tape_r = 1.f; tb = wc - 2u; tf = 0.f; tape_k = powf(.02f, 1.f / (60.f / W.bpm * 44100.f)); }
}
static float loop_val(float p) {       /* loop / reverse sample at slice position p (0..slen) */
    const float xf = slen > 2.f * LFX_XF ? LFX_XF : slen * .5f, e = slen - p;
    if (mode == M_REV) {               /* backwards: a short fade in / out at the wrap (there is no audio to blend with) */
        float a = rdb(st0 + (uint32_t)slen, -1.f - p), w = 1.f;
        if (p < xf) w = p / xf; if (e < xf && e / xf < w) w = e / xf;
        return a * w;
    }
    float a = rdb(st0, p);
    if (e < xf) a += (rdb(st0, p - slen) - a) * (1.f - e / xf);   /* blend into the audio that precedes the slice: the wrap is continuous */
    return a;
}

float worm_lfx_block(float *L, float *R, int n) {
    const int idle_fx = !held && !amt[0] && !amt[1] && !amt[2] && mix <= 0.f && fa <= 0.f;
    if (!armed && idle_fx) return -1.f;
    /* want / start */
    {   const int b = buf_want();
        if (b < 0) { want = M_NONE; cur_fx = -1; }
        else { want = b == LFX_REV ? M_REV : b == LFX_TAPE ? M_TAPE : b == LFX_FRZ ? M_FRZ : M_LOOP;
               if (b != cur_fx) { cur_fx = b; start_fx(b); } } }
    /* record: the ring keeps the dry master; loops freeze it while they play (TAPE keeps recording: it reads behind the head) */
    if (want == M_NONE || want == M_TAPE || mix <= 0.f)
        for (int s = 0; s < n; s++) {
            acc += (L[s] + R[s]) * .5f;
            if (++ph2 == 2) { ph2 = 0; float m = acc * .5f; acc = 0.f; if (m > .999f) m = .999f; if (m < -.999f) m = -.999f; ring[wc & LFX_MASK] = (int16_t)(m * 32767.f); wc++; }
        }
    if (idle_fx) return -1.f;
    /* buffer effect */
    if (mode != M_NONE) {
        const float tgt = want != M_NONE ? 1.f : 0.f, step = 1.f / 128.f;
        for (int s = 0; s < n; s++) {
            if (mix < tgt) { mix += step; if (mix > tgt) mix = tgt; } else if (mix > tgt) { mix -= step; if (mix < 0.f) mix = 0.f; }
            float y;
            if (mode == M_TAPE) {
                y = rdb(tb, tf); tf += tape_r * .5f; while (tf >= 1.f) { tf -= 1.f; tb++; }
                if ((int32_t)(tb - (wc - 2u)) > 0) { tb = wc - 2u; tf = 0.f; }
                tape_r *= tape_k; if (tape_r < .01f) tape_r = 0.f; if (tape_r < .1f) y *= tape_r * 10.f;   /* a stopped tape is silent, not a DC level */
            } else if (mode == M_FRZ) {
                float p2 = pos + slen * .5f; if (p2 >= slen) p2 -= slen;
                const float w1 = 1.f - fabsf(2.f * pos / slen - 1.f), w2 = 1.f - w1;
                y = rdb(st0, pos) * w1 + rdb(st0, p2) * w2; pos += .5f; if (pos >= slen) pos -= slen;
            } else { y = loop_val(pos); pos += .5f; if (pos >= slen) pos -= slen; }
            L[s] += (y - L[s]) * mix; R[s] += (y - R[s]) * mix;
        }
        if (mix <= 0.f && want == M_NONE) mode = M_NONE;
    }
    /* filter: amount -100..100 (- LPF, + HPF); the held LPF / HPF keys push it */
    {   int fv = amt[LFX_A_FILT]; if ((held >> LFX_LPF) & 1u) fv = -85; if ((held >> LFX_HPF) & 1u) fv = 85;
        if (fv == 0 && fa <= 0.f) { fs1l = fs2l = fs1r = fs2r = 0.f; }
        else {
            const int hp = fv > 0; const float u = (float)(fv < 0 ? -fv : fv) * .01f;
            float fc = hp ? 20.f * powf(150.f, u) : 20000.f * powf(.01f, u);   /* LPF 20 kHz .. 200 Hz, HPF 20 Hz .. 3 kHz */
            if (fv == 0) fc = 20000.f;
            fta = 1.f - expf(-6.2831853f * fc * (1.f / 44100.f)); if (fta > 1.f) fta = 1.f;
            if (hp != fhp) { fs1l = fs2l = fs1r = fs2r = 0.f; fhp = hp; }
            const float a0 = fa <= 0.f ? fta : fa, a1 = fta, da = (a1 - a0) / (float)n; float a = a0;
            for (int s = 0; s < n; s++) {
                a += da;
                fs1l += a * (L[s] - fs1l); fs2l += a * (fs1l - fs2l); fs1r += a * (R[s] - fs1r); fs2r += a * (fs1r - fs2r);
                if (hp) { L[s] -= fs2l; R[s] -= fs2r; } else { L[s] = fs2l; R[s] = fs2r; }
            }
            fa = fv == 0 ? 0.f : fta;
        } }
    /* crush: sample-rate and bit-depth reduction */
    {   int c = amt[LFX_A_CRUSH]; if ((held >> LFX_CRU) & 1u && c < 70) c = 70;
        if (c > 0) {
            const int div = 1 + c / 12; const float S = (float)(1 << (15 - c * 11 / 100)), iS = 1.f / S;
            for (int s = 0; s < n; s++) {
                if (--crc <= 0) { crc = div; float x = L[s] * S, y = R[s] * S; hl = (float)(int)(x + (x >= 0.f ? .5f : -.5f)) * iS; hr = (float)(int)(y + (y >= 0.f ? .5f : -.5f)) * iS; }
                L[s] = hl; R[s] = hr;
            }
        } }
    /* duck: the mix dips at each beat and swells back */
    {   int d = amt[LFX_A_DUCK]; if ((held >> LFX_DUCK) & 1u && d < 85) d = 85;
        if (d > 0) {
            const float dd = (float)d * .0085f; const uint32_t p0 = worm_beat_phase(), sl = worm_step_len() * 4u;
            /* 2^32 * n / sl in integer math: no double -> no __floatunsidf (missing in the ARM runtime). */
            const uint32_t adv = sl ? (uint32_t)(((uint64_t)(uint32_t)n << 32) / sl) : 0u, p1 = p0 + adv;
            /* ARM toolchain has no __floatunsidf: split the 32-bit phase into two signed halves (volatile so it is not folded back). */
            volatile int h0 = (int)(p0 >> 16), l0 = (int)(p0 & 0xFFFFu), h1 = (int)(p1 >> 16), l1 = (int)(p1 & 0xFFFFu);
            const float x0 = (float)h0 * (1.f / 65536.f) + (float)l0 * (1.f / 4294967296.f);
            const float x1 = (float)h1 * (1.f / 65536.f) + (float)l1 * (1.f / 4294967296.f);
            const float g0 = 1.f - dd * (1.f - x0) * (1.f - x0), g1 = 1.f - dd * (1.f - x1) * (1.f - x1), dg = (g1 - g0) / (float)n; float g = g0;
            for (int s = 0; s < n; s++) { g += dg; L[s] *= g; R[s] *= g; }
        } }
    float pk = 0.f;
    for (int s = 0; s < n; s++) { const float a = fabsf(L[s]), b = fabsf(R[s]); if (a > pk) pk = a; if (b > pk) pk = b; }
    return pk;
}
