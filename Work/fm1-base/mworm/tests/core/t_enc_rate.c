/* Ускорение K1..K4 на точечных параметрах: медленно = ровно 1 шаг, быстро = больше,
 * направление/пауза/обычный параметр сбрасывают разгон, DEST не встаёт на заголовок. */
#include <stdio.h>
#include <string.h>
#include "worm_ui.c"
static uint8_t nor[1<<20];
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){memset(nor+a,0xFF,l);return 0;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static int call(int k,int id,int d,uint32_t ms){worm_ui_now+=ms;int a=W.p[id];worm_ui_enc(k,d);return W.p[id]-a;}
static int call_from(int k,int id,int from,int d,uint32_t ms){W.p[id]=from;return call(k,id,d,ms);}
int main(void){
 int a,b,c;
 memset(nor,0xFF,sizeof nor);worm_init();worm_ui_init();worm_ui_now=1000;
 to(S_OSC);U.tab=0;
 for(int i=0;i<4;i++)CK(call_from(2,P_OW,0,1,300)==1,"slow OSC WAVE = +1");
 W.p[P_OA]=W.tr[W.pat].p[P_OA]=ALGO_VA;
 CK(call_from(2,P_OW,0,1,8)==5,"fast OSC WAVE in VA caps at 5 basic shapes");
 W.p[P_OA]=W.tr[W.pat].p[P_OA]=ALGO_WT;
 CK(call_from(2,P_OW,WAVE_MAIN0,1,8)>1,"fast OSC WAVE in WAVETABLE (many tables) accelerates");
 W.p[P_OA]=W.tr[W.pat].p[P_OA]=ALGO_VA;
 CK(call_from(2,P_OW,2,-1,300)==-1,"direction change = exactly -1");
 to(S_ARP);U.tab=0;
 CK(call_from(2,P_ARM,0,1,300)==1,"slow ARP MOD = +1");
 a=call_from(2,P_ARM,0,1,8);b=call_from(2,P_ARM,0,1,8);c=call_from(2,P_ARM,0,1,8);
 CK(a==1&&b==1&&c==1,"short list (<=16 values): no acceleration even when fast");
 to(S_SPG);W.p[P_GPG]=8;
 CK(call_from(4,P_GLN,20,1,300)==1,"slow SEQ LENGTH = +1");
 a=call_from(4,P_GLN,20,1,8);b=call_from(4,P_GLN,20,1,8);c=call_from(4,P_GLN,20,1,8);
 CK(c>=b&&b>=a&&c>1,"long list accelerates and holds the rate");
 to(S_OSC);U.tab=0;
 {int w[4];W.p[P_OA]=0;W.p[P_OW]=2;widgets(w);CK(w[1]==-1,"VA SAW: no PW knob");W.p[P_OW]=3;widgets(w);CK(w[1]==-1,"VA SQR: no PW knob");W.p[P_OW]=5;widgets(w);CK(w[1]==-1,"VA NOI: no PW knob");
  W.p[P_OW]=4;widgets(w);CK(w[1]==P_OP,"VA PLS: PW knob is there");}   /* PW — только у волны PLS */
 worm_ui_now+=300;worm_ui_enc(3,1);CK(W.p[P_OP]==65,"PW slow +1");
 W.p[P_OP]=40;worm_ui_now+=8;worm_ui_enc(3,1);worm_ui_now+=8;worm_ui_enc(3,1);worm_ui_now+=8;worm_ui_enc(3,1);CK(W.p[P_OP]>43,"PW (0..127) fast: accelerates");
 worm_ui_now+=400;{int v0=W.p[P_OP];worm_ui_enc(3,1);CK(W.p[P_OP]-v0==1,"PW slow after pause: exactly +1");worm_ui_now+=200;v0=W.p[P_OP];worm_ui_enc(3,1);CK(W.p[P_OP]-v0==1,"PW slow (5 clicks/s): +1 per click");}
 to(S_LFO);U.lfo=0;U.dest_show=0;U.dest_sel=0;W.p[P_L0D]=0;
 CK(call(2,P_L0D,1,300)==0&&dest_a2v(U.dest_sel)==1,"DEST slow selects ALL OSC PITCH right after OFF");
 a=dest_a2v(U.dest_sel);worm_ui_now+=8;worm_ui_enc(2,1);worm_ui_now+=8;worm_ui_enc(2,1);
 CK(dest_a2v(U.dest_sel)>a,"DEST accelerates");CK(worm_dest_param(U.dest_sel)!=-2,"DEST not on a header");
 printf(ok?"OK\n":"FAILED\n");return ok?0:1;
}
