/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-WORM */
/* WORM-FM1: MIDI-дорожки M1..M4 — полноценные каналы, как звуковые, только вместо синтеза они шлют MIDI по USB.
 *
 * У каждой MIDI-дорожки (W.mt[k], секвенция W.mpats[k]):
 *   - своя секвенция на 128 шагов, свои часы (длина, направление, скорость, свинг, страницы), PROB / COND / mTIME / EUCLID / p-lock;
 *   - CHAN (канал 1..16), PROG (Program Change), BK.MSB / BK.LSB (Bank Select), VOL (CC7), PAN (CC10): шлются при изменении,
 *     при смене паттерна, при новом USB-подключении (worm_midi_resend);
 *   - 4 CC-линии: каждая — CC-номер, BASE и свой LFO (DEPTH / SPEED / WAVE / SYNC / PHASE): CC = BASE + LFO * DEPTH;
 *     линия с DEPTH 0 — просто статический CC (шлётся при смене); p-lock BASE на шаге = CC на этом шаге;
 *   - арпеджио (ARPMOD / ARPSPD / ARPOCT / ARPGAT) для живых нот; звуковое арпеджио тоже можно направить в MIDI (ARP>MIDI).
 * Движок тактируется сэмплами из worm_render (worm_midi_samples / worm_midi_advance) — те же границы блока, что у звука.
 * Расписание — на АБСОЛЮТНОМ времени (mnow — счётчик сэмплов, у события due), как в worm_seq.c: в тихом отрезке ни одного цикла по
 * таблицам нот / отложенных нот / дорожек, а worm_midi_samples говорит секвенсору, через сколько сэмплов ближайшее событие.
 * Платформа принимает сообщения через worm_midi_out (app.c -> plat_midi_send). Всё состояние — в пуле (WORM_POOL). */
#include <string.h>
#include <math.h>
#include "worm.h"

__attribute__((weak)) void worm_midi_out(int st, int d1, int d2) { (void)st; (void)d1; (void)d2; }

#define NMON 32            /* одновременно звучащих MIDI-нот (с учётом аккордов и арпеджио) */
#define NMPD 12            /* отложенные ноты (mTIME) + RETRIG: задержка <= 1 шага, ретриггер гасится новым шагом -> <= 2 на дорожку + запас */
#define LANE_TICK 220      /* CC-линии пересчитываются раз в ~5 мс (220 сэмплов) */
#define PI2 6.2831853f

static const int8_t CHI[5][2] = {{0, 0}, {12, 0}, {7, 0}, {4, 7}, {3, 7}};
#define MP(m, id) (W.mt[m].p[worm_mpos(id)])

/* ---- состояние ---- */
typedef struct { uint32_t due; uint8_t on, chan, note, trk, hold; int16_t lv; } mon_t;  /* звучащая нота: due — момент note-off (hold — держится до отпускания) */
typedef struct { uint32_t due; int32_t gate, rint, vq, rvd; int16_t rleft; uint8_t act, hit, vel, chan, trk, note, qz, chd, ch[4]; } mpd_t;   /* отложенная нота (mTIME) + ретриггер; act — ждёт срока */
typedef struct { int pos, pd, loop; uint32_t due, et; uint8_t live; } mts_t;           /* часы дорожки: due — момент шага, et — такты EUCLID, live — бежала на прошлом блоке */
typedef struct { int8_t n[12]; uint8_t cnt, idx, set; int8_t dir; uint32_t due; uint32_t c; } marp_t;   /* арпеджио дорожки (set=0: таймер не запущен) */
typedef struct { float ph; int last, val; } mlane_t;                                   /* CC-линия: фаза LFO, последнее посланное, текущее значение */
typedef struct { int16_t chan, prog, bm, bl, vol, pan; } mcache_t;                     /* что уже послано (-2 = ничего) */
typedef struct { uint8_t on, trk, note, vel; } mlq_t;                                  /* живое событие из UI */
static WORM_POOL mon_t MON[NMON];
static WORM_POOL mpd_t MPD[NMPD];
static WORM_POOL mts_t MS[NMTRK];
static WORM_POOL marp_t AR[NMTRK];
static WORM_POOL mlane_t LN[NMTRK][NMLANE];
static WORM_POOL mcache_t MC[NMTRK];
static WORM_POOL mlq_t LQ[16];
static volatile uint8_t lq_h, lq_t;
static uint32_t mnow;                                    /* сэмплы MIDI-движка (растёт в worm_midi_advance); все due — абсолютные */
#define MLEFT(t) ((int32_t)((t) - mnow))               /* осталось сэмплов (<= 0: срок наступил); устойчиво к переполнению счётчика */
static uint32_t lane_last; static uint8_t lanes_on;      /* момент последнего пересчёта CC-линий; есть ли хоть одна включённая линия */
static uint32_t rng = 987654321u;
static uint32_t mrnd(void) { rng = rng * 1664525u + 1013904223u; return rng >> 8; }

