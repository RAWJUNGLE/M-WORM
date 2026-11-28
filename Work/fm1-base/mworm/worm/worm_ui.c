/* WORM-FM1 UI: 240x240 1-bit framebuffer, Digitone-style pages, button/encoder/key logic (spec: WORM-FM1.rtf). */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "worm.h"
#include "worm_wt.h"
#include "font6x8.h"
#define GWHITE 15   /* «белый» в градациях серого (яркость кадра 0..15) */
enum { S_NOTE,S_OSC,S_AMP,S_FLT,S_LFO,S_ENV,S_FX,S_BANK,S_PTN,S_SPG,S_GRID,S_PLAY,S_MT,S_SCL,S_MET,S_VOX,S_PRE,S_ALG,S_SAV,S_ARP,S_EUCL,S_LIVE,S_SET,S_MID,S_MCC,S_GRIDSZ,S_LMAC,S_LSEQ };
unsigned worm_usbh_dbg = 0; int worm_usbh_msg = 0; int worm_usbh = 0; int worm_battery = -1; int worm_charging = 0;
int worm_uac_und = 0, worm_uac_miss = 0, worm_max_us = 0;   /* USB-запись: опустошения кольца / пропуски пакета; самый долгий блок рендера, мкс (app.c) */
int worm_lim_db = 0;   /* сжатие лимитера мастера, дБ (app.c) */
int worm_cpu_pct = 0; int worm_xruns = 0;   /* загрузка аудио-рендера (% бюджета блока) и число xruns: заполняет app.c, показывает экран BPM */
static int col_old;   /* схема до предпросмотра в GLO > COLOR */
static struct { int scr,tab,env,sel,hold,hold_k,lk,rec,oct,pre,sav_yes,sav_bank,save_long,save_bank,prev,lfo,chan,msg,live,hold_op,hold_op_t,confirm,pending_oa,pending_usb,pending_col,frame,tc,dest_sel,dest_lfo,dest_show,map,map_lfo,map_di,lfo_hold_t,lfo_pg,sel_k,sel_prev,edit_hold_t,edit_long; uint32_t tc_t0,dest_t0,save_t0; uint16_t hb; int8_t lv[27]; int edit_used,pick_prev; uint32_t pick_t0; int pshow,pre_prev,pre_tab,pre_step,set_prev,set_tab,mt_prev,mt_tab,mt_i,mt_home; uint32_t pre_t0; int msg_k,msg_n; } U;
/* ---- LIVE режим (HOME+GLO): состояние ---- */
#define LM_TRIG_MS 60    /* длительность вспышки LED на клавише мьюта: короче шага, чтобы мигание было видимым */
static int lm_on,lm_sel,lm_chan;static int8_t lm_pend=-1;static int lm_pcnt,lm_age,lm_beat=-1;static uint32_t lm_bar;static int lm_pscr,lm_ptab;
static void lm_tick(void);static int lm_enc(int id,int d);static void lm_key(int i,int dn);static int lm_fxkey(int i);static void rel_live(void);
static uint8_t *F;
/* DEST-меню: порядок и группы зависят от движка (worm_dest_order); индексы в W.p/W.tr (= worm_dest_list) не меняются -> банки совместимы. */
static uint8_t dest_vo[DEST_VMAX],dest_gr[DEST_VMAX];static int dest_vn,dest_al=-1;
static void dest_order_init(void){int al=W.p[P_OA];if(al==dest_al&&dest_vn)return;dest_al=al;dest_vn=worm_dest_order(al,dest_vo,dest_gr);}
static int dest_v2a(int v){dest_order_init();if(v<0)v=0;if(v>=dest_vn)v=dest_vn-1;return dest_vo[v];}
static int dest_a2v(int a){dest_order_init();for(int i=0;i<dest_vn;i++)if(dest_vo[i]==a)return i;return 0;}
/* Уведомление: что именно произошло и с каким номером (раньше заголовок выбирался по таймеру и менялся во время показа) */
enum{MK_DONE,MK_PAT,MK_BANK,MK_PRS,MK_FULL,MK_ERR};
static void say(int k,int n,int frames){U.msg_k=k;U.msg_n=n;U.msg=frames;}
static void say_save(int k,int n,int frames,int rc){if(rc==0)say(k,n,frames);else if(rc==-1)say(MK_FULL,0,80);else say(MK_ERR,0,80);}   /* -1 не влезло, прочее — ошибка флеши */
static int8_t pend_pat=-1;static uint32_t pend_bar;   /* ожидающий ПАТТЕРН (-1 нет): применяется на границе такта */
/* паттерн = снимок всего (4 дорожки, звук, FX): переключается целиком. Не влез в банк -> остаёмся на текущем, "BANK FULL" */
static void pat_do(int m){if(worm_store_pat_switch(m)<0)say(MK_FULL,0,80);U.sel=-1;U.hold=-1;U.hold_k=-1;U.lk=0;}
static void pat_request(int m){if(m<0||m>=NPAT)return;if(m==W.slot){pend_pat=-1;return;}if(!W.run){pat_do(m);pend_pat=-1;return;}pend_pat=(int8_t)m;pend_bar=worm_bar_count();worm_store_prefetch(m);}   /* запись — в кэш уже сейчас: на границе такта только распаковка, без флеша */
static void pat_apply_due(void){uint32_t b=worm_bar_count();if(b==pend_bar)return;pend_bar=b;if(pend_pat>=0){int m=pend_pat;pend_pat=-1;pat_do(m);}}
static void set_bank(int b){if(b==W.bank)return;pend_pat=-1;if(worm_store_bank_save(W.bank)==-1){say(MK_FULL,0,80);return;}W.bank=b;W.slot=0;worm_store_bank_load(b);U.sel=-1;U.hold=-1;U.hold_k=-1;U.lk=0;}   /* СНАЧАЛА сохраняем покинутый банк; после смены банка играет паттерн 1 */
static void px(int x,int y,int c){if((unsigned)x<LCD_W&&(unsigned)y<LCD_H)F[y*LCD_W+x]=c?GWHITE:0;}
/* grayscale-примитивы для wavetable (яркость 0..15). gpx — max-комбинация: линии не темнеют при наложении. */
static void gpx(int x,int y,int v){if(v<0)v=0;if(v>15)v=15;if((unsigned)x<LCD_W&&(unsigned)y<LCD_H){uint8_t *p=&F[y*LCD_W+x];if(v>*p)*p=(uint8_t)v;}}
static float gfrac(float x){return x-floorf(x);}
static void gline(int x0,int y0,int x1,int y1,int lvl)   /* линия с антиалиасингом (алгоритм Ву) */
{
    int steep=abs(y1-y0)>abs(x1-x0),t;
    if(steep){t=x0;x0=y0;y0=t;t=x1;x1=y1;y1=t;}
    if(x0>x1){t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;}
    float dx=(float)(x1-x0),dy=(float)(y1-y0),grad=dx==0.f?1.f:dy/dx;
    float xend=floorf((float)x0+.5f),yend=y0+grad*(xend-x0),xgap=1.f-gfrac((float)x0+.5f);
    int xp1=(int)xend,yp1=(int)floorf(yend);
    if(steep){gpx(yp1,xp1,(int)((1.f-gfrac(yend))*xgap*lvl+.5f));gpx(yp1+1,xp1,(int)(gfrac(yend)*xgap*lvl+.5f));}
    else{gpx(xp1,yp1,(int)((1.f-gfrac(yend))*xgap*lvl+.5f));gpx(xp1,yp1+1,(int)(gfrac(yend)*xgap*lvl+.5f));}
    float intery=yend+grad;
    xend=floorf((float)x1+.5f);yend=y1+grad*(xend-x1);xgap=gfrac((float)x1+.5f);
    int xp2=(int)xend,yp2=(int)floorf(yend);
    if(steep){gpx(yp2,xp2,(int)((1.f-gfrac(yend))*xgap*lvl+.5f));gpx(yp2+1,xp2,(int)(gfrac(yend)*xgap*lvl+.5f));}
    else{gpx(xp2,yp2,(int)((1.f-gfrac(yend))*xgap*lvl+.5f));gpx(xp2,yp2+1,(int)(gfrac(yend)*xgap*lvl+.5f));}
    if(steep){for(int x=xp1+1;x<xp2;x++){int iy=(int)floorf(intery);float fy=gfrac(intery);
        gpx(iy,x,(int)((1.f-fy)*lvl+.5f));gpx(iy+1,x,(int)(fy*lvl+.5f));intery+=grad;}}
    else{for(int x=xp1+1;x<xp2;x++){int iy=(int)floorf(intery);float fy=gfrac(intery);
        gpx(x,iy,(int)((1.f-fy)*lvl+.5f));gpx(x,iy+1,(int)(fy*lvl+.5f));intery+=grad;}}
}
static void rect(int x,int y,int w,int h,int fill){for(int j=0;j<h;j++)for(int i=0;i<w;i++)if(fill||i==0||j==0||i==w-1||j==h-1)px(x+i,y+j,1);}
static void box0(int x,int y,int w,int h){for(int j=0;j<h;j++)for(int i=0;i<w;i++)px(x+i,y+j,0);}
static void line(int x0,int y0,int x1,int y1,int dot){int dx=abs(x1-x0),dy=-abs(y1-y0),sx=x0<x1?1:-1,sy=y0<y1?1:-1,e=dx+dy,n=0;
 for(;;){if(!dot||(n++%3)==0)px(x0,y0,1);if(x0==x1&&y0==y1)break;int e2=2*e;if(e2>=dy){e+=dy;x0+=sx;}if(e2<=dx){e+=dx;y0+=sy;}}}
static void circle(int cx,int cy,int r){int x=r,y=0,e=1-r;while(x>=y){px(cx+x,cy+y,1);px(cx-x,cy+y,1);px(cx+x,cy-y,1);px(cx-x,cy-y,1);px(cx+y,cy+x,1);px(cx-y,cy+x,1);px(cx+y,cy-x,1);px(cx-y,cy-x,1);y++;if(e<0)e+=2*y+1;else{x--;e+=2*(y-x)+1;}}}
static void disc(int cx,int cy,int r){for(int y=-r;y<=r;y++)for(int x=-r;x<=r;x++)if(x*x+y*y<=r*r)px(cx+x,cy+y,1);}
static int tw(const char *s,int z){return (int)strlen(s)*8*z;}
static void text(int x,int y,const char *s,int z,int al,int inv){int w=tw(s,z);if(al==1)x-=w/2;else if(al==2)x-=w;
 for(;*s;s++,x+=8*z){int c=(unsigned char)*s;if(c<32||c>126)c='?';const unsigned char *g=worm_font6x8[c-32];
  for(int j=0;j<8;j++)for(int i=0;i<8;i++)if(g[j]&(0x80>>i))for(int a=0;a<z;a++)for(int b=0;b<z;b++)px(x+i*z+a,y+j*z+b,inv?0:1);}}
/* строка статистики: подпись 5 символов + число правым краем в 5 колонок (+ "us"). Без sprintf — тот падал на этом экране. */
static void stat_line(char *d,const char *lab,int v,int us){char t[6];int k=0;
 while(*lab)*d++=*lab++;
 do{t[k++]=(char)('0'+v%10);v/=10;}while(v&&k<5);
 for(int n=k;n<5;n++)*d++=' ';
 while(k)*d++=t[--k];
 if(us){*d++='u';*d++='s';}
 *d=0;}
static pattern_t *PT(void){return worm_sel_pat();}   /* секвенция выбранной дорожки: звуковой или MIDI */
static void enc_rate_reset(int k);   /*below: ускорение K1..K4 */
/* запись параметра с сохранением в хранилище выбранной дорожки (звуковой W.tr[].p или MIDI W.mt[].p) */
static void wt(int id,int v){W.p[id]=(int16_t)v;if(W.mid){int q=worm_mpos(id);if(q>=0)W.mt[W.mpat].p[q]=(int16_t)v;}else if(worm_param_is_track(id))W.tr[W.pat].p[id]=(int16_t)v;}
static int vp(void){int v=W.p[P_GVP];if(v>W.p[P_GPG])v=W.p[P_GPG];return v-1;}
static step_t *sel(void){if(U.live&&W.run&&W.cur>=0)return &PT()->s[W.cur];
 return (U.hold>=0&&U.sel>=0)?&PT()->s[U.sel]:0;}   /* шаг только при parameter lock (зажат) */
/* Шаг, зажатый в STEP SEQUENCER (для ввода ноты ROOT и parameter lock). В Live Record на ходу — текущий шаг (sel()). */
static step_t *hold_step(void){if(U.live&&W.run&&W.cur>=0)return 0;return(U.hold>=0&&U.sel>=0)?&PT()->s[U.sel]:0;}
/* ROOT зажатого шага: энкодер ROOT пишет ноту в шаг (раньше показывался всегда глобальный ROOT, и крутить было «не видно») */
static int val(int id){step_t *s=sel();int v;if(U.live&&W.run&&W.cur>=0)s=&PT()->s[W.cur];   /* Live Record: текущий шаг */
 if(id==P_GST)return (U.sel<0?0:U.sel)+1;if(id==P_GMT)return s?s->mt:0;
 if(id==P_NR){step_t *h=hold_step();return(h&&h->note)?h->note:W.p[P_NR];}   /* ROOT: нота зажатого шага, иначе глобальная */
 if(P_IS_RTRG(id)){if(s&&s->note)return id==P_RTR?s->rtr:id==P_RTL?s->rtl:s->rtv;return W.p[id];}   /* RETRIG: поле зажатого шага, иначе значение для новых шагов */
 if(s){if(id==P_NV)return s->vel;if(id==P_NL)return s->len;if(id==P_NPR)return s->prob;if(id==P_NCD)return s->cond;if(worm_step_lock_get(s,id,&v))return v;}return W.p[id];}
/* WAVE по алгоритмам: VA — только формы 0..5 (SIN TRI SAW SQR PLS NOI); WAVETABLE — только таблицы 6..57 (6..10 MAIN 64 кадра, 11..17 USER, 18..57 LIB); FM — 0..3 */
static int wmax_of(int al){return al==ALGO_FM?7:al==ALGO_VA?WAVE_SHAPES-1:WAVE_MAX;}
static int wmin_of(int al){return al==ALGO_WT?WAVE_MAIN0:0;}
/* MIDI-параметр (id >= NP): зажатый шаг / Live Record -> p-lock шага (канал, PROG, BANK, VOL, PAN, BASE линии = MIDI на этом шаге), иначе значение дорожки */
static void mset(int id,int v){step_t *s=sel();
 if(id!=P_AMO&&s&&(U.hold>=0||(U.live&&W.run&&W.cur>=0))){worm_step_init(s);worm_step_lock_set(s,id,v);if(U.hold>=0)U.lk=1;return;}
 W.p[id]=(int16_t)v;{int q=worm_mpos(id);if(W.mid&&q>=0)W.mt[W.mpat].p[q]=(int16_t)v;}
 if(id==P_MMT)worm_mtrk_mute(W.mpat,v);}
static void setv(int id,int v){const pinfo_t *pi=&worm_pinfo[id];if(v<pi->mn)v=pi->mn;if(v>pi->mx)v=pi->mx;
 if(P_IS_RTRG(id)){step_t *s=sel();   /* RETRIG: в шаг (зажатый / текущий в Live Record), не p-lock; без шага — значение для новых шагов */
  if(s){worm_step_init(s);if(id==P_RTR)s->rtr=(uint8_t)v;else if(id==P_RTL)s->rtl=(uint8_t)v;else s->rtv=(int8_t)v;if(U.hold>=0)U.lk=1;}else W.p[id]=(int16_t)v;return;}
 if(id>=NP){mset(id,v);return;}{int k=W.p[P_OA]==ALGO_FM?worm_fm_of(id):-1;if(k>=0)v=worm_fm_clamp(k,v);}   /* у FM слоты OSC — параметры 4-оператора */
 if((id==P_OW||id==P_OW2)){const int al=W.p[P_OA];if(v>wmax_of(al))v=wmax_of(al);if(v<wmin_of(al))v=wmin_of(al);}   /* WAVE/WAVE2 в границах алгоритма */
 
 if(id==P_NR){wt(P_NR,v);   /* ROOT всегда = последняя выбранная нота (новые шаги наследуют её) */
  step_t *h=hold_step();if(h){worm_step_init(h);h->note=(uint8_t)v;h->on=1;U.lk=1;}return;}
 if(id==P_GLN&&v>W.p[P_GPG]*16)v=W.p[P_GPG]*16;if(id==P_GVP&&v>W.p[P_GPG])v=W.p[P_GPG];step_t *s=sel();
 if(U.live&&W.run&&W.cur>=0)s=&PT()->s[W.cur];   /* Live Record: параметр-лок на текущем шаге */
 if(id==P_GST){U.sel=v-1;return;}if(id==P_GMT){if(s)s->mt=v;return;}
 if(id>=P_GSC){W.p[id]=v;if(worm_param_is_track(id))W.tr[W.pat].p[id]=v;}
 else if(id==P_OA){const int fm_prev=W.p[id];W.p[id]=v;W.tr[W.pat].p[id]=v;
  if(v==ALGO_FM&&fm_prev!=ALGO_FM){worm_fm_defaults(W.p);worm_fm_defaults(W.tr[W.pat].p);}   /* вход в FM: слоты OSC получают FM-значения */
  else if(v!=ALGO_FM&&fm_prev==ALGO_FM){worm_fm_restore(W.p);worm_fm_restore(W.tr[W.pat].p);}
  {int wm=wmax_of(v),wn=wmin_of(v),ow[2]={P_OW,P_OW2};for(int q=0;q<2;q++){
   int w=W.p[ow[q]];if(w>wm)w=v==ALGO_VA?2:wm;   /* таблица -> VA: SAW; FM: не выше 3 */
   if(w<wn)w=wn;                                  /* форма -> WAVETABLE: первая основная таблица (64 кадра) */
   W.p[ow[q]]=w;W.tr[W.pat].p[ow[q]]=w;
  }}
}   /* ALGO — per-track; WAVE подрезается под новый алгоритм */
 else if(s&&(U.hold>=0||id<=P_NPO)){worm_step_init(s);if(id==P_NV)s->vel=v;else if(id==P_NL)s->len=v;else if(id==P_NPR)s->prob=v;else if(id==P_NCD)s->cond=v;else worm_step_lock_set(s,id,v);if(U.hold>=0)U.lk=1;}   /* p-lock: только зажатый шаг (STEP SEQ) или NOTE-параметры; звуковые в Live Record -> automation (ниже) */
 else wt(id,v);
 if(id==P_GPG)W.p[P_GLN]=v*16;   /* длина = страницы*16 (раньше LEN оставалась 16 — до 128 шагов не росла) */
 if(id==P_GLN||id==P_GDR||id==P_GSX||id==P_GSW||id==P_GPG||id==P_GVP)worm_trk_sync();
  /* Live Record: вращение ручки звукового параметра -> точка automation на текущем шаге */
  if(U.live&&W.run&&W.cur>=0&&id>=P_OA&&id<=P_E3V&&worm_param_is_track(id))worm_aut_rec(W.pat,id,W.cur,v);}   /* режимы секвенсора — у каждого канала свои */
/* Суммарная модуляция параметра id выбранной дорожки: LFO1..LFO4 + E4 (свободный ADSR) × DEPTH, -1..1.
 * Та же, что применяется в synth/mp(): LFO ±1 проходит весь диапазон параметра. */
static float mod_of(int id){float m=0;
 for(int l=0;l<NLFO;l++){int di=W.p[P_L0D+l*9],t=worm_dest_param(di);int16_t ex[2]={-1,-1};if(t>=0)worm_dest_ext(di,ex);if(t==id||ex[0]==id||ex[1]==id)m+=worm_lfo_value(l);}
 if(worm_dest_param(W.p[P_E4M])==id)m+=worm_synth_env4()*(W.p[P_E4P]/127.f);
 return worm_mod_span(id,m);}   /* та же кривая, что в синте: стрелка ручки показывает реальную модуляцию */
