/* WORM-FM1 sequencer (переписан с нуля): 4 дорожки (W.pats[0..3], каждая — своя секвенция до 128 шагов), p-locks, trig conditions,
 * probability, mTime, swing, направления, RETRIG, арпеджиатор, метроном, EUCLID. Тактируется сэмплами.
 *
 * Что изменилось по сравнению с прежней версией (звук тот же, нагрузка на CPU ниже):
 *   - Всё расписание на АБСОЛЮТНОМ времени (NOW — счётчик сэмплов аудио-потока, у события — момент срабатывания due).
 *     Раньше у каждого события был свой обратный счётчик, и КАЖДЫЙ чанк в 64 сэмпла пробегал по всем счётчикам (дорожки, пул
 *     отложенных триггеров, арпеджио, MIDI). Теперь в тихом чанке — ни одного цикла: считается время до ближайшего события, и синт
 *     рендерит весь отрезок до него одним вызовом.
 *   - События срабатывают на тех же границах чанка (64 сэмпла от начала блока рендера), что и раньше: звук не изменился.
 *   - BPM-математика (длина шага/четверти) кешируется, а не делится заново на каждом шаге каждого чанка.
 *   - worm_beat_phase без 64-битного деления (на AC79 оно программное: цикл на 64 итерации, вызывалось из главного цикла).
 *   - EUCLID: настоящий алгоритм Бьёрклунда (раньше — таблица на 16 шагов, E.LIMIT просто обрезал её), и он сам расставляет
 *     триггеры (раньше только вырезал шаги из уже включённых).
 * Живые ноты и транспорт приходят из главного цикла, а секвенсор/синт крутятся в аудио-потоке: между ними — SPSC-очередь
 * (разбирается в начале worm_render). */
#include <string.h>
#include <stdlib.h>
#include "worm.h"
WORM_POOL worm_t W;

/* Реальные срабатывания шагов для LED-индикации на клавишах мьютов (не звук, не расписание) */
static uint32_t trig_at[NTRK + NMTRK];
void worm_trig_hit(int t) { if (t >= 0 && t < NTRK + NMTRK) trig_at[t] = worm_ui_now ? worm_ui_now : 1u; }
uint32_t worm_trig_at(int t) { return (t >= 0 && t < NTRK + NMTRK) ? trig_at[t] : 0u; }

/* ---- время ---- */
static volatile uint32_t NOW;                 /* сэмплы аудио-потока; все расписания — абсолютные (due = NOW + задержка) */
#define LEFT(t) ((int32_t)((t)-NOW))          /* сколько сэмплов осталось (<= 0 — срок наступил); работает и при переполнении счётчика */
#define LATE(t) (LEFT(t)<=0)
static float bpm_c, q16;                      /* кеш: BPM -> длина 1/16 в сэмплах (float) */
static void bpm_sync(void){float b=W.bpm;if(b!=bpm_c||q16<=0.f){bpm_c=b;q16=WORM_FS*60.f/b/4.f;}}

#define NPD 16   /* отложенные триггеры (mTime + RETRIG): одна запись на шаг дорожки (аккорд внутри ch[4]); задержка <= 1 шаг,
                  * идущий ретриггер гасится новым шагом дорожки -> одновременно <= 2 на дорожку (12) + 4 запаса. Пул полон — запись
                  * не теряется: крадём хвост ретриггера или играем сразу. */
typedef struct { uint32_t due; int note,vel,gate,trk; int rint,rleft,vq,rvd; uint8_t act,hit; uint8_t ch[4]; int16_t e[NP]; } pend_t;   /* rint — интервал ретриггера, rleft — осталось ударов, vq/rvd — vel*256 и шаг vel */
static WORM_POOL pend_t PD[NPD];
static uint32_t rng=12345; static uint32_t rnd(void){rng=rng*1664525u+1013904223u;return rng>>8;}
static void arp_reset(void);
volatile uint32_t worm_aut_gen;   /* растёт при любой правке automation: синт пересчитывает кеш значений шага */
static void fire(int note,int vel,int gate,const int16_t *e,int trk,int primary);
/* Старые банки: удалённые алгоритмы WT/HIVE/CHIP/TOY -> VA. Голосов больше нет: 4 моно-дорожки. */
void worm_trk_fix(void){
 for(int t=0;t<NTRK;t++)W.tr[t].p[P_OA]=(int16_t)worm_algo_fix(W.tr[t].p[P_OA]);W.p[P_OA]=(int16_t)worm_algo_fix(W.p[P_OA]);}
uint32_t worm_ui_now;   /* монотонные мс (заполняет app.c) */
volatile int worm_full_redraw;   /* 1 = перерисовать весь LCD */

/* Каждый канал (дорожка) имеет СВОИ часы: позицию, направление, длину, скорость, свинг (W.tr[t]).
 * due — момент следующего шага (перелёт за границу чанка переносится в следующий шаг: темп не плывёт);
 * et — счётчик тактов EUCLID (идёт на каждый такт часов дорожки, не зависит от направления и позиции). */
