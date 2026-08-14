#ifndef KLATT_FX_H
#define KLATT_FX_H

#include <stdint.h>

/* IBM built clsyn.cpp for a 32-bit target where long is 32 bits. Nothing here
   may use long: on LP64 hosts that silently changes every result. */

/* imull keeps the low 32 bits and lets the rest go. Signed overflow is
   undefined in C, so wrap in unsigned and reinterpret to get the same bits. */
static inline int32_t mul32(int32_t a, int32_t b)
{
    return (int32_t)((uint32_t)a * (uint32_t)b);
}

void     clr_vector(int32_t *v, int32_t n);
uint32_t klatt_rand(int16_t *out, int32_t n, uint32_t seed);
int16_t  fxdivl(int32_t num, int32_t den);
void     fxmul_vector(const int32_t *src, int16_t coef, int32_t *acc, int32_t n);
void     fxmul1_vector(const int16_t *src, int16_t coef, int32_t *acc, int32_t n);
int32_t  db2lin(int32_t db);
int      verifyKlattHandle(void *handle);

/* One resonator's working state. The synthesizer state block holds 21 of
   these in an array starting at offset 0x64. Fields still called unknown are
   ones only pole_filter and KlattSynth have touched so far. */
typedef struct {
    int16_t sa;              /* 0x00, steady state, weight on the input */
    int16_t sb;              /* 0x02, weight on y[n-1] */
    int16_t sc;              /* 0x04, weight on y[n-2] */
    int8_t  kind;            /* 0x06, KlattSynth sets 2 for a cascade pole */
    int8_t  fresh;           /* 0x07, set whenever the coefficients change */
    int32_t unknown_08;      /* 0x08 */
    int16_t a[3];            /* 0x0c, coefficients ramped over three samples */
    int16_t b[3];            /* 0x12 */
    int16_t c[3];            /* 0x18 */
    int16_t unknown_1e[3];   /* 0x1e */
    int32_t d1;              /* 0x24, previous sample */
    int32_t d2;              /* 0x28, the one before that */
    int32_t unknown_2c[7];   /* 0x2c */
    int32_t enabled;         /* 0x48 */
    int32_t ramp;            /* 0x4c, samples left of the coefficient ramp */
    int32_t unknown_50;      /* 0x50 */
} filter_parms;

/* Steady-state coefficients a zero uses once its ramp has run out. */
typedef struct {
    int16_t a;
    int16_t b;
    int16_t c;
} zero_ABCs;

void zero_filter(filter_parms *fp, const zero_ABCs *z, int32_t *buf, int32_t n);

/* buf must have two writable samples before it: the resonator seeds its own
   history there and reads them back as y[n-1] and y[n-2]. */
void pole_filter(filter_parms *fp, int32_t *buf, int32_t n);
void parallel0_filter(filter_parms *fp, int32_t *buf, int32_t n);

extern const char KlattVersionString[];

#endif