/* Значение параметра с живой модуляцией (LFO + E4), в единицах параметра, обрезано по диапазону: для превью, которое должно двигаться вместе с ручкой */
static float mval(int id){const pinfo_t *pi=&worm_pinfo[id];float nv=(float)val(id)+mod_of(id)*(float)(pi->mx-pi->mn);if(nv<(float)pi->mn)nv=(float)pi->mn;if(nv>(float)pi->mx)nv=(float)pi->mx;return nv;}
/* ENV-меню: 2 amp-огибающие (OSC1 = E0, OSC2 = E3). Огибающие фильтров (E1/E2) — на страницах FLT. */
static int envk(void){return U.env==1?3:U.env==2?4:0;}   /* 0=E0 (amp OSC1), 1=E3 (amp OSC2), 2=E4 (свободный, маппинг) */
static int osc_nt(void);
static void widgets(int *w){if(U.scr==S_OSC&&U.tab>=osc_nt())U.tab=0;int t=U.tab;for(int i=0;i<4;i++)w[i]=-1;
 if(W.mid){int l=U.lfo&(NMLANE-1);   /* MIDI-дорожка: свои страницы (остальные — общие секвенсорные) */
  switch(U.scr){
  case S_NOTE:if(t){w[0]=P_MQZ;w[1]=P_MCD;w[2]=P_MMT;return;}break;
  case S_MID:if(t){w[0]=P_MVL;w[1]=P_MPN;w[2]=P_MMT;}else{w[0]=P_MCH;w[1]=P_MPG;w[2]=P_MBM;w[3]=P_MBL;}return;
  case S_MCC:for(int i=0;i<4;i++)w[i]=P_MLANE(i,t?0:1);return;   /* 0: живые значения (BASE) четырёх CC, 1: их CC-номера */
  case S_LFO:if(U.hb&(1<<8)){w[0]=P_MLANE(l,4);w[1]=P_MLANE(l,5);w[2]=P_MLANE(l,6);}else for(int i=0;i<4;i++)w[i]=P_MLANE(l,i);return;
  case S_ARP:for(int i=0;i<4;i++)w[i]=P_MAM+i;return;
  default:break;}}
 switch(U.scr){case S_NOTE:if(t){w[0]=P_NFT;w[1]=P_NLT;w[2]=P_NPT;w[3]=P_NPO;}else{w[0]=P_NR;w[1]=P_NV;w[2]=P_NL;w[3]=P_NPR;if(U.tc){w[0]=P_RTR;w[1]=P_RTL;w[2]=P_RTV;w[3]=P_NCD;}}break;   /* EDIT в REC: RETRIG / R.LEN / R.VEL / COND */
 case S_OSC:if(W.p[P_OA]==ALGO_FM){for(int i=0;i<4;i++){int k=t*4+i;w[i]=k<FM_N?worm_fm_slot[k]:-1;}}   /* FM: 7 страниц по 4 параметра (FM_*) */
  else if(t&1){int o=t>=2?P_OD2:P_OD;w[0]=o;w[1]=o+1;w[2]=t>=2?P_OPI2:P_OPI;w[3]=o+2;}   /* FM: K4 на обеих страницах = MIX OSC1<->OSC2 (кроссфейд вместо двух громкостей) */else{int o=t>=2?P_OW2:P_OW;w[0]=o;w[1]=o+1;w[2]=o+2;w[3]=o+3;if(W.p[P_OA]==ALGO_VA&&val(o)!=4)w[1]=-1;}break;   /* VA: PW только у PLS (4); у SIN/TRI/SAW/SQR/NOI ручки нет. WT: FRAME / BEND всегда. FM: INDEX */
 case S_AMP:if(t==2){w[0]=P_TRN;}else if(t){w[0]=P_SDL;w[1]=P_SRV;w[2]=P_SCH;}else{w[0]=P_ADW;w[1]=(U.hb&(1<<8))?P_ADR:P_ADS;w[2]=P_APAN;w[3]=P_AVOL;}break;
 case S_FLT:if(t<2){int b=t?P_F2C:P_F1C;for(int i=0;i<4;i++)w[i]=b+i;if(U.hb&(1<<8))w[3]=t?P_F2W:P_F1W;}
  else{int k=t-1;   /* страницы 3/4: ENV фильтра 1/2 (ADSR); с HOME — DRIVE/PAN/VOL огибающей */
   if(U.hb&(1<<8)){w[0]=P_E0X+k*3;w[2]=P_E0N+k*3;w[3]=P_E0V+k*3;}else for(int i=0;i<4;i++)w[i]=P_E0A+k*4+i;}break;
 case S_LFO:if(U.hb&(1<<8)){int o=P_L0D+U.lfo*9;
   if(val(P_L0W+U.lfo*9)==7){w[0]=o+4;w[1]=o+6;w[2]=o+7;w[3]=o+8;}   /* TIDES: MODE, SLOPE, SMOOTH, SHIFT */
   else{w[0]=o+4;w[1]=o+5;w[2]=o+1;w[3]=-1;}                       /* MODE, SYNC (вкл/выкл), SPEED */
  }else for(int i=0;i<4;i++)w[i]=P_L0D+U.lfo*9+i;break;
 case S_ENV:if(envk()==4){if(U.hb&(1<<8)){w[0]=P_E4M;w[1]=P_E4P;w[2]=-1;w[3]=-1;}else for(int i=0;i<4;i++)w[i]=P_E4A+i;}   /* E4: DEST/DEPTH через HOME, иначе ADSR */
  else if(t){int k=envk();w[0]=P_E0X+k*3;w[2]=P_E0N+k*3;w[3]=P_E0V+k*3;}else for(int i=0;i<4;i++)w[i]=P_E0A+envk()*4+i;break;
 case S_FX:if(t==0){w[0]=P_XT;w[1]=P_XFB;w[2]=P_XFL;w[3]=P_XMX;}else if(t==1){for(int i=0;i<4;i++)w[i]=P_RSZ+i;}else if(t==2){for(int i=0;i<4;i++)w[i]=P_CRT+i;}break;
 case S_SPG:w[0]=P_GPG;w[1]=P_GVP;w[2]=P_GLN;w[3]=P_GDR;break;case S_GRID:w[0]=P_GST;w[1]=P_NR;w[2]=P_NV;w[3]=P_NL;break;case S_PLAY:w[0]=P_GDR;w[1]=P_GLN;w[2]=P_GSX;w[3]=P_GSW;break;
 case S_MT:break;case S_SCL:for(int i=0;i<4;i++)w[i]=P_GSC+i;break;case S_MET:for(int i=0;i<4;i++)w[i]=P_GCK+i;break;case S_VOX:break;case S_ARP:if(t){w[0]=P_AQZ;w[1]=P_AMO;}else for(int i=0;i<4;i++)w[i]=P_ARM+i;break;case S_EUCL:for(int i=0;i<4;i++)w[i]=P_EUH+i;break;   /* GRID SIZE = полиметер: своя длина и скорость каждой дорожки (уже per-track в W.tr[]) */
  case S_GRIDSZ:w[0]=P_GLN;w[1]=P_GSX;w[2]=P_GPG;w[3]=P_GSW;break;}}
static int osc_nt(void){return W.p[P_OA]==ALGO_FM?7:W.p[P_OA]>=2?2:4;}   /* страницы OSC: FM — 7, WT/AT.. — 2, VA — 4 */
static void wdraw_fm(int i,int id,int k){int cx=32+(int)(i*58.5f);char b[16];int v=val(id),mn,mx;worm_fm_range(k,&mn,&mx);
 text(cx,146,worm_fm_label(k),1,1,0);worm_fm_fmt(k,v,b);
 if(k==FM_ALGO||k==FM_RC||k==FM_RA||k==FM_RB||k==FM_PHRT||k==FM_ATRG||k==FM_ARST||k==FM_BTRG||k==FM_BRST){rect(cx-20,161,40,24,0);text(cx,169,b,1,1,0);}
 else{const pinfo_t *pi=&worm_pinfo[id];float mm=(float)(mx-mn),lm=mod_of(id)*(float)(pi->mx-pi->mn),nv=(float)v+lm;if(nv<mn)nv=(float)mn;if(nv>mx)nv=(float)mx;
  circle(cx,173,11);float a=(135.f+270.f*(nv-mn)/mm)*3.14159f/180.f;line(cx,173,cx+(int)(cosf(a)*10),173+(int)(sinf(a)*10),0);
  if(lm!=0.f){float ab=(135.f+270.f*(v-mn)/mm)*3.14159f/180.f;disc(cx+(int)(cosf(ab)*14),173+(int)(sinf(ab)*14),2);}
  text(cx,193,b,1,1,0);}
 step_t *s=sel();int d;if(s&&worm_step_lock_get(s,id,&d))rect(cx-28,146,4,4,1);}
static void wdraw(int i,int id){{int k;if(id>=0&&id<NP&&U.scr==S_OSC&&W.p[P_OA]==ALGO_FM&&(k=worm_fm_of(id))>=0){wdraw_fm(i,id,k);return;}}int cx=32+(int)(i*58.5f);char b[16];
 if(id<0){line(cx-10,162,cx+10,162,1);line(cx-10,182,cx+10,182,1);line(cx-10,162,cx-10,182,1);line(cx+10,162,cx+10,182,1);text(cx,146,"-",1,1,0);return;}
 const pinfo_t *pi=&worm_pinfo[id];const char *lb=pi->name;int v=val(id);
 if(id==P_OW||id==P_OW2){static const char*N[9]={"WAVE","ALGO","TABLE","PATCH","PROG","SLEW","SHAPE","SHAPE","SHAPE"};lb=N[W.p[P_OA]];}if(id==P_OP||id==P_OP2){static const char*N[9]={"PW","INDEX","POS","RATE","A","DRIFT","SPREAD","DUTY","BOING"};lb=N[W.p[P_OA]];
  if(W.p[P_OA]!=ALGO_FM){int wk=worm_wave_kind(val(id==P_OP?P_OW:P_OW2));lb=wk==WK_MAIN?"FRAME":wk==WK_CYCLE?"BEND":"PW";}}   /* таблица: FRAME (MAIN, 64 кадра) / BEND (один цикл); формы: PW */
 if(W.p[P_OA]==4&&id==P_OS)lb="B";if(W.p[P_OA]==4&&id==P_OD)lb="C";   /* GS: PW/SUB/DETUNE = параметры a/b/c программы */
 if(W.p[P_OA]==ALGO_FM&&id==P_OL2)lb="MIX";                                /* FM: кроссфейд OSC1 <-> OSC2 */
 if(id==P_NCD)lb="COND";if(U.scr==S_GRIDSZ&&id==P_GLN)lb="GRID";if(U.scr==S_GRIDSZ&&id==P_GPG)lb="PAGES";if(id==P_RTR)lb="RETRIG";if(id==P_RTL)lb="R.LEN";if(id==P_RTV)lb="R.VEL";
 char lbm[10];int mlane=-1,mk=0;   /* линия CC MIDI-дорожки: l, параметр k */
 if(id>=P_M0C&&id<P_M0C+NMLANE*7){mlane=(id-P_M0C)/7;mk=(id-P_M0C)%7;}
 if(mlane>=0&&U.scr==S_MCC&&mk==1){int cc=val(P_MLANE(mlane,0));if(cc<0)lb="OFF";else{sprintf(lbm,"CC%d",cc);lb=lbm;}}   /* живая ручка CC: подпись = номер CC */
 /* DEST: значение = индекс в worm_dest_list */
 int is_dest=(id==P_E4M)||(id>=P_L0D&&id<P_L0D+NLFO*9&&(id-P_L0D)%9==0);
 /* TIDES: при WAVE=TDS переименовать SPEED->FREQ, DEPTH->SHAPE (SLOPE/SMOOTH/SHIFT — свои параметры) */
 if(U.scr==S_LFO&&val(P_L0W+U.lfo*9)==7){int o=P_L0D+U.lfo*9;
  if(id==o+1)lb="FREQ";}
 if(id==P_GST)lb="STEP";text(cx,146,lb,1,1,0);
 int is_lspd=(id>=P_L0D&&id<P_L0D+NLFO*9&&(id-P_L0D)%9==1);
 if(is_dest){strcpy(b,worm_dest_short(v,W.p[P_OA]));rect(cx-22,161,44,24,0);text(cx,169,b,1,1,0);}
 else if(mlane>=0&&mk==3&&val(id+2)){int di=(v+9)/19;if(di>6)di=6;strcpy(b,L_LDIV[di]);rect(cx-20,161,40,24,0);text(cx,169,b,1,1,0);}   /* SYNC вкл: SPEED = 1/16..4/1 */
 else if(is_lspd&&val(id+4)){int di=(v+9)/19;if(di>6)di=6;strcpy(b,L_LDIV[di]);rect(cx-20,161,40,24,0);text(cx,169,b,1,1,0);}   /* SYNC вкл: SPEED = 1/16..4/1 */
 else if(pi->fmt==F_LIST||id==P_OW||id==P_OW2){if(id==P_OW||id==P_OW2){static const char*L[9][6]={{"SIN","TRI","SAW","SQR","PLS","NOI"},
    {"1","2","3","4","-","-"},{"SIN","TRI","SAW","SQR","PLS","NOI"},
    {"P01","P02","P03","P04","P05","P06"},{"G01","G02","G03","G04","G05","G06"},
    {"SL1","SL2","SL3","SL4","SL5","SL6"},
    {"SAW","TRI","SQR","-","-","-"},{"SQR","PULS","TRI","SAW","-","-"},{"SQR","TRI","SINE","-","-","-"}};
    int al=W.p[P_OA];if(al>8)al=8;
    if(al!=ALGO_FM)worm_str5(b,worm_wave_label(v));else strcpy(b,L[al][v<0?0:v>5?5:v]);}else strcpy(b,pi->list[v]);
  rect(cx-20,161,40,24,0);text(cx,169,b,1,1,0);}
 else{float mm=(float)(pi->mx-pi->mn);float lfomod=mod_of(id);
  if(mlane>=0&&mk==1){int lv=worm_mlane_value(W.mpat,mlane);if(lv>=0&&lv!=v)lfomod=(lv-v)/mm;}   /* BASE: стрелка на живом значении CC (с LFO) */
  float nv=v;if(lfomod!=0.f){nv=v+lfomod*mm;if(nv<pi->mn)nv=pi->mn;if(nv>pi->mx)nv=pi->mx;}   /* LFO ±1 проходит весь диапазон параметра */
  circle(cx,173,11);
  float a=(135.f+270.f*(nv-pi->mn)/mm)*3.14159f/180.f;   /* стрелка на МОДУЛИРОВАННОМ значении — ручка «крутится» */
  line(cx,173,cx+(int)(cosf(a)*10),173+(int)(sinf(a)*10),0);
  if(lfomod!=0.f){float ab=(135.f+270.f*(v-pi->mn)/mm)*3.14159f/180.f;   /* метка базового значения (куда повёрнута ручка) */
   disc(cx+(int)(cosf(ab)*14),173+(int)(sinf(ab)*14),2);}
  if(W.p[P_OA]==ALGO_FM&&id==P_OL2){int r=(v*100+63)/127;sprintf(b,"%d:%d",100-r,r);}else if((id==P_OP||id==P_OP2)&&W.p[P_OA]!=ALGO_FM&&worm_wave_kind(val(id==P_OP?P_OW:P_OW2))==WK_MAIN)sprintf(b,"%d/64",(v*63+63)/127+1);   /* MAIN: номер кадра */
  else if((id==P_OP||id==P_OP2)&&W.p[P_OA]!=ALGO_FM&&worm_wave_kind(val(id==P_OP?P_OW:P_OW2))==WK_CYCLE)sprintf(b,"%d%%",(v*100+63)/127);   /* USER/LIB: BEND в % */
  else worm_fmt(id,v,b);text(cx,193,b,1,1,0);   /* MIX: доли OSC1:OSC2 в % */
 }
 step_t *s=sel();int d;if(s&&id>P_NPO&&(id<P_GSC||id>=NP)&&worm_step_lock_get(s,id,&d))rect(cx-28,146,4,4,1);}
static void plot(float(*f)(float,void*),void *u){int py=0;for(int i=0;i<=224;i++){float y=f(i/224.f,u);int yy=83-(int)(y*44);if(i)line(6+i-1,py,6+i,yy,0);py=yy;}}
/* ---- превью WAVETABLE (то же, что считает gen_va): MAIN — кадр POS из 64 с морфингом, USER/LIB — цикл с BEND ---- */
static float tab_at(const int16_t *tb,float ph){float x=ph*256.f;int k=(int)x;float fr=x-(float)k;
 float a=(float)tb[k&255]*(1.f/32768.f),b=(float)tb[(k+1)&255]*(1.f/32768.f);return a+(b-a)*fr;}
static float bend_ph(float ph,float pos){float k=pos*.9f;float q=ph-k*sinf(6.2831853f*ph)*.15915494f;return q-floorf(q);}
static float wave_at(int w,float pos,float ph){
 if(worm_wave_kind(w)==WK_MAIN){float fp=pos*(float)(WT_FRAMES-1);int f0=(int)fp;float mf=fp-(float)f0;int f1=f0<WT_FRAMES-1?f0+1:f0;
  const int16_t (*T)[WT_N]=worm_wt_main[w-WAVE_MAIN0];float a=tab_at(T[f0],ph),b=tab_at(T[f1],ph);return a+(b-a)*mf;}
 const int16_t *tb=worm_wave_cycle(w);return tb?tab_at(tb,bend_ph(ph,pos)):0.f;}
static float f_osc(float t,void *u){(void)u;int al=W.p[P_OA],o2=U.tab>=2,w=val(o2?P_OW2:P_OW);float pp=mval(o2?P_OP2:P_OP)/127.f,ph=t*2;ph-=floorf(ph);
 if(w>=WAVE_MAIN0&&al!=1)return wave_at(w,pp,ph);   /* превью реальной таблицы: MAIN = кадр по POS, USER/LIB = с BEND */
 if(al==1){static const float R[4]={1,2,3,.5f};return sinf(6.2832f*ph+pp*8.f*sinf(6.2832f*ph*R[w&3]));}
 if(al==0){if(w==5){unsigned h=(unsigned)(t*224.f+.5f)*2654435761u;h^=h>>15;h*=2246822519u;h^=h>>13;return (float)(int)h*(1.f/2147483648.f)*.9f;}   /* NOI: шум (раньше рисовался как пульс) */
  if(w==0)return sinf(6.2832f*ph);if(w==1)return 1.f-4.f*fabsf(ph-.5f);if(w==2)return 2.f*ph-1.f;if(w==3)return ph<.5f?1.f:-1.f;return ph<.05f+.45f*pp?1.f:-1.f;}
 if(al==5){float s=(float)(w+1)*0.25f,x=t*2.f;x-=floorf(x);return x<0.5f?(1.f-s*x*2.f):(-1.f+s*(x-0.5f)*2.f);}   /* ELMYRA: slew-трапеция (превью) */
 float s=0;for(int n=1;n<=16;n++)s+=sinf(6.2832f*n*ph)/n;return s*.6f;}
static float f_lfo(float t,void *u){(void)u;int w=val(P_L0W+U.lfo*9);float p=t*3;p-=floorf(p);
 switch(w){case 0:return 1.f-4.f*fabsf(p-.5f);case 1:return sinf(6.2832f*p);case 2:return p<.5f?1.f:-1.f;
 case 3:return 2.f*p-1.f;case 4:{int k=(int)(p*8.f);float h=sinf(k*12.9898f*6.2832f)*43758.5453f;h-=floorf(h);return h*2.f-1.f;}
 case 5:return expf(-p*6.f)*2.f-1.f;case 6:return p;
 default:{int o=P_L0D+U.lfo*9;float s=val(o+6)/127.f;if(s<.01f)s=.01f;if(s>.99f)s=.99f;float x=p<s?p/s:(1.f-p)/(1.f-s),sn=.5f-.5f*cosf(3.14159265f*x);x+=(sn-x)*(val(o+7)/127.f);
  float y=x*2.f-1.f+(val(o+8)-64)/64.f;return y>1.f?1.f:y<-1.f?-1.f:y;}}}   /* TDS: SLOPE/SMOOTH/SHIFT */
