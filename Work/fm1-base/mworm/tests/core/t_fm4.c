/* 4-операторный FM (как Digitone): диапазоны слотов, подписи, DEST (порядок/ext), звук алгоритмов, HARM, ENV A/B, LFO RATIO ALL, виртуальный Pitch. */
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
#define N 22050
static float X[N+512];
static void render(void){int16_t o[512];for(int n=0;n<N;n+=256){worm_synth_render(o,256);for(int i=0;i<256;i++)X[n+i]=o[2*i]/32768.f;}}
static void patch(int algo){worm_init();int16_t *p=W.tr[0].p;p[P_OA]=ALGO_FM;worm_fm_defaults(p);p[worm_fm_slot[FM_ALGO]]=algo;p[P_E0S]=127;p[P_E0D]=127;p[P_E0A]=0;}
static float rms(int a,int b){double s=0;for(int i=a;i<b;i++)s+=X[i]*X[i];return (float)sqrt(s/(b-a));}
static float dif(const float *A,const float *B){double s=0,e=0;for(int i=0;i<N;i++){s+=(A[i]-B[i])*(A[i]-B[i]);e+=A[i]*A[i];}return (float)sqrt(s/(e+1e-9));}
static float Y[N];
static void note(void){worm_synth_note_on_v(W.tr[0].p,48,127,44100*2,0,1,-1);render();}
int main(void){
 /* 1) диапазоны PLIST вмещают FM-диапазоны; форматы; defaults/restore */
 for(int k=0;k<FM_N;k++){int mn,mx;worm_fm_range(k,&mn,&mx);const pinfo_t *pi=&worm_pinfo[worm_fm_slot[k]];char b[24];
  if(pi->mn>mn||pi->mx<mx){printf("slot %d range %d..%d vs %d..%d\n",k,pi->mn,pi->mx,mn,mx);ok=0;}
  worm_fm_fmt(k,mn,b);worm_fm_fmt(k,mx,b);CK(worm_fm_of(worm_fm_slot[k])==k,"fm_of inverse");CK(strlen(worm_fm_label(k))<=5,"label fits a 40px knob");}
 { int16_t p[NP];memset(p,0,sizeof p);worm_fm_defaults(p);CK(p[P_FR2]==3&&p[P_FV1]==64,"fm defaults");worm_fm_restore(p);CK(p[P_FV1]==worm_pinfo[P_FV1].def,"fm restore");}
 { char b[24];worm_fm_fmt(FM_RC,3,b);CK(!strcmp(b,"2"),"ratio C fmt");worm_fm_fmt(FM_RA,0,b);CK(!strcmp(b,".25"),"ratio A .25");worm_fm_fmt(FM_RB,24,b);CK(!strcmp(b,"1:1"),"ratio B default 1:1");worm_fm_fmt(FM_RB,3*11+4,b);CK(!strcmp(b,"2:3"),"ratio B pair");
   worm_fm_fmt(FM_HARM,-3,b);CK(!strcmp(b,"-1.5"),"harm fmt");worm_fm_fmt(FM_OC,64+16,b);CK(!strcmp(b,"+1.00"),"offset +1"); }
 /* 2) порядок DEST: уникален, валиден, индексы не вне списка; FM начинается с Algorithm, composite — через ext */
 for(int al=0;al<3;al++){uint8_t ix[DEST_VMAX],gr[DEST_VMAX];int n=worm_dest_order(al==0?ALGO_VA:al==1?ALGO_FM:ALGO_WT,ix,gr);CK(n>20&&n<=DEST_VMAX,"dest order size");
  for(int i=0;i<n;i++){CK(ix[i]<worm_dest_n,"dest idx in range");for(int j=0;j<i;j++)if(ix[i]==ix[j]){printf("dup dest %d at %d/%d algo %d\n",ix[i],j,i,al);ok=0;}}
  if(al==1){CK(ix[0]==0&&ix[1]==2,"FM: Off, Algorithm");CK(!strcmp(worm_dest_grp(gr[1]),"OSC"),"OSC group");}}
 CK(worm_dest_param(115)==P_VPAL&&worm_dest_param(116)==P_VPAB,"virtual dests");
 { int16_t ex[2];CK(worm_dest_ext(117,ex)==2&&ex[0]==P_FR2&&ex[1]==P_FF1,"ratio all ext");CK(worm_dest_ext(111,ex)==1&&ex[0]==P_OPI2,"all osc pitch ext");CK(worm_dest_ext(5,ex)==0,"plain dest no ext");}
 /* 3) звук: каждый алгоритм конечен, звучит, и все различаются */
 static float R[8][N];
 for(int a=0;a<8;a++){patch(a);W.tr[0].p[worm_fm_slot[FM_LEVB]]=90;W.tr[0].p[worm_fm_slot[FM_LEVA]]=90;W.tr[0].p[worm_fm_slot[FM_DECB]]=127;W.tr[0].p[worm_fm_slot[FM_DECA]]=127;note();
  int bad=0;for(int i=0;i<N;i++)if(!isfinite(X[i])||fabsf(X[i])>2.f)bad=1;CK(!bad,"FM finite, bounded");CK(rms(4000,N)>.02f,"FM sounds");memcpy(R[a],X,sizeof Y);}
 for(int a=0;a<8;a++)for(int b=a+1;b<8;b++)if(dif(R[a],R[b])<.05f){printf("algo %d ~ %d (%.3f)\n",a+1,b+1,dif(R[a],R[b]));ok=0;}
 /* 3b) с параметрами ПО УМОЛЧАНИЮ (без ручных LEV) все 8 алгоритмов должны звучать по-разному */
 for(int a=0;a<8;a++){patch(a);note();memcpy(R[a],X,sizeof Y);}
 for(int a=0;a<8;a++)for(int b=a+1;b<8;b++)if(dif(R[a],R[b])<.03f){printf("default patch: algo %d ~ %d (%.3f)\n",a+1,b+1,dif(R[a],R[b]));ok=0;}
 /* 3c) MIX: у КАЖДОГО алгоритма два выхода X/Y, кроссфейд меняет звук (0 = только X, 127 = только Y) */
 for(int a=0;a<8;a++){patch(a);W.tr[0].p[worm_fm_slot[FM_RB]]=36;W.tr[0].p[worm_fm_slot[FM_MIX]]=0;note();memcpy(R[0],X,sizeof Y);patch(a);W.tr[0].p[worm_fm_slot[FM_RB]]=36;W.tr[0].p[worm_fm_slot[FM_MIX]]=127;note();
  if(dif(R[0],X)<.05f){printf("MIX has no effect in algo %d\n",a+1);ok=0;}
  patch(a);W.tr[0].p[worm_fm_slot[FM_MIX]]=64;note();CK(rms(4000,N)>.02f,"MIX centre sounds");}
 /* 4) HARM, FDBK, ENV A (DEC) меняют звук; ENV A спадает */
 patch(0);W.tr[0].p[worm_fm_slot[FM_LEVA]]=100;W.tr[0].p[worm_fm_slot[FM_DECA]]=127;note();memcpy(Y,X,sizeof Y);
 patch(0);W.tr[0].p[worm_fm_slot[FM_LEVA]]=100;W.tr[0].p[worm_fm_slot[FM_DECA]]=127;W.tr[0].p[worm_fm_slot[FM_HARM]]=30;note();CK(dif(X,Y)>.05f,"HARM changes the tone");
 patch(0);W.tr[0].p[worm_fm_slot[FM_LEVA]]=100;W.tr[0].p[worm_fm_slot[FM_DECA]]=127;W.tr[0].p[worm_fm_slot[FM_FDBK]]=100;W.tr[0].p[worm_fm_slot[FM_LEVB]]=90;note();memcpy(Y,X,sizeof Y);W.tr[0].p[worm_fm_slot[FM_FDBK]]=0;note();CK(dif(X,Y)>.05f,"FDBK changes the tone");
 patch(0);W.tr[0].p[worm_fm_slot[FM_LEVA]]=127;W.tr[0].p[worm_fm_slot[FM_DECA]]=20;W.tr[0].p[worm_fm_slot[FM_ENDA]]=0;note();
 { /* спектральная яркость: A-индекс спадает -> поздний кусок ближе к синусу (меньше «скачков» производной) */
  double e1=0,e2=0;for(int i=2000;i<4000;i++)e1+=fabs(X[i+1]-2*X[i]+X[i-1]);for(int i=18000;i<20000;i++)e2+=fabs(X[i+1]-2*X[i]+X[i-1]);
  CK(e2<e1*.8,"ENV A decay darkens the tone"); }
 /* 5) PHRT: ALL -> две ноты начинаются одинаково; OFF -> разной фазой */
 patch(0);W.tr[0].p[worm_fm_slot[FM_PHRT]]=1;note();CK(fabsf(X[0])<.01f,"PHRT ALL: starts at phase 0");memcpy(Y,X,sizeof Y);
 patch(0);W.tr[0].p[worm_fm_slot[FM_PHRT]]=0;note();CK(dif(X,Y)>.01f,"PHRT OFF: free-running start phase");
 /* 5b) щелчки: ретриггер посреди звука и старт со случайной фазой не дают скачка больше, чем у самого сигнала */
 for(int mode=0;mode<2;mode++){patch(2);int16_t *p=W.tr[0].p;p[worm_fm_slot[FM_PHRT]]=mode?0:1;p[worm_fm_slot[FM_DECA]]=30;p[worm_fm_slot[FM_LEVA]]=127;p[worm_fm_slot[FM_LEVB]]=127;p[worm_fm_slot[FM_DECB]]=30;
  int16_t o[512];static float Z[8192];int n=0;worm_synth_note_on_v(p,48,127,44100*2,0,1,-1);
  for(int b=0;b<16;b++){worm_synth_render(o,256);for(int i=0;i<256;i++)Z[n++]=o[2*i]/32768.f;}
  worm_synth_note_on_v(p,48,127,44100*2,0,1,-1);for(int b=0;b<16;b++){worm_synth_render(o,256);for(int i=0;i<256;i++)Z[n++]=o[2*i]/32768.f;}
  float mx=0,mr=0;for(int i=1;i<n;i++){float d=fabsf(Z[i]-Z[i-1]);if(d>mx)mx=d;}
  for(int i=1;i<4096;i++){float d=fabsf(Z[i]-Z[i-1]);if(d>mr)mr=d;}   /* первая нота: обычный максимум шага */
  printf("click mode %d: max step %.3f (first note %.3f)\n",mode,mx,mr);
  CK(mx<1.15f*mr+.02f,"no click at retrigger / start");}
 /* 6) LFO на RATIO ALL (ext, 2 цели) и Pitch All: меняют звук и не ломают */
 patch(0);W.tr[0].p[worm_fm_slot[FM_LEVA]]=100;W.tr[0].p[worm_fm_slot[FM_DECA]]=127;note();memcpy(Y,X,sizeof Y);
 patch(0);W.tr[0].p[worm_fm_slot[FM_LEVA]]=100;W.tr[0].p[worm_fm_slot[FM_DECA]]=127;W.tr[0].p[P_L0D]=117;W.tr[0].p[P_L0P]=100;W.tr[0].p[P_L0S]=60;note();CK(dif(X,Y)>.05f,"LFO -> RATIO ALL");
 patch(0);W.tr[0].p[worm_fm_slot[FM_LEVA]]=100;W.tr[0].p[worm_fm_slot[FM_DECA]]=127;W.tr[0].p[P_L0D]=115;W.tr[0].p[P_L0P]=100;W.tr[0].p[P_L0S]=60;note();CK(dif(X,Y)>.05f,"LFO -> virtual Pitch All");
 printf(ok?"FM4 OK\n":"FM4 FAIL\n");return !ok;}
