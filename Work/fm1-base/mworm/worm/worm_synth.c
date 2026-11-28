/* M-WORM compact audio engine (written from scratch; replaces the old synth/filters/lag/smp).
 *
 * Budget rules (the FM-1 has a single-precision FPU, no libm, no second core worth counting on):
 *   - per SAMPLE only add/mul/compare: no sin/exp/tan/div/sqrt, no table lookups, no lag objects;
 *   - everything else (envelopes, LFOs, p-locks, filter coefficients, pitch, FX constants) is computed once per
 *     BL-sample block and the audio path ramps linearly between block values (no zipper clicks);
 *   - oscillators are 32-bit integer phase accumulators; sine is a parabola (+1 refinement), FM is phase modulation
 *     done on the integer phase, so a carrier+modulator pair costs two parabolas;
 *   - one mono voice per track (NVOICE == NTRK), two engines: VA (sine/tri/saw/square/pulse/noise + sub) and FM;
 *   - per track: 2 oscillators, 2 TPT-SVF filters (OFF/LP/HP/BP), 5 ADSR, 4 LFO, distortion, pan, 3 sends;
 *   - shared FX: tempo delay (feedback LPF), 4-comb/2-allpass reverb, 1-voice chorus, each skipped while idle;
 *   - master: DC block + block look-ahead peak limiter (the whole block is known before it is output).
 * Public API is unchanged (worm.h), so UI / sequencer / MIDI / store are untouched. */
#include <math.h>
#include <string.h>
#include "worm.h"
#include "worm_wt.h"

#define BL 64                         /* control block, samples (1.45 ms) */
#define TBR 0.9976463f                /* .86 ^ (1/BL): per-sample decay of the TRANSIENT boost */
#define PI2 6.2831853f
#define LN20 2.9957323f               /* ln(20 Hz) */
#define OUTMAX 512                    /* frames of OUT24 kept (a worm_render call longer than this is clamped); аудио-блок платформы — 256 кадров */

float worm_master = 0.7f;

/* Второе ядро (CPU1): мост ставит app.c (worm_smp_init). Без него — как раньше, всё на CPU0. */
int (*worm_smp_run)(void (*fn)(uint32_t), uint32_t);
int (*worm_smp_wait)(void);
int worm_smp_on;

/* ------------------------------------------------------------------------------------------ state ---- */
typedef struct {
 uint8_t act, rel, init, rs, st[NENV];   /* rs: легато-перезапуск — огибающие на этом блоке гасим к нулю и атакуют заново */
 int gate, note, lv, trk;
 float vel, f, fcur, gl;
 uint32_t ph[4], sph, rng;               /* ph[0..1] — OSC1/OSC2, у FM — фазы операторов C, A, B1, B2 */
 float fbm[2];                        /* FM: память обратной связи (fbm[0]) */
 float ft[2], fl[2], fmx;                  /* FM: время ENV A / ENV B от триггера, с; fl — уровни A / B в конце прошлого блока (сглаживание скачков) */
 float env[NENV], envo[NENV];         /* envelope now / at the previous block end */
 float svf[2][2], fcl[2];             /* SVF states; smoothed ln(cutoff) */
 float rp[5], lvp[2], tb;             /* ramped gains (L,R,sendD,sendR,sendC); osc levels; transient boost */
 float pwp[2];                       /* POS (PW) прошлого блока: рампа до нового значения */
 float pk[4], pf[3];                  /* pitch cache: octave offsets / detune, resulting factors */
 int16_t e[NP];                       /* snapshot of the step's effective patch (p-locks) */
 uint8_t lk[(NP + 7) / 8];            /* bit = parameter locked by the step: read from e[], the rest live from the track */
} voice_t;

/* шесть голосов заняли бы ~3.4 КБ пула; пул нужен BSP (буферы USB/BT), поэтому голоса живут в .bss
 * (tam же инициализируются worm_synth_init(): memset(V, 0, sizeof V)). */
static voice_t V[NVOICE];
static int16_t PP[NP];
static int age_ctr;
static float last_f[NTRK];
static int ldst[NTRK][NLFO];
static int16_t ldx[NTRK][NLFO][2];    /* дополнительные цели составного DEST (worm_dest_ext), -1 = нет */
static float lph[NTRK][NLFO], lfo_v[NTRK][NLFO], lfo_holdv[NTRK][NLFO];
static uint8_t lfo_on[NTRK][NLFO];
static float lfo_mod[NTRK][NP + NVDEST];   /* + виртуальные цели (Pitch All / Pitch A+B2) */       /* sum of LFO modulation per parameter (normalised), valid for this block */
static float chv[NTRK];               /* smoothed channel level */
static int edst[NTRK]; static float edep[NTRK];
static int ms_i16 = 1;
static int rt_hint, out_off;
static int32_t OUT24[OUTMAX * 2];
/* USB-аудио: тап для режимов TRACK 1-3 / TRACK 4-6 / FX. Индекс кадра = out_off + сэмпл, 8 int16 на кадр (BSP берёт первые 6). */
static int tap_mode, tap_base;                        /* 0 = выкл, 1 = дорожки 1-3, 2 = дорожки 4-6 (стерео-парами), 3 = FX-возвраты; tap_base = первая дорожка группы */
static WORM_POOL int16_t TAP[OUTMAX * 8];
#define TAPN 3                                        /* дорожек в одной группе тапа (USB: 3 стерео-пары = 6 каналов) */
static WORM_POOL float TL[TAPN][BL], TRK_R[TAPN][BL]; /* сухие L/R дорожек выбранной группы за блок (индекс = дорожка - tap_base) */
/* возвраты delay/reverb/chorus берём в локальные fx* и сразу кладём в TAP */

/* automation cache (value of every automated parameter at the track's current step) */
#define NPB8 ((NP + 7) / 8)
static uint8_t aut_on[NTRK], aut_has[NTRK][NPB8];
static int16_t aut_val[NTRK][NP];
static int aut_key_step[NTRK]; static int16_t aut_key_n[NTRK]; static uint32_t aut_key_gen[NTRK];

/* ------------------------------------------------------------------------------------------ helpers ---- */
static inline float fclamp(float x, float a, float b) { return x < a ? a : x > b ? b : x; }
/* sine of pi*x for x = int32 phase / 2^31: parabola + one refinement step (|err| < 0.001) */
static inline float psin(int32_t p) {
 float x = (float)p * (1.f / 2147483648.f), y = 4.f * (x - x * fabsf(x));
 return y * (.775f + .225f * fabsf(y));
}
static inline uint32_t xs32(uint32_t *s) { uint32_t x = *s; x ^= x << 13; x ^= x >> 17; x ^= x << 5; return *s = x; }
static float ET(int v) { float x = v * (1.f / 127.f); return .002f + 5.f * x * x * x; }
/* cheap soft clip: odd cubic, flat at +-1 (no division) */
static inline float sknee(float x) { float a = fabsf(x); if (a <= .9f) return x; float u = (a - .9f) * 10.f; if (u > 1.f) u = 1.f; a = .9f + .1f * u * (2.f - u); return x < 0.f ? -a : a; }
static inline float sclip(float x) { x = fclamp(x, -1.f, 1.f); return x * (1.5f - .5f * x * x); }
/* тап 4 TRACK / FX идёт до мастера и лимитера: дорожка выше 1.0 раньше жёстко срезалась (треск в записи); теперь плавное колено 0.8 -> 1.0 (ниже 0.8 сигнал не тронут) */
static inline int16_t tap16(float x) { const float a = fabsf(x); if (a > .8f) { float u = (a - .8f) * (1.f / .8f); if (u > 1.f) u = 1.f; x = (x < 0.f ? -1.f : 1.f) * (.8f + .2f * u * (2.f - u)); } return (int16_t)(int)(x * 32767.f); }
static float shape(float x, int t, float d) {
 float g = 1.f + d * (1.f / 127.f) * (t == 1 ? 3.f : t == 2 ? 10.f : 30.f), y = sclip(x * g);
 if (t == 3) y = fclamp(y * 1.3f + .1f, -.6f, .9f);
 return y;
}

float worm_lfo_value(int lfo) { int t = W.pat; if (t < 0 || t >= NTRK) t = 0; return lfo >= 0 && lfo < NLFO ? lfo_v[t][lfo] : 0.f; }
void worm_synth_set_patch(const int16_t *p) { memcpy(PP, p, sizeof PP); }
void worm_synth_i16(int on) { ms_i16 = on ? 1 : 0; }
void worm_synth_offset(int f) { out_off = f < 0 ? 0 : f; }
const int32_t *worm_synth_out24(void) { return OUT24; }
void worm_tap_mode(int m) { tap_mode = m >= 0 && m <= 3 ? m : 0; tap_base = tap_mode == 2 ? 3 : 0; }
const int16_t *worm_synth_tap(void) { return TAP; }
void worm_synth_retrig_hint(int on) { rt_hint = on; }

/* ------------------------------------------------------------------------------------------ parameters ---- */
/* value of parameter id for the voice's track: step lock (snapshot) > automation > live track value, + LFO / free ENV. */
static uint8_t mmask[NTRK][(NP + NVDEST + 7) / 8];   /* bit = parameter is modulated (LFO / free ENV / automation) on this track right now */
static float mp_slow(const voice_t *v, int t, int id, float x, int lk) {
 float m = lfo_mod[t][id];
 if (!lk && aut_on[t] && ((aut_has[t][id >> 3] >> (id & 7)) & 1)) x = (float)aut_val[t][id];
 if (id == edst[t]) m += v->env[4] * edep[t];
 if (m != 0.f) { const pinfo_t *pi = &worm_pinfo[id]; x = fclamp(x + worm_mod_span(id, m) * (float)(pi->mx - pi->mn), (float)pi->mn, (float)pi->mx); }
 return x;
}
static inline float vmod(int t, int id) {   /* виртуальная цель LFO (Pitch All / Pitch A+B2), нормированная глубина */
 return ((mmask[t][id >> 3] >> (id & 7)) & 1) ? lfo_mod[t][id] : 0.f; }
