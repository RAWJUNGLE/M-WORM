/* BPM хранится в паттерне; сохранение банка/паттернов/пресетов переживает «перезагрузку»; ленивая запись не теряет правки. */
#include <stdio.h>
#include <string.h>
#include "worm.h"
static uint8_t nor[1<<20];
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){memset(nor+a,0xFF,l);return 0;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static void reboot(void){worm_init();worm_store_proj_load();worm_store_bank_load(W.bank);}
static void step(int t,int k,int n){worm_step_init(&W.pats[t].s[k]);W.pats[t].s[k].on=1;W.pats[t].s[k].note=(uint8_t)n;}
int main(void){
 memset(nor,0xFF,sizeof nor);worm_init();W.bank=0;W.slot=0;worm_store_bank_load(0);
 /* паттерн 1: 150 BPM, шаг */
 W.bpm=150.f;step(0,1,60);
 CK(worm_store_pat_switch(1)==0,"switch to 2");
 CK(W.bpm==150.f,"new empty pattern inherits tempo");
 CK(!(worm_store_bank_used()&2u),"inherited tempo alone does not mark pattern used");
 W.bpm=90.5f;                                   /* только темп, без шагов */
 CK(worm_store_bank_used()&2u,"tempo-only edit marks pattern used");
 CK(worm_store_pat_switch(2)==0,"switch to 3");W.bpm=174.f;step(1,2,64);
 CK(worm_store_pat_switch(0)==0,"back to 1");
 CK(W.bpm==150.f,"pattern 1 tempo 150");
 CK(worm_store_pat_switch(1)==0&&W.bpm==90.5f,"pattern 2 tempo 90.5 (tempo only)");
 CK(worm_store_pat_switch(2)==0&&W.bpm==174.f&&W.pats[1].s[2].on,"pattern 3 tempo 174");
 /* SAVE + «перезагрузка»: темп каждого паттерна из флеша */
 W.slot=2;CK(worm_store_bank_save(0)==0,"save");CK(worm_store_proj_save()==0,"proj save");
 reboot();
 CK(W.slot==2&&W.bpm==174.f,"after reboot: slot 3 tempo 174");
 CK(worm_store_pat_switch(0)==0&&W.bpm==150.f&&W.pats[0].s[1].note==60,"after reboot: pattern 1 tempo 150");
 CK(worm_store_pat_switch(1)==0&&W.bpm==90.5f,"after reboot: pattern 2 tempo 90.5");
 CK(worm_store_pat_switch(5)==0&&W.bpm==90.5f,"untouched pattern keeps current tempo");
 /* ленивая запись: правка раннего паттерна ПОСЛЕ частичной записи не должна теряться */
 memset(nor,0xFF,sizeof nor);worm_init();W.bank=0;W.slot=0;worm_store_bank_load(0);
 W.run=0;worm_ui_now=10000;step(0,0,50);worm_store_pat_switch(1);       /* грязный банк */
 worm_ui_now=12000;worm_store_flush();                                  /* записан 1-й сектор */
 W.bpm=111.f;worm_store_pat_switch(0);                                  /* правка паттерна 1 (сектор 0) */
 for(int i=0;i<20;i++){worm_ui_now+=2000;worm_store_flush();}
 worm_init();W.bank=0;W.slot=0;worm_store_bank_load(0);
 CK(W.pats[0].s[0].on&&W.pats[0].s[0].note==50,"lazy flush: pattern 1 step");
 worm_store_pat_switch(1);CK(W.bpm==111.f,"lazy flush: pattern 2 tempo 111 reached flash");
 /* пресеты и проект не затирают друг друга */
 {uint8_t v[PRS_NVALS],r[PRS_NVALS];char nm[12];for(int k=0;k<PRS_NVALS;k++)v[k]=(uint8_t)(k*3);
  W.bpm=133.f;W.bank=2;W.slot=3;
  CK(worm_store_preset_save(5,"TESTPRS",v)==0,"preset save");CK(worm_store_proj_save()==0,"proj save 2");
  CK(worm_store_preset_save(6,"SECOND",v)==0,"preset save 2");
  CK(worm_store_preset_read(5,nm,r)==1&&!strcmp(nm,"TESTPRS")&&!memcmp(v,r,PRS_NVALS),"preset 5 survives proj_save + other preset");
  worm_init();CK(worm_store_proj_load()==0&&W.bpm==133.f&&W.bank==2&&W.slot==3,"project survives preset save");}
 printf(ok?"PATBPM OK\n":"PATBPM FAIL\n");return !ok;}