/* АЧХ фильтра (лог-ось 20 Гц..20 кГц — как у CUTOFF). Типы: 0 OFF, 1 LP, 2 HP, 3 BP (пик 1, как у SVF синта).
 * Учтён D/WET: звук = dry*(1-w) + фильтр*w (комплексная сумма с фазой фильтра). Ось Y: ±36 дБ. */
static float f_flt(float t,void *u){(void)u;int b=U.tab?P_F2C:P_F1C;float c=20.f*powf(1000.f,val(b)/127.f),q=.5f+val(b+1)/127.f*18.f,w=val(U.tab?P_F2W:P_F1W)/127.f;int ty=val(b+2);
 if(ty<1||ty>3)return 0.f;                                   /* OFF: ровная линия 0 дБ */
 float f=20.f*powf(1000.f,t),r=f/c,r2=r*r,a=1.f-r2,bb=r/q,m2=a*a+bb*bb+1e-9f,hr,hi;   /* знаменатель 1-r²+j·r/Q */
 if(ty==1){hr=a/m2;hi=-bb/m2;}else if(ty==2){hr=-r2*a/m2;hi=r2*bb/m2;}else{hr=bb*bb/m2;hi=a*bb/m2;}
 hr=(1.f-w)+w*hr;hi=w*hi;
 float db=20.f*log10f(sqrtf(hr*hr+hi*hi)+1e-4f),v=db/36.f;
 return v>1.f?1.f:v<-1.f?-1.f:v;}
static float f_dist(float t,void *u){(void)u;float x=t*2-1,g=1.f+val(P_ADR)/127.f*(val(P_ADS)==1?3:val(P_ADS)==2?10:30);return val(P_ADS)?tanhf(x*g):x;}
static float f_chr(float t,void *u){return sinf(t*18.8f+(*(int*)u?1.5f:0))*(*(int*)u?.8f:1.f);}
static void kbd(int y,int h,int (*fn)(int)){static const int wp[7]={0,2,4,5,7,9,11};for(int i=0;i<14;i++)rect(7+i*16,y,16,h,fn((i/7)*12+wp[i%7]));
 static const int bi[10]={0,1,3,4,5,7,8,10,11,12};for(int k=0;k<10;k++){int i=bi[k],m=(i/7)*12+wp[i%7]+1,x=7+i*16+11;box0(x-1,y,12,h*6/10+1);rect(x,y,10,h*6/10,1);if(fn(m))box0(x+2,y+2,6,h*6/10-4);}}
static int kb_root(int m){return m==(val(P_NR)%24);}
static int kb_scale(int m){int r=W.p[P_GRT],sc=W.p[P_GSC];for(int k=0;k<worm_scale_n[sc];k++)if(worm_scale_iv[sc][k]==((m-r)%12+12)%12)return 1;return 0;}
static void grid16(int mode){char b[8];int n=mode?NPAT:NBANK;const unsigned used=mode?worm_store_bank_used():0;   /* mode 0 = банки (NBANK=8), mode 1 = паттерны (NPAT=16) */
 for(int i=0;i<n;i++){int x=3+(i%4)*59,y=27+(i/4)*28,s=mode?i==W.slot:i==W.bank;rect(x,y,55,26,s);
  if(mode)sprintf(b,"%c%02d",'A'+W.bank,i+1);else sprintf(b,"%c",'A'+i);text(x+27,y+5,b,2,1,s);
  int has=(int)((used>>i)&1u);if(has)rect(x+47,y+3,4,4,!s);}}
static float bessel(int n,float b){float s=0;for(int i=0;i<32;i++){float t=3.14159265f*(i+.5f)/32.f;s+=cosf(n*t-b*sinf(t));}return fabsf(s/32.f);}
/* Схема алгоритма FM (Appendix A.3). Раскладка задана вручную: {столбец, строка снизу} для C, A, B1, B2.
 * Стрелка = фазовая модуляция; выход несущей помечен X / Y; пунктир = громкость выхода не зависит от огибающей оператора, сплошная = зависит.
 * Петля у оператора с FDBK. */
static const int8_t FMPOS[8][4][2]={
 {{0,0},{0,1},{1,1},{1,2}},{{0,0},{0,1},{1,0},{1,1}},{{0,0},{1,1},{2,0},{1,0}},{{0,0},{0,1},{0,2},{0,3}},
 {{0,0},{0,1},{0,2},{1,2}},{{0,0},{0,1},{1,0},{1,1}},{{0,0},{0,1},{2,0},{2,1}},{{0,0},{0,1},{2,0},{1,0}}};
enum{FBW=28,FBH=16,FCP=40};
static void fm_arrow(int x,int y,int dx,int dy){line(x-dx*3+dy*3,y-dy*3+dx*3,x,y,0);line(x-dx*3-dy*3,y-dy*3-dx*3,x,y,0);}   /* голова стрелки, направление (dx,dy) */
static void fm_dot(int x0,int y0,int x1,int y1){if(x0==x1)for(int y=y0;y<=y1;y+=3)rect(x0,y,1,2,1);else for(int x=x0;x<=x1;x+=3)rect(x,y0,2,1,1);}
static void fmpanel(void){const int al=val(worm_fm_slot[FM_ALGO])&7;const fmalg_t *A=&worm_fm_algo[al];
 static const char *const N[4]={"C","A","B1","B2"};int bx[4],by[4],x0=999,x1=-999,y0=999,y1=-999,occ[4][4],mr=0;memset(occ,0,sizeof occ);
 for(int op=0;op<4;op++)if(FMPOS[al][op][1]>mr)mr=FMPOS[al][op][1];
 const int frp=mr?((83-FBH)/mr>34?34:(83-FBH)/mr):0;   /* шаг строк: 3 ряда шире, 4 ряда теснее */
 for(int op=0;op<4;op++){bx[op]=FMPOS[al][op][0]*FCP;by[op]=(mr-FMPOS[al][op][1])*frp;occ[FMPOS[al][op][0]][FMPOS[al][op][1]]=1;
  if(bx[op]<x0)x0=bx[op];if(bx[op]+FBW+22>x1)x1=bx[op]+FBW+22;if(by[op]-5<y0)y0=by[op]-5;}
 y1=mr*frp+FBH+20;
 const int ox=120-(x0+x1)/2,oy=81-(y0+y1)/2,base=mr*frp+FBH+oy;   /* по центру панели (3..237 x 27..135); base — низ линий выхода */
 for(int op=0;op<4;op++){bx[op]+=ox;by[op]+=oy;rect(bx[op],by[op],FBW,FBH,0);text(bx[op]+FBW/2,by[op]+5,N[op],1,1,0);}
 for(int op=0;op<4;op++)for(int t=0;t<4;t++)if(A->mod[t]>>op&1){   /* модуляция op -> t */
  const int sx=bx[op]+FBW/2,sy=by[op]+FBH,tx=bx[t]+FBW/2,ty=by[t],dr=FMPOS[al][op][1]-FMPOS[al][t][1],dc=FMPOS[al][op][0]-FMPOS[al][t][0];
  if(dc==0){line(sx,sy,sx,ty,0);fm_arrow(sx,ty,0,1);}
  else if(dr==1&&(dc==1||dc==-1)){line(sx,sy,tx,ty,0);fm_arrow(tx,ty,0,1);}
  else{int ex=dc>0?bx[op]:bx[op]+FBW,yy=by[op]+FBH/2;line(ex,yy,tx,yy,0);line(tx,yy,tx,ty,0);fm_arrow(tx,ty,0,1);}}
 for(int op=0;op<4;op++){const unsigned b=1u<<op;const int gx=(A->xd|A->xs)&b?1:(A->yd|A->ys)&b?2:0,dot=(A->xd|A->yd)&b;if(!gx)continue;
  const char *lt=gx==1?"X":"Y";int free_below=1;for(int r=FMPOS[al][op][1]-1;r>=0;r--)if(occ[FMPOS[al][op][0]][r])free_below=0;
  const int cx=bx[op]+FBW/2;
  if(free_below){if(dot)fm_dot(cx,by[op]+FBH,cx,base+4);else line(cx,by[op]+FBH,cx,base+4,0);fm_arrow(cx,base+4,0,1);text(cx,base+12,lt,1,1,0);}
  else{int yy=by[op]+FBH/2,xe=bx[op]+FBW+10;if(dot)fm_dot(bx[op]+FBW,yy,xe,yy);else line(bx[op]+FBW,yy,xe,yy,0);fm_arrow(xe,yy,1,0);text(xe+8,yy-3,lt,1,1,0);}}
 {const int op=A->fb;int x=bx[op]+FBW,y=by[op];line(x,y+4,x+5,y+4,0);line(x+5,y+4,x+5,y-5,0);line(x+5,y-5,x-7,y-5,0);line(x-7,y-5,x-7,y,0);}   /* петля FDBK */
}
static void fmt_mt(char *o,int pct){int ms=worm_mt_ms10(pct,W.bpm),a=ms<0?-ms:ms;char c[16];
 if(a>=1000)sprintf(c,"%s%d MS",ms<0?"-":"+",(a+5)/10);else sprintf(c,"%s%d.%d MS",ms<0?"-":"+",a/10,a%10);strcpy(o,pct?c:"0 MS");}
/* микротайминг как у Elektron: сдвиг в долях шага, сокращённая дробь ("+1/4", "-3/20", "0"); 100% = целый шаг */
static void fmt_frac(char *o,int pct){int a=pct<0?-pct:pct,x=a,y=100;if(!pct){strcpy(o,"0");return;}
 while(y){int t=x%y;x=y;y=t;}   /* НОД */
 if(a==100)sprintf(o,"%c1 STEP",pct<0?'-':'+');else sprintf(o,"%c%d/%d",pct<0?'-':'+',a/x,100/x);}
/* RETRIG (NOTE TRIG + EDIT в REC): удары серии — столбики, высота = velocity удара (наклон R.VEL), ширина = гейт */
static void rtpanel(void){char b[40];step_t q;memset(&q,0,sizeof q);
 q.rtr=(uint8_t)val(P_RTR);q.rtl=(uint8_t)val(P_RTL);q.len=(uint8_t)val(P_NL);int vel=val(P_NV),rv=val(P_RTV);
 uint32_t ss=worm_step_len(),it=ss;int n=worm_retrig_hits(&q,ss,&it);
 static const uint8_t RL[6]={0,1,2,4,8,16};
 uint32_t L=q.rtl?RL[q.rtl>5?5:q.rtl]*ss:worm_len_samples(q.len,ss);if(!q.rtr){n=1;it=ss;}if(L<it)L=it;if(!L)L=1;
 int ve=vel+rv*2;if(ve<1)ve=1;if(ve>127)ve=127;
 const int X0=8,W0=224,YB=112,HM=48;   /* поле: x 8..232, дно y=112, высота столбика до 48 px */
 /* шапка: режим и число ударов / длина серии */
 if(!q.rtr)text(X0,29,"RETRIG OFF",2,0,0);else{sprintf(b,"RETRIG %s",worm_pinfo[P_RTR].list[q.rtr]);text(X0,29,b,2,0,0);}
 sprintf(b,"%d HIT%s",n,n==1?"":"S");text(X0,49,b,1,0,0);
 sprintf(b,"LEN %s",q.rtl?worm_pinfo[P_RTL].list[q.rtl]:"NOTE");text(X0+W0,49,b,1,2,0);
 /* поле: дно, граница конца серии, метки шагов (каждая метка — один шаг секвенсора) */
 line(X0,YB+1,X0+W0,YB+1,0);line(X0+W0,YB-HM,X0+W0,YB,1);
 for(uint32_t t=0;t<=L;t+=ss){int x=X0+(int)((uint64_t)t*W0/L);line(x,YB+2,x,YB+5,0);if(!ss)break;}
 /* удары: шаг между столбиками >= 3.5 px (n<=64), ширина ~55% шага — столбики не слипаются; высота = velocity удара (наклон R.VEL) */
 for(int k=0;k<n;k++){int x=X0+(int)((uint64_t)k*it*W0/L),x2=X0+(int)((uint64_t)(k+1)*it*W0/L),w=(x2-x)*55/100;if(w<2)w=2;if(x+w>X0+W0)w=X0+W0-x;if(w<1)continue;
  int v=n>1?vel+(ve-vel)*k/(n-1):vel;if(v<1)v=1;int h=v*HM/127;if(h<2)h=2;rect(x,YB-h,w,h,1);}
 /* низ: одна строка — VEL слева, COND справа (не пересекаются, рамка экрана не режет) */
 sprintf(b,"VEL %d>%d",vel,n>1?ve:vel);text(X0,124,b,1,0,0);
 sprintf(b,"COND %s",worm_pinfo[P_NCD].list[val(P_NCD)]);text(X0+W0,124,b,1,2,0);}
/* MIDI-дорожка: экран CC-LFO — форма волны линии (2 цикла) с глубиной и живое значение CC */
static float f_mlfo(float t,void *u){(void)u;int l=U.lfo&(NMLANE-1);float p=t*2+val(P_MLANE(l,6))/127.f;return worm_mlane_wave(val(P_MLANE(l,4)),p)*val(P_MLANE(l,2))/127.f;}
static void mlfo_panel(void){char b[32];int l=U.lfo&(NMLANE-1),cc=val(P_MLANE(l,0));line(6,83,230,83,1);plot(f_mlfo,0);
 if(cc<0){text(8,31,"CC OFF",1,0,0);return;}
 sprintf(b,"CC%d  BASE %d",cc,val(P_MLANE(l,1)));text(8,31,b,1,0,0);
 {int lv=worm_mlane_value(W.mpat,l);if(lv>=0){sprintf(b,"OUT %d",lv);text(8,126,b,1,0,0);rect(70,127,lv*160/127,8,1);}}}
static void mid_panel(void){char b[40],c[16];
 if(!U.tab){int ch=val(P_MCH);
  for(int i=0;i<16;i++){int x=6+(i%8)*28,y=29+(i/8)*28,a=i+1==ch;rect(x,y,26,24,a);sprintf(b,"%d",i+1);text(x+13,y+8,b,1,1,a);}
  worm_fmt(P_MPG,val(P_MPG),c);sprintf(b,"PROGRAM %s",c);text(8,90,b,1,0,0);
  {char m[8],l[8];worm_fmt(P_MBM,val(P_MBM),m);worm_fmt(P_MBL,val(P_MBL),l);sprintf(b,"BANK MSB %s  LSB %s",m,l);text(8,104,b,1,0,0);}
  sprintf(b,"M%d > USB MIDI OUT  %s",W.mpat+1,worm_mtrk_active(W.mpat)?"NOTE ON":"");text(8,120,b,1,0,0);}
 else{static const int I[2]={P_MVL,P_MPN};static const char*N[2]={"VOL CC7","PAN CC10"};
  for(int i=0;i<2;i++){int y=44+i*34,v=val(I[i]);text(10,y,N[i],1,0,0);rect(10,y+12,200,12,0);if(v>=0)rect(10,y+12,v*200/127,12,1);worm_fmt(I[i],v,c);text(216,y+14,c,1,0,0);}
  sprintf(b,"MUTE %s",val(P_MMT)?"ON":"OFF");text(10,114,b,1,0,0);}}
static void mcc_panel(void){char b[16];
 for(int i=0;i<NMLANE;i++){int x=10+i*56,cc=val(P_MLANE(i,0)),lv=worm_mlane_value(W.mpat,i);
  rect(x,34,34,84,0);if(lv>=0)rect(x,34+84-lv*84/127,34,lv*84/127,1);
  if(cc<0)strcpy(b,"OFF");else sprintf(b,"CC%d",cc);text(x+17,124,b,1,1,0);
  if(lv>=0){sprintf(b,"%d",lv);text(x+17,28,b,1,1,0);}}}
/* NOTE TRIG + удержание HOME: обзор микротайминга всех 16 шагов текущей страницы секвенсора */
static int mt_view(void){return U.rec&&U.scr==S_NOTE&&U.mt_home&&(U.hb&(1u<<8));}   /* только если HOME нажат УЖЕ в NOTE TRIG (не при переходе HOME+OP1 из другого меню) */
static void mt_overview(int v0){char b[40];
 text(8,30,"OFFSET, % OF STEP",1,0,0);
 rect(6,80,228,1,1);   /* ноль: ровно в сетку; вверх — позже, вниз — раньше */
 text(0,66,"+",1,0,0);text(0,86,"-",1,0,0);
 for(int j=0;j<16;j++){step_t *s=&PT()->s[v0+j];int x=8+j*14,m=s->mt,a=m<0?-m:m,h=a*28/MT_MAX;if(m&&!h)h=1;   /* столбик: 100% = 28 px */
  if(m)rect(x+2,m>0?80-h:81,8,h,s->on);else rect(x+2,80,8,1,1);   /* выключенный шаг — контур */
  if(m){sprintf(b,"%d",a);text(x+6,m>0?80-h-9:81+h+1,b,1,1,0);}
  b[0]="123456789ABCDEFG"[j];b[1]=0;if(s->on)rect(x+1,122,10,10,1);text(x+6,124,b,1,1,s->on);}}   /* номер шага в странице; включённый — инверсия */   /* значение у столбика; номера шагов — штатная строка клавиш ниже */
/* OSC-панель WAVETABLE: MAIN — «водопад» из кадров (текущий кадр ярко, задние тускнее, передние закрывают задние);
 * USER/LIB — цикл с BEND (исходная форма пунктиром). Формы SIN..NOI — как раньше (plot). */