static inline float mp(const voice_t *v, const int16_t *tp, int t, int id) {
 const int lk = (v->lk[id >> 3] >> (id & 7)) & 1;
 const float x = lk ? (float)v->e[id] : (float)tp[id];
 return ((mmask[t][id >> 3] >> (id & 7)) & 1) ? mp_slow(v, t, id, x, lk) : x;
}

/* ------------------------------------------------------------------------------------------ LFO (block rate) ---- */
static float lfo_wave(int w, float p, const int16_t *tp, int i) {
 p -= (float)(int)p; if (p < 0.f) p += 1.f;
 switch (w) {
  case 0: return 1.f - 4.f * fabsf(p - .5f);                              /* TRI */
  case 1: return -psin((int32_t)((p - .5f) * 4294967296.f));                  /* SIN */
  case 2: return p < .5f ? 1.f : -1.f;                                     /* SQR */
  case 3: return 2.f * p - 1.f;                                            /* SAW */
  case 4: { uint32_t h = (uint32_t)(p * 8.f) * 2654435761u; h ^= h >> 15; h *= 2246822519u; h ^= h >> 13;
            return (float)(int32_t)h * (1.f / 2147483648.f); }             /* RND: 8 steps per cycle */
  case 5: return expf(-p * 6.f) * 2.f - 1.f;                               /* EXP */
  case 6: return p;                                                        /* RAMP */
  default: { float s = fclamp(tp[P_L0Q + i * 9] * (1.f / 127.f), .01f, .99f);   /* TDS: slope / smooth / shift */
            float x = p < s ? p / s : (1.f - p) / (1.f - s), sn = x * x * (3.f - 2.f * x);
            x += (sn - x) * (tp[P_L0R + i * 9] * (1.f / 127.f));
            return fclamp(x * 2.f - 1.f + (tp[P_L0T + i * 9] - 64) * (1.f / 64.f), -1.f, 1.f); }
 }
}

static void lfo_block(int bl) {
 for (int t = 0; t < NTRK; t++) {
  const int16_t *tp = W.tr[t].p;
  for (int i = 0; i < NLFO; i++) { int d = ldst[t][i]; if (d >= 0) lfo_mod[t][d] = 0.f; for (int q = 0; q < 2; q++) { d = ldx[t][i][q]; if (d >= 0) lfo_mod[t][d] = 0.f; } }
  int need = 0;
  for (int i = 0; i < NVOICE; i++) if (V[i].act && V[i].trk == t) need = 1;
  for (int i = 0; i < NLFO; i++) { int d = worm_dest_param(tp[P_L0D + i * 9]); ldst[t][i] = d >= 0 && d < NP + NVDEST ? d : -1; ldx[t][i][0] = ldx[t][i][1] = -1; if (ldst[t][i] >= 0) worm_dest_ext(tp[P_L0D + i * 9], ldx[t][i]); }
  edst[t] = -1; { int d = worm_dest_param(tp[P_E4M]); if (d >= 0 && d < NP + NVDEST) edst[t] = d; }
  edep[t] = tp[P_E4P] * (1.f / 127.f);
  memset(mmask[t], 0, sizeof mmask[t]);
  if (aut_on[t]) for (int b = 0; b < (NP + 7) / 8; b++) mmask[t][b] = aut_has[t][b];
  for (int i = 0; i < NLFO; i++) for (int q = 0; q < 3; q++) { int d = q ? ldx[t][i][q - 1] : ldst[t][i]; if (d >= 0) mmask[t][d >> 3] |= (uint8_t)(1u << (d & 7)); }
  if (edst[t] >= 0) mmask[t][edst[t] >> 3] |= (uint8_t)(1u << (edst[t] & 7));
  if (need) for (int i = 0; i < NLFO; i++) {
   const int m = tp[P_L0M + i * 9], o = i * 9;
   if (ldst[t][i] < 0) { lfo_v[t][i] = 0.f; continue; }
   if (m == 1) { lfo_v[t][i] = lfo_holdv[t][i]; continue; }
   if (!lfo_on[t][i]) { lfo_v[t][i] = 0.f; continue; }
   float rmod = 1.f, dmod = 1.f, rate, prev = lph[t][i];
   for (int j = 0; j < NLFO; j++) { if (j == i) continue; int d = ldst[t][j];
    if (d == P_L0S + o) rmod += lfo_v[t][j] * 2.f; else if (d == P_L0P + o) dmod += lfo_v[t][j] * .5f; }
   if (rmod < .05f) rmod = .05f;
   if (tp[P_L0Y + o]) { static const float bt[7] = {.25f, .5f, 1.f, 2.f, 4.f, 8.f, 16.f};
    int di = (tp[P_L0S + o] + 9) / 19; if (di > 6) di = 6; rate = (W.bpm / 60.f) / bt[di];
   } else rate = .1f + tp[P_L0S + o] * (1.f / 127.f) * 19.9f;
   lph[t][i] += bl * rate * rmod * (1.f / (float)WORM_FS);
   lfo_v[t][i] = fclamp(lfo_wave(tp[P_L0W + o], lph[t][i], tp, i) * tp[P_L0P + o] * (1.f / 127.f) * dmod, -1.f, 1.f);
   if (m == 2 && prev < 1.f && lph[t][i] >= 1.f) { lfo_on[t][i] = 0; lph[t][i] -= (float)(int)lph[t][i]; }
   else if (m == 3 && prev < .5f && lph[t][i] >= .5f) lfo_on[t][i] = 0;
   lph[t][i] -= (float)(int)lph[t][i];
  }
  for (int i = 0; i < NLFO; i++) { int d = ldst[t][i]; if (d >= 0) lfo_mod[t][d] += lfo_v[t][i]; for (int q = 0; q < 2; q++) { d = ldx[t][i][q]; if (d >= 0) lfo_mod[t][d] += lfo_v[t][i]; } }
 }
}

/* automation values at each track's current step: recomputed only when the step / points change */
static void aut_block(void) {
 for (int t = 0; t < NTRK; t++) {
  int na = W.tr[t].naut;
  if (!W.run || na <= 0) { aut_on[t] = 0; continue; }
  const int st = W.trk_step[t]; const uint32_t g = worm_aut_gen;
  if (!aut_on[t] || st != aut_key_step[t] || na != aut_key_n[t] || g != aut_key_gen[t]) {
   aut_key_step[t] = st; aut_key_n[t] = (int16_t)na; aut_key_gen[t] = g; memset(aut_has[t], 0, sizeof aut_has[t]);
   if (na > NAUT) na = NAUT;
   for (int k = 0; k < na; k++) { int id = W.tr[t].aut[k].id, av;
    if (id >= NP || ((aut_has[t][id >> 3] >> (id & 7)) & 1)) continue;
    if (worm_aut_value(t, id, st, &av)) { aut_has[t][id >> 3] |= (uint8_t)(1u << (id & 7)); aut_val[t][id] = (int16_t)av; } }
  }
  aut_on[t] = 1;
 }
}

/* ------------------------------------------------------------------------------------------ voice control ---- */
static void rel_voice(voice_t *v) { v->rel = 1; for (int k = 0; k < NENV; k++) v->st[k] = 3; }

int worm_synth_note_on_v(const int16_t *eff, int note, int vel, int gate, int trk, int primary, int lvnote) {
 if (trk < 0 || trk >= NTRK) trk = 0;
 if (!primary) return -1;                                   /* mono tracks: no chords / unison voices */
 voice_t *v = &V[trk];
 const int legato = v->act && v->trk == trk;                /* same track still sounding: restart in place, no click */
 if (legato) { v->rel = 0; for (int k = 0; k < NENV; k++) v->st[k] = 0; memset(v->lk, 0, sizeof v->lk);
  /* нота на ещё звучащей дорожке: огибающие перезапускаются С НУЛЯ (короткий спад за один блок ~1.5 мс без щелчка, затем атака) —
   * каждый шаг получает полноценный удар. Раньше атака шла от текущего уровня sustain: ноты склеивались, звук был «плоским». PORTA on = настоящее легато. */
  if (rt_hint || !eff[P_NPO]) v->rs = 1; }
 else memset(v, 0, sizeof *v);
 v->rng = 0x9E3779B9u * (uint32_t)(trk + 1 + age_ctr * 7); if (!v->rng) v->rng = 1;
 age_ctr++; v->act = 1; v->trk = trk; v->gate = gate; v->note = note; v->vel = vel * (1.f / 127.f); v->lv = lvnote; if (!legato) v->tb = 1.f;   /* legato: the boost is re-armed in voice_run after the restart fade — a gain jump on a sounding note was a click */
 memcpy(v->e, eff, sizeof v->e);
 { const int16_t *tp = W.tr[trk].p; for (int i = P_NPO + 1; i < NP; i++) if (eff[i] != tp[i]) v->lk[i >> 3] |= (uint8_t)(1u << (i & 7)); }
 if (!legato) { v->fcl[0] = LN20 + eff[P_F1C] * (1.f / 127.f) * 6.9077553f; v->fcl[1] = LN20 + eff[P_F2C] * (1.f / 127.f) * 6.9077553f; }
 v->f = 440.f * exp2f((note - 69) * (1.f / 12.f));
 v->fcur = (eff[P_NPO] && last_f[trk] > 0) ? last_f[trk] : v->f; last_f[trk] = v->f;   /* portamento is per track */
 if (eff[P_NLT]) for (int i = 0; i < NLFO; i++) { int m = eff[P_L0M + i * 9]; lfo_on[trk][i] = 1;
  if (m == 1) lfo_holdv[trk][i] = lfo_v[trk][i]; else lph[trk][i] = 0.f; }
 if (!eff[P_NFT]) for (int k = 1; k < 3; k++) { v->env[k] = eff[P_E0S + k * 4] * (1.f / 127.f); v->st[k] = 1; }   /* FLT.T off: filter envs sit on sustain */
 { float x = (float)BL / ((float)WORM_FS * (.01f + eff[P_NPT] * (1.f / 127.f) * .5f) * (1.f / 3.f)); v->gl = x / (1.f + x); }
 return trk;
}
int worm_synth_note_on(const int16_t *eff, int note, int vel, int gate) { return worm_synth_note_on_v(eff, note, vel, gate, 0, 1, -1); }
void worm_synth_note_off(int i) { if (i >= 0 && i < NVOICE && V[i].act && !V[i].rel) rel_voice(&V[i]); }
void worm_synth_note_off_lv(int note) { for (int i = 0; i < NVOICE; i++) if (V[i].act && !V[i].rel && V[i].lv == note) rel_voice(&V[i]); }
void worm_synth_off_trk(int t) { for (int i = 0; i < NVOICE; i++) if (V[i].act && V[i].trk == t) worm_synth_note_off(i); }
int worm_synth_active(int v) { return v >= 0 && v < NVOICE ? V[v].act : 0; }
int worm_synth_active_trk(int t) { int n = 0; for (int i = 0; i < NVOICE; i++) if (V[i].act && V[i].trk == t) n++; return n; }
float worm_synth_env4(void) { for (int i = 0; i < NVOICE; i++) if (V[i].act && V[i].trk == W.pat) return V[i].env[4]; return 0.f; }

