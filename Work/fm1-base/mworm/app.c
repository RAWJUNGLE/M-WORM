/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-WORM */
/* M-WORM app: мост между ядром WORM (worm_*.c — UI + synth + seq + store,
 * перенесён из MVAVE-code) и платформой fm1-base (felucca-bsp).
 *
 * Ядро worm — портируемый C без зависимостей от SDK (worm.h). Здесь только
 * привязка: flash -> plat_data_*, LCD -> lcd_*, input -> plat_*, audio -> app_render.
 */
#include "fastmath.h"
#include "plat.h"
#include "worm/worm.h"
#include "worm/worm_wt.h"
#if FM1B_USB_HOST
extern volatile uint32_t fm1_uh_dbg;      /* felucca-bsp usb_host.c (M-WORM diagnostics) */
#endif

/* --------------------------------------------------------------- flash --- */
/* store пишет в 0x97000.. (ST_BASE) — это data-окно платформы PLAT_DATA_LO..HI (plat.h).
 * plat_data* принимают АБСОЛЮТНЫЕ адреса флеша (раньше тут вычиталось 0x97000: смещение попадало вне окна,
 * чтение/стирание/запись всегда возвращали ошибку — паттерны не сохранялись и не загружались). */
int worm_flash_read(uint32_t addr, void *buf, uint32_t len)
{
    if (!PLAT_DATA_OK(addr, len)) return -1;
    const uint8_t *p = plat_data(addr);
    if (!p) return -1;
    for (uint32_t i = 0; i < len; i++) ((uint8_t *)buf)[i] = p[i];
    return 0;
}

int worm_flash_write(uint32_t addr, const void *buf, uint32_t len)
{
    return plat_data_write(addr, buf, len);
}

int worm_flash_erase(uint32_t addr, uint32_t len)
{
    /* plat_data_erase стирает 4 KiB сектор (адрес выровнен); store стирает по одному сектору (4 KiB) */
    uint32_t i;
    for (i = 0; i < len; i += 4096u) {
        if (plat_data_erase(addr + i)) return -1;
    }
    return 0;
}


/* ----------------------------------------------------------- USB disk --- */
/* FAT12-том поверх NOR: 0x93000..0xDF000 (608 x 512 B). Метаданные (boot/FAT/root) живут в XDATA0
 * 0x93000..0x97000 и генерируются заново при входе в диск; данные — это окно банков 0x97000..0xDF000,
 * один кластер = один 512-байтный сектор. Файлы: BANK_A..H.WPB (по 36 KiB, в каждом 16 паттернов) —
 * точное отображение на секторы, где worm_store хранит банки.
 * Дополнительно том объявляет 104 свободных кластера (52 KiB): macOS при замене файла НЕ пишет
 * поверх старой цепочки, а выделяет новую в свободной области. Цепочка ровно из 72 кластеров
 * раскладывается по секторам окна банка (номер банка — из имени BANKx.WPB в root, либо выбранный
 * на устройстве); короткие цепочки служебных файлов (._*, .fseventsd) отбрасываются.
 * Метаданные на флеш НЕ пишем. */
#if FM1B_MSC
#define MW_VOL_SECTORS  712u               /* 32 служебных + 576 кластеров банков + 8 кластеров WTABLE + 96 виртуальных */
#define MW_DATA_LBA     32u                /* 0x97000 = кластер 2 */
#define MW_ROOT_LBA     31u
#define MW_RESERVED     27u                /* 27 + 2*2 FAT + 1 root = 32 */
#define MW_FAT_SZ       2u
#define MW_BANK_CLUS    72u                /* 36 KiB / 512 B */
#define MW_WT_CLUS      8u                 /* 3840 B WTABLE.WPT = 256 Б заголовка + 7 x 512 Б */
#define MW_VIRT_CLUS    96u                /* 48 KiB «свободного» места под временные файлы ОС;
                                            * всего кластеров 2+576+8+96 = 682 — предел 2-секторного FAT12 */
#define MW_WT_LBA       (MW_DATA_LBA + NBANK * MW_BANK_CLUS)   /* 608 */
#define MW_VIRT_LBA     (MW_WT_LBA + MW_WT_CLUS)               /* 616 */
#define MW_WT_CBASE     (2u + NBANK * MW_BANK_CLUS)            /* 578 — цепочка WTABLE.WPT */
#define MW_VIRT_CBASE   (MW_WT_CBASE + MW_WT_CLUS)             /* 586 — первый свободный кластер */
#define MW_NOR_BASE     0x97000u

static PLAT_POOL uint8_t mw_boot[512], mw_root[512], mw_fat[2 * 512], mw_secbuf[512], mw_wbuf[4096];
static uint32_t mw_wbase;
static uint8_t mw_wdirty, mw_wvalid;
static unsigned mw_bank;                   /* банк-приёмник копии, выбранный на устройстве */
static int mw_mode_applied = 1;            /* 1 = AUDIO+MIDI (значение по умолчанию до загрузки проекта) */

static void mw_le16(uint8_t *p, unsigned v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); }
static void mw_le32(uint8_t *p, uint32_t v) { p[0] = (uint8_t)v; p[1] = (uint8_t)(v >> 8); p[2] = (uint8_t)(v >> 16); p[3] = (uint8_t)(v >> 24); }

