/* Differential harness.
 *
 * Links IBM's original clsyn.obj beside our transcription and runs both over
 * the same inputs. Built as a 32-bit PE because the object is MSVC-mangled
 * x86 COFF; it runs under Wine. A pass here means bit-identical, not close.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include <stdlib.h>
#include <stddef.h>

#include "klatt_fx.h"
#include "klatt_state.h"

extern void     ibm_clr_vector(int32_t *v, int32_t n);
extern uint32_t ibm_klatt_rand(int16_t *out, int32_t n, uint32_t seed);
extern int16_t  ibm_fxdivl(int32_t num, int32_t den);
extern void     ibm_fxmul_vector(int32_t *src, int16_t coef, int32_t *acc, int32_t n);
extern void     ibm_fxmul1_vector(int16_t *src, int16_t coef, int32_t *acc, int32_t n);
extern int32_t  ibm_db2lin(int32_t db);
extern int      ibm_verifyKlattHandle(void *handle);
extern const char ibm_KlattVersion[];
extern void     ibm_pole_filter(filter_parms *fp, int32_t *buf, int32_t n);
extern void     ibm_parallel0_filter(filter_parms *fp, int32_t *buf, int32_t n);
extern void     ibm_zero_filter(filter_parms *fp, const zero_ABCs *z,
                                int32_t *buf, int32_t n);
extern uint32_t ibm_noise(klatt_state *k, uint32_t seed);
extern void     ibm_compute_v_start(klatt_state *k);
extern void     ibm_compute_voicing_size(klatt_state *k);
extern void     ibm_output_speech(klatt_state *k, int32_t n);
extern void    *ibm_klatt_new(void *user);
extern void     ibm_klatt_delete(void *handle);
extern int      ibm_KlattOpen(void *handle);
extern void     ibm_KlattClose(void *handle);
extern int32_t  ibm_KlattLength(void *handle);
extern int32_t  ibm_KlattMax(void *handle);
extern void     ibm_KlattSetOutputSamplesOption(void *handle, int32_t option);
extern void     ibm_klattSetVolumeMultiplier(void *handle, int32_t volume);
extern int      ibm_errorKlattIgnore(void);

#define MAXLEN 64

static int total_cases;
static int total_bad;

static uint32_t rng_state;

static void rng_seed(uint32_t s) { rng_state = s; }

static uint32_t rng_next(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state;
}

static void report(const char *name, int cases, int bad, int skipped)
{
    total_cases += cases;
    total_bad += bad;
    if (skipped)
        printf("%-16s %6d cases, %d mismatches, %d skipped\n",
               name, cases, bad, skipped);
    else
        printf("%-16s %6d cases, %d mismatches\n", name, cases, bad);
}

/* Magnitudes that sit exactly on the branch boundaries of the staged multiply,
   where a transcription error is most likely to hide. */
static const int32_t boundaries[] = {
    0, 1, -1, 2, -2, 0x7fff, -0x7fff, 0x8000, -0x8000,
    0xffff, -0xffff, 0x10000, -0x10000, 0x10001, -0x10001,
    0xfffff, -0xfffff, 0x100000, -0x100000, 0x100001, -0x100001,
    0xffffff, -0xffffff, 0x1000000, -0x1000000, 0x1000001, -0x1000001,
    0xfffffff, -0xfffffff, 0x10000000, -0x10000000, 0x10000001, -0x10000001,
    0x7ffffffe, -0x7ffffffe, 0x7fffffff, (-0x7fffffff - 1)
};
#define NBOUND ((int)(sizeof(boundaries) / sizeof(boundaries[0])))

static void test_fxdivl(void)
{
    int cases = 0, bad = 0, skipped = 0;
    int i, j;

    for (i = 0; i < NBOUND; i++) {
        for (j = 0; j < NBOUND; j++) {
            int32_t num = boundaries[i], den = boundaries[j];
            int16_t a, b;

            /* Normalising in the original never terminates when the numerator
               is a nonzero multiple of 65536 and smaller than the divisor. */
            int32_t an = num < 0 ? -num : num;
            int32_t ad = den < 0 ? -den : den;
            if (an != 0 && (an & 0xffff) == 0 && ad != 0 && an < ad) {
                skipped++;
                continue;
            }

            a = ibm_fxdivl(num, den);
            b = fxdivl(num, den);
            cases++;
            if (a != b) {
                if (bad < 5)
                    printf("  fxdivl(%ld, %ld): ibm %d, ours %d\n",
                           (long)num, (long)den, a, b);
                bad++;
            }
        }
    }

    rng_seed(0x12345678u);
    for (i = 0; i < 200000; i++) {
        int32_t num = (int32_t)(rng_next() % 0xffffu) - 0x7fff;
        int32_t den = (int32_t)(rng_next() % 0xfffffu) - 0x7ffff;
        int16_t a, b;
        int32_t an = num < 0 ? -num : num;
        int32_t ad = den < 0 ? -den : den;

        if (an != 0 && (an & 0xffff) == 0 && ad != 0 && an < ad) {
            skipped++;
            continue;
        }

        a = ibm_fxdivl(num, den);
        b = fxdivl(num, den);
        cases++;
        if (a != b) {
            if (bad < 5)
                printf("  fxdivl(%ld, %ld): ibm %d, ours %d\n",
                       (long)num, (long)den, a, b);
            bad++;
        }
    }

    report("fxdivl", cases, bad, skipped);
}

