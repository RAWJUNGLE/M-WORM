/* Снятие триггера должно удалять p-lock'и шага (включая пресет на шаге).
 * Проверяем все страницы секвенсора, все звуковые дорожки и MIDI-дорожки. */
#include <stdio.h>
#include <string.h>
#include "worm.h"
static uint8_t nor[1 << 20];
int worm_flash_read(uint32_t a, void *b, uint32_t l){memcpy(b, nor + a, l); return 0;}
int worm_flash_write(uint32_t a, const void *b, uint32_t l){for (uint32_t i = 0; i < l; i++) nor[a + i] &= ((const uint8_t *)b)[i]; return 0;}
int worm_flash_erase(uint32_t a, uint32_t l){memset(nor + a, 0xFF, l); return 0;}
static int ok = 1;
#define CK(c, m) do { if (!(c)) { printf("FAIL %s\n", m); ok = 0; } } while (0)

static void check_step(const char *tag, step_t *s)
{
    int v = -1;
    worm_step_init(s);
    s->on = 1;
    worm_step_lock_set(s, P_OW, 3);
    worm_step_lock_set(s, P_PSTEP, 7);
    CK(s->nl == 2 && worm_step_lock_get(s, P_OW, &v) && v == 3, tag);
    worm_step_toggle(s);
    CK(!s->on, "trigger removed");
    CK(s->nl == 0, "p-lock slots cleared");
    CK(!worm_step_lock_get(s, P_OW, &v) && !worm_step_lock_get(s, P_PSTEP, &v), "no p-locks left");
    worm_step_toggle(s);                       /* повторное включение: чистый шаг */
    CK(s->on && s->nl == 0, "step re-enabled without old p-locks");
    worm_step_toggle(s);
}

int main(void)
{
    memset(nor, 0xFF, sizeof nor); worm_init();
    char tag[64];
    for (int trk = 0; trk < NTRK; trk++)
        for (int page = 0; page < 8; page++) {
            snprintf(tag, sizeof tag, "audio trk %d page %d", trk + 1, page + 1);
            check_step(tag, &W.pats[trk].s[page * 16 + 5]);
        }
    for (int m = 0; m < NMTRK; m++)
        for (int page = 0; page < 8; page++) {
            snprintf(tag, sizeof tag, "MIDI trk %d page %d", m + 1, page + 1);
            check_step(tag, &W.mpats[m].s[page * 16 + 9]);
        }
    /* снятый триггер с p-lock'ами не должен сохраняться как USED шаг */
    { step_t *s = &W.pats[0].s[3]; worm_step_init(s); s->on = 1;
      worm_step_lock_set(s, P_OW, 2); worm_step_toggle(s);
      worm_store_bank_save(0);
      memset(W.pats, 0, sizeof W.pats); W.bank = 0; worm_store_bank_load(0);
      s = &W.pats[0].s[3]; CK(!s->on && s->nl == 0, "removed step not restored");
    }
    printf(ok ? "PLOCK CLEAR OK\n" : "PLOCK CLEAR FAIL\n");
    return !ok;
}
