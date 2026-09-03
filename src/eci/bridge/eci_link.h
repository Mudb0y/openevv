/* The blocks of memory ECI and the Delta machine pass text through. */

#ifndef ECI_LINK_H
#define ECI_LINK_H

#include <stdint.h>

#include "eci_io.h"

/* Two buffers: what ECI has to say, and what the machine says back. */
typedef struct {
    DynaBuf *in;    /* +0x00 */
    DynaBuf *out;   /* +0x04 */
} EciLink;

EciLink *eciLinkNew(void);
void    *eciLinkDelete(EciLink *l);
int32_t  eciLinkDataFromECI(EciLink *l, const char *text);
int32_t  eciLinkDataToECI(EciLink *l, char *out, int32_t max, int32_t *n);

/* Fill in the two physical file classes a link is reached through. */
void eciLinkClasses(PhysicalFile *link, PhysicalFile *dialog);

#endif