/* ------------------------------------------------------------------------------------------ shared buses ---- */
static float BL_L[BL], BR_[BL], BD[BL], BV[BL], BC[BL];   /* dry L/R, delay / reverb / chorus sends */
static int bus_d, bus_v, bus_c;                           /* a send was written this block */
/* Частичные суммы голосов CPU1: [голос][L,R,D,V,C][сэмпл]. По голосу, а не по ядру: так их можно
 * прибавить в исходном порядке 0..NVOICE-1 (сложение float не ассоциативно — иначе звук отличался бы). */
static float PB[NVOICE / 2][5][BL];                       /* по СЛОТУ CPU1 (порядковый номер среди отданных голосов), не по номеру голоса: в 2 раза меньше .bss */
static uint8_t PBf[NVOICE / 2][3];                            /* sends, посчитанные этим голосом */
static WORM_POOL voice_t SM[NVOICE / 2];                  /* снимок голосов CPU1 на случай брошенной задачи (в .pool) */
static uint8_t smp_mask;                                  /* биты голосов (== дорожек), отданных CPU1 */
static int smp_bl;                                        /* длина текущего блока для задачи CPU1 */
static uint32_t vcost[NVOICE];                    /* EMA стоимости голоса за блок, тики; 0 = неизвестно */
static volatile uint32_t vcost_new[NVOICE];       /* замер голоса на CPU1 (пишет задача, читает CPU0 после wait) */
static volatile uint8_t smp_snap;                 /* голоса, чьё состояние задача уже сняла в SM[] (откат при отказе CPU1) */
static int smp_m;                                 /* сколько старших активных голосов ушло на CPU1 в прошлом блоке */
static uint32_t st_a0, st_w1, st_w2;              /* последний блок: свои голоса CPU0, ожидание CPU1 после них, ожидание reverb */

/* one ADSR stage per block; ids 0 attack, 1 decay->sustain, 3 release */
static void env_step(voice_t *v, int k, const int16_t *tp, int t, int bl) {
 const int b = P_E0A + k * 4; const float bs = (float)bl * (1.f / (float)BL);
 float *e = &v->env[k];
 if (v->st[k] == 0) { *e += (float)BL / ((float)WORM_FS * ET((int)(mp(v, tp, t, b) + .5f))) * bs; if (*e >= 1.f) { *e = 1.f; v->st[k] = 1; } }
 else if (v->st[k] == 1) { float x = (float)BL / ((float)WORM_FS * (ET((int)(mp(v, tp, t, b + 1) + .5f)) * (1.f / 3.f))) * bs, c = x / (1.f + x);
  *e += (mp(v, tp, t, b + 2) * (1.f / 127.f) - *e) * c; }
 else { float tr = ET((int)(mp(v, tp, t, b + 3) + .5f)) * (1.f / 3.f); if (tr < .0025f) tr = .0025f;
  float x = (float)BL / ((float)WORM_FS * tr) * bs, c = x / (1.f + x); *e -= *e * c; }
}

/* ------------------------------------------------------------------------------------------ voice ---- */
/* Staged voice: every stage is a tight loop over the block with its state in registers; stages that are off cost nothing. */
#define K31 (1.f / 2147483648.f)
static inline float blep(float t, float dt, float idt) {
 if (t < dt) { t *= idt; return t + t - t * t - 1.f; }
 if (t > 1.f - dt) { t = (t - 1.f) * idt; return t * t + t + t + 1.f; }
 return 0.f;
}
/* VA oscillator: accumulates n samples of gain g (ramping by dg) into o.
 * w 0..5 — формы (sin/tri/saw/pulse/square/noise);
 * w 6..10 — MAIN: 64-кадровые таблицы, pos (0..1) листает кадры, между соседними — линейный морфинг;
 * w 11..57 — USER/LIB: таблица в один цикл, pos = BEND (фазовое искажение ph - k*sin(ph)/2pi: монотонно, концы цикла на месте).
 * pos плавно идёт от pos0 к pos за блок (никаких ступенек при модуляции LFO/ENV).
 * Фаза — uint32 на полный цикл: старшие 8 бит = отсчёт 0..255, следующие 16 — дробь (линейная интерполяция). */
static void gen_va(float *o, int n, uint32_t *php, uint32_t inc, float dt, int w, float pw, float pw0, uint32_t *rngp, float g, float dg) {
 uint32_t ph = *php; const float idt = dt > 1e-6f ? 1.f / dt : 0.f;
 if (w >= WAVE_MAIN0 && w < WAVE_USER0) {
  const int16_t (*T)[WT_N] = worm_wt_main[w - WAVE_MAIN0];
  float pp = pw0; const float dp = (pw - pw0) / (float)n;
  for (int i = 0; i < n; i++) {
   const float fp = fclamp(pp, 0.f, 1.f) * (float)(WT_FRAMES - 1);
   int f0 = (int)fp; const float mf = fp - (float)f0; const int f1 = f0 < WT_FRAMES - 1 ? f0 + 1 : f0;
   const uint32_t x = ph >> 24, x1 = (x + 1u) & 255u; const float fr = (float)((ph >> 8) & 0xFFFFu) * (1.f / 65536.f);
   const float a0 = (float)T[f0][x], a1 = (float)T[f0][x1], b0 = (float)T[f1][x], b1 = (float)T[f1][x1];
   const float s0 = a0 + (a1 - a0) * fr, s1 = b0 + (b1 - b0) * fr;
   o[i] += (s0 + (s1 - s0) * mf) * (1.f / 32768.f) * g;
   g += dg; ph += inc; pp += dp;
  }
  *php = ph; return;
 }
 if (w >= WAVE_USER0) {
  const int16_t *tab = worm_wave_cycle(w);
  float pp = pw0; const float dp = (pw - pw0) / (float)n;
  for (int i = 0; i < n; i++) {
   const float k = fclamp(pp, 0.f, 1.f) * .9f;
   const uint32_t pwp = ph - (uint32_t)(int32_t)(k * psin((int32_t)ph) * 683565275.f);   /* k/(2pi) * 2^32 */
   const uint32_t x = pwp >> 24, x1 = (x + 1u) & 255u; const float fr = (float)((pwp >> 8) & 0xFFFFu) * (1.f / 65536.f);
   const float a = (float)tab[x];
   o[i] += (a + ((float)tab[x1] - a) * fr) * (1.f / 32768.f) * g;
   g += dg; ph += inc; pp += dp;
  }
  *php = ph; return;
 }
 switch (w) {
  case 0: for (int i = 0; i < n; i++) { o[i] += psin((int32_t)ph) * g; g += dg; ph += inc; } break;
  case 1: for (int i = 0; i < n; i++) { o[i] += (2.f * fabsf((float)(int32_t)ph * K31) - 1.f) * g; g += dg; ph += inc; } break;
  case 2: for (int i = 0; i < n; i++) { float x = (float)(int32_t)ph * K31; o[i] += (x - blep(x * .5f + .5f, dt, idt)) * g; g += dg; ph += inc; } break;
  case 5: { uint32_t r = *rngp; for (int i = 0; i < n; i++) { r ^= r << 13; r ^= r >> 17; r ^= r << 5; o[i] += (float)(int32_t)r * K31 * g; g += dg; } *rngp = r; } break;
  default: { const float d = w == 3 ? .5f : .05f + .45f * pw;
   for (int i = 0; i < n; i++) { float t = (float)(int32_t)ph * K31 * .5f + .5f, q = t - d; if (q < 0.f) q += 1.f;
    o[i] += ((t < d ? 1.f : -1.f) + blep(t, dt, idt) - blep(q, dt, idt)) * g; g += dg; ph += inc; } }
 }
 *php = ph;
}
/* ---------------------------------------------------------------- FM: 4 оператора C, A, B1, B2 (как Digitone) ----
 * Топология алгоритмов — worm_fm_algo (worm_params.c, по Appendix A.3). У каждого алгоритма два выхода X и Y; MIX делает между ними
 * кроссфейд (равная мощность). ENV A -> уровень A, ENV B -> уровень B1/B2 (LEV B — макро-уровень обоих); «сплошные» выходы
 * умножаются на этот уровень, «пунктирные» — нет. Амплитуду голоса ведёт общая амп-огибающая (E0). */