static void test_clr_vector(void)
{
    int32_t mine[MAXLEN], theirs[MAXLEN];
    int cases = 0, bad = 0;
    int n, i;

    for (n = 0; n <= MAXLEN; n++) {
        for (i = 0; i < MAXLEN; i++)
            mine[i] = theirs[i] = (int32_t)rng_next();

        ibm_clr_vector(theirs, n);
        clr_vector(mine, n);
        cases++;
        if (memcmp(mine, theirs, sizeof(mine)) != 0) {
            if (bad < 5)
                printf("  clr_vector(n=%d) differs\n", n);
            bad++;
        }
    }

    report("clr_vector", cases, bad, 0);
}

static void test_klatt_rand(void)
{
    int16_t mine[MAXLEN], theirs[MAXLEN];
    int cases = 0, bad = 0;
    int n, k;

    rng_seed(0xdeadbeefu);
    for (k = 0; k < 64; k++) {
        uint32_t seed = rng_next();

        for (n = 0; n <= MAXLEN; n++) {
            uint32_t ra, rb;

            memset(mine, 0x5a, sizeof(mine));
            memset(theirs, 0x5a, sizeof(theirs));
            rb = ibm_klatt_rand(theirs, n, seed);
            ra = klatt_rand(mine, n, seed);
            cases++;
            if (ra != rb || memcmp(mine, theirs, sizeof(mine)) != 0) {
                if (bad < 5)
                    printf("  klatt_rand(n=%d, seed=%08lx) differs\n",
                           n, (unsigned long)seed);
                bad++;
            }
        }
    }

    report("klatt_rand", cases, bad, 0);
}

static void test_fxmul_vector(void)
{
    int32_t src[MAXLEN];
    int32_t mine[MAXLEN], theirs[MAXLEN];
    int cases = 0, bad = 0;
    int i, k;

    /* Every boundary magnitude against a spread of coefficients. */
    for (k = 0; k < NBOUND; k++) {
        int16_t coefs[] = {0, 1, -1, 32767, -32768, 12345, -12345, 256, -256};
        int c;

        for (c = 0; c < (int)(sizeof(coefs) / sizeof(coefs[0])); c++) {
            for (i = 0; i < MAXLEN; i++)
                src[i] = boundaries[(k + i) % NBOUND];
            for (i = 0; i < MAXLEN; i++)
                mine[i] = theirs[i] = (int32_t)(i * 7919);

            ibm_fxmul_vector(src, coefs[c], theirs, MAXLEN);
            fxmul_vector(src, coefs[c], mine, MAXLEN);
            cases++;
            if (memcmp(mine, theirs, sizeof(mine)) != 0) {
                if (bad < 5) {
                    for (i = 0; i < MAXLEN; i++)
                        if (mine[i] != theirs[i]) {
                            printf("  fxmul_vector src=%ld coef=%d: ibm %ld, ours %ld\n",
                                   (long)src[i], coefs[c],
                                   (long)theirs[i], (long)mine[i]);
                            break;
                        }
                }
                bad++;
            }
        }
    }

    rng_seed(0xfeedfaceu);
    for (k = 0; k < 20000; k++) {
        int16_t coef = (int16_t)rng_next();

        for (i = 0; i < MAXLEN; i++)
            src[i] = (int32_t)rng_next();
        for (i = 0; i < MAXLEN; i++)
            mine[i] = theirs[i] = (int32_t)rng_next();

        ibm_fxmul_vector(src, coef, theirs, MAXLEN);
        fxmul_vector(src, coef, mine, MAXLEN);
        cases++;
        if (memcmp(mine, theirs, sizeof(mine)) != 0) {
            if (bad < 5)
                printf("  fxmul_vector random case %d differs\n", k);
            bad++;
        }
    }

    report("fxmul_vector", cases, bad, 0);
}

static void test_fxmul1_vector(void)
{
    int16_t src[MAXLEN];
    int32_t mine[MAXLEN], theirs[MAXLEN];
    int cases = 0, bad = 0;
    int i, k;

    rng_seed(0x0badc0deu);
    for (k = 0; k < 20000; k++) {
        int16_t coef = (int16_t)rng_next();

        for (i = 0; i < MAXLEN; i++)
            src[i] = (int16_t)rng_next();
        for (i = 0; i < MAXLEN; i++)
            mine[i] = theirs[i] = (int32_t)rng_next();

        ibm_fxmul1_vector(src, coef, theirs, MAXLEN);
        fxmul1_vector(src, coef, mine, MAXLEN);
        cases++;
        if (memcmp(mine, theirs, sizeof(mine)) != 0) {
            if (bad < 5)
                printf("  fxmul1_vector random case %d differs\n", k);
            bad++;
        }
    }

    /* Extremes of the short input range, which is all fxmul1 ever sees. */
    for (k = -32768; k <= 32767; k += 1) {
        int16_t coef = (int16_t)(k & 0x7fff);

        for (i = 0; i < MAXLEN; i++)
            src[i] = (int16_t)(k + i);
        for (i = 0; i < MAXLEN; i++)
            mine[i] = theirs[i] = 0;

        ibm_fxmul1_vector(src, coef, theirs, MAXLEN);
        fxmul1_vector(src, coef, mine, MAXLEN);
        cases++;
        if (memcmp(mine, theirs, sizeof(mine)) != 0) {
            if (bad < 5)
                printf("  fxmul1_vector sweep k=%d differs\n", k);
            bad++;
        }
    }

    report("fxmul1_vector", cases, bad, 0);
}

