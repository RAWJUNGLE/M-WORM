/* REC: MONO / POLY (C#5 / D#5) листают страницы шагов на ходу; снятие триггера обнуляет шаг целиком (микротайминг, p-lock'и, RETRIG...). */
#include <stdio.h>
#include <string.h>
#include "worm_ui.c"
static uint8_t nor[1 << 20];
int worm_flash_read(uint32_t a, void *b, uint32_t l){memcpy(b, nor + a, l); return 0;}
int worm_flash_write(uint32_t a, const void *b, uint32_t l){for (uint32_t i = 0; i < l; i++) nor[a + i] &= ((const uint8_t *)b)[i]; return 0;}
int worm_flash_erase(uint32_t a, uint32_t l){memset(nor + a, 0xFF, l); return 0;}
static int ok = 1;
#define CK(c, m) do { if (!(c)) { printf("FAIL %s\n", m); ok = 0; } } while (0)
static void tap(int k){worm_ui_key(k, 1);worm_ui_key(k, 0);}
int main(void)
{
    memset(nor,0xFF,sizeof nor);worm_init();worm_ui_init();
    W.mid=0;W.pat=0;W.run=0;to(S_NOTE);
    W.p[P_GPG]=3;W.p[P_GLN]=48;W.p[P_GVP]=1;
    /* вне REC клавиши MONO/POLY — обычные ноты, страница не меняется */
    tap(KEY_POLY);CK(W.p[P_GVP]==1,"outside REC: POLY does not turn pages");
    worm_ui_button(13,1);worm_ui_button(13,0);CK(U.rec,"REC on");
    tap(KEY_POLY);CK(W.p[P_GVP]==2,"POLY -> page 2");
    tap(KEY_POLY);CK(W.p[P_GVP]==3,"POLY -> page 3");
    tap(KEY_POLY);CK(W.p[P_GVP]==3,"POLY clamps at last page");
    tap(KEY_MONO);CK(W.p[P_GVP]==2,"MONO -> page 2");
    tap(KEY_MONO);tap(KEY_MONO);CK(W.p[P_GVP]==1,"MONO clamps at page 1");
    /* шаг ставится на текущей странице */
    tap(KEY_POLY);worm_ui_key(0,1);worm_ui_key(0,0);   /* F3 = шаг 0 страницы 2 */
    CK(W.pats[0].s[16].on,"trigger placed on page 2");
    /* зажатый шаг + смена страницы не переключает триггер при отпускании */
    worm_ui_key(0,1);tap(KEY_POLY);worm_ui_key(0,0);
    CK(W.pats[0].s[16].on&&W.p[P_GVP]==3,"hold + page turn keeps trigger");
    /* снятие триггера: шаг очищается полностью */
    step_t *s=&W.pats[0].s[16];
    s->mt=7;s->rtr=3;s->rtl=2;s->rtv=-5;s->vel=33;s->prob=50;s->cond=4;worm_step_lock_set(s,P_OW,3);worm_step_lock_set(s,P_PSTEP,9);
    worm_step_toggle(s);
    { step_t z; memset(&z,0,sizeof z); CK(!memcmp(s,&z,sizeof z),"removed trigger: step fully zeroed (mt, p-locks, retrig, ...)"); }
    worm_step_toggle(s);
    CK(s->on&&s->mt==0&&s->nl==0&&s->rtr==(uint8_t)W.p[P_RTR],"re-enabled step is clean");
    /* микротайминг: удержание OCT+ даёт автоповтор с разгоном, отпускание — стоп */
    { uint32_t a,b2,c,d2; to(S_NOTE); W.pats[0].s[0].on=1; W.p[P_GVP]=1; W.pats[0].s[0].mt=0;
      worm_ui_now=1000; worm_ui_key(0,1);      /* зажат шаг 0 (F3) */
      worm_ui_button(1,1); CK(PT()->s[U.sel].mt==1,"mt: first press = 1 percent");
      worm_ui_now=1200; worm_ui_lights(&a,&b2,&c,&d2,&a,&b2); CK(PT()->s[U.sel].mt==1,"mt: no repeat before delay");
      for(int t=1400;t<4400;t+=5){worm_ui_now=t;worm_ui_lights(&a,&b2,&c,&d2,&a,&b2);}
      CK(PT()->s[U.sel].mt==100,"mt: repeat accelerates and clamps at +100%");
      worm_ui_button(1,0); { int8_t v=PT()->s[U.sel].mt; worm_ui_now=6000; worm_ui_lights(&a,&b2,&c,&d2,&a,&b2); CK(PT()->s[U.sel].mt==v,"mt: stops after release"); }
      worm_ui_key(0,0); }
    printf(ok?"ok\n":"FAILED\n");return !ok;
}
