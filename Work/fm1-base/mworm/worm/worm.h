/* WORM-FM1 groovebox core: portable C (no SDK deps) — params, synth, sequencer, UI. */
#ifndef WORM_H
#define WORM_H
#include <stdint.h>
#define WORM_FS 44100
#define NVOICE 6   /* 6 моно-голосов: по одному на дорожку (пула голосов, призраков и юнисона нет) */
#define NSTEP 128
#define NPAT 16
#define NBANK 8     /* банков паттернов: 8 банков x 16 паттернов = 128 паттернов */
#define NTRK 6      /* 6 моно-дорожек (секвенсор + один голос синта на дорожку); хранение: дорожки 1-4 — как раньше, 5-6 — в хвосте записи (worm_store.c) */
#define NLFO 4      /* LFO на дорожку (по 9 параметров: DEST/SPEED/DEPTH/WAVE/MODE/SYNC/SLOPE/SMOOTH/SHIFT) */
#define NMTRK 4     /* MIDI-каналы (дорожки, шлют MIDI по USB): M1..M4 = SEL + белые клавиши шагов 16/15/14/13 */
#define NMLANE 4    /* CC-линии на MIDI-дорожку: у каждой свой CC-номер, база и свой LFO */
#define MAXLK 7     /* p-lock'ов на шаг: 7-й зарезервирован под пресет шага (P_PSTEP) */
#define P_PSTEP 255 /* виртуальный p-lock: номер пресета на этом шаге (вне worm_pinfo[NPALL]) */

/* Большие буферы (worm_t с 16 паттернами, таблицы синта) — в .pool (336 KB),
 * не в .bss (96 KB). На хосте — обычная память. */
#if defined(PLAT_HOST) || defined(FM1_HOST_TEST)
#define WORM_POOL
#else
#define WORM_POOL __attribute__((section(".pool")))
#endif

/* ---- длина ноты (LEN) и микротайминг ----
 * LEN хранится ИНДЕКСОМ 0..NLEN-1 в таблице длин (в шагах дорожки): 0.063 0.125 0.25 0.375 0.5 0.625 0.75 0.875, затем 1..128 шагов (подписи 1/16, 1/8, 1/4, 1/2 = 1, 2, 4, 8 шагов)
 * (по одному). Раньше было 5 значений (1/2/4/8/16 шагов) — старые банки пересчитываются при чтении (worm_len_load).
 * Микротайминг (step_t.mt) — в ПРОЦЕНТАХ шага 1x (-MT_MAX..+MT_MAX; минус — раньше, плюс — позже), раньше был в тиках 1/24 шага. */
