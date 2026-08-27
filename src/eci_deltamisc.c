/* Odds and ends of the Delta runtime.
 *
 * Six primitives from five different objects that have nothing in common
 * except being small and standing on their own. They are here together
 * rather than in five files because each of them would otherwise be a file
 * with one function in it.
 *
 * Everything here takes the machine as its first argument and reaches what
 * it needs through it: the variable block at 0x68, the owner at 0x64, the
 * logical file table at 0x74. Where a field has no name in delta.h yet it is
 * named here by offset, because these are the only places that touch it.
 *
 * Names carry no prefix: these are plain C names in the original and ours
 * are the same, which is what stands the original's aside.
 */

#include <stdint.h>
#include <stdlib.h>
#include "delta.h"
#include "delta_lang.h"
#include "delta_rules_c.h"

/* What the owner keeps about the command line. Named fields and not the
   offsets they sat at: the owner block is ours, laid out by the compiler, and
   the two the original had here are 468 and 472 bytes in while the whole block
   is 64. Writing there wrote over whatever the arena had handed out next,
   which is how a run of engine instances corrupted the arena at about the
   twentieth. See the note on delta_owner in delta.h. */
#define OWNER_ARGC(d)   (EVV_AT(delta_owner *, (d)->owner)->argc)
#define OWNER_ARGV(d)   (EVV_AT(delta_owner *, (d)->owner)->argv)

extern int32_t logicalIOSetErrorCallback(delta_state *d, void *fn);

/* One word the variable block clears before a run. */
#define VARS_1128(d)    (*(int32_t *)((char *)(d)->vars + 0x1128))

extern int32_t vcmdinit(delta_state *d, int32_t argc, char **argv);
extern int32_t vinitrun(delta_state *d);

/* ---- misc ----------------------------------------------------------- */

/* Whatever the last C helper answered with is not to be believed until one
   actually runs, so it starts as the value nothing returns. */
void ccode_misc_new(delta_state *d)
{
    EVV_AT(delta_vars *, d->vars)->return_code = (uint8_t)-1;
}

void ccode_misc_delete(void)
{
}

/* The command language's own operations, which this engine never runs.
 *
 * Delta is two languages in one object: the rules, which is what a voice is
 * written in, and a command script that drives the machine from outside. The
 * engine is the library case, where the commands come through the ECI
 * interface instead, so every one of these is empty in the original -- not
 * stubbed by us, empty where IBM compiled it, which is why they take no
 * arguments here. A body that reads none cannot say how many its callers
 * push, and cdecl does not mind; a rule of ours that calls one with more
 * will need the declaration widened, and that is the moment to look at the
 * object again.
 *
 * The two that are not empty are the two ways a script stops, and both of
 * them end the process. That is the original's doing and it is worth knowing
 * before anything calls one: in a library the process is the host's, so
 * halting a script would take the caller's program down with it. The same is
 * true of vcmdend above, and is said there too.
 */
void noop1(delta_state *d)       { (void)d; }
void code_end(delta_state *d)    { (void)d; }
void goto_1(delta_state *d)      { (void)d; }
void c_code(delta_state *d)      { (void)d; }
void call(delta_state *d)        { (void)d; }
void call2(delta_state *d)       { (void)d; }
void execcmd(delta_state *d)     { (void)d; }
void startcmd(delta_state *d)    { (void)d; }
void startstmt(delta_state *d)   { (void)d; }
void startstmt_e(delta_state *d) { (void)d; }
void startstmt_l(delta_state *d) { (void)d; }
void tag(delta_state *d)         { (void)d; }
void tag_e(delta_state *d)       { (void)d; }
void tag_l(delta_state *d)       { (void)d; }
void nullines(delta_state *d)    { (void)d; }
void nullines_l(delta_state *d)  { (void)d; }
void fail(delta_state *d)        { (void)d; }

void halt(delta_state *d)
{
    (void)d;
    exit(0);
}

void abort_1(delta_state *d)
{
    (void)d;
    exit(5);
}

/* And one more empty body, from the printing layer rather than the command
   one. What it was for is writing a timing variable out where a person could
   read it; src/delta_trace.c says what happened to that layer and why. */
void prt_tvar(delta_state *d)
{
    (void)d;
}

/* Starting the machine when it is a library rather than a program. The
   count is one less than the caller's because the first word of a command
   line is the command; with nothing after it there is no argument vector at
   all. */
