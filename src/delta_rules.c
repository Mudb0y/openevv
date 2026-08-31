/* The interpreter for the language's rules.

   A rule is a byte stream of operations over operands, produced by
   tools/delta-emit.py from what the language's own compiler generated. The
   machine it runs on is the one that code was written for: eight registers,
   the four condition flags, a frame of bytes addressed from a base, and
   calls out to the runtime. Nothing here is a translation into something
   nicer; that comes later, once this is known to be exact.

   The frame is one buffer with the base part way up it, because the code was
   compiled that way: locals below the base, the rule's own arguments above.
   An offset is signed and reaches either side.

   One operation is not a call at all although it looks like one. A rule
   plants a landing place for a backtrack by calling setjmp, and a call made
   from here would land back in this function rather than in the rule, so it
   is taken as an operation of its own and the landing place is this
   function's. Everything the interpreter needs afterwards therefore lives in
   one block whose address has escaped, so that a landing does not find it
   stale. */

#include <setjmp.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "delta.h"
#include "delta_rules_c.h"
#include "evv_land.h"
#include "evv_arena.h"

/* Every table below belongs to a language, and which language is what the
   machine says. delta_run_rule sets it from the machine it was handed and
   puts back what was there, so everything under it -- the interpreter, a
   rule written as C, and every primitive either calls -- reads the right
   one without being told. Written as names rather than as reaches so that
   the interpreter reads as it did when there was only ever one language. */
#define L                      (delta_lang_now())
#define delta_rules            (L->rules)
#define delta_rule_count       (L->rule_count)
#define delta_rule_code        (L->rule_code)
#define delta_rule_imm         (L->rule_imm)
#define delta_rule_map         (L->rule_map)
#define delta_rule_entry       (L->rule_entry)
#define delta_rule_entry_name  (L->rule_entry_name)
#define delta_rule_setjmp      (L->rule_setjmp)
#define delta_rule_native      (L->rule_native)
#define DELTA_RULE_FRAME_MAX   (L->frame_max)

enum {
    OP_CALL, OP_JUMP, OP_BRANCH, OP_CMP, OP_ALU2, OP_ALU1, OP_LOAD,
    OP_STORE, OP_SWITCH, OP_MAP, OP_RETURN, OP_SCALE, OP_ADDK, OP_MUL,
    OP_DIV, OP_WIDEN, OP_SETCC, OP_PUSH, OP_SETARG, OP_POPN, OP_POPREG,
    OP_FTOL
};

/* The argument area is kept as it was rather than worked out per call. The
   compiler pushes an argument once and lets two paths spend it, writes over
   a slot an earlier call left behind rather than pushing again, and clears
   several calls' worth at once, so anything that tries to say which values
   belong to which call gets it wrong sooner or later. */
#define NARG 64

enum {
    K_NONE, K_IMM, K_SYM, K_SLOT, K_SLOTADDR, K_STATE, K_STATEFLD,
    K_REG, K_IND
};

enum { M_MOVL, M_MOVW, M_MOVB, M_MOVSWL, M_MOVZWL, M_MOVSBL, M_MOVZBL };

#define NREG 8

extern int delta_rule_trace;

/* Which rule is running, so that a run can be told about in the same terms
   as a run of the original: only the calls that leave the object they were
   compiled in can be seen there, because the others were renamed along with
   the definitions they reach. */
static const delta_rule *delta_rule_here;

typedef struct {
    int32_t        reg[NREG];
    int32_t        arg[NARG];
    int            argn;
    unsigned char *base;          /* the frame base: offset zero */
    void          *state;
    const uint8_t *code;          /* the rule's own first byte */
    int32_t        pc;
    int32_t        answer;
    int            done;
    delta_flags    fl;
} interp;

/* The flags are in src/delta_flags.h, written as inline. They were here,
   and by rights they belong here -- they are the machine's arithmetic rather
   than any language's -- but a rule written as C makes the same comparisons
   and has to make them the same way, and out of line the compiler could not
   see that most of what it worked out was never read. */

/* ---- registers ------------------------------------------------------- */

static int32_t reg_read(const interp *st, unsigned char code)
{
    int32_t v = st->reg[code & 7];

    switch (code >> 4) {
    case 1: return (int32_t)((uint32_t)v & 0xffffu);
    case 2: return (int32_t)((uint32_t)v & 0xffu);
    case 3: return (int32_t)(((uint32_t)v >> 8) & 0xffu);
    default: return v;
    }
}

static void reg_write(interp *st, unsigned char code, int32_t v)
{
    int32_t *p = &st->reg[code & 7];

    switch (code >> 4) {
    case 1:
        *p = (int32_t)(((uint32_t)*p & 0xffff0000u) | ((uint32_t)v & 0xffffu));
        break;
    case 2:
        *p = (int32_t)(((uint32_t)*p & 0xffffff00u) | ((uint32_t)v & 0xffu));
        break;
    case 3:
        *p = (int32_t)(((uint32_t)*p & 0xffff00ffu)
                       | (((uint32_t)v & 0xffu) << 8));
        break;
    default:
        *p = v;
        break;
    }
}

/* ---- operands -------------------------------------------------------- */

static uint16_t get16(const uint8_t *p)
{
    return (uint16_t)(p[0] | (p[1] << 8));
}

static int32_t get16s(const uint8_t *p)
{
    return (int32_t)(int16_t)get16(p);
}

