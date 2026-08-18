/* Building the Delta runtime up and taking it down.
 *
 * The runtime is six layers stacked on the machine: the command layer, the C
 * helpers, the library, the language, the logical files, and the block ECI
 * keeps. Building goes up through them and stops at the first that
 * complains, answering with whatever it said. Taking down goes through all
 * six regardless, because a layer that failed to build still has to be given
 * the chance to give back whatever it did manage to take.
 *
 * The four things the machine keeps a list of -- longs, sixteen-bit globals,
 * the compound ones, and the shorts -- are all reset from the same place.
 * Their counts and their tables sit at the very front of the machine, in a
 * region delta.h has never needed to name, so they are named here by offset.
 */

#include <stdint.h>
#include <string.h>
#include "delta.h"

extern int32_t init_new(delta_state *d);
extern void    init_delete(delta_state *d);
extern void    ccode_new(delta_state *d);
extern void    ccode_delete(delta_state *d);
extern int32_t delta_lib_new(delta_state *d);
extern void    delta_lib_delete(delta_state *d);
extern int32_t dlang_new(delta_state *d);
extern void    dlang_delete(delta_state *d);
extern int32_t logio_new(delta_state *d);
extern void    logio_delete(delta_state *d);

/* Nothing was thrown after all. */
void catchDeltaError(delta_state *d)
{
    d->vars->error_thrown = 0;
}

/* Up through the layers, stopping at the first complaint. The C helpers are
   the one layer with nothing to say, so nothing is asked of them. */
int32_t runtime_new(delta_state *d)
{
    int32_t rc = 0;

    if (!d)
        return rc;

    rc = init_new(d);
    if (rc)
        return rc;

    ccode_new(d);

    rc = delta_lib_new(d);
    if (rc)
        return rc;

    rc = dlang_new(d);
    if (rc)
        return rc;

    rc = logio_new(d);
    if (rc)
        return rc;

    return eloqc_new(d);
}

/* Down through all six whatever happened, and then the machine forgets where
   any of them were. */
void runtime_delete(delta_state *d)
{
    if (!d)
        return;

    init_delete(d);
    ccode_delete(d);
    delta_lib_delete(d);
    dlang_delete(d);
    logio_delete(d);
    eloqc_delete(d);

    d->owner = 0;
    d->vars = 0;
    d->stack = 0;
    *(int32_t *)((char *)d + 0x70) = 0;
    d->logio = 0;
    d->eloqc = 0;
}

/* Every global back to what it started as. The plain ones go to nought; a
   compound one gets its own opening value in its first two bytes, all ones
   in the two after that, and nought through however many bytes follow. */
void initGlobalVars(delta_state *d)
{
    int32_t i;

    /* Each index holds its list twice, so every variable is done twice.
       That is the original's doing and it costs nothing. */
    for (i = 0; i < d->nword; i++)
        *d->word[i] = 0;

    for (i = 0; i < d->ncompound; i++) {
        unsigned char *at = d->compound[i].at;

        *(int16_t *)at = (int16_t)d->compound[i].init;
        *(int16_t *)(at + 2) |= (int16_t)-1;
        memset(at + 4, 0, (size_t)d->compound[i].bytes);
    }

    for (i = 0; i < d->nlong; i++)
        *d->lng[i] = 0;

    for (i = 0; i < d->nshort; i++)
        *d->shrt[i] = 0;
}
