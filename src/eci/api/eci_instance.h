/* What one instance of the engine is, from the caller's side.
 *
 * Three objects held by value, so their sizes decide where the fields after
 * them sit and what has to be allocated for the whole. Two files used to say
 * what that was, one of them by a byte count.
 */

#ifndef ECI_INSTANCE_H
#define ECI_INSTANCE_H

#include <stdint.h>
#include "eci_objects.h"
#include "eci_synththread.h"

/* Every parameter the caller can set, and the language they are set for. */
typedef struct {
    int32_t param[20];
    int32_t spr;              /* the engine is in phoneme mode */
    LangIdentifier *lang;
    uint8_t mutex[0x0c];      /* opaque: a Mutex is known only by its address */
} ECIstate;

/* What text passes through on the way in. The whole of it is in
   eci_textfilter.c; two words are all its shape amounts to. */
typedef struct {
    void *thread;
    void *state;
} TextFilter;

typedef struct {
    SynthThread *thread;
    ETIappMessageQueue queue;  /* the messages coming back */
    ECIstate    state;
    TextFilter  filter;
    int32_t     error;         /* how the last thing went */
} ECIinstance;

#endif
