/* Wavetable: WPT (MWT1) -> NOR -> RAM cache -> worm_wt_get(), including replacement.
 * cc -O2 -std=c99 -w -ffp-contract=off -DPLAT_HOST -Iworm -Iworm/worm tests/core/t_wavetable.c \
 *    worm/worm_wt.c worm/worm_wt_data.c worm/worm_synth.c worm/worm_params.c worm/worm_seq.c \
 *    worm/worm_store.c worm/worm_midi.c worm/worm_ui.c div64.c -lm -o t_wavetable */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include <math.h>
#include "worm.h"
#include "worm_wt.h"

static uint8_t nor[0x100000];
static int bad;
#define CK(c,m) do{if(!(c)){printf("FAIL %s\n",m);bad=1;}}while(0)

int worm_flash_read(uint32_t a, void *b, uint32_t l)
{
    if (a + l > sizeof nor) return -1;
    memcpy(b, nor + a, l); return 0;
}
int worm_flash_write(uint32_t a, const void *b, uint32_t l)
{
    if (a + l > sizeof nor) return -1;
    for (uint32_t i = 0; i < l; i++) nor[a + i] &= ((const uint8_t *)b)[i];
    return 0;
}
int worm_flash_erase(uint32_t a, uint32_t l)
{
    if (a + l > sizeof nor || (a & 0xFFFu)) return -1;
    memset(nor + a, 0xFF, l); return 0;
}

static uint16_t crc16(const uint8_t *p, unsigned n)
{
    uint16_t c = 0xFFFFu;
    for (unsigned i = 0; i < n; i++) {
        c ^= (uint16_t)((uint16_t)p[i] << 8);
        for (int k = 0; k < 8; k++) c = (uint16_t)((c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1));
    }
    return c;
}
static void le16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }

/* тот же формат, что выдаёт инструмент: 256 Б заголовка + 7 x 512 Б; слот 0 = синус, слот 1 = -синус */
static void make_wpt(uint8_t *out, int slot0_amp)
{
    memset(out, 0xFF, 3840);
    memcpy(out, "MWT1", 4); out[4] = 1; out[5] = WT_USER;
    for (int i = 0; i < WT_USER; i++) {
        uint8_t *nm = out + 8 + i * 16;
        uint8_t *d = out + 256 + i * 512;
        if (i == 0) {
            memcpy(nm, "SINE_TEST", 9);
            for (unsigned k = 0; k < WT_N; k++) {
                int v = (int)(slot0_amp * sin(2.0 * 3.141592653589793 * (double)k / 256.0));
                le16(d + k * 2, (unsigned)(v < -32768 ? -32768 : v > 32767 ? 32767 : v));
            }
        } else if (i == 1) {
            memcpy(nm, "NEG_SINE", 8);
            for (unsigned k = 0; k < WT_N; k++) le16(d + k * 2, (unsigned)(int16_t)0);
        }
        uint16_t c = crc16(d, WT_BSZ);
        le16(out + 120 + i * 2, c);
    }
    uint16_t hc = crc16(out, 254);
    le16(out + 254, hc);
}

int main(void)
{
    uint8_t wpt[3840];
    memset(nor, 0xFF, sizeof nor);

    /* пустая флешь: встроенные таблицы доступны, пользовательских нет */
    worm_wt_init();
    CK(worm_wt_get(0) == worm_wt_builtin[0], "builtin 0 without flash");
    CK(worm_wt_get(WT_BUILTIN) == worm_wt_builtin[0], "empty user slot -> builtin fallback");
    CK(!worm_wt_used(WT_BUILTIN), "empty user slot not used");

    make_wpt(wpt, 20000);
    memcpy(nor + WT_STAGE, wpt, sizeof wpt);
    worm_wt_init();
    CK(worm_wt_used(WT_BUILTIN), "user slot 0 loaded");
    CK(!strcmp(worm_wt_name(WT_BUILTIN), "SINE_TEST"), "user slot 0 name");
    CK(worm_wt_used(WT_BUILTIN + 1) && !strcmp(worm_wt_name(WT_BUILTIN + 1), "NEG_SINE"), "user slot 1 loaded");
    CK(worm_wt_get(WT_BUILTIN) != worm_wt_builtin[0], "user table replaces fallback");
    const int16_t *s = worm_wt_get(WT_BUILTIN);
    CK(s[0] == 0 && s[64] > 15000 && s[128] == 0 && s[192] < -15000, "sine table samples");

    /* commit: active = копия черновика; повторный commit с тем же файлом ничего не меняет */
    CK(worm_wt_commit() == 0, "commit ok");
    uint8_t active[3840];
    memcpy(active, nor + WT_ACTIVE, sizeof active);
    CK(!memcmp(active, wpt, sizeof wpt), "active copy matches WPT");
    CK(worm_wt_commit() == 0, "second commit ok");
    CK(!memcmp(nor + WT_ACTIVE, active, sizeof active), "second commit left active unchanged");

    /* новая таблица в черновике -> замена активной */
    make_wpt(wpt, -15000);
    memcpy(nor + WT_STAGE, wpt, sizeof wpt);
    CK(worm_wt_commit() == 0, "replacement commit ok");
    CK(memcmp(nor + WT_ACTIVE, active, sizeof active), "active copy replaced");
    memset(nor + WT_STAGE, 0xFF, 4096);          /* черновик стёрт, активная копия жива */
    worm_wt_init();
    CK(worm_wt_used(WT_BUILTIN) && worm_wt_get(WT_BUILTIN)[0] == 0 && worm_wt_get(WT_BUILTIN)[64] < -10000, "replacement survives reboot");

    /* битой CRC черновик не должен портить активную копию */
    uint8_t active2[3840];
    memcpy(active2, nor + WT_ACTIVE, sizeof active2);
    memset(nor + WT_STAGE, 0xFF, 4096);
    memcpy(nor + WT_STAGE, active2, sizeof active2);
    nor[WT_STAGE + 260] ^= 0x55;
    CK(worm_wt_commit() == 1, "corrupt stage rejected");
    CK(!memcmp(nor + WT_ACTIVE + 256, active2 + 256, 7 * 512), "active data intact after corrupt stage");

    printf(bad ? "WTABLE FAIL\n" : "WTABLE OK\n");
    return bad;
}
