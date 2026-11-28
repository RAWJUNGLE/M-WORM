/* Второе ядро: голоса (по любому набору дорожек) и reverb делятся между CPU0 и CPU1, а звук совпадает с одним ядром
 * бит в бит — при любом разбиении, при замерах времени (разбиение «плавает»), при отказе задачи (wait != 0).
 * На хосте x86 компилятор может изображать FMA, которого нет у FM-1; собирайте с -ffp-contract=off. */
#include <stdio.h>
#include <string.h>
#include <stdint.h>
#include "worm.h"
int worm_flash_read(uint32_t a, void *b, uint32_t l) { (void)a; memset(b, 0xFF, l); return 0; }
int worm_flash_write(uint32_t a, const void *b, uint32_t l) { (void)a; (void)b; (void)l; return 0; }
int worm_flash_erase(uint32_t a, uint32_t l) { (void)a; (void)l; return 0; }
void worm_midi_out(int s, int a, int b) { (void)s; (void)a; (void)b; }

static int ok = 1;
#define CK(c, m) do { if (!(c)) { printf("FAIL %s\n", m); ok = 0; } } while (0)

static int jobs;
static int run_stub(void (*fn)(uint32_t), uint32_t arg) { jobs++; fn(arg); return 1; }
static int refuse_stub(void (*fn)(uint32_t), uint32_t arg) { (void)fn; (void)arg; return 0; }   /* CPU1 не берёт: всё на CPU0 */
static int wait_stub(void) { return 0; }
static int drop_stub(void) { return -1; }
static int drop_snap_stub_n;
static int drop_each_third(void) { return (++drop_snap_stub_n % 3) == 0 ? -1 : 0; }

static uint32_t fake_t, rng = 12345;                    /* «таймер»: каждый замер даёт случайную стоимость 50..450 */
static uint32_t fake_clk(void) { rng = rng * 1664525u + 1013904223u; fake_t += 50u + (rng >> 24) % 400u; return fake_t; }

#define FR 4096
static void setup(unsigned mask)
{
    worm_init();
    W.bpm = 120.f; W.run = 0; W.usb_out = 1;
    for (int t = 0; t < NTRK; t++) {
        worm_select_track(t);
        W.tr[t].p[P_OA] = (int16_t)(t & 1);
        W.tr[t].p[P_OW] = (int16_t)(2 + t);
        W.tr[t].p[P_OL] = 100;
        W.tr[t].p[P_SDL] = (int16_t)(20 + t * 10);
        W.tr[t].p[P_SRV] = (int16_t)(30 + t * 15);       /* reverb и chorus тоже в работе */
        W.tr[t].p[P_SCH] = (int16_t)(t * 25);
        W.tr[t].p[P_APAN] = (int16_t)(32 + t * 20);
        if (mask & (1u << t)) worm_synth_note_on_v(W.tr[t].p, 40 + t * 5, 100, 44100, t, 1, -1);
    }
}

static void capture(int16_t *o)                          /* нота, потом релиз и хвост reverb/delay */
{
    for (int f = 0; f < FR; f += 256) {
        if (f == 1024) for (int i = 0; i < NVOICE; i++) worm_synth_note_off(i);
        worm_render(o + f * 2, 256);
    }
}

static int16_t x[FR * 2], y[FR * 2], z[FR * 2];

int main(void)
{
    worm_smp_run = 0; worm_smp_wait = 0; worm_clk = 0;
    static int16_t one[1u << NTRK][FR * 2];
    for (unsigned m = 1; m < (1u << NTRK); m++) { setup(m); capture(one[m]); }

    for (unsigned m = 1; m < (1u << NTRK); m++) {
        const int n = __builtin_popcount(m);
        /* 1. поровну (без таймера) */
        worm_smp_run = run_stub; worm_smp_wait = wait_stub; worm_clk = 0; jobs = 0;
        setup(m); capture(x);
        CK(!memcmp(one[m], x, sizeof x), "two cores == one core");
        CK(n < 2 ? 1 : jobs > 0, "CPU1 gets work");
        /* 2. «замеры» случайны: разбиение голосов меняется, звук тот же */
        worm_clk = fake_clk; setup(m); capture(y);
        CK(!memcmp(one[m], y, sizeof y), "timed split: still bit for bit");
        /* 3. CPU1 отказывает: каждая задача брошена / каждая третья брошена; откат голосов и reverb */
        worm_clk = fake_clk; worm_smp_wait = drop_stub; setup(m); capture(z);
        CK(!memcmp(one[m], z, sizeof z), "all jobs dropped: recomputed bit for bit");
        worm_smp_wait = drop_each_third; drop_snap_stub_n = 0; setup(m); capture(z);
        CK(!memcmp(one[m], z, sizeof z), "every third job dropped: rollback bit for bit");
        /* 4. CPU1 не принимает задачи */
        worm_smp_run = refuse_stub; worm_smp_wait = wait_stub; setup(m); capture(z);
        CK(!memcmp(one[m], z, sizeof z), "refused jobs: bit for bit");
    }

    /* разбиение действительно подбирается по замерам: при дорогих старших голосах CPU1 получает меньше */
    {
        int seen[NVOICE] = {0}, mm, mx = 0;
        worm_smp_run = run_stub; worm_smp_wait = wait_stub; worm_clk = fake_clk;
        setup((1u << NTRK) - 1u);
        for (int f = 0; f < FR; f += 64) { static int16_t o[128]; worm_render(o, 64); worm_synth_smp_info(&mm, 0, 0, 0); if (mm >= 0 && mm < NVOICE) seen[mm] = 1; if (mm > mx) mx = mm; }
        CK(mx <= NVOICE / 2, "CPU1 never gets more than half the voices");
        CK(seen[2] || seen[3], "CPU1 is used with six voices");
    }

    worm_smp_run = 0; worm_smp_wait = 0; worm_clk = 0;
    printf(ok ? "smp OK\n" : "smp FAIL\n");
    return ok ? 0 : 1;
}