static struct { int pos,pd,loop; uint32_t due,et; } TS[NTRK];
static uint8_t TLIVE[NTRK];                   /* дорожка бежала на прошлом блоке (фронт запуска -> часы с начала) */
static uint32_t beatn, barn;                  /* barn — счётчик тактов (4 четверти) */
static int run_trk(int t){return W.run||(W.cur_run&&!W.mid&&t==W.pat);}   /* в STEP SEQUENCER без PLAY бежит только курсор выбранного канала */
static uint32_t step_samples_t(int t){static const float m[3]={2.f,1.f,.5f};int s=W.tr[t].spd;if(s<0||s>2)s=1;bpm_sync();return (uint32_t)(q16*m[s]);}   /* spd 0 = вдвое медленнее, 2 = вдвое быстрее */
uint32_t worm_step_len(void){bpm_sync();return (uint32_t)q16;}   /* шаг 1x: для BPM-мигания */
/* фаза четверти 0..0xFFFFFFFF: свободный счётчик W.beat_ph (сбрасывается по PLAY), общий для всех каналов */
uint32_t worm_bar_count(void){return barn;}   /* растёт на каждой границе такта */
/* задержка микротайминга: mTime=0 = 24 тика (целый шаг 1x: чтобы можно было сдвинуть на шаг назад) — на столько же задержаны ноты, поэтому метроном/LED/счётчик сдвигаем так же */
static uint32_t mt_lat(void){return worm_step_len();}
static uint32_t clk_due;static uint8_t clk_pend,clk_idx;static int8_t beat_disp=-1;   /* отложенный клик/счётчик доли: звучит вместе с нотами */
int worm_beat_idx(void){return beat_disp;}   /* 0..3 — доля такта, звучащая сейчас; -1 — стоп */
uint32_t worm_beat_phase(void){uint32_t blen=worm_step_len()*4u;if(!blen)blen=1u;
 uint32_t bp=W.beat_ph,lt=mt_lat();bp=bp>=lt?bp-lt:bp+blen-lt;if(bp>=blen)bp-=blen;
 float f=(float)bp*(1.f/(float)blen);if(f<0.f)f=0.f;if(f>.9999999f)f=.9999999f;return (uint32_t)(f*4294967296.f);}
int worm_step_lock_get(const step_t *s,int id,int *v){for(int i=0;i<s->nl;i++)if(s->lk[i].id==id){*v=s->lk[i].v;return 1;}return 0;}
void worm_step_lock_set(step_t *s,int id,int v){for(int i=0;i<s->nl;i++)if(s->lk[i].id==id){s->lk[i].v=v;return;}
 if(s->nl<MAXLK){s->lk[s->nl].id=id;s->lk[s->nl].v=v;s->nl++;}}
/* note==0 — признак «шаг ещё не заполнен» (ROOT >= 24). */
void worm_step_init(step_t *s){if(s->note)return;s->note=W.p[P_NR];s->vel=W.p[P_NV];s->len=W.p[P_NL];s->prob=W.p[P_NPR];s->cond=W.p[P_NCD];
 s->rtr=(uint8_t)W.p[P_RTR];s->rtl=(uint8_t)W.p[P_RTL];s->rtv=(int8_t)W.p[P_RTV];}   /* ретриггер нового шага — из настроек по умолчанию */
/* RETRIG: число ударов (>=1) и интервал в сэмплах. Длина серии — LEN шага (R.LEN=LEN) или своя 1/16..1/1. */
int worm_retrig_hits(const step_t *s,uint32_t ss,uint32_t *iv){if(iv)*iv=0;
 if(!s||!s->rtr||s->rtr>9)return 1;
 uint32_t it=16u*ss/worm_rtr_den[s->rtr];if(it<256u)it=256u;   /* не чаще ~170 Гц */
 static const uint8_t RL[6]={0,1,2,4,8,16};
 uint32_t L=s->rtl?(uint32_t)RL[s->rtl>5?5:s->rtl]*ss:worm_len_samples(s->len,ss);
 int n=(int)(L/it);if(n<1)n=1;if(n>64)n=64;if(iv)*iv=it;return n;}
/* снятие триггера убирает и все его p-lock'и (включая пресет шага); иначе они оставались в шаге
 * и продолжали действовать после повторного включения. Одна точка для всех страниц секвенсора
 * и для звуковых, и для MIDI-дорожек. */
void worm_step_toggle(step_t *s){if(s->on){memset(s,0,sizeof *s);return;}worm_step_init(s);s->on=1;}   /* снятие триггера = шаг в исходное состояние: микротайминг (mt), p-lock'и, RETRIG, VEL/LEN/PROB/COND, нота, аккорд — всё обнуляется (note==0 -> при новой постановке шаг заполнится значениями по умолчанию) */
/* пресет шага применяется ПЕРВЫМ, обычные p-lock'ы ложатся поверх него */
static void step_preset(const step_t *s,int16_t *e){for(int i=0;i<s->nl;i++)if(s->lk[i].id==P_PSTEP){
 int q=s->lk[i].v;if(q<0)q=0;if(q>=NPRESET)q=NPRESET-1;worm_preset_load(e,q);break;}}
void worm_effective(const step_t *s,int16_t *e){memcpy(e,W.tr[W.pat].p,sizeof W.tr[0].p);
 if(s){step_preset(s,e);e[P_NR]=s->note;e[P_NV]=s->vel;e[P_NL]=s->len;e[P_NPR]=s->prob;e[P_NCD]=s->cond;for(int i=0;i<s->nl;i++)if(s->lk[i].id<NP)e[s->lk[i].id]=s->lk[i].v;}}   /* id<NP: битый/чужой lock не пишет за массив */