/* ---- рабочая копия <-> хранение (W.p <-> W.mt[k]) ---- */
static int id_of(int q) { int n = P_AMO - NP; return q < n ? NP + q : q - n < 5 ? P_NR + (q - n) : P_EUH + (q - n - 5); }
int worm_mpid(int q) { return q >= 0 && q < NMP ? id_of(q) : 0; }
int worm_mtrk_def(int k, int q) { return q == worm_mpos(P_MCH) ? k + 1 : worm_pinfo[id_of(q)].def; }
void worm_mtrk_load(int k)
{
    if (k < 0 || k >= NMTRK) return;
    for (int q = 0; q < NMP; q++) W.p[id_of(q)] = W.mt[k].p[q];
    W.p[P_GLN] = W.mt[k].len; W.p[P_GDR] = W.mt[k].dir; W.p[P_GSX] = W.mt[k].spd;
    W.p[P_GSW] = W.mt[k].swg; W.p[P_GPG] = W.mt[k].pg; W.p[P_GVP] = W.mt[k].vp;
}
void worm_mtrk_store(int k)
{
    if (k < 0 || k >= NMTRK) return;
    for (int q = 0; q < NMP; q++) W.mt[k].p[q] = W.p[id_of(q)];
    W.mt[k].len = W.p[P_GLN]; W.mt[k].dir = W.p[P_GDR]; W.mt[k].spd = W.p[P_GSX];
    W.mt[k].swg = W.p[P_GSW]; W.mt[k].pg = W.p[P_GPG]; W.mt[k].vp = W.p[P_GVP];
}
/* значения MIDI-дорожки по умолчанию (канал = номер дорожки); режимы секвенсора — как у новой звуковой */
void worm_mtrk_default(int k)
{
    for (int q = 0; q < NMP; q++) W.mt[k].p[q] = (int16_t)worm_mtrk_def(k, q);
    W.mt[k].len = worm_pinfo[P_GLN].def; W.mt[k].dir = worm_pinfo[P_GDR].def; W.mt[k].spd = worm_pinfo[P_GSX].def;
    W.mt[k].swg = worm_pinfo[P_GSW].def; W.mt[k].pg = worm_pinfo[P_GPG].def; W.mt[k].vp = worm_pinfo[P_GVP].def;
}
int worm_mtrk_is_default(int k)   /* параметры и режимы дорожки совпадают с умолчанием (для упаковки паттерна) */
{
    for (int q = 0; q < NMP; q++) if (W.mt[k].p[q] != worm_mtrk_def(k, q)) return 0;
    return W.mt[k].len == worm_pinfo[P_GLN].def && W.mt[k].dir == worm_pinfo[P_GDR].def && W.mt[k].spd == worm_pinfo[P_GSX].def &&
           W.mt[k].swg == worm_pinfo[P_GSW].def && W.mt[k].pg == worm_pinfo[P_GPG].def && W.mt[k].vp == worm_pinfo[P_GVP].def;
}

pattern_t *worm_sel_pat(void) { return W.mid ? &W.mpats[W.mpat & (NMTRK - 1)] : &W.pats[W.pat]; }

void worm_midi_init(void)
{
    memset(MON, 0, sizeof MON); memset(MPD, 0, sizeof MPD); memset(MS, 0, sizeof MS); memset(AR, 0, sizeof AR);
    memset(LN, 0, sizeof LN); memset(LQ, 0, sizeof LQ);
    lq_h = lq_t = 0; lane_last = mnow; lanes_on = 0;
    for (int i = NP; i < NPALL; i++) W.p[i] = worm_pinfo[i].def;
    for (int k = 0; k < NMTRK; k++) {
        worm_mtrk_default(k);
        MS[k].pd = 1;
        MC[k].chan = MC[k].prog = MC[k].bm = MC[k].bl = MC[k].vol = MC[k].pan = -2;
        for (int l = 0; l < NMLANE; l++) { LN[k][l].last = -1; LN[k][l].val = -1; }
    }
    W.mid = 0; W.mpat = 0;
}

