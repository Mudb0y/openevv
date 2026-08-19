/* Starting the command layer.
 *
 * The machine's owner is one block of a little over five hundred bytes,
 * holding whatever the program around the engine wants to keep, and at the
 * front of it a table of the five names a statement can be reported under.
 * Building it is init_new; taking it down is init_delete.
 *
 * vcmdinit is the longer job: the error callback, the fenced-field base, the
 * logical file table and the five physical files behind it, the five streams
 * the engine talks through, and then memory, the machine, the dictionaries
 * and the links. Any one of them failing stops the whole thing, and the
 * answer is simply true or false.
 *
 * The two arguments after the machine are the command line. Nothing here
 * reads them; they are taken because the caller passes them.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "delta.h"

/* The owner block, and the two words in it this file sets. */

/* How many names the table holds, and how much room they take. */
#define NAMES 6

/* Which of the physical files the logical table starts from. */

extern void    errorIgnore(void);
extern void    throwDeltaErrorNow(delta_state *d);

/* What the machine is told to do when the runtime under it reports an
   error: throw at once, so the rule that caused it unwinds rather than
   carrying on with a half-finished result. The original keeps this to
   itself, so it is a static here too. */
static void embedErrorCallback(delta_state *d)
{
    throwDeltaErrorNow(d);
}
extern int32_t vdelinit(delta_state *d);
extern int32_t dtSetErrorCallback(delta_state *d, void *fn);
extern int32_t vmeminit(delta_state *d);
extern int32_t vdltinit(delta_state *d, int32_t initStatements);
extern int32_t vdictinit(delta_state *d);
extern void    vlinkinit(delta_state *d);

/* Room for the owner and for the names in front of it. Minus two is what the
   layer above reads as "there was not enough memory". */
int32_t init_new(delta_state *d)
{
    int32_t rc = 0;

    delta_owner *o = malloc(sizeof *o);

    d->owner = EVV_REF(o);
    if (!o)
        return -2;
    memset(o, 0, sizeof *o);

    o->names = malloc(NAMES * sizeof *o->names);
    if (!o->names)
        return -2;

    o->names[0] = "STATEMENT";
    o->names[1] = "TEST";
    o->names[2] = "NULL";
    o->names[3] = "LOOP";
    o->names[4] = "COMMAND";
    o->names[5] = "";

    o->unknown_04 = 3;
    o->unknown_10 = 2;
    o->unknown_1b0 = 5;
    o->unknown_1dc = 1;
    o->unknown_1ec = "";

    return rc;
}

/* Wiped before it is freed, so nothing of it is left to be found. */
void init_delete(delta_state *d)
{
    if (!d)
        return;

    delta_owner *o = EVV_AT(delta_owner *, d->owner);

    if (o->names) {
        free((void *)o->names);
        o->names = 0;
    }

    memset(o, 0, sizeof *o);
    free(o);
    d->owner = EVV_REF(0);
}

/* The command layer's way out is the program's way out. In a library that is
   the host's process, which is worth knowing before anything calls it. */
void vcmdend(delta_state *d, int32_t code)
{
    (void)d;
    exit(code);
}

/* Everything the machine needs before it can be told to do anything. */
int32_t vcmdinit(delta_state *d, int32_t argc, char **argv)
{
    delta_owner *owner = EVV_AT(delta_owner *, d->owner);
    int32_t      i;

    (void)argc;
    (void)argv;

    if (!dtSetErrorCallback(d, (void *)embedErrorCallback))
        return 0;

    owner->unknown_1d0 = 0;
    owner->unknown_1cc = 0x36b0;

    EVV_AT(delta_vars *, d->vars)->relink = 0;
    EVV_AT(delta_vars *, d->vars)->ctx_both = 1;
    /* The fenced fields start after the six a sync node keeps for itself. */
    EVV_AT(delta_vars *, d->vars)->fence_base = d->nstmts + 6;

    if (!logicalIOInit(d, d->nlfnames + owner->unknown_1b4,
                       (void *)errorIgnore))
        return 0;

    /* The language's own streams, after whatever the runtime declared. */
    for (i = builtInLogicalFiles(d); i < d->nlfnames; i++) {
        if (vfdef_lf(d, EVV_AT(const char *const *, d->lfnames)[i]) == -1)
            return 0;
    }

    /* Five physical files, all of them nowhere: the engine is driven through
       memory rather than through a filing system, so every one of them is
       the null device. */
    if (!logicalFileAddPhysical(d, logicalStandardStream(d, 0), "null", logicalNullClass(d), (void *)0, 0)
     || !logicalFileAddPhysical(d, logicalStandardStream(d, 5), "null", logicalNullClass(d), (void *)0, 1)
     || !logicalFileAddPhysical(d, logicalStandardStream(d, 1), "null", logicalNullClass(d), (void *)0, 0)
     || !logicalFileAddPhysical(d, logicalStandardStream(d, 4), "null", logicalNullClass(d), (void *)0, 1)
     || !logicalFileAddPhysical(d, logicalStandardStream(d, 2), "null", logicalNullClass(d), (void *)0, 1))
        return 0;

    if (!logicalFileOpen(d, (void *)"pgmin", 0)
     || !logicalFileOpen(d, (void *)"pgmout", 1)
     || !logicalFileOpen(d, (void *)"cmdin", 0)
     || !logicalFileOpen(d, (void *)"cmdout", 1)
     || !logicalFileOpen(d, (void *)"prompt", 1))
        return 0;

    if (!vmeminit(d))
        return 0;
    if (!vdelinit(d))
        return 0;
    if (!vdltinit(d, 1))
        return 0;
    if (!vdictinit(d))
        return 0;

    vlinkinit(d);
    return 1;
}