static void test_db2lin(void)
{
    int cases = 0, bad = 0;
    int32_t db;
    int i;

    /* Everything the parameter set can actually carry, plus the clamp edge. */
    for (db = -2000; db <= 20000; db++) {
        int32_t a = ibm_db2lin(db), b = db2lin(db);

        cases++;
        if (a != b) {
            if (bad < 5)
                printf("  db2lin(%ld): ibm %ld, ours %ld\n",
                       (long)db, (long)a, (long)b);
            bad++;
        }
    }

    /* Past 7182219 the original's db * 299 wraps negative, after which it
       shifts by a negative count and indexes off the front of its own table.
       Matching that would mean carrying whatever .rdata sits before fxl2, so
       the transcription is faithful up to this bound and no further. */
    rng_seed(0x5eed1234u);
    for (i = 0; i < 100000; i++) {
        int32_t x = (int32_t)(rng_next() % 7182220u);
        int32_t a = ibm_db2lin(x), b = db2lin(x);

        cases++;
        if (a != b) {
            if (bad < 5)
                printf("  db2lin(%ld): ibm %ld, ours %ld\n",
                       (long)x, (long)a, (long)b);
            bad++;
        }
    }

    for (db = 7182150; db <= 7182219; db++) {
        int32_t a = ibm_db2lin(db), b = db2lin(db);

        cases++;
        if (a != b) {
            if (bad < 5)
                printf("  db2lin(%ld) near wrap: ibm %ld, ours %ld\n",
                       (long)db, (long)a, (long)b);
            bad++;
        }
    }

    printf("  db2lin domain limited to db <= 7182219; above it the original "
           "wraps and reads outside fxl2\n");
    report("db2lin", cases, bad, 0);
}

static void test_verify_handle(void)
{
    int cases = 0, bad = 0;
    const char *strings[4];
    int i;

    strings[0] = ibm_KlattVersion;
    strings[1] = KlattVersionString;
    strings[2] = "not the version banner";
    strings[3] = "";

    for (i = 0; i < 4; i++) {
        void *handle = &strings[i];
        int a = ibm_verifyKlattHandle(handle);
        int b = verifyKlattHandle(handle);

        cases++;
        if (a != b) {
            printf("  verifyKlattHandle(%d): ibm %d, ours %d\n", i, a, b);
            bad++;
        }
    }

    if (strcmp(ibm_KlattVersion, KlattVersionString) != 0) {
        printf("  version banner differs from IBM's\n");
        bad++;
    }
    cases++;

    report("verifyKlattHandle", cases, bad, 0);
}

static void test_zero_filter(void)
{
    int cases = 0, bad = 0;
    int k, i;

    rng_seed(0xa5a5f00du);
    for (k = 0; k < 40000; k++) {
        filter_parms mine, theirs;
        int32_t bufa[MAXLEN], bufb[MAXLEN];
        zero_ABCs z;
        int32_t n = (int32_t)(rng_next() % (MAXLEN + 1));
        unsigned char *pm = (unsigned char *)&mine;
        unsigned char *pt = (unsigned char *)&theirs;

        for (i = 0; i < (int)sizeof(filter_parms); i++)
            pm[i] = pt[i] = (unsigned char)rng_next();

        /* A ramp longer than three samples indexes off the front of the
           coefficient tables in the original, so the engine never sets one. */
        mine.ramp = theirs.ramp = (int32_t)(rng_next() % 4u);
        mine.enabled = theirs.enabled = (int32_t)(rng_next() % 2u);

        z.a = (int16_t)rng_next();
        z.b = (int16_t)rng_next();
        z.c = (int16_t)rng_next();

        for (i = 0; i < MAXLEN; i++)
            bufa[i] = bufb[i] = (int32_t)rng_next();

        ibm_zero_filter(&theirs, &z, bufb, n);
        zero_filter(&mine, &z, bufa, n);

        cases++;
        if (memcmp(bufa, bufb, sizeof(bufa)) != 0 ||
            memcmp(&mine, &theirs, sizeof(mine)) != 0) {
            if (bad < 5)
                printf("  zero_filter n=%ld ramp=%ld enabled=%ld differs\n",
                       (long)n, (long)theirs.ramp, (long)theirs.enabled);
            bad++;
        }
    }

    report("zero_filter", cases, bad, 0);
}