/* Where a jump goes: an offset from the start of the rule, so never negative.
   Read as signed it wrapped at 32,767, and English's longest rule is 30,929
   bytes -- within six per cent of that and never over it, which is why this
   held for three languages. French of France has a rule of 33,075 bytes and
   Canadian French one of 34,154, and both jumped to a negative place and took
   the machine apart. Nothing about the bytecode changes: the emitter always
   wrote a position, and only the reading of it was wrong. */
static int32_t get16to(const uint8_t *p)
{
    return (int32_t)get16(p);
}

static int32_t operand_read(interp *st, const uint8_t **pp, int w, int sext);

/* An operand's address, for the ones that name a place rather than a
   value. Answers null for the ones that do not. */
static unsigned char *operand_place(interp *st, const uint8_t **pp)
{
    const uint8_t *p = *pp;
    int kind = *p++;
    unsigned char *at = 0;

    switch (kind) {
    case K_SLOT:
        at = st->base + get16s(p);
        p += 2;
        break;
    case K_STATEFLD:
        at = (unsigned char *)st->state + get16s(p);
        p += 2;
        break;
    case K_IND: {
        const uint8_t *q = p;
        int32_t inner = operand_read(st, &q, 4, 0);

        at = (unsigned char *)(intptr_t)inner + get16s(q);
        p = q + 2;
        break;
    }
    case K_IMM:
    case K_SYM:
    case K_SLOTADDR:
    case K_STATE:
        p += 2;
        break;
    case K_REG:
        p += 1;
        break;
    default:
        break;
    }
    *pp = p;
    return at;
}

/* An operand read as a value, at the width the operation works in. A place
   is read through; anything else stands for itself. */
static int32_t operand_read(interp *st, const uint8_t **pp, int w, int sext)
{
    const uint8_t *p = *pp;
    int kind = *p;
    int32_t v = 0;

    switch (kind) {
    case K_NONE:
        *pp = p + 1;
        return 0;
    case K_IMM:
        v = delta_rule_imm[get16(p + 1)];
        *pp = p + 3;
        return v;
    case K_SYM:
        v = delta_sym_ref[get16(p + 1)];
        *pp = p + 3;
        return v;
    case K_SLOTADDR:
        v = EVV_REF((st->base + get16s(p + 1)));
        *pp = p + 3;
        return v;
    case K_STATE:
        v = EVV_REF(((unsigned char *)st->state + get16s(p + 1)));
        *pp = p + 3;
        return v;
    case K_REG:
        v = reg_read(st, p[1]);
        *pp = p + 2;
        return v;
    default:
        break;
    }

    {
        unsigned char *at = operand_place(st, pp);

        if (at == 0)
            return 0;
        if (w == 1)
            return sext ? (int32_t)*(const signed char *)at
                        : (int32_t)*(const unsigned char *)at;
        if (w == 2) {
            int16_t half;

            memcpy(&half, at, 2);
            return sext ? (int32_t)half : (int32_t)(uint16_t)half;
        }
        memcpy(&v, at, 4);
        return v;
    }
}

static void operand_skip(interp *st, const uint8_t **pp)
{
    const uint8_t *p = *pp;

    switch (*p) {
    case K_NONE: *pp = p + 1; return;
    case K_REG:  *pp = p + 2; return;
    case K_IND: {
        const uint8_t *q = p + 1;

        operand_skip(st, &q);
        *pp = q + 2;
        return;
    }
    default: *pp = p + 3; return;
    }
}

/* ---- the runtime ----------------------------------------------------- */

/* What a rule pushes is a value, and a value is thirty-two bits. Some of the
   entries it calls declare a pointer where the rule pushed one, so every
   argument is widened on the way in: an entry that wants a value takes the
   low half back and an entry that wants a pointer gets the whole of it. The
   two are the same thing only where a pointer is four bytes. */
typedef uintptr_t evv_word;
#define W(x) ((evv_word)(uint32_t)(x))

typedef evv_word (*I0)(void);
typedef evv_word (*I1)(evv_word);
typedef evv_word (*I2)(evv_word, evv_word);
typedef evv_word (*I3)(evv_word, evv_word, evv_word);
typedef evv_word (*I4)(evv_word, evv_word, evv_word, evv_word);
typedef evv_word (*I5)(evv_word, evv_word, evv_word, evv_word, evv_word);
typedef evv_word (*I6)(evv_word, evv_word, evv_word, evv_word, evv_word, evv_word);
typedef evv_word (*I7)(evv_word, evv_word, evv_word, evv_word, evv_word, evv_word,
                      evv_word);
typedef evv_word (*I8)(evv_word, evv_word, evv_word, evv_word, evv_word, evv_word,
                      evv_word, evv_word);
typedef evv_word (*I9)(evv_word, evv_word, evv_word, evv_word, evv_word, evv_word,
                      evv_word, evv_word, evv_word);
typedef evv_word (*I10)(evv_word, evv_word, evv_word, evv_word, evv_word, evv_word,
                       evv_word, evv_word, evv_word, evv_word);
typedef evv_word (*I11)(evv_word, evv_word, evv_word, evv_word, evv_word, evv_word,
                       evv_word, evv_word, evv_word, evv_word, evv_word);
typedef evv_word (*I12)(evv_word, evv_word, evv_word, evv_word, evv_word, evv_word,
                       evv_word, evv_word, evv_word, evv_word, evv_word, evv_word);
typedef evv_word (*IN)(evv_word, evv_word, evv_word, evv_word, evv_word, evv_word,
                      evv_word, evv_word, evv_word, evv_word, evv_word, evv_word,
                      evv_word, evv_word, evv_word, evv_word, evv_word, evv_word,
                      evv_word, evv_word, evv_word, evv_word, evv_word, evv_word,
                      evv_word);

