/* Everything the outside asks the statement tables, and everything on the
 * spine it asks through them.
 *
 * Two halves. One reads the language module's own description of itself:
 * how many statement types there are, what one is called, whether a field
 * takes a name out of a fixed list, how to turn text into a value and a
 * value back into text. The other walks the spine a token at a time, asking
 * what stands either side of a mark and putting things between.
 *
 * Nothing here decides anything on its own. The language declares vstmtbl
 * and the machine owns the spine; this is the reading glass held over both.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "delta.h"

/* Which side of the machine wants to hear that the spine moved. */
#define OWNER_MOVED(d) (EVV_AT(delta_owner *, (d)->owner)->changed)

/* A spine position is carried as an int32, so reaching a node's words means
   casting one back. The first three words are the node's own; the sync
   links follow, one per statement type. Reaching them through the machine's
   fence base rather than that fixed three is the same arithmetic written
   the other way, and both spellings appear below because both appear in
   what this was taken from. */
#define NODE(n)   ((int32_t *)(intptr_t)(n))
#define OWN_WORDS 3
#define LINK_MASK (~3)
#define IS_SYNC   2

/* A field of one of these kinds takes a name out of the list at values
   rather than a number. English declares only the first; the other is
   carried because the tables allow it. */
#define KIND_NAMED8    (-1)
#define KIND_NAMED_W   (-2)
#define KIND_LONG      (-3)
#define KIND_INT       (-4)

/* The value a field holds when it stands in for a silence, in each of the
   two widths a numeric field comes in. */
#define GAP_LONG ((int32_t)0x80000001)
#define GAP_INT  ((int16_t)0x8001)

/* The name the tables give a field that is not set. Whoever asks for the
   value is told something else, which the machine carries with it; whoever
   asks for the text gets three hyphens. */
#define UNDEFINED "undefined"

/* One field description, reached the way every routine below reaches it. */
#define FD(f, i) (&vstmtbl[(f)].fields[(i)])

/* Reading one field out of a token. The language supplies a reader per
   field and every one of them is handed the token's body, not its head. */
#define READ(f, i, tok) (vstmtbl[(f)].get[(i)]((void *)((char *)(tok) + 8)))

/* A list of statement types with a list of fields under each, which is how
   a caller says which parts of the spine it means to walk. */
typedef struct {
    int8_t   stm;      /* +0x00, which statement type */
    uint8_t  pad_01[3];
    int32_t  nfields;  /* +0x04, how many of its fields are listed */
    uint8_t *fields;   /* +0x08, and which */
} stream_entry;

typedef struct {
    int32_t       n;   /* +0x00 */
    stream_entry *e;   /* +0x04 */
} stream_list;

extern void *vardesc(delta_state *d, uint8_t hi, uint8_t lo, void *frame);


/* ---- what the language says about itself ---- */

/* How many statement types the language declared. */
int num_streams(delta_state *d)
{
    return d->nstmts;
}

/* What it calls one. */
const char *stream_name(int8_t f)
{
    return vstmtbl[f].name;
}

/* What it calls one field of one. */
const char *field_name(int8_t f, int32_t i)
{
    return FD(f, i)->name;
}

/* Whether a statement of this type is written as a whole line rather than
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

/* Whether this field takes a name out of a fixed list. */
int enum_field(int8_t f, int32_t i)
{
    int32_t kind = FD(f, i)->kind;

    if (kind < KIND_NAMED_W)
        return 0;
    if (kind < 0)
        return 1;
    return 0;
}


/* ---- the spine, a token at a time ---- */

/* The two ends of it. */
int32_t left_delta(delta_state *d)
{
    return EVV_AT(delta_stack *, d->stack)->spine_l;
}

int32_t right_delta(delta_state *d)
{
    return EVV_AT(delta_stack *, d->stack)->spine_r;
}

