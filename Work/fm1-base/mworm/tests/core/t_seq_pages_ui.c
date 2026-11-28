/* Индикатор страниц секвенсора в шапке + всплывающие уведомления SAVED/BANK FULL. */
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
#define PX(x,y) fb[(y) * LCD_W + (x)]

/* x первого квадратика: те же плашки шапки, что и в screen() */
static int page_x(int p)
{
    char b[40];int hx=0,hw;
    hw=tw("T1",1)+4;hx+=hw+2;
    sprintf(b,"PTA01");hw=tw(b,1)+4;hx+=hw+2;
    {int bb=(int)(W.bpm*2.f+0.5f);sprintf(b,"%d.%d",bb/2,(bb%2)*5);hw=tw(b,1)+4;hx+=hw;}
    return hx+2+p*11;                 /* SQ=8, GAP=3 */
}
#define INNER(p) PX(page_x(p)+4,5)
#define OUTLINE(p) PX(page_x(p),2)

static void page_tests(void)
{
    memset(nor,0xFF,sizeof nor);worm_init();worm_ui_init();
    W.bpm=120.f;W.bank=0;W.slot=0;W.mid=0;W.pat=0;W.run=0;W.cur_run=0;W.cur=-1;
    to(S_NOTE);
    memset(&W.pats[0],0,sizeof W.pats[0]);
    W.p[P_GLN]=128;W.p[P_GPG]=8;W.p[P_GVP]=5;              /* редактируем страницу 5 */
    W.pats[0].s[0].on=1;                                  /* стр.1: триггер */
    W.pats[0].s[2*16+4].nl=1;                             /* стр.3: p-lock */
    W.pats[0].s[7*16+1].mt=1;                             /* стр.8: микротайминг */
    W.run=1;W.cur=2*16+5;                                 /* бегунок на стр.3 */
    worm_ui_now=0;worm_ui_render(fb);                     /* фаза мигания 0 */
    CK(OUTLINE(1)==15,"page square outline always drawn");
    CK(INNER(0)==15,"used page filled (trigger)");
    CK(INNER(1)==0,"empty page not filled");
    CK(INNER(2)==0,"playhead page blinks: off phase");
    CK(INNER(6)==0,"page 7 unused");
    CK(INNER(7)==15,"used page filled (microtiming)");
    CK(PX(page_x(4)+4,10)==15,"edited page underlined");
    worm_ui_now=250;worm_ui_render(fb);                   /* фаза мигания 1 */
    CK(INNER(2)==15,"playhead page blinks: on phase");
    CK(INNER(0)==15,"other pages keep static fill");
    /* STOP: курсор REC может продолжать бежать, но мигания нет — он не отражает воспроизведение */
    W.run=0;W.cur_run=1;W.cur=2*16+5;worm_ui_now=250;worm_ui_render(fb);
    CK(INNER(2)==15,"stop: no blinking, static used fill");
    CK(INNER(0)==15,"stop: other pages unchanged");
    W.cur_run=0;W.cur=-1;
    /* страницы за пределами длины дорожки не считаются используемыми */
    W.p[P_GLN]=32;W.run=0;W.cur=-1;worm_ui_render(fb);
    CK(INNER(7)==0,"step beyond sequence length ignored");
    /* MIDI-дорожка: та же полоса, свой паттерн */
    worm_select_mtrack(0);memset(&W.mpats[0],0,sizeof W.mpats[0]);
    W.p[P_GLN]=128;W.p[P_GPG]=8;W.mpats[0].s[2*16+5].on=1;
    W.run=1;W.cur=3*16+2;worm_ui_now=0;worm_ui_render(fb);
    CK(INNER(3)==0 && INNER(2)==15,"MIDI track: own page strip");
    W.run=0;W.cur=-1;W.mid=0;worm_select_track(0);
}

static int dark_pixels(int x,int y,int w,int h)
{
    int n=0;for(int j=0;j<h;j++)for(int i=0;i<w;i++)if(!PX(x+i,y+j))n++;return n;
}

static void popup_tests(void)
{
    to(S_NOTE);U.map=0;
    say(MK_PAT,0,80);int x,y,w,h;msg_box(&x,&y,&w,&h);
    CK(!strcmp(msg_title(),"PATTERN SAVED"),"SAVED title");
    CK(x>=0&&x+w<=LCD_W&&y>=0&&y+h<=LCD_H,"popup inside screen");
    worm_ui_render(fb);
    CK(PX(x,y)==15&&PX(x+w-1,y+h-1)==15,"popup border");
    CK(PX(x+1,y+1)==15,"popup header filled");
    CK(PX(x+2,y+20)==0,"popup body cleared");
    CK(dark_pixels(x+3,y+2,96,8)>20,"popup title text drawn");

    say(MK_FULL,0,40);msg_box(&x,&y,&w,&h);
    CK(!strcmp(msg_title(),"BANK FULL"),"BANK FULL title");
    worm_ui_render(fb);
    CK(PX(x,y)==15&&dark_pixels(x+3,y+2,96,8)>15,"BANK FULL popup drawn");

    U.pre=6;say(MK_PRS,6,120);msg_box(&x,&y,&w,&h);
    CK(!strcmp(msg_title(),"PRESET SAVED"),"PRESET SAVED title");
    worm_ui_render(fb);
    CK(PX(x,y)==15&&dark_pixels(x+3,y+2,96,8)>20,"PRESET SAVED popup drawn");
    U.msg=0;worm_ui_render(fb);
}

int main(void)
{
    page_tests();popup_tests();
    printf(ok?"SEQ PAGES UI OK\n":"SEQ PAGES UI FAIL\n");
    return !ok;
}
