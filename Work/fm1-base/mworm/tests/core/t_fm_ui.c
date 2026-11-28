/* UI FM: 7 страниц OSC, схема алгоритма, новый выбор DEST; кадры пишутся в $FMUI_DIR (если задан) как PGM для просмотра. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "worm_ui.c"
static uint8_t nor[1 << 20];
int worm_flash_read(uint32_t a, void *b, uint32_t l){memcpy(b, nor + a, l); return 0;}
int worm_flash_write(uint32_t a, const void *b, uint32_t l){for (uint32_t i = 0; i < l; i++) nor[a + i] &= ((const uint8_t *)b)[i]; return 0;}
int worm_flash_erase(uint32_t a, uint32_t l){memset(nor + a, 0xFF, l); return 0;}
static int ok = 1;
#define CK(c, m) do { if (!(c)) { printf("FAIL %s\n", m); ok = 0; } } while (0)
static uint8_t fb[LCD_W * LCD_H];
static void dump(const char *n){const char *d=getenv("FMUI_DIR");if(!d)return;char p[256];snprintf(p,sizeof p,"%s/%s.pgm",d,n);FILE *f=fopen(p,"wb");if(!f)return;fprintf(f,"P5\n240 240\n255\n");for(int i=0;i<240*240;i++)fputc(fb[i]?255:0,f);fclose(f);}
static void draw(void){worm_full_redraw=1;worm_ui_render(fb);}
int main(void)
{
 memset(nor, 0xFF, sizeof nor); worm_init(); worm_ui_init(); worm_ui_now = 1000;
 setv(P_OA, ALGO_FM);CK(W.p[P_FR2]==3&&W.p[worm_fm_slot[FM_RB]]==24,"entering FM loads FM defaults");
 to(S_OSC);CK(ntabs()==7,"FM OSC has 7 pages");
 for(int t=0;t<7;t++){U.tab=t;draw();char n[16];sprintf(n,"osc%d",t);dump(n);}
 for(int a=0;a<8;a++){U.tab=0;setv(worm_fm_slot[FM_ALGO],a);draw();char n[16];sprintf(n,"alg%d",a);dump(n);}
 int w[4];U.tab=4;widgets(w);CK(w[0]==worm_fm_slot[FM_ATKB]&&w[3]==worm_fm_slot[FM_LEVB],"page 5 = ENV B");
 U.tab=6;widgets(w);CK(w[3]==-1,"last page has 3 params");
 /* DEST-экран: у FM начинается с Algorithm; индексы в патче — старые/новые, не позиции */
 U.dest_lfo=0;U.dest_show=1;U.dest_t0=worm_ui_now;
 U.dest_sel=dest_v2a(0);CK(U.dest_sel==0,"first = Off");U.dest_sel=dest_v2a(1);CK(U.dest_sel==2,"FM: second = Algorithm");draw();dump("dest_fm0");
 U.dest_sel=dest_v2a(12);draw();dump("dest_fm12");
 U.dest_sel=dest_v2a(40);draw();dump("dest_fm40");
 setv(P_OA,ALGO_VA);dest_al=-1;U.dest_sel=dest_v2a(5);draw();dump("dest_va5");
 for(int v=0;v<dest_vn;v++)CK(dest_a2v(dest_v2a(v))==v,"dest order round trip");
 printf(ok?"FMUI OK\n":"FMUI FAIL\n");return !ok;}