/* Two marks are the same mark when they are the same node. */
int syncmark_equ(int32_t l, int32_t r)
{
    return l == r;
}

/* And in the right order when the first is not to the right of the second. */
int syncmark_order(delta_state *d, int32_t l, int32_t r)
{
    return visleft(d, l, r);
}

/* What this field links to either side of a mark, with the flag bits
   masked off. */
int32_t prev_token(int8_t f, int32_t at)
{
    return NODE(at)[OWN_WORDS + f] & LINK_MASK;
}

int32_t next_token(delta_state *d, int8_t f, int32_t at)
{
    return NODE(at)[EVV_AT(delta_vars *, d->vars)->fence_base + f] & LINK_MASK;
}

/* Whether what stands there is a token rather than another sync. Nothing
   at all counts as yes. */
int is_token_prev(int8_t f, int32_t at)
{
    int32_t p = NODE(at)[OWN_WORDS + f] & LINK_MASK;

    if (p != 0 && (NODE(p)[0] & IS_SYNC) != 0)
        return 0;
    return 1;
}

int is_token_next(delta_state *d, int8_t f, int32_t at)
{
    int32_t p = NODE(at)[EVV_AT(delta_vars *, d->vars)->fence_base + f] & LINK_MASK;

    if (p != 0 && (NODE(p)[0] & IS_SYNC) != 0)
        return 0;
    return 1;
}

/* The next sync either side. */
int32_t sync_to_left(delta_state *d, int8_t f, int32_t at)
{
    (void)d;
    return VLSYNC((const delta_node *)(intptr_t)at, f);
}

int32_t sync_to_right(delta_state *d, int8_t f, int32_t at)
{
    return VRSYNC(d, NODE(at), f);
}

/* How many fields a statement type declares. The table says; this is the
   name the outside asks by. */
int32_t num_fields_in_stream(int8_t st)
{
    return vstmtbl[st].nfields;
}

/* Where a context begins, either side. A node that already carries the
   field is its own context; otherwise it is the mark next to it. The two
   differ in nothing but which way they look. */
int32_t left_context(delta_state *d, int8_t f, int32_t at)
{
    if ((NODE(at)[EVV_AT(delta_vars *, d->vars)->fence_base + f] & 1) != 0)
        return at;
    return sync_to_left(d, f, at);
}

int32_t right_context(delta_state *d, int8_t f, int32_t at)
{
    if ((NODE(at)[EVV_AT(delta_vars *, d->vars)->fence_base + f] & 1) != 0)
        return at;
    return sync_to_right(d, f, at);
}

/* Whether a context may be taken from here to there: every step of the way
   has to be a mark in the field, and the walk stops when it arrives. A node
   that does not carry the field at all is allowed, since there is no context
   to be wrong about.

   The two are one body in the original as well as here. Reading the right
   one expecting the mirror of the left is the mistake to guard against: it
   follows the same word of the node as the left one does, and this is not a
   transcription slip but what its own code says. */
static int allow_ctxt(delta_state *d, int32_t at, int8_t f, int32_t stop)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;

    if (at == stop)
        return 1;
    if ((NODE(at)[base + f] & 1) == 0)
        return 1;

    for (;;) {
        int32_t next;

        if (at == stop)
            return 1;

        next = NODE(at)[base + f] & LINK_MASK;
        if (next == 0)
            return 0;
        if ((*NODE(next) & IS_SYNC) == 0)
            return 0;

        at = next;
    }
}

int allow_left_ctxt(delta_state *d, int32_t at, int8_t f, int32_t stop)
{
    return allow_ctxt(d, at, f, stop);
}

int allow_right_ctxt(delta_state *d, int32_t at, int8_t f, int32_t stop)
{
    return allow_ctxt(d, at, f, stop);
}

/* Two the outside asks for as yes or no where the machine answers with a
   number. */
int init_stream(delta_state *d, int8_t f)
{
    return vinit_stm(d, f) != 0;
}