int32_t etiwinMainDLL(delta_state *d, int32_t argc, char **argv)
{
    OWNER_ARGC(d) = argc - 1;
    if (argc > 1)
        OWNER_ARGV(d) = (char **)argv[1];
    else
        OWNER_ARGV(d) = 0;

    VARS_1128(d) = 0;

    if (!vcmdinit(d, OWNER_ARGC(d), (char **)OWNER_ARGV(d)))
        return 0;
    if (!vinitrun(d))
        return 0;
    return 1;
}

/* And starting it as a program, which is what the command layer was written
 * for. The count and the vector are the caller's own here rather than one
 * short, since a program's argv[0] is its name and the layer wants what
 * follows; then the command layer comes up, the run is initialised, and the
 * machine is handed to the language's own main rule. It ends by ending the
 * process, which is the command layer's way out and is why a library uses
 * etiwinMainDLL above instead.
 *
 * One line of the original is not here. Between the main rule and the way
 * out it takes the length of a string and does nothing with it -- a leftover
 * of whatever printed that message once. Nothing can observe the difference.
 */
int32_t etiwinMain(delta_state *d, int32_t argc, char **argv)
{
    delta_owner *o = EVV_AT(delta_owner *, d->owner);

    o->argc = argc - 1;
    if (argc > 1)
        o->argv = argv + 1;
    else
        o->argv = 0;

    VARS_1128(d) = 0;

    if (!vcmdinit(d, o->argc, o->argv))
        return 1;
    if (!vinitrun(d))
        return 1;

    delta_lang_of(d)->proc_main(d);
    vcmdend(d, 0);
    return 0;
}

/* ---- dttime --------------------------------------------------------- */

/* The value of an expression over a run, which is what a rule asks for when
   it wants a duration or a field read across two positions.

   This was left out for years and the reason is worth keeping: its one
   caller is val_expr1, and val_expr1's callers are the command language's,
   so the chain is entered from nothing. Writing it pulls in val_expr2,
   durcalc and firstdefd -- about eight hundred instructions -- for a path no
   rule takes. It is here now because the machine is being finished rather
   than because something wants it, and vgen wants the same three. */
/* ---- dterror -------------------------------------------------------- */

/* Asked of the file table rather than reached into: the field sat at 0xc0 in
   the original and sits at 368 here, so writing at 0xc0 set nothing and trod
   on a physical file instead. */
int32_t dtSetErrorCallback(delta_state *d, void *fn)
{
    return logicalIOSetErrorCallback(d, fn);
}

/* ---- ctxt ----------------------------------------------------------- */

/* Set the flag bits of one entry, keeping the bottom two, which say
   something the caller is not allowed to disturb. Which entry depends on
   whether it is being counted from the table's own start or from where the
   fenced fields begin. */
void vsetsc(delta_state *d, int32_t fromStart, int32_t unused,
            int32_t *table, uint8_t idx, int32_t bits)
{
    int32_t at;

    (void)unused;

    if (fromStart)
        at = 3 + idx;
    else
        at = EVV_AT(delta_vars *, d->vars)->fence_base + idx;

    table[at] = (table[at] & 3) | bits;
}

/* ---- dictinit ------------------------------------------------------- */

/* One entry of a lookup set or a dictionary action. What goes in it is the
   lowest key its kind can hold, so that a search starting there finds the
   first real entry: nought for the plain kinds, and the sign bit set for the
   two that are compared as signed. The width comes from the kind too, and is
   written beside the value because the search needs it.

   A kind outside these four leaves the original copying whatever happened to
   be on its stack. No language declares one, and here it copies nothing.

   The original keeps this to itself, so nothing outside the file could call
   it; it is a static here for the same reason. */
static int32_t dictinit(delta_state *d, void *entry, int32_t isAction,
                        int32_t index)
{
    unsigned char *rec  = (unsigned char *)entry;
    int32_t        kind = vstmtbl[rec[8]].fields->kind;
    int32_t        width = 0;
    uint8_t        value[4];
    int            i;

    for (i = 0; i < 4; i++)
        value[i] = 0;

    switch (kind) {
    case -1:
        width = 1;
        break;
    case -2:
        width = 2;
        break;
    case -3:
        width = 4;
        value[0] = 0x01;
        value[3] = 0x80;
        break;
    case -4:
        width = 2;
        value[1] = 0x80;
        value[0] = 0x01;
        break;
    default:
        break;
    }

    for (i = 0; i < width; i++)
        rec[0x19 + i] = value[i];
    rec[0x18] = (uint8_t)width;

    /* Where the entries themselves live: one store per set, one per action. */
    if (isAction)
        *(evv_ref *)(rec + 4) =
            EVV_REF(delta_low_at(EVV_AT(const uint8_t *const *,
                                        d->act_store)[index]));
    else
        *(evv_ref *)(rec + 4) =
            EVV_REF(delta_low_at(EVV_AT(const uint8_t *const *,
                                        d->set_store)[index]));

    return 1;
}