/* ---- вывод ---- */
static void tx(int st, int d1, int d2) { worm_midi_out(st, d1 & 127, d2 & 127); }
static int chan_of(int k) { return (MP(k, P_MCH) - 1) & 15; }
static void note_off_slot(int i) { tx(0x80 | MON[i].chan, MON[i].note, 0); MON[i].on = 0; }
static void mon_on(int trk, int chan, int note, int vel, int gate, int lv)
{
    int i, fr = -1;
    if (note < 0 || note > 127) return;
    for (i = 0; i < NMON; i++) if (MON[i].on && MON[i].chan == chan && MON[i].note == note) note_off_slot(i);   /* та же нота ещё звучит: сначала погасить */
    for (i = 0; i < NMON; i++) if (!MON[i].on) { fr = i; break; }
    if (fr < 0) {   /* таблица полна: выгнать ноту с наименьшим остатком (держимые — последними) */
        int32_t best = 0x7FFFFFFF;
        for (i = 0; i < NMON; i++) { int32_t r = MON[i].hold ? 0x7FFFFFFE : MLEFT(MON[i].due); if (r < best) { best = r; fr = i; } }
        note_off_slot(fr);
    }
    MON[fr].on = 1; MON[fr].chan = (uint8_t)chan; MON[fr].note = (uint8_t)note; MON[fr].trk = (uint8_t)trk; MON[fr].lv = (int16_t)lv;
    MON[fr].hold = gate < 0; MON[fr].due = mnow + (uint32_t)(gate < 1 ? 1 : gate);
    tx(0x90 | chan, note, vel < 1 ? 1 : vel);
}
static int msnap(int n)   /* квантизация по гамме/транспонирование (общие SCALE/ROOT/TRANSP), как у звуковых дорожек */
{
    n += W.p[P_GTR]; int sc = W.p[P_GSC]; if (!sc) return n; int r = W.p[P_GRT];
    for (int o = 0; o < 12; o++) for (int sg = 1; sg >= -1; sg -= 2) {
        int m = (((n + sg * o - r) % 12) + 12) % 12;
        for (int k = 0; k < worm_scale_n[sc]; k++) if (worm_scale_iv[sc][k] == m) return n + sg * o;
    }
    return n;
}
/* нота + аккорд (тип CHORD дорожки и ноты аккорда шага) -> MIDI; lv >= 0 — живая нота (держится до отпускания) */
static void mnote(int trk, int chan, int note, int vel, int gate, int lv, int qz, int chd, const uint8_t *ch4)
{
    int b = qz ? msnap(note) : note;
    mon_on(trk, chan, b, vel, gate, lv);
    for (int c = 0; c < 2; c++) if (CHI[chd][c]) mon_on(trk, chan, b + CHI[chd][c], vel, gate, lv);
    if (ch4) for (int c = 0; c < 4; c++) if (ch4[c]) mon_on(trk, chan, qz ? msnap(ch4[c]) : ch4[c], vel, gate, lv);
}
static void release_lv(int trk, int note) { for (int i = 0; i < NMON; i++) if (MON[i].on && MON[i].trk == trk && MON[i].lv == note) note_off_slot(i); }
void worm_midi_release(int trk, int note) { if (trk >= 0 && trk < NMTRK) release_lv(trk, note); }

/* ---- MUTE ---- */
void worm_mtrk_mute(int k, int on)
{
    if (k < 0 || k >= NMTRK) return;
    on = on ? 1 : 0;
    MP(k, P_MMT) = (int16_t)on;
    if (W.mid && W.mpat == k) W.p[P_MMT] = (int16_t)on;
    if (on) for (int i = 0; i < NMON; i++) if (MON[i].on && MON[i].trk == k) note_off_slot(i);
}
int worm_mtrk_muted(int k) { return k >= 0 && k < NMTRK ? MP(k, P_MMT) != 0 : 0; }
int worm_mtrk_active(int k) { int n = 0; for (int i = 0; i < NMON; i++) if (MON[i].on && MON[i].trk == k) n++; return n; }
int worm_mlane_value(int k, int l) { return k >= 0 && k < NMTRK && l >= 0 && l < NMLANE ? LN[k][l].val : -1; }
int worm_midi_pos(int k) { return k >= 0 && k < NMTRK ? MS[k].pos : 0; }

