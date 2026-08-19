#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "klatt_state.h"
#include "evv_arena.h"

/* Where every field of the synthesiser's block sits, as the original had it.
   It only has to hold where our block and the original's are handed to each
   other, which is the differential build; a build that links nothing of
   theirs lets the compiler place the fields and the pointers among them
   widen. */
#if !defined(EVV_ARENA) || !EVV_ARENA

#define AT(field, offset) \
    typedef char field##_at_##offset[offsetof(klatt_state, field) == offset ? 1 : -1]

AT(version, 0x0000);
AT(user, 0x0004);
AT(unknown_0010, 0x0010);
AT(const_parms_set, 0x0014);
AT(volume, 0x0058);
AT(open_state, 0x005c);
AT(filters, 0x0064);
AT(out, 0x0760);
AT(cp, 0x0a80);
AT(buf_a, 0x0acc);
AT(ptr_a, 0x0dec);
AT(frication, 0x0df0);
AT(buf_b, 0x1118);
AT(ptr_b, 0x1438);
AT(unknown_1498, 0x1498);
AT(unknown_14a0, 0x14a0);
AT(di, 0x14a8);
AT(f0, 0x14ac);
AT(oq, 0x14b0);
AT(v_start, 0x14b4);
AT(closed_pct, 0x14a4);
AT(pulse_amp, 0x14b8);
AT(pulse_amp_base, 0x14c0);
AT(noise_count, 0x14c4);
AT(voicing_size, 0x14c8);
AT(diplo_shift, 0x14cc);
AT(open_len, 0x14d0);
AT(carry_lead, 0x14e0);
AT(carry_period, 0x14e8);
AT(carry_closed, 0x14f0);
AT(length, 0x14f4);
AT(max, 0x14f8);
AT(spans, 0x14fc);
AT(tilt, 0x1820);
AT(av, 0x1824);
AT(ah, 0x1828);
AT(af, 0x182c);
AT(smooth_span, 0x1840);
AT(flutter, 0x1848);
AT(noise_buf, 0x184c);
AT(unknown_19dc, 0x19dc);
AT(diplo_on, 0x19e4);
AT(callback_result, 0x19ec);
AT(unknown_1818, 0x1818);
AT(unknown_1830, 0x1830);
AT(voiced_flags, 0x19f8);
AT(unknown_1d18, 0x1d18);
AT(output_samples, 0x1d1c);
AT(rate_code, 0x1d20);

typedef char klatt_state_is_0x1d24[sizeof(klatt_state) == 0x1d24 ? 1 : -1];

#endif

/* Fill the noise buffer, then optionally halve it in place over a series of
   spans. Each pair says how far to skip and how far to keep attenuating, so
   the smoothing follows the pitch periods rather than a fixed window. */
uint32_t noise(klatt_state *k, uint32_t seed)
{
    int32_t i, limit, j;

    seed = klatt_rand(k->noise_buf, k->noise_count, seed);

    if (k->av == 0)
        return seed;

    i = 0;
    limit = k->spans[0];

    for (j = 0; j < k->smooth_span / 2; j++) {
        for (; i < limit; i++)
            k->noise_buf[i] = (int16_t)(k->noise_buf[i] >> 1);

        i = i + k->spans[1 + 2 * j];
        limit = i + k->spans[2 + 2 * j];
    }

    return seed;
}

void compute_v_start(klatt_state *k)
{
    k->v_start = k->v_start + mul32(k->voicing_size, 1000)
               - mul32(k->cp.sample_rate, 10000) / k->f0;
}

void compute_voicing_size(klatt_state *k)
{
    k->voicing_size =
        (mul32(k->cp.sample_rate, 10000) / k->f0 - k->v_start + 999) / 1000;

    k->open_len =
        (mul32(mul32(k->cp.sample_rate, 100), k->oq)
         + mul32(499 - k->v_start, k->f0))
        / mul32(k->f0, 1000);

    k->open_part = k->open_len;
    k->closed_len = k->voicing_size - k->open_len;
    k->closed_part = k->closed_len;
}

void output_speech(klatt_state *k, int32_t n)
{
    KlattSamplesStruct s;
    int32_t i;

    if (k->output_samples == 0)
        return;

    s.count = n;
    s.samples = k->out;

    if (k->volume != 100) {
        for (i = 0; i < n; i++)
            k->out[i] = mul32(k->out[i], k->volume) / 100;
    }

    if (k->cp.callback_mode != 2)
        return;

    k->callback_result = k->cp.samples_fn(k->user, &s);
}

