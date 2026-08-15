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

#include <setjmp.h>

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
extern void ibm_reset_field(delta_loc *);
extern int  ibm_push_ptr(delta_state *, int32_t);
extern int  ibm_ret_ptr_active_record(delta_state *);
extern void ibm_throwDeltaErrorNow(delta_state *);
extern void ibm_vnspop(delta_state *, delta_operand *);
extern void ibm_vpush_var(delta_state *, const delta_operand *);
extern void ibm_DELSPINE(delta_state *, delta_node *);
extern int  ibm_vscanadv(delta_state *, int32_t, int32_t);
extern void ibm_flushDeletedDeltaObjects(delta_state *);
extern void ibm_SETSPINEL(delta_node *, int32_t);
extern void ibm_SETSPINER(delta_state *, int32_t *, int32_t);
extern void ibm_bspush_ca_boa(delta_state *, int16_t);
extern void ibm_bspush_ca_scan_boa(delta_state *, int16_t);
extern void ibm_forceErrorBacktrack(delta_state *);
extern void ibm_push_ptr_init(delta_state *, delta_loc *);
extern void ibm_npush_i(delta_state *, int32_t);
extern void ibm_npush_s(delta_state *, int32_t);
extern void ibm_vscaninit(delta_state *);
extern delta_node *ibm_vmovel(delta_node *, uint8_t);
extern int32_t *ibm_vmover(delta_state *, int32_t *, uint8_t);
extern void ibm_INSSPINEL(delta_state *, delta_node *, delta_node *);
extern void ibm_INSSPINER(delta_state *, delta_node *, delta_node *);
extern delta_node *ibm_lmost(delta_state *, int8_t, delta_node *);
extern int32_t *ibm_rmost(delta_state *, int8_t, int32_t *);
extern void ibm_vassign(delta_state *, const delta_operand *,
                        const delta_operand *);
extern int  ibm_npush_fld(delta_state *, uint8_t, uint8_t);
extern int32_t *ibm_ctxspine(delta_state *, int32_t *, uint8_t, int32_t);
extern void ibm_vnsqflags(delta_state *, int32_t *);
extern void ibm_vinitloc_new(delta_state *, delta_operand *, const delta_loc *);
extern void ibm_startloop(delta_state *, int16_t);
extern void ibm_save_var(delta_state *, const delta_loc *);
extern int  ibm_testFldeq(delta_state *, uint8_t, uint8_t, uint8_t);
extern void ibm_vinitflds(delta_state *, uint8_t, void *, const void *);
extern int  ibm_vscanadvOverToken(delta_state *, int32_t);
extern int  ibm_vscanadvUptoTokenOrMarker(delta_state *, int32_t, int32_t);
extern void ibm_seqscan(delta_state *, delta_seqctl *);
extern int  ibm_advance_tok(delta_state *);
extern int  ibm_forall_cont_from(delta_state *, int16_t, int16_t, int32_t,
                                 delta_loc *, const delta_loc *);
extern void ibm_savescptr(delta_state *, int16_t, delta_loc *);
extern int  ibm_get_parm(delta_state *, delta_loc *, delta_loc *, int16_t);
extern int  ibm_test_synch(delta_state *, int16_t, uint8_t, const uint8_t *);
extern int  ibm_test_string_i(delta_state *, uint8_t, uint8_t, const uint8_t *);
extern int  ibm_test_string_s(delta_state *, uint8_t, uint8_t, const uint8_t *);
extern int32_t ibm_ctxlook(delta_state *, int32_t, uint8_t, int32_t);
extern int  ibm_vnormalize(delta_state *, delta_tpos *);
extern int  ibm_vproject(delta_state *, int32_t, int32_t, int32_t, uint8_t);
extern int  ibm_vmove_tv(delta_state *, delta_tpos *);
extern int  ibm_vtstsnc_tv(delta_state *, delta_tpos *);
extern int  ibm_vtsttmark_tv(delta_state *, delta_tpos *, uint8_t);
extern int  ibm_test_ptr(delta_state *);
extern void ibm_lpta_movel(delta_state *, uint8_t);
extern void ibm_lpta_mover(delta_state *, uint8_t);
extern int  ibm_lpta_tstmover(delta_state *, uint8_t);
extern int  ibm_setscan_l(delta_state *, uint8_t);
extern int  ibm_setscan_r(delta_state *, uint8_t);
extern int  ibm_setscan_nof_l(delta_state *, uint8_t);
extern int  ibm_setscan_nof_r(delta_state *, uint8_t);
extern int32_t ibm_vgetsc(delta_state *, int32_t, int32_t, int32_t, uint8_t);
extern int  ibm_vtimept_tv(delta_state *, delta_tpos *, uint8_t);
extern int  ibm_for_loop_preamble(delta_state *, int32_t, int32_t, int32_t,
                                  const delta_token *);
extern int  ibm_dupsync(delta_state *, int32_t, int32_t, uint8_t);
extern int  ibm_vdef_proj(delta_state *, int32_t, uint8_t);
extern int  ibm_vprt_range(delta_state *, delta_tpos *, delta_tpos *);
extern int  ibm_forto_adv_r(delta_state *, int16_t, int16_t, int16_t, uint8_t,
                            delta_token *, const delta_token *);
extern int  ibm_forto_adv_upto_r(delta_state *, int16_t, int16_t, int16_t,
                                 uint8_t, delta_token *, const delta_token *);
extern int  ibm_setd_lookup(delta_state *, int32_t, int16_t);
extern int  ibm_vmark(delta_state *, uint8_t, uint8_t, int32_t, int32_t,
                      const void *);
extern int  ibm_visleft(delta_state *, int32_t, int32_t);
extern int  ibm_visright(delta_state *, int32_t, int32_t);
/* These two keep their MSVC fastcall manglings, which the rename pass leaves
   alone, so they are reached by their literal symbol names. */
extern void * __attribute__((fastcall))
ibm_allocDeltaHeapObject(delta_state *, int32_t) __asm__("@allocDeltaHeapObject@8");
extern void __attribute__((fastcall))
ibm_freeDeltaHeapObject(delta_state *, void *) __asm__("@freeDeltaHeapObject@8");
extern void ibm_freeDeltaHeapTo(delta_state *, uint8_t *, int32_t);
extern int32_t ibm_getDeltaHeapSegNumber(delta_state *, uint8_t *, int32_t);
extern int  ibm_recordDeltaHeapPos(delta_state *);
extern void *ibm_alloc_tok(delta_state *, const delta_stmt *);
extern void *ibm_alloc_sync(delta_state *);
extern void ibm_free_heap(delta_state *, void *);
extern int  ibm_vcomp_pta(delta_state *, delta_tpos *, delta_tpos *);
extern void ibm_cacheDeletedDeltaObject(delta_state *, void *);
extern int  ibm_compare_ptas(delta_state *);
extern void ibm_delsync(delta_state *, void *);
extern int  ibm_mashtoks(delta_state *, uint8_t, int32_t);
extern int  ibm_vchkseqbad(delta_state *, int32_t, uint8_t, const char *);
extern void *ibm_vins_sync(delta_state *, uint8_t, int32_t, int32_t);
extern int  ibm_chkdelnonseq(delta_state *, int32_t, uint8_t);
extern int  ibm_fdeldel(delta_state *, int32_t, int32_t, int32_t);
extern void ibm_fdel(delta_state *, int32_t, int32_t);
extern int  ibm_vdel_1pt(delta_state *, uint8_t, int32_t, int32_t);
extern int  ibm_vdel_2pt(delta_state *, uint8_t, int32_t, int32_t);
extern int  ibm_vins_tok(delta_state *, uint8_t, int32_t, int32_t,
                         const delta_operand *);
extern int  ibm_vinit_stm(delta_state *, int8_t);
extern int  ibm_ins_tokens_s(delta_state *, uint8_t, const uint8_t *, uint8_t,
                             int32_t);
extern int  ibm_ins_tokens_i(delta_state *, uint8_t, const uint8_t *, uint8_t,
                             int32_t);
extern int32_t ibm_vsplit_time(delta_state *, uint8_t, int32_t, int32_t);
extern int  ibm_vsync_tv(delta_state *, delta_tpos *);
extern int  ibm_vtmark_tv(delta_state *, delta_tpos *, uint8_t);
extern void ibm_delete_1pt(delta_state *, uint8_t);
extern void ibm_lpta_storep(delta_state *, delta_loc *);
extern int  ibm_vrange_l(delta_state *, delta_tpos *, delta_tpos *, int8_t,
                         uint8_t);
extern int  ibm_vrange_r(delta_state *, delta_tpos *, delta_tpos *, int8_t,
                         uint8_t);
extern int  ibm_vrange_2pt(delta_state *, delta_tpos *, delta_tpos *, int8_t,
                           uint8_t);
extern void ibm_insert_l(delta_state *, int8_t, uint8_t, const uint8_t *,
                         uint8_t);
extern void ibm_insert_r(delta_state *, int8_t, uint8_t, const uint8_t *,
                         uint8_t);
extern int  ibm_insert_2pt_s(delta_state *, uint8_t, uint8_t, const uint8_t *,
                             uint8_t);
extern int  ibm_insert_2pt_i(delta_state *, uint8_t, uint8_t, const uint8_t *,
                             uint8_t);
extern int  ibm_delete_2pt(delta_state *, uint8_t, uint8_t);
extern int  ibm_mark_s(delta_state *, uint8_t, uint8_t, uint8_t, uint8_t);
extern int  ibm_mark_v(delta_state *, uint8_t, uint8_t, delta_loc *, uint8_t);
extern int  ibm_insert_2ptv(delta_state *, uint8_t, delta_loc *, uint8_t);
extern void ibm_deltaReinit(delta_state *, int32_t);
extern void ibm_initdelta(delta_state *, uint8_t, const uint8_t *);
extern int  ibm_init_ptr_active_record(delta_state *);
extern int  ibm_ventproc(delta_state *, delta_actrec *, uint8_t *, uint8_t *,
                         uint8_t *, void *);
extern int  ibm_vretproc(delta_state *, int32_t);
extern int  ibm_succeed(delta_state *);
extern void ibm_move_i(delta_state *, delta_loc *, int16_t);
extern void ibm_pause(delta_state *);
extern int  ibm_actd_goto(delta_state *);
extern void ibm_npush_lng(delta_state *, int32_t);
extern void ibm_npush_v(delta_state *, delta_loc *);
extern void ibm_npush_vf(delta_state *, delta_loc *);
extern void ibm_c_assvar(delta_state *, delta_loc *);
extern int  ibm_advance_strm(delta_state *);
extern int32_t ibm_absoluteSyncNum(delta_state *, uint8_t *);
extern int  ibm_while_iterate(delta_state *, int16_t, int16_t);
extern void ibm_proj_def(delta_state *, uint8_t);
extern void ibm_rpta_movel(delta_state *, uint8_t);
extern int  ibm_lpta_tstmovel(delta_state *, uint8_t);
extern void ibm_rpta_storep(delta_state *, delta_loc *);
extern void ibm_lpta_loadv(delta_state *, uint8_t, const delta_loc *);
extern void ibm_settvar_i(delta_state *, delta_loc *, int32_t);
extern void ibm_settvar_s(delta_state *, delta_loc *, int32_t);
extern int  ibm_vnegative(delta_state *, const delta_operand *);
extern void ibm_compare_tvars(delta_state *, delta_loc *, delta_loc *);
extern int  ibm_if_testeq(delta_state *);
extern int  ibm_if_testneq(delta_state *);
extern int  ibm_if_testlt(delta_state *);
extern int  ibm_if_testle(delta_state *);
extern int  ibm_if_testgt(delta_state *);
extern int  ibm_if_testge(delta_state *);
extern void ibm_npop(delta_state *, delta_loc *);
extern void ibm_ncompare_s(delta_state *, uint8_t);
extern int  ibm_forall_to_test(delta_state *, delta_loc *, delta_loc *);
extern int  ibm_mark_i(delta_state *, uint8_t, uint8_t, const void *,
                       uint8_t);
extern int  ibm_vctxt_tv(delta_state *, delta_tpos *);
extern int  ibm_testeq_tvars(delta_state *, delta_loc *, delta_loc *);
extern int  ibm_if_testeq_v_i(delta_state *, delta_loc *, int32_t);
extern int  ibm_if_testneq_v_i(delta_state *, delta_loc *, int32_t);
extern int  ibm_if_testlt_v_i(delta_state *, delta_loc *, int32_t);
extern int  ibm_if_testgt_v_i(delta_state *, delta_loc *, int32_t);
extern int  ibm_if_testge_v_i(delta_state *, delta_loc *, int32_t);
extern void ibm_proj_def_mult(delta_state *, uint8_t, const uint8_t *,
                              const delta_token *);
extern void ibm_lpta_ctxtl(delta_state *, uint8_t);
extern void ibm_lpta_ctxtr(delta_state *, uint8_t);
extern void ibm_rpta_ctxtl(delta_state *, uint8_t);
extern void ibm_rpta_ctxtr(delta_state *, uint8_t);
extern int  ibm_calcETI2WPM(delta_state *, const delta_loc *, delta_loc *);
extern int  ibm_calcMidline(delta_state *, const delta_loc *, delta_loc *);
extern int  ibm_calcSpeedFactori(delta_state *, const delta_loc *,
                                 delta_loc *);
extern int32_t ibm_spine_changed;

#define RECORDS   0x200   /* room for the stack to push into */
#define NSEG      6       /* segments the heap tests build */
#define SEGBYTES  0x100
#define SEGWORDS  (SEGBYTES / 8)
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
    uint8_t     marks[FENCE_MAP];
    delta_seg   stackseg;         /* the segment the stack lives in */
    delta_seg   segs[NSEG];       /* a heap built inside the world */
    int64_t     heapmem[NSEG][SEGWORDS];
    uint8_t     names[0x200];     /* the name stack */
    uint8_t     nodes[0x400];     /* room to build a spine to walk */
    int8_t      nsqf[0x20];       /* which fields decide the spine flags */
    int8_t      nsqm[0x20];       /* one mark per fenced field */
    uint8_t     owner[0x200];     /* what the runtime tells about a move */
    delta_actrec actrec;          /* what a rule frame saves */
    uint8_t     fence2[3][0x40];  /* the fence arrays a rule brings itself */
    uint8_t     sets[0x200];      /* the language's lookup set descriptors */
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
    w->state.fence_marks = w->marks;
    w->stack.nsq_fields = w->nsqf;
    w->vars.nsq_marks = w->nsqm;
    w->state.owner = w->owner;
    w->state.sets = w->sets;
    w->state.fence_fill = (uint8_t)(rng_next() % FENCE_MAP);

    w->stack.names = w->names;
    w->stack.names_depth = (int8_t)(rng_next() % 0x10u);
    w->stack.top = w->records + RECORDS / 2;
    w->stack.limit = w->records + RECORDS / 2 - 0x40;
    w->stack.seg = &w->stackseg;
    w->stackseg.end = w->records + RECORDS;
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
    delta_world *w_ = w;

    *st = w->state;
    *va = w->vars;
    *sk = w->stack;

    /* Several fields hold a node address as a plain word rather than as a
       pointer. Rewrite those the same way, but only when they really are
       addresses in this world; elsewhere they are untouched fill and have to
       compare as they stand. */
#define REBASE_WORD(w)                                                  \
    do {                                                                \
        int32_t v_ = (w);                                               \
        if (v_ >= (int32_t)(intptr_t)w_                                 \
            && v_ < (int32_t)(intptr_t)w_ + (int32_t)sizeof(delta_world)) \
            (w) = v_ - (int32_t)(intptr_t)w_;                           \
    } while (0)

    REBASE_WORD(st->lpta.node);
    REBASE_WORD(st->rpta.node);
    REBASE_WORD(va->scan_ptr);
    REBASE_WORD(sk->spine_l);
    REBASE_WORD(sk->spine_r);
    REBASE_WORD(sk->del_from);
    REBASE_WORD(sk->del_to);
    REBASE_WORD(sk->del_left);
    REBASE_WORD(sk->del_right);
    {
        int ri;

        for (ri = 0; ri < 3; ri++) {
            REBASE_WORD(sk->runs[ri].start);
            REBASE_WORD(sk->runs[ri].cur);
        }
    }
#undef REBASE_WORD

#define REBASE(p) ((p) = (void *)((char *)(p) - base))
    REBASE(st->vars);
    REBASE(st->stack);
    REBASE(st->fence_chars);
    REBASE(st->fence_index);
    REBASE(st->fence_marks);
    REBASE(sk->nsq_fields);
    REBASE(va->nsq_marks);
    REBASE(st->owner);
    REBASE(st->sets);
    REBASE(va->back);
    REBASE(sk->top);
    REBASE(sk->limit);
    REBASE(sk->seg);

    /* The heap's own pointers. Most tests never set these up, so rewrite one
       only when it really points into this world. */
#define REBASE_P(p)                                                     \
    do {                                                                \
        char *v_ = (char *)(p);                                         \
        if (v_ >= (char *)w_                                            \
            && v_ < (char *)w_ + sizeof(delta_world))                   \
            (p) = (void *)(v_ - base);                                  \
    } while (0)
    REBASE_P(sk->heap_first);
    REBASE_P(sk->heap_cur);
    REBASE_P(sk->free_segs);
    {
        int mi;

        for (mi = 0; mi < DELTA_MARKS; mi++) {
            REBASE_P(sk->marks[mi].pos);
            REBASE_P(sk->marks[mi].seg);
        }
    }
#undef REBASE_P
    REBASE(sk->vbot);
    REBASE(sk->base);
    REBASE(sk->names);
#undef REBASE
}

/* Compare the stack area. A record holds the pointer it saved, which differs
   by world, and records are not four-byte aligned, so this walks byte by byte
   and treats four bytes that are an address in both worlds as equal when they
   name the same offset. */
static int region_differs(delta_world *a, delta_world *b,
                          const uint8_t *pa, const uint8_t *pb, size_t len)
{
    int32_t ba = (int32_t)(intptr_t)a;
    int32_t bb = (int32_t)(intptr_t)b;
    int32_t span = (int32_t)sizeof(delta_world);
    size_t i = 0;

    while (i + 4 <= len) {
        int32_t wa, wb;

        memcpy(&wa, pa + i, 4);
        memcpy(&wb, pb + i, 4);

        if (wa >= ba && wa < ba + span && wb >= bb && wb < bb + span) {
            if (wa - ba != wb - bb)
                return 1;
            i += 4;
            continue;
        }
        if (pa[i] != pb[i])
            return 1;
        i++;
    }

    for (; i < len; i++)
        if (pa[i] != pb[i])
            return 1;

    return 0;
}

static const char *diff_where;

static int world_differs(delta_world *a, delta_world *b)
{
    delta_state sa, sb;
    delta_vars va, vb;
    delta_stack ka, kb;

    normalise(a, &sa, &va, &ka);
    normalise(b, &sb, &vb, &kb);

    if (memcmp(&sa, &sb, sizeof(sa)) != 0) {
        diff_where = "state";
        return 1;
    }
    if (memcmp(&va, &vb, sizeof(va)) != 0) {
        diff_where = "vars";
        return 1;
    }
    if (memcmp(&ka, &kb, sizeof(ka)) != 0) {
        diff_where = "stack";
        return 1;
    }
    if (region_differs(a, b, a->records, b->records, RECORDS)) {
        diff_where = "records";
        return 1;
    }
    if (memcmp(a->chars, b->chars, FENCE_MAP) != 0) {
        diff_where = "chars";
        return 1;
    }
    if (memcmp(a->map, b->map, FENCE_MAP) != 0) {
        diff_where = "map";
        return 1;
    }
    if (memcmp(a->marks, b->marks, FENCE_MAP) != 0) {
        diff_where = "marks";
        return 1;
    }
    if (region_differs(a, b, (const uint8_t *)&a->stackseg,
                       (const uint8_t *)&b->stackseg, sizeof(a->stackseg))) {
        diff_where = "stackseg";
        return 1;
    }
    if (region_differs(a, b, (const uint8_t *)a->segs,
                       (const uint8_t *)b->segs, sizeof(a->segs))) {
        diff_where = "segs";
        return 1;
    }
    if (region_differs(a, b, (const uint8_t *)a->heapmem,
                       (const uint8_t *)b->heapmem, sizeof(a->heapmem))) {
        diff_where = "heapmem";
        return 1;
    }
    if (memcmp(a->names, b->names, sizeof(a->names)) != 0) {
        diff_where = "names";
        return 1;
    }
    if (region_differs(a, b, a->nodes, b->nodes, sizeof(a->nodes))) {
        diff_where = "nodes";
        return 1;
    }
    if (memcmp(a->nsqf, b->nsqf, sizeof(a->nsqf)) != 0) {
        diff_where = "nsqf";
        return 1;
    }
    if (memcmp(a->nsqm, b->nsqm, sizeof(a->nsqm)) != 0) {
        diff_where = "nsqm";
        return 1;
    }
    if (region_differs(a, b, a->owner, b->owner, sizeof(a->owner))) {
        diff_where = "owner";
        return 1;
    }
    if (region_differs(a, b, (const uint8_t *)&a->actrec,
                       (const uint8_t *)&b->actrec, sizeof(a->actrec))) {
        diff_where = "actrec";
        return 1;
    }
    if (region_differs(a, b, a->sets, b->sets, sizeof(a->sets))) {
        diff_where = "sets";
        return 1;
    }
    diff_where = "none";
    return 0;
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
                if (bad < 3) printf("  " #name " differs in %s\n", diff_where);        \
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
    a.flag = b.flag = 0;
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
    m->state.lpta.node = (int32_t)(intptr_t)m->records;
    o->state.lpta.node = (int32_t)(intptr_t)o->records;
    if (rng_next() % 2u) {
        ibm_addfence(&m->state, idx); addfence(&o->state, idx);
    } else {
        ibm_remfence(&m->state, idx); remfence(&o->state, idx);
    }
    m->state.lpta.node = o->state.lpta.node = 0;
END(fences)

BEGIN(vnspush)
    delta_operand v;
    static const int16_t kinds[] = {-1, -2, -3, -4, -6, 0};
    v.ptr = m->records + 0x80;
    v.kind = kinds[rng_next() % 6u];
    v.flag = 0;
    ibm_vnspush(&m->state, &v);
    v.ptr = o->records + 0x80;
    vnspush(&o->state, &v);
END(vnspush)

BEGIN(vadd)
    delta_operand a, b;
    static const int16_t kinds[] = {-1, -2, -3, -4, -6, 0};
    a.ptr = m->records + 0x80; b.ptr = m->records + 0x90;
    a.kind = kinds[rng_next() % 6u]; b.kind = kinds[rng_next() % 6u];
    a.flag = b.flag = 0;
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
    if (a.kind != b.kind || a.flag != b.flag) {
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
        delta_loc fa, fb;
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

BEGIN(vpush_var)
    delta_operand v;
    static const int16_t kinds[] = {-1, -2, -3, -4, -6, 0, 1, 2};
    /* size_ac decides where the copy lands, so keep it inside the records. */
    m->stack.size_ac = o->stack.size_ac = 12;
    v.ptr = m->records + 0x80;
    v.kind = kinds[rng_next() % 8u];
    v.flag = 0;
    ibm_vpush_var(&m->state, &v);
    v.ptr = o->records + 0x80;
    vpush_var(&o->state, &v);
    /* The record keeps the source pointer, which differs between worlds. */
    *(int32_t *)(m->stack.top + 4) = 0;
    *(int32_t *)(o->stack.top + 4) = 0;
END(vpush_var)

BEGIN(DELSPINE)
    /* Both links have to point at something real, since unlinking writes
       through each of them. */
    delta_node *t = (delta_node *)(m->records + 0x60);
    delta_node *u = (delta_node *)(o->records + 0x60);
    m->vars.fence_base = o->vars.fence_base = 4;
    t->link = (int32_t)(intptr_t)(m->records + 0x20);
    u->link = (int32_t)(intptr_t)(o->records + 0x20);
    *(int32_t *)((char *)t + 4 * 4 - 8) = (int32_t)(intptr_t)(m->records + 0x40);
    *(int32_t *)((char *)u + 4 * 4 - 8) = (int32_t)(intptr_t)(o->records + 0x40);
    {
        int32_t before_ibm = ibm_spine_changed, before_ours = spine_changed;
        ibm_DELSPINE(&m->state, t); DELSPINE(&o->state, u);
        if (ibm_spine_changed - before_ibm != spine_changed - before_ours)
            bad++;
    }
    /* The links themselves hold addresses, so blank them before comparing. */
    *(int32_t *)(m->records + 0x20 + 4 * 4 - 8) = 0;
    *(int32_t *)(o->records + 0x20 + 4 * 4 - 8) = 0;
    *(int32_t *)(m->records + 0x40 + 4) = 0;
    *(int32_t *)(o->records + 0x40 + 4) = 0;
    t->link = u->link = 0;
    *(int32_t *)((char *)t + 4 * 4 - 8) = 0;
    *(int32_t *)((char *)u + 4 * 4 - 8) = 0;
END(DELSPINE)

BEGIN(vscanadv)
    int32_t step = (int32_t)(rng_next() % 2u);
    int32_t usef = (int32_t)(rng_next() % 2u);
    int32_t ra, rb;
    int i;

    /* The scan walks tagged pointers, so the ground it walks has to hold real
       addresses rather than the random fill. Two nodes are enough: one to
       start on and one to arrive at. */
    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);

    m->vars.fence_base = o->vars.fence_base = 2;
    m->vars.fence_count = o->vars.fence_count = (int8_t)(rng_next() % 4u);
    m->vars.scan_field = o->vars.scan_field = (uint8_t)(rng_next() % 2u);
    m->vars.scan_rev = o->vars.scan_rev = (uint8_t)(rng_next() % 2u);
    m->vars.scan_held = o->vars.scan_held = (uint8_t)(rng_next() % 2u);

    for (i = 0; i < 4; i++) {
        m->chars[i] = o->chars[i] = (uint8_t)i;
        m->marks[i] = o->marks[i] = (uint8_t)(rng_next() % 2u);
    }

    /* Every word the walk can read is either null or a tagged address, with
       the tag bits random so both the fence bit and the sync bit get used. */
    for (i = 0; i < 8; i++) {
        uint32_t r = rng_next();
        int null = (r % 5u) == 0;
        ((int32_t *)(m->records + 0x100))[i] = null ? 0
            : (int32_t)((intptr_t)(m->records + 0x140) | (r & 3u));
        ((int32_t *)(o->records + 0x100))[i] = null ? 0
            : (int32_t)((intptr_t)(o->records + 0x140) | (r & 3u));
    }
    for (i = 0; i < 2; i++) {
        uint32_t r = rng_next();
        int null = (r % 5u) == 0;
        ((int32_t *)(m->records + 0x140))[i] = null ? 0
            : (int32_t)((intptr_t)(m->records + 0x180) | (r & 3u));
        ((int32_t *)(o->records + 0x140))[i] = null ? 0
            : (int32_t)((intptr_t)(o->records + 0x180) | (r & 3u));
    }
    m->vars.scan_ptr = (int32_t)(intptr_t)(m->records + 0x100);
    o->vars.scan_ptr = (int32_t)(intptr_t)(o->records + 0x100);

    ra = ibm_vscanadv(&m->state, step, usef);
    rb = vscanadv(&o->state, step, usef);
    if (ra != rb)
        bad++;
    /* The second hop is not checked for null, so the scan can legitimately
       land on nothing; that is not an address and must not be rebased. */
    if ((m->vars.scan_ptr == 0) != (o->vars.scan_ptr == 0))
        bad++;
    else if (m->vars.scan_ptr != 0
             && m->vars.scan_ptr - (int32_t)(intptr_t)m
                != o->vars.scan_ptr - (int32_t)(intptr_t)o)
        bad++;

    /* Everything left holding an address goes before the byte comparison. */
    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->records + 0x100, 0, 0x60);
    memset(o->records + 0x100, 0, 0x60);
END(vscanadv)


BEGIN(spine_setters)
    /* The value written is a plain number rather than an address, so the
       records still compare byte for byte afterwards. */
    int32_t v = (int32_t)(rng_next() & ~3u);
    delta_node *tm = (delta_node *)(m->records + 0x60);
    delta_node *to = (delta_node *)(o->records + 0x60);
    m->vars.fence_base = o->vars.fence_base = (int32_t)(rng_next() % 8u);
    if (rng_next() % 2u) {
        ibm_SETSPINEL(tm, v); SETSPINEL(to, v);
    } else {
        ibm_SETSPINER(&m->state, (int32_t *)tm, v);
        SETSPINER(&o->state, (int32_t *)to, v);
    }
END(spine_setters)

BEGIN(bspush_boa_pairs)
    int16_t tag = (int16_t)rng_next();
    if (rng_next() % 2u) {
        ibm_bspush_ca_boa(&m->state, tag); bspush_ca_boa(&o->state, tag);
    } else {
        ibm_bspush_ca_scan_boa(&m->state, tag);
        bspush_ca_scan_boa(&o->state, tag);
    }
END(bspush_boa_pairs)

BEGIN(push_ptr_init)
    delta_loc pm, po;
    fill(&pm, sizeof(pm)); po = pm;
    m->vars.ptr_count = o->vars.ptr_count = (int32_t)(rng_next() % 1002u);
    ibm_push_ptr_init(&m->state, &pm);
    push_ptr_init(&o->state, &po);
    if (pm.kind != po.kind || pm.field != po.field || pm.value != po.value)
        bad++;
    /* The slot the count landed on holds the address of a local. */
    if (m->vars.ptr_count == o->vars.ptr_count && m->vars.ptr_count > 0)
        m->vars.ptr_stack[m->vars.ptr_count - 1] =
            o->vars.ptr_stack[o->vars.ptr_count - 1] = 0;
    ibm_flushDeletedDeltaObjects(&m->state);
    flushDeletedDeltaObjects(&o->state);
END(push_ptr_init)

BEGIN(npush_scalars)
    int32_t x = (int32_t)rng_next();
    m->stack.names_depth = o->stack.names_depth = (int8_t)(rng_next() % 0x10u);
    if (rng_next() % 2u) {
        ibm_npush_i(&m->state, x); npush_i(&o->state, x);
    } else {
        ibm_npush_s(&m->state, x); npush_s(&o->state, x);
    }
END(npush_scalars)

BEGIN(vscaninit)
    ibm_vscaninit(&m->state); vscaninit(&o->state);
END(vscaninit)

BEGIN(vmove)
    /* A chain of three nodes, each link null or tagged, so the walk both
       stops early and runs to the end. */
    int32_t base[3];
    uint8_t f;
    int i, j;
    void *ra, *rb;

    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);
    m->vars.fence_base = o->vars.fence_base = 3;
    f = (uint8_t)(rng_next() % 3u);
    base[0] = 0x40; base[1] = 0x90; base[2] = 0xe0;

    for (i = 0; i < 3; i++) {
        for (j = 0; j < 12; j++) {
            uint32_t r = rng_next();
            int null = (r % 3u) == 0 || i == 2;
            ((int32_t *)(m->records + base[i]))[j] = null ? 0
                : (int32_t)((intptr_t)(m->records + base[i + (i < 2)]) | (r & 3u));
            ((int32_t *)(o->records + base[i]))[j] = null ? 0
                : (int32_t)((intptr_t)(o->records + base[i + (i < 2)]) | (r & 3u));
        }
    }

    if (rng_next() % 2u) {
        ra = ibm_vmovel((delta_node *)(m->records + 0x40), f);
        rb = vmovel((delta_node *)(o->records + 0x40), f);
    } else {
        ra = ibm_vmover(&m->state, (int32_t *)(m->records + 0x40), f);
        rb = vmover(&o->state, (int32_t *)(o->records + 0x40), f);
    }
    if ((char *)ra - (char *)m != (char *)rb - (char *)o)
        bad++;
    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);