/* ---- часы дорожек ---- */
static int mrun(int m) { return W.run || (W.cur_run && W.mid && m == W.mpat); }   /* в STEP SEQUENCER без PLAY бежит только курсор выбранной дорожки (без звука) */
static int32_t mstep_samples(int m) { static const float mul[3] = {.5f, 1.f, 2.f}; int s = W.mt[m].spd; if (s < 0 || s > 2) s = 1; return (int32_t)(WORM_FS * 60.f / W.bpm / 4.f / mul[s]); }
static void mreset(int m) { MS[m].loop = 0; MS[m].et = 0; MS[m].pd = 1; MS[m].pos = W.mt[m].dir == 1 ? W.mt[m].len - 1 : 0; MS[m].due = mnow + 1u; MS[m].live = 1; }
static void madv(int m)
{
    int len = W.mt[m].len, d = W.mt[m].dir; if (len < 1) len = 1; int *pos = &MS[m].pos;
    if (d == 0) { if (++*pos >= len) { *pos = 0; MS[m].loop++; } }
    else if (d == 1) { if (--*pos < 0) { *pos = len - 1; MS[m].loop++; } }
    else if (d == 2) {
        *pos += MS[m].pd;
        if (*pos >= len) { MS[m].pd = -1; *pos = len > 1 ? len - 2 : 0; MS[m].loop++; }
        else if (*pos < 0) { MS[m].pd = 1; *pos = len > 1 ? 1 : 0; }
    } else *pos = (int)(mrnd() % (uint32_t)len);
}
void worm_midi_cursor(int on) { if (on) { mreset(W.mpat & (NMTRK - 1)); W.cur = MS[W.mpat & (NMTRK - 1)].pos; } }
void worm_midi_reload(void) { for (int m = 0; m < NMTRK; m++) if (MS[m].pos >= W.mt[m].len) MS[m].pos = 0; }
void worm_midi_reset_track(int m) { if (m >= 0 && m < NMTRK) mreset(m); }

