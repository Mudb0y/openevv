/* Reading values off the spine and handing them to the synthesiser.
 *
 * This is the stream side of what arraygen does for arrays. Where an array
 * holds values at offsets, a stream holds them on the spine itself: the
 * value lives in a node, and how long it lasts is how far it is to the next
 * node that carries one. So walking forward is not arithmetic, it is walking
 * the spine, and that is what most of this file is.
 *
 * Two walks do it. One steps over nodes that carry nothing and stops at the
 * first that does. The other keeps going while the value stays the same, so
 * a run of identical values collapses into a single stretch. Both stop at
 * the end of the spine, and both report a failure through a flag the caller
 * passes in -- which is the same flag that stops the whole run, so a bad
 * value ends the sentence rather than being interpolated over.
 *
 * The interpolation itself is the same straight line in whole numbers as
 * the array side uses.
 */

#include <stdint.h>
#include <stdlib.h>
#include "delta.h"
#include "klatt_state.h"
#include "evv_arena.h"
#include "klatt_lang.h"

/* How many parameters a frame carries, and the one that means "no stream". */
#define FRAME_PARMS 0x3e
#define NO_STREAM   (-10)

/* What a value reader answers when it has nothing to say. */
#define NO_VALUE 0xffff8001

/* A node's own words, before the fields start. */
#define OWN_WORDS 3
#define FIELD(n, f)  (((int32_t *)(intptr_t)(n))[(f)])
#define LINK_MASK    (~3)
#define FENCED       1
#define SPACER       2

/* One cursor: the two nodes it is between, what each is worth and when, and
   the gap worked out from them. */
typedef struct Cursor {
    int32_t left;        /* +0x00 */
    int32_t left_at;     /* +0x04 */
    int32_t left_value;  /* +0x08 */
    int32_t right;       /* +0x0c */
    int32_t right_at;    /* +0x10 */
    int32_t right_value; /* +0x14 */
    int32_t at_left;     /* +0x18, what the left node itself was worth */
    int32_t span;        /* +0x1c */
    int32_t rise;        /* +0x20 */
} Cursor;

typedef struct ValueSet {
    Cursor *cursors;  /* +0x00, one per stream */
    int8_t  field;    /* +0x04, which field the walk follows */
    uint8_t pad_05[3];
    int32_t start;    /* +0x08, the node the run starts at */
    int32_t end;      /* +0x0c */
} ValueSet;

/* What the generator hangs off the machine. */
#define GEN(d)       EVV_AT(DeltaLang *, ((delta_state *)(d))->dlang)
#define GEN_SET(d)   (*(ValueSet **)&GEN(d)->set_b)
#define GEN_KLATT(d) (GEN(d)->klatt)

/* Which field of the caller's map says where a parameter comes from, and
   where the map keeps the field the walk follows. */
#define MAP_STREAM(m, i) (*(const int8_t *)((const char *)(m) + 4 + (i) * 4))
#define MAP_FIELD(m)     (*(const int8_t *)((const char *)(m) + 0xfc))

extern int32_t num_streams(delta_state *d);

static int32_t integerValue(int32_t node, int8_t field);
static int32_t moveRightOverSpaces(delta_state *d, int32_t node, int8_t field,
                                   int8_t valField, int32_t *out,
                                   int32_t *failed);
static int32_t moveRightOverVal(delta_state *d, int32_t node, int8_t field,
                                int8_t valField, int32_t *out,
                                int32_t *failed);
static void valueSetReset(delta_state *d, ValueSet *vs, int8_t field,
                          int32_t start, int32_t unused, int32_t end);

/* What one node is worth, read through the reader its statement type
   declares. Only the two narrow kinds carry a number. */
static int32_t integerValue(int32_t node, int8_t field)
{
    int32_t kind = STMTYP(field);
    void   *p;

    if (kind != -4 && kind != -3)
        return 0;

    p = ((void *(*)(void *))vstmtbl[field].get[0])(
            TFLDS((void *)(intptr_t)node));

    return kind == -4 ? *(int16_t *)p : *(int32_t *)p;
}

/* How far it is from one node to another, counted in whatever the field's
   values are worth. A node that carries nothing of its own is stepped
   through rather than counted. */
