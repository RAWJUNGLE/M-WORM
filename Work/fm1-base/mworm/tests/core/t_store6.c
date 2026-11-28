/* 6 дорожек в хранении: дорожки 5-6 переживают переключение паттерна и «перезагрузку» (флеш), а запись без них побайтно та же,
 * что в 4-дорожечной прошивке. Старый банк (argv[1], образ флеша 4-дорожечной прошивки) читается: дорожки 5-6 заводские. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "worm.h"
static uint8_t nor[1<<20];
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){memset(nor+a,0xFF,l);return 0;}
void worm_midi_out(int s,int a,int b){(void)s;(void)a;(void)b;}
#define SWITCH(b) do{worm_store_bank_save(W.bank);W.bank=(b);worm_store_bank_load(b);}while(0)
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
int main(int argc,char**argv){
 /* 1. дорожки 5 и 6 (индексы 4, 5): шаги, патч, mute, automation — через переключение банка и через флеш */
 memset(nor,0xFF,sizeof nor);worm_init();W.bank=0;worm_store_bank_load(0);
 for(int t=4;t<6;t++){for(int k=0;k<6;k++){step_t *s=&W.pats[t].s[k*3+t-4];worm_step_init(s);s->on=1;s->note=(uint8_t)(40+t+k);s->vel=(uint8_t)(80+k);}
  W.tr[t].p[P_OL]=(int16_t)(50+t);W.tr[t].p[P_OW]=(int16_t)(t-2);W.tr[t].len=12;}
 W.tr[5].mute=1;W.tr[4].naut=2;W.tr[4].aut[0].step=1;W.tr[4].aut[0].id=P_OL;W.tr[4].aut[0].v=33;W.tr[4].aut[1].step=5;W.tr[4].aut[1].id=P_OL;W.tr[4].aut[1].v=99;
 W.pat=4;memcpy(W.p,W.tr[4].p,sizeof(int16_t)*NP);W.p[P_GLN]=12;   /* рабочая копия выбранной дорожки */
 SWITCH(1);
 CK(!W.pats[4].s[0].on&&!W.tr[5].mute&&W.tr[4].naut==0&&W.tr[4].p[P_OL]==worm_pinfo[P_OL].def,"bank 1 clean for tracks 5-6");
 SWITCH(0);SWITCH(1);
 worm_init();W.bank=0;worm_store_bank_load(0);                       /* «перезагрузка»: всё из флеша */
 for(int t=4;t<6;t++){for(int k=0;k<6;k++){step_t *s=&W.pats[t].s[k*3+t-4];CK(s->on&&s->note==40+t+k&&s->vel==80+k,"step of track 5/6 survives");}
  CK(W.tr[t].p[P_OL]==50+t,"patch of track 5/6");}
 CK(W.tr[5].mute==1&&W.tr[4].mute==0,"mute of track 6");
 CK(W.tr[4].naut==2&&W.tr[4].aut[1].v==99&&W.tr[4].aut[0].step==1,"automation of track 5");
 CK(W.tr[4].len==12,"length of track 5");
 /* 2. старый банк (4 дорожки): дорожки 1-4 на месте, 5-6 заводские */
 if(argc>1){FILE*f=fopen(argv[1],"rb");CK(f!=0,"open old image");if(f){size_t n=fread(nor,1,sizeof nor,f);CK(n==sizeof nor,"read old image");fclose(f);}
  worm_init();W.bank=0;worm_store_bank_load(0);
  for(int t=0;t<4;t++)for(int k=0;k<8;k++){step_t *s=&W.pats[t].s[k*2+t];CK(s->on&&s->note==48+t*3+k,"old bank: tracks 1-4 intact");}
  for(int t=4;t<6;t++){int n=0;for(int k=0;k<NSTEP;k++)n+=W.pats[t].s[k].on;CK(n==0,"old bank: track 5/6 empty");
   CK(W.tr[t].p[P_OL]==worm_pinfo[P_OL].def&&!W.tr[t].mute&&W.tr[t].naut==0,"old bank: track 5/6 factory sound");}
  SWITCH(1);SWITCH(0);                                               /* и снова читается после записи новой прошивкой */
  for(int t=0;t<4;t++)for(int k=0;k<8;k++){step_t *s=&W.pats[t].s[k*2+t];CK(s->on&&s->note==48+t*3+k,"old bank survives rewrite");}}
 printf(ok?"store6 OK\n":"store6 FAIL\n");return !ok;}
