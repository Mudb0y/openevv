/* The engine's one error slot.
 *
 * Every one of these takes the Delta machine and goes straight to the block
 * the machine keeps for ECI, where three words sit together: the two ends of
 * a range, and the error itself. Nought in the error word means nothing has
 * gone wrong, which is why resetting is the same as writing nought and
 * asking whether anything is wrong is the same as comparing against it.
 *
 * The block is ours, built in eci_toeci.c; only these three words are named
 * here, by offset, because that is all this file touches.
 *
 * Names carry no prefix: the object uses plain C names and so do we, which
 * is what stands the original's aside.
 */

#include <stdint.h>
#include "delta.h"
#include "eci_eloqc.h"

/* Where the machine keeps the block, and the three words in it. */

extern void catchDeltaError(delta_state *d);

void setEngsynError(delta_state *d, int32_t err)
{
    ERROR_CODE(d) = err;
}

int32_t getEngsynError(delta_state *d)
{
    return ERROR_CODE(d);
}

/* Anything but nought counts, and the answer is one rather than the error. */
int32_t checkEngsynError(delta_state *d)
{
    return ERROR_CODE(d) != 0;
}

void catchEngsynError(delta_state *d)
{
    ERROR_CODE(d) = 0;
}

/* Clearing both layers at once: this one and the machine's own. */
void resetEngsynError(delta_state *d)
{
    catchEngsynError(d);
    catchDeltaError(d);
}

void setEngsynErrorRange(delta_state *d, int32_t from, int32_t to)
{
    ERROR_FROM(d) = from;
    ERROR_TO(d) = to;
}

void getEngsynErrorRange(delta_state *d, int32_t *from, int32_t *to)
{
    *from = ERROR_FROM(d);
    *to = ERROR_TO(d);
}