void worm_effective_trk(const step_t *s,int trk,int16_t *e){memcpy(e,W.tr[trk].p,sizeof W.tr[0].p);
 if(s){step_preset(s,e);e[P_NR]=s->note;e[P_NV]=s->vel;e[P_NL]=s->len;e[P_NPR]=s->prob;e[P_NCD]=s->cond;for(int i=0;i<s->nl;i++)if(s->lk[i].id<NP)e[s->lk[i].id]=s->lk[i].v;}}
/* ---- automation (запись вращения ручек на лету) ---- */
int worm_aut_rec(int trk,int id,int step,int v){if(trk<0||trk>=NTRK)return 0;
 auto_pt_t *a=W.tr[trk].aut;int n=W.tr[trk].naut;
 if(step<0)step=0;if(step>=NSTEP)step=NSTEP-1;
 worm_aut_gen++;
 for(int i=0;i<n;i++)if(a[i].id==id&&a[i].step==step){a[i].v=(int16_t)v;return 0;}   /* обновить точку */
 if(n>=NAUT)return 1;   /* бюджет исчерпан */
 a[n].step=(uint8_t)step;a[n].id=(uint8_t)id;a[n].v=(int16_t)v;W.tr[trk].naut=(int16_t)(n+1);return 0;}
void worm_aut_clear_trk(int trk){if(trk<0||trk>=NTRK)return;W.tr[trk].naut=0;worm_aut_gen++;}
void worm_aut_clear_param(int trk,int id){if(trk<0||trk>=NTRK)return;
 auto_pt_t *a=W.tr[trk].aut;int n=W.tr[trk].naut,o=0;
 for(int i=0;i<n;i++)if(a[i].id!=id)a[o++]=a[i];
 W.tr[trk].naut=(int16_t)o;worm_aut_gen++;}
int worm_aut_count(int trk,int id){if(trk<0||trk>=NTRK)return 0;int n=0;
 for(int i=0;i<W.tr[trk].naut;i++)if(W.tr[trk].aut[i].id==id)n++;return n;}
int worm_aut_value(int trk,int id,int step,int *v){if(trk<0||trk>=NTRK)return 0;
 auto_pt_t *a=W.tr[trk].aut;int n=W.tr[trk].naut;if(step<0)step=0;if(step>=NSTEP)step=NSTEP-1;
 /* предыдущая и следующая точки по step для параметра id */
 int lo=-1,hi=-1;
 for(int i=0;i<n;i++)if(a[i].id==id){if(a[i].step<=step&&(lo<0||a[i].step>a[lo].step))lo=i;
  if(a[i].step>=step&&(hi<0||a[i].step<a[hi].step))hi=i;}
 if(lo<0&&hi<0)return 0;
 if(lo<0){*v=a[hi].v;return 1;}        /* только после: удержать первое значение */
 if(hi<0){*v=a[lo].v;return 1;}        /* только до: удержать последнее значение */
 if(lo==hi){*v=a[lo].v;return 1;}      /* ровно на точке */
 int ds=a[hi].step-a[lo].step;if(ds<=0){*v=a[lo].v;return 1;}
 *v=a[lo].v+(int16_t)(((int)(a[hi].v-a[lo].v))*(step-a[lo].step)/ds);return 1;}
static int snap(int n){n+=W.p[P_GTR];int sc=W.p[P_GSC];if(!sc)return n;int r=W.p[P_GRT];
 for(int o=0;o<12;o++){for(int sg=1;sg>=-1;sg-=2){int m=(((n+sg*o-r)%12)+12)%12;for(int k=0;k<worm_scale_n[sc];k++)if(worm_scale_iv[sc][k]==m)return n+sg*o;}}return n;}
static const int8_t CHI[5][2]={{0,0},{12,0},{7,0},{4,7},{3,7}};

/* ---- EUCLID ----
 * E.HITS (0..16) пульсов равномерно на E.LIMIT (1..16) тактов; E.OFS — сдвиг рисунка. HITS=0 — euclid выключен (играют включённые шаги
 * как обычно). Иначе euclid САМ ставит триггеры на пульсах (шаг не обязан быть включён: нота/velocity/p-lock берутся из текущего
 * шага, если он заполнен, иначе — из NOTE TRIG дорожки), а включённые шаги вне пульсов молчат. Рисунок крутится по кругу длиной
 * LIMIT на каждый такт часов дорожки — независимо от длины паттерна и направления воспроизведения (при LIMIT != LENGTH получаются
 * полиритмы). E.MUTE глушит все пульсы.
 * Рисунок — алгоритм Бьёрклунда (Toussaint, «The Euclidean Algorithm Generates Traditional Musical Rhythms»): считается один раз
 * для каждой пары (hits, limit) — при старте, не в аудио-потоке. */