/* ---- шаг ---- */
static void send_cc(int chan, int cc, int v) { if (cc >= 0 && cc <= 127 && v >= 0) tx(0xB0 | chan, cc, v > 127 ? 127 : v); }
static void mfire_step(int m, const step_t *s)
{
    int16_t *p = W.mt[m].p; step_t eu;
    int mt = p[worm_mpos(P_MMT)] != 0;   /* MIDI-мьют глушит звук, но не индикацию: LED мигает тускло */
    if (p[worm_mpos(P_EUH)] > 0) {   /* EUCLID ставит триггеры сам (см. worm_seq.c): на пульсе играет шаг, даже выключенный; вне пульса — тишина */
        if (!worm_euclid_gate(p[worm_mpos(P_EUH)], p[worm_mpos(P_EUO)], p[worm_mpos(P_EUL)], p[worm_mpos(P_EUM)], (int)MS[m].et)) return;
        if (!s->note) {   /* шаг не заполнен: параметры дорожки (NOTE) */
            memset(&eu, 0, sizeof eu); eu.on = 1; eu.note = (uint8_t)p[worm_mpos(P_NR)]; eu.vel = (uint8_t)p[worm_mpos(P_NV)]; eu.len = (uint8_t)p[worm_mpos(P_NL)];
            eu.prob = (uint8_t)p[worm_mpos(P_NPR)]; eu.cond = (uint8_t)p[worm_mpos(P_NCD)];
            eu.rtr = (uint8_t)W.p[P_RTR]; eu.rtl = (uint8_t)W.p[P_RTL]; eu.rtv = (int8_t)W.p[P_RTV]; s = &eu;
        }
    } else if (!s->on) return;
    if ((int)(mrnd() % 127) >= s->prob) return;   /* было «>»: PROB 0 всё равно иногда играл */
    if (!worm_cond_hit(s->cond, (uint32_t)MS[m].loop)) return;
    worm_trig_hit(NTRK + m);   /* MIDI-дорожка тоже мигает своей клавишей мьюта */
    if (mt) return;            /* заглушена: триггер учтён, MIDI не шлём */
    int chan = chan_of(m), qz = p[worm_mpos(P_MQZ)], chd = p[worm_mpos(P_MCD)], k;
    for (k = 0; k < s->nl && k < MAXLK; k++) {   /* p-lock'и, меняющие саму ноту */
        int v = s->lk[k].v;
        switch (s->lk[k].id) { case P_MCH: chan = (v - 1) & 15; break; case P_MQZ: qz = v; break; case P_MCD: chd = v < 0 ? 0 : v > 4 ? 4 : v; break; default: break; }
    }
    for (k = 0; k < s->nl && k < MAXLK; k++) {   /* p-lock'и, шлющие MIDI на этом шаге: банк -> программа -> VOL / PAN / CC-линии */
        int id = s->lk[k].id, v = s->lk[k].v;
        if (id == P_MBM && v >= 0) send_cc(chan, 0, v); else if (id == P_MBL && v >= 0) send_cc(chan, 32, v);
    }
    for (k = 0; k < s->nl && k < MAXLK; k++) {
        int id = s->lk[k].id, v = s->lk[k].v;
        if (id == P_MPG && v >= 0) tx(0xC0 | chan, v, 0);
        else if (id == P_MVL) send_cc(chan, 7, v);
        else if (id == P_MPN) send_cc(chan, 10, v);
        else if (id >= P_M0C && id < P_M0C + NMLANE * 7 && (id - P_M0C) % 7 == 1) {   /* BASE линии: CC этой линии = значение на шаге */
            int l = (id - P_M0C) / 7, cc = p[worm_mpos(P_MLANE(l, 0))], q;
            for (q = 0; q < s->nl && q < MAXLK; q++) if (s->lk[q].id == P_MLANE(l, 0)) cc = s->lk[q].v;   /* CC-номер линии тоже можно залочить */
            send_cc(chan, cc, v);
        }
    }
    int32_t ss = mstep_samples(m);
    int d = (s->mt + MT_MAX) * (int)worm_step_len() / MT_MAX;
    int len = s->len > NLEN - 1 ? NLEN - 1 : s->len;
    for (int i = 0; i < NMPD; i++) if (MPD[i].act && MPD[i].trk == m && MPD[i].hit) MPD[i].act = 0;   /* новый шаг обрывает ретриггер прошлого */
    uint32_t it; int nh = worm_retrig_hits(s, (uint32_t)ss, &it);
    int32_t gate = (int32_t)(worm_len_samples(len, (uint32_t)ss) * .98f); if (gate < 1) gate = 1;
    if (nh > 1) { int32_t g = (int32_t)(it * .65f); if (g < gate) gate = g; }
    k = -1;
    for (int i = 0; i < NMPD; i++) if (!MPD[i].act) { k = i; break; }
    if (k < 0) for (int i = 0; i < NMPD; i++) if (MPD[i].hit && (k < 0 || MPD[i].rleft < MPD[k].rleft)) k = i;   /* полный пул: крадём хвост ретриггера */
    if (k < 0) { mnote(m, chan, s->note, s->vel, gate, -1, qz, chd, s->chord); return; }                         /* иначе сразу, но не теряем */
    mpd_t *P = &MPD[k];
    P->due = mnow + (uint32_t)(d > 0 ? d : 1); P->act = 1; P->gate = gate; P->vel = (uint8_t)s->vel; P->chan = (uint8_t)chan; P->trk = (uint8_t)m;
    P->note = s->note; P->qz = (uint8_t)qz; P->chd = (uint8_t)chd; P->hit = 0;
    P->rint = (int32_t)it; P->rleft = (int16_t)(nh - 1); P->vq = s->vel << 8;
    { int ve = s->vel + s->rtv * 2; if (ve < 1) ve = 1; if (ve > 127) ve = 127; P->rvd = nh > 1 ? ((ve - s->vel) * 256) / (nh - 1) : 0; }
    for (int c = 0; c < 4; c++) P->ch[c] = s->chord[c];
}
static void mpd_hit(mpd_t *d)
{
    int v = (d->vq + 128) >> 8; if (v < 1) v = 1; if (v > 127) v = 127;
    if (!MP(d->trk, P_MMT)) mnote(d->trk, d->chan, d->note, v, d->gate, -1, d->qz, d->chd, d->ch);
    if (d->rleft > 0) { d->rleft--; d->hit = 1; d->act = 1; d->vq += d->rvd; d->due += (uint32_t)d->rint; if (MLEFT(d->due) <= 0) d->due = mnow + 1u; } else { d->act = 0; d->hit = 0; }
}

