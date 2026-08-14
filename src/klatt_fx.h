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

extern const char KlattVersionString[];

#endif