static void test_pole_filter(void)
{
    int cases = 0, bad = 0;
    int k, i;

    rng_seed(0x31415926u);
    for (k = 0; k < 40000; k++) {
        filter_parms mine, theirs;
        int32_t rawa[MAXLEN + 2], rawb[MAXLEN + 2];
        int32_t *bufa = rawa + 2, *bufb = rawb + 2;
        int32_t n = (int32_t)(rng_next() % (MAXLEN + 1));
        unsigned char *pm = (unsigned char *)&mine;
        unsigned char *pt = (unsigned char *)&theirs;

        for (i = 0; i < (int)sizeof(filter_parms); i++)
            pm[i] = pt[i] = (unsigned char)rng_next();

        mine.ramp = theirs.ramp = (int32_t)(rng_next() % 4u);
        mine.enabled = theirs.enabled = (int32_t)(rng_next() % 2u);

        for (i = 0; i < MAXLEN + 2; i++)
            rawa[i] = rawb[i] = (int32_t)rng_next();

        ibm_pole_filter(&theirs, bufb, n);
        pole_filter(&mine, bufa, n);

        cases++;
        if (memcmp(rawa, rawb, sizeof(rawa)) != 0 ||
            memcmp(&mine, &theirs, sizeof(mine)) != 0) {
            if (bad < 5)
                printf("  pole_filter n=%ld ramp=%ld enabled=%ld differs\n",
                       (long)n, (long)theirs.ramp, (long)theirs.enabled);
            bad++;
        }
    }

    report("pole_filter", cases, bad, 0);
}

static void test_parallel0_filter(void)
{
    int cases = 0, bad = 0;
    int k, i;

    rng_seed(0x27182818u);
    for (k = 0; k < 40000; k++) {
        filter_parms mine, theirs;
        int32_t rawa[MAXLEN + 2], rawb[MAXLEN + 2];
        int32_t *bufa = rawa + 2, *bufb = rawb + 2;
        int32_t n = (int32_t)(rng_next() % (MAXLEN + 1));
        unsigned char *pm = (unsigned char *)&mine;
        unsigned char *pt = (unsigned char *)&theirs;

        for (i = 0; i < (int)sizeof(filter_parms); i++)
            pm[i] = pt[i] = (unsigned char)rng_next();

        for (i = 0; i < MAXLEN + 2; i++)
            rawa[i] = rawb[i] = (int32_t)rng_next();

        ibm_parallel0_filter(&theirs, bufb, n);
        parallel0_filter(&mine, bufa, n);

        cases++;
        if (memcmp(rawa, rawb, sizeof(rawa)) != 0 ||
            memcmp(&mine, &theirs, sizeof(mine)) != 0) {
            if (bad < 5)
                printf("  parallel0_filter n=%ld differs\n", (long)n);
            bad++;
        }
    }

    report("parallel0_filter", cases, bad, 0);
}

/* Three fields legitimately differ between two allocations: each side's
   banner pointer, and the two pointers the state holds into itself. Compare
   the banner as a string and blank the self-pointers; test_api checks those
   separately, as offsets. */
static void normalize(klatt_state *dst, const klatt_state *src)
{
    *dst = *src;
    dst->version = NULL;
    dst->ptr_a = NULL;
    dst->ptr_b = NULL;
    /* Each side points at its own copy of the lookup tables; which table got
       picked is checked by name in test_setconstparms. */
    dst->ex_table = NULL;
    dst->co_table = NULL;
}

static int state_differs(const klatt_state *a, const klatt_state *b)
{
    klatt_state ca, cb;

    if (strcmp(a->version, b->version) != 0)
        return 1;

    normalize(&ca, a);
    normalize(&cb, b);

    return memcmp(&ca, &cb, sizeof(ca)) != 0;
}

/* Offset of the first byte that really differs, ignoring the fields above. */
static long first_difference(const klatt_state *a, const klatt_state *b)
{
    klatt_state ca, cb;
    const unsigned char *pa, *pb;
    size_t i;

    normalize(&ca, a);
    normalize(&cb, b);
    pa = (const unsigned char *)&ca;
    pb = (const unsigned char *)&cb;

    for (i = 0; i < sizeof(ca); i++)
        if (pa[i] != pb[i])
            return (long)i;
    return -1;
}

static int self_pointers_differ(const klatt_state *a, const klatt_state *b)
{
    return ((const char *)a->ptr_a - (const char *)a)
        != ((const char *)b->ptr_a - (const char *)b)
        || ((const char *)a->ptr_b - (const char *)a)
        != ((const char *)b->ptr_b - (const char *)b);
}

static void fill_state(klatt_state *k, uint32_t (*next)(void))
{
    unsigned char *p = (unsigned char *)k;
    size_t i;

    for (i = 0; i < sizeof(klatt_state); i++)
        p[i] = (unsigned char)next();
}

static void test_noise(void)
{
    int cases = 0, bad = 0;
    int t, j;

    rng_seed(0x77777777u);
    for (t = 0; t < 20000; t++) {
        klatt_state *mine = malloc(sizeof(klatt_state));
        klatt_state *theirs = malloc(sizeof(klatt_state));
        uint32_t seed = rng_next();
        uint32_t ra, rb;
        int32_t span;

        fill_state(mine, rng_next);
        memcpy(theirs, mine, sizeof(klatt_state));

        /* Keep every index the smoothing walk produces inside noise_buf;
           the original does no bounds checking of its own. */
        mine->noise_count = theirs->noise_count = (int32_t)(rng_next() % 201u);
        mine->av = theirs->av = (int32_t)(rng_next() % 2u);
        span = (int32_t)(rng_next() % 17u);
        mine->smooth_span = theirs->smooth_span = span;
        for (j = 0; j < 199; j++)
            mine->spans[j] = theirs->spans[j] = (int32_t)(rng_next() % 8u);
        mine->spans[0] = theirs->spans[0] = (int32_t)(rng_next() % 201u);
        mine->version = theirs->version = KlattVersionString;

        rb = ibm_noise(theirs, seed);
        ra = noise(mine, seed);

        cases++;
        if (ra != rb || state_differs(mine, theirs)) {
            if (bad < 5)
                printf("  noise count=%ld span=%ld differs\n",
                       (long)theirs->noise_count, (long)span);
            bad++;
        }
        free(mine);
        free(theirs);
    }

    report("noise", cases, bad, 0);
}

