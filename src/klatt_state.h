#ifndef KLATT_STATE_H
#define KLATT_STATE_H

#include <stdint.h>

#include "klatt_fx.h"
#include "klatt_tables.h"

/* The synthesizer's whole working state: one calloc of 0x1d24 bytes, handed
   back to the caller as an opaque handle.
 *
 * Named fields are ones a decoded function actually touches. The pad arrays
 * are not guesses: their sizes fall out of the distance between two offsets
 * we do know, so naming more of them later cannot move anything already here.
 * offsetof assertions at the top of klatt_state.c hold every field in place.
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

/* Everything KlattSetConstParms is handed, 68 bytes copied wholesale into the
   state. It arrives by value, seventeen words wide, which is why the original
   sets it with a rep movsl rather than field by field. */
typedef struct {
    int32_t          unknown_00;
    int32_t          sample_rate;      /* 8000 and 11025 are the only two the
                                          original recognises by name */
    int32_t          unknown_08;
    int32_t          n_formants;       /* KlattSynth loops formants 5..n+4 */
    int16_t          unknown_10;
    int16_t          unknown_12;
    int32_t          unknown_14;
    int32_t          unknown_18;
    int32_t          unknown_1c;
    int32_t          unknown_20;
    int32_t          unknown_24;
    int32_t          unknown_28;
    int32_t          unknown_2c;
    int32_t          unknown_30;
    int32_t          unknown_34;
    klatt_error_fn   error_fn;
    int32_t          callback_mode;    /* 2 means deliver samples */
    klatt_samples_fn samples_fn;
} KlattConstParms;

struct klatt_state {
    const char      *version;             /* 0x0000, doubles as the handle check */
    void            *user;                /* 0x0004 */
    const int16_t   *ex_table;            /* 0x0008, EX8 or EX11 */
    const int16_t   *co_table;            /* 0x000c, CO8 or CO11 */
    int32_t          unknown_0010;        /* 0x0010 */
    int32_t          const_parms_set;     /* 0x0014, KlattOpen refuses until 1 */
    uint8_t          pad_0018[64];
    int32_t          volume;              /* 0x0058, percent */
    int32_t          open_state;          /* 0x005c, 2 once open */
    uint8_t          pad_0060[4];
    filter_parms     filters[21];         /* 0x0064, ends at 0x0748 */
    /* Steady-state coefficients for the two zeros, filters 1 and 2. Eight
       bytes apiece rather than six, so the array stays word aligned. */
    struct { int16_t a, b, c, pad; } zeros[3];   /* 0x0748 */
    int32_t          out[200];            /* 0x0760, the sample buffer */
    KlattConstParms  cp;                  /* 0x0a80 */
    uint8_t          pad_0ac4[8];
    int32_t          buf_a[200];          /* 0x0acc */
    int32_t         *ptr_a;               /* 0x0dec, points at buf_a */
    /* The frication noise, scaled up into accumulator range. The eight bytes
       after it are buf_b's history headroom, the same as before buf_a. */
    int32_t          frication[200];      /* 0x0df0 */
    uint8_t          pad_1110[8];
    int32_t          buf_b[200];          /* 0x1118 */
    int32_t         *ptr_b;               /* 0x1438, points at buf_b */
    int16_t          ab_gain;             /* 0x143c, the bypass path */
    int16_t          co[21];              /* 0x143e, cosine term per resonator */
    int16_t          ex[21];              /* 0x1468, damping term per resonator */
    uint8_t          pad_1492[6];
    int32_t          unknown_1498;        /* 0x1498 */
    int32_t          unknown_149c;        /* 0x149c */
    int32_t          unknown_14a0;        /* 0x14a0 */
    int32_t          closed_pct;          /* 0x14a4, 100 minus diplophonia */
    int32_t          di;                  /* 0x14a8, diplophonia */
    int32_t          f0;                  /* 0x14ac, pitch */
    int32_t          oq;                  /* 0x14b0, open quotient */
    int32_t          v_start;             /* 0x14b4 */
    int32_t          pulse_amp;           /* 0x14b8, this period's pulse */
    int32_t          pulse_amp_alt;       /* 0x14bc, the alternate period's */
    int32_t          pulse_amp_base;      /* 0x14c0, from av and open quotient */
    int32_t          noise_count;         /* 0x14c4 */
    int32_t          voicing_size;        /* 0x14c8 */
    int32_t          diplo_shift;         /* 0x14cc, how far the alternate slips */
    int32_t          open_len;        /* 0x14d0 */
    int32_t          open_part;        /* 0x14d4 */
    int32_t          closed_len;        /* 0x14d8 */
    int32_t          closed_part;        /* 0x14dc */
    int32_t          carry_lead;        /* 0x14e0 */
    int32_t          carry_open;        /* 0x14e4 */
    int32_t          carry_period;        /* 0x14e8, the period it belonged to */
    int32_t          carry_amp;           /* 0x14ec, and its pulse amplitude */
    int32_t          carry_closed;        /* 0x14f0 */
    int32_t          length;              /* 0x14f4, KlattLength returns this */
    int32_t          max;                 /* 0x14f8, KlattMax returns this */
    /* One run length per pitch period. KlattSynth walks this forward as it
       finds periods, using the index in smooth_span, and noise() walks the
       same list back to decide where to attenuate. Entry zero is where the
       first attenuation run ends; after that they come in pairs, a skip and
       a run. It stops at 0x1818 because 0x181c is written as a scalar. */
    int32_t          spans[199];          /* 0x14fc */
    int32_t          unknown_1818;        /* 0x1818 */
    int32_t          n_formants;          /* 0x181c, copied from the const parms */
    int32_t          tilt;                /* 0x1820, spectral tilt, capped at 35 */
    /* KlattSynth copies three amplitudes straight out of the parameter frame
       into these. noise() gates its smoothing on av, so the noise follows the
       pitch periods only while there is voicing to follow. */
    int32_t          av;                  /* 0x1824, amplitude of voicing */
    int32_t          ah;                  /* 0x1828, aspiration */
    int32_t          af;                  /* 0x182c, frication */
    int32_t          unknown_1830;        /* 0x1830 */
    int32_t          unknown_1834;        /* 0x1834 */
    int32_t          unknown_1838;        /* 0x1838 */
    int32_t          unknown_183c;        /* 0x183c */
    int32_t          smooth_span;         /* 0x1840 */
    int32_t          unknown_1844;        /* 0x1844 */
    int32_t          flutter;             /* 0x1848, this frame's pitch wobble */
    /* Same reasoning as pairs: 0x19dc is written as a 32-bit scalar, so the
       buffer stops there. */
    int16_t          noise_buf[200];      /* 0x184c */
    int32_t          unknown_19dc;        /* 0x19dc */
    int32_t          unknown_19e0;        /* 0x19e0 */
    int32_t          diplo_on;        /* 0x19e4 */
    int32_t          diplo_alt;        /* 0x19e8 */
    int32_t          callback_result;     /* 0x19ec */
    int32_t          unknown_19f0;        /* 0x19f0 */
    int32_t          unknown_19f4;        /* 0x19f4 */
    /* One flag per sample saying whether it fell inside a glottal period,
       built from the span list above. */
    int32_t          voiced_flags[200];   /* 0x19f8 */
    int32_t          unknown_1d18;        /* 0x1d18 */
    int32_t          output_samples;      /* 0x1d1c */
    int32_t          rate_code;           /* 0x1d20, 0 at 8k, 1 at 11k, else 2 */
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
void     KlattSetConstParms(void *handle, KlattConstParms parms);
int      KlattSynth(void *handle, const int32_t *parms);

#endif