static void osc_panel(void){
 const int o2=U.tab>=2,w=val(o2?P_OW2:P_OW),wk=worm_wave_kind(w);char b[24];
 if(wk==WK_SHAPE){plot(f_osc,0);return;}
 const float pos=mval(o2?P_OP2:P_OP)/127.f;   /* с LFO/ENV: кадр/изгиб двигаются вместе с ручкой */
 if(wk==WK_MAIN){
  const int X0=12,WD=140,SH=60,YB=122,AM=12,NG=13;const float fp=pos*(float)(WT_FRAMES-1);
  const int16_t (*T)[WT_N]=worm_wt_main[w-WAVE_MAIN0];
  /* фоновые кадры 63,60,..,0 (14 штук) от дальнего к ближнему: кривая ближнего закрывает дальние под собой; потом текущий кадр поверх */
  for(int pass=0;pass<2;pass++){
   const int n=pass?1:NG+1;
   for(int i=0;i<n;i++){
    const float f=pass?fp:(i==0?63.f:(float)((NG-i)*5));
    const int f0=(int)f,f1=f0<WT_FRAMES-1?f0+1:f0;const float mf=f-(float)f0;
    const int xo=X0+(int)(f*SH/63.f*.9f),yb=YB-(int)(f*SH/63.f+.5f);int py=0;
    for(int x=0;x<=WD;x++){
     const float xx=(float)x/(float)WD*255.f;const int k=(int)xx;const float fr=xx-(float)k;
     const float a=(float)T[f0][k]*(1.f-fr)+(float)T[f0][(k+1)&255]*fr,c=(float)T[f1][k]*(1.f-fr)+(float)T[f1][(k+1)&255]*fr;
     const float y=(a+(c-a)*mf)*(1.f/32768.f);const int yy=yb-(int)(y*AM+(y>0.f?.5f:-.5f));
     const int fill=pass?4:6;   /* закрыть то, что за этой кривой (текущий кадр чистит себе дорожку чуть уже) */
     for(int j=1;j<=fill;j++){const int yy2=yy+j;if(yy2>yb+1)break;if((unsigned)(xo+x)<LCD_W&&(unsigned)yy2<LCD_H)F[yy2*LCD_W+xo+x]=0;}
     if(x){if(pass){gline(xo+x-1,py,xo+x,yy,15);gline(xo+x-1,py-1,xo+x,yy-1,11);}
      else gline(xo+x-1,py,xo+x,yy,3+(int)((1.f-f/63.f)*6.f));}
     py=yy;}}}
  { const int yb=YB-(int)(fp*SH/63.f+.5f),xo=X0+(int)(fp*SH/63.f*.9f);   /* метка текущего кадра: стрелка слева от базовой линии */
    line(xo-9,yb,xo-3,yb,0);px(xo-4,yb-1,1);px(xo-4,yb+1,1);px(xo-5,yb-2,1);px(xo-5,yb+2,1);}
  sprintf(b,"%s",worm_wave_label(w));text(8,29,b,1,0,0);
  sprintf(b,"FRAME %d/%d",(int)(fp+.5f)+1,WT_FRAMES);text(232,29,b,1,2,0);
  return;}
 /* USER / LIB: один цикл, BEND */
 const int AMP=44;int py=0,pb=0;
 line(6,83,230,83,1);
 for(int i=0;i<=224;i++){float ph=i/224.f*2.f;ph-=floorf(ph);if(i==224)ph=.999f;
  float y0=tab_at(worm_wave_cycle(w),ph),y1=wave_at(w,pos,ph);int a0=83-(int)(y0*AMP),a1=83-(int)(y1*AMP);
  if(pos>.004f&&i&&(i&7)<4)gline(6+i-1,pb,6+i,a0,5);if(i)gline(6+i-1,py,6+i,a1,15);py=a1;pb=a0;}   /* исходная форма — пунктиром */
 sprintf(b,"%s",worm_wave_label(w));text(8,29,b,1,0,0);
 sprintf(b,"BEND %d%%",(int)(pos*100.f+.5f));text(232,29,b,1,2,0);
}
static void panel(void){char b[40];step_t *st;int v0=vp()*16;
 if(mt_view()){mt_overview(v0);return;}
 switch(U.scr){
 case S_NOTE:if(W.mid&&U.tab){sprintf(b,"QUANT %s",worm_pinfo[P_MQZ].list[val(P_MQZ)]);text(8,40,b,1,0,0);sprintf(b,"CHORD %s",worm_pinfo[P_MCD].list[val(P_MCD)]);text(8,56,b,1,0,0);
   sprintf(b,"MUTE %s",worm_pinfo[P_MMT].list[val(P_MMT)]);text(8,72,b,1,0,0);text(8,96,"QUANT: SCALE LOCK (GLO",1,0,0);text(8,108,"> SCALE). OFF FOR DRUMS",1,0,0);}
  else if(U.tab){for(int i=0;i<224;i++){float y=-.6f+1.2f*(1.f-expf(-(i/224.f)*(8.f-val(P_NPT)/20.f)));line(6+i,83-(int)(y*44),6+i,83-(int)(y*44),0);}}
  else if(U.tc)rtpanel();
  else{worm_fmt(P_NR,val(P_NR),b);text(8,31,b,3,0,0);kbd(76,52,kb_root);
  }break;
 case S_OSC:if(W.p[P_OA]==ALGO_FM)fmpanel();else osc_panel();break;case S_LFO:if(W.mid)mlfo_panel();else plot(f_lfo,0);break;case S_MID:mid_panel();break;case S_MCC:mcc_panel();break;case S_FLT:if(U.tab<2){line(6,83,230,83,1);{int id=U.tab?P_F2C:P_F1C,c=val(id),l=c+(int)(mod_of(id)*127.f+.5f);if(l<0)l=0;if(l>127)l=127;line(6+c*224/127,30,6+c*224/127,136,1);if(l!=c)line(6+l*224/127,30,6+l*224/127,136,0);}plot(f_flt,0);}else{   /* 0 дБ и маркер CUTOFF */int bs=P_E0A+(U.tab-1)*4,a=val(bs),d=val(bs+1),sv=val(bs+2),r=val(bs+3),y=130-(94*sv)/127,x1=10+a*6/10,x2=x1+d*8/10,x3=x2+50;
  line(8,130,x1,36,0);line(x1,36,x2,y,0);line(x2,y,x3,y,0);line(x3,y,x3+r*8/10,130,0);}break;
 case S_AMP:if(U.tab==2){int amt=val(P_TRN);rect(20,90,180,16,0);rect(20,90,amt*180/127,16,1);sprintf(b,"%d%%",amt*100/127);text(210,93,b,1,0,0);}else if(U.tab){static const int I[3]={P_SDL,P_SRV,P_SCH};static const char*N[3]={"DLY","REV","CHO"};for(int i=0;i<3;i++){int y=40+i*30;text(10,y,N[i],1,0,0);rect(40,y-2,150,12,0);int bv=val(I[i]),l=bv+(int)(mod_of(I[i])*127.f+.5f);if(l<0)l=0;if(l>127)l=127;rect(40,y-2,l*150/127,12,1);if(l!=bv){int bx=40+bv*150/127;if(bv<l)box0(bx,y-2,2,12);else rect(bx,y-2,2,12,1);}sprintf(b,"%d",val(I[i]));text(196,y,b,1,0,0);}}else plot(f_dist,0);break;
 case S_ENV:{int e=envk(),bs=P_E0A+e*4,a=val(bs),d=val(bs+1),s=val(bs+2),r=val(bs+3),y=130-(94*s)/127,x1=10+a*6/10,x2=x1+d*8/10,x3=x2+50;
  line(8,130,x1,36,0);line(x1,36,x2,y,0);line(x2,y,x3,y,0);line(x3,y,x3+r*8/10,130,0);
 if(e==4&&worm_synth_active_trk(W.pat)>0){int yl=130-(int)(94.f*worm_synth_env4());line(6,yl,234,yl,1);rect(3,yl-2,4,5,1);}   /* E4: живой уровень огибающей (пунктир + метка слева) */
 break;}
 case S_FX:if(U.tab==0){float f=val(P_XFB)/127.f*.95f,h=95;for(int i=0;i<8;i++){rect(20+i*26,135-(int)h,12,(int)h,1);h*=f;}}
  else if(U.tab==1){int dc=val(P_RDC);for(int i=0;i<60;i++){float k=powf(1.f-i/60.f,1.f+(100-dc)/60.f)*95.f*(.5f+.5f*fabsf(sinf(i*7.3f)));rect(8+(int)(i*3.7f),135-(int)k,2,(int)k,1);}}
  else {int z=0,o=1;plot(f_chr,&o);plot(f_chr,&z);}break;
 case S_SPG:{int pg=W.p[P_GPG];for(int i=0;i<8;i++){int x=6+i*28;rect(x,29,26,12,i==vp());if(i>=pg)box0(x+1,30,24,10),rect(x,29,26,12,0);sprintf(b,"%d",i+1);text(x+13,31,b,1,1,i==vp());}
  for(int j=0;j<16;j++){int x=6+(j%8)*28,y=48+(j/8)*32,k=v0+j;step_t *s=&PT()->s[k];rect(x,y,26,26,s->on);sprintf(b,"%02d",j+1);text(x+13,y+3,b,1,1,s->on);if(k==U.sel)rect(x+11,y+16,4,4,!s->on);if(s->nl)rect(x+20,y+20,4,4,!s->on);}
  if((st=sel())){sprintf(b,"STEP %02d  ",U.sel+1);worm_fmt(P_NR,st->note,b+strlen(b));text(8,116,b,1,0,0);sprintf(b,"COND %s  P.GLUE %s",L_COND[st->cond],st->nl?"ON":"OFF");text(8,127,b,1,0,0);}break;}
 case S_GRID:line(7,90,233,90,1);for(int j=0;j<16;j++){step_t *s=&PT()->s[v0+j];int x=8+j*14;if(v0+j==U.sel)rect(x-1,28,14,1,1);
   if(s->on){int n=s->note<36?36:s->note>84?84:s->note,vv=s->vel*3/10;rect(x,85-(n-36)*11/10,12,4,1);rect(x+3,135-vv,6,vv,1);}else rect(x+5,86,2,2,1);}break;
 case S_PLAY:{int len=W.p[P_GLN];sprintf(b,"%s  LEN %d",L_DIR[W.p[P_GDR]],len);text(10,31,b,1,0,0);for(int i=0;i<(len<16?len:16);i++)rect(10+i*13,70,11,11,0);break;}
 case S_MT:{int mi=U.mt_i<0?0:U.mt_i,m=PT()->s[mi].mt;char c[24];
  /* окно как у Elektron: рамка, инверсный заголовок MICRO TIMING, линейка с засечками, значение — ДРОБЬ ШАГА (+1/4, -3/20...) */
  rect(8,26,224,104,0);rect(8,26,224,12,1);                                               /* рамка (контур) + светлая полоса заголовка */
  text(120,28,"MICRO TIMING",1,1,1);                                                       /* заголовок на светлой полосе */
  for(int t=-100;t<=100;t+=5){int x=120+t*100/100,L=t%100==0?16:t%50==0?13:t%10==0?9:4;rect(x,56-L,1,L,1);}   /* линейка: -1 STEP .. 0 .. +1 STEP; длинные засечки — 0, ±1/2, ±1 */
  rect(20,56,200,1,1);
  text(20,66,"-1",1,1,0);text(120,66,"0",1,1,0);text(220,66,"+1",1,1,0);                  /* подписи в шагах */
  rect(120+m-2,58,5,2,1);rect(120+m-1,60,3,2,1);rect(120+m,62,1,2,1);                      /* маркер-стрелка под текущим сдвигом */
  fmt_frac(b,m);text(120,82,b,2,1,0);                                                      /* крупно: сдвиг дробью шага */
  fmt_mt(c,m);sprintf(b,"STEP %d",mi+1);text(16,112,b,1,0,0);sprintf(b,"%s @ %d BPM",c,(int)(W.bpm+.5f));text(224,112,b,1,2,0);   /* номер шага слева, миллисекунды справа */
  text(24,134,"OCT- EARLIER",1,0,0);text(216,134,"LATER OCT+",1,2,0);
  for(int q=0;q<16;q++){step_t *t2=&PT()->s[v0+q];int x=8+q*14,mm=t2->mt,hh=(mm<0?-mm:mm)*24/MT_MAX;if(mm&&!hh)hh=1;rect(x+3,mm>0?172-hh:172,6,mm?hh:1,1);if(v0+q==mi)rect(x,150,12,1,1);}   /* сдвиги всех 16 шагов страницы */
  break;}
 case S_SCL:sprintf(b,"SCALE %s  ROOT %s",L_SCALE[W.p[P_GSC]],L_NOTE[W.p[P_GRT]]);text(8,31,b,1,0,0);kbd(50,76,kb_scale);break;
 case S_SET:if(U.tab==2){static const char *L[2]={"MUTED WHEN USB","ALWAYS ON"};text(8,31,"FM-1 SPEAKER/PHONES",1,0,0);text(8,43,"(SELECT KNOB)",1,0,0);
  for(int i=0;i<2;i++){int y=62+i*16,a=W.usb_local==i;rect(6,y-2,228,13,a);text(12,y,L[i],1,0,a);}
  text(8,100,"MUTED: OFF WHILE A",1,0,0);text(8,112,"COMPUTER IS CONNECTED",1,0,0);text(8,124,"USB AUDIO UNAFFECTED",1,0,0);}
 else if(!U.tab){static const char *O[4]={"MASTER   24BIT 48K","TRK 1-3  16BIT 44K","TRK 4-6  16BIT 44K","FX       16BIT 44K"};text(8,31,"OUTPUT (SELECT KNOB)",1,0,0);int cf=U.confirm==3,cur=cf?U.pending_usb:W.usb_out;   /* confirm==3: выбор ждёт OCT+ */
  for(int i=0;i<4;i++){int y=44+i*16,a=cur==i;rect(6,y-2,228,13,a);text(12,y,O[i],1,0,a);if(W.usb_out==i)text(198,y,"ON",1,0,a);}
  if(cf){text(8,116,"OCT+ APPLY",1,0,0);text(8,128,"OCT- CANCEL",1,0,0);}else{text(8,116,"USB AUDIO: ONE MODE",1,0,0);text(8,128,"AT A TIME",1,0,0);}}
  else if(U.tab==3){static const char *M[2]={"MASS STORAGE","AUDIO + MIDI"};text(8,31,"USB MODE (SELECT KNOB)",1,0,0);
   int cf=U.confirm==5,cur=cf?U.pending_usb:W.usb_mode;
   for(int i=0;i<2;i++){int y=48+i*16,a=cur==i;rect(6,y-2,228,13,a);text(12,y,M[i],1,0,a);if(W.usb_mode==i)text(198,y,"ON",1,0,a);}
   if(cf){text(8,104,"OCT+ APPLY",1,0,0);text(8,116,"OCT- CANCEL",1,0,0);}
   else if(W.usb_mode==0){text(8,104,"DISK: NO AUDIO / MIDI",1,0,0);text(8,116,"EJECT, THEN SELECT AUDIO",1,0,0);}
   else{text(8,104,"AUDIO + USB MIDI",1,0,0);text(8,116,"ONE USB MODE AT A TIME",1,0,0);}}
  else if(U.tab==4){static const char *C[4]={"DG1","DG2","MD","MM"};text(8,31,"COLOR SCHEME (SELECT KNOB)",1,0,0);
   int cf=U.confirm==7,cur=W.theme,saved=cf?col_old:W.theme;   /* схема применяется сразу (предпросмотр), OCT+ сохраняет, OCT- возвращает прежнюю */
   for(int i=0;i<4;i++){int y=44+i*16,a=cur==i;rect(6,y-2,228,13,a);text(12,y,C[i],1,0,a);if(saved==i)text(198,y,"ON",1,0,a);}
   if(cf){text(8,116,"OCT+ APPLY",1,0,0);text(8,128,"OCT- CANCEL",1,0,0);}
   else{text(8,116,"DG1  DG2  MD  MM",1,0,0);text(8,128,"SELECT KNOB TO CHANGE",1,0,0);}}
  else{static const char *H[2]={"COMPUTER (DEVICE)","EXTERNAL (HOST)"};text(8,31,"USB MIDI MODE (KNOB)",1,0,0);
  int cf=U.confirm==4,cur=cf?U.pending_usb:W.usb_host;for(int i=0;i<2;i++){int y=44+i*16,a=cur==i;rect(6,y-2,228,13,a);text(12,y,H[i],1,0,a);if(W.usb_host==i)text(198,y,"ON",1,0,a);}
  if(cf){text(8,82,"OCT+ APPLY",1,0,0);text(8,94,"OCT- CANCEL",1,0,0);}
  else if(W.usb_host){const char *st=worm_usbh==2?"CONNECTED":worm_usbh==1?"LOOKING FOR DEVICE":"STARTING";text(8,82,st,1,0,0);{static const char *M[7]={"-","HOST ON","DEV GONE","MIDI FOUND","NOT MIDI DEV","LOW-SPEED DEV","LOST"};sprintf(b,"LAST: %s",M[worm_usbh_msg%7]);text(8,94,b,1,0,0);sprintf(b,"S%d LEN%d CLS%d",(int)(worm_usbh_dbg&255u),(int)((worm_usbh_dbg>>8)&0xFFFFu),(int)(worm_usbh_dbg>>24));text(8,106,b,1,0,0);}}
  else{text(8,82,"ABLETON / PC / MAC",1,0,0);text(8,94,"+ USB AUDIO",1,0,0);}
  text(8,118,"OCT+ = PANIC",1,0,0);}break;
 case S_MET:{static const char *const L[6]={"CPU  ","XRUN ","MAX  ","UND  ","MISS ","LIMdB"};int q[6]={worm_cpu_pct,worm_xruns,worm_max_us,worm_uac_und,worm_uac_miss,worm_lim_db};   /* правая колонка: значения выровнены по правому краю поля */
   text(8,31,"BPM  (SELECT KNOB)",1,0,0);sprintf(b,"%d.%d",(int)W.bpm,(int)(W.bpm*10)%10);text(8,43,b,3,0,0);
   for(int i=0;i<4;i++)rect(10+i*34,85,30,30,W.run&&worm_beat_idx()==i);
   for(int i=0;i<6;i++){int v=q[i];if(v<0)v=0;if(v>99999)v=99999;   /* 5 знаков влезают в 240 px, больше — просто урезаем вывод (только экран) */
    stat_line(b,L[i],v,i==2);text(144,43+i*12,b,1,0,0);}}break;
 case S_VOX:{   /* слева 6 звуковых каналов (номер | уровень | шаги/MUTE), справа 4 MIDI-канала M1..M4 (номер | канал | шаги/MUTE); SEL + клавиши 1-6 / 16-13 */
  text(3,28,"AUDIO",1,0,0);text(123,28,"MIDI",1,0,0);
  for(int i=0;i<NTRK;i++){int y=38+i*17,s=!W.mid&&i==U.chan,n=0;for(int k=0;k<NSTEP;k++)n+=W.pats[i].s[k].on;
   rect(3,y,14,14,s);sprintf(b,"%d",i+1);text(10,y+3,b,1,1,s);
   rect(20,y+2,58,10,0);rect(20,y+2,W.p[P_C0L+i]*58/127,10,1);
   int mu=worm_trk_muted(i),on=worm_synth_active_trk(i)>0;rect(82,y+1,34,12,mu||on);
   if(mu)text(99,y+3,"M",1,1,0);else{sprintf(b,"%d",n);text(99,y+3,b,1,1,on);}}
  for(int i=0;i<NMTRK;i++){int y=38+i*17,s=W.mid&&i==W.mpat,n=0;for(int k=0;k<NSTEP;k++)n+=W.mpats[i].s[k].on;
   rect(123,y,14,14,s);sprintf(b,"%d",i+1);text(130,y+3,b,1,1,s);
   rect(140,y+2,58,10,0);sprintf(b,"CH %d",(i==W.mpat&&W.mid?W.p[P_MCH]:W.mt[i].p[worm_mpos(P_MCH)]));text(144,y+3,b,1,0,0);
   int mu=worm_mtrk_muted(i),on=worm_mtrk_active(i)>0;rect(202,y+1,34,12,mu||on);
   if(mu)text(219,y+3,"M",1,1,0);else{sprintf(b,"%d",n);text(219,y+3,b,1,1,on);}}
  break;}
 case S_ARP:if(W.mid){sprintf(b,"M%d  MODE %s  OCT %d",W.mpat+1,L_ARP[W.p[P_MAM]],W.p[P_MAO]);text(8,40,b,1,0,0);text(8,60,"HOLD CHORD: ARP > MIDI",1,0,0);sprintf(b,"CHAN %d",W.p[P_MCH]);text(8,80,b,1,0,0);}
  else if(U.tab){sprintf(b,"ARP > MIDI: %s",L_AMO[W.p[P_AMO]]);text(8,40,b,1,0,0);text(8,60,"ARP NOTES ALSO GO TO",1,0,0);text(8,72,"THE MIDI TRACK (CHAN)",1,0,0);}
  else{sprintf(b,"MODE %s  OCT %d",L_ARP[W.p[P_ARM]],W.p[P_ARO]);text(8,40,b,1,0,0);text(8,60,"HOLD CHORD: ARP PLAYS",1,0,0);}break;
 case S_EUCL:{int hits=W.p[P_EUH],off=W.p[P_EUO],lim=W.p[P_EUL],mute=W.p[P_EUM];
  for(int i=0;i<16;i++){int x=6+i*14,on=i<lim&&worm_euclid_gate(hits,off,lim,mute,i);rect(x,50,12,44,on);}
  sprintf(b,"HITS %d  OFFSET %d  LIM %d",hits,off,lim);text(8,108,b,1,0,0);
  if(mute)text(8,120,"MUTED",1,0,0);break;}
  case S_GRIDSZ:{   /* GRID SIZE: своя длина и скорость каждой дорожки (полиметер). Выбор дорожки: SEL + белые клавиши 1-6. */
    text(6,28,"TRK",1,0,0);text(26,28,"GRID",1,0,0);text(64,28,"SPD",1,0,0);text(110,28,"LENGTH",1,0,0);
    line(6,38,234,38,1);
    {int mx=32;for(int q=0;q<NTRK;q++){int g=W.tr[q].len;if(g>mx)mx=g;}   /* общая шкала: самая длинная дорожка заполняет всю полосу */
    for(int t=0;t<NTRK;t++){
     int y=48+t*15,cur=!W.mid&&t==U.chan;int gl=W.tr[t].len;if(gl<1)gl=1;if(gl>128)gl=128;
     if(cur)rect(4,y-1,14,10,1);                                         /* подсветка выбранной дорожки: белый блок, цифра — чёрная */
     sprintf(b,"%d",t+1);text(11,y,b,1,1,cur);
     sprintf(b,"%3d",gl);text(30,y,b,1,0,0);
     text(64,y,L_SPD[(W.tr[t].spd<0||W.tr[t].spd>2)?1:W.tr[t].spd],1,0,0);
     {int bw=122*gl/mx;if(bw<2)bw=2;rect(110,y-3,124,10,0);rect(111,y-2,bw,8,1);   /* контур у всех + заливка у всех */
      if(W.run&&W.trk_step[t]>=0&&W.trk_step[t]<gl)box0(111+122*W.trk_step[t]/mx,y-2,2,8);}
    }
    }
    if(pend_pat>=0){sprintf(b,"NEXT %c%02d",'A'+W.bank,pend_pat+1);text(88,134,b,1,1,0);}
    break;}}}