END(vmove)

BEGIN(insspine)
    /* Three nodes, every link pointing at a real one, since a splice writes
       through all of them. A fence base of five puts the right link at +0x0c,
       clear of the left one at +0x04. */
    enum { FB = 5, RL = FB * 4 - 8 };
    static const int32_t at[3] = {0x40, 0x90, 0xe0};
    delta_node *nm = (delta_node *)(m->records + at[0]);
    delta_node *no = (delta_node *)(o->records + at[0]);
    delta_node *tm = (delta_node *)(m->records + at[1]);
    delta_node *to = (delta_node *)(o->records + at[1]);
    int32_t om = (int32_t)(intptr_t)(m->records + at[2]);
    int32_t oo = (int32_t)(intptr_t)(o->records + at[2]);
    int left = (int)(rng_next() % 2u);
    int32_t before_ibm, before_ours;
    int i, k;

    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);
    m->vars.fence_base = o->vars.fence_base = FB;

#define LINK(w, node, off, val) \
    (*(int32_t *)((char *)(w) + (off)) = (val))
    {
        int32_t tag_l = (int32_t)(rng_next() & 3u);
        int32_t tag_r = (int32_t)(rng_next() & 3u);
        int32_t tag_n = (int32_t)(rng_next() & 3u);

        LINK(tm, 0, 4, om | tag_l);
        LINK(to, 0, 4, oo | tag_l);
        LINK(tm, 0, RL, om | tag_r);
        LINK(to, 0, RL, oo | tag_r);
        LINK((char *)(intptr_t)om, 0, 4, (int32_t)(intptr_t)tm | tag_r);
        LINK((char *)(intptr_t)oo, 0, 4, (int32_t)(intptr_t)to | tag_r);
        LINK((char *)(intptr_t)om, 0, RL, (int32_t)(intptr_t)tm | tag_l);
        LINK((char *)(intptr_t)oo, 0, RL, (int32_t)(intptr_t)to | tag_l);
        LINK(nm, 0, 4, tag_n);
        LINK(no, 0, 4, tag_n);
        LINK(nm, 0, RL, tag_n);
        LINK(no, 0, RL, tag_n);
    }
#undef LINK

    before_ibm = ibm_spine_changed; before_ours = spine_changed;
    if (left) {
        ibm_INSSPINEL(&m->state, nm, tm); INSSPINEL(&o->state, no, to);
    } else {
        ibm_INSSPINER(&m->state, nm, tm); INSSPINER(&o->state, no, to);
    }
    if (ibm_spine_changed - before_ibm != spine_changed - before_ours)
        bad++;

    /* Both links of all three nodes now hold addresses, so compare each as a
       tag plus an offset rather than as a word. */
    for (i = 0; i < 3; i++) {
        static const int32_t off[2] = {4, RL};

        for (k = 0; k < 2; k++) {
            int32_t a = *(int32_t *)(m->records + at[i] + off[k]);
            int32_t b = *(int32_t *)(o->records + at[i] + off[k]);

            if ((a & 3) != (b & 3))
                bad++;
            else if (((a & ~3) == 0) != ((b & ~3) == 0))
                bad++;
            else if ((a & ~3) != 0
                     && (a & ~3) - (int32_t)(intptr_t)m
                        != (b & ~3) - (int32_t)(intptr_t)o)
                bad++;
        }
    }
    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);
END(insspine)

/* longjmp lands back in this frame, so the buffer and the call have to sit
   in a function of their own rather than in the test's loop body. */
static void jump_ibm(delta_state *d)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0)
        ibm_forceErrorBacktrack(d);
    d->vars->err_jmp = 0;
}

static void jump_ours(delta_state *d)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0)
        forceErrorBacktrack(d);
    d->vars->err_jmp = 0;
}

BEGIN(forceErrorBacktrack)
    jump_ibm(&m->state);
    jump_ours(&o->state);
END(forceErrorBacktrack)


/* The ten statement types English declares. Only Ms is walkable, so only a
   walk over that one reaches the field reader the language supplies. */
#define NSTMT 10

BEGIN(stmt_walks)
    static const int32_t at[4] = {0x00, 0x80, 0x100, 0x180};
    /* The original never initialises the flag that decides whether to keep
       walking unless the type's first field is a long or a short, and then
       dereferences a null on that path. Only feed it the types it is really
       called with. */
    int8_t f = -1;
    void *ra, *rb;
    int i, j;

    for (i = 0; i < NSTMT; i++) {
        int k = (int)((rng_next() + (uint32_t)i) % NSTMT);

        if (vstmtbl[k].fields[0].kind == DK_LONG
            || vstmtbl[k].fields[0].kind == DK_SHORT2) {
            f = (int8_t)k;
            break;
        }
    }
    if (f < 0) {
        free(m); free(o);
        continue;
    }

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = 13;

    /* Four nodes in a row, each link either null or pointing at the next,
       so a walk always makes progress and always terminates. */
    for (i = 0; i < 4; i++) {
        int32_t to = at[i < 3 ? i + 1 : 3];

        for (j = 0; j < 24; j++) {
            uint32_t r = rng_next();
            int null = (r % 3u) == 0 || i == 3;

            ((int32_t *)(m->nodes + at[i]))[j] = null ? 0
                : (int32_t)((intptr_t)(m->nodes + to) | (r & 3u));
            ((int32_t *)(o->nodes + at[i]))[j] = null ? 0
                : (int32_t)((intptr_t)(o->nodes + to) | (r & 3u));
        }
    }

    if (rng_next() % 2u) {
        ra = ibm_lmost(&m->state, f, (delta_node *)m->nodes);
        rb = lmost(&o->state, f, (delta_node *)o->nodes);
    } else {
        ra = ibm_rmost(&m->state, f, (int32_t *)m->nodes);
        rb = rmost(&o->state, f, (int32_t *)o->nodes);
    }
    if ((char *)ra - (char *)m != (char *)rb - (char *)o)
        bad++;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(stmt_walks)

BEGIN(vassign)
    delta_operand dm, sm, dof, so;
    static const int16_t scalars[] = {-1, -2, -3, -4, -6};
    int16_t dk, sk;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));

    /* A language kind copies a whole record, so pick one only when it fits
       in the room the two operands have. */
    if (rng_next() % 4u == 0) {
        dk = (int16_t)(rng_next() % NSTMT);
        if (vstmtbl[dk].length > 0x100)
            dk = 0;
    } else {
        dk = scalars[rng_next() % 5u];
    }
    sk = scalars[rng_next() % 5u];

    fill(m->nodes + 0x200, 0x40);
    memcpy(o->nodes + 0x200, m->nodes + 0x200, 0x40);

    dm.ptr = m->nodes;        dm.kind = dk; dm.flag = 0;
    sm.ptr = m->nodes + 0x200; sm.kind = sk; sm.flag = 0;
    dof.ptr = o->nodes;        dof.kind = dk; dof.flag = 0;
    so.ptr = o->nodes + 0x200; so.kind = sk; so.flag = 0;

    ibm_vassign(&m->state, &dm, &sm);
    vassign(&o->state, &dof, &so);
END(vassign)

BEGIN(npush_fld)
    static const int32_t at[4] = {0x00, 0x80, 0x100, 0x180};
    uint8_t st = (uint8_t)(rng_next() % NSTMT);
    uint8_t fld = (uint8_t)(rng_next() % (uint32_t)vstmtbl[st].nfields);
    int ra, rb;
    int i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = 13;
    m->vars.scan_field = o->vars.scan_field = (uint8_t)(rng_next() % NSTMT);
    m->vars.scan_rev = o->vars.scan_rev = (uint8_t)(rng_next() % 2u);
    m->stack.names_depth = o->stack.names_depth = (int8_t)(rng_next() % 0x10u);

    for (i = 0; i < 4; i++) {
        int32_t to = at[i < 3 ? i + 1 : 3];

        for (j = 0; j < 24; j++) {
            uint32_t r = rng_next();
            int null = (r % 3u) == 0 || i == 3;

            ((int32_t *)(m->nodes + at[i]))[j] = null ? 0
                : (int32_t)((intptr_t)(m->nodes + to) | (r & 3u));
            ((int32_t *)(o->nodes + at[i]))[j] = null ? 0
                : (int32_t)((intptr_t)(o->nodes + to) | (r & 3u));
        }
    }
    m->vars.scan_ptr = (int32_t)(intptr_t)m->nodes;
    o->vars.scan_ptr = (int32_t)(intptr_t)o->nodes;

    ra = ibm_npush_fld(&m->state, st, fld);
    rb = npush_fld(&o->state, st, fld);
    if (ra != rb)
        bad++;

    /* What lands on the name stack is a pointer into the spine when the
       field is one of the sized kinds, so blank the slot it used. */
    {
        int32_t slot = (int32_t)m->stack.names_depth * 8;

        if (slot >= 0 && slot + 8 <= (int32_t)sizeof(m->names)) {
            memset(m->names + slot, 0, 8);
            memset(o->names + slot, 0, 8);
        }
    }
    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(npush_fld)


BEGIN(ctxspine)
    static const int32_t at[4] = {0x00, 0x80, 0x100, 0x180};
    uint8_t f = (uint8_t)(rng_next() % NSTMT);
    int32_t back = (int32_t)(rng_next() % 2u);
    void *ra, *rb;
    int i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = 13;

    /* The walk has no stop other than finding what it wants, so the last
       node always satisfies it. */
    for (i = 0; i < 4; i++) {
        int32_t to = at[i < 3 ? i + 1 : 3];

        for (j = 0; j < 24; j++) {
            uint32_t r = rng_next();

            ((int32_t *)(m->nodes + at[i]))[j] =
                (int32_t)((intptr_t)(m->nodes + to) | (r & 3u));
            ((int32_t *)(o->nodes + at[i]))[j] =
                (int32_t)((intptr_t)(o->nodes + to) | (r & 3u));
        }
    }
    ((int32_t *)(m->nodes + at[3]))[13 + f] |= 1;
    ((int32_t *)(o->nodes + at[3]))[13 + f] |= 1;
    ((int32_t *)(m->nodes + at[3]))[2] &= ~2;
    ((int32_t *)(o->nodes + at[3]))[2] &= ~2;

    ra = ibm_ctxspine(&m->state, (int32_t *)m->nodes, f, back);
    rb = ctxspine(&o->state, (int32_t *)o->nodes, f, back);
    if ((char *)ra - (char *)m != (char *)rb - (char *)o)
        bad++;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(ctxspine)

BEGIN(vnsqflags)
    int i;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = 13;
    m->state.fence_fill = o->state.fence_fill = (uint8_t)(rng_next() % 0x0au);

    for (i = 0; i < 0x20; i++) {
        int8_t v = (int8_t)(rng_next() % 12u);

        /* A negative entry ends the nominated list; make one likely early. */
        m->nsqf[i] = o->nsqf[i] = (rng_next() % 4u == 0) ? (int8_t)-1 : v;
        m->nsqm[i] = o->nsqm[i] = (int8_t)(rng_next() % 2u);
    }
    m->nsqf[0x1f] = o->nsqf[0x1f] = -1;

    for (i = 0; i < 24; i++) {
        uint32_t r = rng_next();

        ((int32_t *)m->nodes)[i] = (int32_t)(r & 3u);
        ((int32_t *)o->nodes)[i] = (int32_t)(r & 3u);
    }

    ibm_vnsqflags(&m->state, (int32_t *)m->nodes);
    vnsqflags(&o->state, (int32_t *)o->nodes);
END(vnsqflags)

BEGIN(vinitloc_new)
    delta_operand am, ao;
    delta_loc *lm = (delta_loc *)m->nodes;
    delta_loc *lo = (delta_loc *)o->nodes;
    static const int16_t kinds[] = {-1, -2, -3, -4, -6};

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    fill(m->nodes + 4, 0x20);
    memcpy(o->nodes + 4, m->nodes + 4, 0x20);

    if (rng_next() % 2u) {
        lm->kind = lo->kind = kinds[rng_next() % 5u];
        lm->field = lo->field = (int16_t)rng_next();
    } else {
        int st = (int)(rng_next() % NSTMT);

        lm->kind = lo->kind = (int16_t)st;
        lm->field = lo->field = (rng_next() % 4u == 0) ? (int16_t)-1
            : (int16_t)(rng_next() % (uint32_t)vstmtbl[st].nfields);
    }

    /* Two of the kinds leave the pointer alone, so seed it with something
       that rebases rather than with garbage that cannot. */
    fill(&am, sizeof(am)); ao = am;
    am.ptr = m->nodes + 0x300;
    ao.ptr = o->nodes + 0x300;
    ibm_vinitloc_new(&m->state, &am, lm);
    vinitloc_new(&o->state, &ao, lo);

    if (am.kind != ao.kind || am.flag != ao.flag)
        bad++;
    else if ((char *)am.ptr - (char *)m != (char *)ao.ptr - (char *)o)
        bad++;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(vinitloc_new)


BEGIN(startloop)
    int16_t tag = (int16_t)rng_next();
    ibm_startloop(&m->state, tag); startloop(&o->state, tag);
END(startloop)

BEGIN(save_var)
    delta_loc *lm = (delta_loc *)m->nodes;
    delta_loc *lo = (delta_loc *)o->nodes;
    /* vinitloc_new leaves the pointer alone for the byte and short kinds,
       so a save through one copies from an uninitialised local. That is what
       the original does too; feed it only the kinds it sets. */
    static const int16_t kinds[] = {-3, -4, -6};

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    fill(m->nodes + 4, 0x20);
    memcpy(o->nodes + 4, m->nodes + 4, 0x20);
    m->stack.size_ac = o->stack.size_ac = 12;

    if (rng_next() % 2u) {
        lm->kind = lo->kind = kinds[rng_next() % 3u];
        lm->field = lo->field = (int16_t)rng_next();
    } else {
        int st = (int)(rng_next() % NSTMT);

        lm->kind = lo->kind = (int16_t)st;
        lm->field = lo->field = (rng_next() % 4u == 0) ? (int16_t)-1
            : (int16_t)(rng_next() % (uint32_t)vstmtbl[st].nfields);
    }

    ibm_save_var(&m->state, lm);
    save_var(&o->state, lo);
    /* The record keeps the source pointer, which differs between worlds. */
    *(int32_t *)(m->stack.top + 4) = 0;
    *(int32_t *)(o->stack.top + 4) = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(save_var)

BEGIN(testFldeq)
    static const int32_t at[4] = {0x00, 0x80, 0x100, 0x180};
    uint8_t st = (uint8_t)(rng_next() % NSTMT);
    uint8_t fld = (uint8_t)(rng_next() % (uint32_t)vstmtbl[st].nfields);
    uint8_t val = (uint8_t)(rng_next() % 4u);
    int ra, rb, i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = 13;
    m->vars.scan_field = o->vars.scan_field = (uint8_t)(rng_next() % NSTMT);
    m->vars.scan_rev = o->vars.scan_rev = (uint8_t)(rng_next() % 2u);

    for (i = 0; i < 4; i++) {
        int32_t to = at[i < 3 ? i + 1 : 3];

        for (j = 0; j < 24; j++) {
            uint32_t r = rng_next();
            int null = (r % 3u) == 0 || i == 3;

            ((int32_t *)(m->nodes + at[i]))[j] = null ? 0
                : (int32_t)((intptr_t)(m->nodes + to) | (r & 3u));
            ((int32_t *)(o->nodes + at[i]))[j] = null ? 0
                : (int32_t)((intptr_t)(o->nodes + to) | (r & 3u));
        }
    }
    m->vars.scan_ptr = (int32_t)(intptr_t)m->nodes;
    o->vars.scan_ptr = (int32_t)(intptr_t)o->nodes;

    ra = ibm_testFldeq(&m->state, st, fld, val);
    rb = testFldeq(&o->state, st, fld, val);
    if (ra != rb)
        bad++;

    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(testFldeq)

BEGIN(vinitflds)
    uint8_t st = (uint8_t)(rng_next() % NSTMT);

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    fill(m->nodes + 0x200, 0x20);
    memcpy(o->nodes + 0x200, m->nodes + 0x200, 0x20);

    ibm_vinitflds(&m->state, st, m->nodes, m->nodes + 0x200);
    vinitflds(&o->state, st, o->nodes, o->nodes + 0x200);
END(vinitflds)


/* Build a forward chain of four nodes in the spare node area, the last one
   all null so any walk over it terminates. Both worlds get the same shape. */
static void build_chain(delta_world *m, delta_world *o)
{
    static const int32_t at[4] = {0x00, 0x80, 0x100, 0x180};
    int i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));

    for (i = 0; i < 4; i++) {
        int32_t to = at[i + (i < 3)];

        for (j = 0; j < 24; j++) {
            uint32_t r = rng_next();
            int null = (r % 3u) == 0 || i == 3;

            ((int32_t *)(m->nodes + at[i]))[j] = null ? 0
                : (int32_t)((intptr_t)(m->nodes + to) | (r & 3u));
            ((int32_t *)(o->nodes + at[i]))[j] = null ? 0
                : (int32_t)((intptr_t)(o->nodes + to) | (r & 3u));
        }
    }
}

BEGIN(vscanadvOverToken)
    int32_t usef = (int32_t)(rng_next() % 2u);
    int ra, rb, i;

    build_chain(m, o);
    m->vars.fence_base = o->vars.fence_base = 13;
    m->vars.fence_count = o->vars.fence_count = (int8_t)(rng_next() % 4u);
    m->vars.scan_field = o->vars.scan_field = (uint8_t)(rng_next() % 3u);
    m->vars.scan_rev = o->vars.scan_rev = (uint8_t)(rng_next() % 2u);
    m->vars.scan_held = o->vars.scan_held = (uint8_t)(rng_next() % 2u);
    for (i = 0; i < 4; i++) {
        m->chars[i] = o->chars[i] = (uint8_t)i;
        m->marks[i] = o->marks[i] = (uint8_t)(rng_next() % 2u);
    }
    m->vars.scan_ptr = (int32_t)(intptr_t)m->nodes;
    o->vars.scan_ptr = (int32_t)(intptr_t)o->nodes;

    ra = ibm_vscanadvOverToken(&m->state, usef);
    rb = vscanadvOverToken(&o->state, usef);
    if (ra != rb)
        bad++;
    if ((m->vars.scan_ptr == 0) != (o->vars.scan_ptr == 0))
        bad++;
    else if (m->vars.scan_ptr != 0
             && m->vars.scan_ptr - (int32_t)(intptr_t)m
                != o->vars.scan_ptr - (int32_t)(intptr_t)o)
        bad++;

    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(vscanadvOverToken)