static void mw_fat_set(uint8_t *fat, unsigned cl, unsigned val)
{
    unsigned o = (cl * 3u) >> 1;
    if (cl & 1u) { fat[o] = (uint8_t)((fat[o] & 0x0Fu) | ((val & 0x0Fu) << 4)); fat[o + 1] = (uint8_t)(val >> 4); }
    else { fat[o] = (uint8_t)val; fat[o + 1] = (uint8_t)((fat[o + 1] & 0xF0u) | ((val >> 8) & 0x0Fu)); }
}
static unsigned mw_fat_get(unsigned cl)
{
    unsigned o = (cl * 3u) >> 1;
    if (cl & 1u) return (unsigned)((mw_fat[o] >> 4) | (mw_fat[o + 1] << 4));
    return (unsigned)(mw_fat[o] | ((mw_fat[o + 1] & 0x0Fu) << 8));
}

static void mw_disk_init(void)
{
    unsigned cl = 2, f;
    mw_bank = (unsigned)W.bank;
    mw_wvalid = 0; mw_wdirty = 0;
    memset(mw_boot, 0, sizeof mw_boot);
    mw_boot[0] = 0xEB; mw_boot[1] = 0x3C; mw_boot[2] = 0x90;
    memcpy(mw_boot + 3, "MWORMFS0", 8);
    mw_le16(mw_boot + 11, 512);            /* байт на сектор */
    mw_boot[13] = 1;                       /* секторов в кластере */
    mw_le16(mw_boot + 14, MW_RESERVED);
    mw_boot[16] = 2;                       /* две копии FAT */
    mw_le16(mw_boot + 17, 16);             /* корневые записи */
    mw_le16(mw_boot + 19, MW_VOL_SECTORS);
    mw_boot[21] = 0xF8;                    /* media descriptor */
    mw_le16(mw_boot + 22, MW_FAT_SZ);
    mw_le16(mw_boot + 24, 1); mw_le16(mw_boot + 26, 1);
    mw_boot[36] = 0x80; mw_boot[38] = 0x29;
    mw_le32(mw_boot + 39, 0x4D574F52u);    /* volume id */
    memcpy(mw_boot + 43, "M-WORM     ", 11);
    memcpy(mw_boot + 54, "FAT12   ", 8);
    mw_boot[510] = 0x55; mw_boot[511] = 0xAA;

    memset(mw_fat, 0xFF, sizeof mw_fat);
    mw_fat_set(mw_fat, 0, 0xFF8u); mw_fat_set(mw_fat, 1, 0xFFFu);
    memset(mw_root, 0, sizeof mw_root);
    memcpy(mw_root, "M-WORM     ", 11); mw_root[11] = 0x08;   /* volume label */
    for (f = 0; f < NBANK; f++) {
        unsigned n = MW_BANK_CLUS;
        uint8_t *e = mw_root + (f + 1u) * 32u;
        for (unsigned k = 0; k < n; k++) mw_fat_set(mw_fat, cl + k, (k + 1u < n) ? cl + k + 1u : 0xFFFu);
        memcpy(e, "BANK?   WPB", 11); e[4] = (uint8_t)('A' + f);
        e[11] = 0x20;                      /* archive */
        mw_le16(e + 26, cl);
        mw_le32(e + 28, n * 512u);
        cl += n;
    }
    /* WTABLE.WPT: 8 кластеров, ровно 3840 Б (заголовок + 7 таблиц). Раскладка — в mw_vaddr(). */
    {   uint8_t *e = mw_root + (NBANK + 1u) * 32u;
        for (unsigned k = 0; k < MW_WT_CLUS; k++) mw_fat_set(mw_fat, cl + k, (k + 1u < MW_WT_CLUS) ? cl + k + 1u : 0xFFFu);
        memcpy(e, "WTABLE  WPT", 11); e[11] = 0x20;
        mw_le16(e + 26, cl);
        mw_le32(e + 28, MW_WT_CLUS * 480u);   /* 3840 */
        cl += MW_WT_CLUS;
    }
    /* виртуальные кластеры выше банков и WTABLE — все свободны, чтобы ОС могла выделить
     * временный файл. Короткие цепочки служебных файлов отбрасываются в mw_vaddr(). */
    for (; cl < 2u + (MW_BANK_CLUS * NBANK) + MW_WT_CLUS + MW_VIRT_CLUS; cl++) mw_fat_set(mw_fat, cl, 0u);
}

/* виртуальный кластер -> сектор NOR банка или черновика WTABLE.WPT. 0 = мусор ОС: запись отбрасываем.
 * Цепочка ровно из 72 кластеров — банк, из 8 — таблицы; имя в root уточняет, что именно за файл. */