/* Полоса страниц секвенсора в шапке (после BPM): 8 квадратиков по числу страниц дорожки.
 * Заливка — на странице есть триггеры/p-lock/микротайминг; страница под бегунком мигает;
 * чёрточка снизу показывает редактируемую страницу. */
static void page_strip(int x0){
  static const int SQ=8,GAP=3;
  const pattern_t *pt=PT();int len=W.p[P_GLN];if(len<1)len=1;if(len>NSTEP)len=NSTEP;
  int cur=(W.run&&W.cur>=0)?W.cur/16:-1;if(cur>7)cur=7;
  int blink=(int)((worm_ui_now/250u)&1u),edit=vp();
  for(int p=0;p<8;p++){
    int x=x0+p*(SQ+GAP),base=p*16,used=0;
    for(int j=0;j<16&&base+j<len;j++){const step_t*s=&pt->s[base+j];if(s->on||s->nl||s->mt){used=1;break;}}
    rect(x,2,SQ,SQ,0);
    if(p==cur?blink:used)rect(x+1,3,SQ-2,SQ-2,1);
    if(p==edit)rect(x+2,10,SQ-4,1,1);
  }
}
/* Всплывающее уведомление: SAVED / PRESET SAVED / BANK FULL вместо строки в шапке. */
static const char *msg_title(void){switch(U.msg_k){case MK_PAT:return "PATTERN SAVED";case MK_BANK:return "BANK SAVED";case MK_PRS:return "PRESET SAVED";case MK_FULL:return "BANK FULL";case MK_ERR:return "SAVE ERROR";default:return "DONE";}}
static void msg_sub(char *d){switch(U.msg_k){case MK_PAT:sprintf(d,"PATTERN %c%02d",'A'+U.msg_n/NPAT,U.msg_n%NPAT+1);break;case MK_BANK:sprintf(d,"BANK %c",'A'+U.msg_n);break;
  case MK_PRS:sprintf(d,"PRESET A%02d",U.msg_n+1);break;case MK_FULL:strcpy(d,"NO SPACE IN BANK");break;case MK_ERR:strcpy(d,"FLASH WRITE FAILED");break;default:strcpy(d,"COMPLETE");}}
static void msg_box(int *x,int *y,int *w,int *h){
  char sub[24];msg_sub(sub);
  int ww=tw(msg_title(),1)+12,sw=tw(sub,1)+12;if(sw>ww)ww=sw;if(ww>230)ww=230;
  *w=ww;*h=30;*x=(LCD_W-ww)/2;*y=96;
}
static void msg_draw(void){
  if(U.msg<=0)return;
  int x,y,w,h;msg_box(&x,&y,&w,&h);char sub[24];msg_sub(sub);
  box0(x,y,w,h);rect(x,y,w,h,0);rect(x+1,y+1,w-2,10,1);
  text(x+3,y+2,msg_title(),1,0,1);text(x+4,y+15,sub,1,0,0);
}

/* ---- LIVE: экраны ---- */
static void ltxt(int x,int y,const char *s,int n,int al,int inv){char t[24];int k=0;while(s[k]&&k<n&&k<23){t[k]=s[k];k++;}t[k]=0;text(x,y,t,1,al,inv);}
static const char *LMN[NLM]={"SEL","ALG","K1","K2","K3","K4"};
static void lm_cellval(const lm_set_t *S,int i,float *nm,char *v){int val,mn,mx;*nm=0;v[0]=0;
 if(worm_lm_get(S->id[i],S->trk[i],&val,&mn,&mx)){*nm=mx>mn?(float)(val-mn)/(float)(mx-mn):0;worm_lm_fmt(S->id[i],val,v);}else strcpy(v,"-");}
static int lm_chain_bars(void){static const uint8_t cb[9]={0,0,0,0,1,2,4,8,16};int c=worm_lm_chain;if(c==0)return 0;if(c<=3)return worm_lm_pass_bars()*(c==1?1:c==2?2:4);return cb[c];}
static void live_draw(void){char b[40],v[24];const lm_set_t *S=worm_lm_active();
 for(int i=0;i<NLM;i++){int cx=2+(i%3)*79,cy=27+(i/3)*50;float nm;lm_cellval(S,i,&nm,v);
  rect(cx,cy,77,48,0);rect(cx,cy,27,10,1);text(cx+2,cy+1,LMN[i],1,0,1);
  if(S->id[i]>=0&&S->id[i]<NPALL&&S->trk[i]>=0&&worm_param_is_track(S->id[i])||S->trk[i]>=NTRK){worm_lm_trk_name(S->trk[i],b);text(cx+75,cy+1,b,1,2,0);}
  ltxt(cx+3,cy+14,worm_lm_name(S->id[i]),9,0,0);
  rect(cx+3,cy+26,71,7,0);{int w=(int)(nm*69.f);if(w>0)rect(cx+4,cy+27,w,5,1);}
  text(cx+38,cy+37,v,1,1,0);}
 /* активность дорожек теперь на клавишах мьютов, на экране её не дублируем */
  sprintf(b,"PT%c%02d",'A'+W.bank,W.slot+1);text(2,140,b,2,0,0);
  if(lm_pend>=0){sprintf(b,">%02d",lm_pend+1);text(86,144,b,1,0,0);}
  {int bi=worm_beat_idx();for(int q=0;q<4;q++){int x=128+q*27;rect(x,138,24,24,0);rect(x+1,139,22,22,W.run&&q==bi);}}   /* счётчик BAR не показываем, только шагающие доли */
  {unsigned h=worm_lfx_held();int n=0;b[0]=0;for(int f=0;f<LFX_N&&n<26;f++)if(h&(1u<<f)){int l=(int)strlen(worm_lfx_name(f));if(n+l+1<26){if(n)b[n++]=' ';strcpy(b+n,worm_lfx_name(f));n+=l;}}
   if(n)text(2,184,b,1,0,0);}   /* эффекты не нажаты — строку не рисуем */
  if(lm_chan)text(120,210,"HOME: KEYS = MUTE T1-6 / M",1,1,1);}
static void lmac_draw(void){char b[24],v[24];const lm_set_t *S=worm_lm_active();
 {const char *sc=worm_lm_own?"PTN":"BANK";int w=tw(sc,1)+4;rect(238-w,12,w,10,1);text(236,14,sc,1,2,1);}
 for(int i=0;i<NLM;i++){int y=26+i*20,sl=i==lm_sel;float nm;lm_cellval(S,i,&nm,v);
  rect(3,y,234,18,sl);text(8,y+5,LMN[i],1,0,sl);ltxt(40,y+5,worm_lm_name(S->id[i]),14,0,sl);
  if(S->id[i]>=0&&S->id[i]<NPALL&&worm_param_is_track(S->id[i])||S->trk[i]>=NTRK||(S->id[i]>=P_MVL&&S->id[i]<NPALL)){worm_lm_trk_name(S->trk[i],b);text(156,y+5,b,1,0,sl);}
  ltxt(232,y+5,v,5,2,sl);}
 text(2,150,"TURN ENC: PICK PARAM",1,0,0);text(2,162,"KEYS 1-6: SELECT SLOT",1,0,0);text(2,174,"OCT-/+: TARGET TRACK",1,0,0);
 text(2,186,"PRESET: BANK / PATTERN",1,0,0);text(2,198,"KEY 16: CLEAR SLOT",1,0,0);text(2,210,"EDIT: DONE  SAVE: STORE PTN",1,0,0);}
static void lseq_draw(void){const char *L[3]={"SWITCH TIME","CHAIN","ORDER"},*V[3];V[0]=worm_lm_sw_names[worm_lm_sw];V[1]=worm_lm_chain_names[worm_lm_chain];V[2]=worm_lm_order_names[worm_lm_order];
 for(int i=0;i<3;i++){int y=30+i*30;text(8,y,L[i],1,0,0);rect(8,y+11,224,13,1);text(120,y+14,V[i],1,1,1);}
 text(2,130,"SELECT/K1: SWITCH TIME",1,0,0);text(2,142,"ALGO/K2: CHAIN",1,0,0);text(2,154,"K3: ORDER",1,0,0);
 text(2,174,"MANUAL PATTERN CHANGE WAITS",1,0,0);text(2,186,"FOR THE SWITCH TIME; CHAIN",1,0,0);text(2,198,"STEPS TO THE NEXT PATTERN",1,0,0);text(2,210,"SEQ: BACK",1,0,0);}
static void screen(void){char b[40];
 {int hx=0,hw;   /* плашки шапки: ширина по тексту (+2 px с каждой стороны), промежуток 2 px — раньше BPM обрезался */
  if(W.mid)sprintf(b,"M%d",W.mpat+1);else sprintf(b,"T%d",U.chan+1);hw=tw(b,1)+4;rect(hx,0,hw,11,1);text(hx+2,2,b,1,0,1);hx+=hw+2;
  sprintf(b,"PT%c%02d",'A'+W.bank,W.slot+1);hw=tw(b,1)+4;rect(hx,0,hw,11,1);text(hx+2,2,b,1,0,1);hx+=hw+2;
  {int bb=(int)(W.bpm*2.f+0.5f);sprintf(b,"%d.%d",bb/2,(bb%2)*5);hw=tw(b,1)+4;rect(hx,0,hw,11,1);text(hx+2,2,b,1,0,1);}
  page_strip(hx+hw+2);}
 /* индикатор батареи/питания (worm_battery: 0..3 заряд, 4 USB, -1 не читается) */
 {int bx=212;rect(bx,1,22,9,0);rect(bx+22,3,2,5,1);        /* корпус батареи + носик */
  if(worm_battery==4){text(bx+2,2,"USB",1,0,1);}           /* питание от USB-хоста */
  else if(worm_battery>=0){for(int q=0;q<worm_battery;q++)rect(bx+3+q*4,3,3,5,1);}  /* заполненность */
  else {text(bx+2,2,"?",1,0,1);}                           /* не читается */
  if(worm_charging){rect(bx+2,3,4,5,1);rect(bx+9,3,4,5,1);rect(bx+16,3,4,5,1);}}  /* мигает при зарядке */
 static const char *T[]={"NOTE TRIG","OSC","AMP","FLT","LFO","ENV","FX","PATTERN BANK","PATTERN","SEQ PAGE","GRID EDIT","SEQ PLAYBACK","MICROTIMING","SCALE","BPM / CLICK","TRACKS","PRESETS","ALGORITHM","SAVE","ARPEGGIO","EUCLID","LIVE","SETTINGS","MIDI","CC","GRID SIZE","MACROS","LIVE SEQ"};
 static const char *TBN[]={"1","2","3","4","5","6","7"},*TBL[]={"L1","L2","L3","L4"},*TBF[]={"F1","F2","F3","F4"};
 int nt=0,act=U.tab;const char **tb=TBN;switch(U.scr){case S_NOTE:nt=2;break;case S_MID:case S_MCC:nt=2;break;case S_ARP:nt=W.mid?0:2;break;case S_ENV:nt=3;act=U.env;break;case S_AMP:nt=3;break;case S_FLT:nt=4;break;case S_OSC:nt=osc_nt();break;case S_LFO:nt=NLFO;tb=TBL;act=U.lfo;break;case S_FX:nt=3;tb=TBF;break;case S_SET:{static const char *TBS[]={"AU","MI","SP","USB","CO"};nt=5;tb=TBS;break;}}
 const char *base=T[U.scr];static const char *EN[]={"OSC1 ENV","OSC2 ENV","FREE ENV"},*FN[]={"FX DELAY","FX REVERB","FX CHORUS"},*FT[]={"FLT1","FLT2","FLT1 ENV","FLT2 ENV"},*AT[]={"AMP","AMP SEND","TRANSIENT"};
 if(U.scr==S_SET)base=U.tab==4?"COLOR":U.tab==3?"USB":U.tab==2?"SPEAKER":U.tab?"MIDI":"AUDIO";else if(U.scr==S_ENV)base=EN[U.env%3];else if(U.scr==S_FX)base=FN[U.tab%3];else if(U.scr==S_FLT)base=FT[U.tab&3];else if(U.scr==S_AMP)base=AT[U.tab%3];else if(U.scr==S_OSC){if(W.p[P_OA]==ALGO_FM){static const char *const PN[7]={"FM ALGO","FM HARM","FM OFFS","FM ENV A","FM ENV B","FM TRG A","FM TRG B"};base=PN[U.tab%7];}else{sprintf(b,"%s %s",U.tab>=2?"OSC2":"OSC1",L_ALGO[W.p[P_OA]]);base=b;}}
 if(W.mid){if(U.scr==S_MID)base=U.tab?"MIDI MIXER":"MIDI OUT";else if(U.scr==S_MCC)base=U.tab?"CC NUMBERS":"CC VALUES";}
 char t[40];strcpy(t,base);if(U.scr==S_LFO)sprintf(t,W.mid?"CC LFO%d":"LFO%d",U.lfo+1);if(nt>1&&U.scr!=S_VOX&&U.scr!=S_FX&&U.scr!=S_LFO&&U.scr!=S_SET&&!(U.scr==S_OSC&&W.p[P_OA]==ALGO_FM))sprintf(t+strlen(t)," %d/%d",U.tab+1,nt);
 if(U.scr==S_SPG||U.scr==S_GRID)sprintf(t,"%s %d/%d",base,vp()+1,W.p[P_GPG]);if(U.scr==S_PTN)sprintf(t,"PATTERN %c%02d",'A'+W.bank,W.slot+1);if(U.scr==S_PRE&&U.pre_step)strcpy(t,"STEP PRESET");if(mt_view())sprintf(t,"MICROTIMING %d/%d",vp()+1,W.p[P_GPG]);
 text(2,13,t,1,0,0);for(int i=0;i<nt;i++){int x=238-(nt-i)*20+1;rect(x,13,19,10,i==act);text(x+9,14,tb[i],1,1,i==act);}
 line(tw(t,1)+6,19,238-nt*20-1,19,1);
 if(U.scr==S_BANK)grid16(0);else if(U.scr==S_PTN)grid16(1);
 else if(U.scr==S_PRE){int a=U.pshow-5;if(a<0)a=0;if(a>NPRESET-11)a=NPRESET-11;for(int i=0;i<11;i++){int k=a+i,y=28+i*10,s=k==U.pshow;if(s)rect(3,y-1,234,10,1);sprintf(b,"A%02d %s",k+1,worm_preset_name(k));text(6,y,b,1,0,s);}}
 else if(U.scr==S_ALG){const int NALG=NALGO_ON,vis=NALGO_ON<7?NALGO_ON:7;int cur=(U.confirm?U.pending_oa:W.p[P_OA]),sel=0;   /* только живые движки (WT/HIVE/CHIP/TOY удалены) */
  for(int k=0;k<NALG;k++)if(worm_algo_list[k]==cur)sel=k;
  int top=sel-vis/2;if(top>NALG-vis)top=NALG-vis;if(top<0)top=0;
  for(int i=0;i<vis;i++){int k=top+i,y=26+i*28,s=k==sel;
   rect(3,y,222,24,s);sprintf(b,"%d",k+1);text(12,y+4,b,2,0,s);text(52,y+8,worm_algo_names[worm_algo_list[k]],1,0,s);}
  if(NALG>vis){int trH=vis*28-4,thumbH=trH*vis/NALG,ty=26+(trH-thumbH)*top/(NALG-vis);
  rect(228,26,6,trH,0);rect(228,ty,6,thumbH,1);}   /* полоса прокрутки */
  text(120,230,"OCT+ APPLY   OCT- CANCEL",1,1,0);}
 else if(U.scr==S_LIVE)live_draw();else if(U.scr==S_LMAC)lmac_draw();else if(U.scr==S_LSEQ)lseq_draw();
 else if(U.scr==S_SAV){rect(30,50,180,100,0);rect(30,50,180,13,1);sprintf(b,"SAVE %s?",U.sav_bank?"BANK":"PATTERN");text(36,53,b,1,0,1);
  if(U.sav_bank)sprintf(b,"BK%c",'A'+W.bank);else sprintf(b,"PT%c%02d",'A'+W.bank,W.slot+1);text(120,80,b,2,1,0);rect(52,118,50,18,!U.sav_yes);text(77,123,"NO",1,1,!U.sav_yes);rect(138,118,50,18,U.sav_yes);text(163,123,"YES",1,1,U.sav_yes);}
 else{rect(3,27,234,112,0);panel();}
 int w[4];widgets(w);if(!mt_view()&&U.scr!=S_LIVE&&U.scr!=S_LMAC&&U.scr!=S_LSEQ&&(U.scr<S_BANK||(U.scr>=S_SPG&&U.scr!=S_PRE&&U.scr!=S_ALG&&U.scr!=S_SAV&&U.scr!=S_SET&&U.scr!=S_MT)))for(int i=0;i<4;i++)wdraw(i,w[i]);
 if(U.scr!=S_SAV&&U.scr!=S_PRE&&U.scr!=S_ALG&&U.scr!=S_MT&&U.scr!=S_LIVE&&U.scr!=S_LMAC&&U.scr!=S_LSEQ&&!mt_view())for(int i=0;i<4;i++){int cx=32+(int)(i*58.5f);rect(cx-8,222,16,10,1);sprintf(b,"K%d",i+1);text(cx,223,b,1,1,1);}
 /* DEST выпадающий список: полноэкранное меню поверх всего */
 if(U.dest_show){
  memset(F,0,240*240);dest_order_init();
  { char t[24];if(U.dest_lfo>=0)sprintf(t,"LFO%d DEST",U.dest_lfo+1);else strcpy(t,"ENV4 DEST");rect(0,0,240,20,1);text(6,6,t,1,0,1);
  /* строки списка: заголовок группы (255) / пункт; слева — группа и имя выбранного, справа — список со скроллбаром (как в Digitone) */
  uint8_t ri[DEST_VMAX*2];uint8_t rg[DEST_VMAX*2];int nr=0,sr=0,sv=dest_a2v(U.dest_sel),last=-1;
  for(int i=0;i<dest_vn;i++){if(dest_gr[i]&&dest_gr[i]!=last){ri[nr]=255;rg[nr++]=dest_gr[i];}last=dest_gr[i];if(i==sv)sr=nr;ri[nr]=(uint8_t)i;rg[nr++]=dest_gr[i];}
  const int vis=11;int lo=sr-vis/2;if(lo>nr-vis)lo=nr-vis;if(lo<0)lo=0;
  for(int y=24;y<240;y+=3)rect(76,y,1,1,1);   /* пунктир-разделитель */
  text(6,32,worm_dest_grp(dest_gr[sv]),1,0,0);text(6,46,"DEST",1,0,0);
  for(int i=0;i<vis&&lo+i<nr;i++){int r=lo+i,y=26+i*19;
   if(ri[r]==255){text(82,y+3,worm_dest_grp(rg[r]),1,0,0);line(82,y+14,226,y+14,1);continue;}
   int s_=ri[r]==sv;if(s_)rect(80,y,150,16,1);text(86,y+4,worm_dest_item(dest_vo[ri[r]],W.p[P_OA]),1,0,s_);}
  { int bh=216*vis/(nr>vis?nr:vis),by=24+(nr>vis?(216-bh)*lo/(nr-vis):0);rect(236,24,1,216,1);rect(234,by,5,bh,1);}
  }
 }
 /* LFO MAP: баннер + занятые LFO */
 if(U.map){rect(0,0,240,20,1);sprintf(b,"LFO MAP  LFO%d",U.map_lfo+1);text(6,5,b,1,0,1);
  sprintf(b,"USED:");for(int l=0;l<NLFO;l++){int t=worm_dest_param(W.p[P_L0D+l*9]);if(t>0){int n=(int)strlen(b);sprintf(b+n," %d",l+1);}}
  text(6,32,b,1,0,0);text(6,48,"KEYS: PICK LFO",1,0,0);text(6,60,"KNOB: BIND PARAM",1,0,0);
  if(U.map_di>0){sprintf(b,"LFO%d > %s",U.map_lfo+1,worm_dest_name(U.map_di,W.p[P_OA]));text(6,80,b,1,0,0);}
  else if(U.map_di<0)text(6,80,"CANNOT MAP THIS",1,0,0);}
 msg_draw();if(U.msg>0)U.msg--;
}
void worm_ui_render(uint8_t *fb){F=fb;memset(fb,0,LCD_W*LCD_H);screen();}

