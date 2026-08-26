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
 * compared is the byte pattern the call leaves behind, eight bytes of it, so
 * that a primitive writing four bytes where the original wrote two is a
 * difference rather than a coincidence.
 *
 * Two families of case are left out because both engines fault on them
 * identically and the fault is the answer: a division by nought, which the
 * original announces to `divzero' and then performs anyway, and the one
 * signed division that overflows, which is the smallest long over minus one.
 *
 * vadd is in the table although it was ported long ago. It is the control:
 * if it differed, the harness would be what is wrong.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "delta.h"

/* The one place the two builds are not the same source. A language's tables
   are plain globals in IBM's build, linked in and reachable with no setup at
   all; in ours a build may carry several languages and every table is reached
   through whichever is in force, so ours has to say which before a primitive
   that asks the statement table is called. The machine itself is bare memory
   on both sides. */
#ifdef EVV_PRIMS_OURS
#include "delta_lang.h"
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
    /* Room for the named fields and a language's cells behind them; nothing
       here runs a rule, so nought in all of it is a machine that answers. */
    delta_state *d = calloc(1, 0x2000);
    delta_vars *vars = calloc(1, 0x1400);
    int i, j, x;

    if (d == NULL || vars == NULL)
        return 1;

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
        void *owner = calloc(1, 0x400);
        int n;

        if (owner == NULL)
            return 1;
        d->vars = EVV_REF(vars);
        d->owner = EVV_REF(owner);

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
        delta_stack *st = calloc(1, 0x1000);
        uint8_t *names = calloc(1, 256);
        int x, y, k;

        if (st == NULL || names == NULL)
            return 1;
        st->names = EVV_REF(names);
        d->stack = EVV_REF(st);

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

    return 0;
}
