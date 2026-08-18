/* The ten ways the outside asks the statement tables a question.
 *
 * Everything here is phrased in terms of vstmtbl, the table the language
 * module declares: how many statement types there are, what one is called,
 * whether a field of it takes a name out of a fixed list, and how to turn
 * a piece of text into the value a field will hold. Nothing here decides
 * anything on its own; it reads the language's own description back out.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "delta.h"

/* Which side of the machine wants to hear that the spine moved. */
#define OWNER_MOVED(d) (*(int32_t *)((d)->owner + 0x1b8))

/* A field whose kind is one of these takes a name out of the list at
   values, rather than a number. The two differ only in how wide the
   chosen index is stored: one byte or two. */
#define KIND_NAMED8  (-1)
#define KIND_NAMED16 (-2)
#define KIND_LONG    (-3)
#define KIND_INT     (-4)

/* The name the tables give a field that is not set. Whoever asks gets told
   something else, which the machine carries with it. */
#define UNDEFINED "undefined"

extern int   syncmark_order(delta_state *d, int32_t l, int32_t r);
extern int   syncmark_equ(int32_t l, int32_t r);
extern int   is_token_next(delta_state *d, int8_t f, int32_t at);
extern int   is_token_prev(int8_t f, int32_t at);
extern void *next_token(delta_state *d, int8_t f, int32_t at);
extern char *field_value(int8_t f, void *tok, int32_t fld);
extern int32_t sync_to_right(delta_state *d, int8_t f, int32_t at);
extern void  ins_sync(delta_state *d, int8_t f, int32_t at, int32_t n);
extern int   ins_tok_named(delta_state *d, int8_t f, const void *value,
                           const char *name, int32_t at);

/* One field description, reached the way every routine below reaches it. */
#define FIELD(f, i) (&vstmtbl[(f)].fields[(i)])

/* How many statement types the language declared. */
int num_streams(delta_state *d)
{
    return d->nstmts;
}

/* What the language calls one. */
const char *stream_name(int8_t f)
{
    return vstmtbl[f].name;
}

/* Whether one statement of this type is written as a whole line rather than
   as a sequence of tokens. */
int single_letter_stream(int8_t f)
{
    return vstmtbl[f].whole_token;
}

/* Whether this type may be walked a statement at a time. */
int time_stream(int8_t f)
{
    return (signed char)vstmtbl[f].walkable;
}

/* Whether this field of this type takes a name out of a fixed list. */
int enum_field(int8_t f, int32_t i)
{
    int32_t kind = FIELD(f, i)->kind;

    if (kind < KIND_NAMED16)
        return 0;
    if (kind < 0)
        return 1;
    return 0;
}

/* Whether a sync mark stands at this field of the node the bits describe.
   The language's fields do not start at zero in that array; the machine
   says where they do. */
int sync_in_stm(delta_state *d, int8_t f, const int32_t *bits)
{
    int32_t i = d->vars->fence_base + f;

    return (bits[i] & 1) != 0;
}

/* Take out everything between two marks. Answers one whether or not
   anything was there. */
int del_two_point(delta_state *d, int8_t f, int32_t l, int32_t r)
{
    vdel_2pt(d, f, l, r);
    return 1;
}

/* Read the text of every token between two marks into a buffer, up to a
   length. Answers where it was put, or nothing if the marks were the wrong
   way round. The count includes the terminator, so a length of one gets an
   empty string. */
char *extract_string(delta_state *d, int8_t f, int32_t l, int32_t r,
                     char *out, int32_t max)
{
    char *p = out;

    if (!syncmark_order(d, l, r))
        return 0;

    max--;
    while (!syncmark_equ(l, r) && max != 0) {
        if (is_token_next(d, f, l)) {
            void *tok = next_token(d, f, l);
            char *v   = field_value(f, tok, 0);

            while (*v != 0 && max != 0) {
                *p++ = *v++;
                max--;
            }
        }
        l = sync_to_right(d, f, l);
    }

    *p = 0;
    return out;
}

/* Put a string in one character at a time, each as its own token, with a
   sync mark between them. Only a type whose first field names single
   characters can take one; anything the field does not name stops it.
   Whatever went in before the refusal stays in. */