BEGIN(vscanadvUptoTokenOrMarker)
    static const int32_t at[4] = {0x00, 0x80, 0x100, 0x180};
    int32_t usef = (int32_t)(rng_next() % 2u);
    int32_t which = (int32_t)(rng_next() % 5u);
    int32_t tm, to;
    int ra, rb, i;

    build_chain(m, o);
    m->vars.fence_base = o->vars.fence_base = 13;
    m->vars.fence_count = o->vars.fence_count = (int8_t)(rng_next() % 4u);
    m->vars.scan_field = o->vars.scan_field = (uint8_t)(rng_next() % 3u);
    m->vars.scan_rev = o->vars.scan_rev = (uint8_t)(rng_next() % 2u);
    m->vars.scan_held = o->vars.scan_held = (uint8_t)(rng_next() % 2u);
    for (i = 0; i < 4; i++) {
        m->chars[i] = o->chars[i] = (uint8_t)i;
        m->marks[i] = o->marks[i] = (uint8_t)(rng_next() % 2u);
    }
    m->vars.scan_ptr = (int32_t)(intptr_t)m->nodes;
    o->vars.scan_ptr = (int32_t)(intptr_t)o->nodes;

    /* Aim the marker at one of the nodes most of the time, so the early
       stop gets used as well as the walk running out. */
    tm = (which < 4) ? (int32_t)(intptr_t)(m->nodes + at[which]) : 0;
    to = (which < 4) ? (int32_t)(intptr_t)(o->nodes + at[which]) : 0;

    ra = ibm_vscanadvUptoTokenOrMarker(&m->state, tm, usef);
    rb = vscanadvUptoTokenOrMarker(&o->state, to, usef);
    if (ra != rb)
        bad++;
    if ((m->vars.scan_ptr == 0) != (o->vars.scan_ptr == 0))
        bad++;
    else if (m->vars.scan_ptr != 0
             && m->vars.scan_ptr - (int32_t)(intptr_t)m
                != o->vars.scan_ptr - (int32_t)(intptr_t)o)
        bad++;

    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(vscanadvUptoTokenOrMarker)

BEGIN(seqscan)
    /* Five nodes: four in a row plus the peer the walk reads its stopping
       fields from. The last of the four carries every one of them, so the
       walk always terminates. */
    static const int32_t at[5] = {0x00, 0x80, 0x100, 0x180, 0x200};
    enum { BASE = 13, FILL = 4 };
    delta_seqctl cm, co;
    int32_t back, walk, peer;
    int i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = BASE;
    m->state.fence_fill = o->state.fence_fill = FILL;

    cm.kind = co.kind = (int8_t)(rng_next() % 2u);
    back = cm.kind == 1;
    walk = back ? (BASE * 4 - 8) / 4 : 1;
    peer = back ? 1 : (BASE * 4 - 8) / 4;

    for (i = 0; i < 5; i++) {
        for (j = 0; j < 24; j++) {
            uint32_t r = rng_next();

            ((int32_t *)(m->nodes + at[i]))[j] = (int32_t)(r & 3u);
            ((int32_t *)(o->nodes + at[i]))[j] = (int32_t)(r & 3u);
        }
    }
    for (i = 0; i < 4; i++) {
        int32_t to = at[i + (i < 3)];
        uint32_t tag = rng_next() & 3u;

        ((int32_t *)(m->nodes + at[i]))[walk] =
            (int32_t)((intptr_t)(m->nodes + to) | tag);
        ((int32_t *)(o->nodes + at[i]))[walk] =
            (int32_t)((intptr_t)(o->nodes + to) | tag);
    }
    {
        uint32_t tag = rng_next() & 3u;

        ((int32_t *)m->nodes)[peer] =
            (int32_t)((intptr_t)(m->nodes + at[4]) | tag);
        ((int32_t *)o->nodes)[peer] =
            (int32_t)((intptr_t)(o->nodes + at[4]) | tag);
    }
    /* At least one field has to be collected or the walk never stops. */
    for (i = 0; i < FILL; i++) {
        int32_t bit = (int32_t)(rng_next() % 2u);

        ((int32_t *)(m->nodes + at[4]))[BASE + i] = bit;
        ((int32_t *)(o->nodes + at[4]))[BASE + i] = bit;
        ((int32_t *)(m->nodes + at[3]))[BASE + i] |= 1;
        ((int32_t *)(o->nodes + at[3]))[BASE + i] |= 1;
    }
    ((int32_t *)(m->nodes + at[4]))[BASE] = 1;
    ((int32_t *)(o->nodes + at[4]))[BASE] = 1;

    cm.flag = co.flag = (int32_t)rng_next();
    cm.start = (int32_t)(intptr_t)m->nodes;
    co.start = (int32_t)(intptr_t)o->nodes;
    cm.pad_01[0] = co.pad_01[0] = 0;
    cm.pad_01[1] = co.pad_01[1] = 0;
    cm.pad_01[2] = co.pad_01[2] = 0;

    ibm_seqscan(&m->state, &cm);
    seqscan(&o->state, &co);

    if (cm.flag != co.flag)
        bad++;
    if (cm.cur - (int32_t)(intptr_t)m != co.cur - (int32_t)(intptr_t)o)
        bad++;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(seqscan)


BEGIN(advance_tok)
    int ra, rb, i;

    build_chain(m, o);
    m->vars.fence_base = o->vars.fence_base = 13;
    m->vars.fence_count = o->vars.fence_count = (int8_t)(rng_next() % 4u);
    m->vars.scan_field = o->vars.scan_field = (uint8_t)(rng_next() % 3u);
    m->vars.scan_rev = o->vars.scan_rev = (uint8_t)(rng_next() % 2u);
    m->vars.scan_held = o->vars.scan_held = (uint8_t)(rng_next() % 2u);
    for (i = 0; i < 4; i++) {
        m->chars[i] = o->chars[i] = (uint8_t)i;
        m->marks[i] = o->marks[i] = (uint8_t)(rng_next() % 2u);
    }
    m->vars.scan_ptr = (int32_t)(intptr_t)m->nodes;
    o->vars.scan_ptr = (int32_t)(intptr_t)o->nodes;

    ra = ibm_advance_tok(&m->state);
    rb = advance_tok(&o->state);
    if (ra != rb)
        bad++;
    if ((m->vars.scan_ptr == 0) != (o->vars.scan_ptr == 0))
        bad++;
    else if (m->vars.scan_ptr != 0
             && m->vars.scan_ptr - (int32_t)(intptr_t)m
                != o->vars.scan_ptr - (int32_t)(intptr_t)o)
        bad++;

    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(advance_tok)

/* Fill in a location the two location-taking primitives will accept, in both
   worlds at once. The byte and short kinds are left out because
   vinitloc_new does not set a pointer for them. */
static void make_loc(delta_world *m, delta_world *o, int32_t at)
{
    static const int16_t kinds[] = {-3, -4, -6};
    delta_loc *lm = (delta_loc *)(m->nodes + at);
    delta_loc *lo = (delta_loc *)(o->nodes + at);

    if (rng_next() % 2u) {
        lm->kind = lo->kind = kinds[rng_next() % 3u];
        lm->field = lo->field = (int16_t)rng_next();
    } else {
        int st = (int)(rng_next() % NSTMT);

        lm->kind = lo->kind = (int16_t)st;
        lm->field = lo->field = (rng_next() % 4u == 0) ? (int16_t)-1
            : (int16_t)(rng_next() % (uint32_t)vstmtbl[st].nfields);
    }
}

BEGIN(forall_cont_from)
    int16_t tag = (int16_t)rng_next();
    int16_t loop = (int16_t)rng_next();
    int ra, rb;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);
    fill(m->nodes + 0x100, 0x80);
    memcpy(o->nodes + 0x100, m->nodes + 0x100, 0x80);
    m->stack.size_ac = o->stack.size_ac = 12;
    m->vars.testing = (int8_t)(rng_next() % 2u);
    o->vars.testing = m->vars.testing;

    make_loc(m, o, 0x00);
    make_loc(m, o, 0x40);

    ra = ibm_forall_cont_from(&m->state, tag, loop, 0,
                              (delta_loc *)m->nodes,
                              (const delta_loc *)(m->nodes + 0x40));
    rb = forall_cont_from(&o->state, tag, loop, 0,
                          (delta_loc *)o->nodes,
                          (const delta_loc *)(o->nodes + 0x40));
    if (ra != rb)
        bad++;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(forall_cont_from)

BEGIN(savescptr)
    int16_t tag = (int16_t)rng_next();

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);
    m->stack.size_ac = o->stack.size_ac = 12;
    m->vars.testing = (int8_t)(rng_next() % 2u);
    o->vars.testing = m->vars.testing;
    m->vars.scan_ptr = (int32_t)(intptr_t)(m->nodes + 0x80);
    o->vars.scan_ptr = (int32_t)(intptr_t)(o->nodes + 0x80);

    make_loc(m, o, 0x00);

    ibm_savescptr(&m->state, tag, (delta_loc *)m->nodes);
    savescptr(&o->state, tag, (delta_loc *)o->nodes);

    /* The location and the saved record both now hold the scan pointer. */
    if (((delta_loc *)m->nodes)->value - (int32_t)(intptr_t)m
        != ((delta_loc *)o->nodes)->value - (int32_t)(intptr_t)o)
        bad++;
    ((delta_loc *)m->nodes)->value = ((delta_loc *)o->nodes)->value = 0;
    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(savescptr)


BEGIN(get_parm)
    static const int16_t kinds[] = {-1, -2, -3, -4, -6};
    int16_t want = kinds[rng_next() % 5u];
    delta_loc *om = (delta_loc *)(m->nodes + 0x100);
    delta_loc *oo = (delta_loc *)(o->nodes + 0x100);
    int ra, rb;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);
    m->vars.ptr_count = o->vars.ptr_count = (int32_t)(rng_next() % 1002u);

    make_loc(m, o, 0x00);
    /* The source may also be one of the two kinds vinitloc_new skips, since
       get_parm only reads its halves in that case. */
    if (rng_next() % 4u == 0) {
        ((delta_loc *)m->nodes)->kind = ((delta_loc *)o->nodes)->kind
            = (rng_next() % 2u) ? (int16_t)-1 : (int16_t)-2;
    }
    ((delta_loc *)m->nodes)->value = ((delta_loc *)o->nodes)->value
        = (int32_t)rng_next();

    fill(om, sizeof(*om));
    *oo = *om;

    ra = ibm_get_parm(&m->state, om, (delta_loc *)m->nodes, want);
    rb = get_parm(&o->state, oo, (delta_loc *)o->nodes, want);
    if (ra != rb)
        bad++;
    if (om->kind != oo->kind || om->field != oo->field
        || om->value != oo->value)
        bad++;
    /* A sync parameter is pushed by address, which differs by world. */
    if (m->vars.ptr_count == o->vars.ptr_count && m->vars.ptr_count > 0)
        m->vars.ptr_stack[m->vars.ptr_count - 1] =
            o->vars.ptr_stack[o->vars.ptr_count - 1] = 0;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(get_parm)

BEGIN(test_synch)
    static const int32_t at[4] = {0x00, 0x80, 0x100, 0x180};
    int16_t tag = (int16_t)rng_next();
    uint8_t n = (uint8_t)(1u + rng_next() % 3u);
    uint8_t list[3];
    int ra, rb, i, j;

    build_chain(m, o);
    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);
    m->vars.fence_base = o->vars.fence_base = 13;
    m->vars.fence_count = o->vars.fence_count = (int8_t)(rng_next() % 3u);
    m->vars.scan_field = o->vars.scan_field = (uint8_t)(rng_next() % 3u);
    m->vars.scan_rev = o->vars.scan_rev = (uint8_t)(rng_next() % 2u);
    m->vars.scan_held = o->vars.scan_held = (uint8_t)(rng_next() % 2u);
    m->stack.size_b0 = o->stack.size_b0 = 16;
    m->stack.ca_size = o->stack.ca_size = 8;

    for (i = 0; i < 4; i++) {
        m->chars[i] = o->chars[i] = (uint8_t)i;
        m->marks[i] = o->marks[i] = (uint8_t)(rng_next() % 2u);
    }
    for (i = 0; i < 3; i++)
        list[i] = (uint8_t)(rng_next() % 4u);
    for (i = 0; i < FENCE_MAP; i++)
        m->map[i] = o->map[i] = (uint8_t)(rng_next() % 4u);

    /* The last node carries every field, so the walk always terminates. */
    for (j = 0; j < 4; j++) {
        ((int32_t *)(m->nodes + at[3]))[13 + j] |= 1;
        ((int32_t *)(o->nodes + at[3]))[13 + j] |= 1;
    }
    m->vars.scan_ptr = (int32_t)(intptr_t)m->nodes;
    o->vars.scan_ptr = (int32_t)(intptr_t)o->nodes;

    ra = ibm_test_synch(&m->state, tag, n, list);
    rb = test_synch(&o->state, tag, n, list);
    if (ra != rb)
        bad++;
    if ((m->vars.scan_ptr == 0) != (o->vars.scan_ptr == 0))
        bad++;
    else if (m->vars.scan_ptr != 0
             && m->vars.scan_ptr - (int32_t)(intptr_t)m
                != o->vars.scan_ptr - (int32_t)(intptr_t)o)
        bad++;

    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(test_synch)


/* The two string tests need a chain the scan can walk and field bytes small
   enough that a match happens often rather than never. */
static void string_setup(delta_world *m, delta_world *o, uint8_t st)
{
    static const int32_t at[4] = {0x00, 0x80, 0x100, 0x180};
    int i;

    build_chain(m, o);

    /* These two loop, and vscanadv can leave the scan on null without saying
       so, which the next pass then dereferences. The original faults there
       too, so keep every step word pointing at something: each node forward,
       the last at itself, and its first word not a sync or the string would
       never be consumed. */
    for (i = 0; i < 4; i++) {
        int32_t to = at[i + (i < 3)];
        uint32_t tag = rng_next() & 3u;

        ((int32_t *)(m->nodes + at[i]))[0] =
            (int32_t)((intptr_t)(m->nodes + to) | (i == 3 ? (tag & 1u) : tag));
        ((int32_t *)(o->nodes + at[i]))[0] =
            (int32_t)((intptr_t)(o->nodes + to) | (i == 3 ? (tag & 1u) : tag));
        tag = rng_next() & 3u;
        ((int32_t *)(m->nodes + at[i]))[1] =
            (int32_t)((intptr_t)(m->nodes + to) | tag);
        ((int32_t *)(o->nodes + at[i]))[1] =
            (int32_t)((intptr_t)(o->nodes + to) | tag);
    }
    for (i = 3; i < 24; i++) {
        uint32_t tag = rng_next() & 3u;

        ((int32_t *)(m->nodes + at[3]))[i] =
            (int32_t)((intptr_t)(m->nodes + at[3]) | tag);
        ((int32_t *)(o->nodes + at[3]))[i] =
            (int32_t)((intptr_t)(o->nodes + at[3]) | tag);
    }
    m->vars.fence_base = o->vars.fence_base = 13;
    m->vars.fence_count = o->vars.fence_count = (int8_t)(rng_next() % 3u);
    m->vars.scan_field = o->vars.scan_field = (uint8_t)(rng_next() % 3u);
    m->vars.scan_rev = o->vars.scan_rev = (uint8_t)(rng_next() % 2u);
    m->vars.scan_held = o->vars.scan_held = (uint8_t)(rng_next() % 2u);

    for (i = 0; i < 4; i++) {
        m->chars[i] = o->chars[i] = (uint8_t)i;
        m->marks[i] = o->marks[i] = (uint8_t)(rng_next() % 2u);
    }

    for (i = 0; i < 4; i++) {
        uint8_t v = (uint8_t)(rng_next() % 4u);

        *(uint8_t *)vstmtbl[st].get[0](m->nodes + at[i] + 8) = v;
        *(uint8_t *)vstmtbl[st].get[0](o->nodes + at[i] + 8) = v;
    }

    m->vars.scan_ptr = (int32_t)(intptr_t)m->nodes;
    o->vars.scan_ptr = (int32_t)(intptr_t)o->nodes;
}

BEGIN(test_string_i)
    uint8_t st = (uint8_t)(rng_next() % NSTMT);
    uint8_t str[8];
    uint8_t n = (uint8_t)(2u * (1u + rng_next() % 3u));
    int ra, rb, i;

    string_setup(m, o, st);
    for (i = 0; i < 8; i += 2) {
        str[i] = (uint8_t)(rng_next() % 2u ? 0x80 : 0x00);
        str[i + 1] = (uint8_t)(rng_next() % 4u);
    }

    ra = ibm_test_string_i(&m->state, st, n, str);
    rb = test_string_i(&o->state, st, n, str);
    if (ra != rb)
        bad++;
    if ((m->vars.scan_ptr == 0) != (o->vars.scan_ptr == 0))
        bad++;
    else if (m->vars.scan_ptr != 0
             && m->vars.scan_ptr - (int32_t)(intptr_t)m
                != o->vars.scan_ptr - (int32_t)(intptr_t)o)
        bad++;

    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(test_string_i)

BEGIN(test_string_s)
    uint8_t st = (uint8_t)(rng_next() % NSTMT);
    uint8_t str[8];
    uint8_t n = (uint8_t)(1u + rng_next() % 4u);
    int ra, rb, i;

    string_setup(m, o, st);
    for (i = 0; i < 8; i++)
        str[i] = (uint8_t)(rng_next() % 4u);

    ra = ibm_test_string_s(&m->state, st, n, str);
    rb = test_string_s(&o->state, st, n, str);
    if (ra != rb)
        bad++;
    if ((m->vars.scan_ptr == 0) != (o->vars.scan_ptr == 0))
        bad++;
    else if (m->vars.scan_ptr != 0
             && m->vars.scan_ptr - (int32_t)(intptr_t)m
                != o->vars.scan_ptr - (int32_t)(intptr_t)o)
        bad++;

    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(test_string_s)


BEGIN(ctxlook)
    /* A real little spine: six nodes in a row, three header words, ten left
       syncs, two trailer words, then ten right syncs, so the fence base is
       fifteen. Every sync points at the neighbour rather than anywhere, which
       is what makes the walk converge; only the flag bits are randomised. */
    enum { FB = 15, NNODE = 6, STEP = 0x80 };
    uint8_t f = (uint8_t)(rng_next() % NSTMT);
    int32_t right = (int32_t)(rng_next() % 2u);
    int32_t ra, rb;
    int i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = FB;
    m->state.fence_fill = o->state.fence_fill = (uint8_t)(rng_next() % 5u);

#define NODE(w, i) ((int32_t *)((w)->nodes + (i) * STEP))
#define AT(w, i)   ((int32_t)(intptr_t)((w)->nodes + (i) * STEP))
    for (i = 0; i < NNODE; i++) {
        uint32_t r;

        /* Every node is itself a sync, so a lookup lands on the neighbour
           rather than taking a second hop into nothing. */
        r = rng_next();
        NODE(m, i)[0] = 2 | (int32_t)(r & 1u);
        NODE(o, i)[0] = NODE(m, i)[0];

        r = rng_next();
        NODE(m, i)[1] = (i + 1 < NNODE ? AT(m, i + 1) : 0) | (int32_t)(r & 3u);
        NODE(o, i)[1] = (i + 1 < NNODE ? AT(o, i + 1) : 0) | (int32_t)(r & 3u);

        r = rng_next();
        NODE(m, i)[2] = (r % 3u == 0) ? 0
            : (AT(m, (int)((r >> 8) % NNODE)) | (int32_t)(r & 3u));
        NODE(o, i)[2] = (r % 3u == 0) ? 0
            : (AT(o, (int)((r >> 8) % NNODE)) | (int32_t)(r & 3u));

        for (j = 0; j < 10; j++) {
            r = rng_next();
            NODE(m, i)[3 + j] = (i > 0 ? AT(m, i - 1) : 0) | (int32_t)(r & 1u);
            NODE(o, i)[3 + j] = (i > 0 ? AT(o, i - 1) : 0) | (int32_t)(r & 1u);

            r = rng_next();
            NODE(m, i)[FB + j] =
                (i + 1 < NNODE ? AT(m, i + 1) : 0) | (int32_t)(r & 1u);
            NODE(o, i)[FB + j] =
                (i + 1 < NNODE ? AT(o, i + 1) : 0) | (int32_t)(r & 1u);
        }

        NODE(m, i)[FB - 2] = (i > 0 ? AT(m, i - 1) : 0);
        NODE(o, i)[FB - 2] = (i > 0 ? AT(o, i - 1) : 0);
        /* The context link is what ctxlook borrows, and the runtime leaves it
           at zero when idle. Anything else lets its chain close on itself. */
        NODE(m, i)[FB - 1] = 0;
        NODE(o, i)[FB - 1] = 0;
    }

    m->stack.spine_l = AT(m, 0);
    o->stack.spine_l = AT(o, 0);
    m->stack.spine_r = AT(m, NNODE - 1);
    o->stack.spine_r = AT(o, NNODE - 1);

    ra = ibm_ctxlook(&m->state, AT(m, 2), f, right);
    rb = ctxlook(&o->state, AT(o, 2), f, right);
#undef NODE
#undef AT

    if ((ra == 0) != (rb == 0))
        bad++;
    else if (ra != 0
             && ra - (int32_t)(intptr_t)m != rb - (int32_t)(intptr_t)o)
        bad++;

    m->stack.spine_l = o->stack.spine_l = 0;
    m->stack.spine_r = o->stack.spine_r = 0;
END(ctxlook)



/* Six nodes in a row plus a terminator at each end, in both worlds. Every
   link is followable, because the timing walks spend an offset stepping along
   a field and never check for a null; the ends cannot point at themselves
   either, or lmost and rmost never stop. A terminator is not a sync, which is
   what makes both walks halt on it. Every field value is positive, so
   spending an offset always finishes. Returns the field to use, or -1 when
   no statement type has a kind the timing code can read. */
static int8_t build_tspine(delta_world *m, delta_world *o)
{
    enum { FB = 15, NSPINE = 6, NNODE = 8, STEP = 0x80 };
    int8_t f = -1;
    int i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = FB;

    for (i = 0; i < NSTMT; i++) {
        int k = (int)((rng_next() + (uint32_t)i) % NSTMT);

        if (vstmtbl[k].fields[0].kind == DK_LONG
            || vstmtbl[k].fields[0].kind == DK_SHORT2) {
            f = (int8_t)k;
            break;
        }
    }
    if (f < 0)
        return -1;

#define NODE(w, i) ((int32_t *)((w)->nodes + (i) * STEP))
#define AT(w, i)   ((int32_t)(intptr_t)((w)->nodes + (i) * STEP))
    for (i = 0; i < NNODE; i++) {
        int end = i >= NSPINE;
        /* A terminator points only at itself. Anything that walks off the
           end has to stop there rather than be sent back into the spine,
           which would close a loop no walk ever leaves. */
        int lo = end ? i : (i > 0 ? i - 1 : NSPINE);
        int hi = end ? i : (i + 1 < NSPINE ? i + 1 : NSPINE + 1);
        uint32_t r;

        r = rng_next();
        NODE(m, i)[0] = AT(m, lo) | (int32_t)(end ? (r & 1u) : (r & 3u));
        NODE(o, i)[0] = AT(o, lo) | (int32_t)(end ? (r & 1u) : (r & 3u));

        r = rng_next();
        NODE(m, i)[1] = AT(m, hi) | (int32_t)(r & 3u);
        NODE(o, i)[1] = AT(o, hi) | (int32_t)(r & 3u);

        /* A terminator's sync arrays are empty. Anything that walks them
           has to be able to reach an end, and pointing them back into the
           spine closes a loop the walk never leaves. */
        for (j = 0; j < 10; j++) {
            r = rng_next();
            NODE(m, i)[3 + j] = end ? 0 : (AT(m, lo) | (int32_t)(r & 3u));
            NODE(o, i)[3 + j] = end ? 0 : (AT(o, lo) | (int32_t)(r & 3u));

            r = rng_next();
            NODE(m, i)[FB + j] = end ? 0 : (AT(m, hi) | (int32_t)(r & 3u));
            NODE(o, i)[FB + j] = end ? 0 : (AT(o, hi) | (int32_t)(r & 3u));
        }

        /* Comfortably more than any offset a test hands in, so a walk
           always runs out on the first statement it looks at. */
        r = 16u + rng_next() % 16u;
        if (vstmtbl[f].fields[0].kind == DK_LONG) {
            *(int32_t *)vstmtbl[f].get[0](m->nodes + i * STEP + 8) =
                (int32_t)r;
            *(int32_t *)vstmtbl[f].get[0](o->nodes + i * STEP + 8) =
                (int32_t)r;
        } else {
            *(int16_t *)vstmtbl[f].get[0](m->nodes + i * STEP + 8) =
                (int16_t)r;
            *(int16_t *)vstmtbl[f].get[0](o->nodes + i * STEP + 8) =
                (int16_t)r;
        }
    }

    m->stack.spine_l = AT(m, 0);
    o->stack.spine_l = AT(o, 0);
    m->stack.spine_r = AT(m, NSPINE - 1);
    o->stack.spine_r = AT(o, NSPINE - 1);
#undef NODE
#undef AT
    return f;
}

/* Where a timing test starts from, filled in for both worlds at once. */
static void make_tpos(delta_world *m, delta_world *o, int8_t f,
                      delta_tpos *pm, delta_tpos *po, uint8_t flags)
{
    pm->node = (int32_t)(intptr_t)(m->nodes + 2 * 0x80);
    po->node = (int32_t)(intptr_t)(o->nodes + 2 * 0x80);
    pm->field = po->field = f;
    pm->pad_05[0] = po->pad_05[0] = 0;
    pm->pad_05[1] = po->pad_05[1] = 0;
    pm->pad_05[2] = po->pad_05[2] = 0;
    pm->offset = po->offset = (int32_t)(rng_next() % 21u) - 10;
    pm->flags = po->flags = flags;
    pm->pad_0d[0] = po->pad_0d[0] = 0;
    pm->pad_0d[1] = po->pad_0d[1] = 0;
    pm->pad_0d[2] = po->pad_0d[2] = 0;
}