static void test_compute(void)
{
    int cases = 0, bad = 0;
    int t;

    rng_seed(0x13571357u);
    for (t = 0; t < 20000; t++) {
        klatt_state *mine = malloc(sizeof(klatt_state));
        klatt_state *theirs = malloc(sizeof(klatt_state));
        int32_t rate;

        fill_state(mine, rng_next);
        memcpy(theirs, mine, sizeof(klatt_state));

        /* A zero rate divides by zero in the original just as it would here. */
        rate = (int32_t)(rng_next() % 48000u) + 1;
        mine->f0 = theirs->f0 = rate;
        mine->cp.sample_rate = theirs->cp.sample_rate = (int32_t)(rng_next() % 2000u);
        mine->v_start = theirs->v_start = (int32_t)(rng_next() % 4000u) - 2000;
        mine->voicing_size = theirs->voicing_size = (int32_t)(rng_next() % 2000u);
        mine->oq = theirs->oq = (int32_t)(rng_next() % 1000u);
        mine->version = theirs->version = KlattVersionString;

        ibm_compute_voicing_size(theirs);
        compute_voicing_size(mine);
        cases++;
        if (state_differs(mine, theirs)) {
            if (bad < 5)
                printf("  compute_voicing_size rate=%ld differs\n", (long)rate);
            bad++;
        }

        ibm_compute_v_start(theirs);
        compute_v_start(mine);
        cases++;
        if (state_differs(mine, theirs)) {
            if (bad < 5)
                printf("  compute_v_start rate=%ld differs\n", (long)rate);
            bad++;
        }

        free(mine);
        free(theirs);
    }

    report("compute_*", cases, bad, 0);
}

static int sample_sink(void *user, KlattSamplesStruct *s)
{
    (void)user;
    return (int)(s->count * 3 + 7);
}

static void test_output_speech(void)
{
    int cases = 0, bad = 0;
    int t;

    rng_seed(0x24682468u);
    for (t = 0; t < 20000; t++) {
        klatt_state *mine = malloc(sizeof(klatt_state));
        klatt_state *theirs = malloc(sizeof(klatt_state));
        int32_t n = (int32_t)(rng_next() % 201u);

        fill_state(mine, rng_next);
        memcpy(theirs, mine, sizeof(klatt_state));

        mine->output_samples = theirs->output_samples = (int32_t)(rng_next() % 2u);
        mine->cp.callback_mode = theirs->cp.callback_mode = (int32_t)(rng_next() % 3u);
        mine->volume = theirs->volume = (int32_t)(rng_next() % 201u);
        mine->cp.samples_fn = theirs->cp.samples_fn = sample_sink;
        mine->version = theirs->version = KlattVersionString;

        ibm_output_speech(theirs, n);
        output_speech(mine, n);

        cases++;
        if (state_differs(mine, theirs)) {
            if (bad < 5)
                printf("  output_speech n=%ld vol=%ld differs\n",
                       (long)n, (long)theirs->volume);
            bad++;
        }
        free(mine);
        free(theirs);
    }

    report("output_speech", cases, bad, 0);
}

static char seen_tag[128];
static char seen_msg[256];

static void error_sink(void *user, const char *tag, const char *msg)
{
    (void)user;
    strncpy(seen_tag, tag ? tag : "", sizeof(seen_tag) - 1);
    strncpy(seen_msg, msg ? msg : "", sizeof(seen_msg) - 1);
}

