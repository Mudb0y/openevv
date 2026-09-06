/* The heteronym filter: entry point and nothing else.
 *
 * A caller registers it the way it registers the SSML one, through
 * eciRegisterFilter with this as the entry.
 */

#ifndef ECI_HETERO_H
#define ECI_HETERO_H

#include <stdint.h>
#include "evv_abi.h"

extern STDCALL int hetero_getFilterObject(uint32_t idInterface, void **out);
extern int hetero_isUsable(const char *text);

#endif
