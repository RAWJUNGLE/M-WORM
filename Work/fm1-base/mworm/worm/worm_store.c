/* WORM-FM1 persistence on the 1 MiB SPI NOR (окно данных 0x97000..0xE0000).
 *
 * ПАТТЕРН = снимок всего: NTRK (6) секвенций + звук каждой дорожки (патч, режимы секвенсора, mute, automation) + глобальные FX/параметры.
 * Банк = NPAT (16) таких паттернов, упакованных подряд в BANK_SZ (9 секторов по 4 KB); NBANK (8) банков подряд от ST_BASE,
 * сектор проекта (BPM, последний банк/паттерн) — в самом конце окна. 8 x 36 KB + 4 KB = всё окно данных.
 *
 * Формат банка:  'WPB1' | NPAT x { u16 len, байты записи } | 0xFF...    (len = 0 — паттерн пуст)
 * Запись паттерна: для каждой из дорожек 1-4 (ST_TRK0): секвенция (u16 n + шаги по 13+3*nl байт), 7 байт режимов (len,dir,spd,swg,pg,vp,mute),
 *   патч дорожки (битовая карта «не по умолчанию» + значения zigzag-LEB128), u8 naut + точки (step,id,v16);
 * затем глобальные параметры (карта + значения), хвост MIDI, затем хвост дорожек 5-6: u8 маска тронутых + их записи в том же формате
 * (маска 0 — хвоста нет: паттерн без дорожек 5-6 побайтно как в 4-дорожечной прошивке, старые банки читаются: дорожки 5-6 пусты). Паттерн хранится целиком, если в нём есть шаги/automation ИЛИ хоть одна ручка, режим,
 * mute, FX или MIDI-дорожка не по умолчанию. Нетронутый паттерн хранится как пустой (len=0): при выборе он даёт ЗАВОДСКОЕ состояние
 * (чистые секвенции + весь звук/FX/режимы по умолчанию), от предыдущего паттерна ничего не наследуется («новый пустой паттерн»).
 *
 * RAM: НЕ весь банк, а MRU-кэш из MRU_N (3) упакованных записей по MRU_CAP байт + один секторный буфер sbuf (4 KB).
 * Банк остаётся во флеше; в RAM только таблица смещений (roff/rlen, 16 записей). Переключение паттерна: упаковать уходящий в кэш,
 * распаковать входящий из кэша (промах — чтение записи из флеша). worm_store_prefetch(m) подтягивает запись заранее (pend_pat:
 * переключение на границе такта не ходит во флеш). Изменённая запись помечена dirty и ждёт flush: пока играет секвенсор, флеш не
 * трогаем, поэтому выкинуть из кэша можно только чистую запись; если все 3 грязные — переключение отклоняется («FULL»), пока не стоп.
 * Запись во флеш — ленивая (worm_store_flush из main loop, STOP, через FLUSH_DELAY_MS после правки), по одному сектору за вызов.
 * Запись меняет длину -> хвост банка сдвигается на dl: сектора переписываются потоково (при росте — с конца, при усадке — с начала),
 * так что каждый выходной сектор собирается из ещё не тронутых старых данных. Формат банка прежний (WPB1). */
#include <string.h>
#include "worm.h"
#define ST_BASE 0x97000u
#define SECT 4096u
#define NSEC 9u
#define BANK_SZ (NSEC*SECT)                       /* 0x9000 */
#define PROJ_ADDR (ST_BASE+NBANK*BANK_SZ)         /* 0xDF000: последний сектор окна данных */
#define BANK_MAGIC 0x57504231u                    /* 'WPB1' */
#define PROJ_MAGIC 0x57504A31u
#define PRS_MAGIC 0x57505331u                       /* 'WPS1' — пользовательские пресеты в секторе проекта */
#define PRS_OFF 0x10u                               /* первые 16 Б — проект (proj_t) */
#define PRS_BASE (PROJ_ADDR+PRS_OFF+8u)             /* 8 Б заголовка: magic/version/count/rsv */
#define PRS_REC 126u                                /* 10 имя + 1 used + PRS_NVALS значений + 1 XOR: 32 x 126 + 24 = 4056 < 4096 */
#define PRS_NAM 10u
#define LM_OFF 4056u                                /* хвост сектора проекта (40 Б свободно): 'LM' + sw,chain,order + набор банка (6 x id16 + 6 x trk) = 23 Б */
volatile int worm_prs_busy;                         /* во время erase/write проекта пресет шага берёт заводской звук */
#define FLUSH_DELAY_MS 1500u
#define NPB ((NP+7)/8)
#define ST_TRK0 4                                  /* дорожки 0..3 лежат в записи как раньше; остальные — в хвосте (появился с 6 дорожками) */
#define REC_MAX 8192u                             /* потолок записи одного паттерна при упаковке (плотнее — «BANK FULL») */

/* .bss, не .pool: пул устройства занят почти целиком (свободно ~10 KB), а в .bss после уборки старых буферов ~55 KB */
#ifndef PLAT_HOST
#define MW_NOINIT __attribute__((section(".noinit")))   /* не инициализируется: буфер всегда перезаписывается перед чтением */
#else
#define MW_NOINIT
#endif
#ifndef MRU_N
#define MRU_N 3
#endif
#ifndef MRU_CAP
#define MRU_CAP REC_MAX                           /* потолок упакованного паттерна; меньше = меньше SRAM, но раньше «FULL» у плотных паттернов */
#endif
typedef struct { int8_t slot; uint8_t dirty, fv; uint16_t len; uint32_t t, fh; } mru_t;   /* slot -1: пусто; len — длина записи в буфере (dirty: новая); fh — хеш записи, как она лежит во флеше (fv: известен) */
static uint8_t mbuf[MRU_N][MRU_CAP];              /* упакованные записи паттернов (MRU): в .bss — .noinit на устройстве всего 12 КБ */
static MW_NOINIT uint8_t sbuf[SECT];              /* сектор при сдвиге банка; 4К-буфер сектора проекта/пресетов (вызовы не вложены) */
static mru_t mru[MRU_N];static uint32_t mru_tick;
static uint8_t cur_keep;                          /* паттерн загружен из записи, где был только темп (шагов/звука нет): хранить дальше */
static float cur_bpm0;                            /* BPM, с которым паттерн загружен (хранённый или унаследованный): отличие от W.bpm = паттерн правили */
#define BPM_TAG 0xB7u                              /* хвост записи паттерна: BPM_TAG, u16 BPM*10 (LE) — темп ПАТТЕРНА */
static int img_bank=-1;                           /* какой банк в img (-1: нет) */
static uint32_t roff[NPAT],rlen[NPAT],uend;       /* где запись паттерна, её длина; конец занятого места */
static int bank_dirty,flush_pos;static uint32_t dirty_t;