BEGIN(vnormalize)
    static const uint8_t sets[3] = {0, 4, 8};
    delta_tpos pm, po;
    int8_t f = build_tspine(m, o);
    int ra, rb;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    make_tpos(m, o, f, &pm, &po, sets[rng_next() % 3u]);

    ra = ibm_vnormalize(&m->state, &pm);
    rb = vnormalize(&o->state, &po);

    if (ra != rb)
        bad++;
    if (pm.offset != po.offset || pm.flags != po.flags)
        bad++;
    if (pm.node - (int32_t)(intptr_t)m != po.node - (int32_t)(intptr_t)o)
        bad++;

    m->stack.spine_l = o->stack.spine_l = 0;
    m->stack.spine_r = o->stack.spine_r = 0;
END(vnormalize)



/* The spine the projection primitives need: four nodes in a row with every
   neighbour real, because two of the three splices write through one of them
   without checking. Only the flag bits vary. */
static void build_pspine(delta_world *m, delta_world *o)
{
    enum { FB = 15, NNODE = 4, STEP = 0x80 };
    int i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = FB;
    m->vars.relink = o->vars.relink = (int32_t)(rng_next() % 2u);
    m->vars.ctx_both = o->vars.ctx_both = (int32_t)(rng_next() % 2u);
    m->state.fence_fill = o->state.fence_fill = (uint8_t)(rng_next() % 5u);
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = (int8_t)(rng_next() % 2u);

#define NODE(w, i) ((int32_t *)((w)->nodes + (i) * STEP))
#define AT(w, i)   ((int32_t)(intptr_t)((w)->nodes + (i) * STEP))
    for (i = 0; i < NNODE; i++) {
        int lo = i > 0 ? i - 1 : 0;
        int hi = i + 1 < NNODE ? i + 1 : NNODE - 1;
        uint32_t r;

        r = rng_next();
        NODE(m, i)[0] = AT(m, lo) | 2 | (int32_t)(r & 1u);
        NODE(o, i)[0] = AT(o, lo) | 2 | (int32_t)(r & 1u);

        r = rng_next();
        NODE(m, i)[1] = AT(m, hi) | (int32_t)(r & 3u);
        NODE(o, i)[1] = AT(o, hi) | (int32_t)(r & 3u);

        r = rng_next();
        NODE(m, i)[2] = (int32_t)(r & 3u);
        NODE(o, i)[2] = (int32_t)(r & 3u);

        for (j = 0; j < 10; j++) {
            r = rng_next();
            NODE(m, i)[3 + j] = AT(m, lo) | (int32_t)(r & 3u);
            NODE(o, i)[3 + j] = AT(o, lo) | (int32_t)(r & 3u);

            r = rng_next();
            NODE(m, i)[FB + j] = AT(m, hi) | (int32_t)(r & 3u);
            NODE(o, i)[FB + j] = AT(o, hi) | (int32_t)(r & 3u);
        }

        NODE(m, i)[FB - 2] = AT(m, lo);
        NODE(o, i)[FB - 2] = AT(o, lo);
        NODE(m, i)[FB - 1] = 0;
        NODE(o, i)[FB - 1] = 0;
    }
    m->stack.spine_l = AT(m, 0);
    o->stack.spine_l = AT(o, 0);
    m->stack.spine_r = AT(m, NNODE - 1);
    o->stack.spine_r = AT(o, NNODE - 1);
#undef NODE
#undef AT
}

BEGIN(vproject)
    /* Four nodes. The two neighbours are always real, because two of the
       three splices write through one of them without checking, and only
       their sync bits are varied. */
    enum { FB = 15, NNODE = 4, STEP = 0x80 };
    uint8_t f = (uint8_t)(rng_next() % NSTMT);
    int32_t ra, rb;
    int i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = FB;
    m->vars.relink = o->vars.relink = (int32_t)(rng_next() % 2u);
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = (int8_t)(rng_next() % 2u);

#define NODE(w, i) ((int32_t *)((w)->nodes + (i) * STEP))
#define AT(w, i)   ((int32_t)(intptr_t)((w)->nodes + (i) * STEP))
    for (i = 0; i < NNODE; i++) {
        int lo = i > 0 ? i - 1 : 0;
        int hi = i + 1 < NNODE ? i + 1 : NNODE - 1;
        uint32_t r;

        r = rng_next();
        NODE(m, i)[0] = AT(m, lo) | (int32_t)(r & 3u);
        NODE(o, i)[0] = AT(o, lo) | (int32_t)(r & 3u);

        r = rng_next();
        NODE(m, i)[1] = AT(m, hi) | (int32_t)(r & 3u);
        NODE(o, i)[1] = AT(o, hi) | (int32_t)(r & 3u);

        r = rng_next();
        NODE(m, i)[2] = (int32_t)(r & 3u);
        NODE(o, i)[2] = (int32_t)(r & 3u);

        for (j = 0; j < 10; j++) {
            r = rng_next();
            NODE(m, i)[3 + j] = AT(m, lo) | (int32_t)(r & 3u);
            NODE(o, i)[3 + j] = AT(o, lo) | (int32_t)(r & 3u);

            r = rng_next();
            NODE(m, i)[FB + j] = AT(m, hi) | (int32_t)(r & 3u);
            NODE(o, i)[FB + j] = AT(o, hi) | (int32_t)(r & 3u);
        }

        /* The right-hand spine link INSSPINEL writes through. */
        NODE(m, i)[FB - 2] = AT(m, lo);
        NODE(o, i)[FB - 2] = AT(o, lo);
    }

    ra = ibm_vproject(&m->state, AT(m, 1), AT(m, 0), AT(m, 2), f);
    rb = vproject(&o->state, AT(o, 1), AT(o, 0), AT(o, 2), f);
#undef NODE
#undef AT

    if (ra != rb)
        bad++;
END(vproject)


BEGIN(timing_tests)
    static const uint8_t sets[4] = {0, 1, 4, 8};
    delta_tpos pm, po;
    int8_t f = build_tspine(m, o);
    uint32_t which = rng_next() % 3u;
    int ra, rb;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    make_tpos(m, o, f, &pm, &po, sets[rng_next() % 4u]);

    if (which == 0) {
        ra = ibm_vmove_tv(&m->state, &pm);
        rb = vmove_tv(&o->state, &po);
    } else if (which == 1) {
        ra = ibm_vtstsnc_tv(&m->state, &pm);
        rb = vtstsnc_tv(&o->state, &po);
    } else {
        uint8_t back = (uint8_t)(rng_next() % 2u);

        ra = ibm_vtsttmark_tv(&m->state, &pm, back);
        rb = vtsttmark_tv(&o->state, &po, back);
    }

    if (ra != rb)
        bad++;
    if (pm.offset != po.offset || pm.flags != po.flags)
        bad++;
    if (pm.node - (int32_t)(intptr_t)m != po.node - (int32_t)(intptr_t)o)
        bad++;

    m->stack.spine_l = o->stack.spine_l = 0;
    m->stack.spine_r = o->stack.spine_r = 0;
END(timing_tests)

BEGIN(test_ptr)
    int ra, rb, i;

    build_chain(m, o);
    m->vars.fence_base = o->vars.fence_base = 13;
    m->vars.fence_count = o->vars.fence_count = (int8_t)(rng_next() % 3u);
    m->vars.scan_field = o->vars.scan_field = (uint8_t)(rng_next() % 3u);
    m->vars.scan_rev = o->vars.scan_rev = (uint8_t)(rng_next() % 2u);
    m->vars.scan_held = o->vars.scan_held = (uint8_t)(rng_next() % 2u);
    for (i = 0; i < 4; i++) {
        m->chars[i] = o->chars[i] = (uint8_t)i;
        m->marks[i] = o->marks[i] = (uint8_t)(rng_next() % 2u);
    }
    m->vars.scan_ptr = (int32_t)(intptr_t)m->nodes;
    o->vars.scan_ptr = (int32_t)(intptr_t)o->nodes;

    /* Aim the register at one of the nodes, and sometimes at nothing. The
       normalising path is left out: it would need the timing spine, which
       the scan chain is not. */
    {
        uint32_t k = rng_next() % 5u;

        m->state.lpta.node = (k < 4)
            ? (int32_t)(intptr_t)(m->nodes + k * 0x80) : 0;
        o->state.lpta.node = (k < 4)
            ? (int32_t)(intptr_t)(o->nodes + k * 0x80) : 0;
    }
    m->state.lpta.flags = o->state.lpta.flags = (uint8_t)(rng_next() % 2u);

    ra = ibm_test_ptr(&m->state);
    rb = test_ptr(&o->state);
    if (ra != rb)
        bad++;

    m->state.lpta.node = o->state.lpta.node = 0;
    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(test_ptr)


/* The register walks and the scan setters all settle the left register
   first, so they need the timing spine and a register aimed into it. */
static int8_t reg_setup(delta_world *m, delta_world *o)
{
    int8_t f = build_tspine(m, o);
    delta_tpos pm, po;

    if (f < 0)
        return -1;

    make_tpos(m, o, f, &pm, &po, (uint8_t)(rng_next() % 2u));
    m->state.lpta = pm;
    o->state.lpta = po;
    return f;
}

BEGIN(lpta_walks)
    int8_t f = reg_setup(m, o);
    uint32_t which = rng_next() % 3u;
    int ra = 0, rb = 0;

    if (f < 0) {
        free(m); free(o);
        continue;
    }

    if (which == 0) {
        ibm_lpta_movel(&m->state, (uint8_t)f);
        lpta_movel(&o->state, (uint8_t)f);
    } else if (which == 1) {
        ibm_lpta_mover(&m->state, (uint8_t)f);
        lpta_mover(&o->state, (uint8_t)f);
    } else {
        ra = ibm_lpta_tstmover(&m->state, (uint8_t)f);
        rb = lpta_tstmover(&o->state, (uint8_t)f);
    }

    if (ra != rb)
        bad++;

    m->stack.spine_l = o->stack.spine_l = 0;
    m->stack.spine_r = o->stack.spine_r = 0;
END(lpta_walks)

BEGIN(setscan)
    int8_t f = reg_setup(m, o);
    uint32_t which = rng_next() % 4u;
    int ra, rb;

    if (f < 0) {
        free(m); free(o);
        continue;
    }

    if (which == 0) {
        ra = ibm_setscan_l(&m->state, (uint8_t)f);
        rb = setscan_l(&o->state, (uint8_t)f);
    } else if (which == 1) {
        ra = ibm_setscan_r(&m->state, (uint8_t)f);
        rb = setscan_r(&o->state, (uint8_t)f);
    } else if (which == 2) {
        ra = ibm_setscan_nof_l(&m->state, (uint8_t)f);
        rb = setscan_nof_l(&o->state, (uint8_t)f);
    } else {
        ra = ibm_setscan_nof_r(&m->state, (uint8_t)f);
        rb = setscan_nof_r(&o->state, (uint8_t)f);
    }

    if (ra != rb)
        bad++;

    m->vars.scan_ptr = o->vars.scan_ptr = 0;
    m->stack.spine_l = o->stack.spine_l = 0;
    m->stack.spine_r = o->stack.spine_r = 0;
END(setscan)


BEGIN(vgetsc)
    /* ctxlook and ctxspine both need the spine ctxlook's own test builds,
       with the context links idle. */
    enum { FB = 15, NNODE = 6, STEP = 0x80 };
    uint8_t f = (uint8_t)(rng_next() % NSTMT);
    int32_t back = (int32_t)(rng_next() % 2u);
    int32_t ctx = (int32_t)(rng_next() % 2u);
    int32_t ra, rb;
    int i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = FB;
    m->state.fence_fill = o->state.fence_fill = (uint8_t)(rng_next() % 5u);
    m->vars.relink = o->vars.relink = (int32_t)(rng_next() % 2u);
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = (int8_t)(rng_next() % 2u);

#define NODE(w, i) ((int32_t *)((w)->nodes + (i) * STEP))
#define AT(w, i)   ((int32_t)(intptr_t)((w)->nodes + (i) * STEP))
    for (i = 0; i < NNODE; i++) {
        int lo = i > 0 ? i - 1 : 0;
        int hi = i + 1 < NNODE ? i + 1 : NNODE - 1;
        uint32_t r;

        r = rng_next();
        NODE(m, i)[0] = AT(m, lo) | 2 | (int32_t)(r & 1u);
        NODE(o, i)[0] = AT(o, lo) | 2 | (int32_t)(r & 1u);

        r = rng_next();
        NODE(m, i)[1] = AT(m, hi) | (int32_t)(r & 3u);
        NODE(o, i)[1] = AT(o, hi) | (int32_t)(r & 3u);

        r = rng_next();
        NODE(m, i)[2] = (int32_t)(r & 3u);
        NODE(o, i)[2] = (int32_t)(r & 3u);

        for (j = 0; j < 10; j++) {
            r = rng_next();
            NODE(m, i)[3 + j] = AT(m, lo) | (int32_t)(r & 1u);
            NODE(o, i)[3 + j] = AT(o, lo) | (int32_t)(r & 1u);

            r = rng_next();
            NODE(m, i)[FB + j] = AT(m, hi) | (int32_t)(r & 1u);
            NODE(o, i)[FB + j] = AT(o, hi) | (int32_t)(r & 1u);
        }

        NODE(m, i)[FB - 2] = AT(m, lo);
        NODE(o, i)[FB - 2] = AT(o, lo);
        NODE(m, i)[FB - 1] = 0;
        NODE(o, i)[FB - 1] = 0;
    }
    /* ctxspine only stops on a node that both carries the field and is
       sequential, so the far end has to be one. */
    NODE(m, NNODE - 1)[FB + f] |= 1;
    NODE(o, NNODE - 1)[FB + f] |= 1;
    NODE(m, NNODE - 1)[2] &= ~2;
    NODE(o, NNODE - 1)[2] &= ~2;
    NODE(m, 0)[FB + f] |= 1;
    NODE(o, 0)[FB + f] |= 1;
    NODE(m, 0)[2] &= ~2;
    NODE(o, 0)[2] &= ~2;

    m->stack.spine_l = AT(m, 0);
    o->stack.spine_l = AT(o, 0);
    m->stack.spine_r = AT(m, NNODE - 1);
    o->stack.spine_r = AT(o, NNODE - 1);

    ra = ibm_vgetsc(&m->state, back, ctx, AT(m, 2), f);
    rb = vgetsc(&o->state, back, ctx, AT(o, 2), f);
#undef NODE
#undef AT

    if ((ra == 0) != (rb == 0))
        bad++;
    else if (ra != 0
             && ra - (int32_t)(intptr_t)m != rb - (int32_t)(intptr_t)o)
        bad++;
END(vgetsc)

BEGIN(vtimept_tv)
    static const uint8_t sets[4] = {0, 1, 4, 8};
    delta_tpos pm, po;
    int8_t f = build_tspine(m, o);
    uint8_t back = (uint8_t)(rng_next() % 2u);
    int ra, rb;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    make_tpos(m, o, f, &pm, &po, sets[rng_next() % 4u]);

    ra = ibm_vtimept_tv(&m->state, &pm, back);
    rb = vtimept_tv(&o->state, &po, back);

    if (ra != rb)
        bad++;
    if (pm.offset != po.offset || pm.flags != po.flags)
        bad++;
    if (pm.node - (int32_t)(intptr_t)m != po.node - (int32_t)(intptr_t)o)
        bad++;
END(vtimept_tv)

BEGIN(for_loop_preamble)
    delta_token tm, to;
    int8_t f = build_tspine(m, o);
    int32_t tag = (int32_t)rng_next();
    int32_t loop = (int32_t)rng_next();
    int32_t which = (int32_t)(rng_next() % NSTMT);
    int ra, rb, i;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    for (i = 0; i < FENCE_MAP; i++)
        m->map[i] = o->map[i] = (uint8_t)(rng_next() % 4u);

    tm.unknown_00 = to.unknown_00 = (int32_t)rng_next();
    tm.value = (int32_t)(intptr_t)(m->nodes + 2 * 0x80);
    to.value = (int32_t)(intptr_t)(o->nodes + 2 * 0x80);
    m->state.lpta.field = o->state.lpta.field = f;

    ra = ibm_for_loop_preamble(&m->state, tag, loop, which, &tm);
    rb = for_loop_preamble(&o->state, tag, loop, which, &to);

    if (ra != rb)
        bad++;
END(for_loop_preamble)


BEGIN(dupsync)
    uint8_t back = (uint8_t)(rng_next() % 2u);
    int ra, rb;

    build_pspine(m, o);
    ra = ibm_dupsync(&m->state, (int32_t)(intptr_t)(m->nodes + 0x80),
                     (int32_t)(intptr_t)(m->nodes + 0x100), back);
    rb = dupsync(&o->state, (int32_t)(intptr_t)(o->nodes + 0x80),
                 (int32_t)(intptr_t)(o->nodes + 0x100), back);
    if (ra != rb)
        bad++;
END(dupsync)

BEGIN(vdef_proj)
    uint8_t f = (uint8_t)(rng_next() % NSTMT);
    int ra, rb;

    build_pspine(m, o);
    /* vgetsc with a context can reach ctxlook, which stops on a node that is
       both marked and sequential; give it one at each end. */
    ((int32_t *)(m->nodes))[15 + f] |= 1;
    ((int32_t *)(o->nodes))[15 + f] |= 1;
    ((int32_t *)(m->nodes))[2] &= ~2;
    ((int32_t *)(o->nodes))[2] &= ~2;
    ((int32_t *)(m->nodes + 3 * 0x80))[15 + f] |= 1;
    ((int32_t *)(o->nodes + 3 * 0x80))[15 + f] |= 1;
    ((int32_t *)(m->nodes + 3 * 0x80))[2] &= ~2;
    ((int32_t *)(o->nodes + 3 * 0x80))[2] &= ~2;

    ra = ibm_vdef_proj(&m->state, (int32_t)(intptr_t)(m->nodes + 0x80), f);
    rb = vdef_proj(&o->state, (int32_t)(intptr_t)(o->nodes + 0x80), f);
    if (ra != rb)
        bad++;
END(vdef_proj)

BEGIN(vprt_range)
    static const uint8_t sets[4] = {0, 1, 2, 4};
    delta_tpos am, ao, bm, bo;
    int8_t f = build_tspine(m, o);
    int ra, rb;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    make_tpos(m, o, f, &am, &ao, sets[rng_next() % 4u]);
    make_tpos(m, o, f, &bm, &bo, sets[rng_next() % 4u]);

    ra = ibm_vprt_range(&m->state, &am, &bm);
    rb = vprt_range(&o->state, &ao, &bo);

    if (ra != rb)
        bad++;
    if (am.offset != ao.offset || am.flags != ao.flags)
        bad++;
    if (bm.offset != bo.offset || bm.flags != bo.flags)
        bad++;
    if (am.node - (int32_t)(intptr_t)m != ao.node - (int32_t)(intptr_t)o)
        bad++;
    if (bm.node - (int32_t)(intptr_t)m != bo.node - (int32_t)(intptr_t)o)
        bad++;
END(vprt_range)

BEGIN(forto_adv_r)
    delta_token tm, to, em, eo;
    int8_t f = build_tspine(m, o);
    int16_t tag = (int16_t)rng_next();
    int16_t loop = (int16_t)rng_next();
    int16_t bound = (int16_t)rng_next();
    int32_t which = (int32_t)(rng_next() % NSTMT);
    int ra, rb, i;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    for (i = 0; i < FENCE_MAP; i++)
        m->map[i] = o->map[i] = (uint8_t)(rng_next() % 4u);
    for (i = 0; i < 4; i++) {
        m->chars[i] = o->chars[i] = (uint8_t)i;
        m->marks[i] = o->marks[i] = (uint8_t)(rng_next() % 2u);
    }
    m->vars.fence_count = o->vars.fence_count = (int8_t)(rng_next() % 3u);

    tm.unknown_00 = to.unknown_00 = (int32_t)rng_next();
    tm.value = (int32_t)(intptr_t)(m->nodes + 2 * 0x80);
    to.value = (int32_t)(intptr_t)(o->nodes + 2 * 0x80);
    em.unknown_00 = eo.unknown_00 = (int32_t)rng_next();
    {
        uint32_t k = rng_next() % 6u;

        em.value = (int32_t)(intptr_t)(m->nodes + k * 0x80);
        eo.value = (int32_t)(intptr_t)(o->nodes + k * 0x80);
    }
    m->state.lpta.field = o->state.lpta.field = f;

    ra = ibm_forto_adv_r(&m->state, tag, loop, bound, (uint8_t)which,
                         &tm, &em);
    rb = forto_adv_r(&o->state, tag, loop, bound, (uint8_t)which, &to, &eo);

    if (ra != rb)
        bad++;
    if (tm.value - (int32_t)(intptr_t)m != to.value - (int32_t)(intptr_t)o)
        bad++;
END(forto_adv_r)


BEGIN(forto_adv_upto_r)
    delta_token tm, to, em, eo;
    int8_t f = build_tspine(m, o);
    int16_t tag = (int16_t)rng_next();
    int16_t loop = (int16_t)rng_next();
    int16_t bound = (int16_t)rng_next();
    int32_t which = (int32_t)(rng_next() % NSTMT);
    int ra, rb, i;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    for (i = 0; i < FENCE_MAP; i++)
        m->map[i] = o->map[i] = (uint8_t)(rng_next() % 4u);
    for (i = 0; i < 4; i++) {
        m->chars[i] = o->chars[i] = (uint8_t)i;
        m->marks[i] = o->marks[i] = (uint8_t)(rng_next() % 2u);
    }
    m->vars.fence_count = o->vars.fence_count = (int8_t)(rng_next() % 3u);

    tm.unknown_00 = to.unknown_00 = (int32_t)rng_next();
    tm.value = (int32_t)(intptr_t)(m->nodes + 2 * 0x80);
    to.value = (int32_t)(intptr_t)(o->nodes + 2 * 0x80);
    em.unknown_00 = eo.unknown_00 = (int32_t)rng_next();
    {
        uint32_t k = rng_next() % 6u;

        em.value = (int32_t)(intptr_t)(m->nodes + k * 0x80);
        eo.value = (int32_t)(intptr_t)(o->nodes + k * 0x80);
    }
    m->state.lpta.field = o->state.lpta.field = f;

    ra = ibm_forto_adv_upto_r(&m->state, tag, loop, bound, (uint8_t)which,
                              &tm, &em);
    rb = forto_adv_upto_r(&o->state, tag, loop, bound, (uint8_t)which,
                          &to, &eo);

    if (ra != rb)
        bad++;
    if (tm.value - (int32_t)(intptr_t)m != to.value - (int32_t)(intptr_t)o)
        bad++;
END(forto_adv_upto_r)

/* setd_lookup backtracks a span it cannot settle, so each side needs a place
   to land. The lookup itself is not part of the runtime and is stubbed, so
   what this compares is the guard, the range settling and the descriptor the
   set index picks out. */
static int lookup_ibm(delta_state *d, int32_t arg, int16_t set)
{
    jmp_buf jb;
    int r = -1;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0)
        r = ibm_setd_lookup(d, arg, set);
    d->vars->err_jmp = 0;
    return r;
}

static int lookup_ours(delta_state *d, int32_t arg, int16_t set)
{
    jmp_buf jb;
    int r = -1;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0)
        r = setd_lookup(d, arg, set);
    d->vars->err_jmp = 0;
    return r;
}

BEGIN(setd_lookup)
    static const uint8_t sets[4] = {0, 1, 2, 4};
    delta_tpos am, ao, bm, bo;
    int8_t f = build_tspine(m, o);
    int16_t which = (int16_t)(rng_next() % 8u);
    int32_t arg = (int32_t)rng_next();
    int ra, rb;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    make_tpos(m, o, f, &am, &ao, sets[rng_next() % 4u]);
    make_tpos(m, o, f, &bm, &bo, sets[rng_next() % 4u]);

    /* Sometimes leave a register empty, which is the early way out. */
    if (rng_next() % 4u == 0)
        am.node = ao.node = 0;
    if (rng_next() % 4u == 0)
        bm.node = bo.node = 0;

    m->state.lpta = am; m->state.rpta = bm;
    o->state.lpta = ao; o->state.rpta = bo;

    ra = lookup_ibm(&m->state, arg, which);
    rb = lookup_ours(&o->state, arg, which);
    if (ra != rb)
        bad++;
END(setd_lookup)


BEGIN(vmark)
    /* The write goes through the language's own field setter, so the field
       index has to be one the statement type declares, and every link the
       walk follows has to be real: the original hands a null straight to the
       setter. */
    uint8_t st = (uint8_t)(rng_next() % NSTMT);
    uint8_t fld = (uint8_t)(rng_next() % (uint32_t)vstmtbl[st].nfields);
    uint8_t value = (uint8_t)(rng_next() % 4u);
    int ra, rb;

    build_pspine(m, o);
    {
        uint32_t k = rng_next() % 5u;

        ra = ibm_vmark(&m->state, st, fld,
                       (int32_t)(intptr_t)(m->nodes),
                       (k < 4) ? (int32_t)(intptr_t)(m->nodes + k * 0x80) : 0,
                       &value);
        rb = vmark(&o->state, st, fld,
                   (int32_t)(intptr_t)(o->nodes),
                   (k < 4) ? (int32_t)(intptr_t)(o->nodes + k * 0x80) : 0,
                   &value);
    }
    if (ra != rb)
        bad++;

    /* The parked field address is a stack address, which no two runs share. */
    m->stack.mark_fld = o->stack.mark_fld = NULL;