#define A W(a[0]), W(a[1]), W(a[2]), W(a[3]), W(a[4]), W(a[5]), W(a[6]), \
          W(a[7]), W(a[8]), W(a[9]), W(a[10]), W(a[11]), W(a[12]), \
          W(a[13]), W(a[14]), W(a[15]), W(a[16]), W(a[17]), W(a[18]), \
          W(a[19]), W(a[20]), W(a[21]), W(a[22]), W(a[23]), W(a[24])

/* Calling with more arguments than the entry declares is what the original
   never has to do; here the number is only known at run time, so the common
   arities are called exactly and the rare long ones go through one wide
   signature. Every entry is cdecl, so the extra words are simply not read. */
static int32_t call_entry(delta_rule_fn fn, const int32_t *a, int n)
{
    switch (n) {
    case 0:  return ((I0)fn)();
    case 1:  return ((I1)fn)(W(a[0]));
    case 2:  return ((I2)fn)(W(a[0]), W(a[1]));
    case 3:  return ((I3)fn)(W(a[0]), W(a[1]), W(a[2]));
    case 4:  return ((I4)fn)(W(a[0]), W(a[1]), W(a[2]), W(a[3]));
    case 5:  return ((I5)fn)(W(a[0]), W(a[1]), W(a[2]), W(a[3]), W(a[4]));
    case 6:  return ((I6)fn)(W(a[0]), W(a[1]), W(a[2]), W(a[3]), W(a[4]), W(a[5]));
    case 7:  return ((I7)fn)(W(a[0]), W(a[1]), W(a[2]), W(a[3]), W(a[4]), W(a[5]), W(a[6]));
    case 8:  return ((I8)fn)(W(a[0]), W(a[1]), W(a[2]), W(a[3]), W(a[4]), W(a[5]), W(a[6]), W(a[7]));
    case 9:  return ((I9)fn)(W(a[0]), W(a[1]), W(a[2]), W(a[3]), W(a[4]), W(a[5]), W(a[6]), W(a[7]),
                             W(a[8]));
    case 10: return ((I10)fn)(W(a[0]), W(a[1]), W(a[2]), W(a[3]), W(a[4]), W(a[5]), W(a[6]), W(a[7]),
                              W(a[8]), W(a[9]));
    case 11: return ((I11)fn)(W(a[0]), W(a[1]), W(a[2]), W(a[3]), W(a[4]), W(a[5]), W(a[6]), W(a[7]),
                              W(a[8]), W(a[9]), W(a[10]));
    case 12: return ((I12)fn)(W(a[0]), W(a[1]), W(a[2]), W(a[3]), W(a[4]), W(a[5]), W(a[6]), W(a[7]),
                              W(a[8]), W(a[9]), W(a[10]), W(a[11]));
    default: return ((IN)fn)(A);
    }
}

/* ---- the loop -------------------------------------------------------- */

#define MAXARG 32

