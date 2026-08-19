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
#define OWNER_BYTES     0x214
#define OWNER_NAMES(o)  (*(const char ***)(o))
#define OWNER_W(o, n)   (*(int32_t *)((char *)(o) + (n)))
#define OWNER_B(o, n)   (*(int8_t *)((char *)(o) + (n)))

/* How many names the table holds, and how much room they take. */
#define NAMES_BYTES 0x18

/* Which of the physical files the logical table starts from. */
#define LOGIO_KIND(d, n) (*(uint8_t *)(EVV_AT(char *, (d)->logio) + (n)))
#define LOGIO_ROOM(d)    (EVV_AT(char *, (d)->logio) + 0x80)

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

    d->owner = EVV_REF((uint8_t *)malloc(OWNER_BYTES));
    if (!EVV_AT(uint8_t *, d->owner))
        return -2;
    memset(EVV_AT(uint8_t *, d->owner), 0, OWNER_BYTES);

    OWNER_NAMES(EVV_AT(uint8_t *, d->owner)) = (const char **)malloc(NAMES_BYTES);
    if (!OWNER_NAMES(EVV_AT(uint8_t *, d->owner)))
        return -2;

    OWNER_NAMES(EVV_AT(uint8_t *, d->owner))[0] = "STATEMENT";
    OWNER_NAMES(EVV_AT(uint8_t *, d->owner))[1] = "TEST";
    OWNER_NAMES(EVV_AT(uint8_t *, d->owner))[2] = "NULL";
    OWNER_NAMES(EVV_AT(uint8_t *, d->owner))[3] = "LOOP";
    OWNER_NAMES(EVV_AT(uint8_t *, d->owner))[4] = "COMMAND";
    OWNER_NAMES(EVV_AT(uint8_t *, d->owner))[5] = "";

    OWNER_W(EVV_AT(uint8_t *, d->owner), 0x04) = 3;
    OWNER_W(EVV_AT(uint8_t *, d->owner), 0x10) = 2;
    OWNER_B(EVV_AT(uint8_t *, d->owner), 0x1b0) = 5;
    OWNER_W(EVV_AT(uint8_t *, d->owner), 0x1dc) = 1;
    *(const char **)((char *)EVV_AT(uint8_t *, d->owner) + 0x1ec) = "";

    return rc;
}

/* Wiped before it is freed, so nothing of it is left to be found. */
void init_delete(delta_state *d)
{
    if (!d)
        return;

    if (OWNER_NAMES(EVV_AT(uint8_t *, d->owner))) {
        free((void *)OWNER_NAMES(EVV_AT(uint8_t *, d->owner)));
        OWNER_NAMES(EVV_AT(uint8_t *, d->owner)) = 0;
    }

    memset(EVV_AT(uint8_t *, d->owner), 0, OWNER_BYTES);
    free(EVV_AT(uint8_t *, d->owner));
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
    uint8_t *owner = EVV_AT(uint8_t *, d->owner);
    int32_t  i;

    (void)argc;
    (void)argv;

    if (!dtSetErrorCallback(d, (void *)embedErrorCallback))
        return 0;

    OWNER_W(owner, 0x1d0) = 0;
    OWNER_W(owner, 0x1cc) = 0x36b0;

    EVV_AT(delta_vars *, d->vars)->relink = 0;
    EVV_AT(delta_vars *, d->vars)->ctx_both = 1;
    /* The fenced fields start after the six a sync node keeps for itself. */
    EVV_AT(delta_vars *, d->vars)->fence_base = d->nstmts + 6;

    if (!logicalIOInit(d, d->nlfnames + OWNER_W(owner, 0x1b4),
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
    if (!logicalFileAddPhysical(d, LOGIO_KIND(d, 0), "null", LOGIO_ROOM(d), (void *)0, 0)
     || !logicalFileAddPhysical(d, LOGIO_KIND(d, 5), "null", LOGIO_ROOM(d), (void *)0, 1)
     || !logicalFileAddPhysical(d, LOGIO_KIND(d, 1), "null", LOGIO_ROOM(d), (void *)0, 0)
     || !logicalFileAddPhysical(d, LOGIO_KIND(d, 4), "null", LOGIO_ROOM(d), (void *)0, 1)
     || !logicalFileAddPhysical(d, LOGIO_KIND(d, 2), "null", LOGIO_ROOM(d), (void *)0, 1))
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