static float fm_env(float t, int atk, int dec, int end, int del) {   /* задержка -> атака -> спад к END (DEC=127: без спада) */
 const float td = (float)(del * del) * (2.f / 16129.f); t -= td; if (t < 0.f) return 0.f;
 const float at = (float)(atk * atk) * (3.f / 16129.f); if (t < at) return t / at;
 if (dec >= 127) return 1.f;
 const float e = (float)end * (1.f / 127.f), tau = .005f + (float)(dec * dec) * (4.f / 16129.f);
 return e + (1.f - e) * expf(-(t - at) / tau);
}
static inline float htab(const int16_t *a, const int16_t *b, float fr, uint32_t ph) {
 const uint32_t i = ph >> 25, j = (i + 1) & 127; const float f = (float)(ph & 0x1FFFFFFu) * (1.f / 33554432.f);
 const float x0 = (float)a[i] + (float)(a[j] - a[i]) * f;
 if (fr <= 0.f) return x0 * (1.f / 32768.f);
 const float x1 = (float)b[i] + (float)(b[j] - b[i]) * f;
 return (x0 + (x1 - x0) * fr) * (1.f / 32768.f);
}
typedef struct { uint32_t inc[4]; int al, hop, fade; const int16_t *ha, *hb; float hf, fq, wx, wy, dwx, dwy, la, dla, lb, dlb; } fm4_t;
static void gen_fm4(float *o, int n, uint32_t *ph, float *fbp, const fm4_t *c, float g, float dg) {
 const fmalg_t *A = &worm_fm_algo[c->al];
 unsigned used = A->xd | A->xs | A->yd | A->ys;   /* нужные операторы: выходы и (рекурсивно) их модуляторы */
 for (int r = 0; r < 3; r++) for (int op = 0; op < 4; op++) if (used >> op & 1) used |= A->mod[op];
 const int fo = A->fb; float fb = *fbp, la = c->la, lb = c->lb, wx = c->wx, wy = c->wy, z[4] = {0.f, 0.f, 0.f, 0.f}, y[4] = {0.f, 0.f, 0.f, 0.f};
 uint32_t p[4] = {ph[0], ph[1], ph[2], ph[3]};
 for (int i = 0; i < n; i++) {
  const float lev[4] = {1.f, la, lb, lb};
  for (int k = 0; k < 4; k++) {
   const int op = A->ord[k];
   if (!(used >> op & 1)) continue;
   uint32_t q = p[op]; float m = 0.f; const unsigned mm = A->mod[op];
   for (int j = 0; j < 4; j++) if (mm >> j & 1) m += z[j];
   if (m != 0.f) { float u = m * 1.5f; u -= (float)(int32_t)(u + (u > 0.f ? .5f : -.5f)); q += (uint32_t)(int32_t)(u * 4294967296.f * .999f); }
   if (op == fo && c->fq > 0.f) q += (uint32_t)(int32_t)(fb * c->fq * 2147483648.f * .999f);
   const float v = op == c->hop ? htab(c->ha, c->hb, c->hf, q) : psin((int32_t)q);
   if (op == fo) fb = v;
   y[op] = v; z[op] = v * lev[op];
  }
  float x = 0.f, yy = 0.f;
  for (int op = 0; op < 4; op++) {
   const unsigned b = 1u << op;
   if (A->xd & b) x += y[op]; if (A->xs & b) x += z[op];
   if (A->yd & b) yy += y[op]; if (A->ys & b) yy += z[op];
  }
  o[i] += (x * wx + yy * wy) * g * (c->fade ? (float)(i + 1) / (float)n : 1.f); g += dg;   /* fade: короткий вход на новом голосе — без щелчка со случайной фазы */
  la += c->dla; lb += c->dlb; wx += c->dwx; wy += c->dwy;
  for (int op = 0; op < 4; op++) p[op] += c->inc[op];
 }
 for (int op = 0; op < 4; op++) ph[op] = p[op];
 *fbp = fb;
}
/* TPT state-variable filter; ty 1 LP, 2 HP, 3 BP. out = dry*cd + filtered*cw (cd/cw already carry the stage volume) */
static void svf_stage(float *x, int n, float *st, float a1, float a2, float a3, float k, int ty, float cd, float cw, float dr) {
 float s1 = st[0], s2 = st[1];
#define SVFBODY(Y) for (int i = 0; i < n; i++) { float xi = x[i]; float in = dr > 0.f ? sclip(xi * (1.f + dr)) : xi; \
   float v3 = in - s2, v1 = a1 * s1 + a2 * v3, v2 = s2 + a2 * s1 + a3 * v3; s1 = v1 + v1 - s1; s2 = v2 + v2 - s2; x[i] = xi * cd + (Y) * cw; }
 if (ty == 1) { SVFBODY(v2) } else if (ty == 3) { SVFBODY(v1 * k) } else { SVFBODY(in - k * v1 - v2) }
#undef SVFBODY
 if (!(fabsf(s1) < 16.f && fabsf(s2) < 16.f)) s1 = s2 = 0.f;   /* blow-up guard (checked per block) */
 st[0] = s1; st[1] = s2;
}

/* Фильтр q (0 = F1, 1 = F2) на буфере x: cutoff (+ENV) сглажен в ln-Hz, коэффициенты TPT раз в блок. */
static void flt_run(voice_t *v, const int16_t *tp, int t, int q, float *x, int bl, const float *ev, const float *vl, const float *dr, int first) {
 const int b = q ? P_F2C : P_F1C; int ty = (int)(mp(v, tp, t, b + 2) + .5f); if (ty < 1) return; if (ty > 3) ty = 3;
 const float w = mp(v, tp, t, q ? P_F2W : P_F1W) * (1.f / 127.f); if (w <= 0.f) return;
 float lt = LN20 + mp(v, tp, t, b) * (1.f / 127.f) * 6.9077553f + (mp(v, tp, t, b + 3) - 64.f) * (1.f / 64.f) * 2.7725887f * ev[q + 1];
 lt = fclamp(lt, LN20, 9.8f); v->fcl[q] += (lt - v->fcl[q]) * (first ? 1.f : .84f);
 const float Q = .5f + mp(v, tp, t, b + 1) * (1.f / 127.f) * 18.f, g = tanf(3.14159265f * expf(v->fcl[q]) * (1.f / (float)WORM_FS)), kq = 1.f / Q;
 const float a1 = 1.f / (1.f + g * (g + kq)), a2 = g * a1, a3 = g * a2;
 svf_stage(x, bl, v->svf[q], a1, a2, a3, kq, ty, (1.f - w) * vl[q + 1], w * vl[q + 1], dr[q + 1]);
}