static void step(interp *st)
{
    const uint8_t *p = st->code + st->pc;
    int op = *p++;

    switch (op) {
    case OP_CALL: {
        int32_t a[MAXARG];
        uint16_t which = get16(p);
        int n, i;

        p += 2;
        n = *p++;
        {
            int want = *p++;

            if (delta_rule_trace && want != st->argn && want < 255)
                fprintf(stderr, "# %s: %d in the area, %d expected\n",
                        delta_rule_entry_name[which], st->argn, want);
        }
        (void)a;
        (void)i;
        st->reg[0] = delta_rule_called(which, st->arg, st->argn, n);
        break;
    }

    case OP_PUSH: {
        int32_t v = operand_read(st, &p, 4, 0);

        if (st->argn < NARG)
            st->arg[st->argn] = v;
        st->argn++;
        break;
    }

    case OP_SETARG: {
        int k = *p++;
        int32_t v = operand_read(st, &p, 4, 0);
        int at = st->argn - 1 - k;

        if (at >= 0 && at < NARG)
            st->arg[at] = v;
        break;
    }

    case OP_POPN:
        st->argn -= *p++;
        if (st->argn < 0)
            st->argn = 0;
        break;

    case OP_POPREG: {
        unsigned char code = *p++;

        if (st->argn > 0) {
            st->argn--;
            if (st->argn < NARG)
                reg_write(st, code, st->arg[st->argn]);
        }
        break;
    }

    case OP_JUMP:
        st->pc = get16to(p);
        return;

    case OP_BRANCH: {
        int cond = *p++;
        int32_t to = get16to(p);

        p += 2;
        if (delta_condition(&st->fl, cond)) {
            st->pc = to;
            return;
        }
        break;
    }

    case OP_CMP: {
        int kind = *p++;
        int w = (kind == CMP_TESTB || kind == CMP_CMPB) ? 1
            : (kind == CMP_TESTW || kind == CMP_CMPW) ? 2 : 4;
        uint32_t a = (uint32_t)operand_read(st, &p, w, 0);
        uint32_t b = (uint32_t)operand_read(st, &p, w, 0);

        delta_rule_cmp(&st->fl, kind, (int32_t)a, (int32_t)b);
        break;
    }

    case OP_ALU2:
    case OP_ALU1: {
        int kind = *p++;
        int w = alu_width[kind];
        uint32_t a = 0;
        const uint8_t *q;
        uint32_t b;
        int32_t out;

        if (op == OP_ALU2)
            a = (uint32_t)operand_read(st, &p, w, 0);
        else if (kind == A_SHLL || kind == A_SHLW || kind == A_SARL
                 || kind == A_SARW)
            a = 1;   /* a shift written with one operand shifts by one */

        /* The answer goes back where the second operand came from, so that
           one is both read and written. */
        q = p;
        b = (uint32_t)operand_read(st, &p, w, 0);

        out = delta_rule_alu(&st->fl, kind, (int32_t)a, (int32_t)b);
        {
            const uint8_t *w2 = q;

            if (*w2 == K_REG) {
                reg_write(st, w2[1], out);
            } else {
                unsigned char *place = operand_place(st, &w2);

                if (place != 0) {
                    if (w == 2)
                        memcpy(place, &out, 2);
                    else
                        memcpy(place, &out, 4);
                }
            }
        }
        break;
    }

    case OP_LOAD: {
        int kind = *p++;
        int w = (kind == M_MOVL) ? 4
            : (kind == M_MOVB || kind == M_MOVSBL || kind == M_MOVZBL) ? 1 : 2;
        int sext = (kind == M_MOVSWL || kind == M_MOVSBL);
        int32_t v = operand_read(st, &p, w, sext);
        unsigned char code = *p++;

        if (kind == M_MOVSWL || kind == M_MOVZWL || kind == M_MOVSBL
            || kind == M_MOVZBL)
            code &= 0x0f;   /* the answer fills the whole register */
        reg_write(st, code, v);
        break;
    }

    case OP_STORE: {
        int kind = *p++;
        int w = (kind == M_MOVL) ? 4 : (kind == M_MOVB) ? 1 : 2;
        int32_t v = operand_read(st, &p, w, 0);
        unsigned char *at = operand_place(st, &p);

        if (at != 0)
            memcpy(at, &v, (size_t)w);
        if (delta_rule_trace > 1)
            fprintf(stderr, "# store %d at %08x = %08x\n", w,
                    (unsigned)(size_t)at, (unsigned)v);
        break;
    }

    case OP_SWITCH: {
        int32_t idx = operand_read(st, &p, 4, 0);
        uint16_t n = get16(p);

        p += 2;
        if (idx >= 0 && idx < (int32_t)n) {
            st->pc = get16to(p + 2 * idx);
            return;
        }
        p += 2 * n;
        break;
    }

    case OP_MAP: {
        uint16_t table = get16(p);
        int32_t idx;
        unsigned char code;

        p += 2;
        idx = operand_read(st, &p, 4, 0);
        code = *p++;
        reg_write(st, (unsigned char)(code & 0x0f),
                  (int32_t)delta_rule_map[table + idx]);
        break;
    }

    case OP_RETURN:
        st->answer = operand_read(st, &p, 4, 0);
        st->done = 1;
        return;

    case OP_SCALE: {
        int32_t disp = delta_rule_imm[get16(p)];
        int32_t base, index;
        int scale;
        unsigned char code;

        p += 2;
        base = operand_read(st, &p, 4, 0);
        index = operand_read(st, &p, 4, 0);
        scale = *p++;
        code = *p++;
        reg_write(st, code, disp + base + index * scale);
        break;
    }

    /* A little floating point, which only the Frenches use: two rules in
       France's module and eight in Canada's. An integer is pushed, a double
       constant or another integer is combined into it, and the result is
       truncated towards zero into a register -- which is what __ftol2 does.

       It is worked out in long double because that is the x87 register the
       original computes in, and the difference is not academic: with the
       constant 0.4, an input of 5 and an addend of -3, sixty-four bit
       arithmetic keeps 2.0 exactly and truncates to -1, where the eighty-bit
       register keeps 2.000000000000000111 and truncates to 0. Two of two
       point nine million combinations differ, and this is them. A host whose
       long double is no wider than double would take the first answer. */
    case OP_FTOL: {
        long double acc = 0;
        unsigned char steps = *p++;
        unsigned char code;
        unsigned char i;

        for (i = 0; i < steps; i++) {
            unsigned char what = *p++;
            union { uint64_t bits; double d; } k;

            switch (what) {
            case 0:
                acc = (long double)operand_read(st, &p, 4, 1);
                break;
            case 1:
                acc += (long double)operand_read(st, &p, 4, 1);
                break;
            case 2:
            case 3:
                k.bits = (uint32_t)delta_rule_imm[get16(p)];
                p += 2;
                k.bits |= (uint64_t)(uint32_t)delta_rule_imm[get16(p)] << 32;
                p += 2;
                if (what == 2)
                    acc *= (long double)k.d;
                else
                    acc += (long double)k.d;
                break;
            default:
                break;
            }
        }
        code = *p++;
        reg_write(st, code, (int32_t)acc);
        break;
    }

    case OP_ADDK: {
        int32_t k = delta_rule_imm[get16(p)];
        int32_t v;
        unsigned char code;

        p += 2;
        v = operand_read(st, &p, 4, 0);
        code = *p++;
        reg_write(st, code, v + k);
        break;
    }

    case OP_MUL: {
        int kind = *p++;
        int w = (kind == A_IMULW) ? 2 : 4;
        int32_t a = operand_read(st, &p, w, 1);
        int32_t b = operand_read(st, &p, w, 1);
        unsigned char code = *p++;

        reg_write(st, code, (int32_t)((uint32_t)a * (uint32_t)b));
        break;
    }

    case OP_DIV: {
        int32_t by;
        int64_t num;

        p++;
        by = operand_read(st, &p, 4, 0);
        if (by != 0) {
            num = ((int64_t)st->reg[2] << 32) | (uint32_t)st->reg[0];
            st->reg[0] = (int32_t)(num / by);
            st->reg[2] = (int32_t)(num % by);
        }
        break;
    }

    case OP_WIDEN:
        p++;
        st->reg[2] = st->reg[0] >> 31;
        break;

    case OP_SETCC: {
        int cond = *p++;
        unsigned char code = *p++;

        reg_write(st, (unsigned char)(0x20 | (code & 0x0f)),
                  delta_condition(&st->fl, cond) ? 1 : 0);
        break;
    }

    default:
        st->answer = 0;
        st->done = 1;
        return;
    }

    st->pc = (int32_t)(p - st->code);
}