int divide_time(delta_state *d, uint8_t f, int32_t t, int16_t off)
{
    return vsplit_time(d, f, t, off) != 0;
}

/* Carry a mark from one node to another, leftwards or rightwards as asked.
   Neither end may be nothing. */
int project_sync(delta_state *d, int32_t l, int8_t f, int32_t r, int32_t back)
{
    if (l == 0 || r == 0)
        return 0;

    if (back) {
        if (!vproj_l(d, (delta_node *)(intptr_t)l, (delta_node *)(intptr_t)r,
                     (uint8_t)f))
            return 0;
    } else {
        if (!vproj_r(d, (delta_node *)(intptr_t)l, (delta_node *)(intptr_t)r,
                     (uint8_t)f))
            return 0;
    }

    return 1;
}

/* Whether joining two marks would leave the spine sound.
 *
 * A mark may always be joined with itself, and the spine's own two ends may
 * never be joined with each other. Past that it is a question asked of every
 * field in turn, and the four cases are which of the two marks carries that
 * field:
 *
 * both -- one has to link straight to the other, either way round;
 * one of them -- whatever the other one links to has to be on the right side
 *   of the one that carries it, which is what visleft and visright answer;
 * neither -- the two must not be threaded past each other, which is the same
 *   pair of questions asked the other way about.
 *
 * A field that fails any of those is a join that would cross something, and
 * the answer is no.
 */
static int safe_mergable(delta_state *d, int32_t l, int32_t r)
{
    delta_stack   *s = EVV_AT(delta_stack *, d->stack);
    int32_t        base = EVV_AT(delta_vars *, d->vars)->fence_base;
    const int32_t *L = NODE(l);
    const int32_t *R = NODE(r);
    int32_t        i;

    if (l == r)
        return 1;

    if (l == s->spine_l && r == s->spine_r)
        return 0;
    if (l == s->spine_r && r == s->spine_l)
        return 0;

    for (i = 0; i < d->nstmts; i++) {
        int32_t a;
        int32_t b;

        if ((L[base + i] & 1) != 0 && (R[base + i] & 1) != 0) {
            if ((L[OWN_WORDS + i] & LINK_MASK) == r)
                continue;
            if ((L[base + i] & LINK_MASK) == r)
                continue;
            return 0;
        }

        if ((L[base + i] & 1) != 0) {
            a = R[OWN_WORDS + i] & LINK_MASK;
            b = R[base + i] & LINK_MASK;

            if (l != a && !visleft(d, a, l))
                return 0;
            if (l != b && !visright(d, b, l))
                return 0;
            continue;
        }

        if ((R[base + i] & 1) != 0) {
            a = L[OWN_WORDS + i] & LINK_MASK;
            b = L[base + i] & LINK_MASK;

            if (r != a && !visleft(d, a, r))
                return 0;
            if (r != b && !visright(d, b, r))
                return 0;
            continue;
        }

        if (visleft(d, L[base + i] & LINK_MASK, R[OWN_WORDS + i] & LINK_MASK))
            return 0;
        if (visright(d, L[OWN_WORDS + i] & LINK_MASK, R[base + i] & LINK_MASK))
            return 0;
    }

    return 1;
}

/* Carry a mark across and then join the two, which is what a rule asks for
   when it wants one statement where there were two. The mark has to be there
   to begin with, the carry has to take, and the join has to be sound. */
int merge_sync(delta_state *d, int32_t l, int8_t f, int32_t r)
{
    if ((NODE(r)[EVV_AT(delta_vars *, d->vars)->fence_base + f] & 1) == 0)
        return 0;

    if (!project_sync(d, l, f, r, 0))
        return 0;

    if (!safe_mergable(d, l, r))
        return 0;

    if (!vmerge(d, r, l))
        return 0;

    return 1;
}

/* Whether a sync mark stands at this field of a node. The language's
   fields do not start at zero in a node's words; the machine says where
   they do. */
