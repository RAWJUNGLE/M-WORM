/* SEL + чёрные клавиши (F#3,G#3,A#3,C#4,D#4,F#4) = MUTE каналов 1-6; SEL + белые клавиши 1-6 выбирают канал; подсветка: белые = каналы, чёрные = мьюты. */
#include <stdio.h>
#include <string.h>
#include "worm.h"
int worm_flash_read(uint32_t a,void*b,uint32_t l){memset(b,0xFF,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){return 0;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
int main(){worm_init();worm_ui_init();static int16_t o[256*2];
 const int blk[4]={1,3,5,8};   /* индексы клавиш: 54,56,58,61 */
 uint32_t b,k,db,dk,bb,bk;
 worm_ui_button(3,1);   /* SEL зажата */
 for(int j=0;j<4;j++){worm_ui_key(blk[j],1);worm_ui_key(blk[j],0);CK(worm_trk_muted(j),"mute on");}
 worm_ui_lights(&b,&k,&db,&dk,&bb,&bk);
 for(int j=0;j<4;j++)CK(dk&(1u<<blk[j]),"muted key dim");
 worm_ui_key(blk[2],1);worm_ui_key(blk[2],0);CK(!worm_trk_muted(2),"toggle off");
 worm_ui_lights(&b,&k,&db,&dk,&bb,&bk);CK(k&(1u<<blk[2]),"unmuted key bright");
 worm_ui_button(3,0);
 /* замьюченный канал не звучит */
 worm_select_track(0);W.p[P_OL]=W.tr[0].p[P_OL]=100;worm_live_on(60,100);for(int i=0;i<10;i++)worm_render(o,256);
 CK(worm_synth_active_trk(0)==0,"muted channel produces no voices");
 worm_trk_mute(0,0);worm_live_on(60,100);for(int i=0;i<4;i++)worm_render(o,256);CK(worm_synth_active_trk(0)>0,"unmuted plays");
 /* каналы 5 и 6: выбор SEL + белые клавиши C4 / D4 (индексы клавиш 7, 9), мьют — D#4 / F#4 (индексы 10, 13) */
 worm_ui_button(3,1);
 {static const int wk[6]={0,2,4,6,7,9};for(int t=0;t<6;t++){worm_ui_key(wk[t],1);worm_ui_key(wk[t],0);CK(W.pat==t&&!W.mid,"SEL + white key selects track 1-6");}}
 {const int blk6[2]={10,13};
  for(int j=0;j<2;j++){worm_ui_key(blk6[j],1);worm_ui_key(blk6[j],0);CK(worm_trk_muted(4+j),"mute track 5/6");}
  worm_ui_lights(&b,&k,&db,&dk,&bb,&bk);
  for(int j=0;j<2;j++)CK(dk&(1u<<blk6[j]),"muted key 5/6 dim");
  worm_ui_key(blk6[0],1);worm_ui_key(blk6[0],0);CK(!worm_trk_muted(4),"toggle track 5 off");
  worm_ui_lights(&b,&k,&db,&dk,&bb,&bk);CK(k&(1u<<blk6[0]),"unmuted key 5 bright");
  CK(!worm_trk_muted(0)&&worm_trk_muted(1)&&!worm_trk_muted(2)&&worm_trk_muted(3),"tracks 1-4 untouched");}
 worm_ui_button(3,0);
 /* HOME + OP-клавиши — MUTE MIDI-дорожек; клавиши T5/T6 в этом режиме не трогают звуковые каналы */
 worm_trk_mute(5,0);worm_ui_button(3,1);worm_ui_button(8,1);worm_ui_key(13,1);worm_ui_key(13,0);worm_ui_button(8,0);worm_ui_button(3,0);
 CK(!worm_trk_muted(5),"HOME held: D#4/F#4 do not mute audio tracks");
 /* дорожка 6 звучит и глушится */
 worm_select_track(5);W.p[P_OL]=W.tr[5].p[P_OL]=100;worm_live_on(64,100);for(int i=0;i<4;i++)worm_render(o,256);CK(worm_synth_active_trk(5)>0,"track 6 plays");
 worm_trk_mute(4,1);worm_select_track(4);W.p[P_OL]=W.tr[4].p[P_OL]=100;worm_live_on(64,100);for(int i=0;i<10;i++)worm_render(o,256);CK(worm_synth_active_trk(4)==0,"muted track 5 silent");
 printf(ok?"MUTE OK\n":"MUTE FAIL\n");return !ok;}