/* One call to a primitive, with the arity known where the call is written.
 *
 * Every call a rule makes used to go through delta_rule_called, which cleared
 * a scratch array of twenty-five words, copied the arguments into it off the
 * back of the machine's argument area, and then read the arity again in
 * call_entry to pick which signature to make the call under. That last one
 * was an indirect branch through a jump table on every call, and between them
 * the two were a fifth of a run.
 *
 * A rule written as C knows its arity where it stands, so it says which of
 * these it wants and the whole of that goes: no array, no copy, no branch on
 * the arity. What is left is reading the arguments off the top of the
 * argument area, in the order the entry takes them -- the last thing pushed
 * is the first argument -- and making the call.
 *
 * Two things send it back to the old path. Tracing wants the arguments
 * written out, which no rule should pay for; and an argument area shallower
 * than the call asks for is a place the compiled code and the interpreter
 * disagree about, which delta_rule_called answers with noughts as it always
 * has. Both are the one test at the top.
 *
 * All twenty-six arities are here whether or not English uses them. Which a
 * language wants is the language's business, and a missing one would be a
 * link error in the middle of somebody else's build.
 */
int32_t delta_call_0(int which, const int32_t *stack, int argn)
{
    if (delta_rule_trace != 0)
        return delta_rule_called(which, stack, argn, 0);
    return (int32_t)((I0)delta_rule_entry[which])();
}

int32_t delta_call_1(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 1)
        return delta_rule_called(which, stack, argn, 1);
    return (int32_t)((I1)delta_rule_entry[which])(W(t[-1]));
}

int32_t delta_call_2(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 2)
        return delta_rule_called(which, stack, argn, 2);
    return (int32_t)((I2)delta_rule_entry[which])(W(t[-1]), W(t[-2]));
}

int32_t delta_call_3(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 3)
        return delta_rule_called(which, stack, argn, 3);
    return (int32_t)((I3)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]));
}

int32_t delta_call_4(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 4)
        return delta_rule_called(which, stack, argn, 4);
    return (int32_t)((I4)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]));
}

int32_t delta_call_5(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 5)
        return delta_rule_called(which, stack, argn, 5);
    return (int32_t)((I5)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]));
}

int32_t delta_call_6(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 6)
        return delta_rule_called(which, stack, argn, 6);
    return (int32_t)((I6)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]));
}

int32_t delta_call_7(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 7)
        return delta_rule_called(which, stack, argn, 7);
    return (int32_t)((I7)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]));
}

int32_t delta_call_8(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 8)
        return delta_rule_called(which, stack, argn, 8);
    return (int32_t)((I8)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]));
}

int32_t delta_call_9(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 9)
        return delta_rule_called(which, stack, argn, 9);
    return (int32_t)((I9)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]));
}

int32_t delta_call_10(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 10)
        return delta_rule_called(which, stack, argn, 10);
    return (int32_t)((I10)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]));
}

int32_t delta_call_11(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 11)
        return delta_rule_called(which, stack, argn, 11);
    return (int32_t)((I11)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]));
}

int32_t delta_call_12(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 12)
        return delta_rule_called(which, stack, argn, 12);
    return (int32_t)((I12)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]));
}

int32_t delta_call_13(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 13)
        return delta_rule_called(which, stack, argn, 13);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

int32_t delta_call_14(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 14)
        return delta_rule_called(which, stack, argn, 14);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

int32_t delta_call_15(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 15)
        return delta_rule_called(which, stack, argn, 15);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), 0, 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

int32_t delta_call_16(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 16)
        return delta_rule_called(which, stack, argn, 16);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), W(t[-16]), 0, 0, 0, 0, 0, 0, 0, 0, 0);
}

int32_t delta_call_17(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 17)
        return delta_rule_called(which, stack, argn, 17);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), W(t[-16]), W(t[-17]), 0, 0, 0, 0, 0, 0, 0, 0);
}

int32_t delta_call_18(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 18)
        return delta_rule_called(which, stack, argn, 18);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), W(t[-16]), W(t[-17]), W(t[-18]), 0, 0, 0, 0, 0, 0, 0);
}

int32_t delta_call_19(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 19)
        return delta_rule_called(which, stack, argn, 19);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), W(t[-16]), W(t[-17]), W(t[-18]), W(t[-19]), 0, 0, 0, 0, 0, 0);
}

int32_t delta_call_20(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 20)
        return delta_rule_called(which, stack, argn, 20);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), W(t[-16]), W(t[-17]), W(t[-18]), W(t[-19]), W(t[-20]), 0, 0, 0, 0, 0);
}

int32_t delta_call_21(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 21)
        return delta_rule_called(which, stack, argn, 21);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), W(t[-16]), W(t[-17]), W(t[-18]), W(t[-19]), W(t[-20]), W(t[-21]), 0, 0, 0, 0);
}