static uint16_t EM[17][17]; static uint8_t EV[17][17];
static uint16_t bjorklund(int k,int n){   /* бит i = такт i */
 if(k<=0)return 0;
 if(k>=n)return (uint16_t)((n>=16)?0xFFFFu:((1u<<n)-1u));
 uint16_t ab[16],bb[16],tb[16];uint8_t al[16],bl[16],tl[16];int na=k,nb=n-k,i;
 for(i=0;i<na;i++){ab[i]=1;al[i]=1;}
 for(i=0;i<nb;i++){bb[i]=0;bl[i]=1;}
 while(nb>1){int m=na<nb?na:nb,nn;
  for(i=0;i<m;i++){tb[i]=(uint16_t)((ab[i]<<bl[i])|bb[i]);tl[i]=(uint8_t)(al[i]+bl[i]);}   /* пара: A[i] затем B[i] */
  if(na>nb){nn=na-m;for(i=0;i<nn;i++){bb[i]=ab[m+i];bl[i]=al[m+i];}}   /* остаток — лишние A */
  else{nn=nb-m;for(i=0;i<nn;i++){bb[i]=bb[m+i];bl[i]=bl[m+i];}}       /* остаток — лишние B */
  for(i=0;i<m;i++){ab[i]=tb[i];al[i]=tl[i];}
  na=m;nb=nn;}
 uint16_t r=0;int pos=0;
 for(i=0;i<na;i++)for(int b=al[i]-1;b>=0;b--){if((ab[i]>>b)&1)r|=(uint16_t)(1u<<pos);pos++;}
 for(i=0;i<nb;i++)for(int b=bl[i]-1;b>=0;b--){if((bb[i]>>b)&1)r|=(uint16_t)(1u<<pos);pos++;}
 return r;}
static uint16_t eu_mask(int h,int n){if(!EV[h][n]){EM[h][n]=bjorklund(h,n);EV[h][n]=1;}return EM[h][n];}
/* 1 = на такте step пульс (или euclid выключен: hits<=0) */
int worm_euclid_gate(int hits,int off,int lim,int mute,int step){
 if(hits<=0)return 1;
 if(mute)return 0;
 if(lim<1)lim=1;if(lim>16)lim=16;if(hits>lim)hits=lim;
 if(off<0)off=0;
 int i=(int)(((uint32_t)step%(uint32_t)lim+(uint32_t)off)%(uint32_t)lim);
 return (eu_mask(hits,lim)>>i)&1;}

/* --- арпеджиатор (live-режим синтезатора): зажатые ноты раскладываются во времени --- */
static int8_t arp_notes[12]; static int arp_n; static int arp_idx, arp_dir;
static uint32_t arp_due; static uint8_t arp_set; static uint32_t arp_cnt;   /* arp_set=0: таймер не запущен (запустится с началом следующего блока) */
static void arp_reset(void){arp_n=0;arp_idx=0;arp_dir=1;arp_set=0;arp_cnt=0;}
static void arp_sort(void){for(int i=1;i<arp_n;i++){int8_t x=arp_notes[i];int j=i;while(j>0&&arp_notes[j-1]>x){arp_notes[j]=arp_notes[j-1];j--;}arp_notes[j]=x;}}
static void arp_note(int n){int i;for(i=0;i<arp_n;i++)if(arp_notes[i]==n)return;if(arp_n<12)arp_notes[arp_n++]=n;arp_sort();}
static void arp_drop(int n){int i;for(i=0;i<arp_n;i++)if(arp_notes[i]==n){for(int j=i;j<arp_n-1;j++)arp_notes[j]=arp_notes[j+1];arp_n--;if(arp_idx>=arp_n)arp_idx=0;return;}}
static uint32_t arp_ss(void){static const float am[5]={.25f,.5f,1.f,2.f,4.f};int s=W.p[P_ARS];if(s<0||s>4)s=1;bpm_sync();uint32_t v=(uint32_t)(q16/am[s]);return v<1u?1u:v;}
static void arp_tick(void){int m=W.p[P_ARM],n=arp_n,oct=W.p[P_ARO];
 if(!m||!n)return;
 /* порядок нот: arp_idx — счётчик шагов цикла. UP: вверх; DOWN: вниз; UPDN: вверх и обратно без повтора крайних; RND: случайно */
 int per=m==3&&n>1?2*n-2:n,s=arp_idx%per,b;
 if(m==1)b=s;else if(m==2)b=n-1-s;else if(m==3)b=s<n?s:2*n-2-s;else b=(int)(rnd()%(uint32_t)n);
 arp_idx=(s+1)%per;
 /* октавы: свой счётчик; нота отпускается по гейту ARG */
 int nn=arp_notes[b]+12*((int)((arp_cnt++/(uint32_t)n)%(uint32_t)(oct<1?1:oct)));
 uint32_t ss=arp_ss();int gate=(int)(ss*(uint32_t)W.p[P_ARG]/127u);
 fire(nn,100,gate,W.tr[W.pat].p,W.pat,1);
 if(W.p[P_AMO])worm_midi_arp_note(W.p[P_AMO]-1,nn,100,gate);}   /* ARP>MIDI: то же арпеджио уходит в MIDI-дорожку */

/* trk — дорожка секвенсора: нота играет моно-голосом этой дорожки (аккорды не звучат: голос один) */
static void fire(int note,int vel,int gate,const int16_t *e,int trk,int primary){if(W.tr[trk].mute)return;   /* MUTE: трек не триггерит */
 int b=snap(note);int lvnote=gate<0?note:-1;   /* live-нота (для note_off_lv): исходная нота до snap; секвенсор/арп — -1 */
 worm_synth_note_on_v(e,b,vel,gate,trk,primary,lvnote);
 for(int c=0;c<2;c++)if(CHI[W.p[P_GCH]][c])worm_synth_note_on_v(e,b+CHI[W.p[P_GCH]][c],vel,gate,trk,0,lvnote);}
