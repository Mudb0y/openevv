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
extern void *ibm_getDeltaStackVBot(delta_state *);
extern void ibm_setDeltaStackVBot(delta_state *, void *);
extern void *ibm_popDeltaStackTop(delta_state *);
extern int  ibm_FENCED(delta_state *, const int32_t *, int8_t);
extern int32_t ibm_absoluteSyncNumPtr(int32_t);
extern void ibm_freeDeltaStackTo(delta_state *, uint8_t *);
extern void ibm_clearDeltaStackBack(delta_state *);
extern void ibm_starttest(delta_state *, int16_t);
extern void ibm_vcompare(delta_state *, const delta_operand *,
                         const delta_operand *);
extern int16_t ibm_STMTYP(int8_t);
extern int  ibm_ONESTM(const delta_node *);
extern int  ibm_ALLNSQ(const delta_node *);
extern int  ibm_NONSEQ(const delta_node *);
extern void ibm_SETONESTM(delta_node *);
extern void ibm_SETALLNSQ(delta_node *);
extern void ibm_SETNONSEQ(delta_node *);
extern void ibm_CLRONESTM(delta_node *);
extern void ibm_CLRALLNSQ(delta_node *);
extern void ibm_bsclear(delta_state *);
extern void *ibm_bspop_boa(delta_state *);
extern void ibm_starttest_e(delta_state *, int16_t);
extern void ibm_starttest_l(delta_state *, int16_t);
extern void ibm_SETFENCE(delta_state *, int32_t *, int8_t);
extern void ibm_UNSETFENCE(delta_state *, int32_t *, int8_t);
extern void ibm_addfence(delta_state *, int8_t);
extern void ibm_remfence(delta_state *, int8_t);
extern int32_t ibm_deltaErrorThrown(delta_state *);
extern int  ibm_emptyDeltaStack(delta_state *);
extern void *ibm_popDeltaStackFrame(delta_state *, uint8_t *);
extern void ibm_vnspush(delta_state *, const delta_operand *);
extern void ibm_vadd(delta_state *, const delta_operand *, const delta_operand *);
extern int32_t ibm_VLSYNC(const delta_node *, int8_t);
extern int32_t ibm_VRSYNC(delta_state *, const int32_t *, int8_t);
extern void ibm_reset_field(delta_field *);
extern int  ibm_push_ptr(delta_state *, int32_t);
extern int  ibm_ret_ptr_active_record(delta_state *);
extern void ibm_throwDeltaErrorNow(delta_state *);
extern void ibm_vnspop(delta_state *, delta_operand *);

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
    uint8_t     blockhdr[0x20];   /* the allocation header, read at +0x10 */
    uint8_t     names[0x200];     /* the name stack */
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

    w->stack.names = w->names;
    w->stack.names_depth = (int8_t)(rng_next() % 0x10u);
    w->stack.top = w->records + RECORDS / 2;
    w->stack.limit = w->records + RECORDS / 2 - 0x40;
    w->stack.block = w->blockhdr;
    *(uint8_t **)(w->blockhdr + 0x10) = w->records + RECORDS;
    w->stack.base = w->records + RECORDS;
    w->stack.vbot = w->records + 0x20;
    w->vars.back = w->records + 0x30;
    w->stack.size_a8 = 4;
    w->stack.size_ac = 12;
    w->stack.size_b0 = 16;
    w->stack.ca_size = 8;
    w->stack.size_b8 = 20;
    w->stack.boa_size = 24;
}

/* Every pointer in the state and its blocks points into the world it belongs
   to, so two worlds can never hold equal pointer values. Rewrite each one as
   an offset from the world's base before comparing, and anything that is not
   a pointer is then compared byte for byte. */
static void normalise(delta_world *w, delta_state *st, delta_vars *va,
                      delta_stack *sk)
{
    char *base = (char *)w;

    *st = w->state;
    *va = w->vars;
    *sk = w->stack;

#define REBASE(p) ((p) = (void *)((char *)(p) - base))
    REBASE(st->vars);
    REBASE(st->stack);
    REBASE(st->fence_chars);
    REBASE(st->fence_index);
    REBASE(va->back);
    REBASE(sk->top);
    REBASE(sk->limit);
    REBASE(sk->block);
    REBASE(sk->vbot);
    REBASE(sk->base);
    REBASE(sk->names);
#undef REBASE
}