/* Every lookup set and then every dictionary action, each told which it is
   and where it comes in the list. A language with neither is not an error. */
int32_t vdictinit(delta_state *d)
{
    int32_t i;

    if (d->nsets == 0 && d->nactions == 0)
        return 1;

    for (i = 0; i < d->nsets; i++) {
        if (!dictinit(d, EVV_AT(uint8_t *, d->sets) + i * 0x24, 0, i))
            return 0;
    }

    for (i = 0; i < d->nactions; i++) {
        if (!dictinit(d, EVV_AT(uint8_t *, d->act_table) + i * 0x28, 1, i))
            return 0;
    }

    return 1;
}


/* How long the run between two positions is, remembering the last answer.
 *
 * Asked outright -- the flag set -- it is dur2 and nothing more. Asked
 * through the cache it measures from where it measured last: the two nodes
 * it saw before are in the cache with the duration between them, so the new
 * duration is that one, plus the distance from the old right end to the new,
 * less the distance from the old left end to the new. The cache is then told
 * the new pair, with the offsets folded in so that a position part way into
 * a statement is not counted twice.
 */
int32_t durcalc(delta_state *d, delta_tpos *a, delta_tpos *b, int8_t f,
                int32_t *cache, int32_t direct)
{
    delta_tpos was_l;
    delta_tpos was_r;
    int32_t    v;

    if (direct)
        return dur2(d, a, b, f, 0);

    memset(&was_l, 0, sizeof was_l);
    memset(&was_r, 0, sizeof was_r);

    was_l.node = cache[0];
    was_l.offset = 0;
    was_l.flags = 1;

    was_r.node = cache[1];
    was_r.offset = 0;
    was_r.flags = 1;

    v = cache[2] + dur2(d, &was_r, b, f, 1) - dur2(d, &was_l, a, f, 1);

    cache[2] = v - b->offset + a->offset;
    cache[0] = a->node;
    cache[1] = b->node;

    return v;
}

/* The first position at or beyond one where the field is actually written.
 *
 * A sync is stepped over unless it carries the statement type asked about.
 * Anything else is looked at: a field whose value is nought is not written,
 * and the walk goes on in whichever direction was asked for. The first that
 * is written is the answer, and running out answers with where it started.
 *
 * A field kind other than the two integer widths leaves the original reading
 * an uninitialised word to decide with. None of the ten English types is
 * one; ours takes it as written rather than as undefined.
 */
int32_t firstdefd(delta_state *d, int8_t f, int32_t t, uint8_t st,
                  int32_t back)
{
    const delta_stmt *e = &vstmtbl[f];
    void *(*const get)(void *) = e->get[0];
    uint8_t           walkable = e->walkable;
    int32_t           base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int32_t           at = t;

    for (;;) {
        int32_t undefined = 0;

        if (at != 0 && (*(const int32_t *)(intptr_t)at & 2) != 0) {
            if ((((const int32_t *)(intptr_t)at)[base + st] & 1) != 0)
                return at;

            if (back)
                at = ((const int32_t *)(intptr_t)at)[base + f] & ~3;
            else
                at = ((const int32_t *)(intptr_t)at)[3 + f] & ~3;

            continue;
        }

        switch (e->fields[0].kind) {
        case DK_LONG:
            undefined = at != 0 && walkable
                        && *(const int32_t *)get(TFLDS((void *)(intptr_t)at))
                           == 0;
            break;

        case DK_SHORT2:
            undefined = at != 0 && walkable
                        && *(const int16_t *)get(TFLDS((void *)(intptr_t)at))
                           == 0;
            break;

        default:
            break;
        }

        if (!undefined)
            return t;

        if (back)
            at = *(const int32_t *)(intptr_t)(at + 4) & ~3;
        else
            at = *(const int32_t *)(intptr_t)at & ~3;
    }
}