int sync_in_stm(delta_state *d, int8_t f, int32_t at)
{
    return (NODE(at)[EVV_AT(delta_vars *, d->vars)->fence_base + f] & 1) != 0;
}

/* Take one mark out, and the pair between two. Both answer one whether or
   not anything was there. */
int del_sync(delta_state *d, int8_t f, int32_t at)
{
    return vdel_1pt(d, f, at, 0) != 0;
}

int del_two_point(delta_state *d, int8_t f, int32_t l, int32_t r)
{
    vdel_2pt(d, f, l, r);
    return 1;
}

/* Put a mark in, to the left or to the right of where we are, and answer
   where we end up. */
int32_t ins_sync(delta_state *d, int8_t f, int32_t at, int32_t left)
{
    if (left) {
        vins_sync(d, f, NODE(at)[OWN_WORDS + f] & LINK_MASK, at);
        return sync_to_left(d, f, at);
    }

    vins_sync(d, f, at, NODE(at)[EVV_AT(delta_vars *, d->vars)->fence_base + f] & LINK_MASK);
    return sync_to_right(d, f, at);
}


/* ---- a field's value, as a number and as text ---- */

/* The value of the first field of a walkable type, which is the only one
   that carries a duration. Anything else answers minus one. */
int32_t time_field_value(int8_t f, void *tok)
{
    int32_t kind;

    if (!(signed char)vstmtbl[f].walkable)
        return -1;

    kind = FD(f, 0)->kind;
    if (kind == KIND_LONG)
        return *(int32_t *)READ(f, 0, tok);
    if (kind == KIND_INT)
        return *(int16_t *)READ(f, 0, tok);
    return -1;
}

/* One field of one token written out. The answer is a static, so it lives
   only until the next call. */
char *field_value(int8_t f, void *tok, int32_t fld)
{
    static char buf[0x54];

    const delta_fielddesc *fd    = FD(f, fld);
    const char *const     *names = (const char *const *)fd->values;

    switch (fd->kind) {
    case KIND_NAMED8:
        strcpy(buf, names[*(uint8_t *)READ(f, fld, tok)]);
        if (strcmp(buf, UNDEFINED) == 0)
            strcpy(buf, "---");
        else if (strcmp(buf, " ") == 0)
            strcpy(buf, "' '");
        break;

    case KIND_NAMED_W:
        strcpy(buf, names[*(int32_t *)READ(f, fld, tok)]);
        if (strcmp(buf, UNDEFINED) == 0)
            strcpy(buf, "---");
        else if (strcmp(buf, " ") == 0)
            strcpy(buf, "' '");
        break;

    case KIND_LONG: {
        int32_t v = *(int32_t *)READ(f, fld, tok);

        if (fd->flag && v == GAP_LONG)
            strcpy(buf, "GAP");
        else
            sprintf(buf, fd->format, v);
        break;
    }

    case KIND_INT: {
        int16_t v = *(int16_t *)READ(f, fld, tok);

        if (fd->flag && v == GAP_INT)
            strcpy(buf, "GAP");
        else
            sprintf(buf, fd->format, (int)v);
        break;
    }

    default:
        strcpy(buf, "???");
        break;
    }

    return buf;
}


/* ---- reading text back into a value ---- */

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

/* Walking the names a field can take.
 *
 * A field whose values are named rather than numbered carries the list in
 * its descriptor, and this is how the outside reads it: first_fieldval sets
 * the walk up and answers the first name, next_fieldval answers each one
 * after it, and nothing is answered when the list runs out. Where the walk
 * stands lives in the stack block rather than in the caller's hands, so only
 * one walk can be under way at a time -- the original's arrangement, kept.
 *
 * The prefix filters: a name is answered only if it starts with it. An empty
 * prefix answers every name, which is the fast arm at the top. A prefix that
 * is nothing but dashes is the odd one: it answers whatever the field calls
 * its undefined value, and that is what the flag set up beside it is for.
 */