/* ---- арпеджио живых нот ---- */
static int arp_on(int m) { return MP(m, P_MAM) != 0; }
static int32_t arp_step(int m)
{
    static const float am[5] = {.25f, .5f, 1.f, 2.f, 4.f};
    int s = MP(m, P_MAS); if (s < 0 || s > 4) s = 1;
    return (int32_t)(WORM_FS * 60.f / W.bpm / 4.f / am[s]);
}
static void arp_reset(int m) { AR[m].cnt = 0; AR[m].idx = 0; AR[m].dir = 1; AR[m].set = 0; AR[m].c = 0; }
static void arp_add(int m, int n)
{
    marp_t *a = &AR[m]; int i;
    for (i = 0; i < a->cnt; i++) if (a->n[i] == n) return;
    if (a->cnt < 12) a->n[a->cnt++] = (int8_t)n;
    for (i = 1; i < a->cnt; i++) { int8_t x = a->n[i]; int j = i; while (j > 0 && a->n[j - 1] > x) { a->n[j] = a->n[j - 1]; j--; } a->n[j] = x; }   /* по возрастанию */
}
static void arp_drop(int m, int n)
{
    marp_t *a = &AR[m];
    for (int i = 0; i < a->cnt; i++) if (a->n[i] == n) { for (int j = i; j < a->cnt - 1; j++) a->n[j] = a->n[j + 1]; a->cnt--; if (a->idx >= a->cnt) a->idx = 0; return; }
}
static void arp_tick(int m)
{
    marp_t *a = &AR[m]; int mode = MP(m, P_MAM), n = a->cnt, oct = MP(m, P_MAO);
    if (!mode || !n) return;
    int per = mode == 3 && n > 1 ? 2 * n - 2 : n, s = a->idx % per, b;
    if (mode == 1) b = s;                                  /* UP */
    else if (mode == 2) b = n - 1 - s;                     /* DOWN */
    else if (mode == 3) b = s < n ? s : 2 * n - 2 - s;     /* UPDN: вверх и обратно без повтора крайних */
    else b = (int)(mrnd() % (uint32_t)n);                  /* RND */
    a->idx = (uint8_t)((s + 1) % per);
    int nn = a->n[b] + 12 * (int)((a->c++ / (uint32_t)n) % (uint32_t)(oct < 1 ? 1 : oct));   /* октавы: полный проход по нотам = следующая октава */
    if (MP(m, P_MMT)) return;
    mnote(m, chan_of(m), nn, 100, (int)(arp_step(m) * MP(m, P_MAG) / 127), -1, MP(m, P_MQZ), MP(m, P_MCD), 0);
}

/* ---- живые ноты (UI -> очередь -> render) ---- */
void worm_midi_live(int on, int note, int vel)
{
    uint8_t h = lq_h, nx = (uint8_t)((h + 1) & 15);
    if (nx == lq_t) return;
    LQ[h].on = (uint8_t)on; LQ[h].trk = (uint8_t)(W.mpat & (NMTRK - 1)); LQ[h].note = (uint8_t)note; LQ[h].vel = (uint8_t)vel;
    lq_h = nx;
}
static void live_apply(const mlq_t *q)
{
    int m = q->trk, note = q->note;
    if (q->on) {
        if (MP(m, P_MMT)) return;
        int qz = MP(m, P_MQZ), chd = MP(m, P_MCD);
        if (arp_on(m)) {
            arp_add(m, note);
            if (AR[m].cnt == 1) { AR[m].idx = 0; AR[m].dir = 1; AR[m].set = 0; AR[m].c = 0; arp_tick(m); }   /* первая нота — сразу первый шаг арпеджио */
            return;
        }
        mnote(m, chan_of(m), note, q->vel, -1, note, qz, chd, 0);
    } else {
        if (arp_on(m)) arp_drop(m, note);
        release_lv(m, note);
    }
}
/* нота звукового арпеджио -> MIDI-дорожка trk (ARP>MIDI); gate < 0 — первая нота аккорда, держится до отпускания клавиши */
void worm_midi_arp_note(int trk, int note, int vel, int gate)
{
    if (trk < 0 || trk >= NMTRK || MP(trk, P_MMT)) return;
    mnote(trk, chan_of(trk), note, vel, gate, gate < 0 ? note : -1, MP(trk, P_MQZ), MP(trk, P_MCD), 0);
}

