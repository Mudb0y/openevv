#ifndef KLATT_FX_H
#define KLATT_FX_H

#include <stdint.h>

/* IBM built clsyn.cpp for a 32-bit target where long is 32 bits. Nothing here
   may use long: on LP64 hosts that silently changes every result. */

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
    int16_t unknown_00[6];   /* 0x00 */
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

extern const char KlattVersionString[];

#endif
