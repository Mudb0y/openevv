#ifndef KLATT_WIDE_H
#define KLATT_WIDE_H

#include <stdint.h>

#include "klatt_state.h"

/* The wideband voice: Eloquence as it has always been below the top of its
   own band, and above that what a second synthesiser makes of the same
   frames at twice the rate. src/klatt/klatt_wide.c says why it is built
   that way and not by synthesising higher outright. */

#define WIDE_RATE     22050
#define WIDE_PARTNER  11025

typedef struct WideBand WideBand;

WideBand      *klatt_wide_new(void);
void           klatt_wide_delete(WideBand *w);

/* The companion is told everything its partner is told, at the moment it is
   told it. */
int            klatt_wide_setup(WideBand *w, KlattConstParms cp);
int            klatt_wide_open(WideBand *w);
void           klatt_wide_close(WideBand *w);
void           klatt_wide_volume(WideBand *w, int32_t volume);

/* One frame through the companion, before the partner speaks it. */
int            klatt_wide_frame(WideBand *w, const int32_t *frame);

/* What the companion made, as it makes it; and n of the partner's samples
   turned into the wideband stream, twice as many. The mix answers null
   when there is no room. */
int            klatt_wide_feed(WideBand *w, const int32_t *s, int32_t n);
const int32_t *klatt_wide_mix(WideBand *w, const int32_t *in, int32_t n,
                              int32_t *made);

#endif
