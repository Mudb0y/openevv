/* The four flags the machine keeps, and the operations that set them.
 *
 * These were in the interpreter, and the interpreter is where they belong by
 * rights: they are the machine's arithmetic, not the language's. They are here
 * because a rule written as C makes the same comparisons, and a comparison
 * whose answer differed between the two forms would be a rule that speaks one
 * way interpreted and another way compiled.
 *
 * Written as inline rather than called. Every operation in a rule sets these
 * and almost every one of them is read by the very next line, so out of line
 * the compiler has to keep a flags structure in memory and work all four out
 * whether or not anything looks at them. Inline it keeps what is read and
 * drops the rest, which for most of the rules is a compare and a branch where
 * there was a call.
 */

#ifndef DELTA_FLAGS_H
#define DELTA_FLAGS_H

#include <stdint.h>

typedef struct {
    int zf, sf, cf, of;
} delta_flags;

/* What a rule tests, works out and asks, under the names the machine's own
   operations carry. A rule reads better saying which comparison it made than
   saying that it made comparison four. */
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

static inline uint32_t mask_to(uint32_t v, int w)
{
    if (w == 1)
        return v & 0xffu;
    if (w == 2)
        return v & 0xffffu;
    return v;
}

static inline int sign_of(uint32_t v, int w)
{
    if (w == 1)
        return (int)((v >> 7) & 1);
    if (w == 2)
        return (int)((v >> 15) & 1);
    return (int)((v >> 31) & 1);
}

static inline void flags_logic(delta_flags *f, uint32_t r, int w)
{
    r = mask_to(r, w);
    f->zf = (r == 0);
    f->sf = sign_of(r, w);
    f->cf = 0;
    f->of = 0;
}

/* b minus a, which is the way round a comparison is written. */
static inline uint32_t flags_sub(delta_flags *f, uint32_t a, uint32_t b,
                                 int w, int keepcf)
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

static inline uint32_t flags_add(delta_flags *f, uint32_t a, uint32_t b,
                                 int w, int keepcf)
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
static inline int32_t delta_rule_alu(delta_flags *f, int kind,
                                     int32_t ain, int32_t bin)
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
static inline void delta_rule_cmp(delta_flags *f, int kind,
                                  int32_t ain, int32_t bin)
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

static inline int delta_condition(const delta_flags *f, int cond)
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

#endif
