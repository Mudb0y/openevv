/* Index marks: what one is, and the queue they wait in.
 *
 * The queue is held by value in two places -- the synthesis thread and the
 * sound device -- so its shape has to be written down once rather than
 * measured in bytes by each of them.
 */

#ifndef ECI_INDEX_H
#define ECI_INDEX_H

#include <stdint.h>

/* One mark and how much sound stands in front of it. */
typedef struct IndexPair {
    int32_t  index;
    uint32_t lead;
} IndexPair;

/* Index marks waiting to be handed out, and how much sound stands in front
   of all of them together. */
typedef struct IndexQueue {
    const void *vt;
    void       *head;
    void       *tail;
    uint32_t    total;      /* every lead in the queue added up */
} IndexQueue;

#endif
