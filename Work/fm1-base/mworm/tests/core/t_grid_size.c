/* GRID SIZE (полиметер): SEQ -> HOME -> EUCLID -> HOME -> GRID SIZE -> HOME -> SEQ PAGE,
 * своя длина/скорость каждой дорожки, новые условия триггера, очередь паттерна на такт. */
#include <stdio.h>
#include <string.h>
#include "worm_ui.c"
static uint8_t nor[1 << 20];
int worm_flash_read(uint32_t a, void *b, uint32_t l){memcpy(b, nor + a, l); return 0;}
int worm_flash_write(uint32_t a, const void *b, uint32_t l){for (uint32_t i = 0; i < l; i++) nor[a + i] &= ((const uint8_t *)b)[i]; return 0;}
int worm_flash_erase(uint32_t a, uint32_t l){memset(nor + a, 0xFF, l); return 0;}
static int ok = 1;
#define CK(c, m) do { if (!(c)) { printf("FAIL %s\n", m); ok = 0; } } while (0)
static uint8_t fb[LCD_W * LCD_H];

static void press(int b){worm_ui_button(b,1);worm_ui_button(b,0);}

int main(void)
{
    memset(nor,0xFF,sizeof nor);worm_init();worm_ui_init();
    W.bpm=120.f;W.bank=0;W.slot=0;W.mid=0;W.pat=0;W.run=0;W.cur_run=0;W.cur=-1;
    U.chan=0;U.hb=0;U.hold=-1;U.sel=-1;

    /* навигация: SEQ -> SEQ PAGE; HOME -> EUCLID; HOME -> GRID SIZE; HOME -> SEQ PAGE */
    press(11);CK(U.scr==S_SPG,"SEQ opens SEQ PAGE");
    press(8); CK(U.scr==S_EUCL,"first HOME opens EUCLID");
    press(8); CK(U.scr==S_GRIDSZ,"second HOME opens GRID SIZE");
    {int w[4];widgets(w);CK(w[0]==P_GLN&&w[1]==P_GSX&&w[2]==P_GPG&&w[3]==P_GSW,"GRID SIZE knobs: GRID / SPD / PAGES / SWING");}
    worm_ui_render(fb);                         /* экран рисуется без вылета за границы */
    CK(fb[(48-1)*LCD_W+4]==15,"T1 selected block drawn");
    CK(fb[(48-1)*LCD_W+11]==15,"T1 digit stays visible");
    CK(fb[(63-1)*LCD_W+4]==0,"unselected T2 has no block");
    CK(fb[(48-3)*LCD_W+110]==15,"T1 bar outline drawn");
    CK(fb[(63-3)*LCD_W+110]==15,"T2 bar outline drawn");
    CK(fb[(123-2)*LCD_W+111]==15,"T6 bar fill drawn");
    press(8); CK(U.scr==S_SPG,"third HOME returns to SEQ PAGE");
    press(11);CK(U.scr==S_SPG,"SEQ from GRID SIZE cycle still opens SEQ PAGE");
    press(8);press(8);CK(U.scr==S_GRIDSZ,"GRID SIZE reachable again");

    /* полиметер: длина и скорость — свои у каждой дорожки */
    W.tr[0].len=16;W.tr[0].spd=1;W.tr[0].pg=1;
    W.tr[1].len=16;W.tr[1].spd=0;W.tr[1].pg=1;
    worm_select_track(1);U.chan=1;
    CK(W.p[P_GLN]==16&&W.p[P_GSX]==0&&W.p[P_GPG]==1,"selected track loads its own GRID/SPD/PAGES");
    worm_ui_enc(4,1);worm_ui_now+=200;worm_ui_enc(4,1);worm_ui_now+=200;
    worm_ui_enc(4,1);worm_ui_now+=200;                                     /* K3: PAGES 1 -> 4 (лимит длины 64) */
    CK(W.tr[1].pg==4,"PAGES knob edits selected track page count");
    worm_ui_enc(2,-1);worm_ui_now+=200;         /* K1: GRID 64 -> 63 */
    CK(W.tr[1].len==63,"GRID knob edits selected track length");
    CK(W.tr[0].len==16,"other track length unchanged (polymeter)");
    worm_ui_enc(3,1);worm_ui_now+=200;          /* K2: SPD +1 */
    CK(W.tr[1].spd==1,"SPD knob edits selected track speed");
    CK(W.tr[0].spd==1,"other track speed unchanged");
    worm_select_track(0);U.chan=0;
    worm_select_track(1);U.chan=1;
    CK(W.tr[1].len==63&&W.tr[1].spd==1,"GRID/SPD kept after track switch");
    CK(W.p[P_GLN]==63&&W.p[P_GSX]==1,"GRID/SPD reloaded into working copy");
    worm_select_track(0);U.chan=0;

    /* условия триггера: старые A:B + новые 1ST / FILL / 4BAR / NEV */
    CK(worm_cond_hit(0,99),"COND -- always");
    CK(worm_cond_hit(1,0)&&!worm_cond_hit(1,1),"COND 1:2");
    CK(worm_cond_hit(12,0)&&!worm_cond_hit(12,4),"COND 1ST");
    CK(worm_cond_hit(13,3)&&!worm_cond_hit(13,2),"COND FILL");
    CK(worm_cond_hit(14,4)&&!worm_cond_hit(14,5),"COND 4BAR");
    CK(!worm_cond_hit(15,0)&&!worm_cond_hit(15,99),"COND NEV");
    CK(L_COND[12] && !strcmp(L_COND[12],"1ST") && !strcmp(L_COND[15],"NEV"),"new COND labels");

    /* очередь паттерна на такт */
    W.run=1;pat_request(3);CK(pend_pat==3,"pattern queued for next bar");
    worm_ui_render(fb);
    pat_request(W.slot);CK(pend_pat<0,"queue cancelled when same pattern requested");
    W.run=0;

    printf(ok?"GRID SIZE OK\n":"GRID SIZE FAIL\n");return ok?0:1;
}
