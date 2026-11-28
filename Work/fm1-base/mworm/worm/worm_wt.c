/* Хранение пользовательских wavetable из WTABLE.WPT (см. инструмент mworm_tool_*.html).
 * Формат 4К-сектора: 256 Б заголовка ('MWT1', версия, порядковый номер, 7 имён и CRC таблиц),
 * 7 x 512 Б int16 LE и 256 Б 0xFF в хвосте (записи обновлений загрузчика: байты 0xF00..0xF07 каждого сектора XDATA1
 * обязаны оставаться 0xFF). WPT на диске — первые 3840 Б сектора (256 + 7 x 512).
 * 0xF0000 вне OTA/autosave/BLE VM; загрузчик ищет записи только по 0xF00..0xF07, тут они 0xFF. */
#include <string.h>
#include "worm.h"
#include "worm_wt.h"

#ifndef PLAT_HOST
#define WT_NOINIT __attribute__((section(".noinit")))
#else
#define WT_NOINIT
#endif

static WT_NOINIT int16_t wt_cache[WT_USER][WT_N];
static char wt_names[WT_USER][16];
static uint8_t wt_present[WT_USER];

static uint16_t wt_crc(const uint8_t *p, unsigned n)
{
    uint16_t c = 0xFFFFu;
    for (unsigned i = 0; i < n; i++) {
        c ^= (uint16_t)((uint16_t)p[i] << 8);
        for (int k = 0; k < 8; k++) c = (uint16_t)((c & 0x8000u) ? (uint16_t)((c << 1) ^ 0x1021u) : (uint16_t)(c << 1));
    }
    return c;
}
static uint16_t wt_le16(const uint8_t *p) { return (uint16_t)(p[0] | ((uint16_t)p[1] << 8)); }

static int wt_hdr_ok(const uint8_t *h)
{
    return h[0] == 'M' && h[1] == 'W' && h[2] == 'T' && h[3] == '1' &&
           h[4] == 1 && h[5] == WT_USER && wt_crc(h, 254) == wt_le16(h + 254);
}
/* инструмент не пишет порядковый номер: сравниваем CRC слотов — этого достаточно, чтобы
 * не перезаписывать флеш при повторном входе с тем же файлом и подхватить любую замену. */
static int wt_same(const uint8_t *a, const uint8_t *b)
{
    for (unsigned i = 0; i < WT_USER * 2u; i++) if (a[120u + i] != b[120u + i]) return 0;
    return 1;
}

/* заголовок мало: битый слот с валидным CRC заголовка не должен попасть ни в кэш, ни в активную копию */
static int wt_valid(uint32_t base)
{
    uint8_t h[WT_HDR], d[WT_BSZ];
    if (worm_flash_read(base, h, WT_HDR) || !wt_hdr_ok(h)) return 0;
    for (int i = 0; i < WT_USER; i++) {
        const uint8_t *nm = h + 8 + (unsigned)i * 16u;
        if (!nm[0]) continue;
        if (worm_flash_read(base + WT_HDR + (unsigned)i * WT_BSZ, d, WT_BSZ)) return 0;
        if (wt_crc(d, WT_BSZ) != wt_le16(h + 120u + (unsigned)i * 2u)) return 0;
    }
    return 1;
}

static int wt_load(uint32_t base)
{
    uint8_t h[WT_HDR];
    memset(wt_cache, 0, sizeof wt_cache);
    memset(wt_names, 0, sizeof wt_names);
    memset(wt_present, 0, sizeof wt_present);
    if (worm_flash_read(base, h, WT_HDR) || !wt_hdr_ok(h)) return 1;
    for (int i = 0; i < WT_USER; i++) {
        const uint8_t *nm = h + 8 + (unsigned)i * 16u;
        uint8_t b[WT_BSZ];
        if (!nm[0]) continue;
        if (worm_flash_read(base + WT_HDR + (unsigned)i * WT_BSZ, b, WT_BSZ)) continue;
        if (wt_crc(b, WT_BSZ) != wt_le16(h + 120u + (unsigned)i * 2u)) continue;
        { unsigned j; for (j = 0; j < 15u && nm[j] >= 0x20u && nm[j] < 0x7Fu; j++) wt_names[i][j] = (char)nm[j];
          wt_names[i][j] = 0; }   /* имя из 15 байт без NUL: обрезаем мусор за последним печатным символом */
        for (unsigned k = 0; k < WT_N; k++) wt_cache[i][k] = (int16_t)(b[k * 2u] | ((uint16_t)b[k * 2u + 1u] << 8));
        wt_present[i] = 1;
    }
    return 0;
}

