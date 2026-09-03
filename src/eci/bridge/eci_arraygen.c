/* Turning a language's arrays into a stream of synthesiser frames.
 *
 * A Delta array is a sparse thing: a stream of values, each at some offset,
 * with nothing said about the gaps. The synthesiser wants the opposite -- a
 * full frame of sixty-two parameters every step, for ever. What sits between
 * the two is a value set: one cursor per stream, each remembering the pair of
 * points it is currently between, so asking for a value at a moment is a
 * matter of walking the cursor forward and interpolating between them.
 *
 * The interpolation is a straight line in whole numbers: how far through the
 * gap the moment is, times how much the value changes across it, divided by
 * how wide the gap is. A gap of no width, or no change, answers the value it
 * started at.
 *
 * The frame handed on is filled from a set of defaults, then overwritten by
 * whichever streams the caller's map names, and its first word is the step --
 * shortened on the last frame so the run ends where it was asked to.
 */

#include <stdint.h>
#include <stdlib.h>
#include "delta.h"
#include "klatt_state.h"
#include "klatt_lang.h"

/* How many parameters a frame carries. */
#define FRAME_PARMS 0x3e

/* One cursor: the two points it is between, and the gap worked out from
   them. */
typedef struct Cursor {
    int32_t at_l;      /* +0x00, where the left point is */
    int32_t val_l;     /* +0x04 */
    int32_t at_r;      /* +0x08, where the right point is; -1 means unstarted */
    int32_t val_r;     /* +0x0c */
    int32_t span;      /* +0x10, how wide the gap is */
    int32_t rise;      /* +0x14, how much the value changes across it */
} Cursor;

typedef struct ValueSet {
    Cursor  *cursors;  /* +0x00, one per stream */
    int32_t  from;     /* +0x04 */
    int32_t  to;       /* +0x08 */
} ValueSet;

/* What the array generator hangs off the machine. */
#define GEN(d)       EVV_AT(DeltaLang *, ((delta_state *)(d))->dlang)
#define GEN_SET(d)      (*(ValueSet **)&GEN(d)->set_a)
#define GEN_AT(d)       (GEN(d)->at)
#define GEN_KLATT(d)    (GEN(d)->klatt)
#define GEN_END(d)      (GEN(d)->spoken)
#define GEN_STARTED(d)  (GEN(d)->marked)
#define GEN_LIMIT(d)    (GEN(d)->queued)

extern int32_t streamArrayCount(delta_state *d);
extern int32_t arrayStreamLastOffset(delta_state *d, int32_t stream);
extern int32_t arrayStreamFirstVal(delta_state *d, int32_t stream,
                                   int32_t *at, int32_t *val);
extern int32_t arrayStreamNextVal(delta_state *d, int32_t stream,
                                  int32_t *at, int32_t *val);

static void valueSetReset(delta_state *d, ValueSet *vs, int32_t from,
                          int32_t to);

/* Every cursor unstarted, and the window recorded. */
static void valueSetReset(delta_state *d, ValueSet *vs, int32_t from,
                          int32_t to)
{
    int32_t i;

    vs->from = from;
    vs->to = to;

    for (i = 0; i < streamArrayCount(d); i++)
        vs->cursors[i].at_r = -1;
}

static ValueSet *valueSetNew(delta_state *d, int32_t from, int32_t to)
{
    ValueSet *vs = (ValueSet *)malloc(sizeof(ValueSet));

    if (!vs)
        return 0;

    vs->cursors = (Cursor *)calloc((size_t)streamArrayCount(d),
                                   sizeof(Cursor));
    if (!vs->cursors) {
        free(vs);
        return 0;
    }

    valueSetReset(d, vs, from, to);
    return vs;
}

static int32_t valueSetDelete(ValueSet *vs)
{
    if (vs) {
        if (vs->cursors)
            free(vs->cursors);
        free(vs);
    }
    return 0;
}

static int32_t valueSetInRange(const ValueSet *vs, int32_t at)
{
    return at >= vs->from && at < vs->to;
}

/* Walk one stream's cursor forward until the moment falls inside the gap it
   is holding, then read off the line between the two points. A stream that
   has run out holds its last value to the end of the window. */
