/* MRU-кэш банка: (1) пока играет секвенсор, флеш не трогается (стирания/запись), (2) prefetch(pend_pat) -> переключение без чтения флеша,
 * (3) все 3 ячейки грязные + play -> переключение отклоняется, паттерн и правки целы, после STOP всё записывается, (4) данные доезжают.
 * cc -O2 -std=c99 -w -ffp-contract=off -DPLAT_HOST -Iworm -Iworm/worm -I. tests/core/t_store_mru.c worm/worm_wt.c worm/worm_wt_data.c \
 *    worm/worm_wt_main.c worm/worm_synth.c worm/worm_params.c worm/worm_seq.c worm/worm_store.c worm/worm_midi.c div64.c -lm */
#include <stdio.h>
#include <string.h>
#include "worm.h"
static uint8_t nor[1<<20];static int erases,writes,reads;
int worm_flash_read(uint32_t a,void*b,uint32_t l){reads++;memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){writes++;for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){erases++;memset(nor+a,0xFF,l);return 0;}
extern uint32_t worm_ui_now;
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static void put(int note){memset(W.pats[0].s,0,sizeof W.pats[0].s);for(int j=0;j<8;j++){worm_step_init(&W.pats[0].s[j*2]);W.pats[0].s[j*2].on=1;W.pats[0].s[j*2].note=(uint8_t)note;}}
static int get(void){return W.pats[0].s[0].on?W.pats[0].s[0].note:-1;}
static void flushall(void){for(int i=0;i<40;i++){worm_ui_now+=2000;worm_store_flush();}}
int main(void){memset(nor,0xFF,sizeof nor);worm_init();
 W.run=0;W.bank=0;W.slot=0;worm_store_bank_load(0);
 for(int s=0;s<6;s++){worm_store_pat_switch(s);put(40+s);}
 worm_store_bank_save(0);worm_store_bank_load(0);worm_store_pat_switch(0);
 int d;
 /* (2) prefetch: игра, pend на паттерн 5 */
 W.run=1;worm_store_pat_switch(1);worm_store_pat_switch(0);
 int e0=erases,w0=writes;
 CK(worm_store_prefetch(5)==1,"prefetch ok");
 int r0=reads;CK(worm_store_pat_switch(5)==0&&get()==45,"switch to prefetched pattern gives its data");
 printf("flash reads at the bar boundary (switch to prefetched): %d (want 0)\n",reads-r0);CK(reads==r0,"no flash reads at the boundary");
 CK(erases==e0&&writes==w0,"no flash writes while running");
 /* (3) три грязных ячейки при play: 4-й паттерн отклоняется */
 worm_store_pat_switch(0);put(60);worm_store_pat_switch(1);put(61);worm_store_pat_switch(2);put(62);   /* 0,1 грязные, 2 текущий */
 int rc=worm_store_pat_switch(3);int n=worm_store_mru_info(&d);
 printf("cache: %d slots, %d dirty; switch to 4th while playing -> %d\n",n,d,rc);
 CK(rc==-1&&W.slot==2&&get()==62,"all dirty + play: switch refused, current pattern and edits intact");
 CK(erases==e0&&writes==w0,"still no flash writes while running");
 W.run=0;CK(worm_store_pat_switch(3)==0&&get()==43,"after STOP the switch works (dirty records flushed on demand)");
 flushall();
 /* (4) всё доехало */
 worm_store_bank_load(0);int exp[6]={60,61,62,43,44,45};
 for(int s=0;s<6;s++){worm_store_pat_switch(s);CK(get()==exp[s],"data survives reload");}
 printf(ok?"STORE MRU OK\n":"STORE MRU FAIL\n");return !ok;}
