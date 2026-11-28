/* EUCLID: ставит триггеры сам (на пульсах Бьёрклунда), не зависит от включённых шагов, длины паттерна и направления;
 * E.LIMIT — длина цикла, E.OFS — сдвиг, E.MUTE — тишина, HITS=0 — обычный секвенсор. Звуковая дорожка (перехват note_on) и MIDI-дорожка.
 * Сборка (хост): gcc -O2 -DFM1_HOST_TEST -Iworm -I. -I../include tests/core/t_euclid.c worm/worm_*.c div64.c -lm \
 *                    -Wl,--wrap=worm_synth_note_on_v -o t_euclid */
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "worm.h"
int worm_flash_read(uint32_t a,void*b,uint32_t l){memset(b,0xFF,l);return 0;}
int worm_flash_write(uint32_t a,const void*b,uint32_t l){(void)a;(void)b;(void)l;return 0;}
int worm_flash_erase(uint32_t a,uint32_t l){(void)a;(void)l;return 0;}
extern int __real_worm_synth_note_on_v(const int16_t*,int,int,int,int,int,int);
static int tn,tstep[4096],tnote[4096],tvel[4096],ttrk[4096];
int __wrap_worm_synth_note_on_v(const int16_t*e,int note,int vel,int gate,int trk,int primary,int lv){
 if(primary&&tn<4096){tstep[tn]=W.trk_step[trk];tnote[tn]=note;tvel[tn]=vel;ttrk[tn]=trk;tn++;}
 return __real_worm_synth_note_on_v(e,note,vel,gate,trk,primary,lv);}
static int mn,mstep[4096],mnote[4096];
void worm_midi_out(int st,int d1,int d2){if((st&0xF0)==0x90&&d2&&mn<4096){mstep[mn]=worm_midi_pos(0);mnote[mn]=d1;mn++;}}
static int16_t o[256*2];
static int ok=1;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);ok=0;}}while(0)
static void setp(int id,int v){W.tr[0].p[id]=(int16_t)v;if(!W.mid&&W.pat==0)W.p[id]=(int16_t)v;}
static void clear(void){memset(W.pats,0,sizeof W.pats);memset(W.mpats,0,sizeof W.mpats);}
/* steps ticks of track 0 at 120 BPM (1/16 = 5512 samples) */
static void play_ticks(int steps){worm_play();int fr=(int)(worm_step_len()*(uint32_t)steps)+300;for(int f=0;f<fr;f+=256)worm_render(o,256);worm_stop();worm_render(o,256);}
static void fresh(int len){worm_init();W.bpm=120.f;W.tr[0].len=(int16_t)len;W.mt[0].len=(int16_t)len;tn=mn=0;
 for(int t=1;t<NTRK;t++)worm_trk_mute(t,1);for(int m=1;m<NMTRK;m++)worm_mtrk_mute(m,1);clear();}