/* ---- упаковка ---- */
static uint8_t *put_vi(uint8_t *o,int v){unsigned z=((unsigned)v<<1)^(unsigned)(v>>31);while(z>=128){*o++=(uint8_t)((z&127u)|128u);z>>=7;}*o++=(uint8_t)z;return o;}
static int get_vi(const uint8_t **pp,const uint8_t *e,int *v){unsigned z=0;int sh=0;const uint8_t *p=*pp;
 for(;;){if(p>=e||sh>14)return -1;uint8_t c=*p++;z|=(unsigned)(c&127u)<<sh;sh+=7;if(!(c&128u))break;}*pp=p;*v=(int)(z>>1)^-(int)(z&1u);return 0;}
static int step_used(const step_t *s){return s->on||s->nl||s->mt;}
static int seq_has(const pattern_t *p){for(int j=0;j<NSTEP;j++)if(step_used(&p->s[j]))return 1;return 0;}
static int mtrk_used(int k){return seq_has(&W.mpats[k])||!worm_mtrk_is_default(k);}   /* MIDI-дорожка тронута: шаги или параметры/режимы не по умолчанию */
/* глобальные параметры: не per-track и не рабочие копии режимов секвенсора (они хранятся в W.tr[t]) */
static int gsel(int i){return !worm_param_is_track(i)&&i!=P_GDR&&i!=P_GLN&&i!=P_GSX&&i!=P_GSW&&i!=P_GPG&&i!=P_GVP;}
/* звук дорожек (ручки OSC/AMP/FLT/ADSR/LFO/SEND/TRANSIENT), режимы секвенсора, mute, глобальные FX/мастер или ARP>MIDI отличаются от умолчания */
static int trk_snd_touched(int t){const int16_t *p=W.tr[t].p;
 for(int i=0;i<NP;i++)if(worm_param_is_track(i)&&p[i]!=worm_pinfo[i].def)return 1;
 return W.tr[t].len!=worm_pinfo[P_GLN].def||W.tr[t].dir!=worm_pinfo[P_GDR].def||W.tr[t].spd!=worm_pinfo[P_GSX].def||
        W.tr[t].swg!=worm_pinfo[P_GSW].def||W.tr[t].pg!=worm_pinfo[P_GPG].def||W.tr[t].vp!=worm_pinfo[P_GVP].def||W.tr[t].mute;}
static int snd_touched(void){
 for(int t=0;t<NTRK;t++)if(trk_snd_touched(t))return 1;
 for(int i=0;i<NP;i++)if(gsel(i)&&W.p[i]!=worm_pinfo[i].def)return 1;
 return W.p[P_AMO]!=worm_pinfo[P_AMO].def;}
/* паттерн «записан», если в нём что-то есть: шаги, automation, звук/FX/режимы не по умолчанию или тронутая MIDI-дорожка.
 * Совсем нетронутый (всё по умолчанию) хранится как пустой (len = 0). Рабочая копия W.p сначала уходит в W.tr[] (sync). */
static int trk_used(int t){return seq_has(&W.pats[t])||W.tr[t].naut>0||trk_snd_touched(t);}   /* дорожка тронута: шаги, automation, звук/режимы/mute не по умолчанию */
static int cur_content(void){worm_trk_sync();
 for(int t=0;t<NTRK;t++)if(seq_has(&W.pats[t])||W.tr[t].naut>0)return 1;
 for(int k=0;k<NMTRK;k++)if(mtrk_used(k))return 1;
 return snd_touched();}
static int cur_nonempty(void){return cur_content()||cur_keep||W.bpm!=cur_bpm0||worm_lm_own;}   /* свой набор макросов LIVE — тоже содержимое */   /* темп паттерна изменён (или хранится один) — паттерн записывается, даже если он пуст */
/* Шаг: 13 байт заголовка (формат прежний) + при необходимости 2 байта расширения (bit7 байта len/RETRIG = «расширение есть»):
 *   [LEN для банка (worm_len_store)] [микротайминг, % шага со знаком]
 * Без расширения (LEN из старой шкалы 1/2/4/8/16 шагов и сдвиг кратен 25%) шаг занимает те же 13 байт, что в старых банках:
 * len = 0..4 (старая шкала), mt = тики 1/24 шага. Старые банки читаются без расширения: LEN пересчитывается, тики -> проценты. */
static int mt_to_ticks(int pct,int *tk){if(pct%25)return 0;*tk=pct*24/100;return 1;}   /* 25% = 6 тиков: только точные значения */
static uint8_t *pack_seq(uint8_t *o,const uint8_t *lim,const pattern_t *p){uint8_t *cnt=o;if(o+2>lim)return 0;o+=2;uint16_t n=0;
 for(int i=0;i<NSTEP;i++){const step_t *s=&p->s[i];if(!step_used(s))continue;
  int lsv=worm_len_store(s->len),tk=0,ext=lsv>4||!mt_to_ticks(s->mt,&tk);
  if(o+13+(ext?2:0)+3*s->nl>lim)return 0;
  /* RETRIG — в свободных битах (формат 13 байт не меняется, старые банки читаются как «ретриггер выключен»):
   * on = bit0 | R.VEL(7 бит со знаком)<<1, len = LEN(3 бита) | RETRIG<<3 | расширение<<7, cond = COND(4 бита) | R.LEN<<4 */
  *o++=(uint8_t)i;*o++=(uint8_t)((s->on&1)|((s->rtv&0x7F)<<1));*o++=s->note;*o++=s->vel;*o++=(uint8_t)((ext?0:lsv)|((s->rtr&15)<<3)|(ext?0x80:0));*o++=s->prob;*o++=(uint8_t)((s->cond&15)|((s->rtl&7)<<4));*o++=(uint8_t)(ext?0:tk);*o++=s->nl;
  *o++=s->chord[0];*o++=s->chord[1];*o++=s->chord[2];*o++=s->chord[3];
  if(ext){*o++=(uint8_t)lsv;*o++=(uint8_t)(int8_t)s->mt;}
  for(int k=0;k<s->nl;k++){*o++=s->lk[k].id;*o++=(uint8_t)(s->lk[k].v&0xFF);*o++=(uint8_t)((s->lk[k].v>>8)&0xFF);}n++;}
 cnt[0]=(uint8_t)(n&0xFF);cnt[1]=(uint8_t)(n>>8);return o;}
