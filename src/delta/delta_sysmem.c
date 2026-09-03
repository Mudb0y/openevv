#include <stdlib.h>

#include "delta.h"

/* The only place the Delta heap touches the outside world. A target without a
   general allocator points these at an arena of its own. */
void *delta_sys_alloc(size_t n)
{
    return malloc(n);
}

void delta_sys_free(void *p)
{
    free(p);
}