#define NLEN 136
#define LEN_DEF 9        /* 2 шага: как прежнее LEN=1 ("1/8") */
#define MT_MAX 100
int      worm_len_x16(int idx);                          /* длина в 1/16 шага */
uint32_t worm_len_samples(int idx, uint32_t step_samples);   /* длина ноты в сэмплах при длине шага step_samples */
int      worm_len_store(int idx);                        /* индекс -> значение для банка (0..4 — старая шкала, иначе idx+5) */
int      worm_len_load(int sv);                          /* значение из банка -> индекс (старые 0..4 -> 1/2/4/8/16 шагов) */
int      worm_mt_ms10(int pct, float bpm);               /* сдвиг в 0.1 мс при темпе bpm (шаг 1x = 1/16) */
/* X(id,name,min,max,def,fmt,list) */
#define PLIST(X) \
 X(NR,"ROOT",24,96,60,F_NOTE,0) X(NV,"VEL",0,127,100,F_NUM,0) X(NL,"LEN",0,NLEN-1,LEN_DEF,F_LIST,L_LEN) X(NPR,"PROB",0,127,127,F_PCT,0) \
 X(NCD,"COND",0,15,0,F_LIST,L_COND) X(NFT,"FLT.T",0,1,1,F_LIST,L_ON) X(NLT,"LFO.T",0,1,1,F_LIST,L_ON) X(NPT,"PORT.T",0,127,45,F_NUM,0) X(NPO,"PORTA",0,1,0,F_LIST,L_ON) \
 X(OA,"ALGO",0,2,0,F_LIST,L_ALGO) X(OW,"WAVE",0,57,2,F_NUM,0) X(OP,"PW",0,127,64,F_NUM,0) X(OT,"TUNE",0,127,64,F_SG,0) X(OO,"OCT",-3,3,0,F_SGN,0) \
 X(OD,"DETUNE",0,127,0,F_NUM,0) X(OS,"SUB",0,127,0,F_NUM,0) X(OL,"LEVEL",0,127,100,F_NUM,0) \
 X(OW2,"WAVE2",0,57,2,F_NUM,0) X(OP2,"PW2",0,127,64,F_NUM,0) X(OT2,"TUNE2",0,127,64,F_SG,0) X(OO2,"OCT2",-3,3,0,F_SGN,0) X(OD2,"DETUNE2",0,127,0,F_NUM,0) X(OS2,"SUB2",0,127,0,F_NUM,0) X(OL2,"LEVEL2",0,127,0,F_NUM,0) \
 X(OPI,"PITCH",-24,24,0,F_SGN,0) X(OPI2,"PITCH",-24,24,0,F_SGN,0) \
 X(FR1,"RAT C",0,17,2,F_FM,0) X(FF1,"RAT B",0,120,24,F_FM,0) X(FE1,"FDBK",0,127,0,F_FM,0) X(FV1,"OFS A",0,127,64,F_FM,0) \
 X(FR2,"RAT A",0,27,3,F_FM,0) X(FF2,"HARM",-52,52,0,F_FM,0) X(FE2,"OFS C",0,127,64,F_FM,0) X(FV2,"OFS B1",0,127,64,F_FM,0) \
 X(WH1,"OFS B2",0,127,64,F_FM,0) X(WF1,"DEL A",0,127,0,F_FM,0) X(WM1,"DEL B",0,127,0,F_FM,0) \
 X(ADW,"D/WET",0,127,76,F_PCT,0) X(ADS,"DIST",0,3,0,F_LIST,L_DIST) X(APAN,"PAN",0,127,64,F_PAN,0) X(AVOL,"VOL",0,127,100,F_NUM,0) X(ADR,"DRIVE",0,127,12,F_NUM,0) \
 X(SDL,"DELAY",0,127,0,F_NUM,0) X(SRV,"REVERB",0,127,0,F_NUM,0) X(SCH,"CHORUS",0,127,0,F_NUM,0) \
 X(XT,"TIME",0,4,1,F_LIST,L_XT) X(XFB,"FDBK",0,127,60,F_NUM,0) X(XFL,"FILT",0,127,50,F_NUM,0) X(XMX,"MIX",0,127,127,F_NUM,0) \
 X(RSZ,"SIZE",0,127,50,F_NUM,0) X(RDC,"DECAY",0,127,50,F_NUM,0) X(RDM,"DAMP",0,127,40,F_NUM,0) X(RMX,"MIX",0,127,127,F_NUM,0) \
 X(CRT,"RATE",0,127,16,F_CRT,0) X(CDP,"DEPTH",0,127,50,F_NUM,0) X(CDL,"DELAY",0,127,40,F_NUM,0) X(CMX,"MIX",0,127,127,F_NUM,0) \
 X(TRN,"TRANS",0,127,0,F_NUM,0) \
 X(F1C,"CUTOFF",0,127,127,F_HZ,0) X(F1R,"RESO",0,127,0,F_NUM,0) X(F1T,"TYPE",0,3,0,F_LIST,L_FT) X(F1E,"ENV",0,127,96,F_SG,0) \
 X(F2C,"CUTOFF",0,127,127,F_HZ,0) X(F2R,"RESO",0,127,0,F_NUM,0) X(F2T,"TYPE",0,3,0,F_LIST,L_FT) X(F2E,"ENV",0,127,64,F_SG,0) \
 X(F1W,"D/WET",0,127,127,F_PCT,0) X(F2W,"D/WET",0,127,127,F_PCT,0) \
 X(L0D,"DEST",0,127,0,F_LIST,L_LD) X(L0S,"SPEED",0,127,20,F_LHZ,0) X(L0P,"DEPTH",0,127,80,F_NUM,0) X(L0W,"WAVE",0,7,1,F_LIST,L_LW) X(L0M,"MODE",0,3,0,F_LIST,L_LM) X(L0Y,"SYNC",0,1,0,F_LIST,L_LY) X(L0Q,"SLOPE",0,127,64,F_NUM,0) X(L0R,"SMOOTH",0,127,0,F_NUM,0) X(L0T,"SHIFT",0,127,64,F_SG,0) \
 X(L1D,"DEST",0,127,0,F_LIST,L_LD) X(L1S,"SPEED",0,127,20,F_LHZ,0) X(L1P,"DEPTH",0,127,80,F_NUM,0) X(L1W,"WAVE",0,7,1,F_LIST,L_LW) X(L1M,"MODE",0,3,0,F_LIST,L_LM) X(L1Y,"SYNC",0,1,0,F_LIST,L_LY) X(L1Q,"SLOPE",0,127,64,F_NUM,0) X(L1R,"SMOOTH",0,127,0,F_NUM,0) X(L1T,"SHIFT",0,127,64,F_SG,0) \
 X(L2D,"DEST",0,127,0,F_LIST,L_LD) X(L2S,"SPEED",0,127,20,F_LHZ,0) X(L2P,"DEPTH",0,127,80,F_NUM,0) X(L2W,"WAVE",0,7,2,F_LIST,L_LW) X(L2M,"MODE",0,3,0,F_LIST,L_LM) X(L2Y,"SYNC",0,1,0,F_LIST,L_LY) X(L2Q,"SLOPE",0,127,64,F_NUM,0) X(L2R,"SMOOTH",0,127,0,F_NUM,0) X(L2T,"SHIFT",0,127,64,F_SG,0) \
 X(L3D,"DEST",0,127,0,F_LIST,L_LD) X(L3S,"SPEED",0,127,20,F_LHZ,0) X(L3P,"DEPTH",0,127,80,F_NUM,0) X(L3W,"WAVE",0,7,0,F_LIST,L_LW) X(L3M,"MODE",0,3,0,F_LIST,L_LM) X(L3Y,"SYNC",0,1,0,F_LIST,L_LY) X(L3Q,"SLOPE",0,127,64,F_NUM,0) X(L3R,"SMOOTH",0,127,0,F_NUM,0) X(L3T,"SHIFT",0,127,64,F_SG,0) \
 X(E0A,"ATK",0,127,0,F_NUM,0) X(E0D,"DEC",0,127,61,F_NUM,0) X(E0S,"SUS",0,127,64,F_NUM,0) X(E0R,"REL",0,127,64,F_NUM,0) \
 X(E1A,"ATK",0,127,0,F_NUM,0) X(E1D,"DEC",0,127,61,F_NUM,0) X(E1S,"SUS",0,127,64,F_NUM,0) X(E1R,"REL",0,127,64,F_NUM,0) \
 X(E2A,"ATK",0,127,0,F_NUM,0) X(E2D,"DEC",0,127,61,F_NUM,0) X(E2S,"SUS",0,127,64,F_NUM,0) X(E2R,"REL",0,127,64,F_NUM,0) \
 X(E3A,"ATK",0,127,0,F_NUM,0) X(E3D,"DEC",0,127,61,F_NUM,0) X(E3S,"SUS",0,127,64,F_NUM,0) X(E3R,"REL",0,127,64,F_NUM,0) \
 X(E4A,"ATK",0,127,0,F_NUM,0) X(E4D,"DEC",0,127,61,F_NUM,0) X(E4S,"SUS",0,127,64,F_NUM,0) X(E4R,"REL",0,127,64,F_NUM,0) X(E4M,"DEST",0,127,0,F_LIST,L_LD) X(E4P,"DEPTH",0,127,64,F_NUM,0) \
 X(E0X,"DRIVE",0,127,0,F_NUM,0) X(E0N,"PAN",0,127,64,F_PAN,0) X(E0V,"VOL",0,127,100,F_NUM,0) X(E1X,"DRIVE",0,127,0,F_NUM,0) X(E1N,"PAN",0,127,64,F_PAN,0) X(E1V,"VOL",0,127,100,F_NUM,0) X(E2X,"DRIVE",0,127,0,F_NUM,0) X(E2N,"PAN",0,127,64,F_PAN,0) X(E2V,"VOL",0,127,100,F_NUM,0) X(E3X,"DRIVE",0,127,0,F_NUM,0) X(E3N,"PAN",0,127,64,F_PAN,0) X(E3V,"VOL",0,127,100,F_NUM,0) \
 X(EUH,"E.HITS",0,16,0,F_NUM,0) X(EUO,"E.OFS",0,15,0,F_NUM,0) X(EUL,"E.LIMIT",1,16,16,F_NUM,0) X(EUM,"E.MUTE",0,1,0,F_LIST,L_ON) \
 X(GSC,"SCALE",0,7,0,F_LIST,L_SCALE) X(GRT,"ROOT",0,11,0,F_LIST,L_NOTE) X(GCH,"CHORD",0,4,0,F_LIST,L_CHORD) X(GTR,"TRANSP",-12,12,0,F_SGN,0) \
 X(GCK,"CLICK",0,1,0,F_LIST,L_ON) X(GMV,"VOL",0,127,60,F_NUM,0) X(GPR,"PRE",0,1,0,F_LIST,L_PRE) X(GSD,"SOUND",0,0,0,F_LIST,L_SND) \
 X(RS0,"-",0,0,0,F_NUM,0) X(RS1,"-",0,8,1,F_NUM,0) X(RS2,"-",0,1,0,F_NUM,0) X(RS3,"-",0,127,30,F_NUM,0) \
 X(GDR,"DIR",0,3,0,F_LIST,L_DIR) X(GLN,"LENGTH",1,128,16,F_NUM,0) X(GSX,"SPEED",0,2,1,F_LIST,L_SPD) X(GSW,"SWING",50,75,50,F_NUM,0) \
 X(GPG,"PAGES",1,8,1,F_NUM,0) X(GVP,"VIEW",1,8,1,F_NUM,0) X(GST,"STEP",1,128,1,F_NUM,0) X(GMT,"mTIME",-MT_MAX,MT_MAX,0,F_SGN,0) X(C0L,"LEVEL",0,127,100,F_NUM,0) X(C1L,"LEVEL",0,127,100,F_NUM,0) X(C2L,"LEVEL",0,127,100,F_NUM,0) X(C3L,"LEVEL",0,127,100,F_NUM,0) X(C4L,"LEVEL",0,127,100,F_NUM,0) X(C5L,"LEVEL",0,127,100,F_NUM,0) X(C6L,"LEVEL",0,127,100,F_NUM,0) X(C7L,"LEVEL",0,127,100,F_NUM,0) \
 X(ARM,"ARPMOD",0,4,0,F_LIST,L_ARP) X(ARS,"ARPSPD",0,4,1,F_LIST,L_XT) X(ARO,"ARPOCT",1,4,1,F_NUM,0) X(ARG,"ARPGAT",1,127,100,F_NUM,0) X(AQZ,"QUANT",0,1,1,F_LIST,L_ON)