static uint32_t mw_vaddr(unsigned cl)
{
    const unsigned cfirst = MW_WT_CBASE, clast = MW_WT_CBASE + MW_WT_CLUS + MW_VIRT_CLUS;
    unsigned head = cl, prev, c, k = 0, len = 0, owner = mw_bank, nx;
    int found = 0, wt = 0, i;
    for (unsigned guard = 0; guard < clast - cfirst; guard++) {       /* начало цепочки */
        prev = 0;
        for (c = cfirst; c < clast; c++)
            if (mw_fat_get(c) == head) { prev = c; break; }
        if (!prev) break;
        head = prev;
    }
    c = head;                                                          /* позиция и длина цепочки */
    while (c >= cfirst && c < clast && len < clast - cfirst) {
        if (c == cl) { k = len; found = 1; }
        len++;
        nx = mw_fat_get(c);
        if (nx == 0u || nx >= 0xFF8u) break;
        c = nx;
    }
    if (!found) return 0;
    if (len == MW_BANK_CLUS && k < MW_BANK_CLUS) wt = 0;
    else if (len == MW_WT_CLUS && k < MW_WT_CLUS) wt = 1;
    else return 0;                                                     /* короткая цепочка — мусор ОС */
    for (i = 1; i < 16; i++) {                                         /* имя в root уточняет файл */
        const uint8_t *e = mw_root + (unsigned)i * 32u;
        unsigned st;
        if (!e[0] || e[0] == 0xE5u || (e[11] & 0x08u)) continue;
        st = (unsigned)e[26] | ((unsigned)e[27] << 8);
        if (st != head) continue;
        if (e[0] == '_' || e[0] == '.') return 0;                       /* ._*, .fseventsd и т.п. */
        if (e[0] == 'B' && e[1] == 'A' && e[2] == 'N' && e[3] == 'K' &&
            e[4] >= 'A' && e[4] <= 'H') {
            if (wt) return 0;
            owner = (unsigned)(e[4] - 'A');
        } else if (e[0] == 'W' && e[1] == 'T' && e[2] == 'A' && e[3] == 'B' && e[4] == 'L' && e[5] == 'E') {
            if (!wt) return 0;
            return WT_STAGE + (uint32_t)k * 512u;
        } else return 0;
        break;
    }
    if (owner >= NBANK) return 0;
    return MW_NOR_BASE + (owner * MW_BANK_CLUS + k) * 512u;
}

static void mw_disk_wflush(void)
{
    uint32_t n;
    if (!mw_wvalid || !mw_wdirty) return;
    n = sizeof mw_wbuf;
    while (n && mw_wbuf[n - 1] == 0xFF) n--;   /* хвост после стирания уже 0xFF */
    if (!plat_data_erase(mw_wbase) && n) (void)plat_data_write(mw_wbase, mw_wbuf, n);
    mw_wvalid = 0; mw_wdirty = 0;
}

static const uint8_t *mw_disk_read(uint32_t lba)
{
    uint32_t addr;
    const uint8_t *p;
    if (lba >= MW_VOL_SECTORS) { memset(mw_secbuf, 0xFF, sizeof mw_secbuf); return mw_secbuf; }
    if (lba == 0u) return mw_boot;
    if (lba >= MW_RESERVED && lba < MW_RESERVED + MW_FAT_SZ) return mw_fat + (lba - MW_RESERVED) * 512u;
    if (lba >= MW_RESERVED + MW_FAT_SZ && lba < MW_RESERVED + 2u * MW_FAT_SZ) return mw_fat + (lba - MW_RESERVED - MW_FAT_SZ) * 512u;
    if (lba == MW_ROOT_LBA) return mw_root;
    if (lba < MW_DATA_LBA) { memset(mw_secbuf, 0, sizeof mw_secbuf); return mw_secbuf; }
    if (lba >= MW_VIRT_LBA) {
        addr = mw_vaddr(MW_VIRT_CBASE + (lba - MW_VIRT_LBA));
        if (!addr) { memset(mw_secbuf, 0, sizeof mw_secbuf); return mw_secbuf; }
    } else if (lba >= MW_WT_LBA) {
        addr = mw_vaddr(MW_WT_CBASE + (lba - MW_WT_LBA));
        if (!addr) { memset(mw_secbuf, 0, sizeof mw_secbuf); return mw_secbuf; }
    } else {
        addr = MW_NOR_BASE + (lba - MW_DATA_LBA) * 512u;
    }
    p = plat_data(addr);
    if (p) memcpy(mw_secbuf, p, 512); else memset(mw_secbuf, 0xFF, 512);
    if (mw_wvalid && addr >= mw_wbase && addr < mw_wbase + sizeof mw_wbuf) {
        unsigned sub = (unsigned)((addr - mw_wbase) / 512u);
        if (mw_wdirty & (1u << sub)) memcpy(mw_secbuf, mw_wbuf + (addr - mw_wbase), 512);
    }
    return mw_secbuf;
}