/* ---- CC-линии (LFO) ---- */
static float lane_wave(int w, float p)   /* биполярный -1..1, как LFO синтеза (без TDS) */
{
    p -= floorf(p);
    switch (w) {
    case 0: return 1.f - 4.f * fabsf(p - .5f);
    case 1: return sinf(PI2 * p);
    case 2: return p < .5f ? 1.f : -1.f;
    case 3: return 2.f * p - 1.f;
    case 4: { int k = (int)(p * 8.f); float h = sinf(k * 12.9898f * PI2) * 43758.5453f; h -= floorf(h); return h * 2.f - 1.f; }
    case 5: return expf(-p * 6.f) * 2.f - 1.f;
    default: return p * 2.f - 1.f;
    }
}
float worm_mlane_wave(int w, float p) { return lane_wave(w, p); }   /* форма волны для экрана LFO */
static void lanes_tick(int dt)
{
    static const float bt[7] = {.25f, .5f, 1.f, 2.f, 4.f, 8.f, 16.f};   /* долей на цикл при SYNC: 1/16 .. 4/1 (как у LFO синтеза) */
    for (int m = 0; m < NMTRK; m++) {
        int chan = chan_of(m);
        for (int l = 0; l < NMLANE; l++) {
            int cc = MP(m, P_MLANE(l, 0)); mlane_t *ln = &LN[m][l];
            if (cc < 0) { ln->last = -1; ln->val = -1; continue; }
            int base = MP(m, P_MLANE(l, 1)), depth = MP(m, P_MLANE(l, 2)), spd = MP(m, P_MLANE(l, 3));
            int wave = MP(m, P_MLANE(l, 4)), sy = MP(m, P_MLANE(l, 5)), phs = MP(m, P_MLANE(l, 6));
            float rate;
            if (sy) { int di = (spd + 9) / 19; if (di > 6) di = 6; rate = (W.bpm / 60.f) / bt[di]; }
            else rate = .1f + spd * (1.f / 127.f) * 19.9f;
            ln->ph += dt * rate * (1.f / (float)WORM_FS); ln->ph -= floorf(ln->ph);
            float x = (float)base;
            if (depth) x += lane_wave(wave, ln->ph + phs * (1.f / 127.f)) * (float)depth;
            int v = (int)(x + .5f); if (v < 0) v = 0; if (v > 127) v = 127;
            ln->val = v;
            if (v != ln->last && !MP(m, P_MMT)) { tx(0xB0 | chan, cc, v); ln->last = v; }
        }
    }
}

/* ---- смена CHAN / PROG / BANK / VOL / PAN: шлём при изменении, после смены канала — всё заново ---- */
static void params_tick(void)
{
    for (int m = 0; m < NMTRK; m++) {
        mcache_t *c = &MC[m]; int ch = chan_of(m);
        int pg = MP(m, P_MPG), bm = MP(m, P_MBM), bl = MP(m, P_MBL), vo = MP(m, P_MVL), pn = MP(m, P_MPN);
        if (c->chan != ch) { if (c->chan != -2) { c->prog = c->bm = c->bl = c->vol = c->pan = -2; for (int l = 0; l < NMLANE; l++) LN[m][l].last = -1; } c->chan = (int16_t)ch; }
        if (bm < 0) c->bm = -2; else if (c->bm != bm) { send_cc(ch, 0, bm); c->bm = (int16_t)bm; c->prog = -2; }          /* новый банк вступает со следующим PROG */
        if (bl < 0) c->bl = -2; else if (c->bl != bl) { send_cc(ch, 32, bl); c->bl = (int16_t)bl; c->prog = -2; }
        if (pg < 0) c->prog = -2; else if (c->prog != pg) { tx(0xC0 | ch, pg, 0); c->prog = (int16_t)pg; }
        if (vo < 0) c->vol = -2; else if (c->vol != vo) { send_cc(ch, 7, vo); c->vol = (int16_t)vo; }
        if (pn < 0) c->pan = -2; else if (c->pan != pn) { send_cc(ch, 10, pn); c->pan = (int16_t)pn; }
    }
}
void worm_midi_resend(void)
{
    for (int m = 0; m < NMTRK; m++) {
        MC[m].prog = MC[m].bm = MC[m].bl = MC[m].vol = MC[m].pan = -2;
        for (int l = 0; l < NMLANE; l++) LN[m][l].last = -1;
    }
}