enum { 
#define X(id,n,a,b,d,f,l) P_##id,
PLIST(X)
#undef X
 NP };
enum { F_NUM,F_PCT,F_SG,F_SGN,F_LIST,F_NOTE,F_HZ,F_LHZ,F_CRT,F_PAN,F_FRT,F_PGM,F_OFFN,F_FM };   /* F_FM: подпись по таблице FM (worm_fm_fmt) */
/* ---- MIDI-параметры ("виртуальные" id >= NP) ----
 * Живут отдельно от звукового PLIST: массивы звуковых дорожек (W.tr[].p, голоса, pending) остаются размера NP, а память
 * пула не растёт. Рабочая копия (редактируемая MIDI-дорожка) — W.p[id], хранение каждой дорожки — W.mt[k].p[worm_mpos(id)].
 * OFF-значения (-1): PROG / BK.MSB / BK.LSB / VOL / PAN и CC-номер линии: «не слать». Все значения влезают в int8 (p-lock шага). */
#define MLIST(X) \
 X(MCH,"CHAN",1,16,1,F_NUM,0) X(MPG,"PROG",-1,127,-1,F_PGM,0) X(MBM,"BK.MSB",-1,127,-1,F_OFFN,0) X(MBL,"BK.LSB",-1,127,-1,F_OFFN,0) \
 X(MVL,"VOL",-1,127,-1,F_OFFN,0) X(MPN,"PAN",-1,127,-1,F_OFFN,0) X(MQZ,"QUANT",0,1,1,F_LIST,L_ON) X(MCD,"CHORD",0,4,0,F_LIST,L_CHORD) X(MMT,"MUTE",0,1,0,F_LIST,L_ON) \
 X(MAM,"ARPMOD",0,4,0,F_LIST,L_ARP) X(MAS,"ARPSPD",0,4,1,F_LIST,L_XT) X(MAO,"ARPOCT",1,4,1,F_NUM,0) X(MAG,"ARPGAT",1,127,100,F_NUM,0) \
 X(M0C,"CC",-1,127,-1,F_OFFN,0) X(M0B,"BASE",0,127,64,F_NUM,0) X(M0D,"DEPTH",0,127,0,F_NUM,0) X(M0S,"SPEED",0,127,20,F_LHZ,0) X(M0W,"WAVE",0,6,1,F_LIST,L_MLW) X(M0Y,"SYNC",0,1,0,F_LIST,L_LY) X(M0P,"PHASE",0,127,0,F_NUM,0) \
 X(M1C,"CC",-1,127,-1,F_OFFN,0) X(M1B,"BASE",0,127,64,F_NUM,0) X(M1D,"DEPTH",0,127,0,F_NUM,0) X(M1S,"SPEED",0,127,20,F_LHZ,0) X(M1W,"WAVE",0,6,1,F_LIST,L_MLW) X(M1Y,"SYNC",0,1,0,F_LIST,L_LY) X(M1P,"PHASE",0,127,0,F_NUM,0) \
 X(M2C,"CC",-1,127,-1,F_OFFN,0) X(M2B,"BASE",0,127,64,F_NUM,0) X(M2D,"DEPTH",0,127,0,F_NUM,0) X(M2S,"SPEED",0,127,20,F_LHZ,0) X(M2W,"WAVE",0,6,1,F_LIST,L_MLW) X(M2Y,"SYNC",0,1,0,F_LIST,L_LY) X(M2P,"PHASE",0,127,0,F_NUM,0) \
 X(M3C,"CC",-1,127,-1,F_OFFN,0) X(M3B,"BASE",0,127,64,F_NUM,0) X(M3D,"DEPTH",0,127,0,F_NUM,0) X(M3S,"SPEED",0,127,20,F_LHZ,0) X(M3W,"WAVE",0,6,1,F_LIST,L_MLW) X(M3Y,"SYNC",0,1,0,F_LIST,L_LY) X(M3P,"PHASE",0,127,0,F_NUM,0) \
 X(AMO,"TO MIDI",0,4,0,F_LIST,L_AMO)