/* COND 0..15: общая проверка для звуковых и MIDI-дорожек. 1..11 — A:B из старых банков,
 * 12=1ST (первый проход), 13=FILL (4-й такт цикла), 14=4BAR (первый такт цикла), 15=NEV (никогда). */
int worm_cond_hit(int cond,uint32_t loop)
{
 static const uint8_t CA[12]={0,1,2,1,2,3,1,2,3,4,1,8},CB[12]={1,2,2,3,3,3,4,4,4,4,8,8};
 if(cond<=0)return 1;
 if(cond<12)return loop%CB[cond]==CA[cond]-1;
 if(cond==12)return loop==0;
 if(cond==13)return loop%4u==3u;
 if(cond==14)return loop%4u==0u;
 return 0;   /* NEV */
}
static void fire_step(const step_t *s,int trk){int16_t *tp=W.tr[trk].p;step_t eu;
 if(tp[P_EUH]>0){   /* EUCLID ставит триггеры сам: на пульсе играем шаг, даже выключенный; вне пульса — тишина */
  if(!worm_euclid_gate(tp[P_EUH],tp[P_EUO],tp[P_EUL],tp[P_EUM],(int)TS[trk].et))return;
  if(!s->note){memset(&eu,0,sizeof eu);eu.on=1;eu.note=(uint8_t)tp[P_NR];eu.vel=(uint8_t)tp[P_NV];eu.len=(uint8_t)tp[P_NL];eu.prob=(uint8_t)tp[P_NPR];eu.cond=(uint8_t)tp[P_NCD];
   eu.rtr=(uint8_t)W.p[P_RTR];eu.rtl=(uint8_t)W.p[P_RTL];eu.rtv=(int8_t)W.p[P_RTV];s=&eu;}   /* шаг не заполнен: параметры дорожки */
 }else if(!s->on)return;
 int16_t e[NP];worm_effective_trk(s,trk,e);
 if((int)(rnd()%127)>=e[P_NPR])return;   /* PROB 0..127: при PROB=0 шаг не играет никогда */
 if(!worm_cond_hit(e[P_NCD],(uint32_t)TS[trk].loop))return;
 worm_trig_hit(trk);   /* LED мьюта мигает на каждом реально сработавшем шаге, даже если дорожка заглушена */
 for(int i=0;i<NPD;i++)if(PD[i].act&&PD[i].trk==trk&&PD[i].hit)PD[i].act=0;   /* новый шаг дорожки обрывает идущий ретриггер прошлого шага */
 uint32_t ss=step_samples_t(trk);int d=(s->mt+MT_MAX)*(int)worm_step_len()/MT_MAX;   /* mt — % шага 1x (не зависит от скорости дорожки); весь секвенсор задержан на шаг, поэтому -100% = без задержки */
 uint32_t it;int nh=worm_retrig_hits(s,ss,&it);int len=e[P_NL];if(len<0)len=0;if(len>NLEN-1)len=NLEN-1;
 int gate=(int)(worm_len_samples(len,ss)*.98f);if(gate<1)gate=1;if(nh>1){int g=(int)(it*.65f);if(g<gate)gate=g;}   /* удар ретриггера короче интервала: каждый слышен отдельно */
 int k=-1;for(int i=0;i<NPD;i++)if(!PD[i].act){k=i;break;}
 if(k<0)for(int i=0;i<NPD;i++)if(PD[i].hit&&(k<0||PD[i].rleft<PD[k].rleft))k=i;   /* пул полон: крадём самый короткий хвост ретриггера */
 if(k<0){fire(e[P_NR],e[P_NV],gate,e,trk,1);for(int c=0;c<4;c++)if(s->chord[c])fire(s->chord[c],e[P_NV],gate,e,trk,0);return;}   /* иначе — играть сразу, но не терять */
 pend_t *P=&PD[k];P->due=NOW+(uint32_t)(d>0?d:1);P->act=1;P->trk=trk;P->note=e[P_NR];P->vel=e[P_NV];P->gate=gate;P->hit=0;
 P->rint=(int)it;P->rleft=nh-1;P->vq=e[P_NV]<<8;
 {int ve=e[P_NV]+s->rtv*2;if(ve<1)ve=1;if(ve>127)ve=127;P->rvd=nh>1?((ve-e[P_NV])*256)/(nh-1):0;}   /* R.VEL: наклон громкости к последнему удару */
 for(int c=0;c<4;c++)P->ch[c]=s->chord[c];
 memcpy(P->e,e,sizeof e);}