END(vmark)


BEGIN(visleft)
    /* Both the remembered path and the field-by-field one, on the same four
       node spine the projection primitives use. The cache lives in the stack
       block, so filling it in one world and not the other would show up. */
    int32_t ra, rb;
    uint32_t ia, ib;
    int i;

    build_pspine(m, o);


    /* Both of visleft's walks run off the end of the spine to answer no, so
       the ends have to be null here rather than pointing at themselves. */
    for (i = 0; i < 10; i++) {
        ((int32_t *)m->nodes)[3 + i] = 0;
        ((int32_t *)o->nodes)[3 + i] = 0;
    }
    ((int32_t *)(m->nodes + 3 * 0x80))[1] = 0;
    ((int32_t *)(o->nodes + 3 * 0x80))[1] = 0;

    /* Without a field both nodes carry, visleft falls through to vgetsc and
       hands its null straight to a dereference. Give it one. */
    m->state.fence_fill = o->state.fence_fill = (uint8_t)(1u + rng_next() % 4u);
    {
        int k = (int)(rng_next() % m->state.fence_fill);
        int n;

        for (n = 0; n < 4; n++) {
            ((int32_t *)(m->nodes + n * 0x80))[15 + k] |= 1;
            ((int32_t *)(o->nodes + n * 0x80))[15 + k] |= 1;
        }
    }

    for (i = 0; i < 50; i++) {
        uint32_t r = rng_next();

        m->stack.left_a[i] = o->stack.left_a[i] = 0;
        m->stack.left_b[i] = o->stack.left_b[i] = 0;
        m->stack.left_ans[i] = o->stack.left_ans[i] = (int32_t)(r & 1u);
        m->stack.left_hits[i] = o->stack.left_hits[i] =
            (int32_t)((r >> 8) % 20u);
    }
    m->stack.left_next = o->stack.left_next = (int32_t)(rng_next() % 50u);
    /* Half the time the table is current, half the time it is stale. */
    m->stack.left_stamp = o->stack.left_stamp =
        (rng_next() % 2u) ? spine_changed : spine_changed + 1;

    ia = rng_next() % 4u;
    ib = rng_next() % 4u;

    /* Seed one entry so the hit path gets used too. */
    if (rng_next() % 2u) {
        int32_t k = m->stack.left_next;

        m->stack.left_a[k] = (int32_t)(intptr_t)(m->nodes + ia * 0x80);
        o->stack.left_a[k] = (int32_t)(intptr_t)(o->nodes + ia * 0x80);
        m->stack.left_b[k] = (int32_t)(intptr_t)(m->nodes + ib * 0x80);
        o->stack.left_b[k] = (int32_t)(intptr_t)(o->nodes + ib * 0x80);
    }

    if (rng_next() % 2u) {
        ra = ibm_visleft(&m->state, (int32_t)(intptr_t)(m->nodes + ia * 0x80),
                         (int32_t)(intptr_t)(m->nodes + ib * 0x80));
        rb = visleft(&o->state, (int32_t)(intptr_t)(o->nodes + ia * 0x80),
                     (int32_t)(intptr_t)(o->nodes + ib * 0x80));
    } else {
        ra = ibm_visright(&m->state, (int32_t)(intptr_t)(m->nodes + ia * 0x80),
                          (int32_t)(intptr_t)(m->nodes + ib * 0x80));
        rb = visright(&o->state, (int32_t)(intptr_t)(o->nodes + ia * 0x80),
                      (int32_t)(intptr_t)(o->nodes + ib * 0x80));
    }
    if (ra != rb)
        bad++;

    /* The table holds node addresses; compare each as an offset. */
    for (i = 0; i < 50; i++) {
        if ((m->stack.left_a[i] == 0) != (o->stack.left_a[i] == 0))
            bad++;
        else if (m->stack.left_a[i] != 0
                 && m->stack.left_a[i] - (int32_t)(intptr_t)m
                    != o->stack.left_a[i] - (int32_t)(intptr_t)o)
            bad++;
        if ((m->stack.left_b[i] == 0) != (o->stack.left_b[i] == 0))
            bad++;
        else if (m->stack.left_b[i] != 0
                 && m->stack.left_b[i] - (int32_t)(intptr_t)m
                    != o->stack.left_b[i] - (int32_t)(intptr_t)o)
            bad++;
        m->stack.left_a[i] = o->stack.left_a[i] = 0;
        m->stack.left_b[i] = o->stack.left_b[i] = 0;
    }
END(visleft)


/* A whole Delta heap inside the world, so every address the harness compares
   is an offset. Three segments live and three on the free list, which keeps
   the free count under ten: every path except the two that reach the system
   allocator then runs without ever calling it, and those two are the layer
   this port deliberately supplies itself. */
static void build_heap(delta_world *m, delta_world *o)
{
    int i;

    for (i = 0; i < NSEG; i++) {
        delta_seg *a = &m->segs[i];
        delta_seg *b = &o->segs[i];
        int32_t used;

        a->block = (uint8_t *)m->heapmem[i];
        b->block = (uint8_t *)o->heapmem[i];
        a->end = a->block + SEGBYTES - 1;
        b->end = b->block + SEGBYTES - 1;

        used = (int32_t)(intptr_t)a->end & 3;
        if (((int32_t)(intptr_t)a->end & 7) == 0)
            used += 4;
        used += (int32_t)(rng_next() % 0x40u) * 8;
        a->used = b->used = used;
        a->live = b->live = (int32_t)(1u + rng_next() % 3u);
        a->prev = b->prev = NULL;
        a->next = b->next = NULL;
    }

    for (i = 0; i < 3; i++) {
        m->segs[i].next = (i < 2) ? &m->segs[i + 1] : NULL;
        o->segs[i].next = (i < 2) ? &o->segs[i + 1] : NULL;
        m->segs[i].prev = (i > 0) ? &m->segs[i - 1] : NULL;
        o->segs[i].prev = (i > 0) ? &o->segs[i - 1] : NULL;
    }
    for (i = 3; i < NSEG; i++) {
        m->segs[i].next = (i + 1 < NSEG) ? &m->segs[i + 1] : NULL;
        o->segs[i].next = (i + 1 < NSEG) ? &o->segs[i + 1] : NULL;
    }

    m->stack.heap_first = &m->segs[0];
    o->stack.heap_first = &o->segs[0];
    m->stack.heap_cur = &m->segs[2];
    o->stack.heap_cur = &o->segs[2];
    m->stack.free_segs = &m->segs[3];
    o->stack.free_segs = &o->segs[3];
    m->stack.free_count = o->stack.free_count = 3;
    m->stack.seg_size = o->stack.seg_size = SEGBYTES;
    m->stack.sync_size = o->stack.sync_size = 0x20;

    for (i = 0; i < DELTA_MARKS; i++) {
        m->stack.marks[i].unused = o->stack.marks[i].unused = 1;
        m->stack.marks[i].pos = NULL;
        o->stack.marks[i].pos = NULL;
        m->stack.marks[i].seg = NULL;
        o->stack.marks[i].seg = NULL;
        m->stack.marks[i].used = o->stack.marks[i].used = 0;
        m->stack.marks[i].live = o->stack.marks[i].live = 0;
    }
}

/* Point a pointer at a place in a segment with that segment stamped in front
   of it, which is the shape allocDeltaHeapObject hands back. */
static void stamp_object(delta_world *w, int seg, int32_t off, uint8_t **out)
{
    uint8_t *p = (uint8_t *)w->heapmem[seg] + off;

    *(delta_seg **)p = &w->segs[seg];
    *out = p + 4;
}

BEGIN(heap_record)
    int ra, rb, i;

    build_heap(m, o);
    /* Use up some of the ten so the search has to skip. */
    for (i = 0; i < DELTA_MARKS; i++) {
        int32_t taken = (int32_t)(rng_next() % 2u);

        m->stack.marks[i].unused = o->stack.marks[i].unused = 1 - taken;
    }

    ra = ibm_recordDeltaHeapPos(&m->state);
    rb = recordDeltaHeapPos(&o->state);
    if (ra != rb)
        bad++;
END(heap_record)

BEGIN(heap_segnum)
    int32_t ra, rb;
    uint8_t *pm, *po;
    int32_t unit = (int32_t)(1u + rng_next() % 8u);
    int seg = (int)(rng_next() % NSEG);
    int32_t off = (int32_t)((rng_next() % 0x18u) * 8u + 8u);

    build_heap(m, o);
    stamp_object(m, seg, off, &pm);
    stamp_object(o, seg, off, &po);

    ra = ibm_getDeltaHeapSegNumber(&m->state, pm, unit);
    rb = getDeltaHeapSegNumber(&o->state, po, unit);
    if (ra != rb)
        bad++;
END(heap_segnum)

BEGIN(heap_alloc)
    void *ra, *rb;
    int32_t size = (int32_t)(rng_next() % 0x60u);

    build_heap(m, o);
    ra = ibm_allocDeltaHeapObject(&m->state, size);
    rb = allocDeltaHeapObject(&o->state, size);

    if ((ra == NULL) != (rb == NULL))
        bad++;
    else if (ra != NULL
             && (char *)ra - (char *)m != (char *)rb - (char *)o)
        bad++;
END(heap_alloc)

BEGIN(heap_free)
    uint8_t *pm, *po;
    /* Never segment zero: freeing the first one unlinks through a null. */
    int seg = (int)(1u + rng_next() % 2u);
    int32_t off = (int32_t)((rng_next() % 0x18u) * 8u + 8u);

    build_heap(m, o);
    m->segs[seg].live = o->segs[seg].live = (int32_t)(1u + rng_next() % 2u);
    stamp_object(m, seg, off, &pm);
    stamp_object(o, seg, off, &po);

    switch (rng_next() % 4u) {
    case 3:
        ibm_delsync(&m->state, pm);
        delsync(&o->state, po);
        break;
    case 0:
        ibm_freeDeltaHeapObject(&m->state, pm);
        freeDeltaHeapObject(&o->state, po);
        break;
    case 1:
        ibm_free_heap(&m->state, pm);
        free_heap(&o->state, po);
        break;
    default:
        ibm_cacheDeletedDeltaObject(&m->state, pm);
        cacheDeletedDeltaObject(&o->state, po);
        break;
    }
END(heap_free)

BEGIN(heap_rewind)
    int32_t release = (int32_t)(rng_next() % 2u);
    int slot = (int)(rng_next() % DELTA_MARKS);
    uint8_t *pos;

    build_heap(m, o);
    /* Aim the mark at the segment being filled: unwinding past a segment is
       the one path that gives memory back to the system. */
    m->stack.marks[slot].unused = o->stack.marks[slot].unused = 0;
    m->stack.marks[slot].seg = m->stack.heap_cur;
    o->stack.marks[slot].seg = o->stack.heap_cur;
    m->stack.marks[slot].used = o->stack.marks[slot].used =
        (int32_t)(rng_next() % 0x40u) * 8;
    m->stack.marks[slot].live = o->stack.marks[slot].live =
        (int32_t)(rng_next() % 4u);
    m->stack.marks[slot].pos =
        m->stack.heap_cur->end - m->stack.marks[slot].used;
    o->stack.marks[slot].pos =
        o->stack.heap_cur->end - o->stack.marks[slot].used;

    pos = (rng_next() % 4u == 0) ? NULL : m->stack.marks[slot].pos;
    ibm_freeDeltaHeapTo(&m->state, pos, release);
    freeDeltaHeapTo(&o->state,
                    pos ? o->stack.marks[slot].pos : NULL, release);
END(heap_rewind)

BEGIN(heap_objects)
    void *ra, *rb;

    build_heap(m, o);
    if (rng_next() % 2u) {
        const delta_stmt *e = &vstmtbl[rng_next() % NSTMT];

        ra = ibm_alloc_tok(&m->state, e);
        rb = alloc_tok(&o->state, e);
    } else {
        ra = ibm_alloc_sync(&m->state);
        rb = alloc_sync(&o->state);
    }

    if ((ra == NULL) != (rb == NULL))
        bad++;
    else if (ra != NULL
             && (char *)ra - (char *)m != (char *)rb - (char *)o)
        bad++;
END(heap_objects)


BEGIN(vcomp_pta)
    static const uint8_t sets[4] = {0, 1, 2, 3};
    delta_tpos am, ao, bm, bo;
    int8_t f = build_tspine(m, o);
    int ra, rb, i;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    /* visleft's remembered path walks the forward links, and on the timing
       spine those close a loop between the last node and its terminator.
       That path has its own test; here visleft takes the field walk. */
    m->vars.relink = o->vars.relink = 0;
    m->state.fence_fill = o->state.fence_fill = (uint8_t)(1u + rng_next() % 4u);
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = (int8_t)(rng_next() % 2u);

    /* Without a field every node carries, visleft falls through to vgetsc and
       hands its null straight to a dereference. */
    {
        int k = (int)(rng_next() % m->state.fence_fill);
        int n;

        for (n = 0; n < 8; n++) {
            ((int32_t *)(m->nodes + n * 0x80))[15 + k] |= 1;
            ((int32_t *)(o->nodes + n * 0x80))[15 + k] |= 1;
        }
    }
    for (i = 0; i < 50; i++) {
        m->stack.left_a[i] = o->stack.left_a[i] = 0;
        m->stack.left_b[i] = o->stack.left_b[i] = 0;
        m->stack.left_ans[i] = o->stack.left_ans[i] = 0;
        m->stack.left_hits[i] = o->stack.left_hits[i] = 0;
    }
    m->stack.left_next = o->stack.left_next = 0;
    m->stack.left_stamp = o->stack.left_stamp = spine_changed;

    make_tpos(m, o, f, &am, &ao, sets[rng_next() % 4u]);
    make_tpos(m, o, f, &bm, &bo, sets[rng_next() % 4u]);

    if (rng_next() % 2u) {
        ra = ibm_vcomp_pta(&m->state, &am, &bm);
        rb = vcomp_pta(&o->state, &ao, &bo);
    } else {
        /* compare_ptas is the same thing on the two registers. */
        m->state.lpta = am; m->state.rpta = bm;
        o->state.lpta = ao; o->state.rpta = bo;
        ra = ibm_compare_ptas(&m->state);
        rb = compare_ptas(&o->state);
        am = m->state.lpta; bm = m->state.rpta;
        ao = o->state.lpta; bo = o->state.rpta;
    }

    if (ra != rb)
        bad++;
    if (am.offset != ao.offset || am.flags != ao.flags)
        bad++;
    if (bm.offset != bo.offset || bm.flags != bo.flags)
        bad++;
    if (am.node - (int32_t)(intptr_t)m != ao.node - (int32_t)(intptr_t)o)
        bad++;
    if (bm.node - (int32_t)(intptr_t)m != bo.node - (int32_t)(intptr_t)o)
        bad++;

    for (i = 0; i < 50; i++) {
        m->stack.left_a[i] = o->stack.left_a[i] = 0;
        m->stack.left_b[i] = o->stack.left_b[i] = 0;
    }
END(vcomp_pta)


BEGIN(mashtoks)
    /* Only a walkable statement type gets past the second guard, and only Ms
       is walkable, so most draws take the short way out and a fixed share
       goes all the way through. Neither neighbour may be a sync, hence the
       cleared bits. */
    uint8_t f = (rng_next() % 2u) ? 9 : (uint8_t)(rng_next() % NSTMT);
    int32_t t;
    int ra, rb, i;

    build_pspine(m, o);
    build_heap(m, o);
    for (i = 0; i < 4; i++) {
        ((int32_t *)(m->nodes + i * 0x80))[0] &= ~2;
        ((int32_t *)(o->nodes + i * 0x80))[0] &= ~2;
    }
    /* The merged statement goes back to the heap, so it has to look like an
       object that came from one. */
    for (i = 0; i < 4; i++) {
        *(delta_seg **)(m->nodes + i * 0x80 - 4 + 0x80) = &m->segs[1];
        *(delta_seg **)(o->nodes + i * 0x80 - 4 + 0x80) = &o->segs[1];
    }
    m->segs[1].live = o->segs[1].live = 8;

    t = (int32_t)(intptr_t)(m->nodes + 0x80);
    ra = ibm_mashtoks(&m->state, f, t);
    rb = mashtoks(&o->state, f, (int32_t)(intptr_t)(o->nodes + 0x80));
    if (ra != rb)
        bad++;
END(mashtoks)


BEGIN(vchkseqbad)
    uint8_t f = (uint8_t)(rng_next() % NSTMT);
    int ra, rb;

    build_pspine(m, o);
    ra = ibm_vchkseqbad(&m->state, (int32_t)(intptr_t)(m->nodes + 0x80), f,
                        "insertion");
    rb = vchkseqbad(&o->state, (int32_t)(intptr_t)(o->nodes + 0x80), f,
                    "insertion");
    if (ra != rb)
        bad++;
END(vchkseqbad)


BEGIN(vins_sync)
    /* Four nodes with the right-hand spine link pointing forward, so the span
       walk between the two neighbours reaches its end. Every node is a sync,
       which is the case where the new one is linked through the field. */
    enum { FB = 15, NNODE = 4, STEP = 0x80 };
    uint8_t f = (uint8_t)(rng_next() % NSTMT);
    void *ra, *rb;
    int i, j;

    build_pspine(m, o);
    build_heap(m, o);
    m->stack.sync_size = o->stack.sync_size = 0x80;
    for (i = 0; i < NSEG; i++) {
        int32_t used = (int32_t)(intptr_t)m->segs[i].end & 3;

        if (((int32_t)(intptr_t)m->segs[i].end & 7) == 0)
            used += 4;
        m->segs[i].used = o->segs[i].used = used;
    }

    for (i = 0; i < NNODE; i++) {
        int hi = i + 1 < NNODE ? i + 1 : NNODE - 1;

        ((int32_t *)(m->nodes + i * STEP))[0] |= 2;
        ((int32_t *)(o->nodes + i * STEP))[0] |= 2;
        ((int32_t *)(m->nodes + i * STEP))[FB - 2] =
            (int32_t)(intptr_t)(m->nodes + hi * STEP);
        ((int32_t *)(o->nodes + i * STEP))[FB - 2] =
            (int32_t)(intptr_t)(o->nodes + hi * STEP);
        for (j = 0; j < 10; j++) {
            uint32_t r = rng_next();

            ((int32_t *)(m->nodes + i * STEP))[FB + j] =
                (int32_t)((intptr_t)(m->nodes + hi * STEP) | (r & 3u));
            ((int32_t *)(o->nodes + i * STEP))[FB + j] =
                (int32_t)((intptr_t)(o->nodes + hi * STEP) | (r & 3u));
        }
    }

    ra = ibm_vins_sync(&m->state, f,
                       (int32_t)(intptr_t)m->nodes,
                       (int32_t)(intptr_t)(m->nodes + 2 * STEP));
    rb = vins_sync(&o->state, f,
                   (int32_t)(intptr_t)o->nodes,
                   (int32_t)(intptr_t)(o->nodes + 2 * STEP));

    if ((ra == NULL) != (rb == NULL))
        bad++;
    else if (ra != NULL
             && (char *)ra - (char *)m != (char *)rb - (char *)o)
        bad++;
END(vins_sync)


BEGIN(chkdelnonseq)
    /* Four nodes. The statement is the second, so the neighbour walks have
       somewhere to stop; the two outer nodes carry every field and are
       sequential, which is what lets seqscan and the closing walk reach an
       end. Every node carries at least one field, or the peer seqscan reads
       would nominate nothing and it would never stop. */
    enum { FB = 15, NNODE = 4, STEP = 0x80, NFIELD = 4 };
    uint8_t f = (uint8_t)(rng_next() % NFIELD);
    int ra, rb, i, j;

    build_pspine(m, o);
    m->state.fence_fill = o->state.fence_fill = NFIELD;
    m->vars.relink = o->vars.relink = (int32_t)(rng_next() % 2u);
    m->vars.ctx_both = o->vars.ctx_both = (int32_t)(rng_next() % 2u);

    for (i = 0; i < NNODE; i++) {
        int outer = (i == 0 || i == NNODE - 1);
        uint32_t bits = outer ? 0xfu : (1u + rng_next() % 15u);

        for (j = 0; j < NFIELD; j++) {
            int32_t *a = &((int32_t *)(m->nodes + i * STEP))[FB + j];
            int32_t *b = &((int32_t *)(o->nodes + i * STEP))[FB + j];

            if ((bits >> j) & 1u) {
                *a |= 1;
                *b |= 1;
            } else {
                *a &= ~1;
                *b &= ~1;
            }
        }

        /* The outer two have to be sequential or the neighbour walks that
           look for the first sequential node never finish. */
        if (outer) {
            ((int32_t *)(m->nodes + i * STEP))[2] &= ~2;
            ((int32_t *)(o->nodes + i * STEP))[2] &= ~2;
        }
    }

    /* The statement itself must count as neither a lone statement nor wholly
       nonsequential, so the flag that picks the closing walk is set. */
    ((int32_t *)(m->nodes + STEP))[1] &= ~3;
    ((int32_t *)(o->nodes + STEP))[1] &= ~3;

    for (i = 0; i < 3; i++) {
        m->stack.runs[i].kind = o->stack.runs[i].kind = 0;
        m->stack.runs[i].pad_01[0] = o->stack.runs[i].pad_01[0] = 0;
        m->stack.runs[i].pad_01[1] = o->stack.runs[i].pad_01[1] = 0;
        m->stack.runs[i].pad_01[2] = o->stack.runs[i].pad_01[2] = 0;
        m->stack.runs[i].flag = o->stack.runs[i].flag = 0;
        m->stack.runs[i].start = o->stack.runs[i].start = 0;
        m->stack.runs[i].cur = o->stack.runs[i].cur = 0;
    }

    ra = ibm_chkdelnonseq(&m->state, (int32_t)(intptr_t)(m->nodes + STEP), f);
    rb = chkdelnonseq(&o->state, (int32_t)(intptr_t)(o->nodes + STEP), f);
    if (ra != rb)
        bad++;
END(chkdelnonseq)


BEGIN(fdeldel)
    /* Six nodes so the run has somewhere to start and stop with neighbours
       left over on each side. Every node carries every field, which makes
       chkdelnonseq refuse early and so keeps its scans out of this test;
       what is being compared here is the unlinking and the rejoining. */
    enum { FB = 15, NNODE = 4, STEP = 0x80, NFIELD = 4 };
    int8_t fd = (int8_t)(rng_next() % NFIELD);
    int ra, rb, i, j;

    build_pspine(m, o);
    build_heap(m, o);
    m->state.fence_fill = o->state.fence_fill = NFIELD;
    m->vars.relink = o->vars.relink = (int32_t)(rng_next() % 2u);
    m->vars.ctx_both = o->vars.ctx_both = (int32_t)(rng_next() % 2u);
    m->stack.del_field = o->stack.del_field = fd;
    m->nsqf[0] = o->nsqf[0] = -1;

    for (i = 0; i < NNODE; i++) {
        for (j = 0; j < NFIELD; j++) {
            ((int32_t *)(m->nodes + i * STEP))[FB + j] |= 1;
            ((int32_t *)(o->nodes + i * STEP))[FB + j] |= 1;
        }
        ((int32_t *)(m->nodes + i * STEP))[2] &= ~2;
        ((int32_t *)(o->nodes + i * STEP))[2] &= ~2;
        /* Anything that goes back to the heap has to look like it came from
           one, so stamp the segment in the four bytes in front. Not the
           first node: those four bytes are outside the array. */
        if (i > 0) {
            *(delta_seg **)(m->nodes + i * STEP - 4) = &m->segs[1];
            *(delta_seg **)(o->nodes + i * STEP - 4) = &o->segs[1];
        }
    }
    m->segs[1].live = o->segs[1].live = 16;

    {
        int32_t arg = (int32_t)rng_next();

        if (rng_next() % 2u) {
            ra = ibm_fdeldel(&m->state, (int32_t)(intptr_t)(m->nodes + STEP),
                             (int32_t)(intptr_t)(m->nodes + 2 * STEP), arg);
            rb = fdeldel(&o->state, (int32_t)(intptr_t)(o->nodes + STEP),
                         (int32_t)(intptr_t)(o->nodes + 2 * STEP), arg);
        } else {
            /* fdel takes the same run out of the stack block. A whole delete
               names the two ends; a partial one steps in from each. */
            int32_t whole = (int32_t)(rng_next() % 2u);

            m->stack.del_from = (int32_t)(intptr_t)(m->nodes + STEP);
            o->stack.del_from = (int32_t)(intptr_t)(o->nodes + STEP);
            m->stack.del_to = (int32_t)(intptr_t)(m->nodes + 2 * STEP);
            o->stack.del_to = (int32_t)(intptr_t)(o->nodes + 2 * STEP);
            m->stack.del_left = (int32_t)(intptr_t)m->nodes;
            o->stack.del_left = (int32_t)(intptr_t)o->nodes;
            m->stack.del_right = (int32_t)(intptr_t)(m->nodes + 3 * STEP);
            o->stack.del_right = (int32_t)(intptr_t)(o->nodes + 3 * STEP);

            ibm_fdel(&m->state, whole, arg);
            fdel(&o->state, whole, arg);
            ra = rb = 0;

            /* The two entry points that fill the block in for themselves. */
            if (rng_next() % 2u) {
                ra = ibm_vdel_1pt(&m->state, (uint8_t)fd,
                                  (int32_t)(intptr_t)(m->nodes + STEP), arg);
                rb = vdel_1pt(&o->state, (uint8_t)fd,
                              (int32_t)(intptr_t)(o->nodes + STEP), arg);
            } else {
                ra = ibm_vdel_2pt(&m->state, (uint8_t)fd,
                                  (int32_t)(intptr_t)m->nodes,
                                  (int32_t)(intptr_t)(m->nodes + 3 * STEP));
                rb = vdel_2pt(&o->state, (uint8_t)fd,
                              (int32_t)(intptr_t)o->nodes,
                              (int32_t)(intptr_t)(o->nodes + 3 * STEP));
            }

            m->stack.del_from = o->stack.del_from = 0;
            m->stack.del_to = o->stack.del_to = 0;
            m->stack.del_left = o->stack.del_left = 0;
            m->stack.del_right = o->stack.del_right = 0;
        }
    }
    if (ra != rb)
        bad++;