#define KEY_MONO 20   /* C#5 (нота 73): подпись MONO на корпусе */
#define KEY_POLY 22   /* D#5 (нота 75): подпись POLY на корпусе */
static int krole(int i);   /* forward: объявлена ниже */
/* MUTE MIDI-дорожек при зажатой SEL: D#5 / C#5 / A#4 / G#4 = M1..M4 (ноты 75/73/70/68 без сдвига октавы) */
static int mmute_key(int i){switch(53+i){case 75:return 0;case 73:return 1;case 70:return 2;case 68:return 3;}return -1;}
/* MUTE звуковых дорожек 5 и 6: D#4 / F#4 (ноты 63 / 66) — продолжают ряд OP1-OP4 (F#3,G#3,A#3,C#4): слева направо чёрные клавиши = T1..T6 */
static int tmute_key(int i){switch(53+i){case 63:return 4;case 66:return 5;}return -1;}
static void to(int s);     /* forward: объявлена ниже (нужна в worm_ui_lights) */
#define PRE_MS 2000u    /* окно выбора пресета: показать 2 с и вернуться на прошлое меню */
#define PICK_MS 10000u   /* выбор паттерна/банка: возврат на последнее меню через 10 с без действий */
static void open_pick(int s);   /* открыть выбор паттерна/банка (возврат на прошлое меню через 10 с бездействия) */

/* Подсветка LED: какие кнопки/клавиши светить. Шаги 1-16 = белые клавиши F3..G5.
 * Возвращает маски в битах plat.h (кнопки 0..13, клавиши 0..26). */
static void mt_rep_tick(void);
void worm_ui_lights(uint32_t *btns, uint32_t *keys, uint32_t *dim_btns, uint32_t *dim_keys, uint32_t *breath_btns, uint32_t *breath_keys)
{
    uint32_t b = 0, k = 0, db = 0, dk = 0, bb = 0, bk = 0;
    U.frame++;                                         /* счётчик кадров (мигание ДА) */
    /* hold_op: кнопка/OP-клавиша удерживается > ~0.5 с -> подсветить страницы */
    if (U.hold_op_t >= 0) { if (++U.hold_op_t > 30) U.hold_op = 1; }
    /* Trig Condition (REC+EDIT): авто-возврат на OP1 через ~10 c бездействия */
    if (U.tc && (worm_ui_now - U.tc_t0) > 10000u) { U.tc = 0; U.tab = 0; }
    /* DEST выпадающий список: авто-подтверждение через ~1 c после последнего кручения */
    if (U.dest_show && (worm_ui_now - U.dest_t0) > 1000u) {
        int did = U.dest_lfo >= 0 ? P_L0D + U.dest_lfo * 9 : P_E4M;   /* LFO DEST или E4 свободного ADSR */
        W.p[did] = (int16_t)U.dest_sel;
        W.tr[W.pat].p[did] = (int16_t)U.dest_sel;
        U.dest_show = 0; worm_full_redraw = 1;
    }
    /* LFO MAP: удержание кнопки LFO ~0.5 c -> режим маппинга ПРЯМО В ТЕКУЩЕМ МЕНЮ (экран не переключается):
     * белые клавиши 1-8 выбирают LFO, ручка привязывает его к своему параметру. Короткое нажатие — меню LFO. */
    if (U.lfo_hold_t && U.scr != S_LFO && !W.mid && (worm_ui_now + 1u - U.lfo_hold_t) > 500u) { U.map = 1; U.map_lfo = U.lfo; U.map_di = 0; U.lfo_hold_t = 0; worm_full_redraw = 1; }
    /* EDIT: короткое нажатие -> паттерны (S_PTN); удержание 2 c -> банки (S_BANK); выбор — белые клавиши секвенсора, как страницы */
    mt_rep_tick();
    if (U.edit_hold_t && (worm_ui_now + 1u - U.edit_hold_t) > 2000u) { U.edit_hold_t = 0; U.edit_long = 1; open_pick(S_BANK); }   /* удержание EDIT 2 с -> банки паттернов */
    /* SAVE: удержание 2 с в любом режиме — сохранить звук текущей дорожки в выбранный пресет; только pop-up, без диалога */
    if (U.save_t0 && !(U.hb&(1u<<8)) && (worm_ui_now + 1u - U.save_t0) > 2000u) {   /* HOME+SAVE 2 с — режим обновления (app.c), пресет не пишем */
        U.save_t0=0;U.save_long=1;U.confirm=0;
        if(U.scr==S_SAV)to(U.prev);
        say_save(MK_PRS,U.pre,120,worm_preset_save_cur(U.pre));worm_full_redraw=1;
    }
    pat_apply_due();
    if (lm_on) { lm_tick(); if (worm_lm_dirty && !W.run) worm_store_lm_save(); }
    if (U.scr == S_MT && U.hold < 0 && U.hold_k < 0) { U.scr = U.mt_prev; U.tab = U.mt_tab; worm_full_redraw = 1; }   /* MICROTIMING: окно живёт, пока зажат триггер */
    if (U.scr == S_PRE && (worm_ui_now - U.pre_t0) > PRE_MS) { U.scr = U.pre_prev; U.tab = U.pre_tab; U.pre_step = 0; worm_full_redraw = 1; }   /* 2 с после последнего кручения -> назад на последнее меню (вкладку сохраняем) */
    if ((U.scr == S_PTN || U.scr == S_BANK) && (worm_ui_now - U.pick_t0) > PICK_MS) { to(U.pick_prev); worm_full_redraw = 1; }   /* 10 с без выбора -> назад на последнее меню */
    /* кнопки в plat-битах (B_*): B_PLAY=10, B_REC=11, B_OCTDN=12, B_OCTUP=13 */
    if (U.rec) b |= 1u << 11;                          /* REC активен */
    if (W.run) b |= 1u << 10;                          /* PLAY */
    if (U.confirm) {
        /* диалог ДА/НЕТ: OCT+ (ДА) дышит, OCT- (НЕТ) просто горит */
        b |= 1u << 12;                                 /* OCT- = НЕТ: подсвечен */
        bb |= 1u << 13;                                /* OCT+ = ДА: дышит */
    } else {
        if (U.oct < 0) bb |= 1u << 12;                  /* OCT- дышит: октава ниже 0 */
        if (U.oct > 0) bb |= 1u << 13;                  /* OCT+ дышит: октава выше 0 */
    }
    /* кнопки меню: FX SEL ENV LFO EDIT GLO SAVE ARP SEQ REC — тускло всегда,
     * ярко только активная (в её меню). HOME — максимальная подсветка (всегда ярко). */
    b |= 1u << 6;                                      /* HOME (SHIFT) всегда ярко */
    db |= 1u << 11;                                    /* REC тускло (ярко — выше, при U.rec) */
    {
        static const uint8_t menu_btn[9] = {0,1,2,3,4,5,7,8,9};  /* B_FX..B_SEQ без HOME(6) */
        int active = -1;   /* id кнопки (plat.h): FX0 SEL1 ENV2 LFO3 EDIT4 GLO5 SAVE7 ARP8 SEQ9 */
        switch(U.scr){case S_FX:active=0;break;case S_VOX:active=1;break;case S_ENV:active=2;break;case S_LFO:active=3;break;
                      case S_BANK:case S_PTN:active=4;break;case S_SAV:active=7;break;case S_ARP:active=8;break;case S_SET:active=5;break;case S_SPG:active=9;break;case S_EUCL:active=9;break;case S_GRIDSZ:active=9;break;}
        for (int i = 0; i < 9; i++) {
            if (menu_btn[i] == active) b |= 1u << menu_btn[i];   /* активное меню горит ярко (раньше сравнивался индекс: ARP светил SEQ) */
            else db |= 1u << menu_btn[i];              /* остальные тускло */
        }
    }
    /* чёрные клавиши OP1-OP4 = меню: подсветить открытое меню (индекс в krole). FX/LFO открываются кнопками. */
    if (U.map) {                                       /* LFO MAP: белые клавиши 1-8 = LFO, выбранный дышит */
        for (int i = 0; i < 27; i++) { int m = krole(i);
            if (m >= 0 && m < NLFO) { if (m == U.map_lfo) bk |= 1u << i; else dk |= 1u << i; } }
    } else if (U.hb & (1u << 3)) {                     /* зажатая SEL: выбор дорожки — белые клавиши 0..3; заглушенный трек гаснет */
        for (int i = 0; i < 27; i++) { int m = krole(i);
            if (m >= 0 && m < NTRK) { if (worm_trk_muted(m)) {} else if (!W.mid && m == U.chan) bk |= 1u << i; else dk |= 1u << i; }
            if (m >= 12 && m <= 15) { int q = 15 - m; if (worm_mtrk_muted(q)) {} else if (W.mid && q == W.mpat) bk |= 1u << i; else dk |= 1u << i; }   /* MIDI-дорожки M1..M4 = клавиши шагов 16..13 */
            if (m >= 16 && m <= 19) { int mu = (U.hb & (1u << 8)) ? worm_mtrk_muted(m - 16) : worm_trk_muted(m - 16); if (mu) dk |= 1u << i; else k |= 1u << i; }
            { int q = mmute_key(i); if (q >= 0) { if (worm_mtrk_muted(q)) dk |= 1u << i; else k |= 1u << i; } }   /* MUTE MIDI M1..M4: звучит — ярко, заглушена — тускло */
    { int q = tmute_key(i); if (q >= 0 && !(U.hb & (1u << 8))) { if (worm_trk_muted(q)) dk |= 1u << i; else k |= 1u << i; } } }   /* MUTE T5/T6 (D#4 / F#4) */   /* с HOME — MUTE MIDI-дорожек */   /* чёрные = MUTE каналов: звучит — ярко, заглушен — тускло */
    } else
    {
        static const int op_map[4] = {1,3,5,8};   /* F#3,G#3,A#3,C#4 = OP1..OP4 */
        if (U.scr == S_BANK) {                       /* банки: белые клавиши 0..7; активный дышит, остальные тускло (как страницы) */
            for (int i = 0; i < 27; i++) { int m = krole(i);
                if (m >= 0 && m < NBANK) { if (m == W.bank) bk |= 1u << i; else dk |= 1u << i; } }
        } else if (U.scr == S_PTN) {                 /* паттерны: белые клавиши 0..15; паттерн текущей дорожки дышит */
            for (int i = 0; i < 27; i++) { int m = krole(i);
                if (m >= 0 && m < NPAT) { if (m == W.slot) bk |= 1u << i; else if (m == pend_pat) k |= 1u << i; else dk |= 1u << i; } }
        } else if (U.scr == S_VOX) {                 /* VOICES: OP1-4 = MUTE треков (включённый горит ярко, заглушенный гаснет) */
            for (int j = 0; j < 4; j++) { int mu = (U.hb & (1u << 8)) ? worm_mtrk_muted(j) : worm_trk_muted(j); if (mu) dk |= 1u << op_map[j]; else k |= 1u << op_map[j]; }
        } else {
        int cur = -1;
        switch(U.scr){case S_NOTE:cur=0;break;case S_OSC:cur=1;break;case S_AMP:cur=2;break;
                      case S_FLT:cur=3;break;}
        if (!U.hold_op) {
            if (U.hb & (1u << 8)) {                        /* HOME: подсветить все OP1-4 */
                for (int j = 0; j < 4; j++) k |= 1u << op_map[j];
            } else if (U.rec && cur >= 0) {                /* REC: только открытое меню */
                k |= 1u << op_map[cur];
            }
        }
        /* при удержании OP: страницы на белых клавишах — активная «дышит», неактивные тускло */
        if (U.hold_op) {
            int ntab = (U.scr == S_LFO) ? NLFO : (U.scr == S_OSC) ? osc_nt() : (U.scr == S_FLT) ? 4 : (U.scr == S_SET) ? 5 : (U.scr == S_AMP || U.scr == S_FX) ? 3 : 2;
            int act = U.scr == S_LFO ? U.lfo : U.scr == S_ENV ? U.env : U.tab;
            if (U.scr == S_ARP) ntab = W.mid ? 1 : 2;
            if (U.scr == S_SPG) { ntab = W.p[P_GPG]; act = vp(); }   /* SEQ удержан: страницы секвенсора, активная дышит */
            for (int i = 0; i < 27; i++) {
                int m = krole(i);
                if (m < 0 || m >= ntab) continue;
                if (m == act) bk |= 1u << i;           /* активная дышит */
                else dk |= 1u << i;                    /* неактивные тускло */
            }
        } else if (U.rec) {
            /* STEP SEQUENCER: белые клавиши = шаги + бегунок */
            int v0 = vp() * 16;
            for (int i = 0; i < 27; i++) {
                int m = krole(i);
                if (m < 0 || m >= 16) continue;
                if (PT()->s[v0 + m].on) k |= 1u << i;
                if (W.run && W.cur == v0 + m) k |= 1u << i;
            }
            if (W.p[P_GPG] > 1) {                    /* MONO / POLY = страница назад / вперёд: горит, пока есть куда листать */
                if (vp() > 0) k |= 1u << KEY_MONO; else dk |= 1u << KEY_MONO;
                if (vp() < W.p[P_GPG] - 1) k |= 1u << KEY_POLY; else dk |= 1u << KEY_POLY;
            }
        } else {
            /* зажатые клавиши (играющие ноты) — дышат (пульсируют), а не произвольная клавиша от ручки */
            for (int i = 0; i < 27; i++) if (U.lv[i] >= 0) bk |= 1u << i;
        }
        }
    }
    if (lm_on) {   /* LIVE: подсветка */
        k = dk = bk = 0; b &= ~((1u << 11)); db &= ~((1u << 0) | (1u << 2) | (1u << 3) | (1u << 8) | (1u << 11));
        if (U.scr == S_LMAC) { b |= 1u << 4; db &= ~(1u << 4); } else if (U.scr == S_LSEQ) { b |= 1u << 9; db &= ~(1u << 9); }
        for (int i = 0; i < 27; i++) { int m = krole(i), fx = lm_fxkey(i);
            if (fx >= 0) { if (worm_lfx_held() & (1u << fx)) k |= 1u << i; else dk |= 1u << i; continue; }
            if (m < 0 || m > 15) continue;
            if (U.scr == S_LMAC) { if (m < NLM) { if (m == lm_sel) bk |= 1u << i; else dk |= 1u << i; } continue; }
            if (lm_chan) {   /* клавиши мьютов: вспышка на каждом реальном триггере секвенсора; заглушённый трек мигает тускло */
                if (m < NTRK || m >= 12) { int ti = m < NTRK ? m : NTRK + (15 - m), mu = m < NTRK ? worm_trk_muted(m) : worm_mtrk_muted(15 - m);
                    uint32_t at = worm_trig_at(ti);
                    if (at && (uint32_t)(worm_ui_now - at) < LM_TRIG_MS) { if (mu) dk |= 1u << i; else k |= 1u << i; }
                }
                continue;
            }
            { if (m == W.slot) bk |= 1u << i; else if (m == lm_pend) k |= 1u << i; } }   /* записанные паттерны не подсвечиваем: клавиша горит только по текущему/ожидающему */
    }
    *btns = b; *keys = k; *dim_btns = db; *dim_keys = dk; *breath_btns = bb; *breath_keys = bk;
}
void worm_ui_init(void){lm_on=0;lm_chan=0;lm_pend=-1;worm_lm_defaults(&worm_lm_bank);worm_lm_own=0;memset(&U,0,sizeof U);U.sel=-1;U.hold=-1;U.hold_k=-1;U.sav_yes=1;U.scr=S_NOTE;U.hold_op_t=-1;memset(U.lv,-1,sizeof U.lv);for(int k=0;k<4;k++)enc_rate_reset(k);}
/* --- ускорение K1..K4 на «точечных» параметрах --------------------------
 * Математика скорости из старых сборок (klast/krate): деления/сек, быстрый
 * разгон и плавный спад. Применяем только к спискам из enc_discrete(), чтобы
 * непрерывные параметры и SELECT/ALGO/PRESET крутились как раньше. Пауза,
 * смена направления или параметра — первый клик снова ровно 1 шаг. */
typedef struct { uint32_t t; int16_t id; int8_t dir; int16_t rate; uint8_t on; } enc_rate_t;
static enc_rate_t er[5];   /* K1..K4 + SELECT (громкость / BPM) */
static void enc_rate_reset(int k){memset(&er[k],0,sizeof er[k]);}

/* Разгон энкодера по скорости кручения — для ВСЕХ параметров K1..K4 (и SELECT: громкость, BPM).
 *  - медленно (до ~12 кликов/с) и первый клик после паузы/смены направления: ровно 1 шаг;
 *  - дальше множитель растёт плавно, по квадрату скорости: 24 кл/с ~x4, 36 ~x9, 60 ~x25, 100 ~x69;
 *  - потолок множителя зависит от числа значений параметра (rg): много значений (0..127, 0..255) —
 *    быстрое кручение пролетает диапазон за долю секунды; мало значений (до 16: списки, WAVE VA, OCT) — разгона нет. */