/* трек-шаги, на которых сработали триггеры дорожки 0 за первые steps тактов, в виде строки x/. */
static void pat(char*out,int steps){memset(out,'.',steps);out[steps]=0;for(int i=0;i<tn;i++)if(ttrk[i]==0&&tstep[i]<steps)out[tstep[i]]='x';}
int main(void){char s[160];
 /* E(5,16) на пустой дорожке: триггеры расставляет euclid; нота = ROOT дорожки */
 fresh(16);setp(P_EUH,5);setp(P_EUL,16);play_ticks(16);pat(s,16);printf("E(5,16) empty track : %s (%d hits)\n",s,tn);
 CK(!strcmp(s,"x..x..x..x..x..."),"E(5,16) pattern");CK(tn==5,"5 hits");CK(tnote[0]==60&&tvel[0]==100,"defaults from track NOTE TRIG");
 /* цикл LIMIT=8 при длине 16: E(3,8) повторяется дважды */
 fresh(16);setp(P_EUH,3);setp(P_EUL,8);play_ticks(16);pat(s,16);printf("E(3,8) in LEN 16     : %s\n",s);CK(!strcmp(s,"x..x..x.x..x..x."),"E(3,8) twice");
 /* LIMIT не обрезает, а замыкает цикл: E(5,12) на 24 тактах = два одинаковых полуцикла */
 fresh(24);setp(P_EUH,5);setp(P_EUL,12);play_ticks(24);pat(s,24);printf("E(5,12) in LEN 24    : %s\n",s);CK(!strncmp(s,s+12,12)&&tn==10,"E(5,12) cycle of 12");
 /* полиритм: LEN 16, LIMIT 12 — цикл euclid не привязан к длине паттерна */
 fresh(16);setp(P_EUH,5);setp(P_EUL,12);play_ticks(32);printf("E(5,12) in LEN 16 x32 ticks: %d hits (want 13-14)\n",tn);CK(tn==13||tn==14,"polyrhythm: cycle 12 ticks inside LEN 16");
 /* сдвиг: OFS=2 */
 fresh(16);setp(P_EUH,3);setp(P_EUL,8);setp(P_EUO,2);play_ticks(8);pat(s,8);printf("E(3,8) ofs 2         : %s\n",s);CK(!strcmp(s,".x..x.x."),"offset rotates the pattern left by 2");
 /* включённые шаги вне пульсов молчат; заполненный, но выключенный шаг на пульсе играет СВОЮ ноту */
 fresh(16);for(int i=0;i<16;i++){step_t*st=&W.pats[0].s[i];worm_step_init(st);st->on=(i!=3);st->note=(uint8_t)(40+i);st->mt=-50;}
 setp(P_EUH,5);setp(P_EUL,16);play_ticks(16);pat(s,16);printf("on-steps vs euclid   : %s\n",s);CK(!strcmp(s,"x..x..x..x..x..."),"euclid ignores on/off");
 int f3=-1;for(int i=0;i<tn;i++)if(tstep[i]==3)f3=tnote[i];CK(f3==43,"off-but-filled step plays its own note on a pulse");
 /* E.MUTE */
 fresh(16);setp(P_EUH,5);setp(P_EUM,1);play_ticks(16);CK(tn==0,"E.MUTE silences pulses");
 /* HITS=0: обычный секвенсор */
 fresh(16);for(int i=0;i<16;i+=4){step_t*st=&W.pats[0].s[i];worm_step_init(st);st->on=1;st->mt=-50;}play_ticks(16);pat(s,16);printf("HITS=0 (plain)       : %s\n",s);CK(!strcmp(s,"x...x...x...x..."),"HITS=0 plays enabled steps");
 /* направление RANDOM: ритм euclid остаётся ровным, считает такты, а не позиции */
 fresh(16);W.tr[0].dir=3;setp(P_EUH,4);setp(P_EUL,16);play_ticks(64);CK(tn==16,"random direction: 4 hits per 16 ticks, steady");
 /* MIDI-дорожка M1 */
 fresh(16);W.mt[0].p[worm_mpos(P_EUH)]=5;W.mt[0].p[worm_mpos(P_EUL)]=16;play_ticks(16);
 {char m[40];memset(m,'.',16);m[16]=0;for(int i=0;i<mn;i++)m[(mstep[i]+15)%16]='x';   /* note-on идёт с mTIME-задержкой в полшага: позиция уже сдвинулась на 1 */
  printf("MIDI E(5,16) empty   : %s (%d notes)\n",m,mn);CK(mn==5&&!strcmp(m,"x..x..x..x..x..."),"MIDI euclid: E(5,16), 5 notes");CK(mnote[0]==W.mt[0].p[worm_mpos(P_NR)],"MIDI default note");}
 /* UI-функция: HITS=0 -> все такты включены, LIMIT цикл */
 CK(worm_euclid_gate(0,0,16,0,7)==1,"gate: off = always");CK(worm_euclid_gate(3,0,8,0,8)==worm_euclid_gate(3,0,8,0,0),"gate: cycle of limit");
 printf(ok?"EUCLID OK\n":"EUCLID FAIL\n");return ok?0:1;}