END(fdeldel)


BEGIN(vins_tok)
    /* The same four-node spine and heap the deletions use, since an insert
       between two nodes that are not already neighbours deletes first. */
    enum { FB = 15, NNODE = 4, STEP = 0x80, NFIELD = 4 };
    uint8_t f = (uint8_t)(rng_next() % NSTMT);
    int8_t fd = (int8_t)(rng_next() % NFIELD);
    int wide = (int)(rng_next() % 2u);
    delta_operand vm, vo;
    int ra, rb, i, j;

    build_pspine(m, o);
    build_heap(m, o);
    m->state.fence_fill = o->state.fence_fill = NFIELD;
    m->vars.relink = o->vars.relink = (int32_t)(rng_next() % 2u);
    m->vars.ctx_both = o->vars.ctx_both = (int32_t)(rng_next() % 2u);
    m->stack.del_field = o->stack.del_field = fd;
    m->nsqf[0] = o->nsqf[0] = -1;

    for (i = 0; i < NNODE; i++) {
        /* Every field of every statement type, not just the fenced four: a
           context lookup walks until it finds a node carrying the one it was
           asked about, and never stops if nothing does. */
        for (j = 0; j < NSTMT; j++) {
            ((int32_t *)(m->nodes + i * STEP))[FB + j] |= 1;
            ((int32_t *)(o->nodes + i * STEP))[FB + j] |= 1;
        }
        ((int32_t *)(m->nodes + i * STEP))[2] &= ~2;
        ((int32_t *)(o->nodes + i * STEP))[2] &= ~2;
        if (i > 0) {
            *(delta_seg **)(m->nodes + i * STEP - 4) = &m->segs[1];
            *(delta_seg **)(o->nodes + i * STEP - 4) = &o->segs[1];
        }
    }
    m->segs[1].live = o->segs[1].live = 16;

    /* Either a record of the language's own type, copied whole, or something
       narrower that gets laid down field by field. */
    vm.kind = vo.kind = wide ? (int16_t)f : (int16_t)-1;
    vm.flag = vo.flag = 0;
    vm.pad_07 = vo.pad_07 = 0;
    fill(m->nodes + 0x300, 0x40);
    memcpy(o->nodes + 0x300, m->nodes + 0x300, 0x40);
    vm.ptr = m->nodes + 0x300;
    vo.ptr = o->nodes + 0x300;

    if (rng_next() % 2u) {
        ra = ibm_vins_tok(&m->state, f, (int32_t)(intptr_t)(m->nodes + STEP),
                          (int32_t)(intptr_t)(m->nodes + 2 * STEP), &vm);
        rb = vins_tok(&o->state, f, (int32_t)(intptr_t)(o->nodes + STEP),
                      (int32_t)(intptr_t)(o->nodes + 2 * STEP), &vo);
    } else {
        ra = ibm_vins_tok(&m->state, f, (int32_t)(intptr_t)m->nodes,
                          (int32_t)(intptr_t)(m->nodes + 3 * STEP), &vm);
        rb = vins_tok(&o->state, f, (int32_t)(intptr_t)o->nodes,
                      (int32_t)(intptr_t)(o->nodes + 3 * STEP), &vo);
    }
    if (ra != rb)
        bad++;
END(vins_tok)


/* The spine and heap the insert and delete entry points share: four nodes,
   every field carried so chkdelnonseq refuses early, and every node stamped
   as a heap object so it can go back. */
static void build_edit(delta_world *m, delta_world *o, int8_t fd)
{
    enum { FB = 15, NNODE = 4, STEP = 0x80, NFIELD = 4 };
    int i, j;

    build_pspine(m, o);
    build_heap(m, o);
    m->state.fence_fill = o->state.fence_fill = NFIELD;
    m->vars.relink = o->vars.relink = (int32_t)(rng_next() % 2u);
    m->vars.ctx_both = o->vars.ctx_both = (int32_t)(rng_next() % 2u);
    m->stack.del_field = o->stack.del_field = fd;
    m->nsqf[0] = o->nsqf[0] = -1;
    m->stack.sync_size = o->stack.sync_size = 0x80;

    for (i = 0; i < NSEG; i++) {
        int32_t used = (int32_t)(intptr_t)m->segs[i].end & 3;

        if (((int32_t)(intptr_t)m->segs[i].end & 7) == 0)
            used += 4;
        m->segs[i].used = o->segs[i].used = used;
    }

    for (i = 0; i < NNODE; i++) {
        /* Every field of every statement type, not just the fenced four: a
           context lookup walks until it finds a node carrying the one it was
           asked about, and never stops if nothing does. */
        for (j = 0; j < NSTMT; j++) {
            ((int32_t *)(m->nodes + i * STEP))[FB + j] |= 1;
            ((int32_t *)(o->nodes + i * STEP))[FB + j] |= 1;
        }
        ((int32_t *)(m->nodes + i * STEP))[2] &= ~2;
        ((int32_t *)(o->nodes + i * STEP))[2] &= ~2;
        if (i > 0) {
            *(delta_seg **)(m->nodes + i * STEP - 4) = &m->segs[1];
            *(delta_seg **)(o->nodes + i * STEP - 4) = &o->segs[1];
        }

        /* The right-hand spine link has to run forward here: vins_sync walks
           it looking for the far end of the span, and build_pspine points it
           the other way. */
        {
            int hi = i + 1 < NNODE ? i + 1 : NNODE - 1;

            ((int32_t *)(m->nodes + i * STEP))[FB - 2] =
                (int32_t)(intptr_t)(m->nodes + hi * STEP);
            ((int32_t *)(o->nodes + i * STEP))[FB - 2] =
                (int32_t)(intptr_t)(o->nodes + hi * STEP);
        }
    }
    m->segs[1].live = o->segs[1].live = 16;

    m->stack.spine_l = (int32_t)(intptr_t)m->nodes;
    o->stack.spine_l = (int32_t)(intptr_t)o->nodes;
    m->stack.spine_r = (int32_t)(intptr_t)(m->nodes + 3 * STEP);
    o->stack.spine_r = (int32_t)(intptr_t)(o->nodes + 3 * STEP);
}

BEGIN(vinit_stm)
    int8_t f = (int8_t)(rng_next() % NSTMT);
    int ra, rb;

    build_edit(m, o, (int8_t)(rng_next() % 4u));

    ra = ibm_vinit_stm(&m->state, f);
    rb = vinit_stm(&o->state, f);
    if (ra != rb)
        bad++;
END(vinit_stm)

BEGIN(ins_tokens)
    /* A byte string can only feed a statement whose first field is itself a
       byte: vassign refuses to widen a byte into anything larger, so what the
       original writes there is whatever the stack held. A wide string fits
       every kind, so pick the statement first and the width to suit it. */
    uint8_t f = 0xffu;
    int wide = 1;
    uint8_t str[8];
    uint8_t n;
    int ra, rb, i;

    for (i = 0; i < NSTMT; i++) {
        int k = (int)((rng_next() + (uint32_t)i) % NSTMT);
        int16_t kind = vstmtbl[k].fields[0].kind;

        if (kind >= -4 && kind <= -1) {
            f = (uint8_t)k;
            if (kind == DK_UBYTE)
                wide = (int)(rng_next() % 2u);
            break;
        }
    }
    if (f == 0xffu) {
        free(m); free(o);
        continue;
    }
    n = (uint8_t)(wide ? 2u * (rng_next() % 3u) : (rng_next() % 3u));

    build_edit(m, o, (int8_t)(rng_next() % 4u));
    for (i = 0; i < 8; i++)
        str[i] = (uint8_t)rng_next();

    m->state.lpta.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    o->state.lpta.node = (int32_t)(intptr_t)(o->nodes + 0x80);
    m->state.rpta.node = (int32_t)(intptr_t)(m->nodes + 2 * 0x80);
    o->state.rpta.node = (int32_t)(intptr_t)(o->nodes + 2 * 0x80);

    if (wide) {
        ra = ibm_ins_tokens_i(&m->state, f, str, n, 0);
        rb = ins_tokens_i(&o->state, f, str, n, 0);
    } else {
        ra = ibm_ins_tokens_s(&m->state, f, str, n, 0);
        rb = ins_tokens_s(&o->state, f, str, n, 0);
    }
    if (ra != rb)
        bad++;

END(ins_tokens)


BEGIN(vsplit_time)
    /* Only the two kinds the split knows how to read; anything else leaves
       the operand it inserts with whatever the frame held. */
    uint8_t f = 0xffu;
    int32_t off = (int32_t)(rng_next() % 21u) - 10;
    int32_t ra, rb;
    int i;

    for (i = 0; i < NSTMT; i++) {
        int k = (int)((rng_next() + (uint32_t)i) % NSTMT);
        int16_t kind = vstmtbl[k].fields[0].kind;

        if (kind == DK_LONG || kind == DK_SHORT2) {
            f = (uint8_t)k;
            break;
        }
    }
    if (f == 0xffu) {
        free(m); free(o);
        continue;
    }

    build_edit(m, o, (int8_t)(rng_next() % 4u));

    ra = ibm_vsplit_time(&m->state, f,
                         (int32_t)(intptr_t)(m->nodes + 0x80), off);
    rb = vsplit_time(&o->state, f,
                     (int32_t)(intptr_t)(o->nodes + 0x80), off);

    if ((ra == 0) != (rb == 0))
        bad++;
    else if (ra != 0
             && ra - (int32_t)(intptr_t)m != rb - (int32_t)(intptr_t)o)
        bad++;
END(vsplit_time)


BEGIN(time_marks)
    /* Both of these settle a position and then cut the run, so they need the
       edit scaffold rather than the plain timing spine. */
    static const uint8_t sets[4] = {0, 1, 4, 8};
    uint8_t f = 0xffu;
    uint8_t back = (uint8_t)(rng_next() % 2u);
    delta_tpos pm, po;
    int ra, rb, i;

    for (i = 0; i < NSTMT; i++) {
        int k = (int)((rng_next() + (uint32_t)i) % NSTMT);
        int16_t kind = vstmtbl[k].fields[0].kind;

        if (kind == DK_LONG || kind == DK_SHORT2) {
            f = (uint8_t)k;
            break;
        }
    }
    if (f == 0xffu) {
        free(m); free(o);
        continue;
    }

    build_edit(m, o, (int8_t)(rng_next() % 4u));

    /* The ends must not be syncs, or rmost and lmost park on them, and every
       value must be non-zero or those two keep walking past. */
    for (i = 0; i < 4; i++) {
        uint32_t r = 16u + rng_next() % 16u;

        if (i == 0 || i == 3) {
            ((int32_t *)(m->nodes + i * 0x80))[0] &= ~2;
            ((int32_t *)(o->nodes + i * 0x80))[0] &= ~2;
        }
        if (vstmtbl[f].fields[0].kind == DK_LONG) {
            *(int32_t *)vstmtbl[f].get[0](m->nodes + i * 0x80 + 8) =
                (int32_t)r;
            *(int32_t *)vstmtbl[f].get[0](o->nodes + i * 0x80 + 8) =
                (int32_t)r;
        } else {
            *(int16_t *)vstmtbl[f].get[0](m->nodes + i * 0x80 + 8) =
                (int16_t)r;
            *(int16_t *)vstmtbl[f].get[0](o->nodes + i * 0x80 + 8) =
                (int16_t)r;
        }
    }

    pm.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    po.node = (int32_t)(intptr_t)(o->nodes + 0x80);
    pm.field = po.field = (int8_t)f;
    pm.pad_05[0] = po.pad_05[0] = 0;
    pm.pad_05[1] = po.pad_05[1] = 0;
    pm.pad_05[2] = po.pad_05[2] = 0;
    pm.offset = po.offset = (int32_t)(rng_next() % 21u) - 10;
    pm.flags = po.flags = sets[rng_next() % 4u];
    pm.pad_0d[0] = po.pad_0d[0] = 0;
    pm.pad_0d[1] = po.pad_0d[1] = 0;
    pm.pad_0d[2] = po.pad_0d[2] = 0;

    if (rng_next() % 2u) {
        ra = ibm_vsync_tv(&m->state, &pm);
        rb = vsync_tv(&o->state, &po);
    } else {
        ra = ibm_vtmark_tv(&m->state, &pm, back);
        rb = vtmark_tv(&o->state, &po, back);
    }

    if (ra != rb)
        bad++;
    if (pm.offset != po.offset || pm.flags != po.flags)
        bad++;
    if ((pm.node == 0) != (po.node == 0))
        bad++;
    else if (pm.node != 0
             && pm.node - (int32_t)(intptr_t)m != po.node - (int32_t)(intptr_t)o)
        bad++;
END(time_marks)


/* The edit scaffold with a field the timing code can read and ends that no
   walk parks on, which is what everything from here down needs. */
static int build_time_edit(delta_world *m, delta_world *o, uint8_t *out)
{
    uint8_t f = 0xffu;
    int i;

    for (i = 0; i < NSTMT; i++) {
        int k = (int)((rng_next() + (uint32_t)i) % NSTMT);
        int16_t kind = vstmtbl[k].fields[0].kind;

        if (kind == DK_LONG || kind == DK_SHORT2) {
            f = (uint8_t)k;
            break;
        }
    }
    if (f == 0xffu)
        return 0;

    build_edit(m, o, (int8_t)(rng_next() % 4u));

    for (i = 0; i < 4; i++) {
        uint32_t r = 16u + rng_next() % 16u;

        if (i == 0 || i == 3) {
            ((int32_t *)(m->nodes + i * 0x80))[0] &= ~2;
            ((int32_t *)(o->nodes + i * 0x80))[0] &= ~2;
        }
        if (vstmtbl[f].fields[0].kind == DK_LONG) {
            *(int32_t *)vstmtbl[f].get[0](m->nodes + i * 0x80 + 8) = (int32_t)r;
            *(int32_t *)vstmtbl[f].get[0](o->nodes + i * 0x80 + 8) = (int32_t)r;
        } else {
            *(int16_t *)vstmtbl[f].get[0](m->nodes + i * 0x80 + 8) = (int16_t)r;
            *(int16_t *)vstmtbl[f].get[0](o->nodes + i * 0x80 + 8) = (int16_t)r;
        }
    }

    *out = f;
    return 1;
}

/* Both of these backtrack when they cannot do what was asked, so each side
   needs somewhere to land. */
static void store_ibm(delta_state *d, delta_loc *loc, uint8_t f, int which)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0) {
        if (which)
            ibm_lpta_storep(d, loc);
        else
            ibm_delete_1pt(d, f);
    }
    d->vars->err_jmp = 0;
}

static void store_ours(delta_state *d, delta_loc *loc, uint8_t f, int which)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0) {
        if (which)
            lpta_storep(d, loc);
        else
            delete_1pt(d, f);
    }
    d->vars->err_jmp = 0;
}

BEGIN(point_edits)
    uint8_t f;
    int which = (int)(rng_next() % 2u);
    /* The saved record keeps the address of what was saved, so it has to be
       somewhere the harness can rewrite as an offset. */
    delta_loc *lm;
    delta_loc *lo;

    if (!build_time_edit(m, o, &f)) {
        free(m); free(o);
        continue;
    }
    lm = (delta_loc *)(m->nodes + 0x300);
    lo = (delta_loc *)(o->nodes + 0x300);

    m->state.lpta.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    o->state.lpta.node = (int32_t)(intptr_t)(o->nodes + 0x80);
    m->state.lpta.field = o->state.lpta.field = (int8_t)f;
    /* A left-over offset would send this down the cut-and-insert path, which
       vsplit_time's own test already covers; here the position is exact. */
    m->state.lpta.offset = o->state.lpta.offset = 0;
    m->state.lpta.flags = o->state.lpta.flags = (uint8_t)(rng_next() % 2u);
    m->vars.testing = o->vars.testing = (int8_t)(rng_next() % 2u);
    m->stack.size_ac = o->stack.size_ac = 12;

    memset(lm, 0, sizeof(*lm));
    memset(lo, 0, sizeof(*lo));
    lm->kind = lo->kind = DK_SYNC;

    store_ibm(&m->state, lm, f, which);
    store_ours(&o->state, lo, f, which);

    if (lm->kind != lo->kind || lm->field != lo->field)
        bad++;
    if ((lm->value == 0) != (lo->value == 0))
        bad++;
    else if (lm->value != 0
             && lm->value - (int32_t)(intptr_t)m
                != lo->value - (int32_t)(intptr_t)o)
        bad++;
END(point_edits)

BEGIN(vrange)
    uint8_t f;
    uint8_t dup = (uint8_t)(rng_next() % 2u);
    int left = (int)(rng_next() % 2u);
    delta_tpos pm, po, om, oo;
    int ra, rb;

    if (!build_time_edit(m, o, &f)) {
        free(m); free(o);
        continue;
    }

    /* Keep the context lookup on its cheap path: the full one borrows links
       and this spine is not built for it. */
    m->vars.relink = o->vars.relink = 1;
    for (ra = 0; ra < 0x20; ra++)
        m->nsqm[ra] = o->nsqm[ra] = 0;

    memset(&pm, 0, sizeof(pm));
    pm.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    pm.field = (int8_t)f;
    /* Exact, for the same reason point_edits is: a left-over offset sends
       this through the cut and insert, which vsplit_time covers itself. */
    pm.offset = 0;
    pm.flags = (uint8_t)(rng_next() % 2u);
    po = pm;
    po.node = (int32_t)(intptr_t)(o->nodes + 0x80);
    memset(&om, 0, sizeof(om));
    oo = om;

    if (left) {
        ra = ibm_vrange_l(&m->state, &pm, &om, (int8_t)f, dup);
        rb = vrange_l(&o->state, &po, &oo, (int8_t)f, dup);
    } else {
        ra = ibm_vrange_r(&m->state, &pm, &om, (int8_t)f, dup);
        rb = vrange_r(&o->state, &po, &oo, (int8_t)f, dup);
    }

    if (ra != rb)
        bad++;
    if (om.flags != oo.flags || pm.flags != po.flags
        || pm.offset != po.offset)
        bad++;
    if ((om.node == 0) != (oo.node == 0))
        bad++;
    else if (om.node != 0
             && om.node - (int32_t)(intptr_t)m != oo.node - (int32_t)(intptr_t)o)
        bad++;
    if ((pm.node == 0) != (po.node == 0))
        bad++;
    else if (pm.node != 0
             && pm.node - (int32_t)(intptr_t)m != po.node - (int32_t)(intptr_t)o)
        bad++;
END(vrange)


/* Both of these backtrack when the range cannot be opened, and the language
   call that would fill it is not part of the runtime, so what is compared is
   the range opening and the backtrack. */
static void insert_ibm(delta_state *d, int8_t f, uint8_t n,
                       const uint8_t *str, uint8_t dup, int left)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0) {
        if (left)
            ibm_insert_l(d, f, n, str, dup);
        else
            ibm_insert_r(d, f, n, str, dup);
    }
    d->vars->err_jmp = 0;
}

static void insert_ours(delta_state *d, int8_t f, uint8_t n,
                        const uint8_t *str, uint8_t dup, int left)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0) {
        if (left)
            insert_l(d, f, n, str, dup);
        else
            insert_r(d, f, n, str, dup);
    }
    d->vars->err_jmp = 0;
}

BEGIN(insert_points)
    uint8_t f;
    uint8_t dup = (uint8_t)(rng_next() % 2u);
    int left = (int)(rng_next() % 2u);
    uint8_t str[4];
    int i;

    if (!build_time_edit(m, o, &f)) {
        free(m); free(o);
        continue;
    }
    m->vars.relink = o->vars.relink = 1;
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = 0;
    for (i = 0; i < 4; i++)
        str[i] = (uint8_t)rng_next();

    m->state.lpta.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    o->state.lpta.node = (int32_t)(intptr_t)(o->nodes + 0x80);
    m->state.rpta.node = (int32_t)(intptr_t)(m->nodes + 2 * 0x80);
    o->state.rpta.node = (int32_t)(intptr_t)(o->nodes + 2 * 0x80);
    m->state.lpta.field = o->state.lpta.field = (int8_t)f;
    m->state.rpta.field = o->state.rpta.field = (int8_t)f;
    m->state.lpta.offset = o->state.lpta.offset = 0;
    m->state.rpta.offset = o->state.rpta.offset = 0;
    m->state.lpta.flags = o->state.lpta.flags = (uint8_t)(rng_next() % 2u);
    m->state.rpta.flags = o->state.rpta.flags = (uint8_t)(rng_next() % 2u);

    insert_ibm(&m->state, (int8_t)f, 2, str, dup, left);
    insert_ours(&o->state, (int8_t)f, 2, str, dup, left);
END(insert_points)


BEGIN(vrange_2pt)
    /* No mode at all sends this through vcomp_pta, whose remembered walk
       follows the forward links and never leaves this spine's self-pointing
       end. That path is what vcomp_pta's own test is for. */
    static const uint8_t modes[4] = {0xcd, 0xce, 0xcf, 0x11};
    uint8_t f;
    uint8_t mode = modes[rng_next() % 4u];
    delta_tpos am, ao, bm, bo;
    int ra, rb, i;

    if (!build_time_edit(m, o, &f)) {
        free(m); free(o);
        continue;
    }
    m->vars.relink = o->vars.relink = 1;
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = 0;
    for (i = 0; i < 50; i++) {
        m->stack.left_a[i] = o->stack.left_a[i] = 0;
        m->stack.left_b[i] = o->stack.left_b[i] = 0;
        m->stack.left_ans[i] = o->stack.left_ans[i] = 0;
        m->stack.left_hits[i] = o->stack.left_hits[i] = 0;
    }
    m->stack.left_next = o->stack.left_next = 0;
    m->stack.left_stamp = o->stack.left_stamp = spine_changed;

    memset(&am, 0, sizeof(am));
    am.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    am.field = (int8_t)f;
    /* Exact: a leftover offset would cut, which vsplit_time covers itself. */
    am.offset = 0;
    am.flags = (uint8_t)(rng_next() % 2u);
    ao = am;
    ao.node = (int32_t)(intptr_t)(o->nodes + 0x80);

    memset(&bm, 0, sizeof(bm));
    bm.node = (int32_t)(intptr_t)(m->nodes + 2 * 0x80);
    bm.field = (int8_t)f;
    bm.offset = 0;
    bm.flags = (uint8_t)(rng_next() % 2u);
    bo = bm;
    bo.node = (int32_t)(intptr_t)(o->nodes + 2 * 0x80);

    ra = ibm_vrange_2pt(&m->state, &am, &bm, (int8_t)f, mode);
    rb = vrange_2pt(&o->state, &ao, &bo, (int8_t)f, mode);

    if (ra != rb)
        bad++;
    if (am.flags != ao.flags || bm.flags != bo.flags
        || am.offset != ao.offset || bm.offset != bo.offset)
        bad++;
    if ((am.node == 0) != (ao.node == 0))
        bad++;
    else if (am.node != 0
             && am.node - (int32_t)(intptr_t)m != ao.node - (int32_t)(intptr_t)o)
        bad++;
    if ((bm.node == 0) != (bo.node == 0))
        bad++;
    else if (bm.node != 0
             && bm.node - (int32_t)(intptr_t)m != bo.node - (int32_t)(intptr_t)o)
        bad++;

    for (i = 0; i < 50; i++) {
        m->stack.left_a[i] = o->stack.left_a[i] = 0;
        m->stack.left_b[i] = o->stack.left_b[i] = 0;
    }
END(vrange_2pt)


