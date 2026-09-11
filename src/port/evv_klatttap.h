/* See klatttap.c. Writes every parameter frame the synthesiser is given,
   when EVV_KLATT_TAP names a file. */

#ifndef EVV_KLATTTAP_H
#define EVV_KLATTTAP_H

#include <stdint.h>

void evv_klatt_tap(const int32_t *parms);

/* In frame order. Shared so that anything writing down what the synthesiser
   was told, or what it was worked out from, names a parameter the same way. */
extern const char *const evv_klatt_parm_names[62];

#endif