/* ---- РЕТРИГГЕР шага (NOTE TRIG + EDIT): «виртуальные» id после MIDI-параметров ----
 * Значения живут в самом шаге (step_t.rtr/rtl/rtv), как VEL/LEN/PROB/COND; W.p[id] — значение для новых шагов.
 * В патчи и в p-lock не попадают (формат банка не меняется: на флеше они упакованы в старшие биты байтов шага). */
#define RLIST(X) \
 X(RTR,"RETRIG",0,9,0,F_LIST,L_RTR) X(RTL,"R.LEN",0,5,0,F_LIST,L_RTL) X(RTV,"R.VEL",-63,63,0,F_SGN,0)
enum {
 P__MFIRST = NP-1,
#define X(id,n,a,b,d,f,l) P_##id,
MLIST(X)
RLIST(X)
#undef X
 NPALL };
#define P_IS_RTRG(id) ((id)>=P_RTR&&(id)<=P_RTV)
#define NMP ((P_AMO-NP)+9)   /* значений на MIDI-дорожку: свои (P_MCH..P_M3P) + NOTE (5) + EUCLID (4) от звуковых id */
#define P_MLANE(l,k) (P_M0C+(l)*7+(k))   /* линия l: k = 0 CC, 1 BASE, 2 DEPTH, 3 SPEED, 4 WAVE, 5 SYNC, 6 PHASE */
int worm_mpos(int id);   /* id -> индекс в W.mt[k].p, или -1 (звуковой/глобальный параметр: у MIDI-дорожки его нет) */
typedef struct { const char *name; int16_t mn, mx, def; uint8_t fmt; const char *const *list; } pinfo_t;
#define LD(n) extern const char *const n[];
LD(L_LDIV) LD(L_ON) LD(L_LEN) LD(L_COND) LD(L_ALGO) LD(L_DIST) LD(L_XT) LD(L_FT) LD(L_LD) LD(L_LW) LD(L_LM) LD(L_LY) LD(L_SCALE) LD(L_NOTE) LD(L_CHORD) LD(L_PRE) LD(L_SND) LD(L_MODE) LD(L_DIR) LD(L_SPD) LD(L_ARP) LD(L_MLW) LD(L_AMO) LD(L_RTR) LD(L_RTL)
extern const pinfo_t worm_pinfo[NPALL];
extern const char *const worm_algo_names[9];
/* Движки (P_OA): 0 VA, 1 FM, 2 WAVETABLE (1 OSC / 1 FLT / 1 AMP, таблицы через WAVE 6..57: 5 MAIN по 64 кадра, 7 USER, 40 LIB; см. worm_wt.h).
 * Номера остальных (WT/AT/GS/ELMYRA/HIVE/CHIP/TOY) удалены вместе со старым движком:
 * старый патч с ними играет как VA (worm_algo_fix). */
#define ALGO_VA 0
#define ALGO_FM 1
#define ALGO_WT 2                    /* WAVETABLE: 1 OSC / 1 FLT / 1 AMP */
#define NALGO_ON 3
extern const int8_t worm_algo_list[NALGO_ON];   /* доступные движки в порядке меню ALGO */
int worm_algo_ok(int a);                         /* 1 = движок есть в прошивке */
int worm_algo_fix(int a);                        /* удалённый/неизвестный -> VA */
extern const uint8_t worm_rtr_den[10];           /* RETRIG: знаменатель доли целой ноты (0 = OFF) */
#define NPRESET 32
#define PRS_NVALS 114                      /* OSC+WT+AMP+SEND+TRN+FLT+LFO+ENV: байт на значение (все per-track, кроме NOTE/EUCLID) */
extern const char *worm_preset_name(int i);
extern const char *worm_preset_factory_name(int i);
/* DEST-цели LFO: полный список модулируемых параметров (индекс DEST -> параметр PLIST) */
typedef struct { int id; const char *name; } worm_dest_t;
extern const worm_dest_t worm_dest_list[];
extern const int worm_dest_n;
int worm_dest_ext(int dest_idx,int16_t *ex);    /* составной DEST (ALL OSC PITCH, RATIO ALL, AB LEVEL...): доп. параметры в ex[2], возврат = их число (0 = обычный) */
int worm_dest_index(int param_id);              /* param id -> DEST index, или -1 */
int worm_dest_param(int dest_idx);              /* DEST index -> param id (для synth-модуляции) */
const char *worm_dest_name(int dest_idx,int algo);  /* имя DEST с учётом алгоритма */
const char *worm_dest_short(int dest_idx,int algo); /* короткое имя DEST (<=5 символов) для подписи под ручкой */
/* Виртуальные цели LFO без своего параметра: PITCH ALL / PITCH A+B2 у FM (копятся в lfo_mod[t][NP..]) */
#define P_VPAL NP
#define P_VPAB (NP+1)
#define NVDEST 2
/* Порядок и группы DEST для выбора: algo -> список индексов DEST (idx) и группа каждой строки (grp), возврат = число строк */
#define DEST_VMAX 112
int worm_dest_order(int algo,uint8_t *idx,uint8_t *grp);
const char *worm_dest_grp(int g);                      /* имя группы (<=6 символов) для левой колонки */
const char *worm_dest_item(int dest_idx,int algo);      /* имя строки в списке выбора (без названия группы) */
/* ---- FM (как у Digitone): 4 оператора C / A / B1 / B2, 8 алгоритмов, выходы X и Y (X -> путь OSC1, Y -> путь OSC2) ----
 * Параметры лежат в слотах OSC1/OSC2 PLIST (формат банков / пресетов / p-lock не меняется): worm_fm_slot[k] = id слота. */