static int enc_rate(int k,int id,int d,int rg)
{
    enc_rate_t *e=&er[k];uint32_t now=worm_ui_now,dt;
    int dir=d>0?1:-1,mag=d>0?d:-d,inst,mul,cap,steps;
    if(mag<1)mag=1;
    if(!e->on||e->id!=id||e->dir!=dir||(uint32_t)(now-e->t)>150u){     /* пауза/смена: первый клик = ровно 1 шаг */
        enc_rate_reset(k);e->on=1;e->id=(int16_t)id;e->dir=(int8_t)dir;e->t=now;
        return dir;
    }
    dt=now-e->t;e->t=now;
    if(dt<1u)dt=1u;if(dt>300u)dt=300u;
    inst=(int)((uint32_t)mag*1000u/dt);if(inst>2000)inst=2000;           /* делений/сек */
    if(e->rate<=0||inst>e->rate)e->rate=(int16_t)inst;                  /* быстрый разгон */
    else e->rate=(int16_t)((e->rate*3+inst)>>2);                        /* плавный спад */
    mul=(int)((int32_t)e->rate*e->rate/144);if(mul<1)mul=1;             /* ~x1 до 12 кл/с, затем квадратично */
    cap=rg<=16?1:rg/10;if(cap<1)cap=1;if(cap>64)cap=64;                  /* мало значений — без разгона; много — до ~1/10 диапазона за клик */
    if(mul>cap)mul=cap;
    steps=mag*mul;if(steps>64)steps=64;
    return dir*steps;
}
static void to(int s){if(U.scr!=s){if(U.confirm==7)W.theme=(int8_t)col_old;if(U.confirm>=3)U.confirm=0;U.scr=s;U.tab=0;U.mt_home=0;}}   /* уход из GLO отменяет неподтверждённый выбор USB-выхода */
static void open_pick(int s){if(U.scr!=S_PTN&&U.scr!=S_BANK)U.pick_prev=U.scr;to(s);U.pick_t0=worm_ui_now;U.hold_op=0;U.hold_op_t=-1;worm_full_redraw=1;}
static void pick_touch(void){U.pick_t0=worm_ui_now;}   /* любое действие в выборе продлевает 10 с */
static int ntabs(void){switch(U.scr){case S_SPG:return W.p[P_GPG];case S_NOTE:return 2;case S_ENV:return 3;case S_AMP:return 3;case S_FX:return 3;case S_FLT:return 4;case S_OSC:return osc_nt();case S_LFO:return NLFO;case S_SET:return 5;case S_MID:case S_MCC:return 2;case S_ARP:return W.mid?1:2;}return 0;}
static void knob(int j,int d){int w[4];widgets(w);int id=w[j];if(id<0)return;const pinfo_t *pi=&worm_pinfo[id];int st=(id==P_GLN||id==P_GST||id==P_GMT||pi->fmt==F_LIST)?1:((pi->mx-pi->mn)/64>1?(pi->mx-pi->mn)/64:1);
 if(id>=P_L0D&&id<P_L0D+NLFO*9&&(id-P_L0D)%9==1&&val(id+4))st=19;
 if(id>=NP){if(pi->fmt==F_OFFN||pi->fmt==F_PGM||pi->mx-pi->mn<=64)st=1;if(id>=P_M0C&&id<P_M0C+NMLANE*7&&(id-P_M0C)%7==3&&val(id+2))st=19;}   /* MIDI: номера/каналы по 1, SYNC-деления по одному */   /* SYNC вкл: один клик = одно деление */
 /* LFO MAP: покрутить ручку — привязать выбранный LFO к параметру этой ручки */
 if(U.map){enc_rate_reset(j);int di=worm_dest_index(id);if(di>0){W.p[P_L0D+U.map_lfo*9]=(int16_t)di;W.tr[W.pat].p[P_L0D+U.map_lfo*9]=(int16_t)di;U.map_di=di;}else U.map_di=-1;worm_full_redraw=1;return;}   /* карта остаётся, пока LFO удержан */
 /* точечные параметры K1..K4: ускорение по скорости кручения (медленно = 1 клик = 1 значение) */
 {int rg=(pi->mx-pi->mn)/st;   /* число значений с учётом шага клика */
  if(id>=P_L0D&&id<P_L0D+NLFO*9&&(id-P_L0D)%9==0)rg=worm_dest_n;
  d=enc_rate(j,id,d,rg);if(!d)return;}
 /* DEST: кручение -> выпадающий список, авто-подтверждение через 1 с (LFO DEST или E4 свободного ADSR) */
 if(id==P_E4M||(id>=P_L0D&&id<P_L0D+NLFO*9&&(id-P_L0D)%9==0)){int li=(id==P_E4M)?-1:(id-P_L0D)/9;
  int base=dest_a2v(U.dest_show?U.dest_sel:val(id));   /* накапливать от выбранного, в порядке показа */
  int sd=d>0?1:-1,nv=base+d;while(nv>0&&nv<dest_vn&&worm_dest_param(dest_v2a(nv))==-2)nv+=sd;   /* пропустить заголовки */
  if(nv<0)nv=0;if(nv>=dest_vn)nv=dest_vn-1;
  if(worm_dest_param(dest_v2a(nv))==-2)nv=0;  /* не дать остановиться на заголовке */
  U.dest_lfo=li;U.dest_sel=dest_v2a(nv);U.dest_show=1;U.dest_t0=worm_ui_now;return;}
 setv(id,val(id)+d*st);}
void worm_ui_enc(int id,int d){if(U.tc)U.tc_t0=worm_ui_now;   /* кручение ручек тоже продлевает окно TRIG (раньше гасло через 10 с посреди настройки) */
 if(lm_on&&lm_enc(id,d))return;
 if(id>=2&&id<=5){knob(id-2,d);return;}
 if(id==0&&U.map){ /* SELECT в режиме LFO MAP: выбрать LFO (1..NLFO) */
  U.map_lfo+=d;if(U.map_lfo<0)U.map_lfo=0;if(U.map_lfo>NLFO-1)U.map_lfo=NLFO-1;U.lfo=U.map_lfo;return;}
 if(id==1&&W.mid){to(S_MID);setv(P_MCH,W.p[P_MCH]+d);return;}   /* MIDI-дорожка: ALGO = CHAN */
 if(id==6&&W.mid){to(S_MID);setv(P_MPG,W.p[P_MPG]+d);return;}   /* MIDI-дорожка: PRESET = PROG (Program Change) */
 if(id==1){ /* ALGO: меню списка + подтверждение (OCT+ применить / OCT- отмена) */
  if(U.scr!=S_ALG&&U.scr!=S_SAV)U.prev=U.scr;int cur=(U.confirm?U.pending_oa:W.p[P_OA]),ix=0;for(int k=0;k<NALGO_ON;k++)if(worm_algo_list[k]==cur)ix=k;
  ix+=d;if(ix<0)ix=0;if(ix>NALGO_ON-1)ix=NALGO_ON-1;int na=worm_algo_list[ix];   /* шаг по списку живых движков */
  U.pending_oa=na;U.confirm=1;to(S_ALG);return;}
 if(id==6){ /* PRESET: с зажатым триггером — пресет на шаг (p-lock P_PSTEP), без триггера — звук дорожки.
             * Работает и в STEP SEQUENCER (REC), и в обычном режиме: белая клавиша = шаг текущей страницы. */
   int q=U.pre,oldp;step_t *s=sel();
   if(!s&&!U.rec&&U.hold_k>=0&&U.hold_k<16)s=&PT()->s[vp()*16+U.hold_k];
   if(s&&worm_step_lock_get(s,P_PSTEP,&oldp))q=oldp;
   q+=d;if(q<0)q=0;if(q>=NPRESET)q=NPRESET-1;
   if(U.scr!=S_PRE){U.pre_prev=U.scr;U.pre_tab=U.tab;}   /* куда вернуться через 2 с */
   if(s){   /* p-lock: пишем ТОЛЬКО в шаг; звук дорожки (W.p, U.pre) не трогаем */
    worm_step_init(s);s->on=1;
    if(worm_step_lock_get(s,P_PSTEP,&oldp)||s->nl<MAXLK)worm_step_lock_set(s,P_PSTEP,q);
    else{s->lk[MAXLK-1].id=P_PSTEP;s->lk[MAXLK-1].v=(int8_t)q;}   /* 7 слотов заняты: пресет вытесняет последний lock */
    U.lk=1;U.pre_step=1;}
   else{U.pre=q;U.pre_step=0;worm_preset_load(W.p,q);worm_trk_sync();}
   U.pshow=q;U.pre_t0=worm_ui_now;if(U.scr!=S_PRE){U.scr=S_PRE;U.tab=0;}worm_full_redraw=1;return;}
 if(U.scr==S_SAV)U.sav_yes^=1;else if(U.scr==S_SET){if(U.tab==2){W.usb_local=d>0?1:0;}else if(U.tab==3){int m=(U.confirm==5?U.pending_usb:W.usb_mode)+(d>0?1:-1);if(m<0)m=0;if(m>1)m=1;U.pending_usb=m;U.confirm=(m==W.usb_mode)?0:5;}   /* USB-режим: SELECT выбирает, OCT+ применяет */else if(U.tab==4){if(U.confirm!=7)col_old=W.theme;int c=W.theme+d;if(c<0)c=0;if(c>3)c=3;W.theme=(int8_t)c;U.pending_col=c;U.confirm=(c==col_old)?0:7;worm_full_redraw=1;}   /* COLOR: SELECT выбирает схему, OCT+ применяет */else if(U.tab){int h=(U.confirm==4?U.pending_usb:W.usb_host)+(d>0?1:-1);if(h<0)h=0;if(h>1)h=1;U.pending_usb=h;U.confirm=(h==W.usb_host)?0:4;}   /* режим USB MIDI: выбор ручкой, применяется по OCT+ */else{int o=(U.confirm==3?U.pending_usb:W.usb_out)+d;if(o<0)o=0;if(o>3)o=3;U.pending_usb=o;U.confirm=(o==W.usb_out)?0:3;}}   /* USB-выход: выбор ручкой, применяется только по OCT+ (OCT- / возврат на текущий = отмена) */else if(U.scr==S_BANK){set_bank((W.bank+d+NBANK)&(NBANK-1));pick_touch();}else if(U.scr==S_PTN){int base=pend_pat>=0?pend_pat:W.slot;pat_request((base+d+NPAT)%NPAT);pick_touch();}
 else if(U.scr==S_MET){d=enc_rate(4,-1,d,520);W.bpm+=d*.5f;if(W.bpm<40)W.bpm=40;if(W.bpm>300)W.bpm=300;}else if(W.mid){int c=W.p[P_MVL];d=enc_rate(4,-2,d,64);setv(P_MVL,(c<0?100:c)+d*2);}else{d=enc_rate(4,-2,d,64);setv(P_C0L+U.chan,W.p[P_C0L+U.chan]+d*2);}}   /* MIDI-дорожка: SELECT = VOL (CC7) */
static void put(step_t *s,int n){s->on=1;worm_step_init(s);s->note=n;s->vel=100;}   /* наигранная нота: velocity реального нажатия, не наследует VEL из NOTE TRIG */

/* OP1-OP4 -> меню (страница синтеза). OP5/OP6 и PIT/GLO/MONO/POLY больше не открывают меню:
 * страницы переключаются удержанием OP + белая клавиша.
 * OP3 (AMP): повторное нажатие переключает AMP1 <-> AMP2 (SEND). */
static void op_menu(int j){static const int S[4]={S_NOTE,S_OSC,S_AMP,S_FLT},M[4]={S_NOTE,S_MID,S_MCC,S_EUCL};   /* MIDI-дорожка: OP1 NOTE, OP2 MIDI, OP3 CC, OP4 EUCLID */
 if(W.mid&&j<4){to(M[j]);return;}
 if(j<4){if(j==2&&U.scr==S_AMP){U.tab=(U.tab+1)%3;return;}to(S[j]);return;}}

/* --- модель секвенсора (по спецификации) ---
 * 1 голос = 1 секвенция. 2 режима:
 *   STEP SEQUENCER — REC включён: бегунок бегает, белые клавиши ставят/снимают триггеры
 *                    (всегда ставится текущая нота из меню OP1). Чёрные = OP1-OP6 (меню).
 *                    Отдельного экрана нет.
 *   Live Record    — REC + PLAY/STOP: запись налету.
 *   обычный режим  — REC выключен: клавиатура = ноты.
 */
static void seq_enter(void){U.rec=1;U.live=0;U.sel=-1;U.tc=0;to(S_NOTE);
 if(W.run)W.cur_run=1;            /* проигрывание идёт: просто включить бегунок, не сбрасывать позицию */
 else worm_cursor_run(1);}        /* STEP SEQUENCER: бегунок бегает, звука нет */
static void seq_exit(void){U.rec=0;U.live=0;U.tc=0;U.sel=-1;U.hold=-1;U.lk=0;worm_cursor_run(0);}  /* выходим из REC, но PLAY/STOP (W.run) не трогаем */
static void seq_toggle(void){if(U.rec)seq_exit();else seq_enter();}

/* индекс зажатого триггера в STEP SEQUENCER (U.hold); -1 если не зажат или REC выключен */
static int trig_idx(void){if(U.live&&W.run&&W.cur>=0)return -1;
 if(U.rec&&U.hold>=0&&U.sel>=0)return U.sel;
 return -1;}   /* микротайминг только в REC; вне REC OCT+/- меняют октаву как обычно */
/* микротайминг зажатого шага: сдвиг на d процентов шага (-100..100). Автоповтор при удержании OCT+/OCT-: сначала пауза, затем частота растёт, в конце шаг по 5%. */
#define MT_REP_DELAY 300u
static int mt_rep_dir,mt_rep_n;static uint32_t mt_rep_t;
static void mt_nudge(int d,int n){int ix=trig_idx();if(ix<0)return;step_t *s=&PT()->s[ix];int m=s->mt+d*n;if(m<-MT_MAX)m=-MT_MAX;if(m>MT_MAX)m=MT_MAX;
 worm_step_init(s);s->mt=(int8_t)m;U.lk=1;U.mt_i=ix;
 if(U.scr!=S_MT){U.mt_prev=U.scr;U.mt_tab=U.tab;U.scr=S_MT;U.tab=0;}
 worm_full_redraw=1;}
static void mt_rep_tick(void){if(!mt_rep_dir)return;
 if(!(U.hb&(1u<<(mt_rep_dir>0?1:0)))||trig_idx()<0){mt_rep_dir=0;return;}   /* кнопку или шаг отпустили */
 if((int32_t)(worm_ui_now-mt_rep_t)<0)return;
 mt_nudge(mt_rep_dir,mt_rep_n>=30?5:mt_rep_n>=14?2:1);mt_rep_n++;
 mt_rep_t=worm_ui_now+(mt_rep_n<4?80u:mt_rep_n<10?40u:20u);}   /* 80 -> 40 -> 20 мс; после 14 повторов шаг по 2%, после 30 — по 5% */
/* ---- LIVE режим: логика ---- */
static unsigned lm_rng=12345u;
static int lm_next(void){unsigned u=worm_store_bank_used();int o=worm_lm_order;
 if(o==2){int c=0,l[NPAT];for(int i=0;i<NPAT;i++)if(i!=W.slot&&(u&(1u<<i)))l[c++]=i;if(!c)return -1;lm_rng=lm_rng*1664525u+1013904223u;return l[(lm_rng>>16)%(unsigned)c];}
 for(int k=1;k<NPAT;k++){int m=(W.slot+k)&(NPAT-1);if(o==1||(u&(1u<<m)))return m;}return -1;}
static void lm_apply(int m){pat_do(m);lm_pend=-1;lm_age=0;}
static void lm_request(int m){if(m<0||m>=NPAT)return;if(m==W.slot){lm_pend=-1;return;}
 if(!W.run){lm_apply(m);return;}lm_pend=(int8_t)m;lm_pcnt=worm_lm_sw?1<<(worm_lm_sw-1):0;worm_store_prefetch(m);}
static void lm_tick(void){uint32_t b=worm_bar_count();int bi=worm_beat_idx();
 if(!W.run){lm_bar=b;lm_beat=-1;lm_age=0;if(lm_pend>=0)lm_apply(lm_pend);return;}
 int onbeat=bi>=0&&bi!=lm_beat,onbar=b!=lm_bar;lm_beat=bi;lm_bar=b;
 if(lm_pend>=0){if(worm_lm_sw==0){if(onbeat)lm_apply(lm_pend);}else if(onbar&&--lm_pcnt<=0)lm_apply(lm_pend);return;}
 if(onbar){int cb=lm_chain_bars();if(cb<=0){lm_age=0;return;}lm_age++;
  if(lm_age>=cb){int m=lm_next();if(m>=0)lm_apply(m);else lm_age=0;}else if(lm_age==cb-1){int m=lm_next();if(m>=0)worm_store_prefetch(m);}}}
static void lm_save_now(void){if(!worm_lm_dirty)return;if(!W.run)worm_store_lm_save();}   /* во время игры — позже (lights) */
static void lm_toggle(void){
 if(!lm_on){if(U.scr==S_SET){U.scr=U.set_prev;U.tab=U.set_tab;}
  lm_pscr=U.scr;lm_ptab=U.tab;if(U.rec)seq_exit();U.live=0;U.hold=-1;U.hold_k=-1;U.sel=-1;U.map=0;U.tc=0;U.confirm=0;U.dest_show=0;rel_live();
  lm_on=1;lm_sel=0;lm_chan=0;lm_pend=-1;lm_age=0;lm_bar=worm_bar_count();lm_beat=-1;
  pat_do(0);   /* LIVE всегда открывается с A01 текущего банка сразу, даже во время игры: последний паттерн проекта не должен оставаться активным */
  worm_lfx_arm(1);to(S_LIVE);}
 else{lm_save_now();lm_on=0;lm_chan=0;lm_pend=-1;worm_lfx_arm(0);worm_lfx_reset();U.scr=lm_pscr;U.tab=lm_ptab;worm_full_redraw=1;}
 worm_full_redraw=1;}
static void lm_btn(int b){   /* EDIT / SEQ в LIVE; остальные кнопки меню (FX, ENV, LFO, ARP, REC) не работают */
 if(b==6){if(U.scr==S_LMAC){lm_save_now();to(S_LIVE);}else to(S_LMAC);}
 else if(b==11){if(U.scr==S_LSEQ){lm_save_now();to(S_LIVE);}else to(S_LSEQ);worm_lm_dirty=1;}
 worm_full_redraw=1;}
static int lm_scope(int own){if(own&&!worm_lm_own){worm_lm_pat=worm_lm_bank;worm_lm_own=1;}else if(!own&&worm_lm_own)worm_lm_own=0;return 1;}
static int lm_enc(int id,int d){
 if(U.scr==S_LIVE){if(id<NLM){worm_lm_turn(id,d);return 1;}if(id==6){int base=lm_pend>=0?lm_pend:W.slot;lm_request((base+(d>0?1:-1)+NPAT)&(NPAT-1));return 1;}return 0;}
 if(U.scr==S_LMAC){lm_set_t *S=worm_lm_active();
  if(id<NLM){lm_sel=id;int n=worm_lm_ntargets(),ix=worm_lm_find(S->id[id]);d=worm_lm_accel(id,d);if(ix<0)ix=d>0?-1:n;ix+=d;if(ix<0)ix=0;if(ix>=n)ix=n-1;
   S->id[id]=(int16_t)worm_lm_target(ix);if(S->id[id]<NPALL&&S->id[id]>=0&&worm_param_is_track(S->id[id]))S->trk[id]=(int8_t)(W.mid?NTRK+W.mpat:W.pat);else if(S->id[id]>=P_MVL&&S->id[id]<NPALL)S->trk[id]=(int8_t)(W.mid?NTRK+W.mpat:NTRK);else S->trk[id]=-1;
   if(!worm_lm_own)worm_lm_dirty=1;return 1;}
  if(id==6){lm_scope(d>0);worm_lm_dirty=1;return 1;}return 1;}
 if(U.scr==S_LSEQ){
  if(id==0||id==2){int v=worm_lm_sw+(d>0?1:-1);if(v<0)v=0;if(v>4)v=4;worm_lm_sw=(int8_t)v;}
  else if(id==1||id==3){int v=worm_lm_chain+(d>0?1:-1);if(v<0)v=0;if(v>8)v=8;worm_lm_chain=(int8_t)v;lm_age=0;}
  else if(id==4){int v=worm_lm_order+(d>0?1:-1);if(v<0)v=0;if(v>2)v=2;worm_lm_order=(int8_t)v;}
  worm_lm_dirty=1;return 1;}
 return 0;}
static int lm_fxkey(int i){switch(53+i){case 54:return LFX_R16;case 56:return LFX_R32;case 58:return LFX_R8;case 61:return LFX_REV;case 63:return LFX_TAPE;case 66:return LFX_FRZ;case 68:return LFX_LPF;case 70:return LFX_HPF;case 73:return LFX_CRU;case 75:return LFX_DUCK;}return -1;}
static void lm_key(int i,int dn){int m=krole(i),fx=lm_fxkey(i);
 if(fx>=0){worm_lfx_key(fx,dn);return;}
 if(!dn||m<0||m>15)return;
 if(U.scr==S_LMAC){lm_set_t *S=worm_lm_active();if(m<NLM)lm_sel=m;else if(m==15){S->id[lm_sel]=-1;S->trk[lm_sel]=-1;if(!worm_lm_own)worm_lm_dirty=1;}return;}
 if(lm_chan){if(m<NTRK)worm_trk_mute(m,!worm_trk_muted(m));else if(m>=12)worm_mtrk_mute(15-m,!worm_mtrk_muted(15-m));return;}
 if(U.scr==S_LIVE||U.scr==S_LSEQ)lm_request(m);}
