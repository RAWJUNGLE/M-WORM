/* WAVE: единая шкала VA/WAVETABLE (0..5 формы, 6..10 MAIN 64 кадра, 11..17 USER, 18..57 LIB):
 * доступна с ручки, подписана, реально звучит (в т.ч. выше 20 — раньше synth резал WAVE до 20),
 * POS у MAIN листает кадры плавно, у USER/LIB — BEND, без DC и без скачков громкости.
 * cc -O2 -std=c99 -w -ffp-contract=off -DPLAT_HOST -Iworm -Iworm/worm -I. tests/core/t_wave_ui.c worm/worm_wt.c worm/worm_wt_data.c \
 *    worm/worm_wt_main.c worm/worm_synth.c worm/worm_params.c worm/worm_seq.c worm/worm_store.c worm/worm_midi.c div64.c -lm */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "worm_ui.c"
int worm_flash_read(uint32_t a, void *b, uint32_t l){(void)a;memset(b,0xFF,l);return 0;}
int worm_flash_write(uint32_t a, const void *b, uint32_t l){(void)a;(void)b;(void)l;return 0;}
int worm_flash_erase(uint32_t a, uint32_t l){(void)a;(void)l;return 0;}
static int ok = 1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
#define NS 8192

static void render(int algo, int w, int pos, float *X)
{
    worm_init(); worm_wt_init();
    W.tr[0].p[P_OA] = (int16_t)algo; W.tr[0].p[P_OW] = (int16_t)w; W.tr[0].p[P_OP] = (int16_t)pos;
    W.tr[0].p[P_OL] = 100; W.tr[0].p[P_E0A] = 0; W.tr[0].p[P_E0S] = 127; W.tr[0].p[P_E0D] = 127; W.tr[0].p[P_F1W] = 0; W.tr[0].p[P_F2W] = 0;
    worm_synth_note_on_v(W.tr[0].p, 60, 127, 44100, 0, 1, -1);
    for (int n = 0; n < NS; n += 256) {
        int16_t o[512]; worm_synth_render(o, 256);
        for (int i = 0; i < 256; i++) X[n + i] = o[2 * i] / 32768.f;
    }
}
static double energy(const float *x){ double e = 0; for (int i = 2048; i < NS; i++) e += (double)x[i] * x[i]; return e; }
static double dist(const float *a, const float *b){ double e = 0; for (int i = 2048; i < NS; i++) { double d = a[i] - b[i]; e += d * d; } return e; }
static double mean(const float *x){ double e = 0; for (int i = 2048; i < NS; i++) e += x[i]; return e / (NS - 2048); }
static double maxstep(const float *x){ double m = 0; for (int i = 2049; i < NS; i++) { double d = fabs(x[i] - x[i - 1]); if (d > m) m = d; } return m; }

