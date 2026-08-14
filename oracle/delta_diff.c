/* Differential harness for the Delta primitives.
 *
 * Same shape as the synthesizer's: IBM's implementation and ours in one
 * 32-bit binary, called on identical inputs, with everything a primitive can
 * reach compared afterwards. A pass means bit-identical, not close.
 */

#include <stdint.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "delta.h"

extern void ibm_lpta_loadp(delta_state *, const delta_token *);
extern void ibm_lpta_loadpn(delta_state *, const delta_token *);
extern void ibm_rpta_loadp(delta_state *, const delta_token *);
extern void ibm_rpta_loadpn(delta_state *, const delta_token *);
extern void ibm_lpta_rpta_loadp(delta_state *, const delta_token *,
                                const delta_token *);
extern void ibm_bspush_ca(delta_state *, int16_t);
extern void ibm_bspush_boa(delta_state *);
extern void ibm_bspush_nboa(delta_state *);
extern void ibm_bspush_ca_scan(delta_state *, int16_t);
extern int  ibm_testeq(delta_state *);
extern int  ibm_testneq(delta_state *);
extern void ibm_fence(delta_state *, int8_t, const uint8_t *);
extern void *ibm_TFLDS(void *);
extern int32_t ibm_getDeltaStackVBot(delta_state *);
extern void ibm_setDeltaStackVBot(delta_state *, int32_t);
extern void *ibm_popDeltaStackTop(delta_state *);
extern int  ibm_FENCED(delta_state *, const int32_t *, int8_t);

#define RECORDS   0x200   /* room for the stack to push into */
#define FENCE_MAP 0x100   /* the reverse fence table is indexed by a byte */

/* A state with everything it can reach hanging off it, so one comparison
   covers the lot. */
typedef struct {
    delta_state state;
    delta_vars  vars;
    delta_stack stack;
    uint8_t     records[RECORDS];
    uint8_t     chars[FENCE_MAP];
    uint8_t     map[FENCE_MAP];
} delta_world;

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
    printf("%-20s %6d cases, %d mismatches\n", name, cases, bad);
}

static void fill(void *p, size_t n)
{
    unsigned char *b = p;
    size_t i;

    for (i = 0; i < n; i++)
        b[i] = (unsigned char)rng_next();
}

/* Point the state at its own blocks and give the stack somewhere real to
   push into. The record sizes are deliberately all different, or a test
   could not tell which one a primitive had used. */
static void world_link(delta_world *w)
{
    w->state.vars = &w->vars;
    w->state.stack = &w->stack;
    w->state.fence_chars = w->chars;
    w->state.fence_index = w->map;
    w->state.fence_fill = (uint8_t)(rng_next() % FENCE_MAP);

    w->stack.top = w->records + RECORDS / 2;
    w->stack.limit = w->records + RECORDS / 2 - 0x40;
    w->stack.size_a8 = 4;
    w->stack.size_ac = 12;
    w->stack.size_b0 = 16;
    w->stack.ca_size = 8;
    w->stack.size_b8 = 20;
    w->stack.boa_size = 24;
}

/* The three block pointers differ between allocations, so compare around them
   and check the stack pointers as offsets instead. */
static int world_differs(delta_world *a, delta_world *b)
{
    delta_state sa = a->state, sb = b->state;

    if (a->stack.top - a->records != b->stack.top - b->records)
        return 1;
    if (a->stack.limit - a->records != b->stack.limit - b->records)
        return 1;

    sa.vars = sb.vars = NULL;
    sa.stack = sb.stack = NULL;
    sa.fence_chars = sb.fence_chars = NULL;
    sa.fence_index = sb.fence_index = NULL;

    if (memcmp(&sa, &sb, sizeof(sa)) != 0)
        return 1;
    if (memcmp(&a->vars, &b->vars, sizeof(a->vars)) != 0)
        return 1;
    if (memcmp(a->records, b->records, RECORDS) != 0)
        return 1;
    if (memcmp(a->chars, b->chars, FENCE_MAP) != 0)
        return 1;
    if (memcmp(a->map, b->map, FENCE_MAP) != 0)
        return 1;

    /* Everything in the stack block except the two pointers, already checked. */
    return memcmp((char *)&a->stack + 0x500, (char *)&b->stack + 0x500,
                  sizeof(delta_stack) - 0x500) != 0
        || memcmp(&a->stack, &b->stack, 0x4f8) != 0;
}

