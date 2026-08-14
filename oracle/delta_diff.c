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
#include <stddef.h>

#include "delta.h"

extern void ibm_lpta_loadp(delta_state *d, const delta_token *p);
extern void ibm_lpta_loadpn(delta_state *d, const delta_token *p);
extern void ibm_rpta_loadp(delta_state *d, const delta_token *p);
extern void ibm_rpta_loadpn(delta_state *d, const delta_token *p);
extern void ibm_lpta_rpta_loadp(delta_state *d, const delta_token *lp,
                                const delta_token *rp);
extern void ibm_bspush_ca(delta_state *d, int16_t tag);
extern void ibm_bspush_boa(delta_state *d);
extern void ibm_bspush_nboa(delta_state *d);
extern int  ibm_testeq(delta_state *d);
extern int  ibm_testneq(delta_state *d);

/* A state with its two side blocks attached, so a comparison covers
   everything a primitive can reach. */
typedef struct {
    delta_state state;
    delta_vars  vars;
    delta_stack stack;
} delta_world;

static void world_link(delta_world *w)
{
    w->state.vars = &w->vars;
    w->state.stack = &w->stack;
    /* Park the stack pointers in the middle of the record area so pushes land
       inside the block being compared. */
    w->stack.top = w->stack.pad_0500 + 0x80;
    w->stack.limit = w->stack.pad_0500 + 0x40;
    /* Distinct, or a test could not tell which size field a push used. */
    w->stack.ca_size = 8;
    w->stack.boa_size = 24;
}

/* The two block pointers differ between allocations, so compare around them. */
static int world_differs(delta_world *a, delta_world *b)
{
    delta_state sa = a->state, sb = b->state;
    ptrdiff_t ta, tb, la, lb;

    ta = a->stack.top - a->stack.pad_0500;
    tb = b->stack.top - b->stack.pad_0500;
    la = a->stack.limit - a->stack.pad_0500;
    lb = b->stack.limit - b->stack.pad_0500;
    if (ta != tb || la != lb)
        return 1;

    sa.vars = sb.vars = NULL;
    sa.stack = sb.stack = NULL;
    if (memcmp(&sa, &sb, sizeof(sa)) != 0)
        return 1;
    if (memcmp(&a->vars, &b->vars, sizeof(a->vars)) != 0)
        return 1;
    return memcmp(a->stack.pad_0500, b->stack.pad_0500,
                  sizeof(a->stack.pad_0500)) != 0;
}

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

#define PAIR(setup, callibm, callours, name)                                  \
static void test_##name(void)                                                 \
{                                                                             \
    int cases = 0, bad = 0;                                                   \
    int t;                                                                    \
    rng_seed(0x9e3779b9u ^ (uint32_t)__LINE__);                               \
    for (t = 0; t < 20000; t++) {                                             \
        delta_world *mine = malloc(sizeof(delta_world));                      \
        delta_world *theirs = malloc(sizeof(delta_world));                    \
        delta_token tok;                                                      \
        int16_t tag;                                                          \
        fill(mine, sizeof(delta_world));                                      \
        memcpy(theirs, mine, sizeof(delta_world));                            \
        fill(&tok, sizeof(tok));                                              \
        tag = (int16_t)rng_next();                                            \
        world_link(mine); world_link(theirs);                                 \
        setup;                                                                \
        callibm; callours;                                                    \
        cases++;                                                              \
        if (world_differs(mine, theirs)) {                                    \
            if (bad < 3) printf("  %s differs\n", #name);                     \
            bad++;                                                            \
        }                                                                     \
        free(mine); free(theirs);                                             \
    }                                                                         \
    report(#name, cases, bad);                                                \
}

PAIR((void)0, ibm_lpta_loadpn(&mine->state, &tok), lpta_loadpn(&theirs->state, &tok), lpta_loadpn)
PAIR((void)0, ibm_rpta_loadp(&mine->state, &tok), rpta_loadp(&theirs->state, &tok), rpta_loadp)
PAIR((void)0, ibm_rpta_loadpn(&mine->state, &tok), rpta_loadpn(&theirs->state, &tok), rpta_loadpn)
PAIR((void)0, ibm_bspush_ca(&mine->state, tag), bspush_ca(&theirs->state, tag), bspush_ca)
PAIR((void)0, ibm_bspush_boa(&mine->state), bspush_boa(&theirs->state), bspush_boa)
PAIR((void)0, ibm_bspush_nboa(&mine->state), bspush_nboa(&theirs->state), bspush_nboa)

static void test_tests(void)
{
    int cases = 0, bad = 0;
    int t;

    rng_seed(0x7e57e57eu);
    for (t = 0; t < 20000; t++) {
        delta_world *w = malloc(sizeof(delta_world));

        fill(w, sizeof(delta_world));
        world_link(w);

        cases += 2;
        if (ibm_testeq(&w->state) != testeq(&w->state)) {
            if (bad < 3) printf("  testeq differs\n");
            bad++;
        }
        if (ibm_testneq(&w->state) != testneq(&w->state)) {
            if (bad < 3) printf("  testneq differs\n");
            bad++;
        }
        free(w);
    }

    report("testeq/testneq", cases, bad);
}

int main(void)
{
    printf("delta diff: comparing our primitives against IBM's\n");
    test_lpta_loadp();
    test_lpta_rpta_loadp();
    test_lpta_loadpn();
    test_rpta_loadp();
    test_rpta_loadpn();
    test_bspush_ca();
    test_bspush_boa();
    test_bspush_nboa();
    test_tests();
    printf("delta diff: %d cases, %d mismatches\n", total_cases, total_bad);
    return total_bad != 0;
}