const char *next_fieldval(delta_state *d)
{
    delta_stack           *s = EVV_AT(delta_stack *, d->stack);
    const delta_fielddesc *fd = FD(s->vals_stm, s->vals_fld);
    const char *const     *names = (const char *const *)fd->values;
    const char            *want = EVV_AT(const char *, s->vals_str);

    s->vals_at++;

    if (s->vals_at < fd->nvalues && (want == 0 || *want == 0))
        return names[s->vals_at];

    for (;;) {
        if (s->vals_at >= fd->nvalues)
            return 0;

        if (strprefix(names[s->vals_at], want))
            return names[s->vals_at];

        if (s->vals_dashes != 0
            && strcmp(names[s->vals_at], "undefined") == 0)
            return names[s->vals_at];

        s->vals_at++;
    }
}

const char *first_fieldval(delta_state *d, int8_t stm, int32_t fld,
                           const char *want)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    s->vals_stm = stm;
    s->vals_fld = fld;
    s->vals_str = EVV_REF(want);
    s->vals_at = -1;
    s->vals_dashes = want != 0 ? allchrs(want, '-') : 0;

    return next_fieldval(d);
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
   pointer and not the thing. */
int non_unique_value(delta_state *d, int8_t f, int32_t fld, const char *s,
                     const char **out_name, void **out_value)
{
    static int16_t lfound;
    static int8_t  sfound;
    static long    lval;
    static int     ival;

    const char *const *names;
    int32_t            kind;
    int16_t            j;

    if (*s == 0)
        return 0;

    kind = FD(f, fld)->kind;

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

    names  = (const char *const *)FD(f, fld)->values;
    lfound = -1;

    if (allchrs(s, '-')) {
        for (j = 0; j < FD(f, fld)->nvalues; j++)
            if (strcmp(names[j], UNDEFINED) == 0) {
                lfound = j;
                break;
            }
    }

    if (lfound == -1) {
        for (j = 0; j < FD(f, fld)->nvalues; j++)
            if (strprefix(names[j], s)) {
                lfound = j;
                break;
            }
    }

    if (lfound == -1)
        return 0;

    *out_name = names[lfound];
    if (strcmp(*out_name, UNDEFINED) == 0)
        *out_name = EVV_AT(const char *, EVV_AT(delta_stack *, d->stack)->undefined_text);

    if (FD(f, fld)->kind == KIND_NAMED8) {
        sfound     = (int8_t)lfound;
        *out_value = &sfound;
    } else {
        *out_value = &lfound;
    }

    return 1;
}

/* The same question asked strictly: a name only counts if it is the only one
 * the string could mean.
 *
 * Where non_unique_value takes the first name the string is a prefix of and
 * stops, this one goes on looking and gives up the moment a second matches.
 * Two smaller differences follow from that. The run of dashes and the
 * ordinary prefix are not alternatives here -- the dash pass runs and then
 * the prefix pass runs over the whole list as well, so a string that is both
 * is two matches and therefore none. And nothing breaks out of either loop
 * early, since finding one match is not the end of the question.
 *
 * The statics are the original's: what is answered is a pointer into them,
 * so a second call overwrites what the first handed back.
 */
