/* Пользовательские и встроеные wavetable-таблицы.
 * 40 встроенных таблиц из Work/tables лежат в образе (worm_wt_data.c), 7 пользовательских — в XDATA1 0xF0000:
 * два 4К-сектора (черновик с диска и активная копия). На время звука таблицы кэшируются в .noinit. */
#ifndef WORM_WT_H
#define WORM_WT_H
#include <stdint.h>

#define WT_BUILTIN 40
#define WT_USER    7
#define WT_N       256
#define WT_BSZ     (WT_N * 2u)
#define WT_HDR     256u
#define WT_SEC     4096u
#define WT_BASE    0xF0000u          /* XDATA1: вне OTA (0xE0000..0xE5000), autosave (0xE5000..0xE7000),
                                       BLE VM/BTIF (0xE7000..0xEA000) и globals (0xFC000) */
#define WT_STAGE   (WT_BASE)         /* сюда пишет USB-диск как WTABLE.WPT */
#define WT_ACTIVE  (WT_BASE + WT_SEC)

extern const int16_t worm_wt_builtin[WT_BUILTIN][WT_N];

/* ---- WAVE (OSC1/OSC2) — единая шкала для VA и WAVETABLE:
 *   0..5    формы SIN TRI SAW SQR PLS NOI
 *   6..10   MAIN: 5 основных таблиц по 64 кадра (POS = кадр, плавный морфинг между соседними кадрами)
 *   11..17  USER: 7 слотов WTABLE.WPT (пустой слот звучит как одноимённая LIB-таблица), один цикл, POS = BEND
 *   18..57  LIB: 40 встроенных таблиц (кадр 0), один цикл, POS = BEND */
#define WT_MAIN    5
#define WT_FRAMES  64
#define WAVE_SHAPES 6
#define WAVE_MAIN0 WAVE_SHAPES
#define WAVE_USER0 (WAVE_MAIN0 + WT_MAIN)
#define WAVE_LIB0  (WAVE_USER0 + WT_USER)
#define WAVE_MAX   (WAVE_LIB0 + WT_BUILTIN - 1)
enum { WK_SHAPE = 0, WK_MAIN = 1, WK_CYCLE = 2 };
extern const int16_t worm_wt_main[WT_MAIN][WT_FRAMES][WT_N];
int worm_wave_kind(int w);                     /* WK_SHAPE / WK_MAIN / WK_CYCLE */
const int16_t *worm_wave_cycle(int w);         /* один цикл для WK_CYCLE (USER/LIB), иначе NULL */
const char *worm_wave_label(int w);            /* имя (<=5 символов) для подписи WAVE: SIN, PPG, UTBL1, ... */

void worm_wt_init(void);                       /* прочитать активную копию в RAM-кэш (вызывать в app_init) */
int  worm_wt_commit(void);                     /* после выхода из USB-диска: сверить черновик, перенести в активную копию */
const int16_t *worm_wt_get(int idx);           /* 0..39 встроенные, 40..46 пользовательские; пустой слот -> встроенная с тем же номером */
int  worm_wt_used(int idx);                    /* 1 = пользовательский слот заполнен */
const char *worm_wt_name(int idx);             /* имя пользовательского слота (для UI) */
const char *worm_wt_label(int idx);            /* короткое имя таблицы 0..14 для подписи WAVE (пустой слот -> имя встроенной) */

#endif
