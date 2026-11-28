/* 64-bit integer division + freestanding malloc, for the fm1-base DEVICE build
 * (no compiler-rt, no libc). On the host (PLAT_HOST) the real libc/compiler-rt
 * provide these, so the shims are compiled out.
 * GPL-3.0-only. Copyright (C) 2026 M-WORM. */
#include <stdint.h>
#include <stddef.h>
#include <stdarg.h>

#ifndef PLAT_HOST

typedef uint64_t u64;
typedef int64_t s64;

static u64 udiv(u64 n, u64 d)
{
    u64 q = 0, r = 0;
    int i;
    if (d == 0) return 0xFFFFFFFFFFFFFFFFu;
    for (i = 63; i >= 0; i--) {
        r = (r << 1) | ((n >> i) & 1u);
        if (r >= d) { r -= d; q |= 1ull << i; }
    }
    return q;
}

u64 __udivdi3(u64 n, u64 d) { return udiv(n, d); }
u64 __umoddi3(u64 n, u64 d) { u64 q = udiv(n, d); return n - q * d; }

s64 __divdi3(s64 n, s64 d)
{
    int neg = (n < 0) != (d < 0);
    u64 q = udiv(n < 0 ? (u64)(-n) : (u64)n, d < 0 ? (u64)(-d) : (u64)d);
    return neg ? (s64)(-q) : (s64)q;
}
s64 __moddi3(s64 n, s64 d)
{
    s64 q = __divdi3(n, d);
    return n - q * d;
}

/* freestanding malloc: a static arena (the DSP's delay line, a few KB).
 * No free (single allocation at init); big buffers live in .pool, not here. */
#define ARENA_SIZE (20 * 1024)
#ifndef PLAT_HOST
static uint8_t arena[ARENA_SIZE] __attribute__((aligned(8), section(".pool")));
#else
static uint8_t arena[ARENA_SIZE] __attribute__((aligned(8)));
#endif
static uint32_t arena_used;

void *malloc(uint32_t size)
{
    size = (size + 7u) & ~7u;
    if (arena_used + size > ARENA_SIZE) return 0;
    void *p = &arena[arena_used];
    arena_used += size;
    return p;
}
void *calloc(uint32_t n, uint32_t sz)
{
    uint32_t total = n * sz, i;
    uint8_t *p = (uint8_t *)malloc(total);
    if (!p) return 0;
    for (i = 0; i < total; i++) p[i] = 0;
    return p;
}
void free(void *p) { (void)p; }

/* Минимальный snprintf/sprintf для worm-ядра (форматы: %d %s %c %02d %+d %%.d %%,
 * без %f). fm1-base фристайнд-сборка не линкует libc sprintf. */
#include <stdarg.h>

static void fmt_emit(char **pp, char *end, char c)
{
    if (*pp < end)
        *(*pp)++ = c;
}

static void fmt_str(char **pp, char *end, const char *s)
{
    while (s && *s && *pp < end)
        *(*pp)++ = *s++;
}

static void fmt_int(char **pp, char *end, int v, int width, int sign)
{
    char t[16], *q = t + sizeof t;
    unsigned u = (unsigned)(v < 0 ? -v : v);
    int n = 0;
    *--q = '\0';                    /* завершающий ноль: fmt_str копирует до него */
    do { *--q = (char)('0' + u % 10u); u /= 10u; n++; } while (u);
    /* знак */
    if (v < 0) fmt_emit(pp, end, '-');
    else if (sign > 0) fmt_emit(pp, end, '+');
    /* нули для ширины */
    while (n < width) { fmt_emit(pp, end, '0'); n++; }
    fmt_str(pp, end, q);
}

int vsnprintf(char *s, size_t n, const char *fmt, va_list ap)
{
    char *p = s, *end = s + n - 1;
    if (!s || !n) return 0;
    while (*fmt && p < end) {
        if (*fmt != '%') { *p++ = *fmt++; continue; }
        fmt++;
        int width = 0, sign = 0;
        if (*fmt == '0') { width = fmt[1] - '0'; fmt += 2; }
        if (*fmt == '+') { sign = 1; fmt++; }
        switch (*fmt) {
        case 'd': fmt_int(&p, end, va_arg(ap, int), width, sign); break;
        case 's': fmt_str(&p, end, va_arg(ap, const char *)); break;
        case 'c': { char c = (char)va_arg(ap, int); if (p < end) *p++ = c; } break;
        case '%': if (p < end) *p++ = '%'; break;
        default: if (p < end) *p++ = *fmt; break;
        }
        fmt++;
    }
    *p = 0;
    return (int)(p - s);
}

int snprintf(char *s, size_t n, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt);
    r = vsnprintf(s, n, fmt, ap);
    va_end(ap);
    return r;
}

int sprintf(char *s, const char *fmt, ...)
{
    va_list ap; int r;
    va_start(ap, fmt);
    r = vsnprintf(s, 0x7fffffff, fmt, ap);
    va_end(ap);
    return r;
}

/* string.h: strcpy/strlen/strcmp (worm-ядро использует; freestanding без libc) */
char *strcpy(char *d, const char *s)
{
    char *r = d;
    while (*s) *d++ = *s++;
    *d = 0;
    return r;
}
size_t strlen(const char *s)
{
    size_t n = 0;
    while (*s++) n++;
    return n;
}
int strcmp(const char *a, const char *b)
{
    while (*a && *a == *b) { a++; b++; }
    return (unsigned char)*a - (unsigned char)*b;
}
int abs(int x) { return x < 0 ? -x : x; }

#endif /* !PLAT_HOST */

