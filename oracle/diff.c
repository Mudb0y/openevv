/* Differential harness.
 *
 * Links IBM's original clsyn.obj beside our transcription and runs both over
 * the same inputs. Built as a 32-bit PE because the object is MSVC-mangled
 * x86 COFF; it runs under Wine. A pass here means bit-identical, not close.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "klatt_fx.h"

extern void     ibm_clr_vector(int32_t *v, int32_t n);
extern uint32_t ibm_klatt_rand(int16_t *out, int32_t n, uint32_t seed);
extern int16_t  ibm_fxdivl(int32_t num, int32_t den);
extern void     ibm_fxmul_vector(int32_t *src, int16_t coef, int32_t *acc, int32_t n);
extern void     ibm_fxmul1_vector(int16_t *src, int16_t coef, int32_t *acc, int32_t n);
extern int32_t  ibm_db2lin(int32_t db);
extern int      ibm_verifyKlattHandle(void *handle);
extern const char ibm_KlattVersion[];
extern void     ibm_zero_filter(filter_parms *fp, const zero_ABCs *z,
                                int32_t *buf, int32_t n);

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

    printf("diff: %d cases, %d mismatches\n", total_cases, total_bad);
    return total_bad != 0;
}