static int32_t valueSetValue(delta_state *d, ValueSet *vs, int32_t stream,
                             int32_t at)
{
    Cursor *c = &vs->cursors[stream];

    while (at > c->at_r) {
        int32_t next = 0;
        int32_t val = 0;

        if (c->at_r == -1) {
            /* Nothing read yet: take the first point, or give up on the
               stream and hold nought to the end. */
            if (arrayStreamFirstVal(d, stream, &val, &next)) {
                c->at_l = 0;
                c->val_l = next;
                c->at_r = val;
                c->val_r = next;
            } else {
                c->at_l = 0;
                c->val_l = 0;
                c->at_r = vs->to;
                c->val_r = 0;
            }
        } else {
            /* Step on: what was the right point becomes the left one. */
            c->at_l = c->at_r;
            c->val_l = c->val_r;
            if (arrayStreamNextVal(d, stream, &val, &next)) {
                c->at_r = val;
                c->val_r = next;
            } else {
                c->at_r = vs->to;
            }
        }
    }

    c->span = c->at_r - c->at_l;
    c->rise = c->val_r - c->val_l;

    if (c->rise == 0 || c->span == 0)
        return c->val_l;

    return (at - c->at_l) * c->rise / c->span + c->val_l;
}

/* How far the streams the map names actually go. Nothing is generated past
   the first one that runs out. */
static int32_t definitionBoundary(delta_state *d, const int32_t *map,
                                  int32_t to)
{
    int32_t best = to;
    int32_t i;

    for (i = 0; i < FRAME_PARMS; i++) {
        int32_t stream = map[i + 1];
        int32_t last;

        if (stream == -1)
            continue;

        last = arrayStreamLastOffset(d, stream);
        if (last < best)
            best = last;
    }

    return best;
}

/* The language is done with its arrays. */
void dlangCleanup(delta_state *d)
{
    if (GEN_SET(d)) {
        valueSetDelete(GEN_SET(d));
        GEN_SET(d) = 0;
    }
}

/* Walk the window a step at a time, building a frame at each step and
   handing it to the synthesiser. Answers false if the synthesiser stopped,
   the caller interrupted, or the window ran out. */
int32_t sendArrayParameters(delta_state *d, int32_t from, int32_t to,
                            int32_t bounded, int32_t continuing,
                            int32_t exact, int32_t unused,
                            int32_t step, const int32_t *map,
                            const int32_t *defaults)
{
    int32_t frame[FRAME_PARMS];
    int32_t stopped = 0;
    int32_t at;
    int32_t i;

    (void)unused;

    if (from == 0)
        GEN_STARTED(d) = 0;
    GEN_LIMIT(d) = to;

    for (i = 0; i < FRAME_PARMS; i++)
        frame[i] = defaults[i];
    frame[0] = step;

    if (bounded) {
        if (!continuing)
            from = GEN_AT(d);
        if (!exact) {
            to = definitionBoundary(d, map, to);
            if (to < from)
                to = from;
            /* End on a whole step. */
            to -= (to - from) % step;
        }
        GEN_AT(d) = to;
    }
    GEN_END(d) = to;

    if (GEN_SET(d) == 0) {
        GEN_SET(d) = valueSetNew(d, from, to);
        if (GEN_SET(d) == 0)
            return 0;
    } else if (continuing || !bounded) {
        valueSetReset(d, GEN_SET(d), from, to);
    } else {
        GEN_SET(d)->to = to;
    }

    for (at = from; !stopped; at += step) {
        if (checkInterrupt(d))
            break;
        if (!valueSetInRange(GEN_SET(d), at))
            break;

        for (i = 0; i < FRAME_PARMS; i++) {
            if (map[i + 1] == -1)
                continue;
            frame[i] = valueSetValue(d, GEN_SET(d), map[i + 1], at);
        }

        /* The last frame is short, so the run stops where it was asked to. */
        if (at + step > to)
            frame[0] = at + step - to;

        if (!KlattSynth(GEN_KLATT(d), frame))
            stopped = 1;
    }

    return stopped == 0;
}