int unique_value(delta_state *d, int8_t f, int32_t fld, const char *s,
                 const char **out_name, void **out_value)
{
    static int16_t lfound;
    static int8_t  sfound;
    static long    lval;
    static int     ival;

    const char *const *names;
    int32_t            kind;
    int16_t            j;

    if (*s == 0)
        return 0;

    kind = FD(f, fld)->kind;

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

    names  = (const char *const *)FD(f, fld)->values;
    lfound = -1;

    if (allchrs(s, '-')) {
        for (j = 0; j < FD(f, fld)->nvalues; j++)
            if (strcmp(names[j], UNDEFINED) == 0) {
                if (lfound != -1)
                    return 0;
                lfound = j;
            }
    }

    for (j = 0; j < FD(f, fld)->nvalues; j++)
        if (strprefix(names[j], s)) {
            if (lfound != -1)
                return 0;
            lfound = j;
        }

    if (lfound == -1)
        return 0;

    *out_name = names[lfound];
    if (strcmp(*out_name, UNDEFINED) == 0)
        *out_name = EVV_AT(const char *,
                           EVV_AT(delta_stack *, d->stack)->undefined_text);

    if (FD(f, fld)->kind == KIND_NAMED8) {
        sfound     = (int8_t)lfound;
        *out_value = &sfound;
    } else {
        *out_value = &lfound;
    }

    return 1;
}

/* Whether a string could still become a value of this field, and whether a
 * single character could still be the start of one.
 *
 * These are what something offering a person a choice asks as the text is
 * typed: not "is this a value" but "could it yet be one". A numbered field
 * answers by whether the text reads as a number at all, and a character by
 * whether it is a digit or the minus sign. A named field answers by whether
 * any of its names begins that way, with a run of dashes standing for the
 * undefined one as everywhere else here.
 *
 * Neither takes the machine: the answer is in the language's own table and
 * nothing about the spine comes into it.
 */
int valid_prefix(int8_t f, int32_t fld, const char *s)
{
    const char *const *names;
    int32_t            kind = FD(f, fld)->kind;
    int32_t            j;
    int                ok = 0;

    if (kind == KIND_INT)
        return legal_int(s, 0);
    if (kind == KIND_LONG)
        return legal_long(s, 0);
    if (kind < KIND_LONG || kind >= 0)
        return 0;

    names = (const char *const *)FD(f, fld)->values;

    if (allchrs(s, '-')) {
        for (j = 0; j < FD(f, fld)->nvalues; j++)
            if (strcmp(names[j], UNDEFINED) == 0) {
                ok = 1;
                break;
            }
    }

    for (j = 0; j < FD(f, fld)->nvalues; j++)
        if (strprefix(names[j], s)) {
            ok = 1;
            break;
        }

    return ok;
}

int valid_prefix_char(int8_t f, int32_t fld, char c)
{
    const char *const *names;
    int32_t            kind = FD(f, fld)->kind;
    int32_t            j;

    if (kind < KIND_INT)
        return 0;
    if (kind <= KIND_LONG)
        return c == '-' || isdigit((unsigned char)c) ? 1 : 0;
    if (kind >= 0)
        return 0;

    names = (const char *const *)FD(f, fld)->values;

    if (c == '-') {
        for (j = 0; j < FD(f, fld)->nvalues; j++)
            if (strcmp(names[j], UNDEFINED) == 0)
                return 1;
    }

    for (j = 0; j < FD(f, fld)->nvalues; j++)
        if (names[j][0] == c)
            return 1;

    return 0;
}


/* ---- putting things in ---- */

/* Put one token in, named. The name only matters in that a token called
   GAP is flagged as one. */
int ins_tok_named(delta_state *d, int8_t f, const void *value,
                  const char *name, int32_t at)
{
    delta_operand v;
    int32_t       l;

    v.ptr  = (void *)value;
    v.kind = FD(f, 0)->kind;
    v.flag = (int8_t)(strcmp(name, "GAP") == 0);

    l = sync_to_left(d, f, at);
    return vins_tok(d, f, l, at, &v) != 0;
}

/* Set one field of whatever stands to the left of a mark. */
int set_fld_value(delta_state *d, int8_t f, int32_t fld, const void *value,
                  int32_t unused, int32_t at)
{
    int32_t l;

    (void)unused;
    l = sync_to_left(d, f, at);
    vmark(d, f, (uint8_t)fld, l, at, value);
    return 1;
}

