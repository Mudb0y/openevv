/* Putting a run of tokens on the spine from the byte string a rule carries.
 *
 * The string starts with one byte saying what width the values in it are,
 * and then holds that many values, each written most significant byte
 * first with the sign in the top bit of the first. That is not necessarily
 * the width the field wants, so where the two differ every value goes
 * through the machine's own conversion before it is put in.
 *
 * A string of length one holds nothing but the width byte. That means clear
 * the span rather than fill it, and the two registers are left as they are.
 */

#include <stdint.h>
#include "delta.h"

#define NODE(n)   ((int32_t *)(intptr_t)(n))
#define OWN_WORDS 3
#define LINK_MASK (~3)

/* What the first byte of the string says the values in it are. */
#define SRC_INT8  0xc7
#define SRC_INT16 0xc8
#define SRC_INT32 0xc9
#define SRC_INT16_ALT 0xca

#define KIND_NAMED8  (-1)
#define KIND_NAMED_W (-2)
#define KIND_LONG    (-3)
#define KIND_INT     (-4)

/* The sign lives in the top bit of the first byte, so the magnitude is the
   rest of it. */
#define SIGN_BIT  0x80
#define MAG_BITS  0x7f

int ins_tokens(delta_state *d, int8_t f, const uint8_t *str, uint8_t n,
               int32_t arg)
{
    delta_operand dst, src;
    const uint8_t *end;
    int8_t   b8  = 0;
    int16_t  i16 = 0;
    int32_t  i32 = 0;
    int32_t  op;

    if (n == 1) {
        /* Nothing to put in. Take out whatever is between the registers,
           unless they are already each other's neighbour. */
        if ((NODE(d->lpta.node)[d->vars->fence_base + f] & LINK_MASK)
                != d->rpta.node
         || (NODE(d->rpta.node)[OWN_WORDS + f] & LINK_MASK) != d->lpta.node)
            vdel_2pt(d, f, d->lpta.node, d->rpta.node);
        return 1;
    }

    dst.kind = STMTYP(f);
    dst.flag = vstmtbl[f].fields[0].flag;

    switch (dst.kind) {
    case KIND_NAMED8:  dst.ptr = &b8;  break;
    case KIND_LONG:    dst.ptr = &i32; break;
    case KIND_NAMED_W:
    case KIND_INT:     dst.ptr = &i16; break;
    default:           return 0;
    }

    end = str + n;
    op  = *str++;

    switch (op) {
    case SRC_INT8:      src.kind = KIND_NAMED8;  src.ptr = &b8;  break;
    case SRC_INT16:     src.kind = KIND_NAMED_W; src.ptr = &i16; break;
    case SRC_INT32:     src.kind = KIND_LONG;    src.ptr = &i32; break;
    case SRC_INT16_ALT: src.kind = KIND_INT;     src.ptr = &i16; break;
    default:            return 0;
    }
    src.flag = dst.flag;

    while (str < end) {
        switch (src.kind) {
        case KIND_NAMED8:
            b8 = (int8_t)*str++;
            break;

        case KIND_LONG:
            i32 = (int32_t)(((uint32_t)(str[0] & MAG_BITS) << 24)
                          | ((uint32_t)str[1] << 16)
                          | ((uint32_t)str[2] << 8)
                          |  (uint32_t)str[3]);
            if (str[0] & SIGN_BIT)
                i32 = -i32;
            str += 4;
            break;

        case KIND_NAMED_W:
        case KIND_INT:
            i16 = (int16_t)(((str[0] & MAG_BITS) << 8) | str[1]);
            if (str[0] & SIGN_BIT)
                i16 = (int16_t)(-i16);
            str += 2;
            break;

        default:
            return 0;
        }

        if (dst.kind != src.kind)
            vassign(d, &dst, &src);

        if (!vins_tok(d, f, d->lpta.node, d->rpta.node, &dst))
            return 0;

        if (str < end) {
            d->lpta.node = (int32_t)(intptr_t)vins_sync(d, f,
                NODE(d->rpta.node)[OWN_WORDS + f] & LINK_MASK,
                d->rpta.node);
            if (d->lpta.node == 0)
                return 0;
        }
    }

    (void)arg;
    return 1;
}