BEGIN(two_point_edits)
    static const uint8_t modes[4] = {0xcd, 0xce, 0xcf, 0x11};
    uint8_t f;
    uint8_t mode = modes[rng_next() % 4u];
    uint8_t str[4];
    /* The byte spelling is left out: it can only feed a statement whose
       first field is a byte, and the range code here needs a long or a
       short. ins_tokens tests that pairing itself. */
    uint32_t which = 1u + rng_next() % 3u;
    int ra, rb, i;

    if (!build_time_edit(m, o, &f)) {
        free(m); free(o);
        continue;
    }
    m->vars.relink = o->vars.relink = 1;
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = 0;
    for (i = 0; i < 4; i++)
        str[i] = (uint8_t)rng_next();

    m->state.lpta.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    o->state.lpta.node = (int32_t)(intptr_t)(o->nodes + 0x80);
    m->state.rpta.node = (int32_t)(intptr_t)(m->nodes + 2 * 0x80);
    o->state.rpta.node = (int32_t)(intptr_t)(o->nodes + 2 * 0x80);
    m->state.lpta.field = o->state.lpta.field = (int8_t)f;
    m->state.rpta.field = o->state.rpta.field = (int8_t)f;
    m->state.lpta.offset = o->state.lpta.offset = 0;
    m->state.rpta.offset = o->state.rpta.offset = 0;
    m->state.lpta.flags = o->state.lpta.flags = (uint8_t)(rng_next() % 2u);
    m->state.rpta.flags = o->state.rpta.flags = (uint8_t)(rng_next() % 2u);

    if (which == 1) {
        ra = ibm_insert_2pt_i(&m->state, f, 2, str, mode);
        rb = insert_2pt_i(&o->state, f, 2, str, mode);
    } else if (which == 2) {
        ra = ibm_delete_2pt(&m->state, f, mode);
        rb = delete_2pt(&o->state, f, mode);
    } else {
        uint8_t fld = (uint8_t)(rng_next() % (uint32_t)vstmtbl[f].nfields);
        uint8_t v = (uint8_t)(rng_next() % 4u);

        ra = ibm_mark_s(&m->state, f, fld, v, mode);
        rb = mark_s(&o->state, f, fld, v, mode);
    }

    if (ra != rb)
        bad++;

    /* vmark parks the address of its own argument, which is a stack one. */
    m->stack.mark_fld = o->stack.mark_fld = NULL;
END(two_point_edits)


static void var_ibm(delta_state *d, uint8_t f, uint8_t fld, delta_loc *loc,
                    uint8_t mode, int which, int *out)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0)
        *out = which ? ibm_mark_v(d, f, fld, loc, mode)
                     : ibm_insert_2ptv(d, f, loc, mode);
    d->vars->err_jmp = 0;
}

static void var_ours(delta_state *d, uint8_t f, uint8_t fld, delta_loc *loc,
                     uint8_t mode, int which, int *out)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0)
        *out = which ? mark_v(d, f, fld, loc, mode)
                     : insert_2ptv(d, f, loc, mode);
    d->vars->err_jmp = 0;
}

BEGIN(var_edits)
    static const uint8_t modes[4] = {0xcd, 0xce, 0xcf, 0x11};
    static const int16_t kinds[3] = {-3, -4, -6};
    uint8_t f;
    uint8_t mode = modes[rng_next() % 4u];
    int which = (int)(rng_next() % 2u);
    delta_loc *lm;
    delta_loc *lo;
    int ra = -1, rb = -1;
    int i;

    if (!build_time_edit(m, o, &f)) {
        free(m); free(o);
        continue;
    }
    m->vars.relink = o->vars.relink = 1;
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = 0;

    m->state.lpta.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    o->state.lpta.node = (int32_t)(intptr_t)(o->nodes + 0x80);
    m->state.rpta.node = (int32_t)(intptr_t)(m->nodes + 2 * 0x80);
    o->state.rpta.node = (int32_t)(intptr_t)(o->nodes + 2 * 0x80);
    m->state.lpta.field = o->state.lpta.field = (int8_t)f;
    m->state.rpta.field = o->state.rpta.field = (int8_t)f;
    m->state.lpta.offset = o->state.lpta.offset = 0;
    m->state.rpta.offset = o->state.rpta.offset = 0;
    m->state.lpta.flags = o->state.lpta.flags = (uint8_t)(rng_next() % 2u);
    m->state.rpta.flags = o->state.rpta.flags = (uint8_t)(rng_next() % 2u);

    /* The variable lives in the world so its address rebases, and its kind
       is one vinitloc_new gives a pointer for. */
    lm = (delta_loc *)(m->nodes + 0x300);
    lo = (delta_loc *)(o->nodes + 0x300);
    memset(lm, 0, sizeof(*lm));
    memset(lo, 0, sizeof(*lo));
    lm->kind = lo->kind = kinds[rng_next() % 3u];
    lm->field = lo->field = (int16_t)rng_next();
    lm->value = lo->value = (int32_t)rng_next();

    {
        uint8_t fld = (uint8_t)(rng_next() % (uint32_t)vstmtbl[f].nfields);

        var_ibm(&m->state, f, fld, lm, mode, which, &ra);
        var_ours(&o->state, f, fld, lo, mode, which, &rb);
    }

    if (ra != rb)
        bad++;
    if (lm->kind != lo->kind || lm->field != lo->field)
        bad++;

    m->stack.mark_fld = o->stack.mark_fld = NULL;

END(var_edits)


static void init_ibm(delta_state *d, int which, uint8_t n, const uint8_t *l)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0) {
        if (which)
            ibm_deltaReinit(d, (int32_t)n);
        else
            ibm_initdelta(d, n, l);
    }
    d->vars->err_jmp = 0;
}

static void init_ours(delta_state *d, int which, uint8_t n, const uint8_t *l)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0) {
        if (which)
            deltaReinit(d, (int32_t)n);
        else
            initdelta(d, n, l);
    }
    d->vars->err_jmp = 0;
}

BEGIN(reinit)
    /* These two rebuild a whole spine, so what they need is the spine they
       are meant to produce: two ends and nothing between them. */
    enum { FB = 15, NNODE = 2, STEP = 0x80, NFIELD = 4 };
    /* Only the pass that relinks the two ends. Asking for the full rebuild
       drives the language's own statement initialisation over the whole
       spine, which is a state this harness cannot build; every primitive
       that path is made of is compared on its own. initdelta reaches that
       rebuild whatever it is given, so it is transcribed but not compared. */
    int which = 1;
    uint8_t n = 0;
    uint8_t list[4];
    int i, j;

    build_heap(m, o);
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = FB;
    m->state.fence_fill = o->state.fence_fill = NFIELD;
    m->vars.relink = o->vars.relink = 1;
    m->vars.ctx_both = o->vars.ctx_both = (int32_t)(rng_next() % 2u);
    m->stack.sync_size = o->stack.sync_size = 0x80;
    m->nsqf[0] = o->nsqf[0] = -1;
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = 0;
    for (i = 0; i < 4; i++)
        list[i] = (uint8_t)(rng_next() % 4u);

    for (i = 0; i < NSEG; i++) {
        int32_t used = (int32_t)(intptr_t)m->segs[i].end & 3;

        if (((int32_t)(intptr_t)m->segs[i].end & 7) == 0)
            used += 4;
        m->segs[i].used = o->segs[i].used = used;
    }

#define NODE(w, i) ((int32_t *)((w)->nodes + (i) * STEP))
#define AT(w, i)   ((int32_t)(intptr_t)((w)->nodes + (i) * STEP))
    for (i = 0; i < NNODE; i++) {
        int other = 1 - i;

        /* Neither end is a sync, and each points at the other both ways. */
        NODE(m, i)[0] = AT(m, other);
        NODE(o, i)[0] = AT(o, other);
        NODE(m, i)[1] = AT(m, other);
        NODE(o, i)[1] = AT(o, other);
        NODE(m, i)[FB - 2] = AT(m, other);
        NODE(o, i)[FB - 2] = AT(o, other);

        for (j = 0; j < NSTMT; j++) {
            NODE(m, i)[3 + j] = AT(m, other) | 1;
            NODE(o, i)[3 + j] = AT(o, other) | 1;
            NODE(m, i)[FB + j] = AT(m, other) | 1;
            NODE(o, i)[FB + j] = AT(o, other) | 1;
        }

    }
    m->stack.spine_l = AT(m, 0);
    o->stack.spine_l = AT(o, 0);
    m->stack.spine_r = AT(m, 1);
    o->stack.spine_r = AT(o, 1);
    /* Whatever goes back to the heap must look like it came from one. */
    *(delta_seg **)(m->nodes + STEP - 4) = &m->segs[1];
    *(delta_seg **)(o->nodes + STEP - 4) = &o->segs[1];
    m->segs[1].live = o->segs[1].live = 32;
#undef NODE
#undef AT

    init_ibm(&m->state, which, n, list);
    init_ours(&o->state, which, n, list);
END(reinit)


/* Entering and leaving a rule is the frame protocol the compiled rules are
   built on: ventproc saves the machine into the rule's own record and pushes
   a marker, vretproc pops it and puts everything back. */
static int frame_ibm(delta_state *d, delta_world *w, int enter)
{
    jmp_buf jb;
    int r = -1;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0) {
        if (enter)
            r = ibm_ventproc(d, &w->actrec, w->fence2[0], w->fence2[1],
                             w->fence2[2], jb);
        else
            r = ibm_vretproc(d, 1);
    }
    d->vars->err_jmp = 0;
    return r;
}

static int frame_ours(delta_state *d, delta_world *w, int enter)
{
    jmp_buf jb;
    int r = -1;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0) {
        if (enter)
            r = ventproc(d, &w->actrec, w->fence2[0], w->fence2[1],
                         w->fence2[2], jb);
        else
            r = vretproc(d, 1);
    }
    d->vars->err_jmp = 0;
    return r;
}

BEGIN(rule_frame)
    int ra, rb;

    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);
    /* Left as fill this would look like an error already thrown, and
       vretproc would backtrack into a frame that has already returned. */
    m->vars.error_thrown = o->vars.error_thrown = 0;
    m->vars.ptr_count = o->vars.ptr_count = (int32_t)(rng_next() % 900u);
    m->vars.active_record = o->vars.active_record =
        (int32_t)(rng_next() % 900u);
    m->stack.size_a8 = o->stack.size_a8 = 0x18;
    m->vars.back = m->records + 0x40;
    o->vars.back = o->records + 0x40;
    m->stack.vbot = m->records + 0x20;
    o->stack.vbot = o->records + 0x20;

    if (rng_next() % 2u) {
        ra = frame_ibm(&m->state, m, 1);
        rb = frame_ours(&o->state, o, 1);
    } else {
        /* Leaving needs a frame to pop, so enter first on both sides. */
        frame_ibm(&m->state, m, 1);
        frame_ours(&o->state, o, 1);
        if (rng_next() % 2u) {
            ra = frame_ibm(&m->state, m, 0);
            rb = frame_ours(&o->state, o, 0);
        } else {
            ra = ibm_succeed(&m->state);
            rb = succeed(&o->state);
        }
    }

    if (ra != rb)
        bad++;

    /* The record and the pushed marker both hold addresses of the caller's
       own frame, which no two runs share. */
    m->actrec.err_jmp = o->actrec.err_jmp = NULL;
    m->vars.err_jmp = o->vars.err_jmp = NULL;

END(rule_frame)


static void move_ibm(delta_state *d, delta_loc *loc, int16_t v)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0)
        ibm_move_i(d, loc, v);
    d->vars->err_jmp = 0;
}

static void move_ours(delta_state *d, delta_loc *loc, int16_t v)
{
    jmp_buf jb;

    d->vars->err_jmp = jb;
    if (setjmp(jb) == 0)
        move_i(d, loc, v);
    d->vars->err_jmp = 0;
}

BEGIN(move_i)
    /* Every kind, including the ones that only backtrack, since each side
       has its own place to land. The byte and short kinds are left out when
       the rule is under test, because saving one copies through the pointer
       vinitloc_new never sets; that is the original's own hazard and the
       save_var test leaves them out for the same reason. */
    static const int16_t kinds[7] = {-1, -2, -3, -4, -5, -6, 0};
    static const int16_t safe[4] = {-3, -4, -6, 0};
    delta_loc *lm = (delta_loc *)(m->nodes + 0x300);
    delta_loc *lo = (delta_loc *)(o->nodes + 0x300);
    int16_t v = (int16_t)rng_next();
    int16_t kind;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);
    m->stack.size_ac = o->stack.size_ac = 12;
    m->vars.testing = o->vars.testing = (int8_t)(rng_next() % 2u);
    m->vars.error_thrown = o->vars.error_thrown = 0;

    kind = m->vars.testing ? safe[rng_next() % 4u] : kinds[rng_next() % 7u];

    if (kind == 0)
        kind = (int16_t)(rng_next() % NSTMT);

    memset(lm, 0, sizeof(*lm));
    memset(lo, 0, sizeof(*lo));
    lm->kind = lo->kind = kind;
    lm->field = lo->field = (kind >= 0)
        ? (int16_t)(rng_next() % (uint32_t)vstmtbl[kind].nfields)
        : (int16_t)rng_next();
    /* The value a variable of a language kind holds sits in the record
       itself, so this is a number in every case and never a pointer. */
    lm->value = lo->value = (int32_t)rng_next();

    move_ibm(&m->state, lm, v);
    move_ours(&o->state, lo, v);

    /* Saving a variable records where it came from, and that address is a
       different one in each world. */
    if (m->vars.testing) {
        *(int32_t *)(m->stack.top + 4) = 0;
        *(int32_t *)(o->stack.top + 4) = 0;
    }

    if (lm->kind != lo->kind || lm->field != lo->field)
        bad++;
    else if (region_differs(m, o, m->nodes, o->nodes, sizeof(m->nodes)))
        bad++;
END(move_i)


BEGIN(actd_goto)
    int ra, rb;

    m->vars.unknown_11ec = o->vars.unknown_11ec = (int16_t)rng_next();
    ibm_pause(&m->state);
    pause(&o->state);
    ra = ibm_actd_goto(&m->state);
    rb = actd_goto(&o->state);
    if (ra != rb)
        bad++;
END(actd_goto)

BEGIN(while_iterate)
    int16_t a = (int16_t)rng_next();
    int16_t b = (int16_t)rng_next();
    int ra = ibm_while_iterate(&m->state, a, b);
    int rb = while_iterate(&o->state, a, b);

    if (ra != rb)
        bad++;
END(while_iterate)

BEGIN(npush_lng)
    int32_t v = (int32_t)rng_next();

    ibm_npush_lng(&m->state, v);
    npush_lng(&o->state, v);
END(npush_lng)

/* The kinds a variable can take when a rule saves or pushes it. The byte and
   short kinds are left out because vinitloc_new never sets the pointer for
   them, so anything that copies through it reads a stale local; that is the
   original's own hazard, and save_var's test avoids it the same way. */
static int16_t var_kind(void)
{
    static const int16_t kinds[4] = {-3, -4, -6, 0};
    int16_t k = kinds[rng_next() % 4u];

    return k ? k : (int16_t)(rng_next() % NSTMT);
}

static void var_setup_at(delta_world *m, delta_world *o,
                      delta_loc *lm, delta_loc *lo)
{
    int16_t kind = var_kind();

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->stack.size_ac = o->stack.size_ac = 12;
    lm->kind = lo->kind = kind;
    lm->field = lo->field = (kind >= 0)
        ? (int16_t)(rng_next() % (uint32_t)vstmtbl[kind].nfields)
        : (int16_t)rng_next();
    /* A variable of a language kind names a record, so it is left naming
       nothing rather than naming a number. The number kinds keep their value
       here and are given one. */
    lm->value = lo->value = (kind >= 0) ? 0 : (int32_t)rng_next();
}

BEGIN(npush_v)
    delta_loc *lm = (delta_loc *)(m->nodes + 0x300);
    delta_loc *lo = (delta_loc *)(o->nodes + 0x300);

    var_setup_at(m, o, lm, lo);
    if (rng_next() % 2u) {
        ibm_npush_v(&m->state, lm);
        npush_v(&o->state, lo);
    } else {
        ibm_npush_vf(&m->state, lm);
        npush_vf(&o->state, lo);
    }
    if (lm->kind != lo->kind || lm->field != lo->field
        || lm->value != lo->value)
        bad++;
END(npush_v)

BEGIN(c_assvar)
    delta_loc *lm = (delta_loc *)(m->nodes + 0x300);
    delta_loc *lo = (delta_loc *)(o->nodes + 0x300);

    var_setup_at(m, o, lm, lo);
    m->vars.testing = o->vars.testing = (int8_t)(rng_next() % 2u);

    ibm_c_assvar(&m->state, lm);
    c_assvar(&o->state, lo);

    /* Saving records where the value came from, and that address belongs to
       the world it was taken from. */
    if (m->vars.testing) {
        *(int32_t *)(m->stack.top + 4) = 0;
        *(int32_t *)(o->stack.top + 4) = 0;
    }
    if (lm->kind != lo->kind || lm->field != lo->field
        || lm->value != lo->value)
        bad++;
END(c_assvar)

BEGIN(absoluteSyncNum)
    int32_t ra, rb;
    uint8_t *pm, *po;
    int seg = (int)(rng_next() % NSEG);
    int32_t off = (int32_t)((rng_next() % 0x18u) * 8u + 8u);
    int null = (rng_next() % 4u) == 0;

    build_heap(m, o);
    stamp_object(m, seg, off, &pm);
    stamp_object(o, seg, off, &po);
    m->stack.sync_size = o->stack.sync_size = (int32_t)(1u + rng_next() % 8u);

    ra = ibm_absoluteSyncNum(&m->state, null ? NULL : pm);
    rb = absoluteSyncNum(&o->state, null ? NULL : po);
    if (ra != rb)
        bad++;
END(absoluteSyncNum)

BEGIN(advance_strm)
    int ra, rb;
    int i;

    memset(m->records, 0, RECORDS);
    memset(o->records, 0, RECORDS);

    m->vars.fence_base = o->vars.fence_base = 2;
    m->vars.fence_count = o->vars.fence_count = (int8_t)(rng_next() % 4u);
    m->vars.scan_field = o->vars.scan_field = (uint8_t)(rng_next() % 2u);
    m->vars.scan_rev = o->vars.scan_rev = (uint8_t)(rng_next() % 2u);
    m->vars.scan_held = o->vars.scan_held = (uint8_t)(rng_next() % 2u);

    for (i = 0; i < 4; i++) {
        m->chars[i] = o->chars[i] = (uint8_t)i;
        m->marks[i] = o->marks[i] = (uint8_t)(rng_next() % 2u);
    }
    for (i = 0; i < 8; i++) {
        uint32_t r = rng_next();
        int null = (r % 5u) == 0;
        ((int32_t *)(m->records + 0x100))[i] = null ? 0
            : (int32_t)((intptr_t)(m->records + 0x140) | (r & 3u));
        ((int32_t *)(o->records + 0x100))[i] = null ? 0
            : (int32_t)((intptr_t)(o->records + 0x140) | (r & 3u));
    }
    for (i = 0; i < 2; i++) {
        uint32_t r = rng_next();
        int null = (r % 5u) == 0;
        ((int32_t *)(m->records + 0x140))[i] = null ? 0
            : (int32_t)((intptr_t)(m->records + 0x180) | (r & 3u));
        ((int32_t *)(o->records + 0x140))[i] = null ? 0
            : (int32_t)((intptr_t)(o->records + 0x180) | (r & 3u));
    }
    m->vars.scan_ptr = (int32_t)(intptr_t)(m->records + 0x100);
    o->vars.scan_ptr = (int32_t)(intptr_t)(o->records + 0x100);

    ra = ibm_advance_strm(&m->state);
    rb = advance_strm(&o->state);
    if (ra != rb)
        bad++;
    if ((m->vars.scan_ptr == 0) != (o->vars.scan_ptr == 0))
        bad++;
    else if (m->vars.scan_ptr != 0
             && m->vars.scan_ptr - (int32_t)(intptr_t)m
                != o->vars.scan_ptr - (int32_t)(intptr_t)o)
        bad++;
END(advance_strm)


/* Anything that can fault needs somewhere to land, one landing place per
   world, so the two are never compared across a jump. */
#define GUARDED(call)                                                    \
    do {                                                                 \
        jmp_buf jb;                                                      \
        d->vars->err_jmp = jb;                                           \
        if (setjmp(jb) == 0) { call; }                                   \
        d->vars->err_jmp = 0;                                            \
    } while (0)

static void run_proj_def(delta_state *d, uint8_t f, int ours)
{
    GUARDED(ours ? proj_def(d, f) : ibm_proj_def(d, f));
}

static void run_rpta_movel(delta_state *d, uint8_t f, int ours)
{
    GUARDED(ours ? rpta_movel(d, f) : ibm_rpta_movel(d, f));
}

static void run_rpta_storep(delta_state *d, delta_loc *l, int ours)
{
    GUARDED(ours ? rpta_storep(d, l) : ibm_rpta_storep(d, l));
}

static void run_lpta_loadv(delta_state *d, uint8_t f, const delta_loc *l,
                           int ours)
{
    GUARDED(ours ? lpta_loadv(d, f, l) : ibm_lpta_loadv(d, f, l));
}

BEGIN(pta_movel)
    static const uint8_t sets[4] = {0, 1, 4, 8};
    delta_tpos pm, po;
    int8_t f = build_tspine(m, o);
    uint8_t g = (uint8_t)(rng_next() % NSTMT);
    int right = (int)(rng_next() % 2u);
    int ra = 0, rb = 0;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    make_tpos(m, o, f, &pm, &po, sets[rng_next() % 4u]);

    if (right) {
        m->state.rpta = pm;
        o->state.rpta = po;
        run_rpta_movel(&m->state, g, 0);
        run_rpta_movel(&o->state, g, 1);
        pm = m->state.rpta;
        po = o->state.rpta;
    } else {
        m->state.lpta = pm;
        o->state.lpta = po;
        ra = ibm_lpta_tstmovel(&m->state, g);
        rb = lpta_tstmovel(&o->state, g);
        pm = m->state.lpta;
        po = o->state.lpta;
    }

    if (ra != rb)
        bad++;
    else if (pm.offset != po.offset || pm.flags != po.flags)
        bad++;
    else if ((pm.node == 0) != (po.node == 0))
        bad++;
    else if (pm.node != 0
             && pm.node - (int32_t)(intptr_t)m
                != po.node - (int32_t)(intptr_t)o)
        bad++;

    m->stack.spine_l = o->stack.spine_l = 0;
    m->stack.spine_r = o->stack.spine_r = 0;
    memset(&m->state.lpta, 0, sizeof(m->state.lpta));
    memset(&o->state.lpta, 0, sizeof(o->state.lpta));
    memset(&m->state.rpta, 0, sizeof(m->state.rpta));
    memset(&o->state.rpta, 0, sizeof(o->state.rpta));
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(pta_movel)

BEGIN(proj_def)
    uint8_t g = (uint8_t)(rng_next() % NSTMT);

    /* The projection walk needs the pointer spine, not the timing one, so the
       pointer is placed on a node of that. vsync_tv decides for itself
       whether the statement is one it can project, and either answer is a
       path through the rule. */
    build_pspine(m, o);
    ((int32_t *)(m->nodes))[15 + g] |= 1;
    ((int32_t *)(o->nodes))[15 + g] |= 1;
    ((int32_t *)(m->nodes))[2] &= ~2;
    ((int32_t *)(o->nodes))[2] &= ~2;
    ((int32_t *)(m->nodes + 3 * 0x80))[15 + g] |= 1;
    ((int32_t *)(o->nodes + 3 * 0x80))[15 + g] |= 1;
    ((int32_t *)(m->nodes + 3 * 0x80))[2] &= ~2;
    ((int32_t *)(o->nodes + 3 * 0x80))[2] &= ~2;

    memset(&m->state.lpta, 0, sizeof(m->state.lpta));
    memset(&o->state.lpta, 0, sizeof(o->state.lpta));
    m->state.lpta.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    o->state.lpta.node = (int32_t)(intptr_t)(o->nodes + 0x80);
    m->state.lpta.field = o->state.lpta.field = (int8_t)g;

    run_proj_def(&m->state, g, 0);
    run_proj_def(&o->state, g, 1);

    memset(&m->state.lpta, 0, sizeof(m->state.lpta));
    memset(&o->state.lpta, 0, sizeof(o->state.lpta));
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(proj_def)

BEGIN(rpta_storep)
    /* Settling the pointer onto a sync needs the edit scaffold, the same one
       the timing marks need, rather than the plain timing spine. */
    static const uint8_t sets[4] = {0, 1, 4, 8};
    static const int16_t kinds[4] = {-3, -4, -6, 0};
    uint8_t f = 0xffu;
    delta_loc *lm, *lo;
    int16_t kind = kinds[rng_next() % 4u];
    int i;

    for (i = 0; i < NSTMT; i++) {
        int k = (int)((rng_next() + (uint32_t)i) % NSTMT);
        int16_t fk = vstmtbl[k].fields[0].kind;

        if (fk == DK_LONG || fk == DK_SHORT2) {
            f = (uint8_t)k;
            break;
        }
    }
    if (f == 0xffu) {
        free(m); free(o);
        continue;
    }

    build_edit(m, o, (int8_t)(rng_next() % 4u));

    for (i = 0; i < 4; i++) {
        uint32_t r = 16u + rng_next() % 16u;

        if (i == 0 || i == 3) {
            ((int32_t *)(m->nodes + i * 0x80))[0] &= ~2;
            ((int32_t *)(o->nodes + i * 0x80))[0] &= ~2;
        }
        if (vstmtbl[f].fields[0].kind == DK_LONG) {
            *(int32_t *)vstmtbl[f].get[0](m->nodes + i * 0x80 + 8) =
                (int32_t)r;
            *(int32_t *)vstmtbl[f].get[0](o->nodes + i * 0x80 + 8) =
                (int32_t)r;
        } else {
            *(int16_t *)vstmtbl[f].get[0](m->nodes + i * 0x80 + 8) =
                (int16_t)r;
            *(int16_t *)vstmtbl[f].get[0](o->nodes + i * 0x80 + 8) =
                (int16_t)r;
        }
    }

    memset(&m->state.rpta, 0, sizeof(m->state.rpta));
    memset(&o->state.rpta, 0, sizeof(o->state.rpta));
    m->state.rpta.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    o->state.rpta.node = (int32_t)(intptr_t)(o->nodes + 0x80);
    m->state.rpta.field = o->state.rpta.field = (int8_t)f;
    m->state.rpta.offset = o->state.rpta.offset =
        (int32_t)(rng_next() % 21u) - 10;
    m->state.rpta.flags = o->state.rpta.flags = sets[rng_next() % 4u];

    m->stack.size_ac = o->stack.size_ac = 12;
    m->vars.testing = o->vars.testing = (int8_t)(rng_next() % 2u);

    /* The variable sits in a node the edit scaffold leaves alone, so that
       the address the save records is one that rebases. */
    lm = (delta_loc *)(m->nodes + 0x300);
    lo = (delta_loc *)(o->nodes + 0x300);
    memset(lm, 0, sizeof(*lm));
    lm->kind = kind ? kind : (int16_t)(rng_next() % NSTMT);
    lm->field = (lm->kind >= 0)
        ? (int16_t)(rng_next() % (uint32_t)vstmtbl[lm->kind].nfields)
        : (int16_t)rng_next();
    lm->value = (lm->kind >= 0) ? 0 : (int32_t)rng_next();
    *lo = *lm;

    run_rpta_storep(&m->state, lm, 0);
    run_rpta_storep(&o->state, lo, 1);

    if (m->vars.testing) {
        *(int32_t *)(m->stack.top + 4) = 0;
        *(int32_t *)(o->stack.top + 4) = 0;
    }
    if (lm->kind != lo->kind || lm->field != lo->field)
        bad++;
    else if ((lm->value == 0) != (lo->value == 0))
        bad++;
    else if (lm->value != 0
             && lm->value - (int32_t)(intptr_t)m
                != lo->value - (int32_t)(intptr_t)o)
        bad++;

    memset(&m->state.rpta, 0, sizeof(m->state.rpta));
    memset(&o->state.rpta, 0, sizeof(o->state.rpta));