static void voice_run(voice_t *v, int bl, int dst) {
 const int t = v->trk; const int16_t *tp = W.tr[t].p;
 const float ibl = 1.f / (float)bl;
 /* dst 0 = общие шины CPU0; иначе частичная сумма голоса dst-1 в PB[]. */
 float *L = dst ? PB[dst - 1][0] : BL_L, *R = dst ? PB[dst - 1][1] : BR_;
 float *S[3] = { dst ? PB[dst - 1][2] : BD, dst ? PB[dst - 1][3] : BV, dst ? PB[dst - 1][4] : BC };
 if (v->gate >= 0 && !v->rel) { v->gate -= bl; if (v->gate <= 0) rel_voice(v); }
 float ev0[NENV], ev[NENV];
 for (int k = 0; k < NENV; k++) { ev0[k] = v->env[k];
  if (v->rs && !((k == 1 || k == 2) && !v->e[P_NFT])) v->env[k] = 0.f;   /* перезапуск: этот блок — спад к нулю (ev0 -> 0), атака со следующего */
  else env_step(v, k, tp, t, bl);
  ev[k] = v->env[k]; }
 const int rs_now = v->rs; v->rs = 0;
 const int first = !v->init; v->init = 1;
 if (first) for (int k = 0; k < NENV; k++) ev0[k] = ev[k];
 const int al = v->e[P_OA] == ALGO_FM ? ALGO_FM : v->e[P_OA] == ALGO_WT ? ALGO_WT : ALGO_VA;
 float lvl, lvl2;
 { const float xm = al == ALGO_FM ? 0.f : fclamp(mp(v, tp, t, P_OL2) * (1.f / 127.f), 0.f, 1.f);   /* у FM слоты OSC значат другое (worm_fm_slot) */
   if (al == ALGO_FM) { lvl = 100.f / 127.f * .5f; lvl2 = 0.f; }   /* один 4-операторный голос -> тракт OSC1 */
   else { lvl = mp(v, tp, t, P_OL) * (1.f / 127.f) * .5f; lvl2 = al == ALGO_WT ? 0.f : xm * .5f; } }
 const float sub = al == ALGO_FM ? 0.f : mp(v, tp, t, P_OS) * (1.f / 127.f) * .5f,
             sub2 = al != ALGO_VA ? 0.f : mp(v, tp, t, P_OS2) * (1.f / 127.f) * .5f, subs = sub + sub2;
 /* the voice ends when the envelopes are down AND the filters have stopped ringing: a resonant filter keeps sounding after its input is
  * gone, and cutting it at once was an audible click (-40 dBFS with high RESO) */
 const float ring = fabsf(v->svf[0][0]) + fabsf(v->svf[0][1]) + fabsf(v->svf[1][0]) + fabsf(v->svf[1][1]);   /* sum of |states| (fmaxf is not in the project math.h) */
 if (v->rel && ((lvl + sub <= 0.f) || ev[0] < .0005f) && ((lvl2 + sub2 <= 0.f) || ev[3] < .0005f) && ring < 2e-5f) { v->act = 0; return; }
 if (first) { v->lvp[0] = lvl; v->lvp[1] = lvl2; }
 const int on1 = lvl > 0.f || v->lvp[0] > 0.f, on2 = lvl2 > 0.f || v->lvp[1] > 0.f;

 /* pitch */
 const int wmax = al == ALGO_FM ? 3 : al == ALGO_VA ? WAVE_SHAPES - 1 : WAVE_MAX, wmin = al == ALGO_WT ? WAVE_MAIN0 : 0;   /* VA — формы, WT — таблицы */ int wv[2] = {0, 0}; float pw[2] = {0.f, 0.f};
 for (int k = 0; k < 2; k++) { if (k ? !on2 : !on1) continue;
  int wi = (int)(mp(v, tp, t, k ? P_OW2 : P_OW) + .5f); wv[k] = wi < wmin ? wmin : wi > wmax ? wmax : wi;
  pw[k] = fclamp(mp(v, tp, t, k ? P_OP2 : P_OP) * (1.f / 127.f), 0.f, 1.f); if (first) v->pwp[k] = pw[k]; }
 v->fcur += (v->f - v->fcur) * v->gl;
 { const int fm = al == ALGO_FM;
   float exA = fm ? vmod(t, P_VPAL) * 2.f : mp(v, tp, t, P_OO) + (mp(v, tp, t, P_OT) - 64.f) * (1.f / 768.f) + mp(v, tp, t, P_OPI) * (1.f / 12.f), exB = 0.f, det2 = 0.f,
         det = fm ? 0.f : mp(v, tp, t, P_OD) * (1.f / 127.f) * 50.f;
   if (on2) { exB = mp(v, tp, t, P_OO2) + (mp(v, tp, t, P_OT2) - 64.f) * (1.f / 768.f) + mp(v, tp, t, P_OPI2) * (1.f / 12.f); det2 = mp(v, tp, t, P_OD2) * (1.f / 127.f) * 50.f; }
   if (first || exA != v->pk[0] || det != v->pk[1] || exB != v->pk[2] || det2 != v->pk[3]) {
    v->pk[0] = exA; v->pk[1] = det; v->pk[2] = exB; v->pk[3] = det2;
    v->pf[0] = exp2f(exA - det * (1.f / 1200.f)); v->pf[1] = exp2f(exB + det2 * (1.f / 1200.f)); v->pf[2] = exp2f(exA) * .5f; } }
 const float fi = v->fcur * (1.f / (float)WORM_FS);
 const float dA = fclamp(fi * v->pf[0], 0.f, .49f), dB = fclamp(fi * v->pf[1], 0.f, .49f), dS = fclamp(fi * v->pf[2], 0.f, .49f);
 /* per-envelope drive / volume / pan (E0 = OSC1+SUB, E1/E2 = filters, E3 = OSC2) */
 float dr[4], vl[4], pv = 0.f;
 for (int k = 0; k < 4; k++) { const int ix = k < 3 ? P_E0X + k * 3 : P_E3X; dr[k] = mp(v, tp, t, ix) * 8.f * (1.f / 127.f); vl[k] = mp(v, tp, t, ix + 2) * .01f; pv += mp(v, tp, t, ix + 1) - 64.f; }
 pv = fclamp(pv * (1.f / 64.f) + (mp(v, tp, t, P_APAN) - 64.f) * (1.f / 64.f), -1.f, 1.f);

 float X[BL], Y[BL], T[BL]; float *const B[2] = {X, Y};
 memset(X, 0, sizeof(float) * (size_t)bl); memset(Y, 0, sizeof(float) * (size_t)bl);
 /* ---- per oscillator: gen (+SUB) -> drive -> amp envelope -> its own filter (F1 on OSC1+SUB, F2 on OSC2) ---- */
 for (int k = 0; k < 2; k++) {
  const int on = k ? on2 : on1; if (!on) continue;
  const float l0 = v->lvp[k] * 2.f, l1 = (k ? lvl2 : lvl) * 2.f;
  const int e = k ? 3 : 0; const float a0 = ev0[e] * vl[e], a1 = ev[e] * vl[e];
  const int slow = dr[e] > 0.f || (k == 0 && subs > 0.f);
  const uint32_t inc = (uint32_t)(int32_t)((k ? dB : dA) * 4294967296.f);
  float *dst = slow ? T : B[k]; if (slow) memset(T, 0, sizeof(float) * (size_t)bl);
  const float g0 = slow ? l0 : l0 * a0, g1 = slow ? l1 : l1 * a1, dg = (g1 - g0) * ibl;
  if (al != ALGO_FM) gen_va(dst, bl, &v->ph[k], inc, k ? dB : dA, wv[k], pw[k], v->pwp[k], &v->rng, g0, dg);
  else {   /* FM: 4 оператора. Слоты PLIST -> worm_fm_slot[FM_*] */
   #define FS(k_) mp(v, tp, t, worm_fm_slot[k_])
   fm4_t c; const int al4 = (int)(FS(FM_ALGO) + .5f) & 7;
   const int ic = (int)(FS(FM_RC) + .5f), ia = (int)(FS(FM_RA) + .5f), ib = (int)(FS(FM_RB) + .5f);
   const float rc = worm_fm_rc[ic < 0 ? 0 : ic >= FM_NRC ? FM_NRC - 1 : ic], ra = worm_fm_ra[ia < 0 ? 0 : ia >= FM_NRA ? FM_NRA - 1 : ia];
   const int ib1 = ib < 0 ? 0 : ib >= FM_NRB * FM_NRB ? FM_NRB * FM_NRB - 1 : ib;
   float r[4] = {rc + (FS(FM_OC) - 64.f) * (1.f / 16.f), ra + (FS(FM_OA) - 64.f) * (1.f / 16.f),
                 worm_fm_rb[ib1 / FM_NRB] + (FS(FM_OB1) - 64.f) * (1.f / 16.f), worm_fm_rb[ib1 % FM_NRB] + (FS(FM_OB2) - 64.f) * (1.f / 16.f)};
   const float dt = FS(FM_DTUN) * (1.f / 127.f) * (30.f / 1200.f), pab = vmod(t, P_VPAB) * 2.f;
   const float mul[4] = {1.f, exp2f(dt + pab), exp2f(-dt), exp2f(dt * .5f + pab)};
   for (int q = 0; q < 4; q++) { float x = dA * (r[q] < 0.f ? 0.f : r[q]) * mul[q]; c.inc[q] = (uint32_t)(int32_t)(fclamp(x, 0.f, .49f) * 4294967296.f); }
   /* огибающие A / B: время от триггера; TRG выкл — легато не перезапускает, RST выкл — перезапуск без задержки */
   const int dl_[2] = {(int)(FS(FM_ADEL) + .5f), (int)(FS(FM_BDEL) + .5f)};
   const int tg_[2] = {(int)(FS(FM_ATRG) + .5f), (int)(FS(FM_BTRG) + .5f)}, rs_[2] = {(int)(FS(FM_ARST) + .5f), (int)(FS(FM_BRST) + .5f)};
   const int ph_ = (int)(FS(FM_PHRT) + .5f); static const uint8_t PM[5] = {0, 15, 1, 14, 10};
   if (first || rs_now) {   /* фазы сбрасываем только на новом голосе: на ретриггере сброс посреди звука даёт щелчок */
    for (int q = 0; q < 2; q++) if (first || tg_[q]) v->ft[q] = first || rs_[q] ? 0.f : (float)(dl_[q] * dl_[q]) * (2.f / 16129.f);
    const uint32_t hs = v->rng + (uint32_t)v->note * 40503u;   /* детерминированно: rng задаётся при note-on */
    for (int q = 0; q < 4; q++) { uint32_t h = (hs + (uint32_t)q * 0x9E3779B9u) * 2246822519u; h ^= h >> 15; if (!first) continue; if (PM[ph_ > 4 ? 0 : ph_] >> q & 1) v->ph[q] = 0; else v->ph[q] = h; }
   }
   const float dts = (float)bl * (1.f / (float)WORM_FS);
   float e0[2], e1[2];
   for (int q = 0; q < 2; q++) { const int b = q ? FM_ATKB : FM_ATKA;
    const int at = (int)(FS(b) + .5f), dc = (int)(FS(b + 1) + .5f), en = (int)(FS(b + 2) + .5f);
    e0[q] = fm_env(v->ft[q], at, dc, en, dl_[q]); v->ft[q] += dts; e1[q] = fm_env(v->ft[q], at, dc, en, dl_[q]); }
   const float lA = FS(FM_LEVA) * (1.f / 127.f), lB = FS(FM_LEVB) * (1.f / 127.f);
   /* уровни A/B идут от конца прошлого блока к концу этого: скачок огибающей (ретриггер, ручка LEV) не слышен как щелчок */
   c.la = first ? e0[0] * lA : v->fl[0]; c.lb = first ? e0[1] * lB : v->fl[1]; v->fl[0] = e1[0] * lA; v->fl[1] = e1[1] * lB;
   c.dla = (v->fl[0] - c.la) * ibl; c.dlb = (v->fl[1] - c.lb) * ibl; c.fade = first;
   { const float m1 = fclamp(FS(FM_MIX) * (1.f / 127.f), 0.f, 1.f), m0 = first ? m1 : v->fmx; v->fmx = m1;   /* MIX: кроссфейд X<->Y, равная мощность; плавно по блоку */
     c.wx = sqrtf(1.f - m0); c.wy = sqrtf(m0); c.dwx = (sqrtf(1.f - m1) - c.wx) * ibl; c.dwy = (sqrtf(m1) - c.wy) * ibl; }
   c.al = al4;
   c.fq = fclamp(FS(FM_FDBK) * (1.f / 127.f) * .9f, 0.f, .9f);
   { const int h = (int)(FS(FM_HARM) + (FS(FM_HARM) < 0.f ? -.5f : .5f)); const int hh = h < 0 ? -h : h; const float fr = (float)(hh > 52 ? 52 : hh) * .5f; int i0 = (int)fr; if (i0 > FM_NH - 1) i0 = FM_NH - 1;
    const int i1 = i0 + 1 < FM_NH ? i0 + 1 : i0; c.hop = h > 0 ? 0 : h < 0 ? 1 : -1; c.ha = worm_fm_harm[i0]; c.hb = worm_fm_harm[i1]; c.hf = fr - (float)i0; }
   gen_fm4(dst, bl, v->ph, &v->fbm[0], &c, g0, dg);
   #undef FS
  }
  if (slow) {   /* SUB (OSC1 only) -> drive -> envelope */
   if (k == 0 && subs > 0.f) { uint32_t ps = v->sph; const uint32_t is = (uint32_t)(int32_t)(dS * 4294967296.f); for (int i = 0; i < bl; i++) { T[i] += psin((int32_t)ps) * subs; ps += is; } v->sph = ps; }
   if (dr[e] > 0.f) for (int i = 0; i < bl; i++) T[i] = sclip(T[i] * (1.f + dr[e]));
   float a = a0; const float da = (a1 - a0) * ibl; for (int i = 0; i < bl; i++) { B[k][i] += T[i] * a; a += da; }
  }
 /* ---- filters: cutoff (+ENV) smoothed in ln-Hz, TPT coefficients once per block ---- */
  flt_run(v, tp, t, k, B[k], bl, ev, vl, dr, first); }
 if (al == ALGO_FM) flt_run(v, tp, t, 1, X, bl, ev, vl, dr, first);   /* FM: тракт OSC2 выключен, F2 идёт последовательно после F1 */
 for (int i = 0; i < bl; i++) X[i] += Y[i];
 /* ---- amp distortion ---- */
 const int ads = (int)(mp(v, tp, t, P_ADS) + .5f); const float adw = mp(v, tp, t, P_ADW) * (1.f / 127.f);
 if (ads && adw > 0.f) { const float adrv = mp(v, tp, t, P_ADR), g = 1.f + adrv * (1.f / 127.f) * (ads == 1 ? 3.f : ads == 2 ? 10.f : 30.f), dw = 1.f - adw;
  if (ads == 3) for (int i = 0; i < bl; i++) X[i] = X[i] * dw + fclamp(sclip(X[i] * g) * 1.3f + .1f, -.6f, .9f) * adw;
  else for (int i = 0; i < bl; i++) X[i] = X[i] * dw + sclip(X[i] * g) * adw; }
 /* ---- output: VOL / PAN / sends, with the transient boost folded into the gain ramps ---- */
 const float trn = mp(v, tp, t, P_TRN) * (1.f / 127.f);
 const float G = v->vel * chv[t] * mp(v, tp, t, P_AVOL) * (1.f / 127.f);
 /* TRANSIENT: boost = 2*tb - .3*(1-tb), tb decays by .86 per 64 samples. It used to step once per block (a staircase of 1.45 ms,
  * a buzz at the block rate); now tb decays per sample, so the gain curve is smooth */
 static float TG[BL];
 if (trn > 0.f) { const float ka = 1.f - .3f * trn, kb = 2.3f * trn; float tbi = v->tb;
  for (int i = 0; i < bl; i++) { tbi *= TBR; TG[i] = ka + kb * tbi; } v->tb = tbi; }
 else { for (int i = 0; i < bl; i++) TG[i] = 1.f; v->tb *= .86f; }
 const float tg[5] = {G * (pv > 0.f ? 1.f - pv : 1.f), G * (pv < 0.f ? 1.f + pv : 1.f), mp(v, tp, t, P_SDL) * (1.f / 127.f), mp(v, tp, t, P_SRV) * (1.f / 127.f), mp(v, tp, t, P_SCH) * (1.f / 127.f)};
 if (first) for (int i = 0; i < 5; i++) v->rp[i] = tg[i];
 { float gl = v->rp[0], gr = v->rp[1]; const float dl = (tg[0] - gl) * ibl, dr_ = (tg[1] - gr) * ibl;
   const int ti = t - tap_base; const int tapt = (tap_mode == 1 || tap_mode == 2) && ti >= 0 && ti < TAPN;
   float *tl = tapt ? TL[ti] : NULL, *tr = tapt ? TRK_R[ti] : NULL;
   for (int i = 0; i < bl; i++) { float y = X[i] * TG[i], l = y * gl, r = y * gr; L[i] += l; R[i] += r; if (tl) { tl[i] += l; tr[i] += r; } gl += dl; gr += dr_; } }
 { for (int q = 0; q < 3; q++) { const float s0 = v->rp[2 + q], s1 = tg[2 + q]; if (s0 == 0.f && s1 == 0.f) continue;
    if (dst) PBf[dst - 1][q] = 1;
    else { if (q == 0) bus_d = 1; else if (q == 1) bus_v = 1; else bus_c = 1; }
    float g = s0; const float d = (s1 - g) * ibl; float *B = S[q]; for (int i = 0; i < bl; i++) { B[i] += X[i] * TG[i] * g; g += d; } } }
 for (int i = 0; i < 5; i++) v->rp[i] = tg[i];
 v->lvp[0] = lvl; v->lvp[1] = lvl2; if (on1) v->pwp[0] = pw[0]; if (on2) v->pwp[1] = pw[1];
 if (rs_now) v->tb = 1.f;   /* legato restart: the fade block is over, the new note gets its transient (the signal is ~0 here, no step) */
}

