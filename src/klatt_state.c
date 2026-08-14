#include <stddef.h>
#include <stdlib.h>
#include <string.h>

#include "klatt_state.h"

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
AT(period, 0x0a84);
AT(error_fn, 0x0ab8);
AT(callback_mode, 0x0abc);
AT(samples_fn, 0x0ac0);
AT(buf_a, 0x0acc);
AT(ptr_a, 0x0dec);
AT(buf_b, 0x1118);
AT(ptr_b, 0x1438);
AT(unknown_1498, 0x1498);
AT(unknown_14a0, 0x14a0);
AT(sample_rate, 0x14ac);
AT(unknown_14b0, 0x14b0);
AT(v_start, 0x14b4);
AT(noise_count, 0x14c4);
AT(voicing_size, 0x14c8);
AT(unknown_14d0, 0x14d0);
AT(unknown_14e0, 0x14e0);
AT(unknown_14f0, 0x14f0);
AT(length, 0x14f4);
AT(max, 0x14f8);
AT(noise_limit, 0x14fc);
AT(pairs, 0x1500);
AT(smooth_noise, 0x1824);
AT(smooth_span, 0x1840);
AT(noise_buf, 0x184c);
AT(unknown_19e4, 0x19e4);
AT(callback_result, 0x19ec);
AT(output_samples, 0x1d1c);

typedef char klatt_state_is_0x1d24[sizeof(klatt_state) == 0x1d24 ? 1 : -1];

/* Fill the noise buffer, then optionally halve it in place over a series of
   spans. Each pair says how far to skip and how far to keep attenuating, so
   the smoothing follows the pitch periods rather than a fixed window. */
uint32_t noise(klatt_state *k, uint32_t seed)
{
    int32_t i, limit, j;

    seed = klatt_rand(k->noise_buf, k->noise_count, seed);

    if (k->smooth_noise == 0)
        return seed;

    i = 0;
    limit = k->noise_limit;

    for (j = 0; j < k->smooth_span / 2; j++) {
        for (; i < limit; i++)
            k->noise_buf[i] = (int16_t)(k->noise_buf[i] >> 1);

        i = i + k->pairs[j].a;
        limit = i + k->pairs[j].b;
    }

    return seed;
}

void compute_v_start(klatt_state *k)
{
    k->v_start = k->v_start + mul32(k->voicing_size, 1000)
               - mul32(k->period, 10000) / k->sample_rate;
}

void compute_voicing_size(klatt_state *k)
{
    k->voicing_size =
        (mul32(k->period, 10000) / k->sample_rate - k->v_start + 999) / 1000;

    k->unknown_14d0 =
        (mul32(mul32(k->period, 100), k->unknown_14b0)
         + mul32(499 - k->v_start, k->sample_rate))
        / mul32(k->sample_rate, 1000);

    k->unknown_14d4 = k->unknown_14d0;
    k->unknown_14d8 = k->voicing_size - k->unknown_14d0;
    k->unknown_14dc = k->unknown_14d8;
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

    if (k->callback_mode != 2)
        return;

    k->callback_result = k->samples_fn(k->user, &s);
}

void *klatt_new(void *user)
{
    klatt_state *k = calloc(1, sizeof(klatt_state));

    if (k == NULL)
        return NULL;

    k->version = KlattVersionString;
    k->user = user;
    k->open_state = 0;
    k->unknown_14e0 = 0;
    k->unknown_14e4 = 0;
    k->unknown_14f0 = 0;
    k->length = 0;
    k->max = 0;
    k->unknown_19e4 = 0;
    k->unknown_19e8 = 0;
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
        k->error_fn(k->user, " KlattOpen error",
                    "Call KlattSetConstParms at least once before KlattOpen!");
        return 0;
    }

    if (k->open_state == 2) {
        k->error_fn(k->user, " KlattOpen error", "Synthesizer is already open!");
        return 0;
    }

    k->open_state = 2;
    k->ptr_a = k->buf_a;
    k->ptr_b = k->buf_b;
    k->unknown_14a0 = 0;

    for (i = 0; i < 21; i++) {
        k->filters[i].d1 = 0;
        k->filters[i].d2 = 0;
        k->filters[i].unknown_2c[0] = 0;
        k->filters[i].unknown_2c[2] = -1;
        k->filters[i].unknown_2c[3] = -1;
        k->filters[i].enabled = 0;
        k->filters[i].ramp = 0;
        k->filters[i].unknown_50 = 0;
    }

    k->unknown_0010 = 0;
    k->unknown_1498 = 0;
    k->unknown_149c = 0;
    k->unknown_14e0 = 0;
    k->unknown_14e4 = 0;
    k->unknown_14f0 = 0;
    k->length = 0;
    k->output_samples = 1;
    k->max = 0;
    k->unknown_19e4 = 0;
    k->unknown_19e8 = 0;

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