static int mw_disk_write(uint32_t lba, const uint8_t *b)
{
    uint32_t addr, base;
    if (lba >= MW_VOL_SECTORS) return 0;
    /* принимаем FAT/ROOT от хоста: без этого удаление файла живёт только в кэше ОС,
     * а следующее чтение FAT снова показывает 0 свободных кластеров (копия банка падает
     * с "недостаточно места"). Помещаем в XDATA0 на время сессии, на NOR не пишем. */
    if (lba >= MW_RESERVED && lba < MW_RESERVED + MW_FAT_SZ) {
        memcpy(mw_fat + (lba - MW_RESERVED) * 512u, b, 512);
        return 0;
    }
    if (lba >= MW_RESERVED + MW_FAT_SZ && lba < MW_RESERVED + 2u * MW_FAT_SZ) {
        memcpy(mw_fat + (lba - MW_RESERVED - MW_FAT_SZ) * 512u, b, 512);
        return 0;
    }
    if (lba == MW_ROOT_LBA) { memcpy(mw_root, b, 512); return 0; }
    if (lba < MW_DATA_LBA) return 0;                  /* boot и прочее не трогаем */
    /* виртуальные кластеры: цепочки банков (72) и WTABLE.WPT (8), мусор ОС — в никуда */
    if (lba >= MW_VIRT_LBA) {
        addr = mw_vaddr(MW_VIRT_CBASE + (lba - MW_VIRT_LBA));
        if (!addr) return 0;
    } else if (lba >= MW_WT_LBA) {
        addr = mw_vaddr(MW_WT_CBASE + (lba - MW_WT_LBA));
        if (!addr) return 0;
    } else {
        addr = MW_NOR_BASE + (lba - MW_DATA_LBA) * 512u;
    }
    base = addr & ~((uint32_t)sizeof mw_wbuf - 1u);
    if (!mw_wvalid || base != mw_wbase) {
        const uint8_t *p;
        mw_disk_wflush();
        mw_wbase = base; mw_wvalid = 1; mw_wdirty = 0;
        p = plat_data(base);
        if (p) memcpy(mw_wbuf, p, sizeof mw_wbuf); else memset(mw_wbuf, 0xFF, sizeof mw_wbuf);
    }
    memcpy(mw_wbuf + (addr - mw_wbase), b, 512);
    mw_wdirty |= (uint8_t)(1u << ((addr - mw_wbase) / 512u));
    if (mw_wdirty == 0xFFu) mw_disk_wflush();
    return 0;
}

static const plat_disk_t mw_disk = {
    MW_VOL_SECTORS, mw_disk_read, mw_disk_write, mw_disk_wflush, "MWORM", "M-WORM PATTERNS"
};

static void mw_usb_mode_poll(void)
{
    if (W.usb_mode == mw_mode_applied) return;
    if (W.usb_mode == 0) {
        worm_stop();                              /* стирание NOR вне аудио-ISR: чтобы не было щелчка */
        (void)worm_store_bank_save(W.bank);
        (void)worm_store_proj_save();
        mw_disk_init();
        plat_disk_mode(&mw_disk);
        app_say("USB DISK", "BANKS");
    } else {
        mw_disk_wflush();
        (void)worm_wt_commit();                   /* подхватить WTABLE.WPT, заменённый на диске */
        (void)worm_store_proj_save();             /* зафиксировать AUDIO+MIDI ДО перечитывания проекта из NOR */
        plat_disk_mode(0);
        (void)worm_store_proj_load();
        (void)worm_store_lm_load();
        W.usb_mode = 1;                           /* выбор пользователя важнее значения из старого проекта */
        (void)worm_store_bank_load(W.bank);       /* подхватить файлы, записанные с компьютера */
        worm_midi_panic();
        app_say("AUDIO + MIDI", "USB READY");
    }
    mw_mode_applied = W.usb_mode;
}
#endif /* FM1B_MSC */


/* --------------------------------------------------------------- audio --- */
/* worm_master определён в worm_synth.c */

/* ---------------------------------------------------------------- LCD ---- */
#ifndef PLAT_HOST
#define MW_POOL __attribute__((section(".pool")))
#else
#define MW_POOL
#endif
static MW_POOL uint32_t ui_fb32[240 * 240 / 4];   /* кадр: яркость 0..15 (4-битные градации серого), байт на пиксель, слова — для быстрого диффа */
#define ui_fb ((uint8_t *)ui_fb32)
static MW_POOL uint32_t ui_prev32[240 * 240 / 4]; /* предыдущий кадр (байт на пиксель, 0..15): дифф-блит сравнивает по 4 пикселя за слово */
#define ui_prev ((uint8_t *)ui_prev32)
static MW_POOL uint16_t strip[2][240 * 8];      /* двойной буфер: DMA читает один, пока заполняем другой */

/* уровень 0..15 -> RGB565. 0 = DG1 (оригинал, белый на чёрном), 1..3 = DG2, MD, MM; — цветовые схемы GLO > COLOR.
 * Формат как у остального блита: белый = 0xFFFF, чёрный = 0x0000. */
static uint16_t gray_lut[16];
/* bg = уровень 0 (фон), ink = уровень 15 (всё остальное). У MD/MM фон светлый, а ink тёмный местами — как в hortator. */
typedef struct { uint8_t bg[3], ink[3]; } theme_t;
static const theme_t THEMES[3] = {
    {{  0,   0,   0}, {229, 242,  28}},   /* DG2: чистый чёрный #000000, жёлтый #E5F21C */
    {{255,   0,   0}, {  0,   0,   0}},   /* MD:  красный #FF0000, чёрный #000000 */
    {{187, 198, 179}, { 29,  42,  30}},   /* MM:  светлый #BBC6B3, тёмный #1D2A1E (скриншот Monomachine) */
};
static int lut_theme = -1;
/* ВАЖНО: lcd_blit отправляет буфер как есть, а ST7789 ждёт старший байт RGB565 первым — значит, значения в gray_lut
 * хранятся с переставленными байтами (так же делает gfx.c: swap16). lcd_fill, наоборот, принимает обычное значение и меняет байты сам.
 * Без этого каналы R/G/B путались (чёрный и белый симметричны, поэтому были правильными, а цветные схемы — нет). */