int32_t delta_call_22(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 22)
        return delta_rule_called(which, stack, argn, 22);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), W(t[-16]), W(t[-17]), W(t[-18]), W(t[-19]), W(t[-20]), W(t[-21]), W(t[-22]), 0, 0, 0);
}

int32_t delta_call_23(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 23)
        return delta_rule_called(which, stack, argn, 23);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), W(t[-16]), W(t[-17]), W(t[-18]), W(t[-19]), W(t[-20]), W(t[-21]), W(t[-22]), W(t[-23]), 0, 0);
}

int32_t delta_call_24(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 24)
        return delta_rule_called(which, stack, argn, 24);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), W(t[-16]), W(t[-17]), W(t[-18]), W(t[-19]), W(t[-20]), W(t[-21]), W(t[-22]), W(t[-23]), W(t[-24]), 0);
}

int32_t delta_call_25(int which, const int32_t *stack, int argn)
{
    const int32_t *t = stack + argn;

    if (delta_rule_trace != 0 || argn < 25)
        return delta_rule_called(which, stack, argn, 25);
    /* Past twelve there is one wide signature, as in call_entry:
       every entry is cdecl, so the words it does not declare are
       simply not read. */
    return (int32_t)((IN)delta_rule_entry[which])(W(t[-1]), W(t[-2]), W(t[-3]), W(t[-4]), W(t[-5]), W(t[-6]), W(t[-7]), W(t[-8]), W(t[-9]), W(t[-10]), W(t[-11]), W(t[-12]), W(t[-13]), W(t[-14]), W(t[-15]), W(t[-16]), W(t[-17]), W(t[-18]), W(t[-19]), W(t[-20]), W(t[-21]), W(t[-22]), W(t[-23]), W(t[-24]), W(t[-25]));
}

/* And the same for a wrapper written out where it stood. Its arguments are
   already in the order the entry takes them, so there is nothing to read off
   the argument area and nothing to reverse -- and nothing may be taken off it
   either, because the pushes the site made for its own call are still
   standing above. English wants two to five of them; the rest are here for
   the same reason as above. */
int32_t delta_direct_1(int which, int32_t a0)
{
    if (delta_rule_trace != 0) {
        int32_t v[1];

        v[0] = a0;
        return delta_rule_direct(which, v, 1);
    }
    return (int32_t)((I1)delta_rule_entry[which])(W(a0));
}

int32_t delta_direct_2(int which, int32_t a0, int32_t a1)
{
    if (delta_rule_trace != 0) {
        int32_t v[2];

        v[0] = a0;
        v[1] = a1;
        return delta_rule_direct(which, v, 2);
    }
    return (int32_t)((I2)delta_rule_entry[which])(W(a0), W(a1));
}

int32_t delta_direct_3(int which, int32_t a0, int32_t a1, int32_t a2)
{
    if (delta_rule_trace != 0) {
        int32_t v[3];

        v[0] = a0;
        v[1] = a1;
        v[2] = a2;
        return delta_rule_direct(which, v, 3);
    }
    return (int32_t)((I3)delta_rule_entry[which])(W(a0), W(a1), W(a2));
}

int32_t delta_direct_4(int which, int32_t a0, int32_t a1, int32_t a2, int32_t a3)
{
    if (delta_rule_trace != 0) {
        int32_t v[4];

        v[0] = a0;
        v[1] = a1;
        v[2] = a2;
        v[3] = a3;
        return delta_rule_direct(which, v, 4);
    }
    return (int32_t)((I4)delta_rule_entry[which])(W(a0), W(a1), W(a2), W(a3));
}

int32_t delta_direct_5(int which, int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4)
{
    if (delta_rule_trace != 0) {
        int32_t v[5];

        v[0] = a0;
        v[1] = a1;
        v[2] = a2;
        v[3] = a3;
        v[4] = a4;
        return delta_rule_direct(which, v, 5);
    }
    return (int32_t)((I5)delta_rule_entry[which])(W(a0), W(a1), W(a2), W(a3), W(a4));
}

int32_t delta_direct_6(int which, int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5)
{
    if (delta_rule_trace != 0) {
        int32_t v[6];

        v[0] = a0;
        v[1] = a1;
        v[2] = a2;
        v[3] = a3;
        v[4] = a4;
        v[5] = a5;
        return delta_rule_direct(which, v, 6);
    }
    return (int32_t)((I6)delta_rule_entry[which])(W(a0), W(a1), W(a2), W(a3), W(a4), W(a5));
}

int32_t delta_direct_7(int which, int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6)
{
    if (delta_rule_trace != 0) {
        int32_t v[7];

        v[0] = a0;
        v[1] = a1;
        v[2] = a2;
        v[3] = a3;
        v[4] = a4;
        v[5] = a5;
        v[6] = a6;
        return delta_rule_direct(which, v, 7);
    }
    return (int32_t)((I7)delta_rule_entry[which])(W(a0), W(a1), W(a2), W(a3), W(a4), W(a5), W(a6));
}

int32_t delta_direct_8(int which, int32_t a0, int32_t a1, int32_t a2, int32_t a3, int32_t a4, int32_t a5, int32_t a6, int32_t a7)
{
    if (delta_rule_trace != 0) {
        int32_t v[8];

        v[0] = a0;
        v[1] = a1;
        v[2] = a2;
        v[3] = a3;
        v[4] = a4;
        v[5] = a5;
        v[6] = a6;
        v[7] = a7;
        return delta_rule_direct(which, v, 8);
    }
    return (int32_t)((I8)delta_rule_entry[which])(W(a0), W(a1), W(a2), W(a3), W(a4), W(a5), W(a6), W(a7));
}

