/* KlattSynth: one parameter frame in, a run of samples out.
 *
 * The frame's duration decides how many samples come out, and a duration of
 * zero produces none, which lets the setup phase below be tested against the
 * original on its own before the per-sample loop exists.
 */

#include "klatt_state.h"

/* Resonator slots in the filter array. Five through twelve are the cascade
   formants and thirteen through twenty their parallel counterparts, which
   share the cascade's frequencies but carry their own bandwidths. */
#define NASAL_ZERO     1
#define TRACHEAL_ZERO  2
#define NASAL_POLE     3
#define TRACHEAL_POLE  4
#define CASCADE_BASE   5
#define PARALLEL_BASE 13

/* Parameter frame word indices, in the order the string table gives them. */
enum {
    P_UI = 0, P_F0, P_AV, P_OQ, P_TL, P_FL, P_DI, P_AH, P_AF,
    P_F1, P_B1, P_DF1, P_DB1, P_F2, P_B2, P_F3, P_B3, P_F4, P_B4,
    P_F5, P_B5, P_F6, P_B6, P_F7, P_B7, P_F8, P_B8,
    P_FNP, P_BNP, P_FNZ, P_BNZ, P_FTP, P_BTP, P_FTZ, P_BTZ,
    P_A1F, P_A2F, P_A3F, P_A4F, P_A5F, P_A6F, P_A7F, P_A8F, P_AB,
    P_B1F, P_B2F, P_B3F, P_B4F, P_B5F, P_B6F, P_B7F, P_B8F,
    P_ANV, P_A1V, P_A2V, P_A3V, P_A4V, P_A5V, P_A6V, P_A7V, P_A8V,
    P_ATV, P_MS
};

static int32_t clamp(int32_t v, int32_t lo, int32_t hi)
{
    if (v < lo)
        return lo;
    if (v > hi)
        return hi;
    return v;
}

/* The tables are indexed in hertz starting at ten, which is why frequencies
   are pinned to 5000 and bandwidths to 4000 before any lookup. */
static int16_t co_of(const klatt_state *k, int32_t hz)
{
    return k->co_table[hz - 10];
}

static int16_t ex_of(const klatt_state *k, int32_t hz)
{
    return k->ex_table[hz - 10];
}

/* Turn the cosine and damping terms into the three resonator weights. The
   halving and quartering here cancel the doubling and quadrupling pole_filter
   applies on the way out, so the three land at three different scales and
   still sum to unity. */
static void set_coefficients(filter_parms *fp, int16_t ex, int16_t co)
{
    fp->sc = (int16_t)fxmul_scaled((int16_t)-ex, ex);
    fp->sb = (int16_t)fxmul_scaled(ex, co);
    fp->fresh = 1;
    fp->sb = (int16_t)(fp->sb & ~1);
    fp->sc = (int16_t)(fp->sc & ~3);
    fp->sa = (int16_t)(0x2000 - (fp->sb >> 1) - (fp->sc >> 2));
    fp->kind = 2;
    fp->unknown_08 = 1;
}

