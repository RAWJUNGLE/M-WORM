/* Дифф-тест хранилища: один сценарий на старой (весь банк в RAM) и новой (MRU) реализации -> побайтно одинаковый флеш. Запуск: drv SEED NOPS out.bin */
/* Дифф/стресс-тест хранилища: один сценарий -> образ флеша; сравнивается между старым (весь банк в RAM) и новым (MRU) хранилищем.
 * Режим MODEL (по умолчанию): плюс модель ожидаемого содержимого, перепроверка после перезагрузки. */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "worm.h"
static uint8_t nor[1<<20];static int erases;
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){erases++;memset(nor+a,0xFF,l);return 0;}
extern uint32_t worm_ui_now;
static uint32_t rs=12345;static unsigned rnd(void){rs=rs*1664525u+1013904223u;return rs>>8;}
typedef struct{uint8_t on[4][NSTEP],note[4][NSTEP];}img_t;
static img_t model[3][NPAT];
static void apply(img_t*m){for(int t=0;t<4;t++)for(int j=0;j<NSTEP;j++){worm_step_init(&W.pats[t].s[j]);W.pats[t].s[j].on=m->on[t][j];W.pats[t].s[j].note=m->note[t][j];}}
static int check(img_t*m){for(int t=0;t<4;t++)for(int j=0;j<NSTEP;j++)if(W.pats[t].s[j].on!=m->on[t][j]||(m->on[t][j]&&W.pats[t].s[j].note!=m->note[t][j]))return 0;return 1;}
static void flushall(void){for(int i=0;i<40;i++){worm_ui_now+=2000;worm_store_flush();}}
int main(int argc,char**argv){int seed=argc>1?atoi(argv[1]):1;int nops=argc>2?atoi(argv[2]):400;rs=seed*7919u+1;
 memset(nor,0xFF,sizeof nor);worm_init();int bad=0;
 W.run=0;W.bank=0;W.slot=0;worm_store_bank_load(0);
 for(int op=0;op<nops;op++){
  unsigned r=rnd()%100;
  if(r<45){ /* правка текущего паттерна: разное число шагов -> растёт/усаживается запись */
   img_t*m=&model[W.bank][W.slot];int n=rnd()%70;memset(m,0,sizeof *m);
   for(int k=0;k<n;k++){int t=rnd()%4,j=rnd()%NSTEP;m->on[t][j]=1;m->note[t][j]=(uint8_t)(30+rnd()%60);}
   apply(m);
  }else if(r<85){int s=rnd()%NPAT;if(worm_store_pat_switch(s)<0){printf("seed %d op %d: switch refused\n",seed,op);bad=1;}
   else if(!check(&model[W.bank][W.slot])){printf("seed %d op %d: slot %d content wrong after switch\n",seed,op,s);bad=1;}}
  else if(r<92){flushall();}
  else if(r<96){worm_store_bank_save(W.bank);}
  else {int b=rnd()%3;worm_store_bank_save(W.bank);W.bank=b;worm_store_bank_load(b);
   if(!check(&model[W.bank][W.slot])){printf("seed %d op %d: bank %d slot %d wrong after bank load\n",seed,op,b,W.slot);bad=1;}}
  if(bad)break;}
 worm_store_bank_save(W.bank);
 /* свежая перезагрузка всех банков и паттернов */
 for(int b=0;b<3&&!bad;b++){worm_store_bank_load(b);for(int s=0;s<NPAT;s++){worm_store_pat_switch(s);if(!check(&model[b][s])){printf("seed %d: reload bank %d slot %d mismatch\n",seed,b,s);bad=1;break;}}}
 worm_store_bank_save(W.bank);
 if(argc>3){FILE*f=fopen(argv[3],"wb");fwrite(nor+0x97000,1,3*0x9000,f);fclose(f);}
 printf("seed %d: %s (erases %d)\n",seed,bad?"FAIL":"ok",erases);return bad;}
