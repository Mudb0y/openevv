/* The wideband voice.
 *
 * Synthesising at a higher rate outright is not Eloquence at a higher rate.
 * The resonators are two-pole digital filters, and a digital resonator near
 * the top of its band passes more than the same resonator does once the
 * band has been widened under it; five in a row add that up, so the voice
 * comes out three decibels down from 1.5 to 3 kHz and five down from 3 to
 * 5.5 at sixteen thousand, which is where its character lives. Doing the
 * arithmetic in doubles changes none of that, which was measured: it is the
 * model and not the precision.
 *
 * So the voice below the top of its own band is taken from where it has
 * always been, eleven thousand and twenty five, and raised by the same sinc
 * every rate above that is raised by. What goes above is made by a second
 * synthesiser, the companion, running at twice the rate on the same frames:
 * its own idea of everything, of which only the part above its partner's
 * band is kept. The two halves are split by one filter used twice -- the
 * partner raised through the sinc is that filter's low side, and the
 * companion less the same filter is its high side -- so they meet without a
 * gap or a doubling, whatever either of them puts near the join.
 *
 * Which is why twice the rate and not sixteen thousand. The halves have to
 * land on the same instants, sample for sample, or the top of a sibilant
 * comes out after the sibilant: at sixteen thousand a five millisecond frame
 * rounds to a different length than it does at eleven, and over a minute of
 * speech the two drift apart by about an eighth of a second. At exactly twice the
 * rate the companion can take every frame length and pitch period as its
 * partner's doubled, and klatt_state's pace_rate is what tells it to.
 *
 * IBM's frames say little above five thousand hertz. The sixth to eighth
 * formants sit at five thousand, 6,300 and 7,500 on every frame and their
 * parallel amplitudes are always nought, and IBM's synthesiser runs only the
 * first five and stops every one at five thousand, near the top of its band
 * at eleven thousand. A companion run that way gives a vowel nothing past
 * the join. So this one runs all eight, lets the three above the fifth go
 * where the frame puts them at twice the bandwidths it gives them, and
 * raises its cascade -- the voicing and the aspiration, never the frication
 * -- by six decibels; a vowel then falls four to six decibels across the
 * join and goes on falling, as it falls under it. And it keeps four bits
 * below the sample: its high side is raised fourteen decibels, and rounded
 * to sixteen bits first it carried a floor eighty decibels down, too quiet
 * to hear and loud enough to hide what a vowel has up there.
 *
 * Its resonators round once, to nearest, where IBM's round three products
 * down. Rounded IBM's way they sit off nought in silence, and when the
 * cascade is switched off after speech the step back to nought was a click
 * in the top, twenty-five to sixty-five milliseconds after the voice stopped.
 *
 * The rest of what the companion has above the join is frication: the
 * skirts of the resonators an s, an sh and a z are made with, and the bypass
 * an f and a th are. None of the above touches it.
 *
 * How loud the top is, where its noise stops and how a vowel crosses the
 * join were settled by ear, and docs/notes/sample-rates.md has the rounds:
 * fourteen decibels over what the companion makes, eight thousand hertz,
 * and the cascade six over that. EVV_WIDE_TOP, EVV_WIDE_EDGE and
 * EVV_WIDE_VOICED move them, as experiments rather than settings.
 */

#include <math.h>
#include <stdlib.h>
#include <string.h>

#include "klatt_wide.h"
#include "klatt_rates.h"
#include "eci_pcm.h"

struct WideBand {
    klatt_state  *klatt;
    int16_t      *ex;
    int16_t      *co;
    /* What the companion made of the frame in hand, and how much of it the
       partner's samples have used. */
    int32_t      *fifo;
    int32_t       have;
    int32_t       used;
    int32_t       fifo_room;
    /* The partner raised to the wide rate, and the filter that matches it:
       the middle tap and the half before it, since it is symmetric. */
    PcmResampler  up;
    int32_t      *lp;
    int32_t       delay;
    /* The companion's samples the filter is still reaching back over,
       followed by this run's. */
    int32_t      *line;
    int32_t       line_room;
    int32_t      *out;
    int32_t       out_room;
    int32_t       top;
    int32_t       voiced;
    int32_t       edge;
};

/* ---- what a listener chooses ------------------------------------------ */

/* Each read once: an engine that changed its mind halfway through an
   utterance would be comparing two things at once. */

/* How loud the companion's band is against what the companion made, in
   decibels. Its noise is white over twice its partner's band, so at the
   same amplitude every hertz of it carries half the power; three would put
   that back, and the rest of the fourteen is the ear's. Fourteen decibels
   is ten to the power fourteen twentieths, here in units of 2^-16. */
#define WIDE_TOP  328458

/* A level in decibels from the environment, in units of 2^-16, or the one
   settled on. */