#ifdef EVV_ARG_CHECK
/* A rule pushing past the argument area the decompiler said it needs. Said
   once per rule, because one such rule says it a great many times. */
void evv_arg_over(const char *who, int argn, int room)
{
    static const char *said[64];
    static int n;
    int i;

    for (i = 0; i < n; i++)
        if (said[i] == who)
            return;
    if (n < 64)
        said[n++] = who;
    fprintf(stderr, "argument area: %s wants %d, has %d\n", who, argn + 1,
            room);
}
#endif

/* A running count of what the interpreter has been asked to do, for
   finding out where a run stops rather than for the port itself. */
long delta_rule_calls;
long delta_rule_steps;
/* Which rule is running, so that a run can be told about in the same terms
   as a run of the original: only the calls that leave the object they were
   compiled in can be seen there, because the others were renamed along with
   the definitions they reach. */

int delta_rule_trace = -1;
static long delta_rule_limit;

/* Every call a rule makes, from the interpreter and from a rule written as C
   alike, so that a run says the same thing about itself either way. */
int32_t delta_rule_called(int which, const int32_t *stack, int argn, int want)
{
    int32_t a[MAXARG];
    int i;

    memset(a, 0, sizeof(a));
    /* The last thing pushed is the first argument. */
    for (i = 0; i < want && i < MAXARG; i++)
        a[i] = (argn - 1 - i >= 0) ? stack[argn - 1 - i] : 0;

    if (delta_rule_trace && delta_rule_here != 0
        && strcmp(delta_rule_entry_name[which], "backtrack_function") == 0) {
        fprintf(stderr, "# %s dispatches\n", delta_rule_here->name);
        fflush(stderr);
    }
    if (delta_rule_trace > 1) {
        int j;

        fprintf(stderr, "  %s(", delta_rule_entry_name[which]);
        for (j = 0; j < want && j < MAXARG; j++)
            fprintf(stderr, "%s%08x", j ? ", " : "", (unsigned)a[j]);
        fprintf(stderr, ")\n");
        fflush(stderr);
    }
    return call_entry(delta_rule_entry[which], a, want);
}

/* A primitive called with the arguments written out, rather than with
   whatever is on the argument stack. An inlined wrapper needs this: the site's
   own pushes must stay untouched, because nothing pops them and the call after
   this one reads down through them. Arguments here are in the order the entry
   takes them, not the order a machine would have pushed them. */
int32_t delta_rule_direct(int which, const int32_t *a, int n)
{
    if (delta_rule_trace > 1) {
        int j;

        fprintf(stderr, "  %s(", delta_rule_entry_name[which]);
        for (j = 0; j < n && j < MAXARG; j++)
            fprintf(stderr, "%s%08x", j ? ", " : "", (unsigned)a[j]);
        fprintf(stderr, ")\n");
        fflush(stderr);
    }
    return call_entry(delta_rule_entry[which], a, n);
}

static void delta_rule_report(void)
{
    if (delta_rule_trace > 0)
        fprintf(stderr, "rules run: %ld, steps: %ld\n",
                delta_rule_calls, delta_rule_steps);
}

/* The machine itself, kept apart from what runs a rule so that a rule written
   as C does not pay for a frame and an interpreter it will never use. The
   thread this runs on has sixty-four kilobytes, and the rules nest deeply
   enough that paying twice runs out of it. */
#ifndef EVV_NO_BYTECODE

static int32_t run_bytecode(void *state, const delta_rule *r,
                            const int32_t *args, int nargs)
{
    unsigned char *frame = evv_frame_push(DELTA_RULE_FRAME_MAX);
    volatile int depth = 0;
    volatile int at = 0;
    volatile int planted = 0;
    interp st;
    int i;

    if (frame == 0)
        return 0;
    memset(frame, 0, DELTA_RULE_FRAME_MAX);
    memset(&st, 0, sizeof(st));
    st.base = frame + r->frame;
    st.state = state;
    st.code = delta_rule_code + r->offset;
    st.pc = 0;

    for (i = 0; i < nargs && i < r->params; i++)
        memcpy(st.base + r->pbase + 4 * i, &args[i], 4);

    while (!st.done) {
        const uint8_t *p = st.code + st.pc;

        /* The one entry that is not a call the interpreter can make on the
           rule's behalf: a landing place has to be planted in this frame,
           not in the runtime's. */
        if (*p == OP_CALL && (int)get16(p + 1) == delta_rule_setjmp) {
            int32_t buf = (st.argn > 0) ? st.arg[st.argn - 1] : 0;

            st.pc = (int32_t)(p + 5 - st.code);
            /* Landing here again puts the stack pointer back where it was,
               but not what the interpreter had written into its own frame
               since: by then the rule has run on and both the argument area
               and the place it had reached belong to wherever the backtrack
               came from. A landing is a return to just after this call with
               nothing else moved, so both go back by hand. */
            depth = st.argn;
            at = st.pc;
            st.reg[0] = EVV_LAND_SAVE((intptr_t)buf);
            st.argn = depth;
            st.pc = at;
            planted = 1;
            continue;
        }
        step(&st);
        delta_rule_steps++;
    }

    /* The frame goes back for the next rule to have, so any landing planted
       in it stops being one. */
    if (planted)
        evv_land_forget((uintptr_t)frame,
                        (uintptr_t)frame + DELTA_RULE_FRAME_MAX);
    evv_frame_pop(frame);
    return st.answer;
}