void worm_ui_button(int b,int dn){
 if(b==8){U.mt_home=dn&&U.rec&&U.scr==S_NOTE;if(U.scr==S_NOTE)worm_full_redraw=1;}   /* HOME в NOTE TRIG: вход/выход из обзора микротайминга */
 if(b>=0&&b<16){if(dn)U.hb|=1u<<b;else U.hb&=~(1u<<b);}
 if(!dn&&(b==0||b==1))mt_rep_dir=0;  /* held-биты (12=PLAY, 13=REC) */
 if(dn&&((b==7&&(U.hb&(1u<<8)))||(b==8&&(U.hb&(1u<<7))))){lm_toggle();return;}   /* HOME+GLO: вход/выход LIVE */
 if(lm_on){   /* LIVE: EDIT = редактор макросов, SEQ = время переключения; FX/ENV/LFO/ARP/REC не работают; OCT = банк (в редакторе — дорожка цели) */
  if(b==6||b==11){if(dn)lm_btn(b);return;}
  if(b==2||b==4||b==5||b==10){return;}
  if(b==13){U.hb&=~(1u<<13);return;}
  if(b==8){if(dn){lm_chan=!lm_chan;worm_full_redraw=1;}return;}   /* HOME: защёлка каналов/мьютов; повторное нажатие — обратно */
  if((b==0||b==1)&&dn&&!U.confirm){if(U.scr==S_LMAC){lm_set_t *S=worm_lm_active();int t=S->trk[lm_sel]+(b?1:-1);if(t<-1)t=NTRK+NMTRK-1;if(t>=NTRK+NMTRK)t=-1;S->trk[lm_sel]=(int8_t)t;if(!worm_lm_own)worm_lm_dirty=1;}
   else if(U.scr==S_LIVE){set_bank((W.bank+(b?1:-1)+NBANK)&(NBANK-1));lm_pend=-1;lm_age=0;}return;}}
 if(dn&&U.tc)U.tc_t0=worm_ui_now;                       /* нажатие продлевает Trig Condition */
 /* кнопки меню со страницами (FX/ENV/LFO): удержание -> hold_op (показать страницы на белых клавишах) */
 if(b==2||b==4||b==5||b==7||b==10||b==11){ if(!dn){U.hold_op=0;U.hold_op_t=-1;} }
 /* LFO: короткое нажатие — меню LFO (при отпускании); удержание ~0.5 c — LFO MAP в текущем меню (меню НЕ открывается) */
 if(b==5){ if(dn){U.lfo_hold_t=worm_ui_now+1u;}
  else{int tap=U.lfo_hold_t!=0&&!U.map;U.lfo_hold_t=0;if(U.map){U.map=0;worm_full_redraw=1;}else if(tap){if(U.scr==S_LFO)U.lfo=(U.lfo+1)&(NLFO-1);else to(S_LFO);}}   /* повторное нажатие в меню LFO — следующий LFO (1..8) */
  return; }
 if(b==6&&!dn){int t=U.edit_hold_t;U.edit_hold_t=0;if(t&&!U.edit_used&&!U.edit_long)open_pick(S_PTN);U.edit_long=0;return;}   /* короткое нажатие EDIT (< 2 с) -> выбор паттерна */
 if(b==3&&!dn){if(U.scr==S_VOX)to(U.sel_prev);U.sel_k=0;return;}   /* отпустили SEL — вернуться на предыдущее меню */
 if(!dn&&(b==12||b==13)){if(!((U.hb&(1u<<12))&&(U.hb&(1u<<13))))U.live=0;}   /* отпустили PLAY/REC: выйти из Live Record (иначе белые клавиши продолжают писать ноты) */
 if(b==9&&!dn){int lng=U.save_long;U.save_t0=0;U.save_long=0;   /* отпускание SAVE: короткое нажатие -> диалог (раньше эта ветка стояла ПОСЛЕ return для отпускания и не выполнялась никогда) */
  if(!lng){if(U.scr!=S_SAV&&U.scr!=S_ALG)U.prev=U.scr;U.sav_bank=U.save_bank;to(S_SAV);U.confirm=U.save_bank?6:2;}
  return;}
 if(!dn)return;  /* ниже — только нажатие */
 switch(b){
  case 12: /* PLAY/STOP — работает одинаково в любом режиме; HOME+PLAY — меню BPM */
   if(U.hb&(1u<<8)){if(lm_on&&U.scr==S_MET)to(S_LIVE);else to(S_MET);break;}
   if(U.hb&(1u<<13)){ /* PLAY нажат при зажатом REC -> Live Record (налету) */
    U.live=1; if(!U.rec){U.rec=1;worm_cursor_run(1);}
    if(!W.run){U.sel=-1;worm_play();}
   } else if(W.run){U.live=0;worm_stop();}          /* STOP: выйти из Live Record (иначе белые клавиши продолжают писать ноты) */
   else{U.sel=-1;worm_play();}           /* PLAY запускает воспроизведение */
   break;
  case 13: /* REC — вход/выход STEP SEQUENCER (выход = STOP) */
   if(U.hb&(1u<<12)){ /* REC нажат при зажатом PLAY -> Live Record (налету) */
    U.live=1; if(!U.rec){U.rec=1;worm_cursor_run(1);}
    if(!W.run){U.sel=-1;worm_play();}
   } else seq_toggle();
   break;
  case 11: /* SEQ — настройка секвенсора; SEQ при зажатом HOME — EUCLID */
   if(U.hb&(1u<<8))to(S_EUCL);else to(S_SPG);
   U.hold_op_t=0;break;
  case 2: /* FX — настройка эффектов (удержание: страницы) */
   if(W.mid)break;   /* у MIDI-дорожки нет звука */
   to(S_FX);U.hold_op_t=0;break;
  case 4: /* ENV — Envelope; повторное нажатие: OSC1 ENV <-> OSC2 ENV (ENV фильтров — на страницах FLT) */
   if(W.mid)break;
   if(U.scr==S_ENV)U.env=(U.env+1)%3;else {to(S_ENV);U.env=0;}
   U.hold_op_t=0;break;
  case 6: /* EDIT — в REC: Trig Condition; вне REC: банки паттернов (короткое нажатие). Удержание ~0.5 c -> паттерны (S_PTN). EDIT+GLO: очистить automation трека; EDIT+OCT-: очистить automation меню */
   U.edit_hold_t=worm_ui_now+1u;U.edit_used=0;U.edit_long=0;   /* начало удержания EDIT: отпустили раньше 2 с -> паттерны, 2 с -> банки */
   if(U.hb&(1u<<7)){worm_aut_clear_trk(W.pat);say(MK_DONE,0,80);U.edit_used=1;U.edit_hold_t=0;break;}   /* EDIT+GLO: удалить вращение всех ручек трека */
   if(U.hb&(1u<<0)){int w[4];widgets(w);for(int i=0;i<4;i++)if(w[i]>0)worm_aut_clear_param(W.pat,w[i]);say(MK_DONE,0,80);U.edit_used=1;U.edit_hold_t=0;break;}   /* EDIT+OCT-: удалить вращение ручек текущего меню */
   if(U.rec){if(U.tc&&U.scr==S_NOTE&&U.tab==0){U.tc=0;}else{U.tc=1;U.tc_t0=worm_ui_now;to(S_NOTE);U.tab=0;}worm_full_redraw=1;U.edit_used=1;U.edit_hold_t=0;break;}   /* EDIT: окно TRIG (RETRIG + COND) вкл/выкл */
   break;   /* паттерны/банки откроются при отпускании (короткое) или через 2 с удержания */
  case 7: /* GLO — глобальные настройки: AUDIO (Output) / MIDI (пусто); страницы — как у других меню (удержание) */
   if(U.hb&(1u<<6))break;   /* EDIT+GLO — очистка automation, не меню */
   if(U.scr!=S_SET){U.set_prev=U.scr;U.set_tab=U.tab;}to(S_SET);U.hold_op_t=0;break;
  case 10: /* ARP — настройка арпеджио (удержание: страницы; звуковой ARP: стр. 2 = ARP>MIDI) */
   to(S_ARP);U.hold_op_t=0;break;
  case 3: /* SEL: зажать — VOICES (возврат на предыдущее меню при отпускании); SEL+белая клавиша — выбор канала */
   if(U.scr!=S_VOX)U.sel_prev=U.scr;U.sel_k=0;to(S_VOX);break;
  case 8: /* HOME: в секвенсоре — переключить EUCLID (SEQ -> HOME) */
   if(U.scr==S_SPG)to(S_EUCL);else if(U.scr==S_EUCL)to(S_GRIDSZ);else if(U.scr==S_GRIDSZ)to(S_SPG);
   break;
  case 9: /* SAVE: короткое — паттерн; HOME+SAVE вне REC — банк; удержание 2 с — пресет (pop-up в worm_ui_lights) */
   if(dn){U.save_t0=worm_ui_now+1u;U.save_long=0;U.save_bank=(!U.rec&&(U.hb&(1u<<7)))?1:0;}   /* GLO+SAVE: банк (HOME+SAVE оставлен режиму обновления) */
   break;
  case 0: case 1:                       /* OCT- (НЕТ) / OCT+ (ДА) */
   if(U.confirm){ /* активный диалог подтверждения: ДА=OCT+, НЕТ=OCT- */
    if(b==0&&U.confirm==7){W.theme=(int8_t)col_old;worm_full_redraw=1;}   /* COLOR: отмена — вернуть прежнюю схему */
    if(b==1){ /* OCT+ = ДА/применить */
     if(U.confirm==1){setv(P_OA,U.pending_oa);to(S_OSC);}  /* применить алгоритм */
     else if(U.confirm==6){say_save(MK_BANK,W.bank,80,worm_store_bank_save(W.bank));}   /* HOME+SAVE: только банк */
     else if(U.confirm==7){W.theme=(int8_t)U.pending_col;worm_store_proj_save();worm_full_redraw=1;}   /* COLOR: применить схему и запомнить в проекте */
      else if(U.confirm==5){W.usb_mode=(int8_t)U.pending_usb;}   /* USB: MASS STORAGE / AUDIO+MIDI (переключение в app.c) */
     else if(U.confirm==4){W.usb_host=(int8_t)U.pending_usb;}   /* USB MIDI: COMPUTER / EXTERNAL (хост) */
     else if(U.confirm==3){W.usb_out=(int8_t)U.pending_usb;}   /* переключить USB-аудио выход (MASTER / TRK 1-3 / TRK 4-6 / FX) */
     else {int rc=worm_store_bank_save(W.bank);worm_store_proj_save();say_save(MK_PAT,W.bank*NPAT+W.slot,80,rc);}   /* сохранить паттерн + настройки треков/BPM */
    }
    if(U.scr==S_SET){U.scr=U.set_prev;U.tab=U.set_tab;worm_full_redraw=1;}   /* GLO: OCT+ применить / OCT- отмена -> сразу назад на последнее меню */
    if(U.scr==S_SAV||U.scr==S_ALG)to(b==1&&U.confirm==1?S_OSC:U.prev);   /* закрыть диалог */
    U.confirm=0;
   } else if(b==1&&U.scr==S_SET&&U.tab==1){worm_midi_panic();say(MK_DONE,0,80);}
   else if(trig_idx()>=0){ /* зажат триггер: OCT+/- двигает шаг по времени (microtiming); удержание кнопки — автоповтор с разгоном */
    mt_nudge(b?1:-1,1);mt_rep_dir=b?1:-1;mt_rep_n=0;mt_rep_t=worm_ui_now+MT_REP_DELAY;}   /* GLO > MIDI: OCT+ = PANIC (All Notes Off на каналах MIDI-дорожек) */
   else {
    U.oct+=(b?1:-1);if(U.oct<-2)U.oct=-2;if(U.oct>2)U.oct=2;
   }
   break;
  default: break;                       /* остальные кнопки: пока не назначены */
 }
}
/* Клавиши: белые F3..G5 = шаги 0..15; чёрные F#3/G#3/A#3/C#4 = OP1..OP4 (16..19).
 * Прочие чёрные (D#4/F#4/G#4/A#4/C#5/D#5) = -1: вне REC — обычные ноты, в REC — неактивны. */
static int krole(int i){int pc=(53+i)%12;static const uint8_t blk[5]={1,3,6,8,10};int b=-1;for(int k=0;k<5;k++)if(pc==blk[k])b=k;
 if(b<0){int w=0;for(int k=0;k<i;k++){int q=(53+k)%12;int bl=0;for(int t=0;t<5;t++)if(q==blk[t])bl=1;if(!bl)w++;}return w;}
 switch(53+i){case 54:return 16;case 56:return 17;case 58:return 18;case 61:return 19;}return -1;}
/* отпустить все удерживаемые живые ноты ДО смены дорожки (иначе нота «зависнет»: сменится получатель, в который уйдёт отпускание) */
static void rel_live(void){for(int i=0;i<27;i++)if(U.lv[i]>=0){worm_live_off(U.lv[i]);U.lv[i]=-1;}}
/* после смены дорожки: страница, на которую вернёмся по отпусканию SEL, должна существовать у нового типа дорожки */
static void fix_prev(void){int s=U.sel_prev;if(W.mid){if(s==S_OSC||s==S_AMP||s==S_FLT||s==S_ENV||s==S_FX)U.sel_prev=S_NOTE;}else if(s==S_MID||s==S_MCC)U.sel_prev=S_NOTE;}
void worm_ui_key(int i,int dn){int n=53+i+12*U.oct;int m=krole(i);
 if(dn&&U.tc)U.tc_t0=worm_ui_now;                       /* любое нажатие продлевает Trig Condition */
 if(dn&&U.map){ /* LFO MAP: белые клавиши 1-8 выбирают LFO для привязки */
  if(m>=0&&m<NLFO){U.map_lfo=m;U.lfo=m;U.map_di=0;}
  return;}
 if(dn&&(U.hb&(1u<<3))){ /* зажатая SEL: белые клавиши выбирают дорожку: у каждой СВОЯ секвенция (W.pats[дорожка]) и свой голос */
  if(m>=0&&m<NTRK){rel_live();worm_select_track(m);U.chan=m;U.sel=-1;U.hold=-1;U.lk=0;U.sel_k=1;fix_prev();}
  else if(m>=12&&m<=15){rel_live();worm_select_mtrack(15-m);U.sel=-1;U.hold=-1;U.lk=0;U.sel_k=1;fix_prev();}   /* M1..M4 = клавиши шагов 16/15/14/13 */
  else if(m>=16&&m<=19){if(U.hb&(1u<<8))worm_mtrk_mute(m-16,!worm_mtrk_muted(m-16));else worm_trk_mute(m-16,!worm_trk_muted(m-16));U.sel_k=1;}   /* SEL + чёрная клавиша OP1-4 (F#3,G#3,A#3,C#4, справа от белых каналов) = MUTE канала 1-4 */
  else{int q=mmute_key(i);if(q>=0){worm_mtrk_mute(q,!worm_mtrk_muted(q));U.sel_k=1;}   /* SEL + D#5/C#5/A#4/G#4 = MUTE MIDI-дорожек M1..M4 (как аудио-мьюты) */
   else{int tq=tmute_key(i);if(tq>=0&&!(U.hb&(1u<<8))){worm_trk_mute(tq,!worm_trk_muted(tq));U.sel_k=1;}}}   /* SEL + D#4 / F#4 = MUTE дорожек T5 / T6 */
  return;}
 if(lm_on){lm_key(i,dn);return;}   /* LIVE: белые = паттерны (HOME: мьюты), чёрные = эффекты */
 if(!dn){if(U.lv[i]>=0){worm_live_off(U.lv[i]);U.lv[i]=-1;}
  if(m>=16&&m<=19){U.hold_op=0;U.hold_op_t=-1;}        /* отпустили OP1-OP4 */
  /* отпустили шаг: если не было parameter lock — toggle триггер */
  if(U.hold==m&&m>=0&&m<16){if(!U.lk){step_t *s=sel();if(s)worm_step_toggle(s);}U.hold=-1;U.lk=0;}
  if(U.hold_k==m&&m>=0&&m<16){U.hold_k=-1;U.lk=0;}   /* обычный режим: сброс зажатого триггера после p-lock пресета */
  return;}
 if(U.scr==S_VOX&&!(U.hb&(1u<<8))){int tq=tmute_key(i);if(tq>=0){worm_trk_mute(tq,!worm_trk_muted(tq));return;}}   /* VOICES: D#4 / F#4 = MUTE треков 5 / 6 */
 if(m>=16&&m<=19){if(U.scr==S_VOX){if(U.hb&(1u<<8))worm_mtrk_mute(m-16,!worm_mtrk_muted(m-16));else worm_trk_mute(m-16,!worm_trk_muted(m-16));return;}   /* VOICES: OP1-4 = MUTE треков 1-4 */
  if(U.rec||(U.hb&(1u<<8))){U.hold_op_t=0;op_menu(m-16);return;}  /* OP1-4: меню в REC или при HOME */
  /* иначе — обычная чёрная нота (режим синтезатора) */
  if(!U.rec){U.lv[i]=n;worm_live_on(n,100);}
  return;}
 if(U.rec&&(i==KEY_MONO||i==KEY_POLY)){ /* REC: MONO = страница шагов назад, POLY = вперёд — «на горячую», в том числе при зажатом шаге */
  if(dn){int pg=W.p[P_GPG],v=vp()+1+(i==KEY_POLY?1:-1);if(v<1)v=1;if(v>pg)v=pg;
   if(v!=vp()+1){setv(P_GVP,v);if(U.hold>=0)U.lk=1;worm_full_redraw=1;}}   /* U.lk: отпускание зажатого шага после смены страницы не переключает триггер */
  return;}
 if(m<0){ /* чёрная нота (не OP): вне REC — обычная нота, в REC — неактивна */
  if(!U.rec){U.lv[i]=n;worm_live_on(n,100);}
  return;}
 if(U.scr==S_BANK||U.scr==S_PTN){ /* белые клавиши: банки (0..7) / паттерны (0..15) — моментальное переключение */
  if(U.scr==S_BANK&&m>=0&&m<NBANK){set_bank(m);to(U.pick_prev);worm_full_redraw=1;return;}   /* банк: сразу, и назад в меню */
  if(U.scr==S_PTN&&m>=0&&m<NPAT){pat_request(m);U.hold=-1;U.lk=0;to(U.pick_prev);worm_full_redraw=1;return;}   /* паттерн: в очередь до границы такта, и назад в меню */
  pick_touch();return;}
 if(U.hold_op){ /* удержание OP: белая клавиша переключает страницу (как раньше PIT/GLO/MONO/POLY) */
  if(m<ntabs()){if(U.scr==S_SPG)W.p[P_GVP]=m+1;   /* SEQ+клавиша: страница секвенсора */
   else if(U.scr==S_ENV)U.env=m;else{U.tab=m;if(U.confirm==7)W.theme=(int8_t)col_old;if(U.confirm>=3)U.confirm=0;if(U.scr==S_LFO)U.lfo=m;}}
  return;}
 if(U.rec){ /* STEP SEQUENCER / Live Record */
  if(U.live){ /* Live Record: белая клавиша = нота, пишется в текущий шаг (по бегущему курсору) */
   int st=W.cur>=0?W.cur:0;step_t *s=&PT()->s[st];
   put(s,n);                       /* записать наигранную ноту в текущий шаг */
   U.lv[i]=n;worm_live_on(n,100);  /* и услышать её */
   return;
  }
  /* STEP SEQUENCER: белая клавиша = выбор шага (для trigger/parameter lock) */
  U.hold=m;U.lk=0;U.sel=vp()*16+m;
  return;
 }
 /* обычный режим: белая клавиша = нота (не трогаем U.sel — иначе knob-ы прыгают на шаг) */
 U.hold_k=m;U.lv[i]=n;worm_live_on(n,100);}