/* ------------------------------------------------------------------------------------------ FX ---- */
#define DLY_N 22050                    /* 0.5 s */
static WORM_POOL float dly_buf[DLY_N]; static int dly_w; static float dly_lp; static int dly_tail;
#define RC0 1116
#define RC1 1277
#define RC2 1422
#define RC3 1557
#define RA0 556
#define RA1 341
static WORM_POOL float rv_c[RC0 + RC1 + RC2 + RC3], rv_a[RA0 + RA1];
static int rv_ci[4], rv_ai[2]; static float rv_lp[4]; static int rv_tail;
static const int rv_len[4] = {RC0, RC1, RC2, RC3}, rv_off[4] = {0, RC0, RC0 + RC1, RC0 + RC1 + RC2};
#define CH_N 2048
static WORM_POOL float ch_buf[CH_N]; static int ch_w, ch_tail; static uint32_t ch_ph;
static int clk_n; static float clk_f, clk_v; static uint32_t clk_ph;
static float dcx[2], dcy[2], lim_g = 1.f, lim_pk = 0.f, lim_gmin = 1.f; static int lim_hold;   /* лимитер: удерживаемый пик, остаток удержания (сэмплы), самое низкое усиление с прошлого опроса */
int worm_synth_lim_take(void) { const float g = lim_gmin; lim_gmin = 1.f; return g >= .999f ? 0 : (int)(-logf(g) * 86.8589f + .5f); }   /* самое сильное сжатие лимитера с прошлого вызова, 0.1 дБ (экран BPM, LIM) */

void worm_synth_click(int acc, int vol) { clk_n = 2200; clk_f = acc ? 1500.f : 1000.f; clk_v = vol * (1.f / 127.f) * .4f; clk_ph = 0; }

void worm_synth_init(void) {
 age_ctr = 0;   /* воспроизводимость: rng голосов (фазы FM при PHRT OFF) не зависит от прошлых прогонов */
 memset(V, 0, sizeof V); memset(aut_on, 0, sizeof aut_on); memset(lfo_mod, 0, sizeof lfo_mod); memset(lph, 0, sizeof lph); memset(lfo_v, 0, sizeof lfo_v);
 for (int t = 0; t < NTRK; t++) { aut_key_step[t] = -1; chv[t] = 1.f; last_f[t] = 0.f; edst[t] = -1; for (int i = 0; i < NLFO; i++) { lfo_on[t][i] = 1; lfo_holdv[t][i] = 0.f; ldst[t][i] = -1; ldx[t][i][0] = ldx[t][i][1] = -1; } }
 memset(dly_buf, 0, sizeof dly_buf); memset(rv_c, 0, sizeof rv_c); memset(rv_a, 0, sizeof rv_a); memset(ch_buf, 0, sizeof ch_buf);   /* .pool is not cleared */
 memset(rv_ci, 0, sizeof rv_ci); memset(rv_ai, 0, sizeof rv_ai); memset(rv_lp, 0, sizeof rv_lp);
 memset(vcost, 0, sizeof vcost); smp_m = 0;
 dly_w = 0; dly_lp = 0.f; dly_tail = rv_tail = ch_tail = 0; ch_w = 0; ch_ph = 0; clk_n = 0; dcx[0] = dcx[1] = dcy[0] = dcy[1] = 0.f; lim_g = 1.f; lim_pk = 0.f; lim_hold = 0; lim_gmin = 1.f;
 worm_patch_default(PP);
}

/* FX parameter with the LFO modulation of every track (full-range, like every other DEST) */
static float fxp(int id) {
 float m = 0.f;
 for (int t = 0; t < NTRK; t++) for (int i = 0; i < NLFO; i++) if (ldst[t][i] == id) m += lfo_v[t][i];
 return fclamp(PP[id] + m * (float)(worm_pinfo[id].mx - worm_pinfo[id].mn), (float)worm_pinfo[id].mn, (float)worm_pinfo[id].mx);
}

