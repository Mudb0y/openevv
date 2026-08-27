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

/* ---- dttime --------------------------------------------------------- */

/* val_expr is not here. Its one caller is val_expr1 in misc, and val_expr1
   is named by no call and by no relocation anywhere in the language, so the
   chain is entered from nothing. Writing it would have pulled in val_expr2,
   durcalc, firstdefd, gcql and gcqr, about eight hundred instructions, for
   a path no sentence can reach. */

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
