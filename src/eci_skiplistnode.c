/* An entry with a tower of forward pointers over it.
 *
 * The count is one more than the level asked for, so a node made at level
 * nought still has one pointer, which is the one every node has. The array is
 * separate from the node because its length is not known until the level is
 * drawn, and a node whose array could not be had is what tells the list to
 * give up.
 */

#include "eci_key.h"

extern void *cpp_new(uint32_t n);
extern void  cpp_delete(void *p);

SkipListNode *sln_ctor(SkipListNode *n, int32_t level)
{
    int32_t i;

    ln_ctorEmpty(&n->base);
    n->levels = level + 1;
    /* A pointer each, not four bytes each: the original's own arithmetic is
       times four because its pointers are four bytes, and on a sixty-four bit
       host that would allocate half of what the loop below writes. This is the
       trap docs/status.md calls objects said once. */
    n->forward = (SkipListNode **)cpp_new(
        (uint32_t)(n->levels * (int32_t)sizeof(SkipListNode *)));
    if (n->forward != 0)
        for (i = 0; i < n->levels; i++)
            n->forward[i] = 0;
    return n;
}

SkipListNode *sln_ctorEmpty(SkipListNode *n)
{
    ln_ctorEmpty(&n->base);
    return n;
}

void sln_dtor(SkipListNode *n)
{
    if (n->forward != 0) {
        cpp_delete(n->forward);
        n->forward = 0;
    }
    tr_dtor(&n->base.trans);
    key_dtor(&n->base.key);
}