int32_t timeDuration(delta_state *d, int32_t from, int32_t to, int8_t field)
{
    int32_t total = 0;
    int32_t p = from;

    while (p != to) {
        if (p == EVV_AT(delta_stack *, d->stack)->spine_r)
            return 0;

        if (p != 0 && (*(int32_t *)(intptr_t)p & SPACER)) {
            p = FIELD(p, EVV_AT(delta_vars *, d->vars)->fence_base + field) & LINK_MASK;
        } else {
            total += integerValue(p, field);
            p = *(int32_t *)(intptr_t)(p + 4) & LINK_MASK;
        }
    }

    return total;
}

/* Forward to the first node that carries something. Answers the last node
   on the way that was fenced on the value field, so the caller knows where
   the stretch it just crossed began. */
static int32_t moveRightOverSpaces(delta_state *d, int32_t node, int8_t field,
                                   int8_t valField, int32_t *out,
                                   int32_t *failed)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t found = 0;
    int32_t p = node;

    for (;;) {
        int32_t next;

        if (p == EVV_AT(delta_stack *, d->stack)->spine_r)
            break;

        next = FIELD(p, base + field) & LINK_MASK;
        if (next != 0 && (*(int32_t *)(intptr_t)next & SPACER)) {
            p = next;
            if (FIELD(p, base + valField) & FENCED)
                found = p;
            continue;
        }

        *out = integerValue(next, field);
        if ((uint32_t)*out == NO_VALUE) {
            *failed = 1;
            return 0;
        }
        break;
    }

    return found;
}

/* Forward while the value stays the same, so a run of identical values is
   crossed in one go. Stops early if the stretch already has a duration. */
static int32_t moveRightOverVal(delta_state *d, int32_t node, int8_t field,
                                int8_t valField, int32_t *out,
                                int32_t *failed)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t found = 0;
    int32_t p = node;

    *out = (int32_t)NO_VALUE;

    for (;;) {
        int32_t next;

        if (p == EVV_AT(delta_stack *, d->stack)->spine_r) {
            found = p;
            break;
        }

        next = FIELD(p, base + field) & LINK_MASK;

        if (next != 0 && (*(int32_t *)(intptr_t)next & SPACER)) {
            if (FIELD(p, base + valField) & FENCED)
                found = p;

            if (FIELD(next, base + valField) & FENCED) {
                if (found != 0 && timeDuration(d, found, next, valField))
                    break;
                found = next;
                p = next;
            } else {
                p = next;
            }
        } else {
            int32_t v = integerValue(next, field);

            if ((uint32_t)*out != NO_VALUE && v != *out) {
                if ((uint32_t)v == NO_VALUE) {
                    *failed = 1;
                    return 0;
                }
                break;
            }
            *out = v;
            p = *(int32_t *)(intptr_t)(next + 4) & LINK_MASK;
            found = p;
        }
    }

    /* Landing anywhere unfenced, or with nothing to say, is a failure. */
    if (found == 0 || !(FIELD(found, base + valField) & FENCED))
        *failed = 1;
    if ((uint32_t)*out == NO_VALUE)
        *failed = 1;

    return found;
}

/* ---- the set of cursors --------------------------------------------- */

static void valueSetReset(delta_state *d, ValueSet *vs, int8_t field,
                          int32_t start, int32_t unused, int32_t end)
{
    int32_t i;

    (void)unused;

    vs->field = field;
    vs->start = start;
    vs->end = end;

    for (i = 0; i < num_streams(d); i++)
        vs->cursors[i].left = 0;
}

static ValueSet *valueSetNew(delta_state *d, int8_t field, int32_t start,
                             int32_t unused, int32_t end)
{
    ValueSet *vs = (ValueSet *)malloc(sizeof(ValueSet));

    if (!vs)
        return 0;

    vs->cursors = (Cursor *)calloc((size_t)num_streams(d), sizeof(Cursor));
    if (!vs->cursors) {
        free(vs);
        return 0;
    }

    valueSetReset(d, vs, field, start, unused, end);
    return vs;
}

static int32_t valueSetInRange(const ValueSet *vs, int32_t at)
{
    return at >= 0 && at < vs->end;
}

/* Walk one stream's cursor forward until the moment falls inside the stretch
   it is holding, then draw the line between the two ends. A stretch of no
   width, or no change across it, answers what it started at. */