int KlattSynth(void *handle, const int32_t *parms)
{
    klatt_state *k = handle;
    int32_t freq[21], bw[21], amp[21];
    int32_t n_samples, i, j, first, remaining;
    int32_t ab_base;
    int16_t four, asp_gain;

    if (!verifyKlattHandle(handle))
        return 0;

    k->unknown_0010++;

    n_samples = mul32(mul32(parms[P_UI], k->cp.unknown_00), k->cp.sample_rate)
              / 100000;

    k->av = parms[P_AV];
    k->ah = parms[P_AH];
    k->af = parms[P_AF];

    if (k->av != 0 || k->ah != 0)
        k->unknown_1498 = 20;
    if (k->af != 0)
        k->unknown_149c = 20;

    first = 1;
    k->unknown_19f0 = 0;

    freq[NASAL_POLE]    = parms[P_FNP];
    bw[NASAL_POLE]      = parms[P_BNP];
    freq[NASAL_ZERO]    = parms[P_FNZ];
    bw[NASAL_ZERO]      = parms[P_BNZ];
    freq[TRACHEAL_POLE] = parms[P_FTP];
    bw[TRACHEAL_POLE]   = parms[P_BTP];
    freq[TRACHEAL_ZERO] = parms[P_FTZ];
    bw[TRACHEAL_ZERO]   = parms[P_BTZ];

    freq[13] = parms[P_F1]; freq[5] = freq[13]; bw[5] = parms[P_B1];
    freq[14] = parms[P_F2]; freq[6] = freq[14]; bw[6] = parms[P_B2];
    freq[15] = parms[P_F3]; freq[7] = freq[15]; bw[7] = parms[P_B3];
    freq[16] = parms[P_F4]; freq[8] = freq[16]; bw[8] = parms[P_B4];
    freq[17] = parms[P_F5]; freq[9] = freq[17]; bw[9] = parms[P_B5];

    bw[13] = parms[P_B1F]; amp[13] = parms[P_A1F];
    bw[14] = parms[P_B2F]; amp[14] = parms[P_A2F];
    bw[15] = parms[P_B3F]; amp[15] = parms[P_A3F];
    bw[16] = parms[P_B4F]; amp[16] = parms[P_A4F];
    bw[17] = parms[P_B5F]; amp[17] = parms[P_A5F];

    if (k->n_formants > 5) {
        freq[18] = parms[P_F6]; freq[10] = freq[18]; bw[10] = parms[P_B6];
        freq[19] = parms[P_F7]; freq[11] = freq[19]; bw[11] = parms[P_B7];
        freq[20] = parms[P_F8]; freq[12] = freq[20]; bw[12] = parms[P_B8];

        bw[18] = parms[P_B6F]; amp[18] = parms[P_A6F];
        bw[19] = parms[P_B7F]; amp[19] = parms[P_A7F];
        bw[20] = parms[P_B8F]; amp[20] = parms[P_A8F];
    }

    if (k->unknown_1498 > 0) {
        /* A nasal pole sitting exactly on its zero cancels, so the original
           switches the pair off rather than filtering with them. */
        if (freq[NASAL_POLE] == freq[NASAL_ZERO] &&
            bw[NASAL_POLE] == bw[NASAL_ZERO]) {
            k->filters[NASAL_ZERO].enabled = 0;
            k->filters[NASAL_POLE].enabled = 0;
        } else {
            k->filters[NASAL_ZERO].enabled = k->unknown_1498;
            k->filters[NASAL_POLE].enabled = k->filters[NASAL_ZERO].enabled;

            freq[NASAL_POLE] = clamp(freq[NASAL_POLE], 10, 5000);
            freq[NASAL_ZERO] = clamp(freq[NASAL_ZERO], 10, 5000);
            bw[NASAL_POLE]   = clamp(bw[NASAL_POLE], 10, 4000);
            bw[NASAL_ZERO]   = clamp(bw[NASAL_ZERO], 10, 4000);

            k->co[NASAL_POLE] = co_of(k, freq[NASAL_POLE]);
            k->co[NASAL_ZERO] = co_of(k, freq[NASAL_ZERO]);
            k->ex[NASAL_POLE] = ex_of(k, bw[NASAL_POLE]);
            k->ex[NASAL_ZERO] = ex_of(k, bw[NASAL_ZERO]);
        }

        if (freq[TRACHEAL_POLE] == freq[TRACHEAL_ZERO] &&
            bw[TRACHEAL_POLE] == bw[TRACHEAL_ZERO]) {
            k->filters[TRACHEAL_ZERO].enabled = 0;
            k->filters[TRACHEAL_POLE].enabled = 0;
        } else {
            k->filters[TRACHEAL_ZERO].enabled = k->unknown_1498;
            k->filters[TRACHEAL_POLE].enabled = k->filters[TRACHEAL_ZERO].enabled;

            freq[TRACHEAL_POLE] = clamp(freq[TRACHEAL_POLE], 10, 5000);
            freq[TRACHEAL_ZERO] = clamp(freq[TRACHEAL_ZERO], 10, 5000);
            bw[TRACHEAL_POLE]   = clamp(bw[TRACHEAL_POLE], 10, 4000);
            bw[TRACHEAL_ZERO]   = clamp(bw[TRACHEAL_ZERO], 10, 4000);

            k->co[TRACHEAL_POLE] = co_of(k, freq[TRACHEAL_POLE]);
            k->co[TRACHEAL_ZERO] = co_of(k, freq[TRACHEAL_ZERO]);
            k->ex[TRACHEAL_POLE] = ex_of(k, bw[TRACHEAL_POLE]);
            k->ex[TRACHEAL_ZERO] = ex_of(k, bw[TRACHEAL_ZERO]);
        }
    }

    /* Clamp and look up each cascade formant, and hand its frequency across to
       the matching parallel slot, which only supplies its own bandwidth. */
    for (i = CASCADE_BASE; i < k->n_formants + CASCADE_BASE; i++) {
        k->filters[i].enabled = k->unknown_1498;

        freq[i] = clamp(freq[i], 10, 5000);
        bw[i] = clamp(bw[i], 10, 4000);

        k->co[i] = co_of(k, freq[i]);
        k->ex[i] = ex_of(k, bw[i]);

        if (k->unknown_149c > 0) {
            if (amp[i + 8] != 0)
                k->filters[i + 8].enabled = k->unknown_149c;

            k->co[i + 8] = k->co[i];

            bw[i + 8] = clamp(bw[i + 8], 10, 4000);
            k->ex[i + 8] = ex_of(k, bw[i + 8]);
        } else {
            k->filters[i + 8].enabled = 0;
        }
    }

    /* Cascade coefficients, and where a formant moved, a three-sample slide
       from the old coefficients to the new instead of a step. */
    for (i = first; i < k->n_formants + CASCADE_BASE; i++) {
        filter_parms *fp = &k->filters[i];

        if (fp->enabled == 0)
            continue;

        set_coefficients(fp, k->ex[i], k->co[i]);

        /* A zero previous frequency means this resonator has no history to
           slide from, so the first frame after it wakes up steps instead. */
        if (fp->prev_freq != 0 &&
            (freq[i] != fp->prev_freq || bw[i] != fp->prev_bw)) {
            int16_t db = (int16_t)((fp->sb - fp->old_sb) >> 2);
            int16_t dc = (int16_t)((fp->sc - fp->old_sc) >> 2);

            fp->ramp = 3;
            for (j = 0; j < 3; j++) {
                fp->c[j] = (int16_t)(fp->old_sc + (j + 1) * dc);
                fp->b[j] = (int16_t)(fp->old_sb + (j + 1) * db);
                fp->c[j] = (int16_t)(fp->c[j] & ~3);
                fp->b[j] = (int16_t)(fp->b[j] & ~1);
                fp->a[j] = (int16_t)(0x2000 - (fp->b[j] >> 1) - (fp->c[j] >> 2));
            }
        } else {
            fp->ramp = 0;
        }
    }

    four = 4;
    ab_base = k->af + k->unknown_1834 + k->unknown_183c;

    if (parms[P_AB] != 0)
        k->ab_gain = (int16_t)fxmul_scaled((int16_t)-four,
                                               db2lin(ab_base + parms[P_AB]));

    /* Parallel branch: same shape, but each resonator is scaled by its own
       amplitude rather than left at unity. */
    for (i = PARALLEL_BASE; i < k->n_formants + PARALLEL_BASE; i++) {
        filter_parms *fp = &k->filters[i];

        if (fp->enabled != 0) {
            fp->sc = (int16_t)fxmul_scaled((int16_t)-k->ex[i], k->ex[i]);
            fp->sb = (int16_t)fxmul_scaled(k->ex[i], k->co[i]);
            fp->fresh = 1;
            fp->kind = 2;

            if (amp[i] != 0) {
                int16_t gain = (int16_t)fxmul_scaled(four,
                                                     db2lin(ab_base + amp[i]));

                fp->sb = (int16_t)(fp->sb & ~1);
                fp->sc = (int16_t)(fp->sc & ~3);
                fp->sa = (int16_t)(0x2000 - (fp->sb >> 1) - (fp->sc >> 2));
                fp->sa = (int16_t)fxmul_scaled(fp->sa, gain);
            } else {
                fp->sa = 0;
            }

            fp->unknown_08 = 1;
        }

        /* Adjacent parallel formants are summed in opposite polarity, so the
           gain flips sign every slot whether or not this one is live. */
        four = (int16_t)-four;
    }

    /* Convert the two zeros from resonator scaling into zero_filter's, where
       one is sixteen rather than 0x2000. */
    if (k->unknown_1498 > 0) {
        for (i = NASAL_ZERO; i <= TRACHEAL_ZERO; i++) {
            filter_parms *fp = &k->filters[i];
            int16_t sa;

            if (fp->enabled == 0)
                continue;

            sa = fp->sa;
            k->zeros[i].b = (int16_t)((-(fp->sb << 3)) / sa);
            k->zeros[i].c = (int16_t)((-(fp->sc << 2)) / sa);
            k->zeros[i].a = (int16_t)(16 - k->zeros[i].b - k->zeros[i].c);

            if (fp->ramp == 0)
                continue;

            for (j = 0; j < 3; j++) {
                sa = fp->a[j];
                fp->b[j] = (int16_t)((-(fp->b[j] << 3)) / sa);
                fp->c[j] = (int16_t)((-(fp->c[j] << 2)) / sa);
                fp->a[j] = (int16_t)(16 - fp->b[j] - fp->c[j]);
            }
        }
    }

    k->f0 = parms[P_F0];
    k->di = parms[P_DI];
    k->oq = parms[P_OQ];

    k->noise_count = n_samples > 200 ? 200 : n_samples;

    asp_gain = 0;
    if (k->ah != 0)
        asp_gain = (int16_t)fxmul_scaled(0x3200,
                                         db2lin(k->unknown_1834
                                                + k->unknown_1838 + k->ah));

    /* Samples come out in blocks of at most one noise buffer at a time. */
    remaining = n_samples;
    while (remaining > 0) {
        int32_t block = k->noise_count < remaining ? k->noise_count : remaining;

        k->noise_count = block;
        k->unknown_14a0 = mul32(k->noise_count, 1000) / k->cp.sample_rate;
        remaining -= k->noise_count;

        /* Nothing excited and nothing still ringing: hand back silence rather
           than run the whole synthesis chain over zeros. */
        if (k->unknown_1498 <= 0 && k->unknown_149c <= 0 &&
            k->unknown_14e0 == 0 && k->unknown_14f0 == 0 &&
            k->unknown_14e4 == 0) {
            for (i = 0; i < k->noise_count; i++)
                k->out[i] = 0;
        } else {
            int32_t left = block;      /* samples of this block still to fill */
            int32_t written = 0;       /* how far into the buffer we have got */
            int32_t filtered = 0;      /* where the bypass pole last stopped */

            k->smooth_span = 0;
            k->spans[k->smooth_span] = 0;

            if (k->unknown_14e0 != 0) {
                /* NOT YET TRANSCRIBED: silence carried in from the previous
                   frame. Only reachable on a continuation frame. */
            }

            k->smooth_span++;
            k->spans[k->smooth_span] = 0;

            if (k->unknown_14e4 != 0) {
                /* NOT YET TRANSCRIBED: a glottal period left half finished by
                   the previous frame. Only reachable on a continuation. */
            }

            k->smooth_span++;
            k->spans[k->smooth_span] = 0;

            if (k->unknown_14f0 != 0 && left != 0) {
                /* NOT YET TRANSCRIBED: likewise a carried-over closed phase. */
            }

            if (written > 0) {
                filtered = written;
                if (parms[P_AB] != 0)
                    pole_filter(&k->filters[0], k->ptr_a, written);
            }

            if (left > 0) {
                if (k->f0 != 0 && k->av != 0) {
                    /* NOT YET TRANSCRIBED: the voiced source. */
                } else {
                    if (k->ah != 0) {
                        /* Aspiration with no pitch: the whole block counts as
                           one open span, so the noise runs through unbroken. */
                        k->voicing_size = left;
                        k->unknown_14d0 = left;
                        k->smooth_span++;
                        k->spans[k->smooth_span] = left;
                        k->unknown_14d8 = 0;
                        k->smooth_span++;
                        k->spans[k->smooth_span] = 0;
                    } else {
                        k->unknown_14d0 = 0;
                        k->smooth_span++;
                        k->spans[k->smooth_span] = 0;
                        k->voicing_size = left;
                        k->unknown_14d8 = left;
                        k->smooth_span++;
                        k->spans[k->smooth_span] = left;
                    }

                    /* Neither voiced nor aspirated: the block is silent at the
                       source, and the resonators still ring over the zeros. */
                    clr_vector(k->ptr_a + written, left);
                    k->v_start = 0;
                    left = 0;
                }

                if (left > 0)
                    compute_voicing_size(k);

                if (left >= k->voicing_size && left > 0) {
                    /* NOT YET TRANSCRIBED: the whole-period source loop. */
                }

                if (left > 0) {
                    /* NOT YET TRANSCRIBED: the part-period source loop. */
                }

                k->unknown_14f0 = 0;
                k->unknown_14e4 = 0;
                k->unknown_14e0 = 0;
            }

            k->smooth_span++;
            k->spans[k->smooth_span] = 0;
            k->smooth_span++;
            k->spans[k->smooth_span] = 0;

            if (k->unknown_19f0 != 0) {
                int32_t upto = 0, m = 0;

                for (i = 0; i < k->smooth_span / 2; i++) {
                    upto += k->spans[i * 2];
                    for (; m < upto; m++)
                        k->voiced_flags[m] = 0;
                    upto += k->spans[i * 2 + 1];
                    for (; m < upto; m++)
                        k->voiced_flags[m] = 1;
                }
            }

            if (parms[P_AB] != 0)
                pole_filter(&k->filters[0], k->ptr_a + filtered,
                            k->noise_count - filtered);

            if (k->cp.unknown_1c == 0) {
                if (k->unknown_1498 > 0) {
                    if (k->ah != 0) {
                        k->unknown_19dc = (int32_t)noise(k, k->unknown_19dc);
                        fxmul1_vector(k->noise_buf, asp_gain, k->ptr_a,
                                      k->noise_count);
                    }

                    /* The cascade, run from the highest formant down so each
                       resonator sees the one above it already applied. */
                    if (k->filters[NASAL_POLE].enabled)
                        pole_filter(&k->filters[NASAL_POLE], k->ptr_a,
                                    k->noise_count);
                    if (k->filters[NASAL_ZERO].enabled)
                        zero_filter(&k->filters[NASAL_ZERO],
                                    (const zero_ABCs *)&k->zeros[NASAL_ZERO],
                                    k->ptr_a, k->noise_count);
                    if (k->filters[TRACHEAL_POLE].enabled)
                        pole_filter(&k->filters[TRACHEAL_POLE], k->ptr_a,
                                    k->noise_count);
                    if (k->filters[TRACHEAL_ZERO].enabled)
                        zero_filter(&k->filters[TRACHEAL_ZERO],
                                    (const zero_ABCs *)&k->zeros[TRACHEAL_ZERO],
                                    k->ptr_a, k->noise_count);

                    for (i = k->n_formants + 4; i > CASCADE_BASE; i--)
                        if (k->filters[i].enabled)
                            pole_filter(&k->filters[i], k->ptr_a,
                                        k->noise_count);

                    if (k->filters[CASCADE_BASE].enabled)
                        pole_filter(&k->filters[CASCADE_BASE], k->ptr_a,
                                    k->noise_count);

                    if (k->ah == 0 && k->av == 0) {
                        k->unknown_1498 -= k->unknown_14a0;
                        if (k->unknown_1498 < 0)
                            k->unknown_1498 = 0;
                    }
                }

                if (k->unknown_149c > 0) {
                    if (k->af != 0) {
                        k->unknown_19e0 = (int32_t)noise(k, k->unknown_19e0);
                        for (i = 0; i < k->noise_count; i++)
                            k->frication[i] = (int32_t)k->noise_buf[i] << 4;
                    }

                    if (parms[P_AB] != 0 && k->af != 0)
                        fxmul_vector(k->frication, k->ab_gain, k->ptr_a,
                                     k->noise_count);

                    /* Each parallel resonator runs on its own copy of the
                       frication and its output is summed back in. */
                    for (i = PARALLEL_BASE;
                         i < k->n_formants + PARALLEL_BASE; i++) {
                        int32_t m;

                        if (k->filters[i].enabled == 0)
                            continue;

                        if (k->af != 0 && amp[i] != 0) {
                            for (m = 0; m < k->noise_count; m++)
                                k->ptr_b[m] = k->frication[m];
                            pole_filter(&k->filters[i], k->ptr_b,
                                        k->noise_count);
                        } else {
                            parallel0_filter(&k->filters[i], k->ptr_b,
                                             k->noise_count);
                        }

                        /* The original compares this amplitude as a double
                           against zero, which is where its three floating
                           point instructions come from. Every int32 converts
                           exactly, so an integer test gives the same answer. */
                        if (amp[i] == 0) {
                            k->filters[i].enabled -= k->unknown_14a0;
                            if (k->filters[i].enabled < 0)
                                k->filters[i].enabled = 0;
                        }

                        for (m = 0; m < k->noise_count; m++)
                            k->ptr_a[m] += k->ptr_b[m];
                    }

                    if (k->af == 0) {
                        k->unknown_149c -= k->unknown_14a0;
                        if (k->unknown_149c < 0)
                            k->unknown_149c = 0;
                    }
                }
            }

            /* Down from the accumulator's headroom into sample range, keeping
               the largest magnitude seen so KlattMax can report it. */
            for (i = 0; i < k->noise_count; i++) {
                int32_t v;

                k->out[i] = k->ptr_a[i] >> 4;
                v = k->out[i];
                if (v < 0)
                    v = (int32_t)(-(uint32_t)v);
                if (v > k->max)
                    k->max = v;
            }
        }

        output_speech(k, k->noise_count);
    }

    k->length += n_samples;

    for (i = first; i < 21; i++) {
        filter_parms *fp = &k->filters[i];

        if (fp->enabled != 0) {
            fp->prev_freq = freq[i];
            fp->prev_bw = bw[i];
            fp->old_sa = fp->sa;
            fp->old_sb = fp->sb;
            fp->old_sc = fp->sc;
            fp->old_kind = fp->kind;
            fp->old_fresh = fp->fresh;
            fp->old_unknown_08 = fp->unknown_08;
            fp->frames++;
        } else {
            fp->prev_freq = 0;
            fp->prev_bw = 0;
            fp->d1 = 0;
            fp->d2 = 0;
            fp->old_sa = 0;
            fp->old_sb = 0;
            fp->old_sc = 0;
            fp->old_kind = 0;
            fp->old_fresh = 0;
            fp->unknown_08 = 1;
            fp->frames = 0;
        }
    }

    return 1;
}
