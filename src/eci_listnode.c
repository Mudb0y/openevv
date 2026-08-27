/* One entry of the stored dictionary: a key and what it means.
 *
 * Both are taken in shallow -- the pointers move across and the node owns them
 * afterwards -- which is the original's arrangement. A caller that has built a
 * Key and a Translation on its stack hands them over and must not free what it
 * handed, and the node's own destructor is what lets them go.
 */

#include <stdio.h>
#include <string.h>
#include "eci_key.h"

ListNode *ln_ctorEmpty(ListNode *n)
{
    n->key.bytes = 0;
    n->key.len = 0;
    tr_ctorEmpty(&n->trans);
    return n;
}

ListNode *ln_ctor(ListNode *n, Key *k, Translation *t)
{
    n->key.bytes = k->bytes;
    n->key.len = k->len;
    memcpy(&n->trans, t, sizeof n->trans);
    return n;
}

void ln_save(ListNode *n, FILE *f)
{
    key_save(&n->key, f);
    tr_save(&n->trans, f);
}

int32_t ln_load(ListNode *n, FILE *f)
{
    if (!key_load(&n->key, f))
        return 0;
    if (!tr_load(&n->trans, f))
        return 0;
    return 1;
}

void ln_dump(ListNode *n)
{
    key_dump(&n->key);
    tr_dump(&n->trans);
}