static void test_api(void)
{
    int cases = 0, bad = 0;
    int t;

    rng_seed(0x99887766u);
    for (t = 0; t < 4000; t++) {
        klatt_state *mine = ibm_klatt_new((void *)0x1234);
        klatt_state *theirs = klatt_new((void *)0x1234);
        char ibm_tag[128], ibm_msg[256];
        int32_t setting = (int32_t)rng_next();
        int ra, rb;

        /* klatt_new's own output first, before anything else touches it. */
        cases++;
        if (state_differs(mine, theirs)) {
            if (bad < 5)
                printf("  klatt_new differs\n");
            bad++;
        }

        mine->cp.error_fn = theirs->cp.error_fn = error_sink;
        mine->const_parms_set = theirs->const_parms_set =
            (int32_t)(rng_next() % 2u);
        mine->open_state = theirs->open_state =
            (int32_t)(rng_next() % 3u);

        seen_tag[0] = seen_msg[0] = '\0';
        rb = ibm_KlattOpen(mine);
        strcpy(ibm_tag, seen_tag);
        strcpy(ibm_msg, seen_msg);

        seen_tag[0] = seen_msg[0] = '\0';
        ra = KlattOpen(theirs);

        cases++;
        if (ra != rb || state_differs(mine, theirs) ||
            strcmp(ibm_tag, seen_tag) != 0 || strcmp(ibm_msg, seen_msg) != 0 ||
            (rb == 1 && self_pointers_differ(mine, theirs))) {
            if (bad < 5)
                printf("  KlattOpen differs: ibm %d \"%s\", ours %d \"%s\"\n",
                       rb, ibm_msg, ra, seen_msg);
            bad++;
        }

        ibm_klattSetVolumeMultiplier(mine, setting);
        klattSetVolumeMultiplier(theirs, setting);
        ibm_KlattSetOutputSamplesOption(mine, setting);
        KlattSetOutputSamplesOption(theirs, setting);
        mine->length = theirs->length = (int32_t)rng_next();
        mine->max = theirs->max = (int32_t)rng_next();

        cases++;
        if (ibm_KlattLength(mine) != KlattLength(theirs) ||
            ibm_KlattMax(mine) != KlattMax(theirs) ||
            state_differs(mine, theirs)) {
            if (bad < 5)
                printf("  setters or getters differ\n");
            bad++;
        }

        ibm_KlattClose(mine);
        KlattClose(theirs);
        cases++;
        if (state_differs(mine, theirs)) {
            if (bad < 5)
                printf("  KlattClose differs\n");
            bad++;
        }

        cases++;
        if (ibm_errorKlattIgnore() != errorKlattIgnore()) {
            printf("  errorKlattIgnore differs\n");
            bad++;
        }

        ibm_klatt_delete(mine);
        klatt_delete(theirs);
    }

    report("klatt api", cases, bad, 0);
}

extern const int16_t ibm_EX8[], ibm_CO8[], ibm_EX11[], ibm_CO11[];
extern const int16_t ibm_fxl2[], ibm_tilt8[], ibm_tilt11[];
extern const int16_t ibm_flutter_sine[], ibm_tl_table[];

extern void ibm_KlattSetConstParms(void *handle, KlattConstParms parms);

static void test_tables(void)
{
    struct { const char *name; const int16_t *ours; const int16_t *ibm; size_t n; }
    t[] = {
        {"fxl2",         klatt_fxl2,         ibm_fxl2,         sizeof(klatt_fxl2)},
        {"tl_table",     klatt_tl_table,     ibm_tl_table,     sizeof(klatt_tl_table)},
        {"tilt8",        klatt_tilt8,        ibm_tilt8,        sizeof(klatt_tilt8)},
        {"tilt11",       klatt_tilt11,       ibm_tilt11,       sizeof(klatt_tilt11)},
        {"flutter_sine", klatt_flutter_sine, ibm_flutter_sine, sizeof(klatt_flutter_sine)},
        {"EX8",          klatt_EX8,          ibm_EX8,          sizeof(klatt_EX8)},
        {"CO8",          klatt_CO8,          ibm_CO8,          sizeof(klatt_CO8)},
        {"EX11",         klatt_EX11,         ibm_EX11,         sizeof(klatt_EX11)},
        {"CO11",         klatt_CO11,         ibm_CO11,         sizeof(klatt_CO11)}
    };
    int cases = 0, bad = 0;
    int i;

    for (i = 0; i < (int)(sizeof(t) / sizeof(t[0])); i++) {
        cases++;
        if (memcmp(t[i].ours, t[i].ibm, t[i].n) != 0) {
            printf("  table %s differs from IBM's copy\n", t[i].name);
            bad++;
        }
    }

    report("tables", cases, bad, 0);
}

/* Which of the four tables a pointer refers to, so the two sides' choices can
   be compared even though they point at different copies. */
static int table_id(const void *p, int ibm)
{
    if (p == NULL)
        return 0;
    if (ibm)
        return p == ibm_EX8 ? 1 : p == ibm_CO8 ? 2
             : p == ibm_EX11 ? 3 : p == ibm_CO11 ? 4 : -1;
    return p == klatt_EX8 ? 1 : p == klatt_CO8 ? 2
         : p == klatt_EX11 ? 3 : p == klatt_CO11 ? 4 : -1;
}

static void test_setconstparms(void)
{
    int cases = 0, bad = 0;
    int t;
    static const int32_t rates[] = {8000, 11025, 22050, 0, 16000, -1};

    rng_seed(0xc0ffee11u);
    for (t = 0; t < 4000; t++) {
        klatt_state *mine = ibm_klatt_new((void *)0x1234);
        klatt_state *theirs = klatt_new((void *)0x1234);
        KlattConstParms parms;
        unsigned char *pp = (unsigned char *)&parms;
        size_t i;

        for (i = 0; i < sizeof(parms); i++)
            pp[i] = (unsigned char)rng_next();
        parms.sample_rate = rates[rng_next() % 6u];
        parms.error_fn = error_sink;
        parms.samples_fn = sample_sink;

        mine->open_state = theirs->open_state = (int32_t)(rng_next() % 3u);

        ibm_KlattSetConstParms(mine, parms);
        KlattSetConstParms(theirs, parms);

        cases++;
        if (state_differs(mine, theirs) ||
            table_id(mine->ex_table, 1) != table_id(theirs->ex_table, 0) ||
            table_id(mine->co_table, 1) != table_id(theirs->co_table, 0)) {
            if (bad < 5)
                printf("  KlattSetConstParms rate=%ld differs\n",
                       (long)parms.sample_rate);
            bad++;
        }

        ibm_klatt_delete(mine);
        klatt_delete(theirs);
    }

    report("SetConstParms", cases, bad, 0);
}

