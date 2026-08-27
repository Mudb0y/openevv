/* The machine's primitives, ours against IBM's, one call at a time.
 *
 * The suite cannot reach these. A primitive that no rule in the nine
 * languages IBM shipped ever calls is a primitive no sentence can exercise,
 * so speaking a case through both engines proves nothing about it -- and the
 * arithmetic, the string tests and the whole generate family are exactly
 * that. They are absent from this engine for the same reason: the link never
 * asked for them.
 *
 * So this is the differential harness put back for one purpose. The same file
 * is compiled twice: once against our engine, and once against IBM's own
 * objects, which define these under plain C names. Both print the same lines
 * for the same table of cases, and `test/prims.sh' diffs them. What is being
 * compared is what the call leaves behind -- eight bytes of an operand,
 * sixteen of each pointer register, the records it pushed -- so that a
 * primitive writing four bytes where the original wrote two is a difference
 * rather than a coincidence.
 *
 * Two families of case are left out because both engines fault on them
 * identically and the fault is the answer: a division by nought, which the
 * original announces to `divzero' and then performs anyway, and the one
 * signed division that overflows, which is the smallest long over minus one.
 *
 * vadd is in the table although it was ported long ago. It is the control:
 * if it differed, the harness would be what is wrong.
 *
 * The machine these are called on is a real one with a sentence in it, and
 * both sides build it the same way: delta_new, the command layer, the
 * streams, the language's own start rule, the text handed to the link and
 * read in. Every one of those is IBM's name and is in its objects too, which
 * is what lets one file drive both, and every step of it answers the same on
 * the two sides -- which is what says they are the same machine rather than
 * two similar ones. The variable block, the owner, the stack and the name
 * stack are the engine's own throughout.
 *
 * What is on that spine is compared without being decoded. A record holds one
 * code per character of the alphabet its statement type declares, and the
 * alphabet is the language's, so rather than spell it out the harness offers
 * every code to the string test and prints the ones that match. Two machines
 * holding the same sentence answer with the same codes, and nothing here has
 * to know what a code means.
 *
 * Where a call answers with a position rather than a number, what is printed
 * is which landmark it came back as. An address is one process's own; which
 * of the places the harness knows it is comes out the same in both.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "delta.h"
#include "eci_eloqc.h"
#include "eci_io.h"

/* The one place the two builds are not the same source. A language's tables
   are plain globals in IBM's build, linked in and reachable with no setup at
   all; in ours a build may carry several languages and every table is reached
   through whichever is in force, so ours has to say which before a primitive
   that asks the statement table is called. Everything else in this file is
   the same source on both sides. */
#ifdef EVV_PRIMS_OURS
#include "delta_lang.h"
#endif

/* What every front end calls before anything else: the static initialisers
   Microsoft's runtime used to walk. IBM's objects want them and so does
   ours. */
void evvRunStaticInitialisers(void);

/* The stream and context calls. Nothing in access.obj is declared in a
   header on either side -- its callers say extern where they use it, which
   is what the engine's own files do -- so the harness says it here. */
extern int32_t next_token(delta_state *d, int8_t f, int32_t at);
extern int32_t peekDeltaStackStart(delta_state *d);
extern int32_t peekDeltaStackNext(delta_state *d);
extern int32_t num_fields_in_stream(int8_t st);
extern int32_t left_context(delta_state *d, int8_t f, int32_t at);
extern int32_t right_context(delta_state *d, int8_t f, int32_t at);
extern int allow_left_ctxt(delta_state *d, int32_t at, int8_t f,
                           int32_t stop);
extern int allow_right_ctxt(delta_state *d, int32_t at, int8_t f,
                            int32_t stop);
extern int project_sync(delta_state *d, int32_t l, int8_t f, int32_t r,
                        int32_t back);
extern const char *first_fieldval(delta_state *d, int8_t stm, int32_t fld,
                                  const char *want);
extern const char *next_fieldval(delta_state *d);
extern void gendef_framedur(delta_state *d, delta_loc *loc);
extern void gendef_timestm(delta_state *d, uint8_t when);
extern void gendef_params(delta_state *d, uint8_t count, uint8_t n,
                          const uint8_t *str);
extern void gencur_framedur(delta_state *d, delta_loc *loc);
extern void gencur_timestm(delta_state *d, uint8_t when);
extern void gencur_params(delta_state *d, uint8_t count, uint8_t n,
                          const uint8_t *str);
extern int32_t gen_copy(delta_state *d);
extern int forall_adv_over_l(delta_state *d, int16_t tag, int16_t loop,
                             int16_t bound, uint8_t f, delta_token *tok);
extern int forall_adv_upto_l(delta_state *d, int16_t tag, int16_t loop,
                             int16_t bound, uint8_t f, delta_token *tok);
extern int forto_adv_over_l(delta_state *d, int16_t tag, int16_t loop,
                            int16_t bound, uint8_t f, delta_token *tok,
                            const delta_token *end);
extern int forto_adv_over_r(delta_state *d, int16_t tag, int16_t loop,
                            int16_t bound, uint8_t f, delta_token *tok,
                            const delta_token *end);
extern int for_cont_from(delta_state *d, int16_t tag, int16_t loop,
                         int32_t unused, delta_loc *dst,
                         const delta_loc *src);
extern void CLRNONSEQ(delta_node *t);
extern void *TVFLDS(void *p);
extern const char *streamName(int8_t st);
extern int visnonseq(delta_state *d, uint8_t f, int32_t l, int32_t r);
extern int vmergable(delta_state *d, int32_t l, int32_t r);
extern int insert_2pt(delta_state *d, uint8_t f, uint8_t n,
                      const uint8_t *str, uint8_t mode);
extern void SETCTXL(delta_state *d, int32_t *table, uint8_t idx,
                    int32_t bits);
extern void SETCTXR(delta_state *d, int32_t *table, uint8_t idx,
                    int32_t bits);
extern int unique_value(delta_state *d, int8_t f, int32_t fld,
                        const char *s, const char **out_name,
                        void **out_value);
extern int valid_prefix(int8_t f, int32_t fld, const char *s);
extern int valid_prefix_char(int8_t f, int32_t fld, char c);
extern int divide_time(delta_state *d, uint8_t f, int32_t t, int16_t off);

/* The machine, built the way the engine builds one, and the three calls the
   engine makes before it can be spoken to. All of them are IBM's own names
   and are in its objects too, which is what lets one file drive both. */
extern delta_state *delta_new(void);
extern int32_t etiwinMainDLL(delta_state *d, int32_t argc, char **argv);
extern int32_t initializeIO(delta_state *d);

/* Three of the language's rules, called by name. Ours carry the language in
   front of them, because a build may have several languages in it and they
   would otherwise collide; IBM's build has one language and no prefix. A
   rule takes the machine as a plain word, which on this side goes through
   the arena reference rather than a cast. */
#ifdef EVV_PRIMS_OURS
extern int32_t enus_DeltaProc_start(int32_t d);
extern int32_t enus_reset_sent_vars(int32_t d);
extern int32_t enus_get_tok(int32_t d);
#define PROC_START(d)       enus_DeltaProc_start(EVV_REF(d))
#define RESET_SENT_VARS(d)  enus_reset_sent_vars(EVV_REF(d))
#define GET_TOK(d)          enus_get_tok(EVV_REF(d))
#else
extern int32_t DeltaProc_start(delta_state *d);
extern int32_t reset_sent_vars(delta_state *d);
extern int32_t get_tok(delta_state *d);
#define PROC_START(d)       DeltaProc_start(d)
#define RESET_SENT_VARS(d)  reset_sent_vars(d)
#define GET_TOK(d)          get_tok(d)
#endif

