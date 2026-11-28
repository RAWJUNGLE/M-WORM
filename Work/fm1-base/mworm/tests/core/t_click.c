/* Щелчки: (1) нота на ещё звучащей дорожке при TRANS=127 не даёт скачка усиления; (2) голос с резонансным фильтром не обрывается,
 * пока фильтр звенит. Сборка (хост): gcc -O1 -DFM1_HOST_TEST -Iworm -I. -I../include tests/core/t_click.c worm/worm_*.c div64.c -lm */
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "worm.h"
int worm_flash_read(uint32_t a,void*b,uint32_t l){(void)a;memset(b,0xFF,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){(void)a;(void)b;(void)l;return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){(void)a;(void)l;return 0;}
void worm_midi_out(int s,int a,int b){(void)s;(void)a;(void)b;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static float buf[44100*4];
static void setp(int id,int v){W.tr[0].p[id]=(int16_t)v;if(W.pat==0)W.p[id]=(int16_t)v;}
static void render(int from,int n){static int16_t o[512];for(int k=from;k<from+n;k+=256){worm_render(o,256);for(int i=0;i<256;i++)buf[k+i]=o[2*i]/32768.f;}}   /* n кратно 256 */
static void base(int wave){worm_init();W.bpm=120.f;W.run=0;for(int t=1;t<NTRK;t++)worm_synth_off_trk(t);
 setp(P_OA,0);setp(P_OW,wave);setp(P_OL,100);setp(P_OL2,0);setp(P_SDL,0);setp(P_SRV,0);setp(P_SCH,0);setp(P_ADS,0);worm_select_track(0);}
static double d2max(int a,int b){double m=0;for(int i=a;i<b;i++){double d=fabs(buf[i]-2*buf[i-1]+buf[i-2]);if(d>m)m=d;}return m;}
int main(void){
 /* 1. legato-перезапуск с TRANS 127: скачок усиления был в ~5000 раз больше обычной 2-й разности синуса */
 base(0);setp(P_E0A,10);setp(P_E0D,64);setp(P_E0S,100);setp(P_E0R,64);setp(P_TRN,127);
 worm_synth_note_on_v(W.tr[0].p,48,110,44100*2,0,1,-1);render(0,22016);
 worm_synth_note_on_v(W.tr[0].p,48,110,44100*2,0,1,-1);render(22016,8192);
 double st=d2max(20000,22000),rt=d2max(22016,22016+4000);printf("retrigger TRANS 127: %.1fx steady state\n",rt/st);
 CK(rt<80*st,"TRANS: gain step on legato retrigger (click)");
 /* 2. конец ноты с резонансным LP: уровень перед обрывом голоса должен быть тихим (раньше около -42 dBFS) */
 base(2);setp(P_E0A,0);setp(P_E0D,64);setp(P_E0S,100);setp(P_E0R,30);setp(P_F1T,1);setp(P_F1C,30);setp(P_F1R,127);setp(P_F1W,127);setp(P_TRN,0);
 worm_synth_note_on_v(W.tr[0].p,48,110,8000,0,1,-1);render(0,44100*3/256*256);
 int last=-1;for(int i=44100*3/256*256-1;i>0;i--)if(buf[i]!=0.f){last=i;break;}
 double pk=0;for(int i=last-64;i<=last;i++)if(i>=0&&fabs(buf[i])>pk)pk=fabs(buf[i]);
 printf("resonant LP: voice ends at %d ms, level before cut %.1f dBFS\n",last*1000/44100,20*log10(pk+1e-9));
 CK(last>0&&last<44100*3-512,"voice must end");CK(pk<.0003,"voice cut while the filter still rings (click)");
 printf(ok?"CLICK OK\n":"CLICK FAIL\n");return ok?0:1;}
