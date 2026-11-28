/* WORM-FM1 LIVE mode model: six macros (SELECT, ALGO, K1..K4), each bound to any parameter of any track (or to BPM / a live effect).
 * A set lives in the pattern record (own set) or is the bank-wide set kept in the project sector (worm_store.c). */
#include <string.h>
#include <stdio.h>
#include "worm.h"
#include "worm_wt.h"

lm_set_t worm_lm_bank, worm_lm_pat; int8_t worm_lm_own;
int8_t worm_lm_sw = 2, worm_lm_chain = 2, worm_lm_order = 0;   /* 1 BAR, 2 PASS, USED — заводские настройки LIVE: идём только по записанным паттернам, один паттерн — по кругу */
volatile int8_t worm_lm_dirty;
const char *const worm_lm_sw_names[5] = { "BEAT", "1 BAR", "2 BAR", "4 BAR", "8 BAR" };
const char *const worm_lm_chain_names[9] = { "OFF", "PASS", "2 PASS", "4 PASS", "1 BAR", "2 BAR", "4 BAR", "8 BAR", "16 BAR" };
const char *const worm_lm_order_names[3] = { "USED", "ALL", "RANDOM" };

static int16_t tg[240]; static int ntg;
static const char *xname(int id, char *b) {
    switch (id) {
    case LM_BPM: return "BPM"; case LM_FILT: return "FILTER"; case LM_CRUSH: return "CRUSH"; case LM_DUCK: return "DUCK";
    case P_MVL: return "MIDI VOL"; case P_MPN: return "MIDI PAN";
    default: break; }
    if (id >= P_C0L && id <= P_C7L) { sprintf(b, "TRK%d LVL", id - P_C0L + 1); return b; }
    if (id >= P_M0C && id < P_M0C + NMLANE * 7) { static const char *n[] = { "CC", "BASE", "DEPTH", "SPEED", "WAVE", "SYNC", "PHASE" }; sprintf(b, "CC%d %s", (id - P_M0C) / 7 + 1, n[(id - P_M0C) % 7]); return b; }
    return 0;
}
static void build(void) {
    if (ntg) return;
    tg[ntg++] = LM_BPM; tg[ntg++] = LM_FILT; tg[ntg++] = LM_CRUSH; tg[ntg++] = LM_DUCK;
    for (int t = 0; t < NTRK; t++) tg[ntg++] = (int16_t)(P_C0L + t);
    for (int i = 1; i < worm_dest_n && ntg < 200; i++) { const int id = worm_dest_list[i].id; if (id < 0 || id >= NP || worm_dest_ext(i, 0)) continue; if (id == P_OA) continue; tg[ntg++] = (int16_t)id; }
    tg[ntg++] = P_MVL; tg[ntg++] = P_MPN;
    for (int l = 0; l < NMLANE; l++) { tg[ntg++] = (int16_t)P_MLANE(l, 1); tg[ntg++] = (int16_t)P_MLANE(l, 2); tg[ntg++] = (int16_t)P_MLANE(l, 3); }
}
int worm_lm_ntargets(void) { build(); return ntg; }
int worm_lm_target(int i) { build(); return i >= 0 && i < ntg ? tg[i] : -1; }
int worm_lm_find(int id) { build(); for (int i = 0; i < ntg; i++) if (tg[i] == id) return i; return -1; }
const char *worm_lm_name(int id) {
    static char nb[2][20]; static int fl;
    if (id < 0) return "---";
    char *b = nb[fl ^= 1]; const char *x = xname(id, b); if (x) return x;
    const int di = worm_dest_index(id); if (di > 0) return worm_dest_name(di, W.p[P_OA]);
    return worm_pinfo[id].name;
}
void worm_lm_defaults(lm_set_t *s) {
    static const int16_t d[NLM] = { LM_BPM, LM_FILT, P_F1C, P_F1R, P_SDL, P_SRV };
    for (int i = 0; i < NLM; i++) { s->id[i] = d[i]; s->trk[i] = -1; }
}
lm_set_t *worm_lm_active(void) { return worm_lm_own ? &worm_lm_pat : &worm_lm_bank; }
int worm_lm_trk_cur(int trk) { if (trk >= 0) return trk; return W.mid ? NTRK + W.mpat : W.pat; }
void worm_lm_trk_name(int trk, char *o) { if (trk < 0) strcpy(o, "CUR"); else if (trk < NTRK) sprintf(o, "T%d", trk + 1); else sprintf(o, "M%d", trk - NTRK + 1); }