/* удар отложенного триггера: нота + аккорд; у ретриггера — следующий удар через rint (перелёт чанка переносится) */
static void pd_hit(pend_t *P){int v=(P->vq+128)>>8;if(v<1)v=1;if(v>127)v=127;
 worm_synth_retrig_hint(P->hit);   /* P->hit=1 — второй и дальше удары серии */
 fire(P->note,v,P->gate,P->e,P->trk,1);for(int c=0;c<4;c++)if(P->ch[c])fire(P->ch[c],v,P->gate,P->e,P->trk,0);worm_synth_retrig_hint(0);
 if(P->rleft>0){P->rleft--;P->hit=1;P->act=1;P->vq+=P->rvd;P->due+=(uint32_t)P->rint;if(LATE(P->due))P->due=NOW+1u;}else{P->act=0;P->hit=0;}}

/* Живые ноты и транспорт приходят из главного цикла (UI, MIDI-вход) -> SPSC-очередь, разбирается в начале worm_render (аудио-поток). */
typedef struct { uint8_t on,note,vel; } lvq_t;
#define NLVQ 32
static lvq_t LVQ[NLVQ]; static volatile uint8_t lvq_h,lvq_r; static volatile uint8_t tr_req;   /* tr_req: 1 = PLAY, 2 = STOP */
static void live_on_now(int note,int vel);static void live_off_now(int note);static void play_now(void);static void stop_now(void);
static int lvq_push(int on,int note,int vel){uint8_t h=lvq_h,n=(uint8_t)((h+1)&(NLVQ-1));if(n==lvq_r)return 0;
 LVQ[h].on=(uint8_t)on;LVQ[h].note=(uint8_t)note;LVQ[h].vel=(uint8_t)vel;lvq_h=n;return 1;}
static void seq_drain(void){uint8_t r=tr_req;if(r){tr_req=0;if(r==1)play_now();else stop_now();}
 while(lvq_r!=lvq_h){lvq_t q=LVQ[lvq_r];lvq_r=(uint8_t)((lvq_r+1)&(NLVQ-1));if(q.on)live_on_now(q.note,q.vel);else live_off_now(q.note);}}
int worm_live_on(int note,int vel){if(!lvq_push(1,note,vel))live_on_now(note,vel);return 0;}
void worm_live_off(int note){if(!lvq_push(0,note,0))live_off_now(note);}
static void live_on_now(int note,int vel){if(W.mid){worm_midi_live(1,note,vel);return;}   /* выбрана MIDI-дорожка: живая нота уходит в MIDI */
 if(W.p[P_ARM]){arp_note(note);if(arp_n==1){arp_idx=0;arp_dir=1;arp_set=0;arp_cnt=0;arp_tick();}return;}   /* первая нота — сразу первый шаг арпеджио (с гейтом, не держится); следующие — по таймеру */
 fire(note,vel,-1,W.tr[W.pat].p,W.pat,1);}
static void live_off_now(int note){if(W.mid){worm_midi_live(0,note,0);return;}
 if(W.p[P_ARM])arp_drop(note);worm_synth_note_off_lv(note);if(W.p[P_AMO])worm_midi_release(W.p[P_AMO]-1,note);}
/* MUTE трека: 1 = заглушить (не триггерить новые, погасить звучащие голоса), 0 = включить */
void worm_trk_mute(int t,int on){if(t<0||t>=NTRK)return;W.tr[t].mute=(int16_t)(on?1:0);
 if(on)worm_synth_off_trk(t);}   /* погасить звучащие голоса трека */
int worm_trk_muted(int t){return t>=0&&t<NTRK?W.tr[t].mute!=0:0;}
void worm_trig(const step_t *s,int trk){fire_step(s,trk);}

/* ---- часы дорожки ---- */
static void adv(int t){int len=W.tr[t].len,d=W.tr[t].dir;if(len<1)len=1;int *pos=&TS[t].pos;
 if(d==0){if(++*pos>=len){*pos=0;TS[t].loop++;}}else if(d==1){if(--*pos<0){*pos=len-1;TS[t].loop++;}}
 else if(d==2){*pos+=TS[t].pd;if(*pos>=len){TS[t].pd=-1;*pos=len>1?len-2:0;TS[t].loop++;}else if(*pos<0){TS[t].pd=1;*pos=len>1?1:0;}}else *pos=rnd()%len;}
static void trk_reset(int t){TS[t].loop=0;TS[t].et=0;TS[t].pd=1;TS[t].pos=W.tr[t].dir==1?W.tr[t].len-1:0;TS[t].due=NOW+1u;TLIVE[t]=1;W.trk_step[t]=TS[t].pos;}
/* режимы секвенсора раздельно по каналам: W.p[P_GLN..] — рабочая копия редактируемого канала.
 * Также копирует per-track звуковые параметры (W.p <-> W.tr[t].p) — см. worm_param_is_track. */