void *klatt_new(void *user)
{
    klatt_state *k = calloc(1, sizeof(klatt_state));

    if (k == NULL)
        return NULL;

    k->version = KlattVersionString;
    k->user = user;
    k->open_state = 0;
    k->carry_lead = 0;
    k->carry_open = 0;
    k->carry_closed = 0;
    k->length = 0;
    k->max = 0;
    k->diplo_on = 0;
    k->diplo_alt = 0;
    k->output_samples = 1;

    return k;
}

void klatt_delete(void *handle)
{
    if (verifyKlattHandle(handle))
        free(handle);
}

int KlattOpen(void *handle)
{
    klatt_state *k = handle;
    int32_t i;

    if (!verifyKlattHandle(handle))
        return 0;

    if (k->const_parms_set != 1) {
        k->cp.error_fn(k->user, " KlattOpen error",
                    "Call KlattSetConstParms at least once before KlattOpen!");
        return 0;
    }

    if (k->open_state == 2) {
        k->cp.error_fn(k->user, " KlattOpen error", "Synthesizer is already open!");
        return 0;
    }

    k->open_state = 2;
    k->ptr_a = k->buf_a;
    k->ptr_b = k->buf_b;
    k->unknown_14a0 = 0;

    for (i = 0; i < 21; i++) {
        k->filters[i].d1 = 0;
        k->filters[i].d2 = 0;
        k->filters[i].prev_freq = 0;
        k->filters[i].unknown_34 = -1;
        k->filters[i].unknown_38 = -1;
        k->filters[i].enabled = 0;
        k->filters[i].ramp = 0;
        k->filters[i].frames = 0;
    }

    k->unknown_0010 = 0;
    k->unknown_1498 = 0;
    k->unknown_149c = 0;
    k->carry_lead = 0;
    k->carry_open = 0;
    k->carry_closed = 0;
    k->length = 0;
    k->output_samples = 1;
    k->max = 0;
    k->diplo_on = 0;
    k->diplo_alt = 0;

    return 1;
}

void KlattClose(void *handle)
{
    klatt_state *k = handle;

    if (verifyKlattHandle(handle))
        k->open_state = 0;
}

int32_t KlattLength(void *handle)
{
    klatt_state *k = handle;

    return verifyKlattHandle(handle) ? k->length : 0;
}

int32_t KlattMax(void *handle)
{
    klatt_state *k = handle;

    return verifyKlattHandle(handle) ? k->max : 0;
}

void KlattSetOutputSamplesOption(void *handle, int32_t option)
{
    klatt_state *k = handle;

    if (verifyKlattHandle(handle))
        k->output_samples = option;
}

void klattSetVolumeMultiplier(void *handle, int32_t volume)
{
    klatt_state *k = handle;

    if (verifyKlattHandle(handle))
        k->volume = volume;
}

int errorKlattIgnore(void)
{
    return 0;
}

/* The parameter block arrives by value and goes into the state wholesale, then
   a handful of fields are copied out of it into working positions. Anything
   other than 8000 or 11025 leaves the excitation and cosine table pointers
   exactly as they were, which for a fresh handle means null. */
void KlattSetConstParms(void *handle, KlattConstParms parms)
{
    klatt_state *k = handle;

    if (!verifyKlattHandle(handle))
        return;

    if (k->open_state == 2)
        KlattClose(handle);

    k->cp = parms;

    k->unknown_19dc = k->cp.unknown_10;
    k->unknown_19e0 = k->unknown_19dc;

    if (k->cp.sample_rate == 11025) {
        k->ex_table = klatt_EX11;
        k->co_table = klatt_CO11;
    } else if (k->cp.sample_rate == 8000) {
        k->ex_table = klatt_EX8;
        k->co_table = klatt_CO8;
    }

    k->v_start = 0;
    k->n_formants = k->cp.n_formants;
    k->unknown_1d18 = 0;
    k->unknown_1830 = k->cp.unknown_20;
    k->unknown_1834 = k->cp.unknown_2c;
    k->unknown_1838 = k->cp.unknown_24;
    k->unknown_183c = k->cp.unknown_28;

    if (k->cp.sample_rate == 11025)
        k->rate_code = 1;
    else if (k->cp.sample_rate == 8000)
        k->rate_code = 0;
    else
        k->rate_code = 2;

    k->const_parms_set = 1;
}
