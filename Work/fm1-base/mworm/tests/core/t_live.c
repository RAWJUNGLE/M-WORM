/* LIVE режим (HOME+GLO): вход/выход, мьюты по HOME-защёлке, макросы, переключение паттернов (время + цепочка), хранение набора, эффекты мастера.
 * gcc -DPLAT_HOST -DFM1_HOST_TEST -Imworm -Imworm/worm tests/core/t_live.c worm/worm_{params,midi,seq,store,ui,synth,live,lfx,wt,wt_data,wt_main}.c div64.c -lm */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "worm.h"
int worm_flash_read(uint32_t a,void*b,uint32_t l){memset(b,0xFF,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){return 0;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static int wk(int m){int w=0;for(int i=0;i<27;i++){int pc=(53+i)%12;int bl=pc==1||pc==3||pc==6||pc==8||pc==10;if(!bl){if(w==m)return i;w++;}}return -1;}
static void key(int i){worm_ui_key(i,1);worm_ui_key(i,0);}
static void btn(int b){worm_ui_button(b,1);worm_ui_button(b,0);}
static void run_bars(int n){static int16_t o[256*2];uint32_t b=worm_bar_count();int g=0;while(worm_bar_count()<b+(uint32_t)n&&g++<20000){worm_render(o,256);worm_ui_now+=6;uint32_t B,K,D,E,F,G;worm_ui_lights(&B,&K,&D,&E,&F,&G);}}
static void light(void){uint32_t B,K,D,E,F,G;worm_ui_lights(&B,&K,&D,&E,&F,&G);}
static int beat_sq(uint8_t *fb){int best=-1,bc=0;for(int q=0;q<4;q++){int c=0;for(int y=139;y<161;y++)for(int x=129+q*27;x<149+q*27;x++)c+=fb[y*240+x]!=0;if(c>bc){bc=c;best=q;}}return bc>200?best:-1;}
int main(){worm_init();worm_ui_init();static int16_t o[256*2];uint32_t B,K,DB,DK,BB,BK;
 worm_store_bank_load(0);
 CK(worm_lm_chain==2&&worm_lm_order==0,"defaults: chain 2 PASS / order USED");
 W.slot=5;   /* проект помнит последний выбранный A06 — в LIVE его быть не должно */
 /* вход: HOME+GLO (в любом порядке) */
 worm_ui_button(8,1);worm_ui_button(7,1);worm_ui_button(7,0);worm_ui_button(8,0);
 CK(W.slot==0,"LIVE opens at A01, not at A06");
 /* регрессия: записанный A06 не должен гореть клавишей в LIVE */
 W.slot=5;W.pats[0].s[0].on=1;worm_step_init(&W.pats[0].s[0]);
 CK(worm_store_pat_switch(0)==0,"A06 committed into MRU cache");
 CK((worm_store_bank_used()&(1u<<5))!=0,"A06 is recorded");
 {uint32_t b2,k2,d2,e2,f2,g2;worm_ui_lights(&b2,&k2,&d2,&e2,&f2,&g2);
  CK(!(k2&(1u<<wk(5)))&&!(e2&(1u<<wk(5)))&&!(g2&(1u<<wk(5))),"recorded A06 key stays dark");
  CK((g2&(1u<<wk(0)))!=0,"current A01 key breathes");}
 /* вход во время игры тоже сразу A01, без ожидания такта */
 W.slot=5;worm_ui_button(12,1);worm_ui_button(12,0);worm_render(o,256);CK(W.run,"play");
 worm_ui_button(7,1);worm_ui_button(8,1);worm_ui_button(8,0);worm_ui_button(7,0);   /* выйти из LIVE */
 worm_ui_button(7,1);worm_ui_button(8,1);worm_ui_button(8,0);worm_ui_button(7,0);   /* войти снова */
 CK(W.slot==0,"re-entry while playing: A01 at once");
 worm_ui_button(12,1);worm_ui_button(12,0);worm_render(o,256);CK(!W.run,"stop");
 W.pats[0].s[0].on=0;worm_step_init(&W.pats[0].s[0]);
 worm_ui_render((uint8_t[240*240]){0});   /* не падает */
 /* мьюты: HOME открывает каналы, повторное HOME закрывает */
 btn(8);key(wk(2));key(wk(14));btn(8);
 CK(worm_trk_muted(2),"HOME latch + key3 mutes T3");CK(worm_mtrk_muted(1),"HOME latch + key15 mutes M2");
 btn(8);key(wk(2));key(wk(14));btn(8);CK(!worm_trk_muted(2)&&!worm_mtrk_muted(1),"toggle back");
 /* макросы: SELECT = BPM, K1 = FLT1 CUT текущей дорожки */
 float b0=W.bpm;worm_ui_enc(0,2);CK(W.bpm>b0,"SELECT macro changes BPM");
 worm_select_track(1);int c0=W.tr[1].p[P_F1C];W.tr[1].p[P_F1C]=W.p[P_F1C]=60;worm_ui_enc(2,-3);CK(W.tr[1].p[P_F1C]<60,"K macro turns FLT1 cut of track 2");
 /* редактор: EDIT, повернуть K1 -> другая цель, OCT+ меняет дорожку */
 worm_ui_button(6,1);worm_ui_button(6,0);worm_ui_enc(2,5);CK(worm_lm_bank.id[2]!=P_F1C,"editor: K1 retargets");
 worm_ui_enc(6,1);CK(worm_lm_own==1,"PRESET > scope PATTERN");
 worm_ui_button(1,1);worm_ui_button(1,0);
 worm_ui_button(6,1);worm_ui_button(6,0);
 /* паттерн с собственным набором переживает переключение (запись в MRU-кэше) */
 int id2=worm_lm_pat.id[2];W.pats[0].s[0].on=1;worm_step_init(&W.pats[0].s[0]);
 worm_ui_key(wk(3),1);worm_ui_key(wk(3),0);light();CK(W.slot==3,"stopped: pattern switches at once");
 CK(worm_lm_own==0,"new pattern: no own set (bank set)");
 key(wk(0));light();CK(W.slot==0&&worm_lm_own==1&&worm_lm_pat.id[2]==id2,"own set restored with pattern");
/* активность дорожек теперь на клавишах-мьютах: реальный триггер шага мигает ярко, заглушённый — тускло */
W.pats[0].s[0].on=1;worm_step_init(&W.pats[0].s[0]);
worm_ui_button(12,1);worm_ui_button(12,0);worm_render(o,256);CK(W.run,"play for trigger LEDs");
btn(8);   /* HOME-защёлка: клавиши 1..6 = мьюты T1..T6 */
{int bit=1u<<wk(0);uint32_t t0=worm_trig_at(0),a,b,c,d,e,f;
 for(int g=0;g<20000&&worm_trig_at(0)==t0;g++){worm_render(o,256);worm_ui_now+=6;}
 worm_ui_lights(&a,&b,&c,&d,&e,&f);
 CK((b&bit)!=0&&!(d&bit),"T1 mute key flashes bright on step trigger");
 worm_trk_mute(0,1);t0=worm_trig_at(0);
 for(int g=0;g<20000&&worm_trig_at(0)==t0;g++){worm_render(o,256);worm_ui_now+=6;}
 worm_ui_lights(&a,&b,&c,&d,&e,&f);
 CK((d&bit)!=0&&!(b&bit),"muted T1 keeps flashing dim");
 worm_trk_mute(0,0);worm_ui_now+=100;worm_ui_lights(&a,&b,&c,&d,&e,&f);
 CK(!(b&bit)&&!(d&bit),"flash ends between steps");}
/* заглушенная MIDI-дорожка тоже мигает тускло: мьют глушит MIDI, но не индикацию */
worm_select_mtrack(0);{step_t *ms=&W.mpats[0].s[0];memset(ms,0,sizeof *ms);worm_step_init(ms);ms->on=1;ms->note=60;ms->vel=100;}
worm_mtrk_mute(0,1);{int bit=1u<<wk(15);uint32_t t0=worm_trig_at(NTRK),a,b,c,d,e,f;
 for(int g=0;g<20000&&worm_trig_at(NTRK)==t0;g++){worm_render(o,256);worm_ui_now+=6;}
 worm_ui_lights(&a,&b,&c,&d,&e,&f);CK((d&bit)!=0&&!(b&bit),"muted MIDI track flashes dim");}
worm_mtrk_mute(0,0);worm_select_track(0);
btn(8);worm_ui_button(12,1);worm_ui_button(12,0);worm_render(o,256);CK(!W.run,"stop after trigger LED test");
 /* игра: ручной выбор ждёт такт, цепочка идёт сама */
 worm_ui_button(12,1);worm_ui_button(12,0);worm_render(o,256);CK(W.run,"play");
 worm_lm_chain=0;worm_lm_sw=1;key(wk(1));CK(W.slot==0,"running: switch is deferred");run_bars(2);CK(W.slot==1,"switch after 1 bar");
 worm_lm_chain=2;worm_lm_order=1;   /* теперь проверяем заводские 2 PASS / ALL с чистым счётчиком тактов */
 int before=W.slot;run_bars(1);CK(W.slot==before,"2 PASS: holds during first pass");run_bars(2);CK(W.slot!=before,"2 PASS + ALL: advances after second pass");
 /* SEQ: время переключения и цепочка */
 worm_ui_button(11,1);worm_ui_button(11,0);worm_ui_enc(0,1);worm_ui_enc(1,1);CK(worm_lm_sw==2&&worm_lm_chain==3,"SEQ page edits switch time / chain");
 worm_ui_button(11,1);worm_ui_button(11,0);
 /* рендер экранов */
 {static uint8_t fb[240*240];worm_ui_render(fb);int n=0;for(int i=0;i<240*240;i++)n+=fb[i]!=0;CK(n>500,"live screen draws");
   int pt=0,sq=0;for(int y=138;y<163;y++){for(int x=2;x<84;x++)pt+=fb[y*240+x]!=0;for(int x=128;x<230;x++)sq+=fb[y*240+x]!=0;}
   CK(pt>120&&pt<600,"pattern name drawn large, not covered by channels");CK(sq>100&&sq<2000,"beat squares enlarged");
   int act=beat_sq(fb);CK(act>=0,"one beat square is filled while playing");
   int b0=worm_beat_idx();for(int g=0;g<2000&&worm_beat_idx()==b0;g++){worm_render(o,256);worm_ui_now+=6;}
   worm_ui_render(fb);int act2=beat_sq(fb);CK(act2>=0&&act2!=act,"beat square moves to the next beat");
   FILE*f=fopen("/tmp/t/live.pgm","wb");fprintf(f,"P5 240 240 15\n");for(int i=0;i<240*240;i++)fputc(fb[i],f);fclose(f);
  worm_ui_button(6,1);worm_ui_button(6,0);worm_ui_render(fb);f=fopen("/tmp/t/mac.pgm","wb");fprintf(f,"P5 240 240 15\n");for(int i=0;i<240*240;i++)fputc(fb[i],f);fclose(f);
  worm_ui_button(6,1);worm_ui_button(6,0);
  worm_ui_button(11,1);worm_ui_button(11,0);worm_ui_render(fb);f=fopen("/tmp/t/seq.pgm","wb");fprintf(f,"P5 240 240 15\n");for(int i=0;i<240*240;i++)fputc(fb[i],f);fclose(f);worm_ui_button(11,1);worm_ui_button(11,0);}
 /* выход */
 worm_ui_button(7,1);worm_ui_button(8,1);worm_ui_button(8,0);worm_ui_button(7,0);
 worm_ui_key(wk(5),1);worm_ui_key(wk(5),0);   /* вне LIVE белая клавиша = нота/шаг, паттерн не меняется */
 /* эффекты: idle = -1; REPEAT даёт повтор; фильтр глушит */
 {float L[64],R[64];for(int i=0;i<64;i++)L[i]=R[i]=0;CK(worm_lfx_block(L,R,64)<0,"idle: untouched");
  worm_lfx_arm(1);float ph=0;float mx=0;
  for(int blk=0;blk<200;blk++){for(int i=0;i<64;i++){L[i]=R[i]=.5f*sinf(ph);ph+=.05f;}worm_lfx_block(L,R,64);}
  worm_lfx_key(LFX_R16,1);float seen=0;for(int blk=0;blk<120;blk++){for(int i=0;i<64;i++){L[i]=R[i]=.5f*sinf(ph);ph+=.3f;}float pk=worm_lfx_block(L,R,64);if(blk>60&&pk>seen)seen=pk;}
  CK(seen>.2f,"REPEAT sounds");worm_lfx_key(LFX_R16,0);
  worm_lfx_set(LFX_A_FILT,-100);float rms=0;for(int blk=0;blk<100;blk++){for(int i=0;i<64;i++){L[i]=R[i]=.5f*sinf(ph);ph+=2.0f;}worm_lfx_block(L,R,64);}for(int i=0;i<64;i++)rms+=L[i]*L[i];
  CK(rms<1.f,"LPF closes a high tone");
  worm_lfx_key(LFX_TAPE,1);for(int blk=0;blk<400;blk++){for(int i=0;i<64;i++){L[i]=R[i]=.5f*sinf(ph);ph+=.05f;}worm_lfx_block(L,R,64);}
  for(int i=0;i<64;i++)mx+=fabsf(L[i]);CK(mx<1.f,"TAPE STOP winds down");}
 printf(ok?"LIVE OK\n":"LIVE FAIL\n");return !ok;}