static void trk_patch_from(int t){for(int i=0;i<NP;i++)if(worm_param_is_track(i))W.p[i]=W.tr[t].p[i];}
static void trk_patch_to(int t){for(int i=0;i<NP;i++)if(worm_param_is_track(i))W.tr[t].p[i]=W.p[i];}
static volatile uint8_t len_chg[NTRK];   /* длина дорожки изменилась в UI: аудио-поток выровняет позицию по общей сетке */
void worm_trk_sync(void){if(W.mid){worm_mtrk_store(W.mpat);return;}int t=W.pat;if(t<0||t>=NTRK)return;if(W.tr[t].len!=W.p[P_GLN])len_chg[t]=1;W.tr[t].len=W.p[P_GLN];W.tr[t].dir=W.p[P_GDR];W.tr[t].spd=W.p[P_GSX];W.tr[t].swg=W.p[P_GSW];W.tr[t].pg=W.p[P_GPG];W.tr[t].vp=W.p[P_GVP];trk_patch_to(t);}
void worm_trk_reload(void){worm_midi_reload();if(W.mid){worm_mtrk_load(W.mpat);W.cur=(W.run||W.cur_run)?worm_midi_pos(W.mpat):-1;return;}int t=W.pat;if(t<0||t>=NTRK)t=0;
 W.p[P_GLN]=W.tr[t].len;W.p[P_GDR]=W.tr[t].dir;W.p[P_GSX]=W.tr[t].spd;W.p[P_GSW]=W.tr[t].swg;W.p[P_GPG]=W.tr[t].pg;W.p[P_GVP]=W.tr[t].vp;trk_patch_from(t);
 for(int i=0;i<NTRK;i++)if(TS[i].pos>=W.tr[i].len)TS[i].pos=0;   /* новая длина короче — не вылетать за неё */
 if(W.run||W.cur_run)W.cur=TS[t].pos;}
void worm_select_mtrack(int k){if(k<0||k>=NMTRK)return;worm_trk_sync();W.mid=1;W.mpat=(int8_t)k;worm_mtrk_load(k);
 if(!W.run&&W.cur_run)worm_midi_reset_track(k);                  /* курсор новой MIDI-дорожки начинает с начала */
 W.cur=(W.run||W.cur_run)?worm_midi_pos(k):-1;}
void worm_select_track(int t){if(t<0||t>=NTRK)return;worm_trk_sync();W.mid=0;W.pat=t;
 W.p[P_GLN]=W.tr[t].len;W.p[P_GDR]=W.tr[t].dir;W.p[P_GSX]=W.tr[t].spd;W.p[P_GSW]=W.tr[t].swg;W.p[P_GPG]=W.tr[t].pg;W.p[P_GVP]=W.tr[t].vp;
 trk_patch_from(t);
 if(!W.run&&W.cur_run)trk_reset(t);                      /* курсор нового канала начинает с начала */
 if(W.run||W.cur_run)W.cur=TS[t].pos;}
static void play_now(void){W.run=1;W.cur_run=1;for(int i=0;i<NPD;i++){PD[i].act=0;PD[i].hit=0;}for(int t=0;t<NTRK;t++)trk_reset(t);worm_midi_play();W.beat_ph=0;beatn=0;W.tick=0;W.cur=-1;clk_pend=1;clk_idx=0;clk_due=NOW+1u+mt_lat();beat_disp=-1;}   /* клик 1-й доли — вместе с первой нотой (с задержкой mTime=0) */
static void stop_now(void){W.run=0;clk_pend=0;beat_disp=-1;W.cur=-1;arp_reset();for(int i=0;i<NPD;i++){PD[i].act=0;PD[i].hit=0;}   /* отложенные/ретриггер-удары не звучат после STOP */
 for(int i=0;i<NVOICE;i++)worm_synth_note_off(i);worm_midi_stop();}
void worm_play(void){tr_req=1;}   /* выполнится в начале следующего блока рендера (аудио-поток) */
void worm_stop(void){tr_req=2;}
void worm_cursor_run(int on){W.cur_run=on;if(on){if(W.mid){worm_midi_cursor(1);}else{trk_reset(W.pat);W.cur=TS[W.pat].pos;}}else if(!W.run)W.cur=-1;}
void worm_init(void){memset(&W,0,sizeof W);W.slot=0;W.usb_mode=1;W.theme=0;   /* цветовая схема по умолчанию — DG1 */memset(PD,0,sizeof PD);memset(TS,0,sizeof TS);memset(TLIVE,0,sizeof TLIVE);   /* .pool не обнуляется */
 worm_patch_default(W.p);W.bpm=132.f;bpm_c=0.f;q16=0.f;bpm_sync();
 for(int t=0;t<NTRK;t++){worm_patch_default(W.tr[t].p);
  W.tr[t].len=W.p[P_GLN];W.tr[t].dir=W.p[P_GDR];W.tr[t].spd=W.p[P_GSX];W.tr[t].swg=W.p[P_GSW];W.tr[t].pg=W.p[P_GPG];W.tr[t].vp=W.p[P_GVP];TS[t].pd=1;}
 W.pd=1;worm_trk_fix();arp_reset();
 for(int h=0;h<=16;h++)for(int n=1;n<=16;n++)eu_mask(h>n?n:h,n);   /* рисунки euclid считаем заранее, не в аудио-потоке */
 worm_synth_init();worm_midi_init();worm_synth_set_patch(W.p);}

/* ---- рендер ----
 * Блок режется на отрезки, кратные SEQ_BLK=64 сэмпла (= блок синта BL), и события срабатывают на границе отрезка, как раньше —
 * на границе того 64-сэмпловика, в котором наступил срок (дрожание <= 1.5 мс, темп не плывёт: перелёт переносится в следующий интервал).
 * Отрезок тянется до ближайшего события одним вызовом синта: пока ничего не происходит, секвенсор не делает ни одного цикла. */
