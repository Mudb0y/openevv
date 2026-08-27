/* An entry with indices where the forward pointers were.
 *
 * A pointer belongs to one process and an index does not, so this is the shape
 * an entry takes in a file. The save walk builds an array of these over the
 * live list, turning each pointer into how many steps along the bottom row it
 * points, and the load walk turns them back.
 */

#include <stdio.h>
#include "eci_key.h"

extern void *cpp_new(uint32_t n);
extern void  cpp_delete(void *p);

ArrayListNode *aln_ctorEmpty(ArrayListNode *n)
{
    ln_ctorEmpty(&n->base);
    n->count = 0;
    n->forward = 0;
    return n;
}

void aln_dtor(ArrayListNode *n)
{
    if (n->forward != 0) {
        cpp_delete(n->forward);
        n->forward = 0;
    }
    tr_dtor(&n->base.trans);
    key_dtor(&n->base.key);
}

void aln_save(ArrayListNode *n, FILE *f)
{
    ln_save(&n->base, f);
    fwrite(&n->count, 4, 1, f);
    fwrite(n->forward, 4, (size_t)n->count, f);
}

int32_t aln_load(ArrayListNode *n, FILE *f)
{
    if (!ln_load(&n->base, f))
        return 0;

    fread(&n->count, 4, 1, f);
    n->forward = (int32_t *)cpp_new((uint32_t)(n->count * 4));
    if (n->forward == 0)
        return 0;
    fread(n->forward, 4, (size_t)n->count, f);
    return 1;
}

void aln_dump(ArrayListNode *n)
{
    ln_dump(&n->base);
}
