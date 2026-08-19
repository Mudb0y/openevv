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

#include "delta_rules.h"
#include "delta_rules_c.h"
#include "evv_arena.h"

enum {
    OP_CALL, OP_JUMP, OP_BRANCH, OP_CMP, OP_ALU2, OP_ALU1, OP_LOAD,
    OP_STORE, OP_SWITCH, OP_MAP, OP_RETURN, OP_SCALE, OP_ADDK, OP_MUL,
    OP_DIV, OP_WIDEN, OP_SETCC, OP_PUSH, OP_SETARG, OP_POPN, OP_POPREG
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

enum {
    C_E, C_NE, C_A, C_AE, C_B, C_BE, C_G, C_GE, C_L, C_LE, C_S, C_NS
};

enum { CMP_TESTL, CMP_TESTW, CMP_TESTB, CMP_CMPL, CMP_CMPW, CMP_CMPB };

enum {
    A_ADDL, A_ADDW, A_SUBL, A_SUBW, A_ANDL, A_ANDW, A_ORL, A_ORW,
    A_INCL, A_INCW, A_DECL, A_DECW, A_SHLL, A_SHLW, A_SARL, A_SARW,
    A_NEGL, A_NEGW, A_SBBL, A_IMULL, A_IMULW
};

/* How wide each of those works, since the names do not run in pairs all
   the way. */
static const unsigned char alu_width[] = {
    4, 2, 4, 2, 4, 2, 4, 2, 4, 2, 4, 2, 4, 2, 4, 2, 4, 2, 4, 4, 2
};

enum { M_MOVL, M_MOVW, M_MOVB, M_MOVSWL, M_MOVZWL, M_MOVSBL, M_MOVZBL };

#define NREG 8

extern int delta_rule_trace;

/* Which rule is running, so that a run can be told about in the same terms
   as a run of the original: only the calls that leave the object they were
   compiled in can be seen there, because the others were renamed along with
   the definitions they reach. */
static const delta_rule *delta_rule_here;

/* Told to the trace so that it can pass over the calls a run of the
   original cannot see. Nothing outside a trace build defines it. */
extern const char *delta_trace_caller __attribute__((weak));

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

/* ---- flags ---------------------------------------------------------- */

static uint32_t mask_to(uint32_t v, int w)
{
    if (w == 1)
        return v & 0xffu;
    if (w == 2)
        return v & 0xffffu;
    return v;
}

static int sign_of(uint32_t v, int w)
{
    if (w == 1)
        return (v >> 7) & 1;
    if (w == 2)
        return (v >> 15) & 1;
    return (int)((v >> 31) & 1);
}

static void flags_logic(delta_flags *f, uint32_t r, int w)
{
    r = mask_to(r, w);
    f->zf = (r == 0);
    f->sf = sign_of(r, w);
    f->cf = 0;
    f->of = 0;
}

/* b minus a, which is the way round a comparison is written. */
static uint32_t flags_sub(delta_flags *f, uint32_t a, uint32_t b, int w, int keepcf)
{
    uint32_t ma = mask_to(a, w);
    uint32_t mb = mask_to(b, w);
    uint32_t r = mask_to(mb - ma, w);

    f->zf = (r == 0);
    f->sf = sign_of(r, w);
    if (!keepcf)
        f->cf = (mb < ma);
    f->of = (sign_of(mb, w) != sign_of(ma, w))
        && (sign_of(r, w) != sign_of(mb, w));
    return r;
}

static uint32_t flags_add(delta_flags *f, uint32_t a, uint32_t b, int w, int keepcf)
{
    uint32_t ma = mask_to(a, w);
    uint32_t mb = mask_to(b, w);
    uint32_t r = mask_to(mb + ma, w);

    f->zf = (r == 0);
    f->sf = sign_of(r, w);
    if (!keepcf)
        f->cf = (r < mb);
    f->of = (sign_of(mb, w) == sign_of(ma, w))
        && (sign_of(r, w) != sign_of(mb, w));
    return r;
}

/* One operation of the machine's arithmetic, flags and all. The interpreter
   and a rule written as C both come here, so neither can drift from the
   other over what a comparison afterwards will say. */
int32_t delta_rule_alu(delta_flags *f, int kind, int32_t ain, int32_t bin)
{
    int w = alu_width[kind];
    uint32_t a = (uint32_t)ain;
    uint32_t b = (uint32_t)bin;
    uint32_t r;

    switch (kind) {
    case A_ADDL: case A_ADDW: r = flags_add(f, a, b, w, 0); break;
    case A_SUBL: case A_SUBW: r = flags_sub(f, a, b, w, 0); break;
    case A_ANDL: case A_ANDW: r = mask_to(a & b, w);
        flags_logic(f, r, w); break;
    case A_ORL:  case A_ORW:  r = mask_to(a | b, w);
        flags_logic(f, r, w); break;
    case A_INCL: case A_INCW: r = flags_add(f, 1, b, w, 1); break;
    case A_DECL: case A_DECW: r = flags_sub(f, 1, b, w, 1); break;
    case A_SHLL: case A_SHLW:
        r = mask_to(b << (a & 31), w);
        f->zf = (r == 0);
        f->sf = sign_of(r, w);
        break;
    case A_SARL: case A_SARW: {
        int32_t sv = (w == 2) ? (int32_t)(int16_t)b : (int32_t)b;

        r = mask_to((uint32_t)(sv >> (a & 31)), w);
        f->zf = (r == 0);
        f->sf = sign_of(r, w);
        break;
    }
    case A_NEGL: case A_NEGW:
        r = flags_sub(f, b, 0, w, 0);
        f->cf = (mask_to(b, w) != 0);
        break;
    case A_SBBL:
        r = mask_to(b - a - (uint32_t)f->cf, w);
        flags_sub(f, a + (uint32_t)f->cf, b, w, 0);
        break;
    case A_IMULL: case A_IMULW:
        r = mask_to(a * b, w);
        break;
    default:
        r = b;
        break;
    }


    return (int32_t)r;
}

/* And one comparison, which sets the flags and nothing else. */
void delta_rule_cmp(delta_flags *f, int kind, int32_t ain, int32_t bin)
{
    int w = (kind == CMP_TESTB || kind == CMP_CMPB) ? 1
        : (kind == CMP_TESTW || kind == CMP_CMPW) ? 2 : 4;
    uint32_t a = (uint32_t)ain;
    uint32_t b = (uint32_t)bin;

    if (kind <= CMP_TESTB)
        flags_logic(f, a & b, w);
    else
        flags_sub(f, a, b, w, 0);
}

int delta_condition(const delta_flags *f, int cond)
{
    switch (cond) {
    case C_E:  return f->zf;
    case C_NE: return !f->zf;
    case C_A:  return !f->cf && !f->zf;
    case C_AE: return !f->cf;
    case C_B:  return f->cf;
    case C_BE: return f->cf || f->zf;
    case C_G:  return !f->zf && (f->sf == f->of);
    case C_GE: return f->sf == f->of;
    case C_L:  return f->sf != f->of;
    case C_LE: return f->zf || (f->sf != f->of);
    case C_S:  return f->sf;
    case C_NS: return !f->sf;
    }
    return 0;
}

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
        v = EVV_REF(delta_rule_sym[get16(p + 1)]);
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

typedef int32_t (*I0)(void);
typedef int32_t (*I1)(int32_t);
typedef int32_t (*I2)(int32_t, int32_t);
typedef int32_t (*I3)(int32_t, int32_t, int32_t);
typedef int32_t (*I4)(int32_t, int32_t, int32_t, int32_t);
typedef int32_t (*I5)(int32_t, int32_t, int32_t, int32_t, int32_t);
typedef int32_t (*I6)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
typedef int32_t (*I7)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                      int32_t);
typedef int32_t (*I8)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                      int32_t, int32_t);
typedef int32_t (*I9)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                      int32_t, int32_t, int32_t);
typedef int32_t (*I10)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                       int32_t, int32_t, int32_t, int32_t);
typedef int32_t (*I11)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                       int32_t, int32_t, int32_t, int32_t, int32_t);
typedef int32_t (*I12)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                       int32_t, int32_t, int32_t, int32_t, int32_t, int32_t);
typedef int32_t (*IN)(int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                      int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                      int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                      int32_t, int32_t, int32_t, int32_t, int32_t, int32_t,
                      int32_t);