#define SEQ_BLK 64
static uint32_t beat_len(void){uint32_t b=worm_step_len()*4u;return b?b:1u;}
/* сэмплов до ближайшего события секвенсора или MIDI-движка (<= 0: уже пора) */
static int32_t next_until(void){int32_t m=0x7FFFFFFF,x;
 for(int t=0;t<NTRK;t++)if(run_trk(t)){x=LEFT(TS[t].due);if(x<m)m=x;}
 for(int i=0;i<NPD;i++)if(PD[i].act){x=LEFT(PD[i].due)-(PD[i].hit?0:1);if(x<m)m=x;}   /* первый удар: «вот-вот» (<= 1 сэмпл) играет на этой же границе */
 if(W.p[P_ARM]&&arp_n&&arp_set){x=LEFT(arp_due);if(x<m)m=x;}
 if(clk_pend){x=LEFT(clk_due);if(x<m)m=x;}
 {uint32_t bl=beat_len();x=W.beat_ph<bl?(int32_t)(bl-W.beat_ph):0;if(x<m)m=x;}
 x=worm_midi_samples();if(x<m)m=x;
 return m;}
/* события на границе отрезка: порядок как раньше — (MIDI уже продвинут), отложенные триггеры, арпеджио, шаги дорожек, «немедленные» удары, метроном */
static void service(int span){
 for(int i=0;i<NPD;i++)if(PD[i].act&&LATE(PD[i].due))pd_hit(&PD[i]);
 if(W.p[P_ARM]&&arp_n&&arp_set&&LATE(arp_due)){uint32_t ss=arp_ss();arp_tick();arp_due+=ss;if(LATE(arp_due))arp_due=NOW+ss;}
 for(int t=0;t<NTRK;t++)if(run_trk(t)&&LATE(TS[t].due)){int i=TS[t].pos;uint32_t ss=step_samples_t(t);if(!W.mid&&t==W.pat)W.cur=i;
  W.trk_step[t]=i;                       /* текущий шаг трека (для automation-интерполяции) */
  if(W.run)worm_trig(&W.pats[t].s[i],t);
  TS[t].et++;adv(t);
  int sw=W.tr[t].swg;int32_t nx=(int32_t)ss;if(TS[t].pos%2)nx+=(int32_t)((sw-50)/50.f*ss*.5f);else if(i%2)nx-=(int32_t)((sw-50)/50.f*ss*.5f);
  TS[t].due+=(uint32_t)(nx>0?nx:1);if(LATE(TS[t].due))TS[t].due=NOW+1u;}
 /* mTime = -12 (задержка 0): триггер на этой же границе, а не через отрезок */
 for(int i=0;i<NPD;i++)if(PD[i].act&&!PD[i].hit&&LEFT(PD[i].due)<=1){PD[i].act=0;PD[i].due=NOW;pd_hit(&PD[i]);}
 /* общий счётчик четверти: BPM-мигание и метроном (идёт и в STOP) */
 {uint32_t blen=beat_len();W.beat_ph+=(uint32_t)span;
  if(W.beat_ph>=blen){W.beat_ph-=blen;beatn++;if((beatn&3u)==0)barn++;if(W.run){clk_pend=1;clk_idx=(uint8_t)(beatn&3u);clk_due=NOW+mt_lat();}}}
 if(clk_pend&&LATE(clk_due)){clk_pend=0;if(W.run){beat_disp=(int8_t)clk_idx;if(W.p[P_GCK])worm_synth_click(clk_idx==0,W.p[P_GMV]);}}}
/* смена длины: позиция = число шагов от старта по сетке новой длины (как у дорожек, не менявших длину) — иначе дорожка
 * «уезжает» на старую фазу и возвращается только через НОК длин. Только вперёд/назад; ping-pong и random не трогаем. */
static void len_realign(void){for(int t=0;t<NTRK;t++)if(len_chg[t]){len_chg[t]=0;int len=W.tr[t].len;if(len<1)len=1;int d=W.tr[t].dir;
 if(d==0)TS[t].pos=(int)(TS[t].et%(uint32_t)len);else if(d==1)TS[t].pos=len-1-(int)(TS[t].et%(uint32_t)len);
 else if(TS[t].pos>=len)TS[t].pos=0;
 if(!W.mid&&t==W.pat&&(W.run||W.cur_run))W.cur=TS[t].pos;}}
void worm_render(int16_t *out,int frames){seq_drain();len_realign();worm_synth_set_patch(W.p);worm_midi_poll();bpm_sync();
 for(int t=0;t<NTRK;t++){int r=run_trk(t);if(r&&!TLIVE[t])TS[t].due=NOW+1u;TLIVE[t]=(uint8_t)r;}   /* фронт запуска без trk_reset: часы с начала блока */
 if(W.p[P_ARM]&&arp_n&&!arp_set){arp_set=1;arp_due=NOW+arp_ss();}                                    /* таймер арпеджио стартует с первого блока после первой ноты */
 int done=0;
 while(done<frames){int rem=frames-done,span;int32_t u=next_until();
  if(u<=0)span=SEQ_BLK;else if(u>=rem)span=rem;else span=(int)((u+SEQ_BLK-1)&~(SEQ_BLK-1));
  if(span>rem)span=rem;
  worm_synth_offset(done);worm_synth_render(out+done*2,span);done+=span;NOW+=(uint32_t)span;
  worm_midi_advance(span);service(span);}}