enum { FM_ALGO,FM_RC,FM_RA,FM_RB,FM_HARM,FM_DTUN,FM_FDBK,FM_MIX,FM_OC,FM_OA,FM_OB1,FM_OB2,
       FM_ATKA,FM_DECA,FM_ENDA,FM_LEVA,FM_ATKB,FM_DECB,FM_ENDB,FM_LEVB,FM_ADEL,FM_ATRG,FM_ARST,FM_PHRT,FM_BDEL,FM_BTRG,FM_BRST,FM_N };
#define FM_NRC 18
#define FM_NRA 28
#define FM_NRB 11
#define FM_NH  27                       /* ключевых кадров гармоник HARM 0..26 */
extern const uint8_t worm_fm_slot[FM_N];
typedef struct { uint8_t mod[4], xd, xs, yd, ys, fb, ord[4]; } fmalg_t;
extern const fmalg_t worm_fm_algo[8];
extern const float worm_fm_rc[FM_NRC],worm_fm_ra[FM_NRA],worm_fm_rb[FM_NRB];
extern const int16_t worm_fm_harm[FM_NH][128];   /* один период, 128 точек (flash) */
int  worm_fm_of(int id);                        /* id слота -> индекс FM_* или -1 */
int  worm_fm_clamp(int k,int v);                /* значение в границах FM-параметра k */
void worm_fm_range(int k,int *mn,int *mx);
const char *worm_fm_label(int k);               /* подпись под ручкой (<=6) */
void worm_fm_fmt(int k,int v,char *o);          /* значение для экрана */
void worm_fm_defaults(int16_t *p);              /* заводские значения FM в слотах (при переходе VA/WT -> FM и для FM-пресетов) */
void worm_fm_restore(int16_t *p);               /* слоты FM обратно к значениям PLIST (FM -> VA/WT) */
void worm_patch_default(int16_t *p);
float worm_mod_span(int id,float m);            /* модуляция m (сумма LFO/ENV, ±1) -> доля полного диапазона параметра (PITCH/OCT: ±1 октава, квадратичная кривая) */
void worm_preset_load(int16_t *p, int i);       /* keeps globals; сначала пользовательский из NOR, потом заводской */
int  worm_preset_save_cur(int i);               /* сохранить звук текущей дорожки в слот i (NOR) */
extern volatile int worm_prs_busy;              /* 1 во время стирания/записи сектора проекта: пресет шага не читает NOR */
/* Per-track vs глобальные (FX/мастер) параметры. Глобальные = общий FX-процессор
 * (delay/reverb/chorus настройки + мастер-уровни каналов + режимы секвенсора + scale/arp).
 * Всё остальное (OSC/AMP/ADSR/FLT/LFO/SEND) — per-track, хранится в W.tr[t].p. */
int worm_param_is_track(int id);   /* 1 = per-track (звук дорожки), 0 = глобальный */
extern const int16_t *worm_globals(void);   /* указатель на глобальный патч (W.p) для FX-модуляции */
void worm_fmt(int id, int v, char *out);        /* value -> text */
extern const uint8_t worm_scale_iv[8][12]; extern const uint8_t worm_scale_n[8];

/* ---- synth ---- */
void worm_synth_init(void);
int  worm_synth_note_on(const int16_t *eff, int note, int vel, int gate_samples); /* gate<0: hold; returns voice id */
int  worm_synth_note_on_v(const int16_t *eff, int note, int vel, int gate_samples, int trk, int primary, int lvnote); /* trk = канал; primary: MONO -> голос канала; lvnote: live-нота для note_off_lv (-1 = секвенсор/арп) */
void worm_synth_note_off(int voice);
void worm_synth_note_off_lv(int note);           /* выключить ВСЕ голоса live-ноты note (main+chord+unison) */
void worm_synth_off_trk(int t);                  /* погасить (release) все голоса трека t */
void worm_synth_set_patch(const int16_t *p);    /* live params (fx, lfo, master) */
void worm_synth_render(int16_t *out, int frames);/* interleaved stereo */
/* Второе ядро (CPU1): движок голоса делится по индексам V[trk] == trk. Платформа (app.c) ставит
 * эти указатели; без них (хост, старый вызов) всё считается на CPU0 как раньше.
 * run: 1 = задача ушла на CPU1, 0 = хозяин должен сделать её сам.
 * wait: 0 = готово, !=0 = задача брошена — пересчитать её на CPU0 (NULL — ждать нечего). */
extern int (*worm_smp_run)(void (*fn)(uint32_t), uint32_t arg);
extern int (*worm_smp_wait)(void);
extern int worm_smp_on;                          /* 1 = CPU1 запущен и принимает задачи */
extern uint32_t (*worm_clk)(void);                 /* тики для замера стоимости голосов (plat_ticks24); NULL — делить поровну */
void worm_synth_smp_info(int *m, uint32_t *a0, uint32_t *w1, uint32_t *w2);   /* отладка: голосов на CPU1, свои голоса CPU0, ожидание CPU1, ожидание reverb (тики) */
void worm_smp_init(void);                        /* вызывается один раз из app_init */
int  worm_synth_active(int v);
int  worm_synth_active_trk(int t);               /* сколько голосов канала t звучит */
void worm_synth_click(int accent, int vol);
void worm_synth_retrig_hint(int on);             /* 1 = следующий note_on — повторный удар ретриггера (огибающие с нуля) */
float worm_synth_env4(void);                     /* текущее значение E4 (свободного ADSR) активного голоса выбранного трека (0..1) для UI */

/* ---- sequencer ---- */
typedef struct { uint8_t on, note, vel, len, prob, cond; int8_t mt; uint8_t nl; uint8_t chord[4]; struct { uint8_t id; int8_t v; } lk[MAXLK];
                 uint8_t rtr, rtl; int8_t rtv; } step_t;   /* v: все залоченные параметры влезают в int8 (-128..127). rtr/rtl/rtv — ретриггер:
                                                             * rtr = частота (L_RTR, 0 = OFF), rtl = длительность (L_RTL, 0 = как LEN ноты), rtv = наклон velocity (-63..63) */
