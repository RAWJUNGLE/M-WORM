/* RETRIG шага (NOTE TRIG + EDIT): число ударов, наклон velocity, PROB 0/127, MUTE MIDI-дорожки, сохранение во флеш.
 * Проверяется по MIDI-выходу дорожки M1 (worm_midi_out — weak в worm_midi.c, здесь перехвачен). */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "worm.h"
static uint8_t nor[1<<20];
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){memset(nor+a,0xFF,l);return 0;}
static int non,vel[256];
void worm_midi_out(int st,int d1,int d2){(void)d1;if((st&0xF0)==0x90&&d2){if(non<256)vel[non]=d2;non++;}}
static int16_t o[256*2];
static void play_loops(int loops){uint32_t ss=worm_step_len();int fr=(int)(ss*16u*(uint32_t)loops);worm_play();
 for(int f=0;f<fr;f+=256)worm_render(o,256);worm_stop();worm_render(o,256);}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static void setup(int rtr,int rtl,int rtv,int len,int prob){memset(W.mpats,0,sizeof W.mpats);
 step_t *s=&W.mpats[0].s[0];worm_step_init(s);s->on=1;s->note=60;s->vel=100;s->len=(uint8_t)len;s->prob=(uint8_t)prob;s->rtr=(uint8_t)rtr;s->rtl=(uint8_t)rtl;s->rtv=(int8_t)rtv;}
int main(){memset(nor,0xFF,sizeof nor);worm_init();W.bpm=120.f;
 for(int m=0;m<NMTRK;m++)W.mt[m].len=16;
 /* 1/16 на 1/4 = 4 удара за проход, velocity вниз */
 setup(5,3,-30,1,127);non=0;play_loops(2);
 printf("1/16 x 1/4: %d note-on (want 8), vel %d %d %d %d\n",non,vel[0],vel[1],vel[2],vel[3]);
 CK(non==8,"retrig 1/16 hits");CK(vel[0]==100&&vel[3]==40&&vel[1]>vel[2],"retrig velocity slope");
 /* 1/32 на LEN шага (LEN=2 -> 4 шага) = 8 ударов */
 setup(7,0,0,11,127);non=0;play_loops(1);   /* LEN idx 11 = 4 шага */printf("1/32 x LEN(4 steps): %d (want 8)\n",non);CK(non==8,"retrig LEN hits");
 /* OFF = один удар */
 setup(0,3,0,1,127);non=0;play_loops(3);printf("OFF: %d (want 3)\n",non);CK(non==3,"retrig off");
 /* PROB 0 = никогда (раньше иногда играл), PROB 127 = всегда */
 setup(0,0,0,1,0);non=0;play_loops(40);printf("PROB 0: %d (want 0)\n",non);CK(non==0,"prob 0");
 /* MUTE MIDI-дорожки */
 setup(5,3,0,1,127);worm_mtrk_mute(0,1);non=0;play_loops(1);printf("muted M1: %d (want 0)\n",non);CK(non==0,"midi mute");worm_mtrk_mute(0,0);
 /* сохранение: поля ретриггера в свободных битах шага */
 W.bank=0;worm_store_bank_load(0);
 setup(9,5,-63,4,127);step_t *a=&W.pats[1].s[7];worm_step_init(a);a->on=1;a->rtr=6;a->rtl=2;a->rtv=45;a->cond=11;a->len=4;
 worm_store_bank_save(0);memset(W.pats,0,sizeof W.pats);memset(W.mpats,0,sizeof W.mpats);W.bank=1;worm_store_bank_load(1);W.bank=0;worm_store_bank_load(0);
 a=&W.pats[1].s[7];step_t *m=&W.mpats[0].s[0];
 printf("store: trk rtr %d rtl %d rtv %d cond %d len %d | midi rtr %d rtl %d rtv %d\n",a->rtr,a->rtl,a->rtv,a->cond,a->len,m->rtr,m->rtl,m->rtv);
 CK(a->on&&a->rtr==6&&a->rtl==2&&a->rtv==45&&a->cond==11&&a->len==4,"store track step");CK(m->on&&m->rtr==9&&m->rtl==5&&m->rtv==-63&&m->len==4,"store midi step");
 printf(ok?"RETRIG OK\n":"RETRIG FAIL\n");return !ok;}
