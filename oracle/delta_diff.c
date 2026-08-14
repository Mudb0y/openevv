/* Differential harness for the Delta primitives.
 *
 * Same shape as the synthesizer's: IBM's implementation and ours in one
 * 32-bit binary, called on identical inputs, with the whole state compared
 * afterwards. A pass means bit-identical, not close.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#include "delta.h"

extern void ibm_lpta_loadp(delta_state *d, const delta_token *p);
extern void ibm_lpta_rpta_loadp(delta_state *d, const delta_token *lp,
                                const delta_token *rp);

static int total_cases;
static int total_bad;

static uint32_t rng_state;

static void rng_seed(uint32_t s) { rng_state = s; }

static uint32_t rng_next(void)
{
    rng_state = rng_state * 1103515245u + 12345u;
    return rng_state;
}

static void report(const char *name, int cases, int bad)
{
    total_cases += cases;
    total_bad += bad;
    printf("%-18s %6d cases, %d mismatches\n", name, cases, bad);
}

static void fill(void *p, size_t n)
{
    unsigned char *b = p;
    size_t i;

    for (i = 0; i < n; i++)
        b[i] = (unsigned char)rng_next();
}

static void test_lpta_loadp(void)
{
    int cases = 0, bad = 0;
    int t;

    rng_seed(0x17a10adeu);
    for (t = 0; t < 20000; t++) {
        delta_state *mine = malloc(sizeof(delta_state));
        delta_state *theirs = malloc(sizeof(delta_state));
        delta_token tok;

        fill(mine, sizeof(delta_state));
        memcpy(theirs, mine, sizeof(delta_state));
        fill(&tok, sizeof(tok));

        ibm_lpta_loadp(mine, &tok);
        lpta_loadp(theirs, &tok);

        cases++;
        if (memcmp(mine, theirs, sizeof(delta_state)) != 0) {
            if (bad < 3)
                printf("  lpta_loadp differs\n");
            bad++;
        }
        free(mine);
        free(theirs);
    }

    report("lpta_loadp", cases, bad);
}

static void test_lpta_rpta_loadp(void)
{
    int cases = 0, bad = 0;
    int t;

    rng_seed(0x2b7a10adu);
    for (t = 0; t < 20000; t++) {
        delta_state *mine = malloc(sizeof(delta_state));
        delta_state *theirs = malloc(sizeof(delta_state));
        delta_token lp, rp;

        fill(mine, sizeof(delta_state));
        memcpy(theirs, mine, sizeof(delta_state));
        fill(&lp, sizeof(lp));
        fill(&rp, sizeof(rp));

        ibm_lpta_rpta_loadp(mine, &lp, &rp);
        lpta_rpta_loadp(theirs, &lp, &rp);

        cases++;
        if (memcmp(mine, theirs, sizeof(delta_state)) != 0) {
            if (bad < 3)
                printf("  lpta_rpta_loadp differs\n");
            bad++;
        }
        free(mine);
        free(theirs);
    }

    report("lpta_rpta_loadp", cases, bad);
}

int main(void)
{
    printf("delta diff: comparing our primitives against IBM's\n");
    test_lpta_loadp();
    test_lpta_rpta_loadp();
    printf("delta diff: %d cases, %d mismatches\n", total_cases, total_bad);
    return total_bad != 0;
}
