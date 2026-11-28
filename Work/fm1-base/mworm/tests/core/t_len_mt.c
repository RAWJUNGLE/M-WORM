/* LEN (длина ноты) и микротайминг в нормальных единицах.
 *  - LEN: индекс 0..135 = 0.06 .. 0.88 шага, затем 1..128 шагов (по одному); длина ноты на MIDI-выходе считается по сэмплам;
 *  - микротайминг: % шага 1x (-100..+100); сдвиг ноты считается по сэмплам;
 *  - банк: старые записи (LEN 0..4 = 1/2/4/8/16 шагов, mt в тиках 1/24) читаются с пересчётом; новые пишутся и читаются точно.
 * Сборка (хост): gcc -O1 -DPLAT_HOST -DFM1_HOST_TEST -I. -Iworm tests/core/t_len_mt.c worm/worm_{params,synth,seq,midi,live,lfx,wt,wt_data,wt_main}.c div64.c -lm
 * (worm_store.c подключён самим тестом: нужны его static-функции упаковки). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "worm.h"
#include "worm_store.c"
static uint8_t nor[1<<20];
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){memset(nor+a,0xFF,l);return 0;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static uint32_t now_fr;static int n_on,n_off;static uint32_t t_on[8],t_off[8];
void worm_midi_out(int st,int d1,int d2){(void)d1;
 if((st&0xF0)==0x90&&d2){if(n_on<8)t_on[n_on]=now_fr;n_on++;}
 else if((st&0xF0)==0x80||((st&0xF0)==0x90&&!d2)){if(n_off<8)t_off[n_off]=now_fr;n_off++;}}
static int16_t o[64*2];
static void run_frames(uint32_t fr){for(uint32_t f=0;f<fr;f+=64){worm_render(o,64);now_fr+=64;}}
/* M1: один шаг (позиция 0) с заданными LEN и mt; играем dur шагов, возвращаем 1x шаг в сэмплах */
static uint32_t play_one(int len,int mt,uint32_t steps){memset(W.mpats,0,sizeof W.mpats);
 step_t *s=&W.mpats[0].s[0];worm_step_init(s);s->on=1;s->note=60;s->vel=100;s->len=(uint8_t)len;s->mt=(int8_t)mt;
 W.mt[0].len=NSTEP;n_on=n_off=0;now_fr=0;
 uint32_t ss=worm_step_len();worm_play();run_frames(ss*steps);worm_stop();worm_render(o,64);return ss;}