static inline uint16_t sw16(uint16_t c) { return (uint16_t)((c >> 8) | (c << 8)); }
/* 8 бит -> 5/6 бит с округлением (а не отбрасыванием), чтобы цвет на экране был ближе к заданному */
static inline uint16_t rgb565w(int r, int g, int b)
{
    uint16_t c = (uint16_t)((((r * 31 + 127) / 255) << 11) | (((g * 63 + 127) / 255) << 5) | ((b * 31 + 127) / 255));
    return sw16(c);
}
/* слабая ссылка: если worm_midi.c этой сборки не содержит worm_midi_resend, линковка не падает, вызов пропускается */
extern void worm_midi_resend(void) __attribute__((weak));
#define MIDI_RESEND() do { if (worm_midi_resend) worm_midi_resend(); } while (0)
static void gray_lut_init(int theme)
{
    for (int i = 0; i < 16; i++) {
        if (theme == 0) {                       /* DG1: дословно из резервной копии — чёрный фон, белый текст */
            int v = (i * 31 + 7) / 15;          /* 0..31 (5 бит на R и B) */
            int g = v * 2;                      /* 0..62 (6 бит на G) */
            if (i == 15) { v = 31; g = 63; }    /* полный белый, как у обычного UI (0xFFFF) */
            gray_lut[i] = sw16((uint16_t)((v << 11) | (g << 5) | v));
        } else {                                /* цветная схема: интерполяция bg -> ink по 16 шагов (у RED — от красного к тёмному) */
            const theme_t *t = &THEMES[theme - 1];
            int r = t->bg[0] + (((int)t->ink[0] - t->bg[0]) * i + 7) / 15;
            int g = t->bg[1] + (((int)t->ink[1] - t->bg[1]) * i + 7) / 15;
            int b = t->bg[2] + (((int)t->ink[2] - t->bg[2]) * i + 7) / 15;
            gray_lut[i] = rgb565w(r, g, b);
        }
    }
    lut_theme = theme;
}

#ifdef PLAT_HOST
static void app_uac_stats(uint32_t *u, uint32_t *m, uint32_t *x) { *u = *m = *x = 0; }
#else
extern void fm1_uac_stats(uint32_t *und, uint32_t *miss, uint32_t *max_us);   /* main_fm1.c */
#define app_uac_stats fm1_uac_stats
#endif
/* -------------------------------------------------------------- render --- */
static uint32_t blk_n;   /* номер блока рендера (для app_overload) */
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12)
{
    blk_n++;
    static int16_t mix[512];
    static float master_sm = -1.f;   /* сглаженная громкость (0..1): без щелчков при кручении Master */
    uint32_t i;
    /* master_q12 (0..4096) -> worm_master (0..1), квадрат как в worm_app;
     * плавно подтягиваем, чтобы резкое вращение ручки не давало скачок/щелчок */
    float m = (float)master_q12 / 4096.0f;
    float target = m * m;
    if (master_sm < 0.f) master_sm = target;
    master_sm += (target - master_sm) * 0.02f;   /* ~3 мс сглаживание на блок */
    if (master_sm < 0.f) master_sm = 0.f;
    if (master_sm > 1.f) master_sm = 1.f;
    worm_master = master_sm;
    worm_synth_i16(0);                 /* int16 mix никому не нужен: I2S и USB берут 24-bit из движка */
    worm_tap_mode(W.usb_out);                /* USB-тап: 0 мастер, 1 = дорожки 1-3, 2 = дорожки 4-6 (3 стерео-пары), 3 = FX-возвраты */
    worm_render(mix, n);
    {   /* настоящий 24-bit мастер (Q23) из движка, а не int16 << 8: в I2S и в USB-запись (режим MASTER) */
        const int32_t *o24 = worm_synth_out24();
        /* В режиме OUTPUT=MASTER USB-запись идёт из OUT24 (24 бит); в TRK 1-3 / TRK 4-6 / FX BSP берёт 16-битный тап.
         * Без дизера хвосты (reverb/delay/release) квантуются в «ступеньки» — мелкие артефакты именно в USB, I2S их не слышит.
         * TPDF ±1 LSB16 (= ±256 в 24-бит) на ненулевых кадрах: тишина остаётся цифровым нулём. */
        static uint32_t dth = 2463534242u;
        for (i = 0; i < n; i++) {
            int32_t l = o24[2u * i], r = o24[2u * i + 1u];
            if (l | r) {
                uint32_t a = dth;
                a ^= a << 13; a ^= a >> 17; a ^= a << 5; dth = a;
                l += (int32_t)((a >> 24) & 255u) - (int32_t)((a >> 16) & 255u);
                r += (int32_t)((a >> 8) & 255u) - (int32_t)(a & 255u);
                if (l > 8388607) l = 8388607; else if (l < -8388608) l = -8388608;
                if (r > 8388607) r = 8388607; else if (r < -8388608) r = -8388608;
            }
            lr[2u * i] = l;
            lr[2u * i + 1u] = r;
        }
    }
}