#define A a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7], a[8], a[9], \
          a[10], a[11], a[12], a[13], a[14], a[15], a[16], a[17], a[18], \
          a[19], a[20], a[21], a[22], a[23], a[24]

/* Calling with more arguments than the entry declares is what the original
   never has to do; here the number is only known at run time, so the common
   arities are called exactly and the rare long ones go through one wide
   signature. Every entry is cdecl, so the extra words are simply not read. */
static int32_t call_entry(delta_rule_fn fn, const int32_t *a, int n)
{
    switch (n) {
    case 0:  return ((I0)fn)();
    case 1:  return ((I1)fn)(a[0]);
    case 2:  return ((I2)fn)(a[0], a[1]);
    case 3:  return ((I3)fn)(a[0], a[1], a[2]);
    case 4:  return ((I4)fn)(a[0], a[1], a[2], a[3]);
    case 5:  return ((I5)fn)(a[0], a[1], a[2], a[3], a[4]);
    case 6:  return ((I6)fn)(a[0], a[1], a[2], a[3], a[4], a[5]);
    case 7:  return ((I7)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6]);
    case 8:  return ((I8)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7]);
    case 9:  return ((I9)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                             a[8]);
    case 10: return ((I10)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                              a[8], a[9]);
    case 11: return ((I11)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                              a[8], a[9], a[10]);
    case 12: return ((I12)fn)(a[0], a[1], a[2], a[3], a[4], a[5], a[6], a[7],
                              a[8], a[9], a[10], a[11]);
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
        st->pc = get16s(p);
        return;

    case OP_BRANCH: {
        int cond = *p++;
        int32_t to = get16s(p);

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
            st->pc = get16s(p + 2 * idx);
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
static int32_t run_bytecode(void *state, const delta_rule *r,
                            const int32_t *args, int nargs)
{
    unsigned char frame[DELTA_RULE_FRAME_MAX];
    volatile int depth = 0;
    interp st;
    int i;

    memset(frame, 0, sizeof(frame));
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
               so the argument area goes back with it. */
            depth = st.argn;
            st.reg[0] = setjmp(*(jmp_buf *)(intptr_t)buf);
            st.argn = depth;
            continue;
        }
        step(&st);
        delta_rule_steps++;
    }

    return st.answer;
}

int32_t delta_run_rule(void *state, const delta_rule *r, const int32_t *args,
                       int nargs)
{
    const delta_rule *was;
    const delta_rule_c *w;
    int n = (int)(r - delta_rules);
    int32_t answer;

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
    if (&delta_trace_caller != 0)
        delta_trace_caller = r->object;

    /* A rule written as C runs as C, and only from here, where everything a
       run says about itself has already been said. The two are then
       interchangeable, and a run with one and a run with the other say the
       same thing or the translation is wrong. */
    answer = 0;
    for (w = delta_rule_native; w->fn != 0; w++)
        if (w->rule == n)
            break;
    if (w->fn != 0)
        answer = w->fn(state, args, nargs);
    else
        answer = run_bytecode(state, r, args, nargs);

    delta_rule_here = was;
    if (&delta_trace_caller != 0)
        delta_trace_caller = was ? was->object : 0;
    if (delta_rule_trace) {
        fprintf(stderr, "# %s left with %08x\n", r->name, (unsigned)answer);
        fflush(stderr);
    }
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