static int32_t valueSetValue(delta_state *d, ValueSet *vs, int8_t stream,
                             int32_t at, int32_t *failed)
{
    int32_t base = EVV_AT(delta_vars *, d->vars)->fence_base;
    Cursor *c = &vs->cursors[stream];
    int32_t restarted = 0;

    while (c->left == 0 || at > c->right_at) {
        int32_t got;
        int32_t v = 0;

        if (c->left == 0) {
            restarted = 1;

            if (!(FIELD(vs->start, base + stream) & FENCED)) {
                /* The run does not start on this stream, so find where it
                   does and count backwards to it. */
                c->left = vgetsc(d, 1, 1, vs->start, (uint8_t)stream);
                while (!(FIELD(c->left, base + vs->field) & FENCED))
                    c->left = VLSYNC((const delta_node *)(intptr_t)c->left,
                                     stream);
                c->left_at = -timeDuration(d, c->left, vs->start, vs->field);
            } else {
                c->left = vs->start;
                c->left_at = 0;
            }

            {
                int32_t m = EVV_REF(vmovel(d,
                        (delta_node *)(intptr_t)c->left, (uint8_t)stream));
                int32_t held = FIELD(m, OWN_WORDS + stream) & LINK_MASK;

                c->at_left = held ? integerValue(held, stream) : 0;
            }
        } else {
            /* What was the right end becomes the left one. */
            c->left = c->right;
            c->left_at = c->right_at;
            c->at_left = c->right_value;
        }

        got = moveRightOverSpaces(d, c->left, stream, vs->field, &v, failed);
        if (*failed)
            return 0;

        if (got != 0) {
            c->right = got;
            c->left_value = c->at_left;
            if (c->right != EVV_AT(delta_stack *, d->stack)->spine_r) {
                c->right_value = v;
                c->rise = v - c->at_left;
            } else {
                c->right_value = c->at_left;
                c->rise = 0;
            }
        } else {
            c->right = moveRightOverVal(d, c->left, stream, vs->field, &v,
                                        failed);
            if (*failed)
                return 0;
            c->left_value = v;
            c->right_value = v;
            c->rise = 0;
        }

        if (restarted) {
            c->right_at = timeDuration(d, vs->start, c->right, vs->field);
            c->span = c->right_at - c->left_at;
        } else {
            c->span = timeDuration(d, c->left, c->right, vs->field);
            c->right_at = c->right_at + c->span;
        }
    }

    if (c->rise == 0 || c->span == 0)
        return c->left_value;

    return (at - c->left_at) * c->rise / c->span + c->left_value;
}

/* Walk the run a step at a time, building a frame and handing it over. */
int32_t sendStreamParameters(delta_state *d, int32_t start, int32_t unused,
                             int32_t end, int32_t continuing, int32_t a,
                             int32_t b, int32_t c, int32_t step,
                             const void *map, const int32_t *defaults)
{
    int32_t frame[FRAME_PARMS];
    int32_t stopped = 0;
    int8_t  field = MAP_FIELD(map);
    int32_t at;
    int32_t i;

    (void)b;
    (void)c;

    for (i = 0; i < FRAME_PARMS; i++)
        frame[i] = defaults[i];
    frame[0] = step;

    GEN_SET(d) = valueSetNew(d, field, start, unused, end);
    if (GEN_SET(d) == 0)
        return 0;

    /* Made and then reset again, which the original does whichever way it
       got here. Continuing a run pushes the end out first. */
    if (a || !continuing) {
        valueSetReset(d, GEN_SET(d), field, start, unused, end);
    } else {
        GEN_SET(d)->end += end;
        valueSetReset(d, GEN_SET(d), field, start, unused, end);
    }

    for (at = 0; !stopped; at += step) {
        if (checkInterrupt(d))
            break;
        if (!valueSetInRange(GEN_SET(d), at))
            break;

        for (i = 0; i < FRAME_PARMS; i++) {
            if (MAP_STREAM(map, i) == NO_STREAM)
                continue;
            /* A value that cannot be read stops the run, which is why the
               same flag serves both. */
            frame[i] = valueSetValue(d, GEN_SET(d), MAP_STREAM(map, i), at,
                                     &stopped);
        }

        if (at + step > end)
            frame[0] = at + step - end;

        if (!KlattSynth(GEN_KLATT(d), frame))
            stopped = 1;
    }

    return stopped == 0;
}