static int16_t *slot(int id, int trk, int *mirror) {   /* где лежит значение; *mirror — рабочая копия W.p, которую надо обновить тоже */
    *mirror = 0;
    if (id < NP) {
        if (!worm_param_is_track(id)) return &W.p[id];
        int t = trk >= 0 && trk < NTRK ? trk : W.pat;
        if (t == W.pat && !W.mid) *mirror = 1;
        return &W.tr[t].p[id];
    }
    int k = trk >= NTRK ? trk - NTRK : (W.mid ? W.mpat : 0); if (k < 0 || k >= NMTRK) k = 0;
    const int q = worm_mpos(id); if (q < 0) return &W.p[id];
    if (W.mid && k == W.mpat) *mirror = 1;
    return &W.mt[k].p[q];
}
int worm_lm_get(int id, int trk, int *v, int *mn, int *mx) {
    if (id < 0) return 0;
    switch (id) {
    case LM_BPM: *v = (int)(W.bpm * 2.f + .5f); *mn = 80; *mx = 600; return 1;
    case LM_FILT: *v = worm_lfx_get(LFX_A_FILT); *mn = -100; *mx = 100; return 1;
    case LM_CRUSH: *v = worm_lfx_get(LFX_A_CRUSH); *mn = 0; *mx = 100; return 1;
    case LM_DUCK: *v = worm_lfx_get(LFX_A_DUCK); *mn = 0; *mx = 100; return 1;
    default: break; }
    if (id >= NPALL) return 0;
    int m; *v = *slot(id, trk, &m); *mn = worm_pinfo[id].mn; *mx = worm_pinfo[id].mx; return 1;
}
void worm_lm_fmt(int id, int v, char *o) {
    switch (id) {
    case LM_BPM: sprintf(o, "%d.%d", v / 2, (v & 1) * 5); return;
    case LM_FILT: if (!v) strcpy(o, "OFF"); else sprintf(o, v < 0 ? "LP %d" : "HP %d", v < 0 ? -v : v); return;
    case LM_CRUSH: case LM_DUCK: sprintf(o, "%d", v); return;
    default: break; }
    if (id >= 0 && id < NPALL) worm_fmt(id, v, o); else o[0] = 0;
}
static int wmax_al(int al) { return al == ALGO_FM ? 3 : al == ALGO_VA ? WAVE_SHAPES - 1 : WAVE_MAX; }
static void put(int id, int trk, int v) {
    int mn, mx, cur; if (!worm_lm_get(id, trk, &cur, &mn, &mx)) return;
    if (v < mn) v = mn; if (v > mx) v = mx;
    switch (id) {
    case LM_BPM: W.bpm = (float)v * .5f; return;
    case LM_FILT: worm_lfx_set(LFX_A_FILT, v); return;
    case LM_CRUSH: worm_lfx_set(LFX_A_CRUSH, v); return;
    case LM_DUCK: worm_lfx_set(LFX_A_DUCK, v); return;
    default: break; }
    { const int t = trk >= 0 && trk < NTRK ? trk : W.pat, k = worm_fm_of(id); if (k >= 0 && W.tr[t].p[P_OA] == ALGO_FM) v = worm_fm_clamp(k, v); else
    if (id == P_OW || id == P_OW2) { const int t = trk >= 0 && trk < NTRK ? trk : W.pat, al = W.tr[t].p[P_OA]; const int hi = wmax_al(al), lo = al == ALGO_WT ? WAVE_MAIN0 : 0; if (v > hi) v = hi; if (v < lo) v = lo; } }
    int m; int16_t *p = slot(id, trk, &m); *p = (int16_t)v; if (m) W.p[id] = (int16_t)v;
    if (id == P_MMT) { int k = trk >= NTRK ? trk - NTRK : W.mpat; worm_mtrk_mute(k, v); }
}
int worm_lm_accel(int s, int d) {   /* разгон по скорости кручения (мс из worm_ui_now) */
    static uint32_t t[NLM + 1]; static int dir[NLM + 1];
    if (s < 0 || s > NLM) s = NLM; const uint32_t dt = worm_ui_now - t[s]; const int dr = d > 0 ? 1 : -1; t[s] = worm_ui_now;
    int mul = 1; if (dir[s] == dr) { if (dt < 25) mul = 8; else if (dt < 55) mul = 4; else if (dt < 110) mul = 2; }
    dir[s] = dr; return d * mul;
}
void worm_lm_turn(int s, int d) {
    lm_set_t *S = worm_lm_active(); if (s < 0 || s >= NLM) return;
    const int id = S->id[s], trk = S->trk[s]; int v, mn, mx; if (!worm_lm_get(id, trk, &v, &mn, &mx)) return;
    const int rg = mx - mn; const int lst = id < NPALL && worm_pinfo[id].fmt == F_LIST;
    if (!lst && rg > 8) d = worm_lm_accel(s, d);
    put(id, trk, v + d);
}
int worm_lm_store_pack(uint8_t *o) {
    int n = 0; o[n++] = 0xB9;
    for (int i = 0; i < NLM; i++) { o[n++] = (uint8_t)(worm_lm_pat.id[i] & 0xFF); o[n++] = (uint8_t)((worm_lm_pat.id[i] >> 8) & 0xFF); }
    for (int i = 0; i < NLM; i++) o[n++] = (uint8_t)worm_lm_pat.trk[i];
    return n;   /* 19 */
}
int worm_lm_store_unpack(const uint8_t *o, int n) {   /* o[0] == 0xB9 */
    if (n < 19 || o[0] != 0xB9) return 0;
    for (int i = 0; i < NLM; i++) {
        int id = (int16_t)(o[1 + 2 * i] | (o[2 + 2 * i] << 8)); int t = (int8_t)o[13 + i];
        if (id >= LM_END || id < -1) id = -1; if (t < -1 || t >= NTRK + NMTRK) t = -1;
        worm_lm_pat.id[i] = (int16_t)id; worm_lm_pat.trk[i] = (int8_t)t;
    }
    return 19;
}
int worm_lm_pass_bars(void) {   /* самая длинная дорожка с шагами: len * (2/1/.5) / 16 шагов на такт */
    int best = 1;
    for (int t = 0; t < NTRK + NMTRK; t++) {
        const pattern_t *p = t < NTRK ? &W.pats[t] : &W.mpats[t - NTRK]; int used = 0;
        for (int j = 0; j < NSTEP && !used; j++) if (p->s[j].on) used = 1;
        if (!used) continue;
        const int len = t < NTRK ? W.tr[t].len : W.mt[t - NTRK].len, sp = t < NTRK ? W.tr[t].spd : W.mt[t - NTRK].spd;
        int bars = sp == 0 ? (len * 2 + 15) / 16 : sp == 2 ? (len + 31) / 32 : (len + 15) / 16;
        if (bars > best) best = bars;
    }
    return best;
}