extern int ibm_KlattSynth(void *handle, const int32_t *parms);

/* A frame of zero duration makes the sample count zero, and the original then
   jumps straight past its per-sample loop to the frame tail. That isolates the
   setup phase so it can be checked on its own. */
static void test_synth_setup(void)
{
    int cases = 0, bad = 0;
    int t, i;
    static const int32_t rates[] = {8000, 11025};

    rng_seed(0x5ec7e70au);
    for (t = 0; t < 20000; t++) {
        klatt_state *mine = ibm_klatt_new((void *)0x1234);
        klatt_state *theirs = klatt_new((void *)0x1234);
        KlattConstParms parms;
        int32_t frame[63];
        unsigned char *pp = (unsigned char *)&parms;
        int ra, rb;
        size_t b;

        for (b = 0; b < sizeof(parms); b++)
            pp[b] = (unsigned char)rng_next();
        parms.sample_rate = rates[rng_next() % 2u];
        parms.n_formants = (int32_t)(rng_next() % 9u);
        parms.error_fn = error_sink;
        parms.samples_fn = sample_sink;

        /* These four end up summed into db2lin's argument, and db2lin is only
           faithful below 7182219; random 32-bit values sail past that. */
        parms.unknown_20 = (int32_t)(rng_next() % 100u);
        parms.unknown_24 = (int32_t)(rng_next() % 100u);
        parms.unknown_28 = (int32_t)(rng_next() % 100u);
        parms.unknown_2c = (int32_t)(rng_next() % 100u);

        ibm_KlattSetConstParms(mine, parms);
        KlattSetConstParms(theirs, parms);

        /* Frequencies and bandwidths land inside the table range once the
           clamps have run, and amplitudes stay inside db2lin's domain. */
        for (i = 0; i < 63; i++)
            frame[i] = (int32_t)(rng_next() % 6000u) - 500;
        frame[0] = 0;                                  /* zero duration */
        frame[43] = (int32_t)(rng_next() % 100u);      /* ab */
        for (i = 35; i <= 42; i++)
            frame[i] = (int32_t)(rng_next() % 100u);   /* a1f..a8f */

        rb = ibm_KlattSynth(mine, frame);
        ra = KlattSynth(theirs, frame);

        cases++;
        if (ra != rb || state_differs(mine, theirs)) {
            if (bad < 4) {
                long o = first_difference(mine, theirs);
                long base = (long)offsetof(klatt_state, filters);
                long end = base + 21 * (long)sizeof(filter_parms);

                printf("  setup differs nf=%ld rate=%ld at 0x%04lx",
                       (long)parms.n_formants, (long)parms.sample_rate, o);

                if (o >= base && o < end) {
                    long f = (o - base) / (long)sizeof(filter_parms);
                    long fld = (o - base) % (long)sizeof(filter_parms);
                    const filter_parms *x = &mine->filters[f];
                    const filter_parms *y = &theirs->filters[f];

                    printf(" = filter %ld field 0x%02lx: en %ld/%ld"
                           " sa %d/%d sb %d/%d sc %d/%d",
                           f, fld, (long)x->enabled, (long)y->enabled,
                           x->sa, y->sa, x->sb, y->sb, x->sc, y->sc);
                }
                printf("\n");
            }
            bad++;
        }

        ibm_klatt_delete(mine);
        klatt_delete(theirs);
    }

    report("synth setup", cases, bad, 0);
}

/* Every amplitude at zero leaves nothing excited and nothing ringing, so the
   block loop takes its silence path and the synthesis body never runs. That
   exercises the loop, the output call and the frame tail on their own. */