typedef struct { step_t s[NSTEP]; } pattern_t;
/* automation (запись вращения ручек на лету): точка = (шаг, параметр, значение). Между точками — линейная интерполяция. */
#define NAUT 64   /* бюджет точек automation на трек */
typedef struct { uint8_t step; uint8_t id; int16_t v; } auto_pt_t;
typedef struct {
    int16_t p[NPALL]; float bpm; int bank, pat; int8_t slot; int8_t usb_out; int8_t usb_host; int8_t usb_local; int8_t usb_mode; int8_t theme;   /* theme: 0 = DG1 (оригинал), 1 = DG2, 2 = MD, 3 = MM; usb_out: 0 = MASTER (24 бит/48 кГц), 1 = TRACK 1-3 (6 кан., 16/44.1), 2 = TRACK 4-6 (6 кан.), 3 = FX (6 кан.); usb_mode: 0 = MASS STORAGE, 1 = AUDIO+MIDI */ pattern_t pats[NTRK];   /* pat — выбранная ДОРОЖКА (0..3); slot — текущий ПАТТЕРН банка (0..NPAT-1): снимок всех дорожек, звука и FX */
   /* RAM: живой снимок текущего паттерна; остальные паттерны банка — в образе банка (worm_store.c) */
    int run, cur, loop, pos, pd; uint32_t samp_to_step; uint32_t tick;
    /* Каждый канал (дорожка) — ПОЛНЫЙ звуковой тракт per-track:
     *   TRACK -> SEQ -> OSC -> AMP -> ADSR1 -> FLT1 -> FLT2 -> ADSR2 -> SENDS -> MASTER.
     * p[NP] — звуковой патч дорожки (OSC1/OSC2, AMP, ADSR1/2, FLT1/2, LFO, SEND-уровни).
     * len/dir/spd/swg/pg/vp — режимы секвенсора (per-track).
     * aut[]/naut — записанные вращения ручек (automation) этого трека. */
    struct { int16_t p[NP]; int16_t len, dir, spd, swg, pg, vp; int16_t mute; auto_pt_t aut[NAUT]; int16_t naut; } tr[NTRK];
    int cur_run;   /* бегунок STEP SEQUENCER (REC): позиция бегает, звука нет */
    int trk_step[NTRK];   /* текущий шаг каждого трека (для automation-интерполяции в голосовом цикле) */
    uint32_t beat_ph;   /* фаза четверти (0..0xFFFFFFFF): для BPM-мигания LED, sample-accurate */
    /* MIDI-дорожки M1..M4: полноценные каналы, как звуковые, но шлют MIDI (USB): своя секвенция на 128 шагов, свои режимы
     * (длина/направление/скорость/свинг/страницы), CHAN / PROG / BANK / CC-линии с LFO / арпеджио. mid=1: выбрана MIDI-дорожка mpat
     * (W.pat остаётся последней звуковой). Рабочая копия редактируемой MIDI-дорожки — в W.p (см. worm_mpos). */
    int8_t mid, mpat;
    struct { int16_t p[NMP]; int16_t len, dir, spd, swg, pg, vp; } mt[NMTRK];
    pattern_t mpats[NMTRK];
} worm_t;
extern worm_t W;
extern uint32_t worm_ui_now;   /* монотонные мс (заполняет мост app.c): таймеры UI (Trig Condition) */
extern volatile int worm_full_redraw;   /* 1 = принудительно перерисовать весь LCD (смена экрана-меню) */
void worm_init(void);
void worm_play(void); void worm_stop(void);
void worm_trk_fix(void);   /* старые банки: удалённые алгоритмы -> VA */
void worm_select_track(int t);   /* выбрать канал: режимы секвенсора (длина/направление/скорость/свинг/страницы) загружаются из канала */
void worm_trk_reload(void);      /* после загрузки паттерна: W.tr[W.pat] -> рабочая копия W.p (режимы секвенсора и звук), позиции дорожек в границах длины */
void worm_trk_sync(void);        /* после правки GLN/GDR/GSX/GSW/GPG/GVP: сохранить в W.tr[W.pat] */
void worm_cursor_run(int on);   /* бегунок (без звука): 1 = бегает */
void worm_render(int16_t *out, int frames);     /* synth + sequencer clocked by samples */
uint32_t worm_step_len(void);                    /* samples per step (for the BPM LED flash) */
void worm_synth_offset(int f);                   /* внутреннее: смещение чанка (кадры) для OUT24/TAP */
void worm_tap_mode(int m);                       /* USB-тап (usb_out): 0 = нет, 1 = дорожки 1-3, 2 = дорожки 4-6, 3 = FX-возвраты; каналы стерео-парами, int16, кадр = 8 слотов (6 заняты) */
void worm_synth_i16(int on);                    /* 0 = master_out не считает int16/дизер (прошивка берёт только OUT24); по умолчанию 1 */
const int32_t *worm_synth_out24(void);           /* мастер после последнего worm_render: 24-bit Q23, стерео */
const int16_t *worm_synth_tap(void);             /* тап-буфер последнего worm_render: кадр = 8 слотов int16, заняты первые 6 (3 стерео-пары) */
uint32_t worm_bar_count(void);                   /* счётчик тактов: паттерны переключаются на его приращении */
int worm_cond_hit(int cond, uint32_t loop);       /* условие триггера 0..15 (--, A:B, 1ST, FILL, 4BAR, NEV): звуковые и MIDI-дорожки */
int worm_beat_idx(void);                         /* доля такта 0..3, звучащая сейчас; -1 — стоп */
uint32_t worm_beat_phase(void);                  /* beat (quarter) phase 0..0xFFFFFFFF, sample-accurate */
float worm_lfo_value(int lfo);                   /* текущее значение LFO выбранной дорожки (-1..1) для отображения */
#define NENV 5     /* огибающие дорожки: E0 = amp OSC1, E3 = amp OSC2, E1/E2 = фильтры 1/2, E4 = свободная (DEST-маппинг) */
int  worm_step_lock_get(const step_t *s, int id, int *v);
void worm_step_lock_set(step_t *s, int id, int v);
int  worm_euclid_gate(int hits, int off, int lim, int mute, int step);   /* 1 = шаг играет (euclid) */
void worm_step_toggle(step_t *s);
void worm_step_init(step_t *s);                 /* заполнить новый шаг значениями по умолчанию (note==0 = шаг не инициализирован) */
void worm_trig(const step_t *s, int trk);       /* trk = дорожка 0..3 */
void worm_trig_hit(int t);                      /* отметка реального срабатывания шага для LED: t = 0..NTRK+NMTRK-1 */
uint32_t worm_trig_at(int t);                   /* время последнего триггера (мс), 0 = не было */
int  worm_live_on(int note, int vel);           /* quantized by scale/chord, returns first voice */
void worm_live_off(int note);
void worm_effective(const step_t *s, int16_t *eff);
void worm_effective_trk(const step_t *s, int trk, int16_t *eff);
int  worm_retrig_hits(const step_t *s, uint32_t step_samples, uint32_t *interval);   /* число ударов ретриггера (1 = без него) и интервал, сэмплов */
void worm_trk_mute(int t, int on);              /* MUTE трека t: 1 = заглушить (не триггерить, погасить голоса), 0 = включить */
int  worm_trk_muted(int t);                     /* 1 = трек t заглушен */