/* USB-аудио: какой набор выдавать и тап-буфер последнего app_render (кадр = 8 слотов int16, BSP берёт первые 6; для MASTER не
 * заполняется). Читает платформа (BSP usb.c). Режимы BSP: 0 = MASTER 24/48, 1 = 8 кан., 2 = 6 кан. 16/44.1. Все три тап-режима
 * приложения (TRK 1-3, TRK 4-6, FX) — это 6 каналов 16/44.1, т.е. режим BSP 2: переключение между ними не перечисляет устройство
 * заново, меняется только содержимое тапа. Режим BSP 1 (8 каналов) не используется. */
uint32_t app_usb_mode(void) { return W.usb_out == 0 ? 0u : 2u; }
const int16_t *app_usb_tap(void) { return worm_synth_tap(); }

void app_panic(void)
{
    worm_stop();
    worm_midi_panic();   /* + All Notes Off / All Sound Off на каналах MIDI-дорожек */
}

/* MIDI-дорожки M1..M4 -> USB MIDI. Пакет USB-MIDI: байт 0 = кабель 0 | CIN (код = старший ниббл статуса), далее статус, данные.
 * plat_midi_out_room = 0, пока никто не слушает (хост не подключён) или очередь полна: сообщение тогда пропускается; при новом
 * подключении (смена plat_midi_out_link) app_frame просит движок заново послать PROG / BANK / VOL / PAN / CC. */
void worm_midi_out(int st, int d1, int d2)
{
    uint32_t cin = ((uint32_t)st >> 4) & 0xFu;
    if (!plat_midi_out_room(TO_USB)) return;
    plat_midi_send(cin | ((uint32_t)(st & 0xFF) << 8) | ((uint32_t)(d1 & 0x7F) << 16) | ((uint32_t)(d2 & 0x7F) << 24), TO_USB);
}

void app_overload(void)
{   /* 4 моно-голоса: гасить нечего. Раньше здесь «сбрасывались» голоса (быстрое затухание 10 мс) — именно оно давало мелкие щелчки
     * в USB-записи, когда нагрузка с USB поднимала xrun. Теперь перегруз не режет звук. */
    (void)plat_xruns();
}

/* -------------------------------------------------------------- engine --- */
/* --------------------------------------------------------------- второе ядро --- */
/* Голоса V[trk] == trk: младшие считает CPU0, старшие уходят одной задачей на CPU1.
 * На хосте задача выполняется синхронно — порядок сложения сумм остаётся тем же, что и на одном ядре. */
#ifdef PLAT_HOST
static int host_smp_run(void (*fn)(uint32_t), uint32_t arg) { fn(arg); return 1; }
static int host_smp_wait(void) { return 0; }
#else
static int dev_smp_run(void (*fn)(uint32_t), uint32_t arg) { return plat_cpu2_run(fn, arg); }
static int dev_smp_wait(void) { return plat_cpu2_wait(); }
#endif

void worm_smp_init(void)
{
    worm_clk = plat_ticks24;      /* замер стоимости голосов: ядра делят их поровну по времени, а не по числу */
#ifdef PLAT_HOST
    worm_smp_run = host_smp_run;
    worm_smp_wait = host_smp_wait;
    worm_smp_on = 1;
#else
    /* единственное место, где можно поднять CPU1: main_fm1.c открывает окно на app_init */
    if (plat_cpu2_set(1) == 0) {
        worm_smp_run = dev_smp_run;
        worm_smp_wait = dev_smp_wait;
        worm_smp_on = 1;
    }
#endif
}

void app_init(void)
{
    worm_init();
    worm_wt_init();
    worm_smp_init();
    worm_ui_init();
    worm_store_proj_load();
    (void)worm_store_lm_load();
    worm_store_bank_load(W.bank);   /* банк и паттерн — из проекта (W.bank/W.slot), иначе A01 */
    plat_glow(0);              /* не подсвечивать тускло все кнопки (OCT- и т.п.) */
}

/* ------------------------------------------------------------------ UI ---- */

void app_ui_init(void)
{
    gray_lut_init((W.theme>=0&&W.theme<=3)?W.theme:0);
    lcd_fill(0, 0, 240, 240, sw16(gray_lut[0]));    /* фон схемы, не всегда чёрный */
    memset(ui_prev32, 0xFF, sizeof ui_prev32);   /* 0xFF != любой уровень 0..15: перерисуется всё (раньше 0 совпадал с фоном и у светлых схем он оставался чёрным) */
}

/* Возврат в сток: HOME+SAVE 2 с -> режим обновления */
#ifdef PLAT_HOST
#define mworm_enter_uboot() do { plat_log("UPDATE MODE (HOME+SAVE)"); } while (0)
#else
extern void fm1_enter_uboot(void);
#define mworm_enter_uboot() fm1_enter_uboot()
#endif
static uint32_t ret_t0;
static void return_to_update(uint32_t buttons)
{
    if ((buttons & (1u << B_HOME)) && (buttons & (1u << B_SAVE))) {
        if (!ret_t0)
            ret_t0 = plat_ms();
        else if (plat_ms() - ret_t0 >= 2000u) {
            app_say("UPDATE MODE", "RELEASE TO FLASH");
            mworm_enter_uboot();
        }
    } else {
        ret_t0 = 0;
    }
}

