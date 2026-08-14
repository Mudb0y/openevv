#include <stddef.h>

#include "delta.h"

#define AT(field, offset) \
    typedef char field##_at_##offset[offsetof(delta_state, field) == offset ? 1 : -1]

AT(lpta, 0x0040);
AT(rpta, 0x0050);

typedef char delta_state_is_0x1088[sizeof(delta_state) == DELTA_STATE_BYTES ? 1 : -1];
typedef char delta_pta_is_16[sizeof(delta_pta) == 16 ? 1 : -1];

/* Point the left register at a token. The flag says a load happened and the
   cleared word is whatever the previous load left behind. */
void lpta_loadp(delta_state *d, const delta_token *p)
{
    d->lpta.loaded = 1;
    d->lpta.value = p->value;
    d->lpta.unknown_08 = 0;
}

/* Both registers at once, which is what a rule matching across a span wants
   and why it is the second most common operation in the whole language. */
void lpta_rpta_loadp(delta_state *d, const delta_token *lp,
                     const delta_token *rp)
{
    d->rpta.loaded = 1;
    d->lpta.loaded = 1;
    d->lpta.value = lp->value;
    d->rpta.value = rp->value;
    d->rpta.unknown_08 = 0;
    d->lpta.unknown_08 = 0;
}
