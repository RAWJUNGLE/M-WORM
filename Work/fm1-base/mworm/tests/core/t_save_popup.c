/* Popup сохранения показывает именно то, что сохранено (паттерн/банк/пресет с номером) всё время показа; пересохранение пишет в тот же слот и не стирает флеш зря. */
#include <stdio.h>
#include <string.h>
#include "worm_ui.c"
static uint8_t nor[1<<20];static int erases;
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){erases++;memset(nor+a,0xFF,l);return 0;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static uint8_t fb[240*240];
static void frames(int n){for(int i=0;i<n;i++)worm_ui_render(fb);}
static void press(int b){worm_ui_button(b,1);worm_ui_button(b,0);}
int main(void){
 memset(nor,0xFF,sizeof nor);worm_init();worm_ui_init();worm_ui_now=1000;W.bank=0;W.slot=0;worm_store_bank_load(0);
 worm_store_pat_switch(5);worm_step_init(&W.pats[0].s[0]);W.pats[0].s[0].on=1;W.pats[0].s[0].note=60;
 press(9);CK(U.scr==S_SAV,"save dialog");press(1);
 CK(U.msg_k==MK_PAT&&U.msg_n==5,"popup: pattern A06");
 char sub[32];
 for(int f=0;f<80;f+=10){frames(10);if(U.msg>0){msg_sub(sub);CK(!strcmp(msg_title(),"PATTERN SAVED")&&!strcmp(sub,"PATTERN A06"),"popup text stable while shown");}}
 /* то же сохранение ещё раз: тот же слот, флеш не стирается, банк не растёт */
 int e0=erases;press(9);press(1);CK(erases==e0,"resave unchanged: no erase");CK(U.msg_k==MK_PAT&&U.msg_n==5,"resave popup A06");
 W.pats[0].s[0].note=62;press(9);press(1);
 CK(worm_store_bank_used()==(1u<<5),"edit overwrote slot 6, no extra slot");
 worm_init();worm_ui_init();W.bank=0;worm_store_proj_load();worm_store_bank_load(0);CK(W.slot==5&&W.pats[0].s[0].note==62,"slot 6 holds latest version");
 /* пресет: в выбранный слот, popup с его номером, повтор без изменений не стирает */
 worm_ui_init();U.pre=5;W.tr[W.pat].p[P_F1C]=33;W.p[P_F1C]=33;
 say_save(MK_PRS,U.pre,120,worm_preset_save_cur(U.pre));
 CK(U.msg_k==MK_PRS&&U.msg_n==5,"popup: preset A06");frames(100);msg_sub(sub);CK(!strcmp(msg_title(),"PRESET SAVED")&&!strcmp(sub,"PRESET A06"),"preset popup text stable");
 e0=erases;CK(worm_preset_save_cur(5)==0&&erases==e0,"resave same preset: no erase");
 W.tr[W.pat].p[P_F1C]=44;W.p[P_F1C]=44;CK(worm_preset_save_cur(5)==0,"resave changed preset");
 {uint8_t v[PRS_NVALS];CK(worm_store_preset_read(5,0,v)==1,"preset 6 present");CK(worm_store_preset_read(6,0,v)==0,"preset 7 not created");}
 say_save(MK_PAT,3,80,-1);CK(!strcmp(msg_title(),"BANK FULL"),"full title");frames(60);msg_sub(sub);CK(!strcmp(sub,"NO SPACE IN BANK"),"full text stable");
 printf(ok?"SAVEPOPUP OK\n":"SAVEPOPUP FAIL\n");return !ok;}