/* Кнопки felucca-bsp (plat.h B_*) -> worm stock id (worm_ui_button).
 * Физические кнопки FM-1: FX SEL ENV LFO EDIT GLO HOME SAVE ARP SEQ PLAY REC OCT- OCT+.
 * worm-ядро ожидает свой порядок (MVAVE-code): 0=OCT- 1=OCT+ 2=FX 3=SCL/SEL 4=ENV
 * 5=LFO 6=EDIT(паттерн) 7=GLO(вкладка) 8=HOME(shift) 9=SAVE 10=ARP/SCALE 11=SEQ 12=PLAY 13=REC. */
static const uint8_t BTN_WORM[NB] = {
    [B_FX]    = 2,   [B_SEL]   = 3,   [B_ENV]  = 4,   [B_LFO] = 5,
    [B_EDIT]  = 6,   [B_GLO]   = 7,   [B_HOME] = 8,   [B_SAVE] = 9,
    [B_ARP]   = 10,  [B_SEQ]   = 11,  [B_PLAY] = 12,  [B_REC] = 13,
    [B_OCTDN] = 0,   [B_OCTUP] = 1,
};
/* Энкодеры felucca-bsp -> worm id: worm 0=SELECT 1=ALGO 2..5=KNOB1..4 6=PRESET;
 * plat.h 0=SELECT 1=ALGO 2=PRESET 3..6=K1..K4. */
static const uint8_t ENC_WORM[NE] = {
    [EN_SELECT] = 0, [EN_ALGO] = 1, [EN_PRESET] = 6,
    [EN_K1] = 2, [EN_K2] = 3, [EN_K3] = 4, [EN_K4] = 5,
};

static void panel_input(void)
{
    uint32_t i;
    /* мастер-громкость */
    static float pot;
    pot += ((float)plat_master() / 4096.0f - pot) * 0.2f;
    /* кнопки/клавиши/энкодеры -> worm_ui (с фронтами) */
    for (i = 0; i < NB; i++) {
        uint32_t bit = 1u << i;
        uint32_t held = plat_buttons() & bit;
        static uint32_t prev_btn;
        if (held && !(prev_btn & bit)) worm_ui_button(BTN_WORM[i], 1);
        else if (!held && (prev_btn & bit)) worm_ui_button(BTN_WORM[i], 0);
        prev_btn = (prev_btn & ~bit) | (held ? bit : 0);
    }
    for (i = 0; i < NKEYS; i++) {
        uint32_t bit = 1u << i;
        uint32_t held = plat_keys() & bit;
        static uint32_t prev_key;
        if (held && !(prev_key & bit)) worm_ui_key((int)i, 1);
        else if (!held && (prev_key & bit)) worm_ui_key((int)i, 0);
        prev_key = (prev_key & ~bit) | (held ? bit : 0);
    }
    for (i = 0; i < NE; i++) {
        int d = plat_enc((int)i);
        if (d) worm_ui_enc(ENC_WORM[i], d);
    }
    /* MIDI ввод (USB/TRS/BLE): ноты -> синтезатор как живые клавиши */
    {
        uint32_t pkt; int port;
        while (plat_midi_in(&pkt, &port)) {
            uint32_t st = (pkt >> 8) & 0xFFu, d1 = (pkt >> 16) & 0x7Fu, d2 = (pkt >> 24) & 0x7Fu;
            if ((st & 0xF0u) == 0x90u && d2) worm_live_on((int)d1, (int)d2);
            else if ((st & 0xF0u) == 0x80u || ((st & 0xF0u) == 0x90u && !d2)) worm_live_off((int)d1);
        }
    }
}