void worm_wt_init(void)
{
    /* черновик с диска — самый свежий; если его нет или он бит, берём активную копию.
     * wt_load сам очистит кэш, если обе копии невалидны (.noinit не обнуляется при перезагрузке). */
    if (wt_valid(WT_STAGE)) { (void)wt_load(WT_STAGE); return; }
    (void)wt_load(WT_ACTIVE);
}

int worm_wt_commit(void)
{
    uint8_t src[WT_HDR], dst[WT_HDR], buf[512];
    if (worm_flash_read(WT_STAGE, src, WT_HDR) || !wt_valid(WT_STAGE)) return 1;  /* черновик бит — активную копию не трогаем */
    if (!worm_flash_read(WT_ACTIVE, dst, WT_HDR) && wt_valid(WT_ACTIVE) && wt_same(src, dst)) return 0;
    if (worm_flash_erase(WT_ACTIVE, WT_SEC)) return -1;
    for (unsigned off = 0; off < WT_HDR + WT_USER * WT_BSZ; off += sizeof buf) {
        if (worm_flash_read(WT_STAGE + off, buf, sizeof buf)) return -1;
        if (worm_flash_write(WT_ACTIVE + off, buf, sizeof buf)) return -1;
    }
    return wt_load(WT_ACTIVE);
}

const int16_t *worm_wt_get(int idx)
{
    if (idx >= WT_BUILTIN && idx < WT_BUILTIN + WT_USER) {
        int u = idx - WT_BUILTIN;
        return wt_present[u] ? wt_cache[u] : worm_wt_builtin[u];   /* пустой пользовательский слот -> одноимённая встроенная таблица */
    }
    if (idx < 0 || idx >= WT_BUILTIN) idx = 0;
    return worm_wt_builtin[idx];
}
int worm_wt_used(int idx)
{
    return idx >= WT_BUILTIN && idx < WT_BUILTIN + WT_USER ? wt_present[idx - WT_BUILTIN] : 1;
}
const char *worm_wt_name(int idx)
{
    if (idx >= WT_BUILTIN && idx < WT_BUILTIN + WT_USER) return wt_names[idx - WT_BUILTIN];
    return "";
}
const char *worm_wt_label(int idx)
{
    static const char *const bn[WT_BUILTIN] = {
        "SINE", "PPG", "PROPH", "PWN", "MORPH", "VIRUS", "SYNLP", "SPECT",
        "JUST", "KEEN", "MELLOW", "PD102", "PHANT", "PLAIT", "POWER", "PURE",
        "QUACK", "RING", "ROLAND", "RLYR5", "RLYR7", "SITAR", "SOHL11", "SOHL18",
        "SOHL39", "SOHL51", "SPC05", "SQ8", "SYN104", "SYN133", "SYNTH", "TALK",
        "TELEF", "TIDAL", "TIDY33", "TRANS", "VOCAL", "VOICE", "WOWEE", "ZAP"
    };
    if (idx < 0) idx = 0;
    if (idx < WT_BUILTIN) return bn[idx];
    if (idx < WT_BUILTIN + WT_USER) {
        int u = idx - WT_BUILTIN;
        return wt_present[u] && wt_names[u][0] ? wt_names[u] : bn[u];   /* пустой слот звучит как встроенная таблица */
    }
    return bn[0];
}

/* ---- единая шкала WAVE (см. worm_wt.h) ---- */
int worm_wave_kind(int w)
{
    return w < WAVE_SHAPES ? WK_SHAPE : w < WAVE_USER0 ? WK_MAIN : WK_CYCLE;
}
const int16_t *worm_wave_cycle(int w)
{
    if (w >= WAVE_LIB0 && w <= WAVE_MAX) return worm_wt_get(w - WAVE_LIB0);
    if (w >= WAVE_USER0 && w < WAVE_LIB0) return worm_wt_get(WT_BUILTIN + (w - WAVE_USER0));
    return 0;
}
const char *worm_wave_label(int w)
{
    static const char *const sn[WAVE_SHAPES] = { "SIN", "TRI", "SAW", "SQR", "PLS", "NOI" };
    static const char *const mn[WT_MAIN] = { "PWN", "PPG", "VIRUS", "VOCAL", "MELLO" };
    static char ub[WT_USER][8];
    if (w < 0) w = 0;
    if (w < WAVE_SHAPES) return sn[w];
    if (w < WAVE_USER0) return mn[w - WAVE_MAIN0];
    if (w < WAVE_LIB0) {   /* USER: имя из WPT или U1..U7 */
        int u = w - WAVE_USER0;
        if (wt_present[u] && wt_names[u][0]) return wt_names[u];
        ub[u][0] = 'U'; ub[u][1] = (char)('1' + u); ub[u][2] = 0;
        return ub[u];
    }
    return worm_wt_label(w - WAVE_LIB0);
}