static const uint8_t *unpack_seq(const uint8_t *o,const uint8_t *e,pattern_t *d){if(o+2>e)return 0;int n=o[0]|(o[1]<<8);o+=2;
 for(int j=0;j<n;j++){if(o+13>e)return 0;step_t *s=&d->s[o[0]&127];s->on=o[1]&1;s->note=o[2];s->vel=o[3];s->prob=o[5];s->cond=o[6]&15;
  if(s->cond>15)s->cond=0;
  {int rv=(o[1]>>1)&0x7F;if(rv&0x40)rv-=128;if(rv<-63)rv=-63;s->rtv=(int8_t)rv;s->rtr=(uint8_t)((o[4]>>3)&15);if(s->rtr>9)s->rtr=0;s->rtl=(uint8_t)((o[6]>>4)&7);if(s->rtl>5)s->rtl=0;}
  int nl=o[8],ext=(o[4]&0x80)!=0;s->nl=(uint8_t)(nl>MAXLK?MAXLK:nl);for(int c=0;c<4;c++)s->chord[c]=o[9+c];
  if(ext){if(o+15>e)return 0;s->len=(uint8_t)worm_len_load(o[13]);int m=(int8_t)o[14];s->mt=(int8_t)(m<-MT_MAX?-MT_MAX:m>MT_MAX?MT_MAX:m);o+=15;}
  else{s->len=(uint8_t)worm_len_load(o[4]&7);int m=((int8_t)o[7])*100;m=(m>=0?m+12:m-12)/24;s->mt=(int8_t)(m<-MT_MAX?-MT_MAX:m>MT_MAX?MT_MAX:m);o+=13;}   /* старая запись: тики 1/24 шага -> % */
  if(o+3*nl>e)return 0;
  for(int k=0;k<nl;k++){if(k<MAXLK){s->lk[k].id=o[0];s->lk[k].v=(int16_t)(o[1]|(o[2]<<8));}o+=3;}}
 return o;}
/* патч: карта отличий от worm_pinfo[].def для выбранного набора (track=1: per-track, 0: глобальные) + значения */
static int psel(int i,int track){return track?worm_param_is_track(i):gsel(i);}
static uint8_t *pack_patch(uint8_t *o,const uint8_t *lim,const int16_t *p,int track){if(o+NPB+3*NP>lim)return 0;
 uint8_t *bm=o;memset(bm,0,NPB);o+=NPB;
 for(int i=0;i<NP;i++)if(psel(i,track)&&p[i]!=worm_pinfo[i].def){bm[i>>3]|=(uint8_t)(1u<<(i&7));o=put_vi(o,i==P_NL?worm_len_store(p[i]):p[i]);}   /* LEN: старая шкала 0..4 остаётся как была */
 return o;}
static const uint8_t *unpack_patch(const uint8_t *o,const uint8_t *e,int16_t *p,int track){if(o+NPB>e)return 0;const uint8_t *bm=o;o+=NPB;
 for(int i=0;i<NP;i++){if(!psel(i,track))continue;int16_t v=worm_pinfo[i].def;
  if(bm[i>>3]&(1u<<(i&7))){int x;if(get_vi(&o,e,&x))return 0;if(i==P_NL)x=worm_len_load(x);if(x<worm_pinfo[i].mn)x=worm_pinfo[i].mn;if(x>worm_pinfo[i].mx)x=worm_pinfo[i].mx;v=(int16_t)x;}
  p[i]=v;}
 return o;}
/* MIDI-дорожка: секвенция, 6 режимов (len,dir,spd,swg,pg,vp), значения параметров (карта отличий от умолчания + zigzag-LEB128) */
#define NMPB ((NMP+7)/8)
static uint8_t *pack_mtrk(uint8_t *o,const uint8_t *lim,int k){
 if(!(o=pack_seq(o,lim,&W.mpats[k])))return 0;
 if(o+6+NMPB+3*NMP>lim)return 0;
 *o++=(uint8_t)W.mt[k].len;*o++=(uint8_t)W.mt[k].dir;*o++=(uint8_t)W.mt[k].spd;*o++=(uint8_t)W.mt[k].swg;*o++=(uint8_t)W.mt[k].pg;*o++=(uint8_t)W.mt[k].vp;
 uint8_t *bm=o;memset(bm,0,NMPB);o+=NMPB;
 for(int q=0;q<NMP;q++)if(W.mt[k].p[q]!=worm_mtrk_def(k,q)){bm[q>>3]|=(uint8_t)(1u<<(q&7));o=put_vi(o,worm_mpid(q)==P_NL?worm_len_store(W.mt[k].p[q]):W.mt[k].p[q]);}
 return o;}
static const uint8_t *unpack_mtrk(const uint8_t *o,const uint8_t *e,int k){
 memset(&W.mpats[k],0,sizeof W.mpats[k]);
 if(!(o=unpack_seq(o,e,&W.mpats[k])))return 0;
 if(o+6+NMPB>e)return 0;
 W.mt[k].len=o[0]<1?1:o[0]>NSTEP?NSTEP:o[0];W.mt[k].dir=o[1]>3?0:o[1];W.mt[k].spd=o[2]>2?1:o[2];W.mt[k].swg=o[3]<50?50:o[3]>75?75:o[3];
 W.mt[k].pg=o[4]<1?1:o[4]>8?8:o[4];W.mt[k].vp=o[5]<1?1:o[5]>8?8:o[5];o+=6;
 const uint8_t *bm=o;o+=NMPB;
 for(int q=0;q<NMP;q++){int v=worm_mtrk_def(k,q);
  if(bm[q>>3]&(1u<<(q&7))){int x;if(get_vi(&o,e,&x))return 0;const pinfo_t *pi=&worm_pinfo[worm_mpid(q)];if(worm_mpid(q)==P_NL)x=worm_len_load(x);if(x<pi->mn)x=pi->mn;if(x>pi->mx)x=pi->mx;v=x;}
  W.mt[k].p[q]=(int16_t)v;}
 return o;}
