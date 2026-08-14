#ifndef KLATT_STATE_H
#define KLATT_STATE_H

#include <stdint.h>

#include "klatt_fx.h"

/* The synthesizer's whole working state: one calloc of 0x1d24 bytes, handed
   back to the caller as an opaque handle.
 *
 * Named fields are ones a decoded function actually touches. The pad arrays
 * are not guesses: their sizes fall out of the distance between two offsets
 * we do know, so naming more of them later cannot move anything already here.
 * offsetof assertions at the bottom of klatt_fx.c hold every field in place.
 */

/* What output_speech hands the host: a count and the samples themselves.
   runklatt.obj names this type in its own symbols. */
typedef struct {
    int32_t  count;
    int32_t *samples;
} KlattSamplesStruct;

typedef struct klatt_state klatt_state;

typedef void (*klatt_error_fn)(void *user, const char *tag, const char *msg);
typedef int  (*klatt_samples_fn)(void *user, KlattSamplesStruct *s);

typedef struct {
    int32_t a;
    int32_t b;
} klatt_pair;

struct klatt_state {
    const char      *version;             /* 0x0000, doubles as the handle check */
    void            *user;                /* 0x0004 */
    uint8_t          pad_0008[8];
    int32_t          unknown_0010;        /* 0x0010 */
    int32_t          const_parms_set;     /* 0x0014, KlattOpen refuses until 1 */
    uint8_t          pad_0018[64];
    int32_t          volume;              /* 0x0058, percent */
    int32_t          open_state;          /* 0x005c, 2 once open */
    uint8_t          pad_0060[4];
    filter_parms     filters[21];         /* 0x0064, ends at 0x0748 */
    uint8_t          pad_0748[24];
    int32_t          out[201];            /* 0x0760, the sample buffer */
    int32_t          period;              /* 0x0a84 */
    uint8_t          pad_0a88[48];
    klatt_error_fn   error_fn;            /* 0x0ab8 */
    int32_t          callback_mode;       /* 0x0abc, 2 means deliver samples */
    klatt_samples_fn samples_fn;          /* 0x0ac0 */
    uint8_t          pad_0ac4[8];
    int32_t          buf_a[200];          /* 0x0acc */
    int32_t         *ptr_a;               /* 0x0dec, points at buf_a */
    uint8_t          pad_0df0[808];
    int32_t          buf_b[200];          /* 0x1118 */
    int32_t         *ptr_b;               /* 0x1438, points at buf_b */
    uint8_t          pad_143c[92];
    int32_t          unknown_1498;        /* 0x1498 */
    int32_t          unknown_149c;        /* 0x149c */
    int32_t          unknown_14a0;        /* 0x14a0 */
    uint8_t          pad_14a4[8];
    int32_t          sample_rate;         /* 0x14ac */
    int32_t          unknown_14b0;        /* 0x14b0 */
    int32_t          v_start;             /* 0x14b4 */
    uint8_t          pad_14b8[12];
    int32_t          noise_count;         /* 0x14c4 */
    int32_t          voicing_size;        /* 0x14c8 */
    uint8_t          pad_14cc[4];
    int32_t          unknown_14d0;        /* 0x14d0 */
    int32_t          unknown_14d4;        /* 0x14d4 */
    int32_t          unknown_14d8;        /* 0x14d8 */
    int32_t          unknown_14dc;        /* 0x14dc */
    int32_t          unknown_14e0;        /* 0x14e0 */
    int32_t          unknown_14e4;        /* 0x14e4 */
    uint8_t          pad_14e8[8];
    int32_t          unknown_14f0;        /* 0x14f0 */
    int32_t          length;              /* 0x14f4, KlattLength returns this */
    int32_t          max;                 /* 0x14f8, KlattMax returns this */
    int32_t          noise_limit;         /* 0x14fc */
    klatt_pair       pairs[100];          /* 0x1500, noise smoothing spans */
    uint8_t          pad_1820[4];
    int32_t          smooth_noise;        /* 0x1824 */
    uint8_t          pad_1828[24];
    int32_t          smooth_span;         /* 0x1840 */
    uint8_t          pad_1844[8];
    int16_t          noise_buf[204];      /* 0x184c */
    int32_t          unknown_19e4;        /* 0x19e4 */
    int32_t          unknown_19e8;        /* 0x19e8 */
    int32_t          callback_result;     /* 0x19ec */
    uint8_t          pad_19f0[812];
    int32_t          output_samples;      /* 0x1d1c */
    uint8_t          pad_1d20[4];
};

uint32_t noise(klatt_state *k, uint32_t seed);
void     compute_v_start(klatt_state *k);
void     compute_voicing_size(klatt_state *k);
void     output_speech(klatt_state *k, int32_t n);

void    *klatt_new(void *user);
void     klatt_delete(void *handle);
int      KlattOpen(void *handle);
void     KlattClose(void *handle);
int32_t  KlattLength(void *handle);
int32_t  KlattMax(void *handle);
void     KlattSetOutputSamplesOption(void *handle, int32_t option);
void     klattSetVolumeMultiplier(void *handle, int32_t volume);
int      errorKlattIgnore(void);

#endif