/* ---- блок рендера ---- */
static int any_lane(void)
{
    const int q0 = worm_mpos(P_M0C);
    for (int m = 0; m < NMTRK; m++) for (int l = 0; l < NMLANE; l++) if (W.mt[m].p[q0 + l * 7] >= 0) return 1;
    return 0;
}
void worm_midi_poll(void)
{
    while (lq_t != lq_h) { mlq_t q = LQ[lq_t]; lq_t = (uint8_t)((lq_t + 1) & 15); live_apply(&q); }
    params_tick();
    for (int m = 0; m < NMTRK; m++) { int r = mrun(m); if (r && !MS[m].live) MS[m].due = mnow + 1u; MS[m].live = (uint8_t)r; }   /* фронт запуска без mreset: часы с начала блока */
    lanes_on = (uint8_t)any_lane();
}
/* сэмплов до ближайшего события MIDI-движка (<= 0: уже пора; 0x7FFFFFFF: событий нет) */
int worm_midi_samples(void)
{
    int32_t ch = 0x7FFFFFFF, x;
    for (int m = 0; m < NMTRK; m++) {
        if (mrun(m)) { x = MLEFT(MS[m].due); if (x < ch) ch = x; }
        if (AR[m].cnt && arp_on(m)) {
            if (!AR[m].set) { AR[m].set = 1; AR[m].due = mnow + (uint32_t)arp_step(m); }   /* таймер арпеджио стартует с первого блока после первой ноты */
            x = MLEFT(AR[m].due); if (x < ch) ch = x;
        }
    }
    for (int i = 0; i < NMPD; i++) if (MPD[i].act) { x = MLEFT(MPD[i].due); if (x < ch) ch = x; }
    for (int i = 0; i < NMON; i++) if (MON[i].on && !MON[i].hold) { x = MLEFT(MON[i].due); if (x < ch) ch = x; }
    if (lanes_on) { x = (int32_t)(LANE_TICK - (int32_t)(mnow - lane_last)); if (x < ch) ch = x; }
    return ch;
}
void worm_midi_advance(int n)
{
    int i, m;
    mnow += (uint32_t)n;
    for (i = 0; i < NMON; i++) if (MON[i].on && !MON[i].hold && MLEFT(MON[i].due) <= 0) note_off_slot(i);                   /* ворота нот */
    for (i = 0; i < NMPD; i++) if (MPD[i].act && MLEFT(MPD[i].due) <= 0) mpd_hit(&MPD[i]);                                  /* отложенные ноты (mTIME) */
    for (m = 0; m < NMTRK; m++) {                                                                                           /* часы: шаги */
        if (mrun(m) && MLEFT(MS[m].due) <= 0) {
            int pos = MS[m].pos; int32_t ss = mstep_samples(m);
            if (W.mid && m == W.mpat) W.cur = pos;
            if (W.run) mfire_step(m, &W.mpats[m].s[pos]);
            MS[m].et++; madv(m);
            int sw = W.mt[m].swg; int32_t nx = ss;
            if (MS[m].pos % 2) nx += (int32_t)((sw - 50) / 50.f * ss * .5f); else if (pos % 2) nx -= (int32_t)((sw - 50) / 50.f * ss * .5f);
            MS[m].due += (uint32_t)(nx > 0 ? nx : 1); if (MLEFT(MS[m].due) <= 0) MS[m].due = mnow + 1u;   /* перелёт за границу чанка переносится: темп не плывёт */
        }
        if (AR[m].cnt && arp_on(m) && AR[m].set && MLEFT(AR[m].due) <= 0) {                                                  /* арпеджио */
            int32_t as = arp_step(m); if (as < 1) as = 1;
            arp_tick(m); AR[m].due += (uint32_t)as; if (MLEFT(AR[m].due) <= 0) AR[m].due = mnow + (uint32_t)as;
        }
    }
    if (!lanes_on) lane_last = mnow;
    else if ((int32_t)(mnow - lane_last) >= LANE_TICK) { int dt = (int)(mnow - lane_last); lane_last = mnow; lanes_tick(dt); }   /* CC-LFO */
}

/* ---- транспорт ---- */
void worm_midi_play(void)
{
    for (int m = 0; m < NMTRK; m++) { mreset(m); for (int l = 0; l < NMLANE; l++) if (MP(m, P_MLANE(l, 5))) LN[m][l].ph = 0.f; }   /* SYNC-линии стартуют с начала */
    lane_last = mnow;
}
void worm_midi_stop(void)
{
    for (int i = 0; i < NMON; i++) if (MON[i].on) note_off_slot(i);
    for (int i = 0; i < NMPD; i++) { MPD[i].act = 0; MPD[i].hit = 0; }
    for (int m = 0; m < NMTRK; m++) arp_reset(m);
}
void worm_midi_panic(void)
{
    worm_midi_stop();
    for (int m = 0; m < NMTRK; m++) { int ch = chan_of(m); tx(0xB0 | ch, 123, 0); tx(0xB0 | ch, 120, 0); }
}