/* ------------------------------------------------------------------------------------ два ядра ----
 * Блок (64 сэмпла) делится на две фазы, между которыми ядра синхронизируются (job + wait):
 *   1) голоса: старшие активные — на CPU1 (в свои PB[]), остальные на CPU0;
 *   2) FX: reverb (самый тяжёлый, своё состояние) — на CPU1; delay, chorus и слияние шин — на CPU0;
 *      потом CPU0 складывает возвраты в исходном порядке и ведёт мастер/лимитер (последовательная часть).
 * Порядок сложения float везде тот же, что на одном ядре, поэтому звук совпадает бит в бит при любом разбиении.
 * Разбиение голосов подбирается по ЗАМЕРЕННОЙ стоимости (таймер 24 МГц, EMA на голос), без таймера — поровну. */
uint32_t (*worm_clk)(void);                       /* тики для замера (plat_ticks24); NULL — баланс по числу голосов */

/* возврат reverb (CPU1 пишет, CPU0 читает после wait) и возвраты delay / chorus (CPU0) */
static float RVL[BL], RVR[BL], FDL[BL], CVL[BL];
static float rv_rfb, rv_rdm, rv_rmx; static int rv_bl;   /* параметры блока для задачи reverb (ставит CPU0 до job) */
/* откат reverb: блок пишет по 64 позиции в каждой из 4 comb и 2 allpass, начиная с их индексов */
static float RVS[6][BL]; static float rvs_lp[4]; static int rvs_ci[4], rvs_ai[2]; static volatile uint8_t rv_snap;

static void rv_save(int bl) {
 for (int c = 0; c < 4; c++) { const float *b = rv_c + rv_off[c]; int p = rv_ci[c]; rvs_ci[c] = p; rvs_lp[c] = rv_lp[c];
  for (int k = 0; k < bl; k++) { RVS[c][k] = b[p]; if (++p >= rv_len[c]) p = 0; } }
 for (int a = 0; a < 2; a++) { const float *b = rv_a + (a ? RA0 : 0); int p = rv_ai[a]; rvs_ai[a] = p;
  for (int k = 0; k < bl; k++) { RVS[4 + a][k] = b[p]; if (++p >= (a ? RA1 : RA0)) p = 0; } }
}
static void rv_restore(int bl) {
 for (int c = 0; c < 4; c++) { float *b = rv_c + rv_off[c]; int p = rvs_ci[c]; rv_ci[c] = p; rv_lp[c] = rvs_lp[c];
  for (int k = 0; k < bl; k++) { b[p] = RVS[c][k]; if (++p >= rv_len[c]) p = 0; } }
 for (int a = 0; a < 2; a++) { float *b = rv_a + (a ? RA0 : 0); int p = rvs_ai[a]; rv_ai[a] = p;
  for (int k = 0; k < bl; k++) { b[p] = RVS[4 + a][k]; if (++p >= (a ? RA1 : RA0)) p = 0; } }
}

/* reverb блока: 2 allpass-диффузора, 4 comb с затуханием, стерео из суммы / разности comb -> RVL / RVR */
static void rv_core(int bl, int snap) {
 if (snap) { rv_save(bl); rv_snap = 1; }
 const float rfb = rv_rfb, rdm = rv_rdm, rmx = rv_rmx;
 for (int s = 0; s < bl; s++) { float in = BV[s] * .15f, sum = 0.f, dif = 0.f;
  for (int a = 0; a < 2; a++) { float *buf = rv_a + (a ? RA0 : 0); int *ix = &rv_ai[a]; float bo = buf[*ix]; float y = bo - in; buf[*ix] = in + bo * .5f; in = y; if (++*ix >= (a ? RA1 : RA0)) *ix = 0; }
  for (int c = 0; c < 4; c++) { float *buf = rv_c + rv_off[c]; int *ix = &rv_ci[c]; float o = buf[*ix];
   rv_lp[c] += (1.f - rdm) * (o - rv_lp[c]); buf[*ix] = in + rv_lp[c] * rfb + 1e-18f; if (++*ix >= rv_len[c]) *ix = 0;
   sum += o; dif += (c & 1) ? -o : o; }
  RVL[s] = sum * .5f * rmx; RVR[s] = dif * .5f * rmx; }
}
static void rv_job(uint32_t arg) { (void)arg; rv_core(rv_bl, 1); }

/* голоса CPU1: снимок состояния (для отката), нули в свои PB[], расчёт, замер. Всё на CPU1 — CPU0 это время не тратит. */
static void smp_voices(int snap) {
 int j = 0;
 for (int i = 0; i < NVOICE; i++) if (smp_mask & (1u << i)) {
  const int k = j++;                                    /* слот: порядковый номер среди голосов CPU1 (SM[k], PB[k]) */
  if (snap) { SM[k] = V[i]; smp_snap = (uint8_t)(smp_snap | (1u << i)); }
  memset(PB[k], 0, sizeof PB[k]); memset(PBf[k], 0, sizeof PBf[k]);
  const uint32_t t0 = worm_clk ? worm_clk() : 0;
  voice_run(&V[i], smp_bl, k + 1);
  if (worm_clk) vcost_new[i] = worm_clk() - t0;
 }
}
static void smp_job(uint32_t arg) { (void)arg; smp_voices(1); }

/* Сколько СТАРШИХ активных голосов отдать CPU1: минимум max(CPU0, CPU1) по оценкам стоимости. Только старшие (суффикс) —
 * тогда суммы CPU1 прибавляются после голосов CPU0, в исходном порядке. Неизвестная стоимость = среднее известных;
 * без замеров все голоса равны (поровну). Гистерезис 10 %: разбиение не дёргается от шума замеров. */
static int smp_pick(const int *act, int n) {
 uint32_t c[NVOICE], sum = 0; int known = 0;
 for (int q = 0; q < n; q++) { c[q] = vcost[act[q]]; if (c[q]) { sum += c[q]; known++; } }
 const uint32_t avg = known ? sum / (uint32_t)known : 1u;
 sum = 0; for (int q = 0; q < n; q++) { if (!c[q]) c[q] = avg; sum += c[q]; }
 const int cap = n < NVOICE / 2 ? n : NVOICE / 2;     /* CPU1 не больше половины: так хватает SM[] и CPU0 остаётся FX */
 uint32_t pred[NVOICE / 2 + 1], hi = 0; pred[0] = sum;
 for (int m = 1; m <= cap; m++) { hi += c[n - m]; const uint32_t lo = sum - hi; pred[m] = lo > hi ? lo : hi; }
 int bm = 0; for (int m = 1; m <= cap; m++) if (pred[m] < pred[bm]) bm = m;
 const int cur = smp_m < cap ? smp_m : cap;
 if (bm != cur && pred[bm] * 10u >= pred[cur] * 9u) bm = cur;
 return bm;
}

void worm_synth_smp_info(int *m, uint32_t *a0, uint32_t *w1, uint32_t *w2) {   /* для отладки: разбиение и ожидания CPU0 (тики 24 МГц) */
 if (m) *m = smp_m; if (a0) *a0 = st_a0; if (w1) *w1 = st_w1; if (w2) *w2 = st_w2;
}

