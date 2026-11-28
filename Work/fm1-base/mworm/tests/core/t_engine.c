/* Компактный движок: конечность/пики, затухание голоса и FX-хвостов, VA/FM, фильтр на OSC, p-lock, 4 независимых дорожки. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "worm.h"
int worm_flash_read(uint32_t a,void*b,uint32_t l){memset(b,0xFF,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){(void)a;(void)b;(void)l;return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){(void)a;(void)l;return 0;}
void worm_midi_out(int s,int a,int b){(void)s;(void)a;(void)b;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static int16_t o[256*2];
static double rms,pk;static int bad;
static void run(int frames){rms=0;pk=0;bad=0;long n=0;for(int f=0;f<frames;f+=256){worm_render(o,256);
 for(int i=0;i<512;i++){double a=fabs(o[i]/32768.0);if(a>pk)pk=a;rms+=a*a;n++;}}rms=sqrt(rms/(n?n:1));}
static void init(void){worm_init();W.bpm=120.f;W.run=0;}
static void note(int t,int n,int v,int gate){worm_select_track(t);worm_synth_note_on_v(W.tr[t].p,n,v,gate,t,1,-1);}
static void setp(int t,int id,int v){W.tr[t].p[id]=(int16_t)v;if(W.pat==t)W.p[id]=(int16_t)v;}
int main(){
 /* 1. VA и FM звучат, пик <= 1, ничего не NaN */
 for(int al=0;al<2;al++){init();for(int t=0;t<NTRK;t++){setp(t,P_OA,al);setp(t,P_OL2,70);setp(t,P_OS,60);setp(t,P_SDL,80);setp(t,P_SRV,80);setp(t,P_SCH,80);setp(t,P_ADS,3);setp(t,P_ADW,127);setp(t,P_ADR,127);note(t,36+t*7,127,44100);}
  run(44100);printf("algo %d: rms %.3f peak %.3f\n",al,rms,pk);CK(rms>.02,"engine silent");CK(pk<=1.0,"peak > 1");}
 /* 2. после отпускания: голоса неактивны, звук и хвосты FX затухают до тишины */
 for(int t=0;t<NTRK;t++)worm_synth_off_trk(t);run(44100*12);
 int act=0;for(int t=0;t<NTRK;t++)act+=worm_synth_active_trk(t);CK(act==0,"voices stay active");
 run(2560);printf("tail after 12 s: rms %.6f\n",rms);CK(rms<.0005,"FX tail never ends");
 /* 3. фильтр на OSC: LP на OSC1 режет верх (OSC2 выключен) */
 double hf[2];for(int f=0;f<2;f++){init();setp(0,P_OA,0);setp(0,P_OW,2);setp(0,P_OL,127);setp(0,P_OL2,0);setp(0,P_F1T,f?1:0);setp(0,P_F1C,30);setp(0,P_F1W,127);
  note(0,57,127,44100);run(22050);hf[f]=rms;}
 printf("saw rms: no filter %.3f, LP@low %.3f\n",hf[0],hf[1]);CK(hf[1]<hf[0]*.7,"per-osc LP has no effect");
 /* 4. p-lock: шаг с eff != дорожка играет значение шага */
 { init();setp(0,P_OL,100);int16_t e[NP];memcpy(e,W.tr[0].p,sizeof e);e[P_OL]=0;e[P_OS]=0;worm_select_track(0);
   worm_synth_note_on_v(e,48,127,22050,0,1,-1);run(8192);CK(rms<.001,"p-lock OL=0 not applied");
   worm_synth_note_on_v(W.tr[0].p,48,127,22050,0,1,-1);run(8192);CK(rms>.02,"track patch silent");}
 /* 5. дорожки независимы: нота на T3 не активирует T1 */
 { init();note(2,60,100,4410);CK(worm_synth_active_trk(2)==1&&worm_synth_active_trk(0)==0,"track isolation");}
 printf(ok?"engine OK\n":"engine FAILED\n");return ok?0:1;}
