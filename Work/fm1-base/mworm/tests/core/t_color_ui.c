/* GLO > COLOR: пятая страница настроек, 4 цветовые схемы, применение OCT+ и возврат на прежнюю страницу секвенсора. */
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
static uint32_t B, K, DB, DK, BB, BK;
static void tick(uint32_t ms){worm_ui_now += ms;worm_ui_lights(&B, &K, &DB, &DK, &BB, &BK);}
static int keyof(int m){for (int i = 0; i < 27; i++) if (krole(i) == m) return i; return -1;}
static void press(int b){worm_ui_button(b, 1);worm_ui_button(b, 0);}

int main(void)
{
    memset(nor, 0xFF, sizeof nor); worm_init(); worm_ui_init(); worm_ui_now = 1000; tick(30);
    CK(W.theme == 0, "default color scheme = DG1");

    /* SEQ PAGE, страница 3: оттуда открываем GLO и должны вернуться именно сюда */
    to(S_SPG); W.p[P_GPG] = 4; W.p[P_GVP] = 3; U.tab = 2; tick(30);
    press(7); tick(30);
    CK(U.scr == S_SET && U.tab == 0 && U.set_prev == S_SPG && U.set_tab == 2, "GLO remembers previous screen and page");
    CK(ntabs() == 5, "GLO has five pages: AU MI SP USB CO");

    /* удержанный GLO + белая клавиша 5 = COLOR */
    worm_ui_button(7, 1);
    for (int i = 0; i < 35; i++) tick(20);
    CK(U.hold_op, "held GLO shows pages");
    int k = keyof(4); worm_ui_key(k, 1); worm_ui_key(k, 0); tick(30);
    CK(U.scr == S_SET && U.tab == 4, "white key 5 opens COLOR page");
    CK(BK & (1u << keyof(4)), "active COLOR page breathes like other pages");
    worm_ui_button(7, 0); tick(30);
    worm_ui_render(fb);   /* страница рисуется */

    /* SELECT: схема применяется сразу (предпросмотр), OCT+ сохраняет */
    worm_ui_enc(0, 1); CK(U.confirm == 7 && W.theme == 1, "SELECT: DG2 live");
    worm_ui_enc(0, 1); CK(W.theme == 2, "SELECT: MD live");
    worm_ui_enc(0, 1); CK(W.theme == 3, "SELECT: MM live");
    worm_ui_enc(0, 1); CK(W.theme == 3, "COLOR clamped at MM");
    worm_ui_enc(0, -1); worm_ui_enc(0, -1); worm_ui_enc(0, -1); CK(W.theme == 0 && !U.confirm, "COLOR clamped at DG1, back to saved = no pending");
    worm_ui_enc(0, 1); worm_ui_enc(0, 1); CK(W.theme == 2, "back to MD");
    press(1); tick(30);
    CK(W.theme == 2 && !U.confirm, "OCT+ keeps MD");
    CK(U.scr == S_SPG && U.tab == 2, "back to previous sequencer page after theme change");

    /* тема сохраняется в проекте и переживает перезагрузку */
    W.theme = 0; CK(worm_store_proj_load() == 0 && W.theme == 2, "theme persisted in project");

    /* OCT- отменяет: предпросмотр MM откатывается на сохранённую MD */
    press(7); tick(30); k = keyof(4);
    worm_ui_button(7, 1); for (int i = 0; i < 35; i++) tick(20);
    worm_ui_key(k, 1); worm_ui_key(k, 0); worm_ui_button(7, 0); tick(30);
    CK(U.scr == S_SET && U.tab == 4, "COLOR opened again");
    worm_ui_enc(0, 1); CK(U.confirm == 7 && W.theme == 3, "MM live preview");
    press(0); tick(30);
    CK(W.theme == 2 && !U.confirm && U.scr == S_SPG && U.tab == 2, "OCT- reverts to MD and returns to the same page");

    printf("COLOR UI %s\n", ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}