/* Two operand cells, filled with a pattern that is not a plausible answer so
   that a write of the wrong width shows up in the bytes beside the value. */
static uint8_t cell_a[8];
static uint8_t cell_b[8];

static const int32_t VALUES[] = {
    0, 1, -1, 2, 3, -3, 7, -7, 100, 32767, -32768, 65535,
    100000, -100000, 2147483647, -2147483647 - 1
};
#define NVALUES ((int)(sizeof VALUES / sizeof VALUES[0]))

/* The two kinds the arithmetic handles, and three it does not, so that
   leaving the operand alone is checked as well as changing it. */
static const int16_t KINDS[] = { DK_LONG, DK_SHORT2, DK_UBYTE, DK_SHORT,
                                 DK_SYNC, 0, 3 };
#define NKINDS ((int)(sizeof KINDS / sizeof KINDS[0]))

static void load(int32_t av, int32_t bv)
{
    memset(cell_a, 0xaa, sizeof cell_a);
    memset(cell_b, 0xaa, sizeof cell_b);
    memcpy(cell_a, &av, 4);
    memcpy(cell_b, &bv, 4);
}

static void show(const char *op, int16_t ka, int16_t kb, int32_t av,
                 int32_t bv)
{
    int i;

    printf("%-16s %4d %4d %11d %11d ->", op, ka, kb, av, bv);
    for (i = 0; i < 8; i++)
        printf(" %02x", cell_a[i]);
    printf("\n");
}

/* Both pointer registers, set to a pattern that no call here writes, and the
   left one's field then set to a statement kind the language really has. That
   last is not tidiness: the immediate loads ask the table about the left
   register's field whichever register they are writing, so a left field of
   0xcc would have them index the statement table at minus fifty-two. IBM's
   build reads whatever lies there and ours faults, which is a difference in
   what is behind the table and not in the code. */
static void begin(delta_state *d, uint8_t left)
{
    memset(&d->lpta, 0xcc, sizeof d->lpta);
    memset(&d->rpta, 0xcc, sizeof d->rpta);
    d->lpta.field = (int8_t)left;
}

/* Sixteen bytes of each register after the call: the node, the field, the
   offset and the flags. */
static void show_ptas(delta_state *d, const char *op, uint8_t left, uint8_t f,
                      int32_t v)
{
    const uint8_t *l = (const uint8_t *)&d->lpta;
    const uint8_t *r = (const uint8_t *)&d->rpta;
    int i;

    printf("%-16s %3u %3u %8d ->", op, (unsigned)left, (unsigned)f, v);
    for (i = 0; i < 16; i++)
        printf(" %02x", l[i]);
    printf(" |");
    for (i = 0; i < 16; i++)
        printf(" %02x", r[i]);
    printf("\n");
}


/* The spine, said as what each node answers to rather than where it is.
 *
 * Walking it gives addresses, which are each process's own, so instead every
 * node is asked the string test for every statement kind and every one-byte
 * code, and the pairs that answer are printed. A node holding the same thing
 * on both sides answers the same pairs on both sides, and the harness never
 * has to know what a pair means. Some of them are degenerate -- a kind whose
 * field takes anything matches every code -- and they are left in, because
 * what is compared is the whole list rather than any one entry.
 *
 * The walk starts at the token the rules left rather than at the spine's own
 * end: the two are not on the same chain, which took some finding. A node is
 * marked L or R if it is one of the spine's ends and n otherwise, and one the
 * scan will not settle on at all is an x.
 */
static void show_spine(delta_state *d, const char *what, int round)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);
    int32_t at = ((const delta_token *)((const char *)d + 748))->value;
    int step;

    printf("%-16s %-6s %d ->", "spine", what, round);

    for (step = 0; at != 0 && step < 16; step++) {
        delta_token tk;
        int k, c;

        tk.unknown_00 = 0;
        tk.value = at;
        lpta_loadp(d, &tk);

        if (setscan_l(d, 1) != 0) {
            printf(" x");
        } else {
            printf(" [%s", at == s->spine_l ? "L"
                         : at == s->spine_r ? "R" : "n");
            for (k = 1; k < 10; k++)
                for (c = 0; c < 256; c++) {
                    uint8_t code = (uint8_t)c;

                    lpta_loadp(d, &tk);
                    setscan_l(d, 1);
                    if (test_string_s(d, (uint8_t)k, 1, &code) == 0)
                        printf(" %d.%d", k, c);
                }
            printf("]");
        }

        if (at == s->spine_r)
            break;
        at = next_token(d, 1, at);
    }

    printf("\n");
}

/* Which of the places the harness knows a position came back as. An address
   is one process's own; which landmark it is is the same in both. */
static const char *landmark(int32_t p, const int32_t *where)
{
    if (p == 0)
        return "none";
    if (p == where[0])
        return "token";
    if (p == where[1])
        return "left";
    if (p == where[2])
        return "right";
    return "other";
}

/* One of the two cells a generate fills, said in what it holds rather than
   where it is: the buffer is a pointer and its length and characters are
   not. */
static void show_gencell(const delta_gencell *c, const char *which, int round)
{
    uint32_t n = c->params != 0
                 ? dynaBufLength(EVV_AT(const DynaBuf *, c->params)) : 0;
    uint32_t i;

    printf("%-16s %d %-4s -> %d %d %d %02x  buf %u", "gencell", round, which,
           (int)c->value, (int)c->time, (int)c->nparams, (unsigned)c->flags,
           (unsigned)n);
    for (i = 0; i < n && i < 16; i++)
        printf(" %c", dynaBufChar(EVV_AT(const DynaBuf *, c->params), (int32_t)i));
    printf("\n");
}

static void operands(delta_operand *a, delta_operand *b, int16_t ka,
                     int16_t kb)
{
    memset(a, 0, sizeof *a);
    memset(b, 0, sizeof *b);
    a->ptr = cell_a;
    a->kind = ka;
    b->ptr = cell_b;
    b->kind = kb;
}

/* A division that would fault, on their side and on ours alike. */
static int would_fault(int16_t ka, int16_t kb, int32_t av, int32_t bv)
{
    int32_t divisor = (kb == DK_SHORT2) ? (int32_t)(int16_t)bv : bv;
    int32_t dividend = (ka == DK_SHORT2) ? (int32_t)(int16_t)av : av;

    if (kb != DK_LONG && kb != DK_SHORT2)
        return 0;
    if (ka != DK_LONG && ka != DK_SHORT2)
        return 0;
    if (divisor == 0)
        return 1;
    return divisor == -1 && dividend == (-2147483647 - 1);
}

static void binary(delta_state *d, const char *name,
                   void (*fn)(delta_state *, const delta_operand *,
                              const delta_operand *),
                   int skip_faults)
{
    int i, j, x, y;

    for (i = 0; i < NKINDS; i++)
        for (j = 0; j < NKINDS; j++)
            for (x = 0; x < NVALUES; x++)
                for (y = 0; y < NVALUES; y++) {
                    delta_operand a, b;

                    if (skip_faults
                        && would_fault(KINDS[i], KINDS[j], VALUES[x],
                                       VALUES[y]))
                        continue;

                    load(VALUES[x], VALUES[y]);
                    operands(&a, &b, KINDS[i], KINDS[j]);
                    fn(d, &a, &b);
                    show(name, KINDS[i], KINDS[j], VALUES[x], VALUES[y]);
                }
}