#define BEGIN(name)                                                  \
    static void test_##name(void)                                    \
    {                                                                \
        int cases = 0, bad = 0, t;                                   \
        rng_seed(0x9e3779b9u ^ (uint32_t)__LINE__);                  \
        for (t = 0; t < 20000; t++) {                                \
            delta_world *m = malloc(sizeof(delta_world));            \
            delta_world *o = malloc(sizeof(delta_world));            \
            fill(m, sizeof(delta_world));                            \
            memcpy(o, m, sizeof(delta_world));                       \
            rng_seed(0x5bd1e995u ^ (uint32_t)t);                     \
            world_link(m);                                           \
            rng_seed(0x5bd1e995u ^ (uint32_t)t);                     \
            world_link(o);

#define END(name)                                                    \
            cases++;                                                 \
            if (world_differs(m, o)) {                               \
                if (bad < 3) printf("  " #name " differs\n");        \
                bad++;                                               \
            }                                                        \
            free(m); free(o);                                        \
        }                                                            \
        report(#name, cases, bad);                                   \
    }

BEGIN(lpta_loadp)
    delta_token tok; fill(&tok, sizeof(tok));
    ibm_lpta_loadp(&m->state, &tok); lpta_loadp(&o->state, &tok);
END(lpta_loadp)

BEGIN(lpta_loadpn)
    delta_token tok; fill(&tok, sizeof(tok));
    ibm_lpta_loadpn(&m->state, &tok); lpta_loadpn(&o->state, &tok);
END(lpta_loadpn)

BEGIN(rpta_loadp)
    delta_token tok; fill(&tok, sizeof(tok));
    ibm_rpta_loadp(&m->state, &tok); rpta_loadp(&o->state, &tok);
END(rpta_loadp)

BEGIN(rpta_loadpn)
    delta_token tok; fill(&tok, sizeof(tok));
    ibm_rpta_loadpn(&m->state, &tok); rpta_loadpn(&o->state, &tok);
END(rpta_loadpn)

BEGIN(lpta_rpta_loadp)
    delta_token lp, rp; fill(&lp, sizeof(lp)); fill(&rp, sizeof(rp));
    ibm_lpta_rpta_loadp(&m->state, &lp, &rp);
    lpta_rpta_loadp(&o->state, &lp, &rp);
END(lpta_rpta_loadp)

BEGIN(bspush_ca)
    int16_t tag = (int16_t)rng_next();
    ibm_bspush_ca(&m->state, tag); bspush_ca(&o->state, tag);
END(bspush_ca)

BEGIN(bspush_boa)
    ibm_bspush_boa(&m->state); bspush_boa(&o->state);
END(bspush_boa)

BEGIN(bspush_nboa)
    ibm_bspush_nboa(&m->state); bspush_nboa(&o->state);
END(bspush_nboa)

BEGIN(bspush_ca_scan)
    int16_t tag = (int16_t)rng_next();
    ibm_bspush_ca_scan(&m->state, tag); bspush_ca_scan(&o->state, tag);
END(bspush_ca_scan)

BEGIN(fence)
    /* n is the count of fenced characters and indexes the forward table, so
       it stays inside the buffer the state points at. */
    uint8_t chars[FENCE_MAP];
    int8_t n = (int8_t)(rng_next() % 0x60);
    fill(chars, sizeof(chars));
    ibm_fence(&m->state, n, chars); fence(&o->state, n, chars);
END(fence)

BEGIN(vbot)
    int32_t v = (int32_t)rng_next();
    ibm_setDeltaStackVBot(&m->state, v); setDeltaStackVBot(&o->state, v);
    if (ibm_getDeltaStackVBot(&m->state) != getDeltaStackVBot(&o->state))
        bad++;
END(vbot)

BEGIN(popDeltaStackTop)
    /* A record kind the original does not recognise leaves the amount it
       moves by uninitialised, so callers never make one and nor does this. */
    ptrdiff_t off;
    m->records[RECORDS / 2] = o->records[RECORDS / 2] = (uint8_t)(rng_next() % 8u);
    off = (ptrdiff_t)((char *)ibm_popDeltaStackTop(&m->state) - (char *)m->records);
    if (off != (ptrdiff_t)((char *)popDeltaStackTop(&o->state) - (char *)o->records))
        bad++;
END(popDeltaStackTop)

static void test_scalars(void)
{
    int cases = 0, bad = 0, t;

    rng_seed(0x7e57e57eu);
    for (t = 0; t < 20000; t++) {
        delta_world *w = malloc(sizeof(delta_world));
        int32_t table[0x200];
        int8_t idx;
        char buf[16];

        fill(w, sizeof(delta_world));
        world_link(w);
        fill(table, sizeof(table));

        cases += 4;
        if (ibm_testeq(&w->state) != testeq(&w->state)) {
            if (bad < 3) printf("  testeq differs\n");
            bad++;
        }
        if (ibm_testneq(&w->state) != testneq(&w->state)) {
            if (bad < 3) printf("  testneq differs\n");
            bad++;
        }
        if (ibm_TFLDS(buf) != TFLDS(buf)) {
            if (bad < 3) printf("  TFLDS differs\n");
            bad++;
        }

        /* Keep the fence lookup inside the table it is handed. */
        w->vars.fence_base = (int32_t)(rng_next() % 0x100u);
        idx = (int8_t)(rng_next() % 0x40u);
        if (ibm_FENCED(&w->state, table, idx) != FENCED(&w->state, table, idx)) {
            if (bad < 3) printf("  FENCED differs\n");
            bad++;
        }

        free(w);
    }

    report("scalars", cases, bad);
}

int main(void)
{
    printf("delta diff: comparing our primitives against IBM's\n");
    test_lpta_loadp();
    test_lpta_loadpn();
    test_rpta_loadp();
    test_rpta_loadpn();
    test_lpta_rpta_loadp();
    test_bspush_ca();
    test_bspush_boa();
    test_bspush_nboa();
    test_bspush_ca_scan();
    test_fence();
    test_vbot();
    test_popDeltaStackTop();
    test_scalars();
    printf("delta diff: %d cases, %d mismatches\n", total_cases, total_bad);
    return total_bad != 0;
}
