/* The one thread that keeps time for everything else, and what it holds.
 *
 * It is here rather than in eci_timer.c because the sound manager keeps one
 * inside itself, and a manager that only knew how many bytes that was would
 * be a manager whose fields moved the moment a pointer grew.
 */

#ifndef ECI_TIMER_H
#define ECI_TIMER_H

#include <stdint.h>
#include "eci_objects.h"

/* One timer: where to post, what to post, how often, and how far through the
   current round it is. */
typedef struct TimerThreadTimer {
    ETImessageQueueThread *queue;
    ETImessage            *message;
    uint32_t               period;
    uint32_t               sofar;
    uint32_t               index;   /* where it sits in the array */
} TimerThreadTimer;

typedef struct TimerThread {
    ETIThread          base;
    unsigned char      guard[0x0c];      /* over the list */
    unsigned char      oneAtATime[0x0c]; /* over set and kill */
    unsigned char      wake[0x0c];       /* ends the sleep */
    uint32_t           elapsed;
    uint32_t           count;
    uint32_t           capacity;
    TimerThreadTimer **timers;
} TimerThread;

#endif