/* ---- MIDI-дорожки (worm_midi.c): секвенсор, CC-LFO, арпеджио и вывод MIDI по USB ---- */
/* Платформа (app.c) отправляет сообщение: st = статус (0x80|chan ...), d1/d2 — данные. Слабое определение в worm_midi.c ничего не шлёт. */
void worm_midi_out(int st, int d1, int d2);
void worm_midi_init(void);                              /* сброс состояния движка (worm_init) */
void worm_select_mtrack(int k);                         /* выбрать MIDI-дорожку k (0..3): режимы секвенсора и MIDI-параметры -> W.p */
pattern_t *worm_sel_pat(void);                          /* секвенция выбранной дорожки (звуковой или MIDI) */
void worm_mtrk_mute(int k, int on);                     /* MUTE MIDI-дорожки: 1 = не играть и погасить звучащие ноты */
int  worm_mtrk_muted(int k);
int  worm_mtrk_active(int k);                           /* сколько нот MIDI-дорожки звучит (для UI) */
int  worm_mlane_value(int k, int lane);                 /* текущее значение CC-линии (0..127), -1 = линия выключена */
int  worm_midi_samples(void);                           /* сэмплов до ближайшего MIDI-события (для нарезки блока рендера) */
void worm_midi_poll(void);                              /* раз на блок: живые события из UI, смена CHAN/PROG/BANK/VOL/PAN */
void worm_midi_advance(int n);                          /* прокрутить n сэмплов: шаги, ворота нот, арпеджио, CC-LFO */
void worm_midi_play(void);                              /* PLAY: позиции MIDI-дорожек и фазы LFO в начало */
void worm_midi_stop(void);                              /* STOP / паника: погасить все звучащие MIDI-ноты */
void worm_midi_panic(void);                             /* жёсткая паника: + All Notes Off / All Sound Off по каналам дорожек */
void worm_midi_resend(void);                            /* заново послать PROG/BANK/VOL/PAN/CC (новое USB-подключение) */
void worm_midi_live(int on, int note, int vel);         /* живая нота -> выбранная MIDI-дорожка (арпеджио, аккорд, квантизация) */
void worm_midi_arp_note(int trk, int note, int vel, int gate);   /* нота звукового арпеджио -> MIDI-дорожка trk (ARP>MIDI) */
void worm_midi_cursor(int on);                          /* бегунок выбранной MIDI-дорожки (STEP SEQUENCER без PLAY) */
int  worm_midi_pos(int k);                              /* текущий шаг MIDI-дорожки k */
void worm_midi_release(int trk, int note);              /* отпустить живую ноту note на MIDI-дорожке trk */
void worm_midi_reload(void);                            /* после загрузки паттерна: позиции MIDI-дорожек в границах длины */
void worm_midi_reset_track(int k);                      /* позиция MIDI-дорожки k в начало */
void worm_mtrk_load(int k);                             /* W.mt[k] -> рабочая копия W.p (MIDI-параметры, NOTE, EUCLID, режимы секвенсора) */
void worm_mtrk_store(int k);                            /* рабочая копия W.p -> W.mt[k] */
void worm_mtrk_default(int k);                          /* значения MIDI-дорожки по умолчанию (канал = k+1) */
float worm_mlane_wave(int w, float p);                  /* волна CC-LFO (-1..1) по фазе p: для экрана */
int  worm_mpid(int q);                                  /* индекс значения MIDI-дорожки q (0..NMP-1) -> id параметра */
int  worm_mtrk_def(int k, int q);                       /* умолчание значения q у MIDI-дорожки k (CHAN = k+1) */
int  worm_mtrk_is_default(int k);                       /* параметры и режимы совпадают с умолчанием (store: паттерн без правок) */

/* ---- automation (запись вращения ручек на лету) ---- */
/* Записать точку automation параметра id трека trk на шаге step (значение v). Если точка с тем же id
 * на том же шаге уже есть — обновить значение. Возвращает 1, если бюджет (NAUT) исчерпан и записать нельзя. */
int  worm_aut_rec(int trk, int id, int step, int v);
/* Удалить все точки automation трека trk. */
void worm_aut_clear_trk(int trk);
/* Удалить точки automation параметра id трека trk. */
void worm_aut_clear_param(int trk, int id);
/* Интерполированное (линейное) значение automation параметра id трека trk на шаге step.
 * Возвращает 1 и пишет в *v, если у параметра есть точки; иначе 0 (брать базовый патч). */
int  worm_aut_value(int trk, int id, int step, int *v);
/* Число точек automation параметра id трека trk (для индикации в UI). */
int  worm_aut_count(int trk, int id);
extern volatile uint32_t worm_aut_gen;   /* растёт при любой правке automation: синт пересчитывает свой кеш значений */