int main(void)
{
    worm_ui_init(); worm_init(); worm_wt_init();
    /* --- шкала и ограничения --- */
    CK(WAVE_MAX == 57 && WAVE_MAIN0 == 6 && WAVE_USER0 == 11 && WAVE_LIB0 == 18, "WAVE layout 6/11/18/57");
    W.p[P_OA] = W.tr[W.pat].p[P_OA] = ALGO_WT;
    setv(P_OW, 57);  CK(W.p[P_OW] == 57, "WAVE 57 (last LIB) reachable in WAVETABLE");
    setv(P_OW, 58);  CK(W.p[P_OW] == 57, "WAVE clamps at 57");
    setv(P_OW, 2);   CK(W.p[P_OW] == 6, "WAVETABLE has no basic shapes: WAVE clamps up to the first MAIN table");
    setv(P_OW, 9);   setv(P_OA, ALGO_VA); CK(W.p[P_OW] == 2, "switch to VA: a table becomes SAW (VA = basic waves only)");
    setv(P_OW, 40);  CK(W.p[P_OW] == 5, "VA: WAVE clamps to 0..5");
    setv(P_OW, 4);   setv(P_OA, ALGO_WT); CK(W.p[P_OW] == WAVE_MAIN0, "switch to WAVETABLE: first MAIN table");
    setv(P_OA, ALGO_FM); setv(P_OW, 40); CK(W.p[P_OW] == 7, "FM: the ALGO slot clamps to 0..7");
    setv(P_OA, ALGO_WT); CK(W.p[P_OW] == WAVE_MAIN0, "WAVETABLE starts on the first MAIN table");
    CK(worm_wave_kind(5) == WK_SHAPE && worm_wave_kind(6) == WK_MAIN && worm_wave_kind(10) == WK_MAIN &&
       worm_wave_kind(11) == WK_CYCLE && worm_wave_kind(57) == WK_CYCLE, "wave kinds");
    CK(!strcmp(worm_wave_label(2), "SAW") && !strcmp(worm_wave_label(7), "PPG") && !strcmp(worm_wave_label(11), "U1") &&
       !strcmp(worm_wave_label(WAVE_LIB0 + 3), "PWN") && !strcmp(worm_wave_label(WAVE_LIB0), "SINE"), "wave labels");
    CK(worm_wave_cycle(3) == 0 && worm_wave_cycle(8) == 0 && worm_wave_cycle(WAVE_LIB0) == worm_wt_builtin[0] &&
       worm_wave_cycle(WAVE_USER0 + 2) == worm_wt_builtin[2], "wave_cycle: LIB direct, empty USER falls back to the same-numbered LIB");

    /* --- MAIN-таблицы: данные чистые (нет DC, ровная громкость по кадрам) --- */
    for (int t = 0; t < WT_MAIN; t++) {
        double rmin = 1e9, rmax = 0, dcmax = 0;
        for (int f = 0; f < WT_FRAMES; f++) { double s = 0, q = 0;
            for (int i = 0; i < WT_N; i++) { s += worm_wt_main[t][f][i]; q += (double)worm_wt_main[t][f][i] * worm_wt_main[t][f][i]; }
            double r = sqrt(q / WT_N), dc = fabs(s / WT_N); if (r < rmin) rmin = r; if (r > rmax) rmax = r; if (dc > dcmax) dcmax = dc; }
        CK(rmin > 2000, "MAIN: no silent frame"); CK(dcmax < 40, "MAIN: DC removed from every frame");
        CK(rmax < 5.0 * rmin, "MAIN: frame loudness is level (narrow PWN pulses are inherently quieter)");
    }

    /* --- звук --- */
    static float a[NS], b[NS], c[NS], d[NS], e[NS];
    render(ALGO_VA, 0, 0, a);                      /* SIN */
    render(ALGO_WT, WAVE_MAIN0, 0, b);             /* PWN, кадр 1 */
    render(ALGO_WT, WAVE_MAIN0, 127, c);           /* PWN, кадр 64 */
    CK(energy(a) > 1.0 && energy(b) > 1.0 && energy(c) > 1.0, "SIN / MAIN frames sound");
    CK(dist(a, b) > .05 * (energy(a) + energy(b)), "MAIN differs from the sine");
    CK(dist(b, c) > .01 * (energy(b) + energy(c)), "POS scans the frames of a MAIN table");
    CK(fabs(mean(b)) < .02 && fabs(mean(c)) < .02, "MAIN output has no DC");
    double lo = 1e9, hi = 0;
    for (int t = 0; t < WT_MAIN; t++) for (int pos = 0; pos <= 127; pos += 16) { render(ALGO_WT, WAVE_MAIN0 + t, pos, d);
        double en = energy(d); if (en < lo) lo = en; if (en > hi) hi = en; }
    CK(lo > 5.0 && hi < 5.0 * lo, "MAIN: loudness stays level across tables and POS");
    /* WAVE выше 20 раньше резался synth'ом до 20 */
    render(ALGO_WT, 20, 0, d); render(ALGO_WT, 57, 0, e);
    CK(energy(e) > 1.0 && dist(d, e) > .02 * (energy(d) + energy(e)), "WAVE 57 is not clamped to 20 in the synth");
    render(ALGO_WT, WAVE_LIB0 + 3, 0, d); render(ALGO_WT, WAVE_LIB0 + 3, 100, e);
    CK(dist(d, e) > .01 * (energy(d) + energy(e)), "LIB table: POS = BEND changes the shape");
    CK(fabs(mean(e)) < .2, "BEND keeps the table centred");
    /* POS плавно: переключение POS между блоками не даёт щелчка */
    {   worm_init(); worm_wt_init();
        int16_t *p = W.tr[0].p; p[P_OA] = ALGO_WT; p[P_OW] = WAVE_MAIN0 + 1; p[P_OP] = 0; p[P_OL] = 100; p[P_E0A] = 0; p[P_E0S] = 127; p[P_F1W] = 0;
        worm_synth_note_on_v(p, 48, 127, 44100, 0, 1, -1);
        static float g[NS]; double m0 = 0;
        for (int n = 0; n < NS; n += 256) { int16_t o[512]; if (n == 4096) p[P_OP] = 127; worm_synth_render(o, 256);
            for (int i = 0; i < 256; i++) g[n + i] = o[2 * i] / 32768.f; }
        m0 = maxstep(g); CK(m0 < 0.8, "POS jump 0 -> 127 is ramped inside the block (no hard click)");
        printf("max sample step across a POS jump: %.3f\n", m0); }
    printf("wave diff %.1f%%, POS scan %.1f%%, level lo/hi %.1f / %.1f\n",
           100.0 * dist(a, b) / (energy(a) + energy(b)), 100.0 * dist(b, c) / (energy(b) + energy(c)), lo, hi);
    printf(ok ? "WAVEUI OK\n" : "WAVEUI FAIL\n");
    return !ok;
}
