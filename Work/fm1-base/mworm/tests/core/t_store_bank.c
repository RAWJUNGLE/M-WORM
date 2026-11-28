#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "worm.h"
static uint8_t nor[1<<20];static int erases,writes;
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){writes++;for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){erases++;memset(nor+a,0xFF,l);return 0;}
int main(){memset(nor,0xFF,sizeof nor);worm_init();int ok=1;
 /* то же, что делает set_bank() после исправления */
 #define SWITCH(b) do{worm_store_bank_save(W.bank);W.bank=(b);worm_store_bank_load(b);}while(0)
 W.bank=0;worm_store_bank_load(0);
 worm_step_init(&W.pats[0].s[3]);W.pats[0].s[3].on=1;W.pats[0].s[3].note=61;
 SWITCH(1);
 if(W.pats[0].s[3].on){printf("FAIL bank1 not empty\n");ok=0;}
 worm_step_init(&W.pats[2].s[5]);W.pats[2].s[5].on=1;W.pats[2].s[5].note=70;
 SWITCH(0);
 if(!(W.pats[0].s[3].on&&W.pats[0].s[3].note==61)){printf("FAIL bank0 lost\n");ok=0;}
 SWITCH(1);
 if(!(W.pats[2].s[5].on&&W.pats[2].s[5].note==70)){printf("FAIL bank1 lost\n");ok=0;}
 int e0=erases;SWITCH(0);SWITCH(1);SWITCH(0);   /* без изменений: стираний быть не должно */
 printf("erases on unchanged switches: %d (want 0)\n",erases-e0);if(erases!=e0)ok=0;
 printf(ok?"STORE OK\n":"STORE FAIL\n");return !ok;}