/* ---- persistence (flash store 0x97000: NBANK banks x 36 KB + project sector) ---- */
int worm_flash_read(uint32_t addr, void *buf, uint32_t len);
int worm_flash_write(uint32_t addr, const void *buf, uint32_t len);
int worm_flash_erase(uint32_t addr, uint32_t len);
int worm_store_bank_save(int bank);   /* SAVE / смена банка: текущий паттерн -> образ банка, всё сразу во флеш. 0 ок, -1 паттерн не влез в банк, -2/-3 ошибка флеши */
int worm_store_bank_load(int bank);   /* загрузить банк и выбрать паттерн W.slot (пустой паттерн = заводское состояние: чистые секвенции, звук/FX/режимы по умолчанию); 1 = банк пуст/не читается */
int worm_store_pat_switch(int m);     /* переключить ПАТТЕРН целиком (все дорожки, звук, FX, automation) на слот m: уходящий пакуется в RAM-образ, флеш не трогается; -1 = не влез в банк (остаёмся) */
int worm_store_prefetch(int m);       /* подтянуть запись паттерна m в MRU-кэш заранее (pend_pat); 1 = в кэше/пусто, 0 = нет места */
int worm_store_mru_info(int *dirty);  /* занято ячеек кэша (и сколько из них грязные) */
void worm_str5(char *d,const char *s); /* первые 5 символов: %.5s в прошивочной libc печатается как ".5s" */
unsigned worm_store_bank_used(void);  /* маска слотов 0..NPAT-1 текущего банка, где что-то записано */
void worm_store_flush(void);          /* ленивая запись изменённых секторов банка (main loop): только в STOP, через 1.5 с после правки, по сектору за вызов */
int worm_store_proj_save(void);
int worm_store_proj_load(void);
/* пользовательские пресеты: 32 x 120 Б в том же секторе проекта (0xDF000) */
int worm_store_preset_read(int i, char *name, uint8_t *vals);          /* 1 = найден и корректен */
int worm_store_preset_save(int i, const char *name, const uint8_t *vals);
extern float worm_master;

/* ---- UI ---- */
#define LCD_W 240
#define LCD_H 240
extern unsigned worm_usbh_dbg; /* диагностика опроса: стадия | длина<<8 | класс<<24 */
extern int worm_usbh_msg;   /* последнее сообщение хоста: 0 нет, 1 ON, 2 OFF, 3 MIDI, 4 NOMIDI, 5 LOW, 6 LOST */
extern int worm_usbh;       /* состояние USB-хоста: 0 выкл, 1 ищет устройство, 2 подключено (app.c задаёт) */
extern int worm_battery;   /* 0..3 (пусто..полно), 4 = USB-питание, -1 = не читается (app.c задаёт) */
extern int worm_charging;  /* 1 = заряжается, 0 = нет */
extern int worm_cpu_pct;   /* загрузка аудио-рендера, % бюджета блока (app.c) */
extern int worm_uac_und, worm_uac_miss, worm_max_us;   /* диагностика USB-записи и рендера (экран BPM) */
extern int worm_xruns;     /* число xruns с загрузки (app.c) */
extern int worm_lim_db;     /* сжатие лимитера мастера, дБ (держится ~1 с; app.c) — хрипы в записи появляются, когда оно > 0 */
int worm_synth_lim_take(void);                   /* самое сильное сжатие лимитера с прошлого вызова, 0.1 дБ */
void worm_ui_init(void);
void worm_ui_button(int id, int down);          /* 0..13 stock ids */
void worm_ui_key(int idx, int down);            /* 0..26 (F3..G5) */
void worm_ui_enc(int id, int delta);            /* stock ids 0..6 */
void worm_ui_render(uint8_t *fb);               /* 240x240, 1 byte/px (0/1) */
void worm_ui_lights(uint32_t *btns, uint32_t *keys, uint32_t *dim_btns, uint32_t *dim_keys, uint32_t *breath_btns, uint32_t *breath_keys);  /* LED: яркие/тусклые/дышащие кнопки и клавиши */
/* ---- LIVE режим (HOME+GLO): макросы энкодеров, живые эффекты мастера (worm_live.c / worm_lfx.c) ---- */
#define NLM 6                         /* макросов: SELECT, ALGO, K1..K4 */
enum { LM_BPM = NPALL, LM_FILT, LM_CRUSH, LM_DUCK, LM_END };   /* псевдо-параметры (id >= NPALL) */
typedef struct { int16_t id[NLM]; int8_t trk[NLM]; } lm_set_t;  /* id: параметр или -1; trk: -1 = выбранная дорожка, 0..NTRK-1 звуковая, NTRK+k MIDI */
extern lm_set_t worm_lm_bank, worm_lm_pat; extern int8_t worm_lm_own;   /* набор банка / набор паттерна; own = у паттерна свой набор */
extern int8_t worm_lm_sw, worm_lm_chain, worm_lm_order;   /* время переключения паттерна / автоцепочка / порядок */
extern volatile int8_t worm_lm_dirty;   /* настройки/набор банка изменены: записать в сектор проекта */
lm_set_t *worm_lm_active(void);
void worm_lm_defaults(lm_set_t *s);
int  worm_lm_ntargets(void); int worm_lm_target(int i); int worm_lm_find(int id);
const char *worm_lm_name(int id);                        /* имя цели (статическая строка) */
int  worm_lm_get(int id, int trk, int *v, int *mn, int *mx);   /* 1 = ок */
void worm_lm_fmt(int id, int v, char *o);
void worm_lm_turn(int slot, int d);                      /* вращение макроса (с ускорением) */
int  worm_lm_accel(int slot, int d);                     /* ускоренный шаг (для редактора) */
int  worm_lm_trk_cur(int trk);                           /* -1 -> текущая дорожка (код) */
void worm_lm_trk_name(int trk, char *o);
int  worm_lm_store_pack(uint8_t *o); int worm_lm_store_unpack(const uint8_t *o, int n);   /* 19 байт: набор паттерна */
extern const char *const worm_lm_sw_names[5]; extern const char *const worm_lm_chain_names[9]; extern const char *const worm_lm_order_names[3];
int  worm_lm_pass_bars(void);                            /* тактов в одном проходе текущего паттерна */
enum { LFX_R8, LFX_R16, LFX_R32, LFX_REV, LFX_TAPE, LFX_FRZ, LFX_LPF, LFX_HPF, LFX_CRU, LFX_DUCK, LFX_N };
enum { LFX_A_FILT, LFX_A_CRUSH, LFX_A_DUCK, LFX_A_N };
void worm_lfx_arm(int on); void worm_lfx_reset(void); void worm_lfx_key(int fx, int on); unsigned worm_lfx_held(void);
void worm_lfx_set(int w, int v); int worm_lfx_get(int w); const char *worm_lfx_name(int fx);
float worm_lfx_block(float *L, float *R, int n);        /* мастер-блок; -1 = ничего не менял (idle), иначе пик */
int worm_store_lm_load(void); int worm_store_lm_save(void);   /* настройки LIVE + набор банка: хвост сектора проекта */

#endif