void worm_synth_render(int16_t *out, int frames) {
 for (int n = 0; n < frames; n += BL) {
  const int bl = frames - n < BL ? frames - n : BL;
  aut_block(); lfo_block(bl);
  for (int t = 0; t < NTRK; t++) chv[t] += (PP[P_C0L + t] * .01f - chv[t]) * .25f;
  memset(BL_L, 0, sizeof(float) * bl); memset(BR_, 0, sizeof(float) * bl); memset(BD, 0, sizeof(float) * bl); memset(BV, 0, sizeof(float) * bl); memset(BC, 0, sizeof(float) * bl);
  bus_d = bus_v = bus_c = 0;
  if (tap_mode == 1 || tap_mode == 2) for (int t = 0; t < TAPN; t++) { memset(TL[t], 0, bl * sizeof(float)); memset(TRK_R[t], 0, bl * sizeof(float)); }
  /* ---- фаза 1: голоса. CPU1 берёт старших из активных (число — smp_pick), остальные считает CPU0 ---- */
  smp_mask = 0; smp_snap = 0;
  int act[NVOICE], n_act = 0, m1 = 0;
  for (int i = 0; i < NVOICE; i++) { if (V[i].act) act[n_act++] = i; else vcost[i] = 0; }
  uint32_t (*const clk)(void) = worm_smp_run ? worm_clk : 0;
  if (worm_smp_run && n_act >= 2) m1 = smp_pick(act, n_act);
  smp_m = m1;
  for (int q = n_act - m1; q < n_act; q++) smp_mask = (uint8_t)(smp_mask | (1u << act[q]));
  smp_bl = bl;
  const int two = smp_mask ? worm_smp_run(smp_job, 0) : 0;       /* 1 = ушла на CPU1; 0 — всё считает CPU0 */
  const uint32_t tA = clk ? clk() : 0;
  for (int q = 0; q < n_act; q++) { const int i = act[q];
   if (two && (smp_mask & (1u << i))) continue;
   const uint32_t t0 = clk ? clk() : 0;
   voice_run(&V[i], bl, 0);
   if (clk) { const uint32_t d = clk() - t0; vcost[i] = vcost[i] ? (vcost[i] * 3u + d) >> 2 : d | 1u; } }
  const uint32_t tB = clk ? clk() : 0;
  if (two) {
   if (worm_smp_wait && worm_smp_wait() != 0) {          /* задача брошена/не доказана: вернуть состояние и пересчитать на CPU0 */
    int j = 0;
    for (int i = 0; i < NVOICE; i++) if (smp_mask & (1u << i)) { if (smp_snap & (1u << i)) V[i] = SM[j]; j++; }
    smp_voices(0);
   } else if (clk) for (int i = 0; i < NVOICE; i++) if (smp_mask & (1u << i)) { const uint32_t d = vcost_new[i]; vcost[i] = vcost[i] ? (vcost[i] * 3u + d) >> 2 : d | 1u; }
  }
  const uint32_t tC = clk ? clk() : 0; st_a0 = tB - tA; st_w1 = tC - tB;
  /* частичные суммы CPU1 — по маске submit, а не по текущему act: голос мог завершить release внутри блока и погаснуть, но его
   * последний вклад всё равно должен попасть в шину. Сначала шина reverb (чтобы CPU1 сразу взял reverb), остальное — пока он считает. */
  if (two) for (int i = 0, k = 0; i < NVOICE; i++) if (smp_mask & (1u << i)) {
   for (int s = 0; s < bl; s++) BV[s] += PB[k][3][s];
   if (PBf[k][1]) bus_v = 1;
   k++;
  }

  /* ---- фаза 2: FX. reverb -> CPU1; delay + chorus + слияние остальных шин -> CPU0 ---- */
  /* reverb: 4 damped combs + 2 allpasses, stereo from comb sum / difference */
  const float rdec = fxp(P_RDC) * (1.f / 127.f), rdmp = fxp(P_RDM) * (1.f / 127.f), rsz = fxp(P_RSZ) * (1.f / 127.f), rmx = fxp(P_RMX) * (1.f / 127.f);
  const float rfb = .7f + rdec * .28f * (.6f + .4f * rsz), rdm = .05f + rdmp * .85f;
  if (bus_v) rv_tail = (int)(3.f * (float)RC3 / (float)WORM_FS / (-logf(rfb)) * (float)WORM_FS) + 2048;
  const int run_v = bus_v || rv_tail > 0; if (!bus_v && rv_tail > 0) rv_tail -= bl;
  rv_rfb = rfb; rv_rdm = rdm; rv_rmx = rmx; rv_bl = bl; rv_snap = 0;
  int two2 = 0;
  if (run_v) { two2 = worm_smp_run ? worm_smp_run(rv_job, 0) : 0; if (!two2) rv_core(bl, 0); }
  if (two) for (int i = 0, k = 0; i < NVOICE; i++) if (smp_mask & (1u << i)) {
   for (int s = 0; s < bl; s++) { BL_L[s] += PB[k][0][s]; BR_[s] += PB[k][1][s]; BD[s] += PB[k][2][s]; BC[s] += PB[k][4][s]; }
   if (PBf[k][0]) bus_d = 1;
   if (PBf[k][2]) bus_c = 1;
   k++;
  }

  /* ---- delay: tempo-synced integer tap, one-pole LPF in the feedback path ---- */
  static const float dtt[5] = {.25f, .5f, .75f, 1.f, 2.f};
  float tm = 60.f / W.bpm * dtt[PP[P_XT] < 0 ? 0 : PP[P_XT] > 4 ? 4 : PP[P_XT]]; while (tm > (float)(DLY_N - 2) * (1.f / (float)WORM_FS)) tm *= .5f;
  const int dlen = (int)(tm * (float)WORM_FS) < 1 ? 1 : (int)(tm * (float)WORM_FS);
  const float dfb = fxp(P_XFB) * (1.f / 127.f) * .9f, xmx = fxp(P_XMX) * (1.f / 127.f);
  const float dlpa = fclamp((20.f + fxp(P_XFL) * (1.f / 127.f) * 19000.f) * (PI2 / (float)WORM_FS), .001f, 1.f);
  if (bus_d) { float nrep = dfb > .001f ? fclamp(9.2f / -logf(dfb) + 1.f, 1.f, 40.f) : 1.f; dly_tail = (int)((float)dlen * nrep) + 1024; }
  const int run_d = bus_d || dly_tail > 0; if (!bus_d && dly_tail > 0) dly_tail -= bl;
  if (run_d) for (int s = 0; s < bl; s++) {
   int rp = dly_w - dlen; if (rp < 0) rp += DLY_N; float d = dly_buf[rp]; dly_lp += dlpa * (d - dly_lp);
   dly_buf[dly_w] = BD[s] + dfb * dly_lp + 1e-18f; if (++dly_w >= DLY_N) dly_w = 0; FDL[s] = d * xmx; }
  /* ---- chorus ---- */
  const float cbase = (.005f + fxp(P_CDL) * (1.f / 127.f) * .02f) * (float)WORM_FS, cdep = fxp(P_CDP) * (1.f / 127.f) * .008f * (float)WORM_FS, cmx = fxp(P_CMX) * (1.f / 127.f);
  const uint32_t cinc = (uint32_t)(int32_t)((fxp(P_CRT) * (1.f / 20.f) + .1f) * (1.f / (float)WORM_FS) * 4294967296.f);
  if (bus_c) ch_tail = CH_N;
  const int run_c = bus_c || ch_tail > 0; if (!bus_c && ch_tail > 0) ch_tail -= bl;
  if (run_c) for (int s = 0; s < bl; s++) {
   ch_buf[ch_w] = BC[s]; ch_ph += cinc; float cd = cbase + psin((int32_t)ch_ph) * cdep, cp = (float)ch_w - cd; if (cp < 0.f) cp += (float)CH_N;
   int ci = (int)cp; float cf = cp - (float)ci; float cv = ch_buf[ci & (CH_N - 1)] * (1.f - cf) + ch_buf[(ci + 1) & (CH_N - 1)] * cf; ch_w = (ch_w + 1) & (CH_N - 1);
   CVL[s] = cv * cmx; }

  /* ---- reverb готов: откат состояния, если CPU1 отказал посреди задачи ---- */
  const uint32_t tD = clk ? clk() : 0;
  if (two2 && worm_smp_wait && worm_smp_wait() != 0) {
   if (rv_snap) rv_restore(bl);
   rv_core(bl, 0);
  }
  st_w2 = clk ? clk() - tD : 0;

  /* ---- сложение возвратов в исходном порядке (delay, reverb, chorus, click), мастер, DC-блок ---- */
  float pk = 0.f; static float OL[BL], OR_[BL];
  for (int s = 0; s < bl; s++) {
   float l = BL_L[s], r = BR_[s], fxdl = 0.f, fxdr = 0.f, fxvl = 0.f, fxvr = 0.f, fxcl = 0.f, fxcr = 0.f;
   if (run_d) { const float fd = FDL[s]; l += fd; r += fd; fxdl = fxdr = fd; }
   if (run_v) { fxvl = RVL[s]; fxvr = RVR[s]; l += fxvl; r += fxvr; }
   if (run_c) { fxcl = fxcr = CVL[s]; l += fxcl; r += fxcr; }
   if (clk_n > 0) { clk_ph += (uint32_t)(int32_t)(clk_f * (1.f / (float)WORM_FS) * 4294967296.f); float k = clk_v * psin((int32_t)clk_ph) * ((float)(--clk_n) * (1.f / 2200.f)); l += k; r += k; }
   if (tap_mode) { int ti = out_off + n + s; if (ti < OUTMAX) { int16_t *q = &TAP[ti * 8];
     if (tap_mode != 3) { for (int t = 0; t < TAPN; t++) { q[2 * t] = tap16(TL[t][s]); q[2 * t + 1] = tap16(TRK_R[t][s]); } q[6] = 0; q[7] = 0; }
     else { q[0]=tap16(fxdl); q[1]=tap16(fxdr); q[2]=tap16(fxvl); q[3]=tap16(fxvr); q[4]=tap16(fxcl); q[5]=tap16(fxcr); q[6]=0; q[7]=0; } } }
   l *= worm_master; r *= worm_master;
   { float yl = l - dcx[0] + .9995f * dcy[0]; dcx[0] = l; dcy[0] = yl; l = yl; float yr = r - dcx[1] + .9995f * dcy[1]; dcx[1] = r; dcy[1] = yr; r = yr; }
   OL[s] = l; OR_[s] = r; { float a = fabsf(l), b = fabsf(r); if (a > pk) pk = a; if (b > pk) pk = b; }
  }
  { const float p2 = worm_lfx_block(OL, OR_, bl); if (p2 >= 0.f) pk = p2; }   /* LIVE: эффекты мастера (idle = ничего не меняют) */
  /* ---- limiter: пик блока удерживается ~20 мс (дольше полупериода баса 41 Гц), потом плавно (~120 мс) возвращается к текущему;
   * усиление = потолок / удерживаемый пик. Раньше усиление отпускалось на 2% за блок (~70 мс), т.е. быстрее полупериода баса:
   * оно «качалось» внутри такта волны — THD 3-5% (-35..-26 дБ) на басу, это и были хрипы (в USB-записи, где уровень полный).
   * Атака: за первые 6 сэмплов блока; мягкое колено ловит остаток ---- */
  const float ceil_ = .89f;
  if (pk >= lim_pk) { lim_pk = pk; lim_hold = 880; }
  else if (lim_hold > 0) lim_hold -= bl;
  else { const float k = (float)bl * (1.f / 5300.f); lim_pk += (pk - lim_pk) * (k < 1.f ? k : 1.f); }
  const float need = lim_pk > ceil_ ? ceil_ / lim_pk : 1.f;
  const float g0 = lim_g, g1 = need;
  lim_g = g1; if (g1 < lim_gmin) lim_gmin = g1;
  { const int rl = bl < 6 ? bl : 6; const float gd = (g1 - g0) * (1.f / (float)rl); float g = g0;
   for (int s = 0; s < bl; s++) { if (s < rl) g += gd; else g = g1;
    const float l = sknee(OL[s] * g), r = sknee(OR_[s] * g);
    const int oi = out_off + n + s;
    if (oi < OUTMAX) { OUT24[oi * 2] = (int32_t)(l * 8388607.f); OUT24[oi * 2 + 1] = (int32_t)(r * 8388607.f); }
    if (ms_i16) { out[(n + s) * 2] = (int16_t)(int)(l * 32767.f); out[(n + s) * 2 + 1] = (int16_t)(int)(r * 32767.f); } } }
 }
}
