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
extern int32_t num_fields_in_stream(int8_t st);
extern int32_t left_context(delta_state *d, int8_t f, int32_t at);
extern int32_t right_context(delta_state *d, int8_t f, int32_t at);
extern int allow_left_ctxt(delta_state *d, int32_t at, int8_t f,
                           int32_t stop);
extern int allow_right_ctxt(delta_state *d, int32_t at, int8_t f,
                            int32_t stop);
extern int project_sync(delta_state *d, int32_t l, int8_t f, int32_t r,
                        int32_t back);
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

                    memset(names, 0xdd, 256);
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

    return 0;
}