static int32_t wide_level(const char *name, int32_t settled)
{
    const char *say = getenv(name);

    if (say != 0 && *say != 0) {
        double db = atof(say);

        /* A level being tried rather than the one settled on, which is the
           only reason a double is worth having here. */
        if (db >= -40.0 && db <= 30.0)
            return (int32_t)(pow(10.0, db / 20.0) * 65536.0 + 0.5);
    }
    return settled;
}

static int32_t wide_top(void)
{
    static int     decided;
    static int32_t top;

    if (!decided) {
        decided = 1;
        top = wide_level("EVV_WIDE_TOP", WIDE_TOP);
    }
    return top;
}

/* Where the companion's noise stops, in hertz. Nought leaves it white to
   the companion's own Nyquist. Eight thousand was the ear's choice over
   eleven and nine and a half. */
static int32_t wide_edge(void)
{
    static int     decided;
    static int32_t edge = 8000;

    if (!decided) {
        const char *say = getenv("EVV_WIDE_EDGE");

        decided = 1;
        if (say != 0 && *say != 0) {
            int32_t v = (int32_t)atoi(say);

            if (v >= 0 && v < WIDE_RATE / 2)
                edge = v;
        }
    }
    return edge;
}

/* How much the companion's cascade is raised, in units of 2^-16: six
   decibels. With the formants above the fifth at twice their bandwidths
   that has a vowel fall four to six decibels across the join, which the ear
   chose over the same formants at IBM's bandwidths raised by nought, six
   and twelve. */
#define WIDE_VOICED  130762

static int32_t wide_voiced(void)
{
    static int     decided;
    static int32_t voiced;

    if (!decided) {
        decided = 1;
        voiced = wide_level("EVV_WIDE_VOICED", WIDE_VOICED);
    }
    return voiced;
}

/* What the companion runs above IBM's synthesiser: every formant a frame
   has, the three above the fifth free to go where the frame puts them, as
   far as the same fraction of its band as IBM's five thousand is of eleven
   thousand; and four more bits than a sample, which are what its high side
   is raised from. */
#define WIDE_FORMANTS  8
#define WIDE_CO_LAST   10000
#define WIDE_KEEP      4

/* Where the sixth formant's bandwidth and the eighth's sit in a frame, in
   klatt_synth.c's order, and how many words a frame has. */
#define WF_B6     22
#define WF_B8     26
#define WF_WORDS  62

/* ---- the companion ----------------------------------------------------- */

static int wide_collect(void *user, KlattSamplesStruct *s)
{
    return klatt_wide_feed((WideBand *)user, s->samples, s->count);
}

/* Room for n words in a buffer kept between calls, so a steady stream
   allocates once. */
static int wide_room(int32_t **buf, int32_t *room, int32_t n)
{
    int32_t *bigger;

    if (n <= *room)
        return 1;
    bigger = realloc(*buf, (size_t)n * sizeof(int32_t));
    if (bigger == 0)
        return 0;
    *buf = bigger;
    *room = n;
    return 1;
}

/* The low side of the split, drawn from the resampler's own kernel rather
   than designed again, so that the two cannot disagree about where the band
   ends whatever EVV_SINC_CUTOFF and EVV_SINC_TAPS say. The resampler works
   out each output sample from the input samples on one side of it or the
   other, alternately, and its weights for each such set come to exactly
   one; at the wide rate those are the even and the odd taps, so read in
   units of 2^-31 rather than 2^-30 each set comes to a half and the two
   come to one between them, exactly. */
static int wide_draw(WideBand *w)
{
    int32_t j;

    w->delay = pcm_resample_delay(&w->up) * (WIDE_RATE / WIDE_PARTNER);
    w->lp = malloc((size_t)(w->delay + 1) * sizeof(int32_t));
    if (w->lp == 0)
        return 0;
    for (j = 0; j <= w->delay; j++)
        w->lp[j] = pcm_resample_weight(&w->up, j - w->delay, 2);
    return 1;
}

WideBand *klatt_wide_new(void)
{
    WideBand *w = calloc(1, sizeof *w);

    if (w == 0)
        return 0;

    w->klatt = klatt_new(w);
    w->ex = malloc(KLATT_EX_COUNT * sizeof(int16_t));
    w->co = malloc((WIDE_CO_LAST - KLATT_CO_FIRST + 1) * sizeof(int16_t));
    if (w->klatt == 0 || w->ex == 0 || w->co == 0
        || !klatt_buildRateTablesTo(WIDE_RATE, w->ex, w->co, WIDE_CO_LAST)
        || !pcm_resample_start(&w->up, WIDE_PARTNER, WIDE_RATE, CVT_SINC)
        || !wide_draw(w)
        || !wide_room(&w->line, &w->line_room, 2 * w->delay + 512)) {
        klatt_wide_delete(w);
        return 0;
    }
    memset(w->line, 0, (size_t)w->line_room * sizeof(int32_t));

    w->top = wide_top();
    w->voiced = wide_voiced();
    w->edge = wide_edge();
    return w;
}