/* одна звуковая дорожка: секвенция, 7 байт режимов, патч, automation */
static uint8_t *pack_trk(uint8_t *o,const uint8_t *lim,int t){
 if(!(o=pack_seq(o,lim,&W.pats[t])))return 0;
 if(o+7>lim)return 0;
 *o++=(uint8_t)W.tr[t].len;*o++=(uint8_t)W.tr[t].dir;*o++=(uint8_t)W.tr[t].spd;*o++=(uint8_t)W.tr[t].swg;*o++=(uint8_t)W.tr[t].pg;*o++=(uint8_t)W.tr[t].vp;*o++=(uint8_t)(W.tr[t].mute!=0);
 if(!(o=pack_patch(o,lim,W.tr[t].p,1)))return 0;
 int na=W.tr[t].naut;if(na<0)na=0;if(na>NAUT)na=NAUT;
 if(o+1+4*na>lim)return 0;
 *o++=(uint8_t)na;for(int k=0;k<na;k++){const auto_pt_t *a=&W.tr[t].aut[k];*o++=a->step;*o++=a->id;*o++=(uint8_t)(a->v&0xFF);*o++=(uint8_t)((a->v>>8)&0xFF);}
 return o;}
static const uint8_t *unpack_trk(const uint8_t *o,const uint8_t *e,int t){
 memset(&W.pats[t],0,sizeof W.pats[t]);
 if(!(o=unpack_seq(o,e,&W.pats[t])))return 0;
 if(o+7>e)return 0;
 W.tr[t].len=o[0]<1?1:o[0]>NSTEP?NSTEP:o[0];W.tr[t].dir=o[1]>3?0:o[1];W.tr[t].spd=o[2]>2?1:o[2];W.tr[t].swg=o[3]<50?50:o[3]>75?75:o[3];
 W.tr[t].pg=o[4]<1?1:o[4]>8?8:o[4];W.tr[t].vp=o[5]<1?1:o[5]>8?8:o[5];worm_trk_mute(t,o[6]!=0);o+=7;
 if(!(o=unpack_patch(o,e,W.tr[t].p,1)))return 0;
 if(o+1>e)return 0;int na=*o++;if(na>NAUT)na=NAUT;if(o+4*na>e)return 0;
 W.tr[t].naut=(int16_t)na;memset(W.tr[t].aut,0,sizeof W.tr[t].aut);
 for(int k=0;k<na;k++){W.tr[t].aut[k].step=o[0];W.tr[t].aut[k].id=o[1];W.tr[t].aut[k].v=(int16_t)(o[2]|(o[3]<<8));o+=4;}
 return o;}
/* заводская дорожка: пустая секвенция, звук/режимы по умолчанию, без mute и automation */
static void trk_default(int t){
 memset(&W.pats[t],0,sizeof W.pats[t]);worm_patch_default(W.tr[t].p);
 W.tr[t].len=worm_pinfo[P_GLN].def;W.tr[t].dir=worm_pinfo[P_GDR].def;W.tr[t].spd=worm_pinfo[P_GSX].def;
 W.tr[t].swg=worm_pinfo[P_GSW].def;W.tr[t].pg=worm_pinfo[P_GPG].def;W.tr[t].vp=worm_pinfo[P_GVP].def;
 worm_trk_mute(t,0);W.tr[t].naut=0;memset(W.tr[t].aut,0,sizeof W.tr[t].aut);}
static uint8_t *pack_rec(uint8_t *o,const uint8_t *lim){
 if(!cur_nonempty())return o;                                 /* пустой паттерн: len = 0 */
 for(int t=0;t<ST_TRK0;t++)if(!(o=pack_trk(o,lim,t)))return 0;
 if(!(o=pack_patch(o,lim,W.p,0)))return 0;
 /* хвост MIDI (появился позже звуковой части: старые записи заканчиваются глобальными параметрами): маска тронутых MIDI-дорожек, ARP>MIDI, затем сами дорожки */
 {unsigned mask=0;for(int k=0;k<NMTRK;k++)if(mtrk_used(k))mask|=1u<<k;
  if(o+2>lim)return 0;*o++=(uint8_t)mask;*o++=(uint8_t)(W.p[P_AMO]&0xFF);
  for(int k=0;k<NMTRK;k++)if(mask&(1u<<k)){if(!(o=pack_mtrk(o,lim,k)))return 0;}}
 /* хвост дорожек 5-6 (появился с 6 дорожками): маска тронутых и их записи; нет тронутых — хвоста нет */
 {unsigned m2=0;for(int t=ST_TRK0;t<NTRK;t++)if(trk_used(t))m2|=1u<<(t-ST_TRK0);
  if(o+1>lim)return 0;*o++=(uint8_t)m2;                      /* маска пишется всегда (0 — дорожек 5-6 нет): за ней идёт BPM-хвост */
  for(int t=ST_TRK0;t<NTRK;t++)if(m2&(1u<<(t-ST_TRK0))){if(!(o=pack_trk(o,lim,t)))return 0;}}
 /* BPM паттерна (старые прошивки хвост игнорируют, старые записи без него — паттерн играет на текущем темпе проекта) */
 {int b=(int)(W.bpm*10.f+.5f);if(b<400)b=400;if(b>3000)b=3000;
  if(o+3>lim)return 0;*o++=BPM_TAG;*o++=(uint8_t)(b&0xFF);*o++=(uint8_t)(b>>8);}
 /* набор макросов LIVE этого паттерна (хвост после BPM; нет хвоста — паттерн играет набором банка; старые прошивки хвост игнорируют) */
 if(worm_lm_own){if(o+19>lim)return 0;o+=worm_lm_store_pack(o);}
 return o;}