static int near(long a,long b,long tol){return labs(a-b)<=tol;}
int main(void){
 memset(nor,0xFF,sizeof nor);worm_init();W.bpm=120.f;
 for(int t=1;t<NTRK;t++)worm_trk_mute(t,1);
 /* --- таблица LEN --- */
 CK(NLEN==136&&worm_pinfo[P_NL].mn==0&&worm_pinfo[P_NL].mx==135,"LEN range 0..135");
 CK(worm_len_x16(0)==1&&worm_len_x16(1)==2&&worm_len_x16(2)==4&&worm_len_x16(4)==8&&worm_len_x16(7)==14,"LEN sub-step values");
 CK(worm_len_x16(8)==16&&worm_len_x16(9)==32&&worm_len_x16(135)==128*16,"LEN 1, 2 ... 128 steps");
 CK(!strcmp(L_LEN[0],"0.063")&&!strcmp(L_LEN[2],"0.25")&&!strcmp(L_LEN[8],"1/16")&&!strcmp(L_LEN[135],"128"),"LEN labels");
 for(int i=1;i<NLEN;i++)if(worm_len_x16(i)<=worm_len_x16(i-1)){CK(0,"LEN strictly increasing");break;}
 CK(worm_pinfo[P_NL].def==9&&worm_len_x16(9)==32,"default LEN = 2 steps (as old LEN=1)");
 { int bad=0;for(int i=0;i<NLEN;i++)if(worm_len_load(worm_len_store(i))!=i)bad++;CK(!bad,"LEN store/load roundtrip, all 136");
   CK(worm_len_load(0)==8&&worm_len_load(1)==9&&worm_len_load(2)==11&&worm_len_load(3)==15&&worm_len_load(4)==23,"old LEN 0..4 = 1/2/4/8/16 steps"); }
 CK(worm_len_samples(135,1000)==128000u&&worm_len_samples(2,1000)==250u&&worm_len_samples(0,1000)==62u,"len samples");
 /* --- микротайминг в мс --- */
 CK(worm_mt_ms10(100,120.f)==1250&&worm_mt_ms10(-50,120.f)==-625&&worm_mt_ms10(0,120.f)==0,"mt percent -> ms (120 BPM: step 125 ms)");
 /* --- звучащая длина на MIDI-выходе --- */
 { static const int L[]={0,4,9,12,135};static const char *N[]={"0.063","0.5","1/8","5","128"};
   for(int i=0;i<5;i++){uint32_t ss=play_one(L[i],0,L[i]==135?140u:12u);
     long want=(long)(worm_len_samples(L[i],ss)*.98f),got=(long)t_off[0]-(long)t_on[0];
     printf("LEN %-4s step: gate %ld samples (want %ld)\n",N[i],got,want);
     CK(n_on>=1&&n_off>=1&&near(got,want,130),"MIDI gate matches LEN"); } }
 /* --- микротайминг: сдвиг ноты --- */
 { uint32_t ss=play_one(9,0,4);long base=t_on[0];
   static const int M[]={-100,-50,-13,25,50,100};
   for(int i=0;i<6;i++){play_one(9,M[i],4);long d=(long)t_on[0]-base,want=(long)((long)M[i]*(long)ss/100);
     printf("mt %+4d%%: note moved %ld samples (want %ld)\n",M[i],d,want);CK(n_on>=1&&near(d,want,130),"mt shifts the note by %% of a step"); } }
 /* --- банк: запись/чтение --- */
 { pattern_t A,B;memset(&A,0,sizeof A);memset(&B,0,sizeof B);uint8_t buf[4096];int bad=0;
   for(int i=0;i<NLEN&&i<NSTEP;i++){step_t *s=&A.s[i];s->on=1;s->note=60;s->vel=100;s->len=(uint8_t)i;s->prob=127;s->mt=(int8_t)(i%2?-(i%101):(i%101));s->rtr=3;s->nl=0;}
   for(int i=0;i<4;i++){step_t *s=&A.s[i];s->len=(uint8_t)(i==0?8:i==1?9:i==2?11:15);s->mt=(int8_t)(i*25);}   /* старая шкала, кратные 25% */
   uint8_t *e=pack_seq(buf,buf+sizeof buf,&A);CK(e!=0,"pack_seq fits");
   if(e&&unpack_seq(buf,e,&B)){for(int i=0;i<NSTEP;i++)if(A.s[i].len!=B.s[i].len||A.s[i].mt!=B.s[i].mt||A.s[i].rtr!=B.s[i].rtr||A.s[i].on!=B.s[i].on)bad++;}else bad=1;
   CK(!bad,"all 128 steps: LEN, mt, retrig survive pack/unpack");
   /* шаг по умолчанию (LEN 2 шага, mt 0) — прежние 13 байт; нестандартный — 15 */
   pattern_t C;memset(&C,0,sizeof C);C.s[0].on=1;C.s[0].note=60;C.s[0].vel=100;C.s[0].len=9;C.s[0].prob=127;
   e=pack_seq(buf,buf+sizeof buf,&C);CK(e-buf==2+13,"default step still 13 bytes");
   C.s[0].len=12;e=pack_seq(buf,buf+sizeof buf,&C);CK(e-buf==2+15,"new LEN: 2 extra bytes"); }
 /* --- старая запись (прошивка до LEN/% ) читается --- */
 { uint8_t old[2+13*3];memset(old,0,sizeof old);old[0]=3;
   /* шаг 0: LEN=3 (8 шагов), mt=+6 тиков (=25%) | шаг 1: LEN=0 (1 шаг), mt=-12 тиков (=-50%) | шаг 2: LEN=1, mt=+3 тика (=13%) */
   static const int lenv[3]={3,0,1},mtv[3]={6,-12,3};
   for(int i=0;i<3;i++){uint8_t *r=old+2+13*i;r[0]=(uint8_t)i;r[1]=1;r[2]=60;r[3]=100;r[4]=(uint8_t)lenv[i];r[5]=127;r[6]=0;r[7]=(uint8_t)(int8_t)mtv[i];r[8]=0;}
   pattern_t D;memset(&D,0,sizeof D);const uint8_t *e=unpack_seq(old,old+sizeof old,&D);
   CK(e==old+sizeof old,"old record parsed to the end");
   CK(D.s[0].len==15&&D.s[0].mt==25,"old LEN 3 -> 8 steps, +6 ticks -> +25%");
   CK(D.s[1].len==8&&D.s[1].mt==-50,"old LEN 0 -> 1 step, -12 ticks -> -50%");
   CK(D.s[2].len==9&&D.s[2].mt==13,"old LEN 1 -> 2 steps, +3 ticks -> +13%");
   printf("old record: LEN idx %d %d %d, mt %d %d %d\n",D.s[0].len,D.s[1].len,D.s[2].len,D.s[0].mt,D.s[1].mt,D.s[2].mt); }
 /* --- старый патч дорожки: LEN=3 (8 шагов) лежал как «не по умолчанию» с индексом 3 --- */
 { int16_t p[NP];worm_patch_default(p);uint8_t buf[256];memset(buf,0,sizeof buf);
   buf[P_NL>>3]|=(uint8_t)(1u<<(P_NL&7));uint8_t *e=put_vi(buf+NPB,3);
   const uint8_t *r=unpack_patch(buf,e,p,1);CK(r==e,"old patch parsed");CK(p[P_NL]==15,"old patch LEN 3 -> 8 steps");
   worm_patch_default(p);p[P_NL]=100;uint8_t b2[2048];uint8_t *e2=pack_patch(b2,b2+sizeof b2,p,1);int16_t q[NP];worm_patch_default(q);
   CK(e2&&unpack_patch(b2,e2,q,1)&&q[P_NL]==100,"new patch LEN 100 roundtrip");
   worm_patch_default(p);e2=pack_patch(b2,b2+sizeof b2,p,1);CK(e2&&!(b2[P_NL>>3]&(1u<<(P_NL&7))),"default LEN is not stored"); }
 /* --- MIDI-дорожка: LEN параметра дорожки (для новых шагов / EUCLID) тоже через банк --- */
 { uint8_t buf[4096];int q=worm_mpos(P_NL);W.mt[1].p[q]=77;uint8_t *e=pack_mtrk(buf,buf+sizeof buf,1);CK(e!=0,"pack_mtrk");
   W.mt[1].p[q]=0;CK(e&&unpack_mtrk(buf,e,1)&&W.mt[1].p[q]==77,"MIDI track LEN 77 roundtrip");
   W.mt[2].p[q]=9;e=pack_mtrk(buf,buf+sizeof buf,2);W.mt[2].p[q]=0;CK(e&&unpack_mtrk(buf,e,2)&&W.mt[2].p[q]==9,"MIDI track default LEN roundtrip"); }
 printf(ok?"LEN/MT OK\n":"LEN/MT FAIL\n");return ok?0:1;}
