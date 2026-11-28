/* Время delay: эхо должно стоять там, где задано (доля такта), если влезает в буфер; иначе — делится пополам. */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "worm.h"
int worm_flash_read(uint32_t a,void*b,uint32_t l){memset(b,0xFF,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){return 0;}
static int16_t o[256*2];
static int echo_pos(float bpm,int xt){worm_init();W.bpm=bpm;
 for(int t=0;t<NTRK;t++){W.tr[t].p[P_SDL]=0;}
 W.tr[0].p[P_SDL]=127;W.tr[0].p[P_OL]=100;W.tr[0].p[P_E0S]=0;W.tr[0].p[P_E0D]=0;W.tr[0].p[P_E0A]=0;W.p[P_XT]=xt;W.p[P_XFB]=0;W.p[P_XFL]=127;W.p[P_XMX]=127;W.p[P_ADW]=0;
 W.tr[0].p[P_AVOL]=100;worm_select_track(0);W.p[P_XT]=xt;W.p[P_XFB]=0;W.p[P_XFL]=127;W.p[P_XMX]=127;W.p[P_SDL]=127;W.tr[0].p[P_SDL]=127;worm_synth_set_patch(W.p);
 W.tr[0].p[P_E0R]=0;W.tr[0].p[P_E0S]=127;W.tr[0].p[P_E0D]=127;worm_live_on(60,127);
 static float y[60000];int n=0;double mx=0;int off=0;
 while(n<60000){worm_render(o,256);if(!off&&n>=512){worm_live_off(60);off=1;}for(int i=0;i<256&&n<60000;i++){y[n]=fabsf((float)o[i*2]);n++;}}
 /* dry: первые 2000 сэмплов; ищем самый поздний заметный пик после 3000 */
 int dry_end=0;for(int i=0;i<n;i++)if(y[i]>200)dry_end=i;
 (void)mx;return dry_end;}
int main(){const float bpm=100.f;static const float dtt[5]={.25f,.5f,.75f,1.f,2.f};
 for(int xt=0;xt<5;xt++){float tm=60.f/bpm*dtt[xt];float maxt=27648/44100.f;int halved=0;while(tm>maxt){tm*=.5f;halved++;}
  printf("XT=%d want %.0f samples (halved %dx) last-audible %d\n",xt,tm*44100,halved,echo_pos(bpm,xt));}
 return 0;}