static int world_differs(delta_world *a, delta_world *b)
{
    delta_state sa, sb;
    delta_vars va, vb;
    delta_stack ka, kb;

    normalise(a, &sa, &va, &ka);
    normalise(b, &sb, &vb, &kb);

    if (memcmp(&sa, &sb, sizeof(sa)) != 0)
        return 1;
    if (memcmp(&va, &vb, sizeof(va)) != 0)
        return 1;
    if (memcmp(&ka, &kb, sizeof(ka)) != 0)
        return 1;
    if (memcmp(a->records, b->records, RECORDS) != 0)
        return 1;
    if (memcmp(a->chars, b->chars, FENCE_MAP) != 0)
        return 1;
    if (memcmp(a->map, b->map, FENCE_MAP) != 0)
        return 1;
    if (*(char **)(a->blockhdr + 0x10) - (char *)a
        != *(char **)(b->blockhdr + 0x10) - (char *)b)
        return 1;
    if (memcmp(a->blockhdr, b->blockhdr, 0x10) != 0)
        return 1;
    if (memcmp(a->blockhdr + 0x14, b->blockhdr + 0x14,
               sizeof(a->blockhdr) - 0x14) != 0)
        return 1;
    return memcmp(a->names, b->names, sizeof(a->names)) != 0;
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
    void *vm = m->records + 0x10, *vo = o->records + 0x10;
    ibm_setDeltaStackVBot(&m->state, vm); setDeltaStackVBot(&o->state, vo);
    if ((char *)ibm_getDeltaStackVBot(&m->state) - (char *)m
        != (char *)getDeltaStackVBot(&o->state) - (char *)o)
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

BEGIN(freeDeltaStackTo)
    uint8_t *to = m->records + (rng_next() % RECORDS);
    ibm_freeDeltaStackTo(&m->state, m->records + (to - m->records));
    freeDeltaStackTo(&o->state, o->records + (to - m->records));
END(freeDeltaStackTo)

BEGIN(clearDeltaStackBack)
    /* Whether the mark at the bottom is a kind eight record decides which
       way this goes, so exercise both. */
    m->records[0x20] = o->records[0x20] = (uint8_t)(rng_next() % 2u ? 8 : 3);
    ibm_clearDeltaStackBack(&m->state); clearDeltaStackBack(&o->state);
END(clearDeltaStackBack)

BEGIN(starttest)
    int16_t tag = (int16_t)rng_next();
    m->records[0x20] = o->records[0x20] = (uint8_t)(rng_next() % 2u ? 8 : 3);
    ibm_starttest(&m->state, tag); starttest(&o->state, tag);
END(starttest)

static void test_syncnum(void)
{
    int cases = 0, bad = 0, t;

    rng_seed(0x5ec0ffeeu);
    for (t = 0; t < 20000; t++) {
        int32_t v = (t % 16 == 0) ? 0 : (int32_t)rng_next();

        cases++;
        if (ibm_absoluteSyncNumPtr(v) != absoluteSyncNumPtr(v)) {
            if (bad < 3) printf("  absoluteSyncNumPtr differs at %ld\n", (long)v);
            bad++;
        }
    }

    report("absoluteSyncNumPtr", cases, bad);
}

BEGIN(vcompare)
    /* Both operands point at eight bytes inside the world so the comparison
       has something real to read, and the type codes cover every arm
       including the pair that dispatch on the right operand as well. */
    delta_operand a, b;
    static const int16_t kinds[] = {-1, -2, -3, -4, -6, 0, 1, 2, 3, 4};

    a.ptr = m->records + 0x80;
    b.ptr = m->records + 0x90;
    a.kind = kinds[rng_next() % 10u];
    b.kind = kinds[rng_next() % 10u];
    a.pad_06 = b.pad_06 = 0;
    ibm_vcompare(&m->state, &a, &b);
    a.ptr = o->records + 0x80;
    b.ptr = o->records + 0x90;
    vcompare(&o->state, &a, &b);
END(vcompare)

BEGIN(bsclear)
    m->records[0x20] = o->records[0x20] = (uint8_t)(rng_next() % 2u ? 8 : 3);
    ibm_bsclear(&m->state); bsclear(&o->state);
END(bsclear)

BEGIN(bspop_boa)
    ptrdiff_t a, b;
    m->records[RECORDS / 2] = o->records[RECORDS / 2] = (uint8_t)(rng_next() % 8u);
    a = (char *)ibm_bspop_boa(&m->state) - (char *)m;
    b = (char *)bspop_boa(&o->state) - (char *)o;
    if (a != b) bad++;
END(bspop_boa)

static void test_nodes(void)
{
    int cases = 0, bad = 0, t;

    rng_seed(0x0de50eedu);
    for (t = 0; t < 20000; t++) {
        delta_node a, b;
        /* The English table has about ten entries; past that is off the end. */
        int8_t kind = (int8_t)(rng_next() % 10u);

        fill(&a, sizeof(a));
        b = a;

        cases += 10;
        if (ibm_STMTYP(kind) != STMTYP(kind)) { if (bad < 3) printf("  STMTYP differs\n"); bad++; }
        if (ibm_ONESTM(&a) != ONESTM(&a)) { if (bad < 3) printf("  ONESTM differs\n"); bad++; }
        if (ibm_ALLNSQ(&a) != ALLNSQ(&a)) { if (bad < 3) printf("  ALLNSQ differs\n"); bad++; }
        if (ibm_NONSEQ(&a) != NONSEQ(&a)) { if (bad < 3) printf("  NONSEQ differs\n"); bad++; }

        ibm_SETONESTM(&a); SETONESTM(&b);
        if (memcmp(&a, &b, sizeof(a))) { if (bad < 3) printf("  SETONESTM differs\n"); bad++; }
        ibm_SETALLNSQ(&a); SETALLNSQ(&b);
        if (memcmp(&a, &b, sizeof(a))) { if (bad < 3) printf("  SETALLNSQ differs\n"); bad++; }
        ibm_SETNONSEQ(&a); SETNONSEQ(&b);
        if (memcmp(&a, &b, sizeof(a))) { if (bad < 3) printf("  SETNONSEQ differs\n"); bad++; }
        ibm_CLRONESTM(&a); CLRONESTM(&b);
        if (memcmp(&a, &b, sizeof(a))) { if (bad < 3) printf("  CLRONESTM differs\n"); bad++; }
        ibm_CLRALLNSQ(&a); CLRALLNSQ(&b);
        if (memcmp(&a, &b, sizeof(a))) { if (bad < 3) printf("  CLRALLNSQ differs\n"); bad++; }
        if (memcmp(&a, &b, sizeof(a))) { bad++; }
    }

    report("spine accessors", cases, bad);
}

BEGIN(starttest_e)
    int16_t tag = (int16_t)rng_next();
    m->records[0x20] = o->records[0x20] = (uint8_t)(rng_next() % 2u ? 8 : 3);
    ibm_starttest_e(&m->state, tag); starttest_e(&o->state, tag);
END(starttest_e)

BEGIN(starttest_l)
    int16_t tag = (int16_t)rng_next();
    m->records[0x20] = o->records[0x20] = (uint8_t)(rng_next() % 2u ? 8 : 3);
    ibm_starttest_l(&m->state, tag); starttest_l(&o->state, tag);
END(starttest_l)

BEGIN(fences)
    /* The fence bit lives in a table the left register points at, so aim it
       at the record area and keep the index inside it. */
    int8_t idx = (int8_t)(rng_next() % 0x20u);
    m->vars.fence_base = o->vars.fence_base = (int32_t)(rng_next() % 0x10u);
    m->state.lpta.value = (int32_t)(intptr_t)m->records;
    o->state.lpta.value = (int32_t)(intptr_t)o->records;
    if (rng_next() % 2u) {
        ibm_addfence(&m->state, idx); addfence(&o->state, idx);
    } else {
        ibm_remfence(&m->state, idx); remfence(&o->state, idx);
    }
    m->state.lpta.value = o->state.lpta.value = 0;
END(fences)

BEGIN(vnspush)
    delta_operand v;
    static const int16_t kinds[] = {-1, -2, -3, -4, -6, 0};
    v.ptr = m->records + 0x80;
    v.kind = kinds[rng_next() % 6u];
    v.pad_06 = 0;
    ibm_vnspush(&m->state, &v);
    v.ptr = o->records + 0x80;
    vnspush(&o->state, &v);
END(vnspush)

BEGIN(vadd)
    delta_operand a, b;
    static const int16_t kinds[] = {-1, -2, -3, -4, -6, 0};
    a.ptr = m->records + 0x80; b.ptr = m->records + 0x90;
    a.kind = kinds[rng_next() % 6u]; b.kind = kinds[rng_next() % 6u];
    a.pad_06 = b.pad_06 = 0;
    ibm_vadd(&m->state, &a, &b);
    a.ptr = o->records + 0x80; b.ptr = o->records + 0x90;
    vadd(&o->state, &a, &b);
END(vadd)

BEGIN(popDeltaStackFrame)
    ptrdiff_t x, y;
    uint8_t *to = m->records + (rng_next() % RECORDS);
    x = (char *)ibm_popDeltaStackFrame(&m->state, to) - (char *)m;
    y = (char *)popDeltaStackFrame(&o->state, o->records + (to - m->records))
        - (char *)o;
    if (x != y) bad++;
END(popDeltaStackFrame)

static void test_queries(void)
{
    int cases = 0, bad = 0, t;

    rng_seed(0x9111e5edu);
    for (t = 0; t < 20000; t++) {
        delta_world *w = malloc(sizeof(delta_world));
        delta_node n;
        int8_t i;

        fill(w, sizeof(delta_world));
        world_link(w);
        /* size_a8 indexes off the unwind mark, so keep it inside the block. */
        w->stack.size_a8 = (int32_t)(rng_next() % 0x40u);

        cases += 3;
        if (ibm_deltaErrorThrown(&w->state) != deltaErrorThrown(&w->state)) {
            if (bad < 3) printf("  deltaErrorThrown differs\n");
            bad++;
        }
        if (ibm_emptyDeltaStack(&w->state) != emptyDeltaStack(&w->state)) {
            if (bad < 3) printf("  emptyDeltaStack differs\n");
            bad++;
        }

        /* A sync link is followed one step, so it must point somewhere real. */
        fill(&n, sizeof(n));
        i = (int8_t)(rng_next() % 8u);
        n.syncs[i] = (rng_next() % 2u)
            ? 0 : (int32_t)(intptr_t)(w->records + 0x40);
        if (ibm_VLSYNC(&n, i) != VLSYNC(&n, i)) {
            if (bad < 3) printf("  VLSYNC differs\n");
            bad++;
        }

        free(w);
    }

    report("state queries", cases, bad);
}

BEGIN(ptr_stack)
    /* Keep the count inside the 999 slots so both sides do real work. */
    int32_t p = (int32_t)rng_next();
    m->vars.ptr_count = o->vars.ptr_count = (int32_t)(rng_next() % 1002u);
    m->vars.active_record = o->vars.active_record =
        (int32_t)(rng_next() % 999u);
    if (rng_next() % 2u) {
        if (ibm_push_ptr(&m->state, p) != push_ptr(&o->state, p)) bad++;
    } else {
        if (ibm_ret_ptr_active_record(&m->state)
            != ret_ptr_active_record(&o->state)) bad++;
    }
    ibm_throwDeltaErrorNow(&m->state); throwDeltaErrorNow(&o->state);
END(ptr_stack)

BEGIN(vnspop)
    delta_operand a, b;
    fill(&a, sizeof(a)); b = a;
    ibm_vnspop(&m->state, &a); vnspop(&o->state, &b);
    /* A type outside the four sized ones leaves the pointer alone, so it is
       still the same garbage on both sides and compares directly. */
    if (a.kind != b.kind || a.pad_06 != b.pad_06) {
        bad++;
    } else if (a.kind >= -4 && a.kind <= -1) {
        if ((char *)a.ptr - (char *)m != (char *)b.ptr - (char *)o) bad++;
    } else if (a.ptr != b.ptr) {
        bad++;
    }
END(vnspop)

static void test_fields(void)
{
    int cases = 0, bad = 0, t;

    rng_seed(0xf1e1d5edu);
    for (t = 0; t < 20000; t++) {
        delta_world *w = malloc(sizeof(delta_world));
        delta_field fa, fb;
        int8_t i;

        fill(w, sizeof(delta_world));
        world_link(w);
        fill(&fa, sizeof(fa));
        fb = fa;

        cases += 2;
        ibm_reset_field(&fa); reset_field(&fb);
        if (memcmp(&fa, &fb, sizeof(fa))) {
            if (bad < 3) printf("  reset_field differs\n");
            bad++;
        }

        /* The right sync walk indexes off the fence base, so keep the pair
           inside the record area and let half of them be null. */
        w->vars.fence_base = 0;
        i = (int8_t)(rng_next() % 8u);
        ((int32_t *)w->records)[i] = (rng_next() % 2u)
            ? 0 : (int32_t)(intptr_t)(w->records + 0x40);
        if (ibm_VRSYNC(&w->state, (int32_t *)w->records, i)
            != VRSYNC(&w->state, (int32_t *)w->records, i)) {
            if (bad < 3) printf("  VRSYNC differs\n");
            bad++;
        }

        free(w);
    }

    report("fields and syncs", cases, bad);
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
    test_syncnum();
    test_freeDeltaStackTo();
    test_clearDeltaStackBack();
    test_starttest();
    test_vcompare();
    test_bsclear();
    test_bspop_boa();
    test_nodes();
    test_starttest_e();
    test_starttest_l();
    test_fences();
    test_vnspush();
    test_vadd();
    test_popDeltaStackFrame();
    test_queries();
    test_ptr_stack();
    test_vnspop();
    test_fields();
    printf("delta diff: %d cases, %d mismatches\n", total_cases, total_bad);
    return total_bad != 0;
}
