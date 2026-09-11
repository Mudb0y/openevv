/* The provenance census, and the two questions it asks of the allocator.
 *
 * src/delta/delta_prov.c says what this is for. Nothing here is compiled
 * unless EVV_PROVENANCE is set, and the two helpers below are the only things
 * it adds to the ordinary build's own files -- both answer a question the
 * allocator and the store table could always have answered and nothing had
 * yet needed to ask. The arena's own half is declared in evv_arena.h, beside
 * the allocator that answers it. */

#ifndef DELTA_PROV_H
#define DELTA_PROV_H

#include <stdint.h>

/* Which store of the language's data an address is in, by the name of the
   object it was copied out of, or nought if it is in none of them. */
const char *delta_low_store_of(const void *p);

#if defined(EVV_PROVENANCE) && EVV_PROVENANCE

/* One reach of one rule, and what it was addressing this time. `which' is the
   site number the decompiler emitted; the table grows to fit, because how
   many there are is not known until every part file has been written. */
void evv_prov_note(uint32_t which, const void *p);

#endif

#endif