static int unpack_rec(const uint8_t *o,const uint8_t *e){worm_aut_gen++;   /* новые точки automation: синт сбросит кеш */
 for(int t=ST_TRK0;t<NTRK;t++)trk_default(t);                 /* дорожки 5-6 берутся из хвоста; нет хвоста (старая запись) — заводские */
 for(int t=0;t<ST_TRK0;t++)if(!(o=unpack_trk(o,e,t)))return -1;
 if(!(o=unpack_patch(o,e,W.p,0)))return -1;
 if(o+2>e){memset(W.mpats,0,sizeof W.mpats);return 0;}   /* старая запись без MIDI: MIDI-секвенции пусты, MIDI-параметры текущие */
 {unsigned mask=o[0];int amo=o[1];o+=2;W.p[P_AMO]=(int16_t)(amo>4?0:amo);
  for(int k=0;k<NMTRK;k++){
   if(mask&(1u<<k)){if(!(o=unpack_mtrk(o,e,k)))return -1;}
   else{memset(&W.mpats[k],0,sizeof W.mpats[k]);worm_mtrk_default(k);}}}
 if(o<e){unsigned m2=*o++;                                    /* хвост дорожек 5-6 */
  for(int t=ST_TRK0;t<NTRK;t++)if(m2&(1u<<(t-ST_TRK0))){if(!(o=unpack_trk(o,e,t)))return -1;}}
 if(o+3<=e&&o[0]==BPM_TAG){int b=o[1]|(o[2]<<8);if(b>=400&&b<=3000)W.bpm=(float)b/10.f;o+=3;}   /* темп паттерна */
 worm_lm_own=0;if(o<e&&o[0]==0xB9&&worm_lm_store_unpack(o,(int)(e-o)))worm_lm_own=1;   /* свой набор макросов LIVE */
 return 0;}
/* «новый пустой паттерн» = заводское состояние: секвенции и automation чистые, звук каждой дорожки (OSC/AMP/FLT/ADSR/LFO/SEND/TRANSIENT),
 * режимы секвенсора, mute, глобальные FX/мастер, ARP и MIDI-дорожки — по умолчанию. От предыдущего паттерна не остаётся ничего (BPM — общий проектный). */
static void new_empty(void){worm_aut_gen++;worm_lm_own=0;
 memset(W.pats,0,sizeof W.pats);memset(W.mpats,0,sizeof W.mpats);
 for(int t=0;t<NTRK;t++)trk_default(t);
 for(int i=0;i<NP;i++)if(gsel(i))W.p[i]=worm_pinfo[i].def;
 W.p[P_AMO]=worm_pinfo[P_AMO].def;
 for(int k=0;k<NMTRK;k++)worm_mtrk_default(k);}

/* ---- банк во флеше + MRU-кэш записей ---- */
static int virt_blank;                            /* банк во флеше невалиден: «старые» байты — виртуальный пустой банк до первой записи */
static int fl_p=-1,fl_s,fl_s1,fl_dir;static int32_t fl_dl;static uint32_t fl_uend2;   /* текущая задача сдвига (fl_p<0: нет) */
static uint32_t bank_base(void){return ST_BASE+(uint32_t)img_bank*BANK_SZ;}
/* старые байты банка [a,a+n) -> d (виртуальный пустой банк: magic, 16 x len=0, 0xFF) */
static int old_read(uint32_t a,uint8_t *d,uint32_t n){
 if(!virt_blank)return worm_flash_read(bank_base()+a,d,n);
 uint32_t m=BANK_MAGIC;for(uint32_t i=0;i<n;i++){uint32_t x=a+i;d[i]=x<4?(uint8_t)(m>>(8*x)):x<4+2*NPAT?0:0xFF;}return 0;}
static int parse_bank(void){uint32_t m=0;uint8_t h[2];if(worm_flash_read(bank_base(),&m,4)||m!=BANK_MAGIC)return -1;uint32_t o=4;
 for(int p=0;p<NPAT;p++){if(o+2>BANK_SZ||worm_flash_read(bank_base()+o,h,2))return -1;uint32_t n=(uint32_t)(h[0]|(h[1]<<8));if(o+2+n>BANK_SZ)return -1;roff[p]=o+2;rlen[p]=n;o+=2+n;}
 uend=o;return 0;}
static void blank_bank(void){uint32_t o=4;for(int p=0;p<NPAT;p++){roff[p]=o+2;rlen[p]=0;o+=2;}uend=o;virt_blank=1;}
static int flush_step(int *done);
static void job_sync(void){while(fl_p>=0){int done;if(flush_step(&done)<0){fl_p=-1;break;}}}   /* дописать начатый сдвиг банка: пока он идёт, записи во флеше читать нельзя */
static int mru_find(int p){for(int i=0;i<MRU_N;i++)if(mru[i].slot==p)return i;return -1;}
static uint32_t pending_end(int skip){int32_t e=(int32_t)uend;for(int i=0;i<MRU_N;i++)if(mru[i].slot>=0&&mru[i].dirty&&mru[i].slot!=skip)e+=(int32_t)mru[i].len-(int32_t)rlen[mru[i].slot];return (uint32_t)e;}
static uint32_t fnv(uint32_t h,const uint8_t *d,uint32_t n){for(uint32_t i=0;i<n;i++){h^=d[i];h*=16777619u;}return h;}
/* запись в буфере == запись во флеше. Хеш флеш-версии известен (читали при подгрузке / записали сами) — тогда флеш не читаем:
 * переключение паттерна на границе такта обходится без обращений к флешу */
static int mru_same_as_flash(int i){const int p=mru[i].slot;if(virt_blank)return mru[i].len==0&&rlen[p]==0;
 if(mru[i].len!=rlen[p])return 0;if(rlen[p]==0)return 1;
 if(!mru[i].fv){uint8_t t[128];uint32_t h=2166136261u;for(uint32_t o=0;o<rlen[p];o+=sizeof t){uint32_t n=rlen[p]-o<sizeof t?rlen[p]-o:sizeof t;if(worm_flash_read(bank_base()+roff[p]+o,t,n))return 0;h=fnv(h,t,n);}mru[i].fh=h;mru[i].fv=1;}
 return fnv(2166136261u,mbuf[i],mru[i].len)==mru[i].fh;}