#else

/* A build where every rule is written as C carries no bytecode: the megabyte
   and a half of it is the largest single thing in the library and nothing
   would read it. So a rule that turns out not to have been written as C is a
   fault in the build rather than something to fall back from, and it says so
   by name rather than reading an array that is not there. */
static int32_t run_bytecode(void *state, const delta_rule *r,
                            const int32_t *args, int nargs)
{
    (void)state; (void)args; (void)nargs;
    fprintf(stderr, "evv: %s was not written as C and this build has no"
            " bytecode to run it as\n", r->name);
    abort();
}

#endif

/* The rules written as C, read out by rule number. Built on the first call,
   because how many rules there are is the language module's to say, and
   kept by the language rather than here, because there may be more than one
   and each has its own. */

/* Which rules are written as C, read into an index by rule number. Settled at
   link time, so this happens once a language; scanning the table instead cost
   every call a walk over the whole of it. Out of line because the check that
   it has been done is on the path every rule entry takes and the doing of it
   is not. */
static delta_rule_cfn *delta_native_index(const delta_language *lang)
{
    delta_rule_cfn *by_number;
    const delta_rule_c *const *p;
    const delta_rule_c *t;

    by_number = calloc((size_t)lang->rule_count, sizeof(*by_number));
    if (by_number != 0)
        for (p = lang->rule_native; *p != 0; p++)
            for (t = *p; t->fn != 0; t++)
                if (t->rule >= 0 && t->rule < lang->rule_count)
                    by_number[t->rule] = t->fn;
    /* A language with none of its rules written as C gets an index of nulls,
       which is what says it has been looked at. The walk below is what
       answers if there was no room for one. */
    *lang->rule_native_by_number = by_number;
    return by_number;
}

/* And what answers when there was no room for an index. */
static delta_rule_cfn delta_native_walk(const delta_language *lang, int n)
{
    const delta_rule_c *const *p;
    const delta_rule_c *w;

    for (p = lang->rule_native; *p != 0; p++)
        for (w = *p; w->fn != 0; w++)
            if (w->rule == n)
                return w->fn;
    return 0;
}

int32_t delta_run_rule(void *state, const delta_rule *r, const int32_t *args,
                       int nargs)
{
    const delta_rule *was;
    delta_rule_cfn     fn;
    delta_rule_cfn    *by_number;
    int32_t answer;
    int n;

    /* Which language, before anything reads a table. The machine says: it
       was made by one language and remembers which, and a rule of another
       cannot reach it, because nothing hands one over. What was in force
       goes back at the end, since a rule may be run from inside a callback
       of a machine speaking something else.

       Read once and held. Every name below is a reach through the language in
       force, and the language in force is a thread-local; asking eight times
       for the same answer was most of what this function cost, on a path
       every one of a run's two and a half million rule entries takes. And
       setting it is a write to that thread-local, which is worth not making
       when what is there is already right -- which, between two rules of one
       module, it always is. */
    const delta_language *lang = delta_lang_of(state);
    const delta_language *was_lang = delta_lang_now();

    if (was_lang != lang)
        delta_lang_set(lang);

    n = (int)(r - lang->rules);

    if (delta_rule_trace < 0) {
        const char *e = getenv("DELTA_RULE_TRACE");

        delta_rule_trace = (e != 0) ? (atoi(e) > 100000 ? 2 : 1) : 0;
        delta_rule_limit = (e != 0 && *e) ? atol(e) : 0;
        if (delta_rule_trace)
            atexit(delta_rule_report);
    }
    if (delta_rule_trace
        && (delta_rule_here == 0
            || strcmp(delta_rule_here->object, r->object) != 0)) {
        int j;

        delta_rule_calls++;
        fprintf(stderr, "rule %ld: %s(", delta_rule_calls, r->name);
        for (j = 0; j < nargs; j++)
            fprintf(stderr, "%s%08x", j ? ", " : "", (unsigned)args[j]);
        fprintf(stderr, ")\n");
        fflush(stderr);
    } else if (delta_rule_trace) {
        fprintf(stderr, "# %s\n", r->name);
        fflush(stderr);
    }

    was = delta_rule_here;
    delta_rule_here = r;

    /* A rule written as C runs as C, and only from here, where everything a
       run says about itself has already been said. The two are then
       interchangeable, and a run with one and a run with the other say the
       same thing or the translation is wrong.

       Which rules are written as C is settled at link time, so the table is
       read into an index by rule number once. Scanning it instead cost every
       call a walk over the whole of it. */
    by_number = *lang->rule_native_by_number;
    if (by_number == 0)
        by_number = delta_native_index(lang);

    if (by_number != 0)
        fn = (n >= 0 && n < lang->rule_count) ? by_number[n] : 0;
    else
        fn = delta_native_walk(lang, n);
    answer = (fn != 0) ? fn(state, args, nargs)
                       : run_bytecode(state, r, args, nargs);

    delta_rule_here = was;
    if (delta_rule_trace) {
        fprintf(stderr, "# %s left with %08x\n", r->name, (unsigned)answer);
        fflush(stderr);
    }
    if (was_lang != lang)
        delta_lang_set(was_lang);
    return answer;
}

const delta_rule *delta_find_rule(const char *name)
{
    int i;

    for (i = 0; i < delta_rule_count; i++)
        if (strcmp(delta_rules[i].name, name) == 0)
            return &delta_rules[i];
    return 0;
}