static void test_synth_silence(void)
{
    int cases = 0, bad = 0;
    int t, i;
    static const int32_t rates[] = {8000, 11025};

    rng_seed(0x511e0c1au);
    for (t = 0; t < 20000; t++) {
        klatt_state *mine = ibm_klatt_new((void *)0x1234);
        klatt_state *theirs = klatt_new((void *)0x1234);
        KlattConstParms parms;
        int32_t frame[63];
        unsigned char *pp = (unsigned char *)&parms;
        int ra, rb;
        size_t b;

        for (b = 0; b < sizeof(parms); b++)
            pp[b] = (unsigned char)rng_next();
        parms.sample_rate = rates[rng_next() % 2u];
        parms.n_formants = (int32_t)(rng_next() % 9u);
        parms.error_fn = error_sink;
        parms.samples_fn = sample_sink;
        parms.unknown_00 = (int32_t)(rng_next() % 20u) + 1;
        parms.unknown_20 = (int32_t)(rng_next() % 100u);
        parms.unknown_24 = (int32_t)(rng_next() % 100u);
        parms.unknown_28 = (int32_t)(rng_next() % 100u);
        parms.unknown_2c = (int32_t)(rng_next() % 100u);

        ibm_KlattSetConstParms(mine, parms);
        KlattSetConstParms(theirs, parms);

        for (i = 0; i < 63; i++)
            frame[i] = (int32_t)(rng_next() % 6000u) - 500;
        frame[0] = (int32_t)(rng_next() % 500u);   /* duration */
        frame[2] = 0;                              /* av */
        frame[7] = 0;                              /* ah */
        frame[8] = 0;                              /* af */
        frame[43] = (int32_t)(rng_next() % 100u);
        for (i = 35; i <= 42; i++)
            frame[i] = (int32_t)(rng_next() % 100u);

        mine->volume = theirs->volume = 100;
        mine->cp.callback_mode = theirs->cp.callback_mode =
            (int32_t)(rng_next() % 3u);

        rb = ibm_KlattSynth(mine, frame);
        ra = KlattSynth(theirs, frame);

        cases++;
        if (ra != rb || state_differs(mine, theirs)) {
            if (bad < 4)
                printf("  silence differs ui=%ld u00=%ld rate=%ld at 0x%04lx\n",
                       (long)frame[0], (long)parms.unknown_00,
                       (long)parms.sample_rate,
                       first_difference(mine, theirs));
            bad++;
        }

        ibm_klatt_delete(mine);
        klatt_delete(theirs);
    }

    report("synth silence", cases, bad, 0);
}

/* Voicing amplitude on but pitch off, with no aspiration or frication, takes
   the shortest route through the synthesis body: a silent source, the cascade
   still ringing over it, and the output scaling. */
static void test_synth_ring(void)
{
    int cases = 0, bad = 0;
    int t, i;
    static const int32_t rates[] = {8000, 11025};

    rng_seed(0x21b9e117u);
    for (t = 0; t < 20000; t++) {
        klatt_state *mine = ibm_klatt_new((void *)0x1234);
        klatt_state *theirs = klatt_new((void *)0x1234);
        KlattConstParms parms;
        int32_t frame[63];
        unsigned char *pp = (unsigned char *)&parms;
        int ra, rb;
        size_t b;

        for (b = 0; b < sizeof(parms); b++)
            pp[b] = (unsigned char)rng_next();
        parms.sample_rate = rates[rng_next() % 2u];
        parms.n_formants = (int32_t)(rng_next() % 9u);
        parms.error_fn = error_sink;
        parms.samples_fn = sample_sink;
        parms.unknown_00 = (int32_t)(rng_next() % 20u) + 1;
        parms.unknown_1c = 0;
        parms.unknown_20 = (int32_t)(rng_next() % 100u);
        parms.unknown_24 = (int32_t)(rng_next() % 100u);
        parms.unknown_28 = (int32_t)(rng_next() % 100u);
        parms.unknown_2c = (int32_t)(rng_next() % 100u);

        ibm_KlattSetConstParms(mine, parms);
        KlattSetConstParms(theirs, parms);

        /* KlattOpen is what points the working buffers at themselves; without
           it the synthesis body writes through a null pointer. */
        ibm_KlattOpen(mine);
        KlattOpen(theirs);

        for (i = 0; i < 63; i++)
            frame[i] = (int32_t)(rng_next() % 6000u) - 500;
        frame[0] = (int32_t)(rng_next() % 400u) + 1;   /* duration */
        frame[1] = 0;                                  /* f0 off */
        frame[2] = (int32_t)(rng_next() % 90u);        /* av */
        frame[7] = (int32_t)(rng_next() % 90u);        /* ah */
        frame[8] = (int32_t)(rng_next() % 90u);        /* af */
        if (frame[2] == 0 && frame[7] == 0 && frame[8] == 0)
            frame[8] = 1;                              /* stay off the silence path */
        frame[43] = (int32_t)(rng_next() % 100u);
        for (i = 35; i <= 42; i++)
            frame[i] = (int32_t)(rng_next() % 100u);

        mine->volume = theirs->volume = 100;
        mine->cp.callback_mode = theirs->cp.callback_mode =
            (int32_t)(rng_next() % 3u);

        rb = ibm_KlattSynth(mine, frame);
        ra = KlattSynth(theirs, frame);

        cases++;
        if (ra != rb || state_differs(mine, theirs)) {
            if (bad < 4)
                printf("  ring differs ui=%ld nf=%ld rate=%ld at 0x%04lx\n",
                       (long)frame[0], (long)parms.n_formants,
                       (long)parms.sample_rate,
                       first_difference(mine, theirs));
            bad++;
        }

        ibm_klatt_delete(mine);
        klatt_delete(theirs);
    }

    report("synth unvoiced", cases, bad, 0);
}

int main(void)
{
    printf("diff: comparing our transcription against IBM clsyn.obj\n");

    rng_seed(1);
    test_fxdivl();
    test_clr_vector();
    test_klatt_rand();
    test_fxmul_vector();
    test_fxmul1_vector();
    test_db2lin();
    test_verify_handle();
    test_zero_filter();
    test_pole_filter();
    test_parallel0_filter();
    test_noise();
    test_compute();
    test_output_speech();
    test_api();
    test_tables();
    test_setconstparms();
    test_synth_setup();
    test_synth_silence();
    test_synth_ring();

    printf("diff: %d cases, %d mismatches\n", total_cases, total_bad);
    return total_bad != 0;
}