/* свободная/вытесняемая ячейка: пусто -> самая старая чистая (не keep1/keep2). -1: все заняты грязными/защищёнными */
static int mru_victim(int keep1,int keep2){int b=-1;
 for(int i=0;i<MRU_N;i++){if(mru[i].slot<0)return i;if(mru[i].slot==keep1||mru[i].slot==keep2||mru[i].dirty)continue;if(b<0||mru[i].t<mru[b].t)b=i;}return b;}
/* то же, но если все ячейки грязные и секвенсор стоит — сначала сбросить грязные во флеш (при игре флеш не трогаем: -1) */
static int mru_alloc(int keep1,int keep2){int i=mru_victim(keep1,keep2);if(i>=0||W.run)return i;
 for(;;){int done,r=flush_step(&done);if(r<0){fl_p=-1;break;}if(done)break;}
 return mru_victim(keep1,keep2);}
/* запись паттерна p -> ячейка кэша (читает из флеша при промахе). -1: места нет (все ячейки грязные/защищены). *empty=1: записи нет (пустой паттерн) */
static int mru_load(int p,int keep1){int i=mru_find(p);if(i>=0){mru[i].t=++mru_tick;return i;}
 job_sync();
 if(rlen[p]==0)return -2;                                   /* пустой паттерн: кэшировать нечего */
 i=mru_alloc(keep1,p);if(i<0)return -1;
 if(worm_flash_read(bank_base()+roff[p],mbuf[i],rlen[p]))return -3;
 mru[i].slot=(int8_t)p;mru[i].dirty=0;mru[i].len=(uint16_t)rlen[p];mru[i].t=++mru_tick;mru[i].fh=fnv(2166136261u,mbuf[i],rlen[p]);mru[i].fv=1;return i;}
/* текущее состояние -> запись паттерна W.slot в ячейку кэша (dirty, если отличается от флеша); -1 — не влезло/нет места */
static int commit_cur(void){if(img_bank<0)return 0;worm_trk_sync();job_sync();
 int i=mru_find(W.slot);if(i<0){i=mru_alloc(-1,-1);if(i<0)return -1;}
 uint8_t *e=pack_rec(mbuf[i],mbuf[i]+MRU_CAP);
 if(!e){mru[i].slot=-1;mru[i].dirty=0;return -1;}               /* не влезло: ячейка испорчена — сбросить (правки живы в W, паттерн остаётся текущим) */
 if(mru[i].slot!=W.slot)mru[i].fv=0;
 mru[i].slot=(int8_t)W.slot;mru[i].len=(uint16_t)(e-mbuf[i]);
 if(pending_end(-1)>BANK_SZ){mru[i].slot=-1;mru[i].dirty=0;return -1;}   /* не влезло в банк */
 mru[i].t=++mru_tick;
 const int d=!mru_same_as_flash(i);
 if(d){bank_dirty=1;dirty_t=worm_ui_now;}
 mru[i].dirty=(uint8_t)d;return 0;}
/* запись паттерна m -> текущее состояние (пустая — «новый паттерн») */
static void load_rec(int m){int i=img_bank>=0?mru_load(m,W.slot):-2;
 if(i<0||unpack_rec(mbuf[i],mbuf[i]+mru[i].len)<0)new_empty();
 worm_trk_reload();worm_trk_fix();cur_keep=(uint8_t)(img_bank>=0&&(i>=0?mru[i].len:0)>0&&!cur_content());cur_bpm0=W.bpm;}   /* новый/старый паттерн без BPM наследует текущий темп */

int worm_store_bank_load(int bank){if(bank<0||bank>=NBANK)return -1;int r=0;
 for(int i=0;i<MRU_N;i++)mru[i].slot=-1;fl_p=-1;virt_blank=0;img_bank=bank;
 if(parse_bank()){blank_bank();r=1;}
 bank_dirty=0;flush_pos=0;W.bank=bank;if(W.slot<0||W.slot>=NPAT)W.slot=0;load_rec(W.slot);return r;}
int worm_store_pat_switch(int m){if(m<0||m>=NPAT)return -1;if(m==W.slot)return 0;
 if(commit_cur()<0)return -1;      /* не влезло в банк / кэш полон грязными: остаёмся, чтобы не потерять правки */
 if(rlen[m]>0&&mru_find(m)<0&&mru_alloc(-1,m)<0)return -1;   /* входящую некуда подгрузить (все ячейки грязные): остаёмся */
 W.slot=(int8_t)m;load_rec(m);return 0;}
/* заранее подтянуть запись m в кэш (pend_pat, пока играет): на границе такта остаётся только распаковать. 1 = в кэше / пусто, 0 = нет места */
int worm_store_prefetch(int m){if(m<0||m>=NPAT||img_bank<0||m==W.slot)return 0;
 if(rlen[m]==0&&mru_find(m)<0)return 1;int i=mru_load(m,W.slot);return i>=0;}
unsigned worm_store_bank_used(void){unsigned u=0;if(img_bank<0)return 0;for(int p=0;p<NPAT;p++)if(rlen[p])u|=1u<<p;
 for(int i=0;i<MRU_N;i++)if(mru[i].slot>=0&&mru[i].dirty&&mru[i].slot!=W.slot){if(mru[i].len)u|=1u<<mru[i].slot;else u&=~(1u<<mru[i].slot);}
 if(cur_nonempty())u|=1u<<W.slot;else u&=~(1u<<W.slot);return u;}
/* для тестов/диагностики: сколько ячеек кэша занято / грязные */
int worm_store_mru_info(int *dirty){int n=0,d=0;for(int i=0;i<MRU_N;i++)if(mru[i].slot>=0){n++;d+=mru[i].dirty;}if(dirty)*dirty=d;return n;}