END(rpta_storep)

BEGIN(lpta_loadv)
    static const int16_t kinds[4] = {-3, -4, -1, 0};
    delta_loc *lm = (delta_loc *)(m->nodes + 0x300);
    delta_loc *lo = (delta_loc *)(o->nodes + 0x300);
    uint8_t g = (uint8_t)(rng_next() % NSTMT);
    int16_t kind = kinds[rng_next() % 4u];

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    lm->kind = lo->kind = kind ? kind : (int16_t)(rng_next() % NSTMT);
    lm->field = lo->field = (int16_t)rng_next();
    lm->value = lo->value = (int32_t)rng_next();

    run_lpta_loadv(&m->state, g, lm, 0);
    run_lpta_loadv(&o->state, g, lo, 1);
END(lpta_loadv)

BEGIN(settvar)
    delta_loc *lm = (delta_loc *)(m->nodes + 0x300);
    delta_loc *lo = (delta_loc *)(o->nodes + 0x300);
    int32_t v = (int32_t)rng_next();
    int16_t st = (int16_t)(rng_next() % NSTMT);

    var_setup_at(m, o, lm, lo);
    /* The setter looks the kind up in the statement table, so it has to be
       a statement number and not one of the scalar kinds. */
    lm->kind = lo->kind = st;
    lm->field = lo->field =
        (int16_t)(rng_next() % (uint32_t)vstmtbl[st].nfields);
    lm->value = lo->value = 0;
    m->vars.testing = o->vars.testing = (int8_t)(rng_next() % 2u);

    if (rng_next() % 2u) {
        ibm_settvar_i(&m->state, lm, v);
        settvar_i(&o->state, lo, v);
    } else {
        ibm_settvar_s(&m->state, lm, v);
        settvar_s(&o->state, lo, v);
    }

    if (m->vars.testing) {
        *(int32_t *)(m->stack.top + 4) = 0;
        *(int32_t *)(o->stack.top + 4) = 0;
    }
END(settvar)


BEGIN(vnegative)
    static const int16_t kinds[6] = {-1, -2, -3, -4, -6, 0};
    delta_operand a;
    int ra, rb;

    fill(m->records + 0x80, 8);
    memcpy(o->records + 0x80, m->records + 0x80, 8);
    a.kind = kinds[rng_next() % 6u];
    a.flag = 0;

    a.ptr = m->records + 0x80;
    ra = ibm_vnegative(&m->state, &a);
    a.ptr = o->records + 0x80;
    rb = vnegative(&o->state, &a);
    if (ra != rb)
        bad++;
END(vnegative)

BEGIN(compare_tvars)
    delta_loc *am = (delta_loc *)(m->nodes + 0x300);
    delta_loc *ao = (delta_loc *)(o->nodes + 0x300);
    delta_loc *bm = (delta_loc *)(m->nodes + 0x310);
    delta_loc *bo = (delta_loc *)(o->nodes + 0x310);

    var_setup_at(m, o, am, ao);
    bm->kind = bo->kind = am->kind;
    bm->field = bo->field = am->field;
    bm->value = bo->value = (int32_t)rng_next();

    ibm_compare_tvars(&m->state, am, bm);
    compare_tvars(&o->state, ao, bo);

    if (am->field != ao->field || bm->field != bo->field)
        bad++;
END(compare_tvars)

/* The number stack the six comparisons and npop read from. Two numbers of
   the same kind, so the comparison has something it can answer about.

   The sync kind is left out: the push never writes its value and the pop
   never sets the operand's pointer for it, so a comparison of two of those
   reads through whatever the caller's local happened to hold. That is the
   original's own hazard and the language never pushes one. */
static void two_numbers(delta_world *m, delta_world *o)
{
    static const int16_t kinds[4] = {-1, -2, -3, -4};
    delta_operand v;
    int16_t k = kinds[rng_next() % 4u];
    int i;

    m->stack.names_depth = o->stack.names_depth = 0;
    for (i = 0; i < 2; i++) {
        fill(m->records + 0x80 + 0x10 * i, 8);
        memcpy(o->records + 0x80 + 0x10 * i,
               m->records + 0x80 + 0x10 * i, 8);
        v.kind = k;
        v.flag = 0;
        v.ptr = m->records + 0x80 + 0x10 * i;
        ibm_vnspush(&m->state, &v);
        v.ptr = o->records + 0x80 + 0x10 * i;
        vnspush(&o->state, &v);
    }
}

BEGIN(if_tests)
    uint32_t which = rng_next() % 6u;
    int ra, rb;

    two_numbers(m, o);

    switch (which) {
    case 0: ra = ibm_if_testeq(&m->state);  rb = if_testeq(&o->state);  break;
    case 1: ra = ibm_if_testneq(&m->state); rb = if_testneq(&o->state); break;
    case 2: ra = ibm_if_testlt(&m->state);  rb = if_testlt(&o->state);  break;
    case 3: ra = ibm_if_testle(&m->state);  rb = if_testle(&o->state);  break;
    case 4: ra = ibm_if_testgt(&m->state);  rb = if_testgt(&o->state);  break;
    default: ra = ibm_if_testge(&m->state); rb = if_testge(&o->state);  break;
    }
    if (ra != rb)
        bad++;
END(if_tests)

BEGIN(npop)
    delta_loc *lm = (delta_loc *)(m->nodes + 0x300);
    delta_loc *lo = (delta_loc *)(o->nodes + 0x300);

    var_setup_at(m, o, lm, lo);
    m->vars.testing = o->vars.testing = (int8_t)(rng_next() % 2u);
    two_numbers(m, o);

    ibm_npop(&m->state, lm);
    npop(&o->state, lo);

    if (m->vars.testing) {
        *(int32_t *)(m->stack.top + 4) = 0;
        *(int32_t *)(o->stack.top + 4) = 0;
    }
    if (lm->kind != lo->kind || lm->field != lo->field)
        bad++;
END(npop)


BEGIN(ncompare_s)
    uint8_t c = (uint8_t)rng_next();

    two_numbers(m, o);
    ibm_ncompare_s(&m->state, c);
    ncompare_s(&o->state, c);
END(ncompare_s)

BEGIN(forall_to_test)
    delta_loc *am = (delta_loc *)(m->nodes + 0x300);
    delta_loc *ao = (delta_loc *)(o->nodes + 0x300);
    delta_loc *bm = (delta_loc *)(m->nodes + 0x310);
    delta_loc *bo = (delta_loc *)(o->nodes + 0x310);
    int ra, rb;

    var_setup_at(m, o, am, ao);
    bm->kind = bo->kind = am->kind;
    bm->field = bo->field = am->field;
    bm->value = bo->value = (rng_next() % 2u) ? am->value
                                              : (int32_t)rng_next();

    ra = ibm_forall_to_test(&m->state, am, bm);
    rb = forall_to_test(&o->state, ao, bo);
    if (ra != rb)
        bad++;
END(forall_to_test)

BEGIN(mark_i)
    /* Spanning a range between the two pointers needs the timing edit
       scaffold, the same one vrange_2pt is exercised on. */
    static const uint8_t modes[4] = {0xcd, 0xce, 0xcf, 0x11};
    uint8_t f;
    uint8_t mode = modes[rng_next() % 4u];
    uint8_t fld;
    void *v = (void *)(intptr_t)(rng_next() % 4u);
    int ra, rb, i;

    if (!build_time_edit(m, o, &f)) {
        free(m); free(o);
        continue;
    }
    fld = (uint8_t)(rng_next() % (uint32_t)vstmtbl[f].nfields);
    m->vars.relink = o->vars.relink = 1;
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = 0;
    for (i = 0; i < 50; i++) {
        m->stack.left_a[i] = o->stack.left_a[i] = 0;
        m->stack.left_b[i] = o->stack.left_b[i] = 0;
        m->stack.left_ans[i] = o->stack.left_ans[i] = 0;
        m->stack.left_hits[i] = o->stack.left_hits[i] = 0;
    }
    m->stack.left_next = o->stack.left_next = 0;
    m->stack.left_stamp = o->stack.left_stamp = spine_changed;

    memset(&m->state.lpta, 0, sizeof(m->state.lpta));
    memset(&o->state.lpta, 0, sizeof(o->state.lpta));
    memset(&m->state.rpta, 0, sizeof(m->state.rpta));
    memset(&o->state.rpta, 0, sizeof(o->state.rpta));
    m->state.lpta.node = (int32_t)(intptr_t)(m->nodes + 0x80);
    o->state.lpta.node = (int32_t)(intptr_t)(o->nodes + 0x80);
    m->state.rpta.node = (int32_t)(intptr_t)(m->nodes + 2 * 0x80);
    o->state.rpta.node = (int32_t)(intptr_t)(o->nodes + 2 * 0x80);
    m->state.lpta.field = o->state.lpta.field = (int8_t)f;
    m->state.rpta.field = o->state.rpta.field = (int8_t)f;
    m->state.lpta.flags = o->state.lpta.flags = (uint8_t)(rng_next() % 2u);
    m->state.rpta.flags = o->state.rpta.flags = (uint8_t)(rng_next() % 2u);

    ra = ibm_mark_i(&m->state, f, fld, v, mode);
    rb = mark_i(&o->state, f, fld, v, mode);
    if (ra != rb)
        bad++;

    /* The remembered walk keeps the nodes it visited, and the spine ends
       name nodes too; those addresses belong to the world they came from. */
    for (i = 0; i < 50; i++) {
        m->stack.left_a[i] = o->stack.left_a[i] = 0;
        m->stack.left_b[i] = o->stack.left_b[i] = 0;
    }
    m->stack.spine_l = o->stack.spine_l = 0;
    m->stack.spine_r = o->stack.spine_r = 0;
    /* The mark keeps the address of the value it is writing, and that is
       the caller's own argument slot, which is nowhere near either world. */
    m->stack.mark_fld = o->stack.mark_fld = NULL;
    memset(&m->state.lpta, 0, sizeof(m->state.lpta));
    memset(&o->state.lpta, 0, sizeof(o->state.lpta));
    memset(&m->state.rpta, 0, sizeof(m->state.rpta));
    memset(&o->state.rpta, 0, sizeof(o->state.rpta));
END(mark_i)

BEGIN(vctxt_tv)
    static const uint8_t sets[4] = {0, 1, 4, 8};
    delta_tpos pm, po;
    int8_t f = build_tspine(m, o);
    int ra, rb;

    if (f < 0) {
        free(m); free(o);
        continue;
    }
    make_tpos(m, o, f, &pm, &po, sets[rng_next() % 4u]);

    ra = ibm_vctxt_tv(&m->state, &pm);
    rb = vctxt_tv(&o->state, &po);
    if (ra != rb)
        bad++;
    else if (pm.offset != po.offset || pm.flags != po.flags)
        bad++;
    else if (pm.node - (int32_t)(intptr_t)m
             != po.node - (int32_t)(intptr_t)o)
        bad++;

    m->stack.spine_l = o->stack.spine_l = 0;
    m->stack.spine_r = o->stack.spine_r = 0;
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(vctxt_tv)


BEGIN(testeq_tvars)
    delta_loc *am = (delta_loc *)(m->nodes + 0x300);
    delta_loc *ao = (delta_loc *)(o->nodes + 0x300);
    delta_loc *bm = (delta_loc *)(m->nodes + 0x310);
    delta_loc *bo = (delta_loc *)(o->nodes + 0x310);
    int ra, rb;

    var_setup_at(m, o, am, ao);
    bm->kind = bo->kind = am->kind;
    bm->field = bo->field = am->field;
    bm->value = bo->value = (rng_next() % 2u) ? am->value
                                              : (int32_t)rng_next();

    ra = ibm_testeq_tvars(&m->state, am, bm);
    rb = testeq_tvars(&o->state, ao, bo);
    if (ra != rb)
        bad++;
END(testeq_tvars)

BEGIN(if_tests_v_i)
    delta_loc *lm = (delta_loc *)(m->nodes + 0x300);
    delta_loc *lo = (delta_loc *)(o->nodes + 0x300);
    int32_t x = (int32_t)rng_next();
    uint32_t which = rng_next() % 5u;
    int ra, rb;

    var_setup_at(m, o, lm, lo);
    m->stack.names_depth = o->stack.names_depth = 0;

    switch (which) {
    case 0:
        ra = ibm_if_testeq_v_i(&m->state, lm, x);
        rb = if_testeq_v_i(&o->state, lo, x);
        break;
    case 1:
        ra = ibm_if_testneq_v_i(&m->state, lm, x);
        rb = if_testneq_v_i(&o->state, lo, x);
        break;
    case 2:
        ra = ibm_if_testlt_v_i(&m->state, lm, x);
        rb = if_testlt_v_i(&o->state, lo, x);
        break;
    case 3:
        ra = ibm_if_testgt_v_i(&m->state, lm, x);
        rb = if_testgt_v_i(&o->state, lo, x);
        break;
    default:
        ra = ibm_if_testge_v_i(&m->state, lm, x);
        rb = if_testge_v_i(&o->state, lo, x);
        break;
    }
    if (ra != rb)
        bad++;
END(if_tests_v_i)

static void run_proj_def_mult(delta_state *d, uint8_t n, const uint8_t *str,
                              const delta_token *p, int ours)
{
    GUARDED(ours ? proj_def_mult(d, n, str, p)
                 : ibm_proj_def_mult(d, n, str, p));
}

BEGIN(proj_def_mult)
    uint8_t str[4];
    uint8_t n = (uint8_t)(rng_next() % 5u);
    delta_token tok;
    int i;

    for (i = 0; i < 4; i++)
        str[i] = (uint8_t)(rng_next() % NSTMT);

    build_pspine(m, o);
    for (i = 0; i < 4; i++) {
        ((int32_t *)(m->nodes))[15 + str[i]] |= 1;
        ((int32_t *)(o->nodes))[15 + str[i]] |= 1;
        ((int32_t *)(m->nodes + 3 * 0x80))[15 + str[i]] |= 1;
        ((int32_t *)(o->nodes + 3 * 0x80))[15 + str[i]] |= 1;
    }
    ((int32_t *)(m->nodes))[2] &= ~2;
    ((int32_t *)(o->nodes))[2] &= ~2;
    ((int32_t *)(m->nodes + 3 * 0x80))[2] &= ~2;
    ((int32_t *)(o->nodes + 3 * 0x80))[2] &= ~2;

    /* The token names the statement the projections start from, so it has
       to name one of this spine's nodes. */
    memset(&tok, 0, sizeof(tok));
    tok.value = (int32_t)(intptr_t)(m->nodes + 0x80);
    run_proj_def_mult(&m->state, n, str, &tok, 0);
    tok.value = (int32_t)(intptr_t)(o->nodes + 0x80);
    run_proj_def_mult(&o->state, n, str, &tok, 1);

    memset(&m->state.lpta, 0, sizeof(m->state.lpta));
    memset(&o->state.lpta, 0, sizeof(o->state.lpta));
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(proj_def_mult)

BEGIN(pta_ctxt)
    /* The spine is the one the context lookup needs. The pointer arrives
       already settled, because whether it can be settled at all is what
       vctxt_tv's own test covers, and an unsettled one here would only be
       walking the timing spine this scaffold does not build. */
    enum { FB = 15, NNODE = 6, STEP = 0x80 };
    uint8_t f = (uint8_t)(rng_next() % NSTMT);
    uint32_t which = rng_next() % 4u;
    delta_tpos *pm, *po;
    int i, j;

    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
    m->vars.fence_base = o->vars.fence_base = FB;
    m->state.fence_fill = o->state.fence_fill = (uint8_t)(rng_next() % 5u);
    m->vars.relink = o->vars.relink = (int32_t)(rng_next() % 2u);
    for (i = 0; i < 0x20; i++)
        m->nsqm[i] = o->nsqm[i] = (int8_t)(rng_next() % 2u);

#define NODE(w, i) ((int32_t *)((w)->nodes + (i) * STEP))
#define AT(w, i)   ((int32_t)(intptr_t)((w)->nodes + (i) * STEP))
    for (i = 0; i < NNODE; i++) {
        int lo = i > 0 ? i - 1 : 0;
        int hi = i + 1 < NNODE ? i + 1 : NNODE - 1;
        uint32_t r;

        r = rng_next();
        NODE(m, i)[0] = AT(m, lo) | 2 | (int32_t)(r & 1u);
        NODE(o, i)[0] = AT(o, lo) | 2 | (int32_t)(r & 1u);

        r = rng_next();
        NODE(m, i)[1] = AT(m, hi) | (int32_t)(r & 3u);
        NODE(o, i)[1] = AT(o, hi) | (int32_t)(r & 3u);

        r = rng_next();
        NODE(m, i)[2] = (int32_t)(r & 3u);
        NODE(o, i)[2] = (int32_t)(r & 3u);

        for (j = 0; j < 10; j++) {
            r = rng_next();
            NODE(m, i)[3 + j] = AT(m, lo) | (int32_t)(r & 1u);
            NODE(o, i)[3 + j] = AT(o, lo) | (int32_t)(r & 1u);

            r = rng_next();
            NODE(m, i)[FB + j] = AT(m, hi) | (int32_t)(r & 1u);
            NODE(o, i)[FB + j] = AT(o, hi) | (int32_t)(r & 1u);
        }

        NODE(m, i)[FB - 2] = AT(m, lo);
        NODE(o, i)[FB - 2] = AT(o, lo);
        NODE(m, i)[FB - 1] = 0;
        NODE(o, i)[FB - 1] = 0;
    }
    NODE(m, NNODE - 1)[FB + f] |= 1;
    NODE(o, NNODE - 1)[FB + f] |= 1;
    NODE(m, NNODE - 1)[2] &= ~2;
    NODE(o, NNODE - 1)[2] &= ~2;
    NODE(m, 0)[FB + f] |= 1;
    NODE(o, 0)[FB + f] |= 1;
    NODE(m, 0)[2] &= ~2;
    NODE(o, 0)[2] &= ~2;

    m->stack.spine_l = AT(m, 0);
    o->stack.spine_l = AT(o, 0);
    m->stack.spine_r = AT(m, NNODE - 1);
    o->stack.spine_r = AT(o, NNODE - 1);

    pm = (which < 2) ? &m->state.lpta : &m->state.rpta;
    po = (which < 2) ? &o->state.lpta : &o->state.rpta;
    memset(pm, 0, sizeof(*pm));
    memset(po, 0, sizeof(*po));
    pm->node = AT(m, 2);
    po->node = AT(o, 2);
    pm->field = po->field = (int8_t)f;
    pm->flags = po->flags = 1;

    switch (which) {
    case 0: ibm_lpta_ctxtl(&m->state, f); lpta_ctxtl(&o->state, f); break;
    case 1: ibm_lpta_ctxtr(&m->state, f); lpta_ctxtr(&o->state, f); break;
    case 2: ibm_rpta_ctxtl(&m->state, f); rpta_ctxtl(&o->state, f); break;
    default: ibm_rpta_ctxtr(&m->state, f); rpta_ctxtr(&o->state, f); break;
    }
#undef NODE
#undef AT

    if ((pm->node == 0) != (po->node == 0))
        bad++;
    else if (pm->node != 0
             && pm->node - (int32_t)(intptr_t)m
                != po->node - (int32_t)(intptr_t)o)
        bad++;

    m->stack.spine_l = o->stack.spine_l = 0;
    m->stack.spine_r = o->stack.spine_r = 0;
    memset(&m->state.lpta, 0, sizeof(m->state.lpta));
    memset(&o->state.lpta, 0, sizeof(o->state.lpta));
    memset(&m->state.rpta, 0, sizeof(m->state.rpta));
    memset(&o->state.rpta, 0, sizeof(o->state.rpta));
    memset(m->nodes, 0, sizeof(m->nodes));
    memset(o->nodes, 0, sizeof(o->nodes));
END(pta_ctxt)


BEGIN(calc_tables)
    /* The three that read a table: every input including the ones outside
       the range, so both ends of the clamp are exercised. */
    delta_loc in, am, ao;
    uint32_t which = rng_next() % 3u;
    int ra, rb;

    memset(&in, 0, sizeof(in));
    memset(&am, 0, sizeof(am));
    in.field = (rng_next() % 4u == 0) ? (int16_t)rng_next()
                                      : (int16_t)(rng_next() % 0x100u);
    ao = am;

    if (which == 0) {
        ra = ibm_calcETI2WPM(&m->state, &in, &am);
        rb = calcETI2WPM(&o->state, &in, &ao);
    } else if (which == 1) {
        ra = ibm_calcMidline(&m->state, &in, &am);
        rb = calcMidline(&o->state, &in, &ao);
    } else {
        ra = ibm_calcSpeedFactori(&m->state, &in, &am);
        rb = calcSpeedFactori(&o->state, &in, &ao);
    }

    if (ra != rb)
        bad++;
    else if (memcmp(&am, &ao, sizeof(am)) != 0)
        bad++;
END(calc_tables)

int main(void)
{
    setvbuf(stdout, NULL, _IONBF, 0);
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
    test_vpush_var();
    test_DELSPINE();
    test_vscanadv();
    test_spine_setters();
    test_bspush_boa_pairs();
    test_push_ptr_init();
    test_npush_scalars();
    test_vscaninit();
    test_vmove();
    test_insspine();
    test_forceErrorBacktrack();
    test_stmt_walks();
    test_vassign();
    test_npush_fld();
    test_ctxspine();
    test_vnsqflags();
    test_vinitloc_new();
    test_startloop();
    test_save_var();
    test_testFldeq();
    test_vinitflds();
    test_vscanadvOverToken();
    test_vscanadvUptoTokenOrMarker();
    test_seqscan();
    test_advance_tok();
    test_forall_cont_from();
    test_savescptr();
    test_get_parm();
    test_test_synch();
    test_test_string_i();
    test_test_string_s();
    test_ctxlook();
    test_vnormalize();
    test_vproject();
    test_timing_tests();
    test_test_ptr();
    test_lpta_walks();
    test_setscan();
    test_vgetsc();
    test_vtimept_tv();
    test_for_loop_preamble();
    test_dupsync();
    test_vdef_proj();
    test_vprt_range();
    test_forto_adv_r();
    test_forto_adv_upto_r();
    test_setd_lookup();
    test_vmark();
    test_visleft();
    test_heap_record();
    test_heap_segnum();
    test_heap_alloc();
    test_heap_free();
    test_heap_rewind();
    test_heap_objects();
    test_vcomp_pta();
    test_mashtoks();
    test_vchkseqbad();
    test_vins_sync();
    test_chkdelnonseq();
    test_fdeldel();
    test_vins_tok();
    test_vinit_stm();
    test_ins_tokens();
    test_vsplit_time();
    test_time_marks();
    test_point_edits();
    test_vrange();
    test_insert_points();
    test_vrange_2pt();
    test_two_point_edits();
    test_var_edits();
    test_reinit();
    test_rule_frame();
    test_move_i();
    test_actd_goto();
    test_while_iterate();
    test_npush_lng();
    test_npush_v();
    test_c_assvar();
    test_absoluteSyncNum();
    test_advance_strm();
    test_pta_movel();
    test_proj_def();
    test_rpta_storep();
    test_lpta_loadv();
    test_settvar();
    test_vnegative();
    test_compare_tvars();
    test_if_tests();
    test_npop();
    test_ncompare_s();
    test_forall_to_test();
    test_mark_i();
    test_vctxt_tv();
    test_testeq_tvars();
    test_if_tests_v_i();
    test_proj_def_mult();
    test_pta_ctxt();
    test_calc_tables();
    printf("delta diff: %d cases, %d mismatches\n", total_cases, total_bad);
    return total_bad != 0;
}