/* What a field is worth at a position, measured across the run it sits in.
 *
 * The shape of it: find the two ends of the run, read the field at each, and
 * answer. If the two ends agree the answer is that value. If they do not, and
 * the run has a duration, the answer is the two interpolated by how far into
 * the run the position lies -- which is what durcalc is for and why the
 * caller is told, through `out', that the answer was worked out rather than
 * read. If there is no duration the selector decides: the left value, the
 * right, or the two averaged.
 *
 * Three things in it are the original's and are kept. A short field whose
 * value is the smallest short means undefined and is answered as such. The
 * walk that checks every mark between the ends agrees does not advance when
 * it meets something that is not a mark, so a spine that put one there would
 * hang -- ours hangs the same way. And the two positions handed to durcalc
 * have their field left unset, which is a word of stack in the original;
 * here it is nought.
 */
int32_t val_expr2(delta_state *d, delta_tpos *p, int8_t st, uint8_t fld,
                  int32_t which, int32_t mode, int32_t *out)
{
    delta_stack      *s = EVV_AT(delta_stack *, d->stack);
    int32_t           base = EVV_AT(delta_vars *, d->vars)->fence_base;
    int8_t            f = (int8_t)p->field;
    const delta_stmt *e = &vstmtbl[st];
    void *(*const     get)(void *) = e->get[fld];
    int32_t           sel;
    int32_t           direct;
    int32_t           l = 0;
    int32_t           r = 0;
    int32_t           lval = (int32_t)0x80000001;
    int32_t           rval = 0;
    int32_t           at;
    int32_t           at2;
    delta_tpos        lpos;
    delta_tpos        rpos;

    *out = 0;

    if (which != 0)
        sel = which;
    else if ((int8_t)vstmtbl[st].unknown_3c != 0)
        sel = vstmtbl[f].gen_sel;
    else
        sel = 0;

    if (mode == 9) {
        direct = 1;
        mode = vnormalize(d, p);

        switch (mode) {
        case 0:
        case 1:
            return (int32_t)0x80000001;

        case 2:
            if (p->offset < 0) {
                int32_t t = ((const int32_t *)(intptr_t)p->node)[3 + f] & ~3;

                l = gcql(d, *(const int32_t *)(intptr_t)t & ~3, st, f);
                r = gcqr(d, p->node, st, f);
            } else {
                int32_t t =
                    ((const int32_t *)(intptr_t)p->node)[base + f] & ~3;

                l = gcql(d, p->node, st, f);
                r = gcqr(d, ((const int32_t *)(intptr_t)t)[1] & ~3, st, f);
            }
            break;

        case 4:
            l = gcql(d, p->node, st, f);
            r = gcqr(d, p->node, st, f);
            break;

        case 3:
            {
                int32_t rr = firstdefd(d, f,
                    (int32_t)(intptr_t)rmost(d, f,
                        (int32_t *)(intptr_t)p->node), st, 0);
                int32_t ll = firstdefd(d, f,
                    (int32_t)(intptr_t)lmost(d, f,
                        (delta_node *)(intptr_t)p->node), st, 1);

                l = gcql(d, ll, st, f);
                r = gcqr(d, rr, st, f);
            }
            break;

        default:
            break;
        }
    } else {
        direct = 0;
        l = EVV_AT(const int32_t *, s->expr_l)[st];
        r = EVV_AT(const int32_t *, s->expr_r)[st];
    }

    /* Every mark between the two ends has to say the same thing. */
    at = l;
    while (at != r) {
        int32_t v;

        if (at == 0)
            continue;
        if ((*(const int32_t *)(intptr_t)at & 2) == 0)
            continue;

        at = ((const int32_t *)(intptr_t)at)[base + st] & ~3;

        v = 0;
        if (e->fields[fld].kind == DK_LONG) {
            v = *(const int32_t *)get(TFLDS((void *)(intptr_t)at));
        } else if (e->fields[fld].kind == DK_SHORT2) {
            v = *(const int16_t *)get(TFLDS((void *)(intptr_t)at));
            if (v == -32767)
                v = (int32_t)0x80000001;
        }

        if (lval == (int32_t)0x80000001)
            lval = v;
        else if (lval != v)
            return (int32_t)0x80000001;

        at = ((const int32_t *)(intptr_t)at)[1] & ~3;
    }

    if (lval != (int32_t)0x80000001)
        return lval;

    memset(&lpos, 0, sizeof lpos);
    memset(&rpos, 0, sizeof rpos);
    lpos.offset = 0;
    lpos.flags = 1;
    rpos.offset = 0;
    rpos.flags = 1;

    /* The statement at the left end, and what the field says there. */
    at = l;
    while (at != 0 && (*(const int32_t *)(intptr_t)at & 2) != 0)
        at = ((const int32_t *)(intptr_t)at)[3 + st] & ~3;

    if (at != 0) {
        int32_t t = ((const int32_t *)(intptr_t)at)[1] & ~3;

        if ((((const int32_t *)(intptr_t)t)[base + f] & 1) != 0)
            lpos.node = t;
        else
            lpos.node = vgetsc(d, 0, 1, t, (uint8_t)f);

        if (e->fields[fld].kind == DK_LONG) {
            lval = *(const int32_t *)get(TFLDS((void *)(intptr_t)at));
        } else if (e->fields[fld].kind == DK_SHORT2) {
            lval = *(const int16_t *)get(TFLDS((void *)(intptr_t)at));
            if (lval == -32767)
                lval = (int32_t)0x80000001;
        }
    }

    /* And the statement at the right end. */
    at2 = r;
    while (at2 != 0 && (*(const int32_t *)(intptr_t)at2 & 2) != 0)
        at2 = ((const int32_t *)(intptr_t)at2)[base + st] & ~3;

    if (at2 != 0) {
        int32_t t = *(const int32_t *)(intptr_t)at2 & ~3;

        if ((((const int32_t *)(intptr_t)t)[base + f] & 1) != 0)
            rpos.node = t;
        else
            rpos.node = vgetsc(d, 1, 1, t, (uint8_t)f);

        if (e->fields[fld].kind == DK_LONG) {
            rval = *(const int32_t *)get(TFLDS((void *)(intptr_t)at2));
        } else if (e->fields[fld].kind == DK_SHORT2) {
            rval = *(const int16_t *)get(TFLDS((void *)(intptr_t)at2));
            if (rval == -32767)
                rval = (int32_t)0x80000001;
        }
    }

    if (at != 0 && at2 != 0) {
        int32_t span = durcalc(d, &lpos, &rpos, f,
                               &EVV_AT(int32_t *, s->dur_cache_a)[st * 3],
                               direct);

        if (span != 0 && span != (int32_t)0x80000001) {
            int32_t into;

            *out = 1;

            into = durcalc(d, &lpos, p, f,
                           &EVV_AT(int32_t *, s->dur_cache_b)[st * 3],
                           direct);
            if (into == (int32_t)0x80000001)
                into = 0;

            return lval + (into * 1000 / span) * (rval - lval) / 1000;
        }

        switch (sel) {
        case -1: return lval;
        case 0:  return (lval + rval) / 2;
        case 1:  return rval;
        default: return (int32_t)0x80000001;
        }
    }

    if (at != 0) {
        int32_t span = durcalc(d, &lpos, p, f,
                               &EVV_AT(int32_t *, s->dur_cache_b)[st * 3],
                               direct);

        if (span != 0 && span != (int32_t)0x80000001)
            return (int32_t)0x80000001;
        return lval;
    }

    if (at2 != 0) {
        int32_t span = durcalc(d, p, &rpos, f,
                               &EVV_AT(int32_t *, s->dur_cache_c)[st * 3],
                               direct);

        if (span != 0 && span != (int32_t)0x80000001)
            return (int32_t)0x80000001;
        return rval;
    }

    return (int32_t)0xffff8001;
}

/* The two names a caller asks by. One fills in the mode that means "work the
   position out for yourself" and a scratch word for the answer; the other
   takes the value into a location, and treats the undefined answer as a
   fault rather than a value. */
int32_t val_expr(delta_state *d, delta_tpos *p, int8_t st, uint8_t fld,
                 int32_t which)
{
    int32_t worked_out = 0;

    return val_expr2(d, p, st, fld, which, 9, &worked_out);
}

void val_expr1(delta_state *d, delta_loc *loc, uint8_t st, uint8_t fld)
{
    int32_t       v = val_expr(d, &d->lpta, (int8_t)st, fld, 0);
    delta_operand src;
    delta_operand dst;

    if (v == (int32_t)0x80000001) {
        reset_field(loc);
        forceErrorBacktrack(d);
    }

    src.ptr = &v;
    src.kind = DK_LONG;
    src.flag = vstmtbl[st].fields[fld].flag;

    vinitloc_new(d, &dst, loc);
    vassign(d, &dst, &src);
    reset_field(loc);
}