void klatt_wide_delete(WideBand *w)
{
    if (w == 0)
        return;
    if (w->klatt != 0)
        klatt_delete(w->klatt);
    pcm_resample_end(&w->up);
    free(w->ex);
    free(w->co);
    free(w->lp);
    free(w->fifo);
    free(w->line);
    free(w->out);
    free(w);
}

int klatt_wide_setup(WideBand *w, KlattConstParms cp)
{
    cp.sample_rate = WIDE_RATE;
    cp.callback_mode = 2;
    cp.samples_fn = wide_collect;
    cp.n_formants = WIDE_FORMANTS;

    /* The tables first: KlattSetConstParms leaves them alone at a rate it
       does not know by name. */
    KlattSetRateTables(w->klatt, w->ex, w->co);
    KlattSetConstParms(w->klatt, cp);
    w->klatt->pace_rate = WIDE_PARTNER;
    klatt_noise_edge(&w->klatt->noise_filter, WIDE_RATE, w->edge);
    w->klatt->high_ceiling = WIDE_CO_LAST;
    w->klatt->cascade_gain = w->voiced;
    w->klatt->out_keep = WIDE_KEEP;
    w->klatt->exact_poles = 1;
    return 1;
}

int klatt_wide_open(WideBand *w)
{
    if (w->klatt->open_state == 2)
        KlattClose(w->klatt);
    return KlattOpen(w->klatt);
}

void klatt_wide_close(WideBand *w)
{
    KlattClose(w->klatt);
}

void klatt_wide_volume(WideBand *w, int32_t volume)
{
    klattSetVolumeMultiplier(w->klatt, volume);
}

int klatt_wide_frame(WideBand *w, const int32_t *frame)
{
    int32_t f[WF_WORDS];
    int32_t j;

    /* Whatever is left of the last frame was cut short by an interrupt on
       the partner's side, and belongs to no sample the partner will make. */
    w->have = 0;
    w->used = 0;

    /* The three formants IBM's synthesiser never ran, at twice the
       bandwidths its frames give them, which the ear chose over IBM's own at
       the same level. */
    memcpy(f, frame, sizeof f);
    for (j = WF_B6; j <= WF_B8; j += 2)
        f[j] *= 2;
    return KlattSynth(w->klatt, f);
}

int klatt_wide_feed(WideBand *w, const int32_t *s, int32_t n)
{
    if (n <= 0)
        return 1;
    if (w->used == w->have) {
        w->have = 0;
        w->used = 0;
    }
    if (!wide_room(&w->fifo, &w->fifo_room, w->have + n))
        return 0;
    memcpy(w->fifo + w->have, s, (size_t)n * sizeof(int32_t));
    w->have += n;
    return 1;
}

/* ---- the join ---------------------------------------------------------- */

/* A sample in units of 2^-32, to sixteen bits. */
static int32_t wide_sample(int64_t v)
{
    v = fx_shift(v, 32);
    if (v > 32767)
        return 32767;
    if (v < -32768)
        return -32768;
    return (int32_t)v;
}

const int32_t *klatt_wide_mix(WideBand *w, const int32_t *in, int32_t n,
                              int32_t *made)
{
    int32_t want = n * (WIDE_RATE / WIDE_PARTNER);
    int32_t span = 2 * w->delay;
    int32_t take, got, m;

    *made = 0;
    if (n <= 0)
        return in;
    if (!wide_room(&w->line, &w->line_room, span + want)
        || !wide_room(&w->out, &w->out_room, want))
        return 0;

    /* Short only if the partner made more than the companion was asked
       for, which pacing rules out; silence is the honest stand-in. */
    take = w->have - w->used;
    if (take > want)
        take = want;
    if (take > 0)
        memcpy(w->line + span, w->fifo + w->used,
               (size_t)take * sizeof(int32_t));
    memset(w->line + span + take, 0, (size_t)(want - take) * sizeof(int32_t));
    w->used += take;

    got = (int32_t)pcm_resample(&w->up, in, (uint32_t)n, w->out);

    for (m = 0; m < got; m++) {
        const int32_t *x = w->line + m;
        int64_t low = (int64_t)w->lp[w->delay] * x[w->delay];
        int64_t high;
        int32_t j;

        for (j = 0; j < w->delay; j++)
            low += (int64_t)w->lp[j] * ((int64_t)x[j] + x[span - j]);
        /* The companion less its low side, in units of 2^-31 of its own
           samples, which are WIDE_KEEP bits finer than its partner's,
           brought to 2^-16 of a sample so that the level can multiply it
           without running out of room. */
        high = fx_shift(((int64_t)x[w->delay] << 31) - low, 15 + WIDE_KEEP);
        w->out[m] = wide_sample(((int64_t)w->out[m] << 32) + high * w->top);
    }

    memmove(w->line, w->line + want, (size_t)span * sizeof(int32_t));
    *made = got;
    return w->out;
}
