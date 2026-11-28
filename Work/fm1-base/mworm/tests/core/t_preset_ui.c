/* PRESET p-lock с зажатым триггером: STEP SEQUENCER и обычный режим. */
#include <stdio.h>
#include <string.h>
#include "worm_ui.c"
static uint8_t nor[1 << 20];
int worm_flash_read(uint32_t a, void *b, uint32_t l){memcpy(b, nor + a, l); return 0;}
int worm_flash_write(uint32_t a, const void *b, uint32_t l){for (uint32_t i = 0; i < l; i++) nor[a + i] &= ((const uint8_t *)b)[i]; return 0;}
int worm_flash_erase(uint32_t a, uint32_t l){memset(nor + a, 0xFF, l); return 0;}
static int ok = 1;
#define CK(c, m) do { if (!(c)) { printf("FAIL %s\n", m); ok = 0; } } while (0)

static void check_lock(const char *tag, int scr_before, int want, int pre_before)
{
    step_t *s = &PT()->s[0];
    int v = -1;
    CK(worm_step_lock_get(s, P_PSTEP, &v) && v == want, tag);
    CK(s->on, "step on");
    CK(U.scr == S_PRE && U.pre_step, "step preset popup shown");
    CK(U.pre == pre_before, "channel preset NOT changed by step lock");
    /* 2 с: окно закрывается и возвращает на предыдущее меню */
    worm_ui_now += 1500; { uint32_t a,b,c,d,e,f; worm_ui_lights(&a,&b,&c,&d,&e,&f); }
    CK(U.scr == S_PRE, "popup still open before 2 s");
    worm_ui_now += 600; { uint32_t a,b,c,d,e,f; worm_ui_lights(&a,&b,&c,&d,&e,&f); }
    CK(U.scr == scr_before, "popup closed after 2 s, back to previous screen");
    worm_ui_key(0, 0);
    CK(U.hold == -1 && U.hold_k == -1 && !U.lk, "held key released");
    CK(s->on, "step stays on after release");
}

int main(void)
{
    memset(nor, 0xFF, sizeof nor); worm_init(); worm_ui_init(); worm_ui_now = 1000;
    int scr = U.scr, pre0 = U.pre;
    seq_enter();
    worm_ui_key(0, 1); worm_ui_enc(6, 1);
    check_lock("REC: P_PSTEP locked", scr, 1, pre0);
    seq_exit(); scr = U.scr;
    worm_ui_key(0, 1); CK(U.hold_k == 0, "normal: trigger held");
    worm_ui_enc(6, 1);
    check_lock("normal: P_PSTEP locked", scr, 2, pre0);
    /* без триггера: меняется звук дорожки, окно тоже закрывается через 2 с */
    worm_ui_enc(6, 1); CK(U.scr == S_PRE && !U.pre_step, "no trigger: preset screen");
    CK(U.pre == pre0 + 1, "no trigger: channel preset changed");
    worm_ui_now += 2100; { uint32_t a,b,c,d,e,f; worm_ui_lights(&a,&b,&c,&d,&e,&f); }
    CK(U.scr == scr, "no trigger: popup closes after 2 s");
    /* GLO: USB-выход выбран ручкой, OCT+ применяет и сразу закрывает окно (без таймера) */
    { to(S_NOTE); U.set_prev=U.scr; U.set_tab=U.tab; to(S_SET); U.tab=0;
      worm_ui_enc(0,1); CK(U.scr==S_SET && U.confirm==3, "GLO: pending USB out");
      worm_ui_button(1,1); worm_ui_button(1,0);
      CK(U.scr==S_NOTE && !U.confirm, "GLO closes right after OCT+"); }
    /* MICROTIMING: зажать триггер, OCT+/- двигают шаг, окно на время удержания */
    { to(S_NOTE); seq_enter(); int sc=U.scr; step_t *s=&PT()->s[0]; s->mt=0;
      worm_ui_key(0,1); worm_ui_button(1,1); worm_ui_button(1,0); worm_ui_button(1,1); worm_ui_button(1,0); worm_ui_button(0,1); worm_ui_button(0,0);
      CK(s->mt==1, "mt: +2 -1 = +1"); CK(U.scr==S_MT && U.mt_i==0, "mt popup shown"); CK(U.oct==0, "octave untouched");
      { uint32_t a,b,c,d,e,f; worm_ui_lights(&a,&b,&c,&d,&e,&f); CK(U.scr==S_MT, "popup while held"); }
      worm_ui_key(0,0); { uint32_t a,b,c,d,e,f; worm_ui_lights(&a,&b,&c,&d,&e,&f); }
      CK(U.scr==sc, "popup closes on release"); CK(PT()->s[0].mt==1, "mt kept");
      worm_ui_key(0,1); for(int i=0;i<130;i++){worm_ui_button(1,1);worm_ui_button(1,0);} CK(PT()->s[0].mt==100,"mt clamp +100%");
      worm_ui_key(0,0);
      { uint8_t fb[240*240]; worm_ui_key(0,1); worm_ui_button(0,1); worm_ui_button(0,0); worm_ui_render(fb); worm_ui_key(0,0); } }
    /* NOTE TRIG + HOME: обзор микротайминга; отпустил HOME — обычный экран */
    { uint8_t fb[240*240]; to(S_NOTE); seq_enter(); worm_ui_button(8,1);
      CK(mt_view(), "HOME in NOTE TRIG: overview"); worm_ui_render(fb); worm_ui_button(8,0);
      CK(!mt_view() && U.scr==S_NOTE, "HOME released: back"); to(S_OSC); worm_ui_button(8,1); CK(!mt_view(), "no overview outside NOTE TRIG");
      to(S_NOTE); CK(!mt_view(), "HOME held while entering NOTE TRIG: no overview"); worm_ui_button(8,0);
      worm_ui_button(8,1); CK(mt_view(), "HOME pressed inside NOTE TRIG: overview"); worm_ui_button(8,0);
      seq_exit(); to(S_NOTE); worm_ui_button(8,1); CK(!mt_view(), "no REC: no overview"); worm_ui_button(8,0);
      worm_ui_key(0,1); worm_ui_button(1,1); worm_ui_button(1,0); CK(U.scr!=S_MT && U.oct==1, "no REC: OCT+ changes octave, no microtiming"); worm_ui_key(0,0); U.oct=0; }
    printf(ok ? "PRESET UI OK\n" : "PRESET UI FAIL\n");
    return !ok;
}