int insert_string(delta_state *d, int8_t f, int32_t at, const char *s)
{
    char    one[2];
    int32_t fld = 0;

    one[0] = 0;
    one[1] = 0;

    if (!enum_field(f, fld) && !single_letter_stream(f))
        return 0;

    if (is_token_prev(f, at))
        ins_sync(d, f, at, 1);

    for (; *s != 0; s++) {
        const delta_fielddesc *fd = FIELD(f, fld);
        const char *const     *names = (const char *const *)fd->values;
        int32_t                j = 0;

        one[0] = *s;

        while (j < FIELD(f, fld)->nvalues) {
            if (strcmp(names[j], one) == 0)
                break;
            j++;
        }
        if (j >= FIELD(f, fld)->nvalues)
            return 0;

        if (FIELD(f, fld)->kind == KIND_NAMED8) {
            int8_t idx = (int8_t)j;

            if (!ins_tok_named(d, f, &idx, one, at))
                return 0;
        } else {
            int16_t idx = (int16_t)j;

            if (!ins_tok_named(d, f, &idx, one, at))
                return 0;
        }

        if (s[1] != 0)
            ins_sync(d, f, at, 1);
    }

    OWNER_MOVED(d) = 1;
    return 1;
}

/* True when every character of a string is the same one. An empty string
   counts. */
static int allchrs(const char *s, char c)
{
    for (; *s != 0; s++)
        if (*s != c)
            return 0;
    return 1;
}

/* True when the second string is a prefix of the first. */
static int strprefix(const char *s, const char *pre)
{
    int32_t i;

    for (i = 0; pre[i] != 0; i++)
        if (s[i] != pre[i])
            return 0;
    return 1;
}

/* A whole string read as a number, with nothing left over and nothing out
   of range. errno is set to something of its own first so a range failure
   left behind by somebody else does not count against this one. */
static int legal_long(const char *s, long *out)
{
    char *end;
    long  v;

    errno = 0x23;
    v = strtol(s, &end, 0);
    if (*end != 0)
        return 0;
    if (errno == ERANGE || v > 0x7fffffffL || v < (-0x7fffffffL - 1))
        return 0;
    if (out != 0)
        *out = v;
    return 1;
}

static int legal_int(const char *s, int *out)
{
    char *end;
    long  v;

    errno = 0x23;
    v = strtol(s, &end, 0);
    if (*end != 0)
        return 0;
    if (errno == ERANGE || v > 0x7fffffffL || v < (-0x7fffffffL - 1))
        return 0;
    if (out != 0)
        *out = (int)v;
    return 1;
}

/* Turn a piece of text into a value for one field, and say both what it
   settled on and where the value itself is. The answer is not unique: an
   abbreviation is accepted, so the first name the text is a prefix of wins,
   and a string of nothing but hyphens asks for the undefined value by name.
   The value is left in a static, which is why the caller is handed a
   pointer and not the thing.
 */
int non_unique_value(delta_state *d, int8_t f, int32_t fld, const char *s,
                     const char **out_name, void **out_value)
{
    static int16_t lfound;
    static int8_t  sfound;
    static long    lval;
    static int     ival;

    const delta_fielddesc *fd;
    const char *const     *names;
    int32_t                kind;
    int16_t                j;

    if (*s == 0)
        return 0;

    kind = FIELD(f, fld)->kind;

    if (kind == KIND_INT) {
        if (!legal_int(s, &ival))
            return 0;
        *out_name  = s;
        *out_value = &ival;
        return 1;
    }
    if (kind == KIND_LONG) {
        if (!legal_long(s, &lval))
            return 0;
        *out_name  = s;
        *out_value = &lval;
        return 1;
    }
    if (kind < KIND_LONG || kind >= 0)
        return 0;

    lfound = -1;

    if (allchrs(s, '-')) {
        for (j = 0; j < FIELD(f, fld)->nvalues; j++) {
            fd    = FIELD(f, fld);
            names = (const char *const *)fd->values;
            if (strcmp(names[j], UNDEFINED) == 0) {
                lfound = j;
                break;
            }
        }
    }

    if (lfound == -1) {
        for (j = 0; j < FIELD(f, fld)->nvalues; j++) {
            fd    = FIELD(f, fld);
            names = (const char *const *)fd->values;
            if (strprefix(names[j], s)) {
                lfound = j;
                break;
            }
        }
    }

    if (lfound == -1)
        return 0;

    fd         = FIELD(f, fld);
    names      = (const char *const *)fd->values;
    *out_name  = names[lfound];

    if (strcmp(*out_name, UNDEFINED) == 0)
        *out_name = d->stack->undefined_text;

    if (FIELD(f, fld)->kind == KIND_NAMED8) {
        sfound     = (int8_t)lfound;
        *out_value = &sfound;
    } else {
        *out_value = &lfound;
    }

    return 1;
}
