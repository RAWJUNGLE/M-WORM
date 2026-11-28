#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "worm.h"
int worm_flash_read(uint32_t a,void*b,uint32_t l){memset(b,0xFF,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){return 0;}
static void setp(int t,int id,int v){W.tr[t].p[id]=v;if(W.pat==t)W.p[id]=v;}
static int16_t out[256*2];
static void run(FILE*f,int blocks,int bsz){for(int i=0;i<blocks;i++){worm_render(out,bsz);fwrite(out,4,bsz,f);}}
int main(int argc,char**argv){FILE*f=fopen(argv[1],"wb");
 for(int sc=0;sc<14;sc++){worm_init();
  for(int t=0;t<NTRK;t++){worm_select_track(t);
   int al=(sc<9)?sc:(sc-9);
   setp(t,P_OA,al);setp(t,P_OL,100);setp(t,P_OL2,sc%2?80:0);setp(t,P_E0S,100);setp(t,P_E0R,20+t*10);setp(t,P_E3S,90);
   setp(t,P_SDL,(sc>=2&&sc%3!=1)?40+t*10:0);setp(t,P_SRV,(sc%2)?50:0);setp(t,P_SCH,(sc%4==0)?60:0);
   if(sc%3==0){setp(t,P_F1T,1);setp(t,P_F1C,60+t*10);setp(t,P_F1R,40);}
   if(sc%4==1){setp(t,P_L0D,worm_dest_index(P_F1C)>0?worm_dest_index(P_F1C):1);setp(t,P_L0S,50+t*7);}
   if(sc==11){setp(t,P_ADS,2);setp(t,P_ADR,60);}
   if(sc==12)setp(t,P_RS0,0);
   for(int i=0;i<16;i++){step_t*s=&W.pats[t].s[i];worm_step_init(s);if((i+t)%3==0||sc==13)s->on=1,s->note=48+((i*5+t*7)%24);}
  }
  W.bpm=100+sc*9;W.run=1;worm_play();
  run(f,100,256);
  worm_select_track(1);setp(1,P_OL,70);W.tr[2].p[P_APAN]=20;
  run(f,60,200);
  worm_live_on(52,100);worm_live_on(59,90);run(f,40,256);worm_live_off(52);
  run(f,80,256);worm_stop();run(f,200,256);   /* tails */
 }
 fclose(f);return 0;}
