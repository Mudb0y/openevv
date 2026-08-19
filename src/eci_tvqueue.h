/* The queue a stream array keeps its values in.
 *
 * It is here rather than in eci_tvqueue.c because a stream array holds one
 * inside itself, and knowing only how many bytes that came to is knowing it
 * only for one word size.
 */

#ifndef ECI_TVQUEUE_H
#define ECI_TVQUEUE_H

#include <stdint.h>

/* One value, and the moment it takes effect. */
typedef struct {
    int16_t time;
    int16_t value;
} TimeValuePair;

typedef struct {
    TimeValuePair *buf;
    uint16_t       room;  /* how many it holds */
    uint16_t       head;
    uint16_t       tail;
    uint16_t       want;  /* the size it was asked for */
} TimeValueQueue;

#endif
