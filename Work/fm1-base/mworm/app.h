/* SPDX-License-Identifier: GPL-3.0-only
 * Copyright (C) 2026 M-WORM */
/* M-WORM: открытый грувбокс-движок на fm1-base.
 * Это заголовок — половина контракта приложения с платформой (device/unity.c, host/plat_host.c). */
#pragma once
#include <stdint.h>
#include "plat.h"

#define APP_NAME "M-WORM"
#define APP_SPLASH_RGB RGB(90, 160, 255)
#define APP_BLOCK 256u                   /* кадров на аудио-блок: 5.8 мс при 44.1 кГц (Felucca-дефолт) */

/* USB-аудио запись: рендер синтезатора (32 LFO, фильтры, реверб/delay/chorus) занимает >40% своего
 * времени, из-за чего кольцо записи иссякало и пакеты повторяли последний кадр (треск/хрип в записи).
 * UAC_DEEP=1 держит на полбуфера больше (4 KB в пуле, +5.8 мс задержки записи): рендеру разрешено всё время. */
#define FM1B_UAC_DEEP 1

/* USB-аудио переключаемое в рантайме (GLO -> OUTPUT): MASTER 24/48, TRK 1-3, TRK 4-6, FX (6 кан. 16/44.1 каждый).
 * Режимы требуют 48 кГц и ресэмплера (bsp/chip/uac_fir.h — на месте). */
#define FM1B_UAC_48K 1
#define FM1B_UAC_MODES 1
#define APP_USB_MODE 1                 /* main_fm1: при смене OUTPUT дёрнуть usb_audio_mode() и перечислиться */
#define APP_LOOP                       /* main_fm1: опрос USB Mass Storage каждый проход главного цикла */
/* Кольцо 11.25 KB кладём в .pool, а не .bss: в WORM пул почти пуст (~56 KB свободно),
 * а RAM упирается в потолок. BSP по умолчанию держит его в .bss для конфигураций,
 * где пул занят (mirror/backup), — у нас эти части выключены. */
#define FELUCCA_UAC_RING_ATTR PLAT_POOL
/* Подушка USB-записи: рендер иногда растягивается при активной перерисовке экрана (UI вытесняет код синта из XIP-кэша),
 * кольцо пустело, пакеты повторяли последний кадр (треск только в записи). +256 кадров (~5.8 мс) к DEEP и кольцо вдвое
 * больше (+11.25 КБ пула; 4 TRACK: 1440 кадров). Задержка записи растёт на ~6 мс — для записи неважно. */
#define FELUCCA_UAC_EXTRA 256u
#define FELUCCA_UAC_POOLB 23040u

/* UI рисует сам (worm_ui_render -> lcd_blit), канвас gfx.c не нужен:
 * отдать его 69 KB пула буферам синта. */
#define APP_CANVAS_PX 0u

/* настройки: один объект store (паттерны/пресеты — отдельным шагом) */
enum { OBJ_SETTINGS, OBJ_PANEL, OBJ_NOBJ };
#define APP_STORE_MAP {0xFC000u, 0xDC000u}

/* контракт: платформа вызывает эти функции */
void app_init(void);
void app_render(int32_t *lr, uint32_t n, uint32_t master_q12);   /* аудио-ISR: n стерео-кадров, Q23 */
void app_panic(void);
uint32_t app_usb_mode(void);              /* USB-аудио (режим BSP): 0 MASTER (24/48), 2 = 6 кан. 16/44.1 (TRK 1-3 / TRK 4-6 / FX) */
const int16_t *app_usb_tap(void);        /* тап последнего app_render: кадр = 8 слотов int16, заняты 6 */
void app_ui_init(void);
void app_frame(void);
void app_loop(void);                  /* каждый проход главного цикла: USB disk poll (APP_LOOP) */
void app_input_only(void);
void app_say(const char *a, const char *b);

/* перегруз рендера (render > 90% бюджета): сбросить голоса, чтобы не было треска/xruns */
#define APP_OVERLOAD
void app_overload(void);
