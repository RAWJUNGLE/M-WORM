/* EDIT: короткое нажатие -> паттерны, удержание 2 c -> банки; выбор белыми клавишами; активный дышит; 10 c -> возврат на прошлое меню;
 * переключение реально меняет секвенцию и не теряет правки; карта слотов переживает перезагрузку банка. */
#include <stdio.h>
#include <string.h>
#include "worm_ui.c"
static uint8_t nor[1<<20];static int erases;
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){erases++;memset(nor+a,0xFF,l);return 0;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static uint32_t B,K,DB,DK,BB,BK;
static void tick(uint32_t ms){worm_ui_now+=ms;worm_ui_lights(&B,&K,&DB,&DK,&BB,&BK);}
static int keyof(int m){for(int i=0;i<27;i++)if(krole(i)==m)return i;return -1;}
static void pickp(int m){worm_ui_button(6,1);worm_ui_button(6,0);tick(30);worm_ui_key(keyof(m),1);worm_ui_key(keyof(m),0);tick(30);}   /* EDIT коротко -> паттерн m */
int main(){memset(nor,0xFF,sizeof nor);worm_init();worm_store_bank_load(0);worm_ui_init();worm_ui_now=1000;tick(30);
 to(S_FLT);tick(30);
 /* 1) короткое EDIT -> паттерны; активный дышит, остальные тускло */
 worm_ui_button(6,1);tick(100);CK(U.scr==S_FLT,"EDIT down alone does not switch yet");worm_ui_button(6,0);tick(30);
 CK(U.scr==S_PTN,"short EDIT opens patterns");CK(BK&(1u<<keyof(0)),"active pattern (A01) breathes");CK(DK&(1u<<keyof(5)),"other patterns dim");
 /* 2) стоим (не играем): выбор переключает сразу и возвращает в прежнее меню; правки не теряются */
 W.pats[0].s[3].on=1;W.pats[0].s[3].note=60;
 worm_ui_key(keyof(5),1);worm_ui_key(keyof(5),0);tick(30);
 CK(U.scr==S_FLT,"pick returns to the previous menu at once");CK(W.slot[0]==5,"stopped: slot switched to 6 at once");CK(!W.pats[0].s[3].on,"pattern 6 is empty");
 W.pats[0].s[7].on=1;W.pats[0].s[7].note=64;
 pickp(0);CK(W.slot[0]==0&&W.pats[0].s[3].on&&W.pats[0].s[3].note==60&&!W.pats[0].s[7].on,"back to pattern 1: its steps are kept");
 pickp(5);CK(W.pats[0].s[7].on&&W.pats[0].s[7].note==64,"pattern 6 edits were kept");
 /* 3) удержание EDIT 2 с -> банки; выбор банка сразу и в прежнее меню */
 worm_ui_button(6,1);tick(1000);CK(U.scr==S_FLT,"not yet at 1 s");tick(1100);CK(U.scr==S_BANK,"hold 2 s opens banks");worm_ui_button(6,0);tick(30);CK(U.scr==S_BANK,"release keeps banks");
 CK(BK&(1u<<keyof(0)),"active bank breathes");
 worm_ui_key(keyof(2),1);worm_ui_key(keyof(2),0);tick(30);CK(W.bank==2,"bank C selected");CK(U.scr==S_FLT,"bank pick returns to the previous menu at once");
 /* 4) без выбора таймаут 10 с по-прежнему возвращает на последнее меню */
 worm_ui_button(6,1);tick(50);worm_ui_button(6,0);tick(30);CK(U.scr==S_PTN,"patterns again");tick(10500);CK(U.scr==S_FLT,"idle 10 s: back to the last menu");
 /* 5) карта слотов хранится в банке: перезагрузка банка 0 вернёт дорожку 0 на паттерн 6 */
 pickp(0);   /* банк C: слот 0 */
 set_bank(0);CK(W.bank==0&&W.slot[0]==5&&W.pats[0].s[7].note==64,"bank 0 remembers track 1 on pattern 6 with its steps");
 CK(worm_store_bank_used()&(1u<<5),"pattern 6 marked used");
 /* 6) энкодер в паттернах листает слоты (стоим: сразу) */
 to(S_FLT);worm_ui_button(6,1);worm_ui_button(6,0);tick(30);worm_ui_enc(0,1);CK(W.slot[0]==6,"encoder next pattern");
 /* 7) на ходу: паттерн ждёт границы такта, банк/паттерн после выбора возвращают в меню */
 W.run=1;to(S_FLT);int cur=W.slot[0];pickp((cur+1)%16);
 CK(U.scr==S_FLT,"running: pattern pick returns to menu at once");CK(W.slot[0]==cur,"running: pattern not switched before the bar");
 {int16_t o[512];for(int i=0;i<400&&W.slot[0]==cur;i++){worm_render(o,256);tick(5);}}
 CK(W.slot[0]==(cur+1)%16,"pattern switched on the bar");W.run=0;
 worm_ui_button(6,1);tick(2100);worm_ui_button(6,0);tick(30);CK(U.scr==S_BANK,"banks open");worm_ui_key(keyof(3),1);worm_ui_key(keyof(3),0);tick(30);CK(W.bank==3&&U.scr==S_FLT,"bank picked at once and menu restored");
 /* 8) GLO: настройки; страницы AUDIO/MIDI; SELECT листает Output; по умолчанию MASTER */
 CK(W.usb_out==0,"default output is MASTER");
 to(S_FLT);worm_ui_button(7,1);worm_ui_button(7,0);tick(30);CK(U.scr==S_SET&&U.tab==0,"GLO opens settings (AUDIO)");
 /* USB-выход переключается ТОЛЬКО по подтверждению OCT+; OCT- и уход из меню — отмена */
 worm_ui_enc(0,1);CK(W.usb_out==0&&U.confirm==3&&U.pending_usb==1,"SELECT: 4 TRACK is pending, not applied");
 worm_ui_enc(0,1);CK(W.usb_out==0&&U.pending_usb==2,"SELECT: FX pending");worm_ui_enc(0,1);CK(U.pending_usb==2,"clamped at FX");
 tick(30);CK((B&(1u<<12))&&(BB&(1u<<13)),"dialog: OCT- lit, OCT+ breathes");
 worm_ui_button(1,1);worm_ui_button(1,0);CK(W.usb_out==2&&!U.confirm&&U.scr==S_SET,"OCT+ applies FX and stays in GLO");
 worm_ui_enc(0,-1);CK(W.usb_out==2&&U.pending_usb==1,"pending 4 TRACK");worm_ui_button(0,1);worm_ui_button(0,0);CK(W.usb_out==2&&!U.confirm&&U.scr==S_SET,"OCT- cancels");
 worm_ui_enc(0,-1);worm_ui_enc(0,1);CK(!U.confirm&&W.usb_out==2,"back to the active one = no dialog");
 worm_ui_enc(0,-1);to(S_FLT);CK(!U.confirm&&W.usb_out==2,"leaving GLO cancels the pending choice");
 worm_ui_button(7,1);worm_ui_button(7,0);tick(30);worm_ui_enc(0,-5);CK(U.pending_usb==0&&U.confirm==3,"clamped at MASTER (pending)");worm_ui_button(1,1);worm_ui_button(1,0);CK(W.usb_out==0,"OCT+ applies MASTER");
 worm_ui_button(7,1);for(int i=0;i<35;i++)tick(20);CK(U.hold_op,"GLO hold shows pages");worm_ui_key(keyof(1),1);worm_ui_key(keyof(1),0);CK(U.tab==1,"page 2 = MIDI");worm_ui_enc(0,1);CK(W.usb_out==0&&!U.confirm,"MIDI page: SELECT does not change Output");worm_ui_button(7,0);
 /* 9) 24-бит мастер и тапы */
 {int16_t o[512];W.usb_out=1;worm_tap_mode(1);worm_render(o,256);const int32_t *q=worm_synth_out24();int bad=0;for(int i=0;i<512;i++)if(q[i]>8388607||q[i]<-8388608)bad=1;CK(!bad,"OUT24 within 24 bit");
  worm_tap_mode(2);worm_render(o,256);worm_tap_mode(0);}
 /* 10) смена паттернов без правок не стирает NOR (раньше каждое переключение, кроме первого, стирало 2 сектора: карта слотов менялась) */
 {set_bank(0);W.run=0;int e0=erases;for(int i=0;i<12;i++)pat_request(0,(W.slot[0]+1)%NPAT);
  CK(erases==e0,"12 pattern switches without edits: no flash erase");
  pat_request(0,10);   /* слот, которого нет у других дорожек (дорожка 1-3 держат 1..3) */
  worm_step_init(&W.pats[0].s[2]);W.pats[0].s[2].on=1;W.pats[0].s[2].note=67;int sl=W.slot[0];
  pat_request(0,(sl+1)%NPAT);CK(erases>e0,"switching away from an edited pattern still saves it");
  pat_request(0,sl);CK(W.pats[0].s[2].on&&W.pats[0].s[2].note==67,"edited pattern comes back intact");}
 printf(ok?"PICK OK\n":"PICK FAIL\n");return !ok;}
