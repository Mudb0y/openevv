/* Four more that stand on their own.
 *
 * A string literal cleaned of its quoting and its escapes; the link-up pass
 * that joins the machine's cross-references once everything exists; and the
 * lookup that turns a variable's number into where it actually lives.
 */

#include <stdint.h>
#include <ctype.h>
#include "delta.h"
#include "evv_arena.h"

/* Where the machine keeps the two tables the link-up pass walks, and how big
   an entry of each is. Neither has ever needed naming anywhere else. */
#define LINK_N(v)     (*(int16_t *)((char *)(v) + 0x118a))
#define LINK_AT(v, i) ((char *)(v) + 0x11a8 + (i) * 0x3c)
#define REF_N(v)      (*(int16_t *)((char *)(v) + 0x1188))
#define REF_AT(v, i)  ((char *)(v) + 0x1190 + (i) * 0xc)

/* One activation record's worth of variable descriptions. */
#define DESC_AT(v, i) ((char *)(v) + 0x119c + (i) * 0xc)
#define DESC_BYTES    0xc

/* A variable number with this bit set belongs to the machine rather than to
   any activation, and the rest of it indexes straight into the table. */
#define GLOBAL_BIT 0x8000
#define NUMBER     0x3fff

/* One statement of a stream, as the reference tables count them. */
#define STREAM_STRIDE 0x38

extern void *vonstack(delta_state *d, int32_t ctx);

/* Undo the quoting a literal was written with: strip the opening character
   if one was named, stop at the closing one, and turn backslash escapes
   into the characters they stand for. Answers how many came out, or minus
   one if it did not start the way it was supposed to. */
int32_t cleanLiteral(char *s, char open, char close)
{
    const char *in = s;
    int32_t     n = 0;
    char        c;

    if (open != 0) {
        if (*in != open)
            return -1;
        in++;
    }

    c = *in++;
    while (c != close && c != 0) {
        if (c == '\\') {
            c = *in++;

            if (c >= '0' && c <= '7') {
                /* Up to three octal digits, and none of them an eight. */
                int32_t i;

                c = (char)(c - '0');
                for (i = 1; i < 3; i++) {
                    if (!isdigit((unsigned char)*in) || *in > '7')
                        break;
                    c = (char)(c * 8 + (*in - '0'));
                    in++;
                }
            } else if (c == 'b') {
                c = 8;
            } else if (c == 'f') {
                c = 12;
            } else if (c == 'n') {
                c = 10;
            } else if (c == 'r') {
                c = 13;
            } else if (c == 't') {
                c = 9;
            }
            /* Anything else after a backslash stands for itself. */
        }

        *s++ = c;
        n++;
        c = *in++;
    }

    *s = 0;
    return n;
}

/* Join everything up once it all exists. The first walk points each link at
   the thing it names and at what that thing points at in turn; the second
   works out where in a stream's statements each reference lands and points
   the statement back at the reference. Neither can be done while the parts
   are still being built, which is why it is a pass of its own. */
void vlinkinit(delta_state *d)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int16_t     i;

    for (i = 0; i < LINK_N(v); i++) {
        char *link = LINK_AT(v, i);
        char *at = *(char **)(link + 4);

        if (!at)
            continue;

        **(char ***)at = link;
        **(char ***)(at + 4) = *(char **)(link + 0x1c);
        **(char ***)(at + 0xc) = *(char **)(at + 8);
    }

    for (i = 0; i < REF_N(v); i++) {
        char *ref = REF_AT(v, i);
        char *named;
        char *stream;

        if (!ref || !*(char **)ref)
            continue;

        named = *(char **)(*(char **)ref + 4);
        if (!named)
            continue;

        stream = *(char **)(named + 8) + *(int16_t *)(ref + 4) * STREAM_STRIDE;
        *(char **)(ref + 8) = stream;
        *(char **)(stream + 0x14) = *(char **)ref;
    }
}

/* Where a variable lives. One with the top bit set belongs to the machine
   and is found in its own table; anything else belongs to an activation,
   and the number indexes that activation's own block. Naming no activation
   means the one that is running. */
void *vardesc(delta_state *d, uint8_t hi, uint8_t lo, void *frame)
{
    delta_vars *v = EVV_AT(delta_vars *, d->vars);
    int32_t     code = (hi << 8) | lo;
    void       *base;

    if (code & GLOBAL_BIT)
        return DESC_AT(v, code & NUMBER);

    if (frame == 0)
        frame = (void *)(intptr_t)v->running;

    if (EVV_REF(frame) == v->running)
        base = *(void **)(EVV_AT(uint8_t *, v->back) + 4);
    else
        base = vonstack(d, EVV_REF(frame));

    if (!base)
        return 0;

    return *(char **)((char *)frame + 8) + (code & NUMBER) * DESC_BYTES;
}