int main(void)
{
    delta_state *d;
    delta_vars  *vars;
    int i, j, x;

    evvRunStaticInitialisers();

#ifdef EVV_PRIMS_OURS
    {
        const delta_language *l;

        delta_lang_bind_all();
        l = delta_lang_by_id(0x10000);
        if (l == NULL) {
            /* The other side of this comparison is built from
               analysis/enus, so English is the only language it can be
               held against. */
            fprintf(stderr, "prims: this build has no US English in it\n");
            return 2;
        }
        delta_lang_set(l);
    }
#endif

    d = delta_new();
    if (d == NULL) {
        fprintf(stderr, "prims: the machine would not build\n");
        return 2;
    }
    vars = EVV_AT(delta_vars *, d->vars);

    /* And a sentence in it. The engine puts text in by starting the command
       layer, opening the streams, running the language's start rule, handing
       the text to the link and then letting the rules read it; the last of
       those is get_tok, and stopping there is what leaves a spine with the
       words on it and no synthesis done. Every step is the same call on both
       sides, which is what makes the two machines comparable rather than
       merely similar.

       What is printed is what each step answered. A step that answered
       differently would mean the machines had already parted company, and
       every case after it would be reporting that rather than the primitive
       under test. */
    {
        /* One step to a statement. Which order a compiler evaluates the
           arguments of a call in is its own business, and these have to
           happen in this order or there is no link to hand the text to. */
        int32_t a1 = etiwinMainDLL(d, 0, 0);
        int32_t a2 = initializeIO(d);
        int32_t a3 = PROC_START(d);
        int32_t a4 = eciLinkDataFromECI(ELOQ_MAINLINK(d), "ab cd. ");
        int32_t a5 = RESET_SENT_VARS(d);
        int32_t a6 = GET_TOK(d);

        printf("%-16s -> %d %d %d %d %d %d\n", "setup",
               (int)a1, (int)a2, (int)a3, (int)a4, (int)a5, (int)a6);
    }

    /* What reading the sentence in left on the spine, before anything here
       has touched it. */
    show_spine(d, "read", 0);

    binary(d, "vadd", vadd, 0);
    binary(d, "vsub", vsub, 0);
    binary(d, "vmult", vmult, 0);
    binary(d, "vdiv", vdiv, 1);

    for (i = 0; i < NKINDS; i++)
        for (x = 0; x < NVALUES; x++) {
            delta_operand a, b;

            load(VALUES[x], 0);
            operands(&a, &b, KINDS[i], DK_LONG);
            vnegate(d, &a);
            show("vnegate", KINDS[i], 0, VALUES[x], 0);
        }

    /* The tests that read what the last comparison left. A machine is a
       state with a variable block behind it and nothing else here needs to
       be true, so the block is bare memory and the one byte they read is
       written by hand. Every value a comparison can leave is tried, and two
       it cannot, since nothing in the primitives says it may not.

       test_eof is not here: it asks the logical file table, and standing one
       of those up by hand is a harness of its own rather than a case.

       test_hasval writes into the owner block, which is the one block whose
       layout is deliberately ours and not IBM's -- 64 bytes where theirs was
       nearly 500, because writing at their offsets was corrupting the arena.
       So only its answer is compared here; what it writes is the pair of
       fields src/delta_trace.c already clears in three places, which is where
       those two offsets were read off in the first place. The block is given
       room for IBM's offsets so that their write lands somewhere harmless. */
    {
        static const int8_t COMPARED[] = { -2, -1, 0, 1, 2 };
        int n;

        for (n = 0; n < 5; n++) {
            int c = COMPARED[n];

            vars->compared_equal = COMPARED[n];
            printf("%-16s %4d -> %d %d %d %d %d %d\n", "tests", c,
                   testeq(d), testneq(d), testgt(d), testge(d), testlt(d),
                   testle(d));
        }

        printf("%-16s      -> %d\n", "test_hasval", test_hasval(d));
    }

    /* The two pointer registers. Only the loads and the two ends are here:
       the moves and the context tests walk the spine, and a spine cannot be
       stood up by hand -- that wants a machine with a language and text in
       it on both sides, which is a harness of its own. Until then those are
       transcription checked by reading, and the suite catches a regression
       in the ones the shipped languages do call.

       The statement kinds tried are real ones: English declares ten, and of
       these six, kinds 1, 2 and 3 are a byte where 0, 7 and 9 are the long
       the loads write, so both arms of every switch are taken. The left
       register's field is varied against the right register's on purpose,
       since that is where the original's slip lives. */
    {
        static const uint8_t STMTS[] = { 0, 1, 2, 3, 7, 9 };
        static const int32_t IMMS[] = { 0, 1, -1, 32767, -32768, 70000,
                                        -70000 };
        int li, fi, ii;

        for (li = 0; li < (int)(sizeof STMTS / sizeof STMTS[0]); li++)
            for (fi = 0; fi < (int)(sizeof STMTS / sizeof STMTS[0]); fi++)
                for (ii = 0; ii < (int)(sizeof IMMS / sizeof IMMS[0]); ii++) {
                    uint8_t left = STMTS[li], f = STMTS[fi];
                    int32_t v = IMMS[ii];
                    delta_loc loc;

                    begin(d, left);
                    rpta_loadi(d, f, v);
                    show_ptas(d, "rpta_loadi", left, f, v);

                    begin(d, left);
                    rpta_loadl(d, f, v);
                    show_ptas(d, "rpta_loadl", left, f, v);

                    memset(&loc, 0, sizeof loc);
                    loc.kind = DK_LONG;
                    loc.value = v;
                    loc.field = (int16_t)v;

                    begin(d, left);
                    rpta_loadv(d, f, &loc);
                    show_ptas(d, "rpta_loadv.l", left, f, v);

                    loc.kind = DK_SHORT2;
                    begin(d, left);
                    rpta_loadv(d, f, &loc);
                    show_ptas(d, "rpta_loadv.s", left, f, v);

                    /* The left register's own loads read their own field, so
                       the left column would only repeat itself; they are run
                       once each rather than once per left field. */
                    if (li != 0)
                        continue;

                    begin(d, left);
                    lpta_loadi(d, f, v);
                    show_ptas(d, "lpta_loadi", left, f, v);

                    begin(d, left);
                    lpta_loadlng(d, f, v);
                    show_ptas(d, "lpta_loadlng", left, f, v);

                    loc.kind = DK_LONG;
                    begin(d, left);
                    lpta_loadv(d, f, &loc);
                    show_ptas(d, "lpta_loadv.l", left, f, v);

                    loc.kind = DK_SHORT2;
                    begin(d, left);
                    lpta_loadv(d, f, &loc);
                    show_ptas(d, "lpta_loadv.s", left, f, v);
                }

        for (fi = 0; fi < (int)(sizeof STMTS / sizeof STMTS[0]); fi++) {
            uint8_t f = STMTS[fi];

            begin(d, 0);
            lpta_leftmost(d, f);
            show_ptas(d, "lpta_leftmost", 0, f, 0);

            begin(d, 0);
            rpta_leftmost(d, f);
            show_ptas(d, "rpta_leftmost", 0, f, 0);

            begin(d, 0);
            lpta_rightmost(d, f);
            show_ptas(d, "lpta_rightmost", 0, f, 0);

            begin(d, 0);
            rpta_rightmost(d, f);
            show_ptas(d, "rpta_rightmost", 0, f, 0);
        }
    }


    /* The name stack, which is where a value waits between being pushed and
       being compared. It is eight bytes an entry -- the value, then the type
       beside it -- and a depth that starts one below the first slot. Both
       sides are given a bare block for it and the whole of what a push wrote
       is compared, so a push of the wrong width shows in the type as well as
       in the bytes.

       The four pushes differ in nothing but the type they say, and ncompare
       takes the top two off and compares them, the later push being the left
       operand. What it leaves is one byte in the variable block, which is
       what every test above reads. */
    {
        static const int32_t NV[] = { 0, 1, -1, 255, 256, 32767, -32768,
                                      65535, 70000, -70000 };
        delta_stack *st = EVV_AT(delta_stack *, d->stack);
        int8_t was = st->names_depth;
        uint8_t *names;
        int x, y, k;

        /* A machine that has never run a rule has no name stack yet -- it is
           taken when the first rule needs one -- so the harness gives it the
           one it would have had. Both sides do it the same way. */
        if (st->names == 0) {
            uint8_t *block = calloc(1, 256);

            if (block == NULL)
                return 1;
            st->names = EVV_REF(block);
        }
        names = EVV_AT(uint8_t *, st->names);

        for (k = 0; k < 4; k++)
            for (x = 0; x < (int)(sizeof NV / sizeof NV[0]); x++)
                for (y = 0; y < (int)(sizeof NV / sizeof NV[0]); y++) {
                    int i;

                    /* Only what is printed below. The block belongs to the
                       machine and its size is the machine's business; the
                       arena guard is what said so, after 256 bytes of this
                       ran over the header of the block behind it. */
                    memset(names, 0xdd, 16);
                    st->names_depth = -1;
                    vars->compared_equal = 0x7f;

                    switch (k) {
                    case 0: npush_s(d, NV[x]); npush_s(d, NV[y]); break;
                    case 1: npush_l(d, NV[x]); npush_l(d, NV[y]); break;
                    case 2: npush_i(d, NV[x]); npush_i(d, NV[y]); break;
                    default: npush_lng(d, NV[x]); npush_lng(d, NV[y]); break;
                    }

                    printf("%-16s %d %8d %8d ->", "npush", k, NV[x], NV[y]);
                    for (i = 0; i < 16; i++)
                        printf(" %02x", names[i]);

                    ncompare(d);
                    printf(" cmp %d depth %d\n",
                           (int)(int8_t)vars->compared_equal,
                           (int)st->names_depth);
                }

        /* And the two backtracks, which say only that they happened; the
           second leaves a word behind that a rule's return clears. */
        vars->unknown_11e8 = 0;
        printf("%-16s      -> %d %d\n", "back", back(d),
               (int)vars->unknown_11e8);
        vars->unknown_11e8 = 0;
        printf("%-16s      -> %d %d\n", "back_nboa", back_nboa(d),
               (int)vars->unknown_11e8);

        st->names_depth = was;
    }

    /* The spine, and what is on it.
     *
     * A rule reaches the token it is working on through the machine's own
     * cell at 748 -- that is what the wrapper the lifted rules call does,
     * `lpta_loadp' with the state plus 748 -- so the harness does the same
     * and is positioned where a rule would be. Both sides use the number,
     * because both machines are laid out the same way.
     *
     * What is on the spine cannot be read as text: a record holds one code
     * per character in the alphabet its statement type declares, and the
     * alphabet is the language's. So rather than decode it, every code is
     * offered to the string test and the ones that match are printed. Two
     * machines that hold the same sentence answer with the same codes, and
     * nothing here has to know what the codes mean.
     *
     * This is the foundation the spine-walking primitives will stand on
     * rather than a test of one: every call in it is ported already. What it
     * proves is that the two machines are the same machine. */
    {
        const delta_token *tok = (const delta_token *)((const char *)d + 748);
        int f, k, c;

        for (f = 0; f < 3; f++) {
            lpta_loadp(d, tok);
            printf("%-16s %d -> %d\n", "setscan_l", f,
                   setscan_l(d, (uint8_t)f));
        }

        for (k = 0; k < 10; k++) {
            printf("%-16s %2d ->", "matches", k);
            for (c = 0; c < 256; c++) {
                uint8_t code = (uint8_t)c;

                lpta_loadp(d, tok);
                if (setscan_l(d, 1))
                    continue;
                if (test_string_s(d, (uint8_t)k, 1, &code) == 0)
                    printf(" %d", c);
            }
            printf("\n");
        }

        /* And the same sweep through the two wide forms, which is what
           proves them. A code is spelled across two bytes or four, sign
           first, so the same character reached by a different route has to
           answer the same way -- and a code the record does not hold has to
           be refused by all three alike. The high half is swept as well as
           the low, since that is the half a single byte cannot reach. */
        for (k = 0; k < 10; k++) {
            printf("%-16s %2d ->", "matches.l", k);
            for (c = 0; c < 512; c++) {
                uint8_t pair[2];

                pair[0] = (uint8_t)(c >> 8);
                pair[1] = (uint8_t)c;
                lpta_loadp(d, tok);
                if (setscan_l(d, 1))
                    continue;
                if (test_string_l(d, (uint8_t)k, 2, pair) == 0)
                    printf(" %d", c);
            }
            printf("\n");
        }

        for (k = 0; k < 10; k++) {
            printf("%-16s %2d ->", "matches.lng", k);
            for (c = 0; c < 512; c++) {
                uint8_t quad[4];

                quad[0] = 0;
                quad[1] = 0;
                quad[2] = (uint8_t)(c >> 8);
                quad[3] = (uint8_t)c;
                lpta_loadp(d, tok);
                if (setscan_l(d, 1))
                    continue;
                if (test_string_lng(d, (uint8_t)k, 4, quad) == 0)
                    printf(" %d", c);
            }
            printf("\n");
        }

        /* The form that carries its own width. Three of its four markers
           are swept the same way as the two above -- 199 for a byte, 202 for
           two and 201 for four -- and 200 is not, because what it compares
           is a cell nothing wrote and in IBM's build that is whatever its
           stack held. A length of nothing is in as well, since there the
           call is a step over a token rather than a comparison. */
        for (k = 0; k < 10; k++) {
            static const int MARKERS[3] = { 199, 202, 201 };
            int m;

            for (m = 0; m < 3; m++) {
                printf("%-16s %2d %3d ->", "marked", k, MARKERS[m]);
                for (c = 0; c < 512; c++) {
                    uint8_t buf[5];
                    uint8_t len;

                    buf[0] = (uint8_t)MARKERS[m];
                    switch (MARKERS[m]) {
                    case 199:
                        buf[1] = (uint8_t)c;
                        len = 2;
                        break;
                    case 202:
                        buf[1] = (uint8_t)(c >> 8);
                        buf[2] = (uint8_t)c;
                        len = 3;
                        break;
                    default:
                        buf[1] = 0;
                        buf[2] = 0;
                        buf[3] = (uint8_t)(c >> 8);
                        buf[4] = (uint8_t)c;
                        len = 5;
                        break;
                    }

                    lpta_loadp(d, tok);
                    if (setscan_l(d, 1))
                        continue;
                    if (test_string(d, (uint8_t)k, len, buf) == 0)
                        printf(" %d", c);
                }
                printf("\n");
            }

            lpta_loadp(d, tok);
            printf("%-16s %2d -> %d\n", "marked.none", k,
                   setscan_l(d, 1) ? -1
                                   : test_string(d, (uint8_t)k, 0, 0));
        }

        /* A negative code, which is the sign bit rather than a large one, and
           a two-token string, which is what makes the loop run twice. */
        for (k = 0; k < 10; k++) {
            uint8_t neg[2];
            uint8_t two[4];
            int r1, r2;

            neg[0] = 0x80;
            neg[1] = 65;
            lpta_loadp(d, tok);
            r1 = setscan_l(d, 1) ? -1 : test_string_l(d, (uint8_t)k, 2, neg);

            two[0] = 0;
            two[1] = 65;
            two[2] = 0;
            two[3] = 66;
            lpta_loadp(d, tok);
            r2 = setscan_l(d, 1) ? -1 : test_string_l(d, (uint8_t)k, 4, two);

            printf("%-16s %2d -> %d %d\n", "wide.odds", k, r1, r2);
        }
    }

    /* The type check is the one call here that reads the machine, and all it
       reads is how many statement types the language declares. Nought, one
       and five say what the answer does at each side of that bound. */
    {
        static const uint8_t NSTMTS[] = { 0, 1, 5 };
        int n;

        for (n = 0; n < 3; n++) {
            d->nstmts = NSTMTS[n];
            for (i = 0; i < NKINDS; i++)
                for (j = 0; j < NKINDS; j++) {
                    delta_operand a, b;

                    load(0, 0);
                    operands(&a, &b, KINDS[i], KINDS[j]);
                    printf("%-16s %4d %4d %11d %11d -> %d\n",
                           "vcompareTypeCheck", KINDS[i], KINDS[j],
                           (int)NSTMTS[n], 0,
                           (int)vcompareTypeCheck(d, &a, &b));
                }
        }
    }

    /* The two that hold the scan where it stands, which are the last cases
       here because they are the only ones that leave the machine somewhere
       else: each pushes two records on the backtracking stack and one of them
       holds the scan. What is compared is the answer, what the scan was left
       holding, how far the stack moved -- a difference, since the addresses
       themselves are each process's own -- and the fence marks. */
    {
        const delta_token *tok = (const delta_token *)((const char *)d + 748);
        delta_stack *s = EVV_AT(delta_stack *, d->stack);
        static const uint8_t CHARS[3] = { 0, 1, 2 };
        int t;

        for (t = 0; t < 6; t++) {
            int32_t before = s->top;
            int rc, i;

            lpta_loadp(d, tok);
            if (setscan_l(d, 1)) {
                printf("%-16s %d -> no scan\n", "held", t);
                continue;
            }

            switch (t) {
            case 0:  rc = test_time(d, 0);                    break;
            case 1:  rc = test_time(d, 4242);                 break;
            case 2:  rc = test_time(d, -1);                   break;
            case 3:  rc = test_fence(d, 7, 0, 0);             break;
            case 4:  rc = test_fence(d, 8, 1, CHARS);         break;
            default: rc = test_fence(d, 9, 3, CHARS);         break;
            }

            {
                /* The two records the call left: the scan's own, and the
                   context record above it carrying the tag. Their kinds and
                   the tag are content rather than addresses, so they compare;
                   the scan bytes beside them do not, and are left alone. */
                const uint8_t *low = EVV_AT(const uint8_t *, s->top);
                const delta_frame *scan = (const delta_frame *)low;
                const delta_frame *ca =
                    (const delta_frame *)(low + s->size_b0);

                printf("%-16s %d -> %d  scan %d %d %d  stack %d"
                       "  recs %d %d %d  marks",
                       "held", t, rc, (int)vars->scan_field,
                       (int)vars->scan_rev, (int)vars->scan_held,
                       (int)(before - s->top),
                       (int)scan->kind, (int)ca->kind, (int)ca->value);
            }
            for (i = 0; i < 12; i++)
                printf(" %d",
                       (int)EVV_AT(const uint8_t *, d->fence_marks)[i]);
            printf("\n");
        }

        /* And a walk over the records those two calls just pushed, which is
           the only thing here that reads the backtracking stack rather than
           writing it. */
        {
            delta_stack *st = EVV_AT(delta_stack *, d->stack);
            int step;

            /* Start sets the walk to the top and steps once, and answers
               where that left it rather than where it began -- the
               original's doing, and the reason this reads the field rather
               than the answer. The walk stops at the bottom the machine
               says, because past that is not a record and what is there is
               each process's own. */
            printf("%-16s top %d start %d ->", "stackwalk",
                   (int)*EVV_AT(const int8_t *, st->top),
                   (int)(peekDeltaStackStart(d) - st->top));
            for (step = 0; step < 12; step++) {
                int32_t here = st->walk;
                int32_t was;

                int kind;

                if (here == 0)
                    break;

                /* The walk stops at the first thing that is not a record
                   kind rather than at a position, because a position past
                   the live records is each process's own and what is
                   written there is not a record at all. */
                kind = (int)*EVV_AT(const int8_t *, here);
                if (kind < 0 || kind > 8)
                    break;

                printf(" %d", kind);
                was = peekDeltaStackNext(d);
                printf(":%d", (int)(st->walk - was));
            }
            printf("\n");
        }
    }

    /* The stream and context calls, which are what a text rule reaches the
       spine through. Three of them answer with a position rather than a
       number, and a position is one process's address and not the other's,
       so what is printed is which landmark it came back as: the node asked
       about, one of the spine's two ends, or nothing. That is content, and
       it compares. */
    {
        const delta_token *tok = (const delta_token *)((const char *)d + 748);
        delta_stack *s = EVV_AT(delta_stack *, d->stack);
        int32_t where[3];
        int i, j, f;

        where[0] = tok->value;
        where[1] = s->spine_l;
        where[2] = s->spine_r;

        /* The names a field can take, walked out in full. This one wants no
           spine at all -- the list is in the field's own descriptor -- so it
           is the one place here where a whole answer can be printed rather
           than a landmark. The prefixes are the empty one, which lets every
           name through, a letter, and a run of dashes, which is the arm that
           answers with whatever the field calls its undefined value. */
        {
            static const char *WANT[4] = { "", "a", "-", "--" };
            int w, fld;

            for (fld = 0; fld < 3; fld++)
                for (w = 0; w < 4; w++) {
                    /* The prefix is a pointer the machine holds in a value,
                       so it has to come from somewhere the arena can name.
                       strdup is the arena's on our side and the C library's
                       on IBM's, which is what one source needs. */
                    char *want = strdup(WANT[w]);
                    const char *name;
                    int count = 0;

                    if (want == NULL)
                        return 1;

                    printf("%-16s %d %d ->", "fieldvals", fld, w);
                    for (name = first_fieldval(d, 1, fld, want);
                         name != 0 && count < 40;
                         name = next_fieldval(d)) {
                        printf(" %s", name);
                        count++;
                    }
                    printf("\n");
                }

            /* And with no prefix at all, which is not the same as an empty
               one: the walk has nothing to compare against and the flag
               beside it stays clear. */
            {
                const char *name;
                int count = 0;

                printf("%-16s ->", "fieldvals.null");
                for (name = first_fieldval(d, 1, 0, 0);
                     name != 0 && count < 40;
                     name = next_fieldval(d)) {
                    printf(" %s", name);
                    count++;
                }
                printf("\n");
            }
        }

        /* And the same list asked whether a string names one of its values
           and only one. What comes back is the name it settled on and, where
           the field is named rather than numbered, which index that is; both
           are content. A string that two names begin with has to be refused,
           which is the whole difference from the call beside it. */
        {
            static const char *TRY[10] = {
                "", "a", "e", "u", "un", "und", "lo", "l", "-", "--"
            };
            int t, fld;

            for (fld = 0; fld < 3; fld++)
                for (t = 0; t < 10; t++) {
                    const char *name = 0;
                    void *value = 0;
                    int rc;

                    rc = unique_value(d, 1, fld, TRY[t], &name, &value);
                    printf("%-16s %d %-3s -> %d", "unique", fld, TRY[t], rc);
                    if (rc) {
                        printf(" %s", name != 0 ? name : "(none)");
                        if (value != 0)
                            printf(" %d", (int)*(const int16_t *)value);
                    }
                    printf("\n");
                }
        }

        /* Whether a string could yet become a value, and whether a
           character could yet start one. Every printable character is
           offered to the second, which is the whole of what it can be
           asked. */
        {
            static const char *TRY[10] = {
                "", "a", "e", "u", "un", "und", "lo", "l", "-", "--"
            };
            int t, fld, ch;

            for (fld = 0; fld < 4; fld++) {
                printf("%-16s %d ->", "prefix", fld);
                for (t = 0; t < 10; t++)
                    printf(" %d", valid_prefix(1, fld, TRY[t]));
                printf("\n");

                printf("%-16s %d ->", "prefix.char", fld);
                for (ch = 32; ch < 127; ch++)
                    if (valid_prefix_char(1, fld, (char)ch))
                        printf(" %c", ch);
                printf("\n");
            }
        }

        /* What each statement type is called, which is the language's own
           word for it and therefore the plainest content in this file. */
        /* Whether a run is a plain sequence, and whether two marks may be
           joined. Both answer yes or no over the positions the harness has,
           and the second is asked about every pair of them because the pair
           it refuses is the spine's own two ends. merge itself is not asked:
           it backtracks on a refusal, and a backtrack wants a rule under it
           that this harness does not have. */
        {
            int32_t both = vars->ctx_both;
            int k;

            /* Twice: once as the machine stands, and once with it looking
               both ways, which is the only state in which two marks are
               ever refused. */
            for (k = 0; k < 2; k++) {
                vars->ctx_both = k;

                for (i = 0; i < 3; i++)
                    for (j = 0; j < 3; j++) {
                        int f;

                        printf("%-16s %d %d %d ->", "nonseq", k, i, j);
                        for (f = 0; f < 3; f++)
                            printf(" %d", visnonseq(d, (uint8_t)f, where[i],
                                                    where[j]));
                        printf("  mergable %d\n",
                               vmergable(d, where[i], where[j]));
                    }
            }

            vars->ctx_both = both;
        }

        printf("%-16s ->", "names");
        for (i = 0; i < 10; i++)
            printf(" %s", streamName((int8_t)i));
        printf("\n");

        /* The two accessors beside it: one clears a bit in a node and the
           other hands back what it was given. The node used is the token
           the rules left, and what is printed is its three own words before
           and after -- content, since a node's flags are not addresses. */
        {
            delta_node *t = (delta_node *)(intptr_t)tok->value;

            /* The link word is a pointer with two flag bits at the bottom,
               so only the bits are printed: the rest is one process's own
               address and would differ for that reason alone. */
            printf("%-16s -> %08x %d %08x", "clrnonseq",
                   (unsigned)t->flags0, (int)(t->link & 3),
                   (unsigned)t->flags8);
            CLRNONSEQ(t);
            printf(" then %08x %d %08x  tvflds %d\n",
                   (unsigned)t->flags0, (int)(t->link & 3),
                   (unsigned)t->flags8,
                   (int)((char *)TVFLDS(t) - (char *)t));
        }

        printf("%-16s ->", "nfields");
        for (i = 0; i < 10; i++)
            printf(" %d", (int)num_fields_in_stream((int8_t)i));
        printf("\n");

        /* Only the token, and only two fields. Two things are worth saying
           plainly about how far this reaches. Asking the spine's ends about
           a field they do not carry walks off the end of what is there, on
           both sides alike, so the harness does not ask. And at the token
           every field carries its mark, so both context calls answer with
           the node they were given and the walk under them is not reached:
           putting sync_to_right where sync_to_left belongs changes nothing
           here, which was checked rather than assumed.

           What that wants is a spine with statements on it, and get_tok
           leaves two nodes rather than a sentence. Running the pipeline
           through to the end would give one, and needs an output for the
           samples to go to; that is the next piece of this harness rather
           than something it does now. */
        for (f = 0; f < 2; f++) {
            int32_t l = left_context(d, (int8_t)f, where[0]);
            int32_t r = right_context(d, (int8_t)f, where[0]);

            printf("%-16s %d -> %s %s\n", "context", f,
                   landmark(l, where), landmark(r, where));
        }

        for (j = 0; j < 3; j++)
            for (f = 0; f < 2; f++)
                printf("%-16s %d %d -> %d %d\n", "allow", j, f,
                       allow_left_ctxt(d, where[0], (int8_t)f, where[j]),
                       allow_right_ctxt(d, where[0], (int8_t)f, where[j]));

        /* init_stream is not here. It tears a stream down and builds it
           again, which on a machine with a sentence in it leaves nothing
           for the next call to stand on -- both engines fall over the same
           way, and a case that only shows they crash alike is not worth
           the harness carrying it. Its body is two lines and is checked by
           reading. */

        /* Neither is project_sync nor divide_time, for a reason worth
           writing down. Each of them is a guard and then a call into the
           machine -- vproj_l, vproj_r, vsplit_time -- and the only positions
           this harness has to offer are the ones the sentence left. IBM's
           own vsplit_time faults on those where ours answers, so the two
           cannot be compared there; which of them is right is not something
           a harness that cannot get past the fault can settle. The guards
           are transcribed and read, and what they call was ported long ago
           and is exercised by every case in the suite. */
    }

    /* The four walks over a run of tokens, and the second name the machine
       has for continuing one. What is compared is what each answered and
       what it left the scan holding; where a loop leaves its token is a
       position, so it is said as a landmark. The spine here is two nodes,
       so a walk has little room; three of the four start and the fourth
       does not, and both engines say the same about each.

       What is not shown here is the difference between stopping at the far
       end and stepping over it: on a spine this short the second look finds
       what the first did, so putting one spelling's body under the other's
       name changes nothing. Running these after the inserts would give a
       longer spine, and there one of them does not come back at all -- so
       the harness asks them on the short one, and the mirror they are of a
       ported pair is two lines wide. */
    {
        const delta_token *tok = (const delta_token *)((const char *)d + 748);
        delta_stack *s = EVV_AT(delta_stack *, d->stack);
        delta_token *out = calloc(1, sizeof *out);
        delta_token *end = calloc(1, sizeof *end);
        int32_t where[3];
        int k;

        if (out == NULL || end == NULL)
            return 1;

        where[0] = tok->value;
        where[1] = s->spine_l;
        where[2] = s->spine_r;
        end->value = s->spine_r;

        for (k = 0; k < 4; k++) {
            int rc;

            /* The token is where the loop starts as well as where it
               reports back, so it has to name a position for the walk to
               begin at all. */
            out->value = tok->value;
            lpta_loadp(d, tok);
            if (setscan_l(d, 1)) {
                printf("%-16s %d -> no scan\n", "loop", k);
                continue;
            }

            switch (k) {
            case 0: rc = forall_adv_over_l(d, 3, 4, 5, 1, out); break;
            case 1: rc = forall_adv_upto_l(d, 3, 4, 5, 1, out); break;
            case 2: rc = forto_adv_over_l(d, 3, 4, 5, 1, out, end); break;
            default: rc = forto_adv_over_r(d, 3, 4, 5, 1, out, end); break;
            }

            printf("%-16s %d -> %d  scan %d %d  tok %s  tags %d %d\n",
                   "loop", k, rc, (int)vars->scan_field, (int)vars->scan_rev,
                   landmark(out->value, where), (int)vars->loop_tag,
                   (int)vars->test_tag);
        }

        /* And the two names for continuing from a value, which are the same
           bytes in the original. */
        {
            delta_loc *dst = calloc(1, sizeof *dst);
            delta_loc *src = calloc(1, sizeof *src);

            if (dst == NULL || src == NULL)
                return 1;

            dst->kind = DK_LONG;
            dst->field = -1;
            dst->value = 0;
            src->kind = DK_LONG;
            src->field = -1;
            src->value = 0x9876;

            printf("%-16s -> %d %d %d\n", "cont_from",
                   for_cont_from(d, 7, 8, 0, dst, src),
                   (int)dst->value, (int)vars->loop_tag);
        }
    }

    /* Collecting a frame. The three parts arrive one at a time and each
       sets its own bit; which of the two cells they land in is decided by
       the byte the generate statement starts with, so the harness sets that
       byte and watches where the part goes. What is printed is the cell --
       the frame, the moment, how many parameters, the flags -- and the
       parameter buffer's length and contents, which are content rather than
       an address and so compare.

       The order matters for the copy at the end: the far cell is filled
       first, because vgen_copy resets that cell's buffer without ever making
       one, and on a cell that has never been filled there is nothing there
       to reset. That is the original's own slip, transcribed as it runs;
       src/delta.c says what it was meant to say. */
    {
        uint8_t *marker = calloc(1, 4);
        uint8_t *params = calloc(1, 8);
        int32_t *value = calloc(1, 4);
        int round;

        if (marker == NULL || params == NULL || value == NULL)
            return 1;

        params[0] = 'p';
        params[1] = 'q';
        params[2] = 'r';
        *value = 0x4321;

        vars->gen_stmt = EVV_REF(marker);
        vars->gen_src.ptr = EVV_REF(value);
        vars->gen_src.kind = DK_SHORT2;
        vars->gen_src.flag = 0;
        vars->gen_nparams = 3;

        for (round = 0; round < 2; round++) {
            /* Nought matches none of the three markers, so the first round
               fills the far cell and the second the near one. */
            vars->gen_len = (uint8_t)(round == 0 ? 5 : 6);

            *marker = (uint8_t)(round == 0 ? 0 : 0xc3);
            printf("%-16s %d frame  -> %d\n", "gen", round, (int)vgen_frame(d));

            *marker = (uint8_t)(round == 0 ? 0 : 0xc4);
            printf("%-16s %d time   -> %d\n", "gen", round, (int)vgen_time(d));

            *marker = (uint8_t)(round == 0 ? 0 : 0xc5);
            vars->gen_at = EVV_REF(params);
            printf("%-16s %d params -> %d\n", "gen", round,
                   (int)vgen_params(d));

            show_gencell(&vars->gen_now, "now", round);
            show_gencell(&vars->gen_done, "done", round);
            printf("%-16s %d -> read %d\n", "gen.at", round,
                   (int)(EVV_AT(const uint8_t *, vars->gen_at) - params));
        }

        /* The walk over the backtracking stack, which is the one thing here
           that reads the records the calls above left. What is printed is
           each record's kind and how far the walk moved -- a difference,
           since the positions themselves are each process's own -- and the
           two engines have to agree on both. The records are whatever the
           cases before this left behind, which is a real stack rather than
           one built for the occasion. */
        printf("%-16s -> %d\n", "gen.copy", (int)vgen_copy(d));
        show_gencell(&vars->gen_now, "now", 2);
        show_gencell(&vars->gen_done, "done", 2);

        /* And the same three parts said the way a rule says them, where
           which cell is filled is not read off the statement but written
           into the name: gendef fills the one being collected and gencur the
           one already finished. The frame arrives as a long here rather than
           a short, so the value is the whole of what the location holds. */
        {
            delta_loc *loc = calloc(1, sizeof *loc);

            if (loc == NULL)
                return 1;

            loc->kind = DK_LONG;
            loc->field = 0;
            loc->value = 0x1357;

            gendef_timestm(d, 11);
            gendef_params(d, 12, 3, params);
            gendef_framedur(d, loc);
            show_gencell(&vars->gen_now, "now", 3);

            loc->kind = DK_LONG;
            loc->value = 0x2468;
            gencur_timestm(d, 13);
            gencur_params(d, 14, 2, params);
            gencur_framedur(d, loc);
            show_gencell(&vars->gen_done, "done", 3);

            printf("%-16s -> %d\n", "gen.copy2", (int)gen_copy(d));
            show_gencell(&vars->gen_now, "now", 4);
            show_gencell(&vars->gen_done, "done", 4);
        }

        /* And the whole of a generate statement written out, which is the
           one thing in the machine that produces text rather than spine.
           The cell is built for the occasion: the moment measured in field
           one, a frame fifty long, and a parameter program of one statement
           type with one of its fields. What can be compared is what it
           answers and what it leaves in the cell -- the lines themselves go
           to a logical file, and on a machine with two nodes on its spine
           there is no span to walk, so the answer is the head of it and the
           first pass rather than the frames. */
        {
            const delta_token *tok =
                (const delta_token *)((const char *)d + 748);
            delta_stack *stk = EVV_AT(delta_stack *, d->stack);
            delta_loc *loc = calloc(1, sizeof *loc);
            uint8_t *prog = calloc(1, 8);
            delta_token from, to;

            if (loc == NULL || prog == NULL)
                return 1;

            prog[0] = 1;
            prog[1] = 1;
            prog[2] = 1;
            prog[3] = 0;

            loc->kind = DK_LONG;
            loc->field = 0;
            loc->value = 50;

            gencur_timestm(d, 1);
            gencur_params(d, 0, 4, prog);
            gencur_framedur(d, loc);

            from.unknown_00 = 0;
            to.unknown_00 = 0;
            from.value = tok->value;
            to.value = stk->spine_r;
            lpta_loadp(d, &from);
            rpta_loadp(d, &to);

            {
                int32_t where[3];
                int     rc;

                where[0] = tok->value;
                where[1] = stk->spine_l;
                where[2] = stk->spine_r;

                rc = (int)vgen(d, &d->lpta, &d->rpta, &vars->gen_done, 0);

                /* What the first pass settled is the observable half of it:
                   the two marks each statement type's value is to be read
                   between, said as landmarks. */
                printf("%-16s -> %d  ends %s %s  reg %d %d\n", "vgen", rc,
                       landmark(EVV_AT(const int32_t *, stk->expr_l)[1],
                                where),
                       landmark(EVV_AT(const int32_t *, stk->expr_r)[1],
                                where),
                       (int)d->lpta.field, (int)d->lpta.flags);
            }
            show_gencell(&vars->gen_done, "done", 5);
        }

        /* Where a context begins and where it ends. The table is the
           harness's own rather than a node's, because what is being compared
           is what the call writes into it: the bottom two bits of the entry
           are kept and the rest replaced, and which entry depends on which
           of the two names was used. */
        {
            int32_t *table = calloc(64, sizeof *table);
            int i, k;

            if (table == NULL)
                return 1;

            for (k = 0; k < 2; k++) {
                for (i = 0; i < 64; i++)
                    table[i] = 0x30 + i;

                for (i = 0; i < 4; i++) {
                    if (k == 0)
                        SETCTXL(d, table, (uint8_t)i, 0x100 << i);
                    else
                        SETCTXR(d, table, (uint8_t)i, 0x100 << i);
                }

                printf("%-16s %d ->", "setctx", k);
                for (i = 0; i < 28; i++)
                    printf(" %x", (unsigned)table[i]);
                printf("\n");
            }
        }
    }

    /* Putting tokens on the spine, in each of the four widths a string can
       be written in. These come last of everything, because unlike the rest
       they change what is there: each inserts between the token the rules
       left and the spine's right end, and the sweep after it says what the
       position holds now. The four widths spell the same kind of code
       differently, so what lands is the test of the decode.

       Only that one pair of positions is used. With both registers on the
       same node the insert is refused -- an answer, and the same one on both
       sides -- and with the right register at the spine's left end both
       engines fall over, which is a shape the harness has no business
       asking for. */
    {
        const delta_token *tok = (const delta_token *)((const char *)d + 748);
        delta_stack *s = EVV_AT(delta_stack *, d->stack);
        delta_token from, to;
        uint8_t first = 0;
        int v;

        from.unknown_00 = 0;
        to.unknown_00 = 0;

        for (v = 0; v < 5; v++) {
            uint8_t buf[4];
            int rc, c;

            from.value = tok->value;
            to.value = v == 4 ? tok->value : s->spine_r;
            lpta_loadp(d, &from);
            rpta_loadp(d, &to);

            switch (v) {
            case 0:
                buf[0] = 7;
                rc = insert_2pt_s(d, 1, 1, buf, 0);
                break;
            case 1:
                buf[0] = 0;
                buf[1] = 8;
                rc = insert_2pt_i(d, 1, 2, buf, 0);
                break;
            case 2:
                buf[0] = 0;
                buf[1] = 9;
                rc = insert_2pt_l(d, 1, 2, buf, 0);
                break;
            case 3:
                buf[0] = 0;
                buf[1] = 0;
                buf[2] = 0;
                buf[3] = 10;
                rc = insert_2pt_lng(d, 1, 4, buf, 0);
                break;
            default:
                /* Both registers on one node: refused. */
                buf[0] = 0;
                buf[1] = 11;
                rc = insert_2pt_l(d, 1, 2, buf, 0);
                break;
            }

            /* What each call answered, the two readable positions at the
               head of the spine, and then the whole spine as show_spine
               says it.

               That last one is what says where the tokens went. The four
               widths leave the spine in four different shapes -- two
               settleable nodes, an alternating run of eight, two the scan
               will not settle on at all -- and both engines agree on each.

               What it still does not see is the value a wide insert
               decodes: the nodes it makes answer no string test, of either
               width, at any statement kind, so putting the two-byte decode
               under the four-byte name passes here. That was tried rather
               than assumed. Those two arms are read; the two the suite
               exercises on every sentence it speaks are the byte and the
               short. */
            printf("%-16s %d -> %d  first", "insert", v, rc);
            for (c = 0; c < 256; c++) {
                uint8_t code = (uint8_t)c;

                lpta_loadp(d, tok);
                if (setscan_l(d, 1))
                    continue;
                if (test_string_s(d, 1, 1, &code) == 0) {
                    printf(" %d", c);
                    first = (uint8_t)c;
                }
            }

            printf("  then");
            for (c = 0; c < 256; c++) {
                uint8_t pair[2];

                pair[0] = first;
                pair[1] = (uint8_t)c;
                lpta_loadp(d, tok);
                if (setscan_l(d, 1))
                    continue;
                if (test_string_s(d, 1, 2, pair) == 0)
                    printf(" %d", c);
            }
            printf("\n");

            /* And the whole spine after each one, which is what says where
               the tokens went rather than only that the call was allowed. */
            show_spine(d, "insert", v);
        }
    }

    /* One position becoming another everywhere the machine wrote it down.
       The values are made up rather than real positions, because nothing in
       the call dereferences one: it compares and replaces. So a word
       variable and two pushed locations are given the same made-up value,
       and what is printed is which of the three moved. The word variable is
       the language's own, so it is put back afterwards. */
    {
        delta_loc *a = calloc(1, sizeof *a);
        delta_loc *b = calloc(1, sizeof *b);
        int32_t   *cell = EVV_AT(int32_t **, d->word)[0];
        int32_t    held = *cell;

        if (a == NULL || b == NULL)
            return 1;

        push_ptr_init(d, a);
        push_ptr_init(d, b);
        a->value = 0x1234;
        b->value = 0x4321;
        *cell = 0x1234;

        set_saved_ptrs(d, 0x1234, 0x5678);

        printf("%-16s -> %x %x %x  depth %d %d\n", "saved_ptrs",
               (unsigned)a->value, (unsigned)b->value, (unsigned)*cell,
               (int)vars->ptr_count, (int)vars->active_record);

        *cell = held;
    }

    /* And the read of the backtracking stack that lays what is on it down
       as tokens. It is last of everything because it pops what it reads and
       relinks the spine.

       Two rounds. The first asks it with the stack as the cases above left
       it, where the record on top is the floor marker: nothing to lay, so
       it refuses. The second builds it the stack it wants -- a bottom
       marker, then two pushed values -- and then it lays a token for each,
       makes a mark between them, and stops on the marker. That second round
       is the one that reaches vins_tok and vins_sync, and the spine after
       it is what says the tokens went where the original puts them.

       What it does not say is what value went in. Reading the record one
       byte over changes nothing printed, and neither does pushing different
       values: the nodes it makes answer the same string tests either way.
       That was tried rather than assumed, and it is the same blind spot the
       wide inserts have. So the position is compared and the content is
       not. */
    {
        const delta_token *tok = (const delta_token *)((const char *)d + 748);
        delta_stack *s = EVV_AT(delta_stack *, d->stack);
        int round;

        for (round = 0; round < 2; round++) {
            int32_t l = tok->value;
            int32_t r = s->spine_r;
            int     rc;

            if (round == 1) {
                bspush_vbot(d);
                npush_s(d, 12);
                npush_s(d, 13);
            }

            rc = ins_rdtoks(d, 1, l, r, 0);
            printf("%-16s %d -> %d  kind %d\n", "ins_rdtoks", round, rc,
                   (int)*EVV_AT(const int8_t *, s->top));
            show_spine(d, "ins_rdtoks", round);
        }
    }

    return 0;
}