/* ---- флеш ---- */
/* выходной сектор s нового образа банка при замене записи p (см. шапку): [0,R0) старое, заголовок len, запись из кэша, хвост старого со сдвигом, 0xFF */
static int build_sector(int s,int ci,uint8_t *dst){
 const int p=fl_p;const uint32_t R0=roff[p]-2,RB=roff[p],n=mru[ci].len,RE=RB+n,E2=fl_uend2,A=(uint32_t)s*SECT,B=A+SECT;
 memset(dst,0xFF,SECT);
 uint32_t a,b;
 a=A>R0?A:R0;b=B<R0+2?B:R0+2;                                    /* заголовок */
 for(uint32_t x=a;x<b;x++)dst[x-A]=(uint8_t)(x==R0?(n&0xFF):(n>>8));
 a=A>RB?A:RB;b=B<RE?B:RE;if(a<b)memcpy(dst+(a-A),mbuf[ci]+(a-RB),b-a);   /* сама запись */
 b=B<R0?B:R0;if(A<b&&old_read(A,dst,b-A))return -1;               /* всё до записи — как было */
 a=A>RE?A:RE;b=B<E2?B:E2;                                         /* после записи — старый хвост, сдвинутый на dl */
 if(a<b&&old_read((uint32_t)((int32_t)a-fl_dl),dst+(a-A),b-a))return -1;
 return 0;}
/* 0 = сектор совпадает, 1 = записан, <0 = ошибка */
static int write_sector(int s,const uint8_t *src){uint32_t a=bank_base()+(uint32_t)s*SECT;
 uint8_t t[256];int same=1;for(uint32_t off=0;off<SECT&&same;off+=sizeof t){if(worm_flash_read(a+off,t,sizeof t)){same=0;break;}if(memcmp(t,src+off,sizeof t))same=0;}
 if(same)return 0;
 uint32_t n=SECT;while(n&&src[n-1]==0xFF)n--;               /* хвост 0xFF после стирания и так 0xFF */
 if(worm_flash_erase(a,SECT))return -2;if(n&&worm_flash_write(a,src,n))return -3;return 1;}
/* начать задачу: самая «усаживающая» грязная запись первой (банк не должен временно переполниться). 0 = грязных нет */
static int job_start(void){int b=-1;
 for(int i=0;i<MRU_N;i++)if(mru[i].slot>=0&&mru[i].dirty){int32_t d=(int32_t)mru[i].len-(int32_t)rlen[mru[i].slot];
  if(b<0||d<(int32_t)mru[b].len-(int32_t)rlen[mru[b].slot])b=i;}
 if(b<0)return 0;
 fl_p=mru[b].slot;fl_dl=(int32_t)mru[b].len-(int32_t)rlen[fl_p];fl_uend2=(uint32_t)((int32_t)uend+fl_dl);
 const int s0=(int)((roff[fl_p]-2)/SECT),s1=(int)(((uend>fl_uend2?uend:fl_uend2)-1)/SECT);
 fl_dir=fl_dl>0?-1:1;fl_s=fl_dir>0?s0:s1;fl_s1=fl_dir>0?s1:s0;return 1;}
static void job_finish(int ci){const int p=fl_p;for(int q=p+1;q<NPAT;q++)roff[q]=(uint32_t)((int32_t)roff[q]+fl_dl);
 rlen[p]=mru[ci].len;uend=fl_uend2;mru[ci].dirty=0;mru[ci].fh=fnv(2166136261u,mbuf[ci],mru[ci].len);mru[ci].fv=1;fl_p=-1;virt_blank=0;}
/* один шаг: один сектор. 1 = что-то записано (вернуться позже), 0 = шаг без стирания, <0 = ошибка; *done=1, когда грязных больше нет */
static int flush_step(int *done){*done=0;
 if(fl_p<0&&!job_start()){*done=1;return 0;}
 const int ci=mru_find(fl_p);if(ci<0){fl_p=-1;return -4;}
 if(build_sector(fl_s,ci,sbuf)){fl_p=-1;return -1;}
 int r=write_sector(fl_s,sbuf);if(r<0){fl_p=-1;return r;}
 if(fl_s==fl_s1)job_finish(ci);else fl_s+=fl_dir;
 return r;}
void worm_store_flush(void){if(!bank_dirty||img_bank<0||W.run)return;if(fl_p<0&&worm_ui_now-dirty_t<FLUSH_DELAY_MS)return;
 for(int guard=0;guard<NSEC+1;guard++){int done,r=flush_step(&done);
  if(r<0){fl_p=-1;dirty_t=worm_ui_now;return;}
  if(done){bank_dirty=0;flush_pos=0;return;}
  if(r>0)return;}}   /* один стёртый сектор за вызов */
/* SAVE / смена банка: текущий паттерн в кэш и сразу всё во флеш. 0 = ок, -1 = паттерн не влез в банк/кэш, -2/-3 = ошибка флеши */
int worm_store_bank_save(int bank){if(bank!=img_bank||img_bank<0)return 0;int rc=commit_cur()<0?-1:0;
 for(;;){int done,r=flush_step(&done);if(r<0)return r;if(done)break;}
 bank_dirty=0;flush_pos=0;return rc;}

/* ---- проект: BPM, где мы были, и 32 пользовательских пресета в том же секторе ---- */
typedef struct { uint32_t magic; float bpm; int8_t bank, slot; int8_t usb_mode; int8_t rsv[3]; } proj_t;
/* sbuf (4 KB, .noinit) — буфер сектора проекта/пресетов; с задачей сдвига банка не пересекается: flush не вложен */
static int prs_hdr_ok(const uint8_t *h){uint32_t m;memcpy(&m,h,4);return m==PRS_MAGIC&&h[4]==1&&h[5]==NPRESET;}
static void prs_hdr_make(uint8_t *h){uint32_t m=PRS_MAGIC;memcpy(h,&m,4);h[4]=1;h[5]=NPRESET;h[6]=0;h[7]=0;}

int worm_store_preset_read(int i,char *name,uint8_t *vals){
 if(i<0||i>=NPRESET)return 0;
 uint8_t h[8],r[PRS_REC];
 if(worm_flash_read(PROJ_ADDR+PRS_OFF,h,8)||!prs_hdr_ok(h))return 0;
 if(worm_flash_read(PRS_BASE+(uint32_t)i*PRS_REC,r,PRS_REC))return 0;
 if(r[PRS_NAM]!=1)return 0;   /* 0xFF (пустая запись) тоже даёт совпадающий XOR — проверяем флаг used явно */
 uint8_t x=0;for(int k=0;k<PRS_REC-1;k++)x^=r[k];if(x!=r[PRS_REC-1])return 0;
 if(name){memcpy(name,r,PRS_NAM-1);name[PRS_NAM-1]=0;}
 if(vals)memcpy(vals,r+PRS_NAM+1,PRS_NVALS);
 return 1;}