void app_frame(void)
{
    uint32_t i;
    return_to_update(plat_buttons());
#if FM1B_MSC
    mw_usb_mode_poll();                    /* GLO > USB: диск <-> аудио/MIDI */
#endif
    panel_input();
    /* картинку обновляем не чаще ~30 к/с: LCD по SPI 12 МГц всё равно не быстрее, а полный рендер + дифф стоят заметного CPU */
    static uint32_t ui_t0;
    const uint32_t ui_now = plat_ms();
    const int ui_due = (uint32_t)(ui_now - ui_t0) >= 33u;
    if (ui_due) ui_t0 = ui_now;
    {   /* новое USB-MIDI подключение: слушатель ничего не знает о текущих программах/банках/CC — послать заново */
        static uint32_t midi_link_prev;
        uint32_t lk = plat_midi_out_link();
        if (lk != midi_link_prev) { midi_link_prev = lk; MIDI_RESEND(); }
    }
    plat_local_mute(!W.usb_local);   /* по умолчанию: при подключении к компьютеру динамик/наушники FM-1 молчат */
    {   /* USB: компьютер (устройство) или хост для внешнего синта (напр. Roland S-1). Порт — либо то, либо другое */
        static int req = 0;
        if (W.usb_host != req) { req = W.usb_host; plat_usb_host(req); worm_midi_panic(); }
        worm_usbh = plat_usb_host_state();
        
#if FM1B_USB_HOST
        worm_usbh_dbg = (unsigned)fm1_uh_dbg;
#endif

        int m = plat_usb_host_msg();
        if (m) worm_usbh_msg = m;
        if (m == USBH_MSG_LOST || m == USBH_MSG_OFF) worm_midi_panic();
        if (plat_usb_lost()) { worm_midi_panic(); MIDI_RESEND(); }   /* сессия кончилась: отпустить ноты, у нового слушателя — заново PROG/CC */
    }
#if FM1B_MSC
    if (mw_mode_applied != 0)
#endif
    worm_store_flush();                    /* ленивая запись изменённых секторов банка (только STOP, через 1.5 с; стирание NOR вне аудио-ISR) */
    /* UI рендер каждый кадр (плавная анимация), блит по полосам */
    {
        int b = plat_battery();
        worm_battery = b;               /* 0..3, 4 = USB-питание, -1 = не читается */
        worm_charging = (b == 4);       /* USB host = заряжается/питается от USB */
        worm_cpu_pct = (int)plat_cpu_pct();   /* для индикатора CPU/XRUN на экране BPM */
        worm_xruns = (int)plat_xruns();
        {   /* сжатие лимитера мастера (дБ): быстро вверх, медленно (6 дБ/с) вниз — чтобы успеть увидеть */
            static int lim10; int r = worm_synth_lim_take();
            if (r >= lim10) lim10 = r; else if (lim10 > 0) lim10--;
            worm_lim_db = (lim10 + 5) / 10;
        }
        {   /* диагностика хрипов в USB-записи: кольцо пустело (UND), IN-токен без пакета (MISS), самый долгий блок рендера (MAX) */
            uint32_t u = 0, m = 0, x = 0;
            app_uac_stats(&u, &m, &x);
            worm_uac_und = (int)u; worm_uac_miss = (int)m; worm_max_us = (int)x;
        }
        if (ui_due) worm_ui_render(ui_fb);
    }
    /* LED обновляем каждый кадр (не раз в 10): мигание BPM и подсветка должны быть плавными */
    {
        worm_ui_now = plat_ms();                       /* время для таймеров UI (Trig Condition) */
        uint32_t lb, lk, ldb, ldk, lbb, lbk;
        worm_ui_lights(&lb, &lk, &ldb, &ldk, &lbb, &lbk);
        /* PLAY двухцветный: зелёный (2-й LED) при воспроизведении; красный (1-й LED)
         * мигает в такт BPM всегда (STOP/PLAY/REC), синхронно с шагами.
         * PLAY неактивен -> тускло; BPM чуть ярче (mid), при воспроизведении ярче PLAY. */
        uint32_t bright = lb & ~(1u << B_PLAY);       /* PLAY не в ярких (управляем вручную) */
        uint32_t dim = 0;                              /* PLAY зелёный — тускло */
        dim |= LED_PLAY_GREEN;
        uint32_t mid = ldb;                            /* тусклые кнопки меню + REC -> mid (ярче) */
        /* BPM красный мигает в темпе, синхронно с шагами секвенсора (worm_beat_phase, sample-accurate):
         * первая половина четверти горит. mid в STOP, ярко (lit) при воспроизведении. */
        {
            uint32_t ph = worm_beat_phase();
            if (ph < 0x80000000u) {                /* первая половина доли (бита) — горим */
                if (W.run) bright |= 1u << B_PLAY;     /* BPM ярко при воспроизведении */
                else mid |= 1u << B_PLAY;              /* BPM (mid) в STOP */
            }
        }
        plat_lights(bright, lk, dim, ldk, 2); /* яркие + PLAY зелёный тускло + тусклые страницы (DIM HI) */
        plat_lights_mid(mid, 0);              /* кнопки меню/REC/BPM на среднем уровне (ярче) */
        plat_lights_breath(lbb, lbk);          /* OCT-/+ и активная страница дышат (плавно) */
    }
    /* дифф-блит: только изменившиеся полосы по 8 строк; сравнение по слову (4 пикселя), без побитовой возни */
    if (ui_due) {
        const int th = (W.theme>=0&&W.theme<=3)?W.theme:0;   /* GLO > COLOR: смена палитры без перезагрузки */
        if (th != lut_theme) { gray_lut_init(th); memset(ui_prev32, 0xFF, sizeof ui_prev32); }
        const uint32_t *fb = ui_fb32; uint32_t *pv = ui_prev32; uint32_t bufi = 0;
        for (uint32_t y = 0; y < 240; y += 8) {
            const uint32_t w0 = y * 60u;                    /* 240 px / 4 = 60 слов на строку */
            uint32_t d = 0;
            for (uint32_t k = 0; k < 480u && !d; k++) d = (fb[w0 + k] & 0x0F0F0F0Fu) ^ pv[w0 + k];
            if (!d) continue;
            const uint8_t *src = ui_fb + y * 240u; uint8_t *dst = ui_prev + y * 240u; uint16_t *o = strip[bufi];
            for (uint32_t p = 0; p < 1920u; p++) { uint8_t c = (uint8_t)(src[p] & 0xFu); o[p] = gray_lut[c]; dst[p] = c; }
            lcd_blit(0, y, 240, 8, strip[bufi]);
            bufi ^= 1u;                                       /* следующий blit — другой буфер (DMA читает этот) */
        }
    }
    (void)i;
}

void app_input_only(void) { panel_input(); }

void app_loop(void)
{   /* APP_LOOP: каждый проход главного цикла, а не только каждый UI-кадр — BOT иначе тормозил бы на 60 Гц */
#if FM1B_MSC
    if (mw_mode_applied == 0) (void)plat_disk_poll();
#endif
}

void app_say(const char *a, const char *b) { (void)a; (void)b; }
