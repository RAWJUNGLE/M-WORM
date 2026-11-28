/* Формат банка не изменён: слоты NTRK..NPAT-1 (вне RAM) должны переживать save/load, в т.ч. старого формата V1 (ремап id). */
#include <stdio.h>
#include <string.h>
#include "worm.h"
static uint8_t nor[1<<20];static int erases;
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){erases++;memset(nor+a,0xFF,l);return 0;}
#define BASE 0x97000u
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
/* пишем слот-образ вручную: magic + 16 слотов; слот 7 содержит один шаг с замком id=40 */
static void put(uint32_t magic,int lockid){uint8_t *b=nor+BASE;memset(nor+BASE,0xFF,0x2000);memcpy(b,&magic,4);b+=4;
 for(int s=0;s<NPAT;s++){if(s==7){b[0]=1;b[1]=0;b+=2;uint8_t st[16]={5,1,64,100,2,127,0,0,1,0,0,0,0,lockid,33,0};memcpy(b,st,16);b+=16;}
  else if(s==2){b[0]=1;b[1]=0;b+=2;uint8_t st[13]={9,1,60,90,2,127,0,0,0,0,0,0,0};memcpy(b,st,13);b+=13;}
  else{b[0]=0;b[1]=0;b+=2;}}}
int main(){
 memset(nor,0xFF,sizeof nor);worm_init();
 put(0x574D4235u,40);W.bank=0;worm_store_bank_load(0);
 CK(W.pats[2].s[9].on&&W.pats[2].s[9].note==60,"slot2 loaded to RAM");
 CK((worm_store_bank_used()>>7)&1,"slot7 flagged used (cache)");CK(!((worm_store_bank_used()>>8)&1),"slot8 not used");
 worm_step_init(&W.pats[1].s[4]);W.pats[1].s[4].on=1;W.pats[1].s[4].note=66;
 int e0=erases;CK(worm_store_bank_save(0)==0,"save ok");CK(erases==e0+1,"erased once");
 pattern_t t;CK(worm_store_pat_load(0,7,&t)==0,"pat_load 7");CK(t.s[5].on&&t.s[5].note==64&&t.s[5].nl==1&&t.s[5].lk[0].id==40&&t.s[5].lk[0].v==33,"slot7 preserved byte-exact");
 CK(worm_store_pat_load(0,1,&t)==0&&t.s[4].note==66,"slot1 saved");
 e0=erases;worm_store_bank_save(0);CK(erases==e0,"no erase when unchanged");
 /* старый формат V1: id>=26 сдвигаются на NFM(8) при сохранении хвоста */
 put(0x574D4231u,40);worm_store_bank_load(0);worm_store_bank_save(0);
 CK(worm_store_pat_load(0,7,&t)==0&&t.s[5].lk[0].id==48,"V1 tail remapped 40->48");
 /* пустая/битая флешь: сохранение даёт валидный банк с пустым хвостом */
 memset(nor+BASE,0xFF,0x2000);worm_init();W.bank=0;worm_store_bank_load(0);worm_step_init(&W.pats[3].s[0]);W.pats[3].s[0].on=1;W.pats[3].s[0].note=70;
 CK(worm_store_bank_save(0)==0,"save into blank");worm_store_bank_load(0);CK(W.pats[3].s[0].note==70,"reload from blank-saved");
 printf(ok?"TAIL OK\n":"TAIL FAIL\n");return !ok;}