int worm_store_preset_save(int i,const char *name,const uint8_t *vals){
 if(i<0||i>=NPRESET)return -1;
 uint8_t *buf=sbuf;
 if(worm_flash_read(PROJ_ADDR,buf,SECT))memset(buf,0xFF,SECT);
 int had=prs_hdr_ok(buf+PRS_OFF);
 if(!had)prs_hdr_make(buf+PRS_OFF);
 uint8_t *r=buf+PRS_OFF+8u+(uint32_t)i*PRS_REC;
 uint8_t old[PRS_REC];memcpy(old,r,PRS_REC);   /* то, что сейчас во флеше в этом слоте */
 memset(r,0,PRS_REC);
 if(name)for(int k=0;k<PRS_NAM-1&&name[k];k++)r[k]=(uint8_t)name[k];
 r[PRS_NAM]=1;memcpy(r+PRS_NAM+1,vals,PRS_NVALS);
 uint8_t x=0;for(int k=0;k<PRS_REC-1;k++)x^=r[k];r[PRS_REC-1]=x;
 if(had&&!memcmp(old,r,PRS_REC))return 0;   /* пересохранение того же пресета без изменений: сектор не стираем */
 worm_prs_busy=1;
 int rc=worm_flash_erase(PROJ_ADDR,SECT);if(!rc)rc=worm_flash_write(PROJ_ADDR,buf,SECT);
 worm_prs_busy=0;return rc?-1:0;}

int worm_store_proj_save(void){proj_t pr;memset(&pr,0,sizeof pr);pr.magic=PROJ_MAGIC;pr.bpm=W.bpm;pr.bank=(int8_t)W.bank;pr.slot=W.slot;pr.usb_mode=W.usb_mode;pr.rsv[0]=(int8_t)W.theme;   /* цветовая схема GLO > COLOR */
 uint8_t *buf=sbuf;if(worm_flash_read(PROJ_ADDR,buf,SECT))memset(buf,0xFF,SECT);
 if(!memcmp(buf,&pr,sizeof pr))return 0;   /* проект не изменился: сектор не стираем (SAVE раньше стирал его каждый раз) */
 memcpy(buf,&pr,sizeof pr);
 worm_prs_busy=1;
 int rc=worm_flash_erase(PROJ_ADDR,SECT);if(!rc)rc=worm_flash_write(PROJ_ADDR,buf,SECT);
 worm_prs_busy=0;return rc?-1:0;}
int worm_store_proj_load(void){proj_t pr;if(worm_flash_read(PROJ_ADDR,&pr,sizeof pr))return -1;if(pr.magic!=PROJ_MAGIC)return 1;
 if(pr.bpm>=40.f&&pr.bpm<=300.f)W.bpm=pr.bpm;if(pr.bank>=0&&pr.bank<NBANK)W.bank=pr.bank;if(pr.slot>=0&&pr.slot<NPAT)W.slot=pr.slot;
  W.usb_mode=1;   /* при старте всегда AUDIO+MIDI: диск — только явный выбор в меню USB */
  W.theme=(pr.rsv[0]>=0&&pr.rsv[0]<=3)?pr.rsv[0]:0;   /* старые проекты: 0xFF/мусор -> ORIGINAL */
  return 0;}

/* ---- LIVE: настройки и набор макросов банка в хвосте сектора проекта (после пресетов); остальные байты сектора не трогаем ---- */
int worm_store_lm_load(void){uint8_t b[23];if(worm_flash_read(PROJ_ADDR+LM_OFF,b,23))return -1;
 if(b[0]!='L'||(b[1]!='3'&&b[1]!='2'&&b[1]!='M')){worm_lm_defaults(&worm_lm_bank);return 1;}
 int legacy=b[1]!='3';   /* 'LM' — старый формат (до 2 PASS / ALL): берём набор макросов, но время/цепочку/order ставим по умолчанию и пересохраняем как 'L3' ('L2' — прежний default order=ALL ходил по пустым паттернам) */
 if(!legacy){if(b[2]<5)worm_lm_sw=(int8_t)b[2];if(b[3]<9)worm_lm_chain=(int8_t)b[3];if(b[4]<3)worm_lm_order=(int8_t)b[4];}
 else worm_lm_dirty=1;
 for(int i=0;i<NLM;i++){int id=(int16_t)(b[5+2*i]|(b[6+2*i]<<8));int t=(int8_t)b[17+i];if(id>=LM_END||id<-1)id=-1;if(t<-1||t>=NTRK+NMTRK)t=-1;worm_lm_bank.id[i]=(int16_t)id;worm_lm_bank.trk[i]=(int8_t)t;}
 return legacy?1:0;}
int worm_store_lm_save(void){uint8_t n[23];n[0]='L';n[1]='3';n[2]=(uint8_t)worm_lm_sw;n[3]=(uint8_t)worm_lm_chain;n[4]=(uint8_t)worm_lm_order;
 for(int i=0;i<NLM;i++){n[5+2*i]=(uint8_t)(worm_lm_bank.id[i]&0xFF);n[6+2*i]=(uint8_t)((worm_lm_bank.id[i]>>8)&0xFF);n[17+i]=(uint8_t)worm_lm_bank.trk[i];}
 uint8_t *buf=sbuf;if(worm_flash_read(PROJ_ADDR,buf,SECT))memset(buf,0xFF,SECT);
 if(!memcmp(buf+LM_OFF,n,23)){worm_lm_dirty=0;return 0;}
 memcpy(buf+LM_OFF,n,23);worm_prs_busy=1;
 int rc=worm_flash_erase(PROJ_ADDR,SECT);if(!rc)rc=worm_flash_write(PROJ_ADDR,buf,SECT);
 worm_prs_busy=0;if(!rc)worm_lm_dirty=0;return rc?-1:0;}
