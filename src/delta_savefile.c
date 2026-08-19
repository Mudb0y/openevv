/* The save layer's file seam, over the C library.

   This is the one file the save and restore routines in delta_trace.c reach
   the outside world through, and the only place stdio is named. A target
   without stdio replaces this file; nothing else has to change. */

#include <stdio.h>

#include "delta.h"

int32_t delta_save_read(delta_state *d, void *buf, int32_t n)
{
    FILE *f = (FILE *)EVV_AT(void *, EVV_AT(delta_stack *, d->stack)->save_file);

    if (f == 0)
        return 0;
    return (int32_t)fread(buf, 1, (size_t)n, f);
}

int32_t delta_save_write(delta_state *d, const void *buf, int32_t n)
{
    FILE *f = (FILE *)EVV_AT(void *, EVV_AT(delta_stack *, d->stack)->save_file);

    if (f == 0)
        return 0;
    return (int32_t)fwrite(buf, 1, (size_t)n, f);
}
