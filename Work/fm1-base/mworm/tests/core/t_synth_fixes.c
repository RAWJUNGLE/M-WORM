/* 1) PITCH-LFO при умолчательной глубине не уходит на ±24 пт с обрезкой; 2) нота на ещё звучащей дорожке перезапускает огибающую
 * (дорожка не «склеивается»), а при PORTA on остаётся легато; 3) worm_mod_span: линейно для обычных параметров. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include "worm.h"
int worm_flash_read(uint32_t a,void*b,uint32_t l){(void)a;(void)b;(void)l;return 1;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){(void)a;(void)b;(void)l;return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){(void)a;(void)l;return 0;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static float X[44100*2];
static void render(int n0,int n1){int16_t o[512];for(int n=n0;n<n1;n+=256){worm_synth_render(o,256);for(int i=0;i<256;i++)X[n+i]=o[2*i]/32768.f;}}
/* амплитуда в окнах по 8 сэмплов после момента a: минимум и максимум огибающей (peak по окну) */
static float wpk(int a){float m=0;for(int i=0;i<8;i++){float v=fabsf(X[a+i]);if(v>m)m=v;}return m;}
int main(void){
 /* ---- 1: pitch LFO ---- */
 worm_init();W.tr[0].p[P_OW]=1;W.tr[0].p[P_E0S]=127;W.tr[0].p[P_E0D]=127;W.tr[0].p[P_L0D]=(int16_t)worm_dest_index(P_OPI);   /* TRI, LFO1 -> PITCH, depth 80 по умолчанию */
 worm_synth_note_on_v(W.tr[0].p,36,127,44100*2,0,1,-1);render(0,44100);
 {double lo=1e9,hi=-1e9;int last=-1;for(int i=8000;i<44100-1;i++)if(X[i]<0&&X[i+1]>=0){if(last>0){double st=12*log2((44100.0/(i-last))/65.406);if(st<lo)lo=st;if(st>hi)hi=st;}last=i;}
  printf("pitch LFO @default depth: %.1f .. %.1f st\n",lo,hi);
  CK(hi<7.5&&lo>-7.5,"pitch LFO default depth stays within ~±5 st (no ±24 siren)");CK(hi-lo>3.f,"pitch LFO still audible");}
 CK(fabsf(worm_mod_span(P_F1C,.5f)-.5f)<1e-6f,"non-pitch params stay linear");
 CK(fabsf(worm_mod_span(P_OPI,1.f)-.25f)<1e-6f&&fabsf(worm_mod_span(P_OPI,-1.f)+.25f)<1e-6f,"pitch full depth = ±12 st");
 /* ---- 2: перезапуск огибающей ---- */
 for(int porta=0;porta<2;porta++){
  worm_init();W.tr[0].p[P_OW]=3;W.tr[0].p[P_E0S]=127;W.tr[0].p[P_E0D]=127;W.tr[0].p[P_E0A]=0;W.tr[0].p[P_NPO]=(int16_t)porta;
  worm_synth_note_on_v(W.tr[0].p,36,127,44100*2,0,1,-1);render(0,5120);
  float before=wpk(5120-16);
  worm_synth_note_on_v(W.tr[0].p,43,127,44100*2,0,1,-1);render(5120,5120+2048);
  float dip=1e9,after=0;for(int a=5120;a<5120+512;a+=8){float p=wpk(a);if(p<dip)dip=p;}for(int a=5120+1024;a<5120+2048;a+=8){float p=wpk(a);if(p>after)after=p;}
  printf("porta=%d: level before %.3f, dip after re-trigger %.3f, level after %.3f\n",porta,before,dip,after);
  if(!porta){CK(dip<before*.35f,"mono re-trigger: envelope re-attacks from zero (articulation)");CK(after>before*.8f,"re-trigger reaches full level again");}
  else CK(dip>before*.6f,"PORTA on: legato, envelope is not restarted");}
 printf(ok?"SYNTHFIX OK\n":"SYNTHFIX FAIL\n");return !ok;}