/* Read the text of every token between two marks into a buffer, up to a
   length. Answers where it was put, or nothing if the marks were the wrong
   way round. The length counts the terminator, so one gets an empty
   string. */
char *extract_string(delta_state *d, int8_t f, int32_t l, int32_t r,
                     char *out, int32_t max)
{
    char *p = out;

    if (!syncmark_order(d, l, r))
        return 0;

    max--;
    while (!syncmark_equ(l, r) && max != 0) {
        if (is_token_next(d, f, l)) {
            void *tok = (void *)(intptr_t)next_token(d, f, l);
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

/* Put a string in one character at a time, each as its own token with a
   sync mark between. Only a type whose first field names single characters
   can take one, and any character the field does not name stops it; what
   went in before the refusal stays in. */
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
        const char *const *names = (const char *const *)FD(f, fld)->values;
        int32_t            j = 0;

        one[0] = *s;

        while (j < FD(f, fld)->nvalues) {
            if (strcmp(names[j], one) == 0)
                break;
            j++;
        }
        if (j >= FD(f, fld)->nvalues)
            return 0;

        if (FD(f, fld)->kind == KIND_NAMED8) {
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

/* Whether a mark may be taken out: not if it is an end of the spine, and
   not if the tokens either side of it disagree about anything, since taking
   it out would leave the two of them as one.

   The field loop starts at one and reads field zero's kind for every field
   it looks at. That is what the original does, and English declares one
   kind per type, so the two never come apart. */
int can_del_sync(delta_state *d, int8_t f, int32_t at)
{
    int32_t prev, next;
    int32_t i;

    if (at == EVV_AT(delta_stack *, d->stack)->spine_l || at == EVV_AT(delta_stack *, d->stack)->spine_r)
        return 0;

    if (!is_token_next(d, f, at) || !is_token_prev(f, at))
        return 1;

    prev = prev_token(f, at);
    next = next_token(d, f, at);

    if (!(signed char)vstmtbl[f].walkable) {
        if (strcmp(field_value(f, (void *)(intptr_t)prev, 0), "GAP") != 0)
            return 0;
        if (strcmp(field_value(f, (void *)(intptr_t)next, 0), "GAP") != 0)
            return 0;
    }

    for (i = 1; i < vstmtbl[f].nfields; i++) {
        void *a = (void *)(intptr_t)prev;
        void *b = (void *)(intptr_t)next;

        switch (FD(f, 0)->kind) {
        case KIND_NAMED8:
            if (*(uint8_t *)READ(f, i, a) != *(uint8_t *)READ(f, i, b))
                return 0;
            break;
        case KIND_NAMED_W:
        case KIND_LONG:
            if (*(int32_t *)READ(f, i, a) != *(int32_t *)READ(f, i, b))
                return 0;
            break;
        case KIND_INT:
            if (*(int16_t *)READ(f, i, a) != *(int16_t *)READ(f, i, b))
                return 0;
            break;
        default:
            return 0;
        }
    }

    return 1;
}


/* ---- which streams and fields a caller wants ---- */

/* How many fields a whole list adds up to. */
int32_t num_fields(const stream_list *list)
{
    int32_t n = 0;
    int32_t i;

    for (i = 0; i < list->n; i++)
        n += list->e[i].nfields;
    return n;
}

/* How many entries one stream's part of a specification holds: a count
   byte, then that many field bytes, of which the zero ones are skipped.
   The stream itself always counts, hence the one. */
static int32_t fields_listed(const uint8_t *spec, int32_t at)
{
    int32_t n = spec[at];
    int32_t k = 1;
    int32_t i;

    at++;
    for (i = 0; i < n; i++)
        if (spec[at + i] != 0)
            k++;
    return k;
}

void free_stream_list(stream_list *list)
{
    int32_t i;

    for (i = 0; i < list->n; i++)
        free(list->e[i].fields);
    free(list->e);
    list->e = 0;
}

/* Build the list. An empty specification means every stream and every field
   of it; otherwise the specification is a count of streams followed, for
   each, by the stream number, a count of fields and those fields.

   The field arrays are asked for four bytes an entry and used one byte an
   entry, which is what the original does and what the readers above expect.
   The failure check afterwards looks at the last entry, so a list of no
   streams at all would read behind the array; no language declares one. */
int fill_stream_list(delta_state *d, stream_list *list, const uint8_t *spec)
{
    int32_t i, j;

    if (spec[0] == 0) {
        list->n = d->nstmts;
        list->e = calloc(list->n, sizeof(stream_entry));
        if (list->e == 0)
            return 0;

        for (i = 0; i < list->n; i++) {
            list->e[i].stm     = (int8_t)i;
            list->e[i].nfields = vstmtbl[i].nfields;
            list->e[i].fields  = calloc(list->e[i].nfields, 4);
            if (list->e[i].fields == 0)
                break;
            for (j = 0; j < list->e[i].nfields; j++)
                list->e[i].fields[j] = (uint8_t)j;
        }
    } else {
        int32_t k = 0;

        list->n = spec[k];
        k++;
        list->e = calloc(list->n, sizeof(stream_entry));
        if (list->e == 0)
            return 0;

        for (i = 0; i < list->n; i++) {
            int32_t left, mark, m;

            list->e[i].stm = (int8_t)spec[k];
            k++;

            left = spec[k];
            mark = k;
            list->e[i].nfields = fields_listed(spec, mark);
            k++;

            list->e[i].fields = calloc(list->e[i].nfields, 4);
            if (list->e[i].fields == 0)
                break;

            list->e[i].fields[0] = 0;
            m = 1;
            while (left > 0) {
                if (spec[k] != 0) {
                    list->e[i].fields[m] = spec[k];
                    m++;
                }
                left--;
                k++;
            }
        }
    }

    if (list->e[list->n - 1].fields == 0) {
        free_stream_list(list);
        return 0;
    }
    return 1;
}

/* Walk the list a field at a time. The cursors live on the stack block, so
   only one walk may be going at once. */
int first_field(delta_state *d, const stream_list *list, int8_t *out_stm,
                int32_t *out_fld)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    s->list_val = 0;
    s->list_fld = 0;

    if (s->list_fld >= list->n)
        return 0;
    if (s->list_val >= list->e[s->list_fld].nfields)
        return 0;

    *out_stm = list->e[s->list_fld].stm;
    *out_fld = list->e[s->list_fld].fields[s->list_val];
    return 1;
}

int next_field(delta_state *d, const stream_list *list, int8_t *out_stm,
               int32_t *out_fld)
{
    delta_stack *s = EVV_AT(delta_stack *, d->stack);

    s->list_val++;
    if (s->list_val >= list->e[s->list_fld].nfields) {
        s->list_fld++;
        if (s->list_fld >= list->n)
            return 0;
        s->list_val = 0;
    }

    *out_stm = list->e[s->list_fld].stm;
    *out_fld = list->e[s->list_fld].fields[s->list_val];
    return 1;
}


/* ---- the variable list a caller is handed ---- */

/* Two bytes of length, most significant first, then a pair of bytes per
   variable. */
int32_t pvlist_size(const uint8_t *p)
{
    if (p == 0)
        return 0;
    return (((int32_t)p[0] << 8) | p[1]) / 2;
}

const char *pvlist_name(delta_state *d, const uint8_t *p, int32_t i)
{
    void *vd = vardesc(d, p[2 + i * 2], p[3 + i * 2], 0);

    if (vd == 0)
        return "";
    return *(const char **)vd;
}

int32_t pvlist_val(delta_state *d, const uint8_t *p, int32_t i)
{
    return *(int32_t *)varloc(d, p[2 + i * 2], p[3 + i * 2], 0);
}
