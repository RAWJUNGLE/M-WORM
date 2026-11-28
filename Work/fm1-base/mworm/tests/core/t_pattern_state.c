/* Паттерн = снимок ВСЕГО (ручки, ADSR, LFO, SEND, TRANSIENT, FX, режимы, mute), даже без шагов; новый паттерн = заводское состояние. */
#include <stdio.h>
#include <string.h>
#include "worm.h"
static uint8_t nor[1<<20];
int worm_flash_read(uint32_t a,void*b,uint32_t l){memcpy(b,nor+a,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){for(uint32_t i=0;i<l;i++)nor[a+i]&=((const uint8_t*)b)[i];return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){memset(nor+a,0xFF,l);return 0;}
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static int all_default(void){
 for(int t=0;t<NTRK;t++){for(int i=0;i<NP;i++)if(worm_param_is_track(i)&&W.tr[t].p[i]!=worm_pinfo[i].def)return 0;if(W.tr[t].mute||W.tr[t].naut)return 0;}
 for(int i=0;i<NP;i++)if(!worm_param_is_track(i)&&W.p[i]!=worm_pinfo[i].def&&i!=P_GLN&&i!=P_GDR&&i!=P_GSX&&i!=P_GSW&&i!=P_GPG&&i!=P_GVP)return 0;
 for(int t=0;t<NTRK;t++)for(int j=0;j<NSTEP;j++)if(W.pats[t].s[j].on||W.pats[t].s[j].nl)return 0;
 return 1;}
int main(){memset(nor,0xFF,sizeof nor);worm_init();W.bank=0;W.slot=0;worm_store_bank_load(0);
 /* паттерн 1: шаги + ручки разных групп, на двух дорожках, + FX + mute + automation */
 worm_step_init(&W.pats[0].s[2]);W.pats[0].s[2].on=1;W.pats[0].s[2].note=64;
 W.p[P_F1C]=33;W.p[P_E0A]=77;W.p[P_SRV]=99;W.p[P_TRN]=55;worm_trk_sync();   /* дорожка 1 (W.pat=0) */
 worm_select_track(2);W.p[P_F1C]=11;W.p[P_L0S]=88;W.p[P_SDL]=66;worm_trk_sync();
 W.p[P_XMX]=70;W.p[P_RMX]=60;worm_trk_mute(3,1);
 W.tr[1].naut=1;W.tr[1].aut[0].step=4;W.tr[1].aut[0].id=P_F1C;W.tr[1].aut[0].v=50;
 worm_select_track(0);
 CK(worm_store_bank_used()&1u,"pat 1 used");
 /* -> паттерн 2: должен быть ПОЛНОСТЬЮ пустым и заводским */
 CK(worm_store_pat_switch(1)==0,"switch to 2");
 CK(all_default(),"new pattern 2 is fully default (steps, knobs, ADSR, LFO, sends, FX, mute, aut)");
 CK(W.p[P_F1C]==worm_pinfo[P_F1C].def&&W.p[P_E0A]==worm_pinfo[P_E0A].def&&W.p[P_TRN]==worm_pinfo[P_TRN].def,"working copy default");
 CK(!(worm_store_bank_used()&2u),"pat 2 untouched is not 'used'");
 /* в паттерне 2 крутим ТОЛЬКО ручки, без шагов: должно сохраниться */
 W.p[P_F1R]=101;W.p[P_SCH]=45;W.p[P_RDC]=20;worm_trk_sync();
 CK(worm_store_bank_used()&2u,"knob-only pattern 2 counts as used");
 /* -> 1 и обратно: всё на месте */
 CK(worm_store_pat_switch(0)==0,"back to 1");
 CK(W.pats[0].s[2].on&&W.pats[0].s[2].note==64,"p1 steps");
 CK(W.p[P_F1C]==33&&W.p[P_E0A]==77&&W.p[P_SRV]==99&&W.p[P_TRN]==55,"p1 track1 knobs/ADSR/send/transient");
 CK(W.p[P_F1R]==worm_pinfo[P_F1R].def,"p2 knob did not leak into p1");
 CK(W.tr[2].p[P_F1C]==11&&W.tr[2].p[P_L0S]==88&&W.tr[2].p[P_SDL]==66,"p1 track3 knobs/LFO/send");
 CK(W.p[P_XMX]==70&&W.p[P_RMX]==60,"p1 global FX");CK(W.tr[3].mute==1,"p1 mute");
 CK(W.tr[1].naut==1&&W.tr[1].aut[0].v==50,"p1 automation");
 CK(worm_store_pat_switch(1)==0,"to 2 again");
 CK(W.p[P_F1R]==101&&W.p[P_SCH]==45&&W.p[P_RDC]==20,"p2 knob-only state kept");
 CK(W.p[P_F1C]==worm_pinfo[P_F1C].def&&W.p[P_XMX]==worm_pinfo[P_XMX].def&&W.tr[3].mute==0&&W.tr[1].naut==0,"p2 has nothing of p1");
 /* пустой слот 3 — снова заводской, хотя в p2 были правки */
 CK(worm_store_pat_switch(2)==0&&W.p[P_F1R]==worm_pinfo[P_F1R].def&&W.p[P_SCH]==worm_pinfo[P_SCH].def&&all_default(),"pattern 3 factory-default");
 /* переживает сохранение во флеш и перезагрузку банка */
 worm_store_pat_switch(0);CK(worm_store_bank_save(0)==0,"save");worm_init();W.bank=0;W.slot=0;worm_store_bank_load(0);
 CK(W.p[P_F1C]==33&&W.p[P_E0A]==77&&W.tr[2].p[P_L0S]==88,"p1 after flash reload");
 worm_store_pat_switch(1);CK(W.p[P_F1R]==101&&W.p[P_SCH]==45,"p2 after flash reload");
 printf(ok?"PATSTATE OK\n":"PATSTATE FAIL\n");return !ok;}
