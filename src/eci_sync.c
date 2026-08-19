/* The three things the engine locks and waits on.
 *
 * Each is a thin cover over the runtime abstraction layer, and each is
 * identified by nothing more than its own address: the layer keeps a table
 * keyed by that, so these objects hold no state of their own except the
 * semaphore's count. That is why a mutex is twelve bytes of nothing.
 *
 * A semaphore is not asked of the layer at all. It is built here out of two
 * mutexes and an event: one mutex guards the count, the other lets only one
 * waiter queue at a time, and the event is what a waiter sleeps on when the
 * count has gone negative. A negative count is the number of waiters.
 *
 * The mutex and the event are thoroughly exercised -- six hundred waits on
 * one and a hundred on the other for a single sentence. The semaphore is
 * never used at all on this path, so it is transcribed and unverified.
 *
 * Names are prefixed and the aliases at the foot carry the real ones.
 */

#include <stdint.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "evv_port.h"

/* What waiting on the layer's recursive mutex answers when it worked. */
#define RAL_GOT  0x2739

/* The level at or above which the engine's log wants to hear about this. */
#define LOG_INTERNAL  8
#define LOG_LEVEL_MIN 4

extern int ralRecMutexSemaphoreCreate(struct ral_req *r);
extern int ralRecMutexSemaphoreDelete(struct ral_req *r);
extern int ralRecMutexSemaphoreTest(struct ral_req *r);
extern int ralRecMutexSemaphoreSignal(struct ral_req *r);
extern int ralEventCreate(struct ral_req *r);
extern int ralEventDelete(struct ral_req *r);
extern int ralEventWait(struct ral_req *r);
extern int ralEventSignal(struct ral_req *r);
extern int ralEventUnSignal(struct ral_req *r);
extern void elgTraceLog(int level, const char *fmt, ...);
extern int g_iTTSLogLevel;

static void sy_complain(const char *what, int rc)
{
    if (g_iTTSLogLevel >= LOG_LEVEL_MIN)
        elgTraceLog(LOG_INTERNAL, what, rc);
}

/* ---- the mutex ------------------------------------------------------- */

typedef struct Mutex { unsigned char opaque[0x0c]; } Mutex;

/* The flag says whether the same thread may take it twice. */
THIS void *sy_mutexCtor(void *m, int32_t recursive)
{
    struct ral_req r;
    int rc;

    r.a = m;
    r.b = (void *)(intptr_t)(recursive ? 1 : 0);
    rc = ralRecMutexSemaphoreCreate(&r);
    if (rc)
        sy_complain("ERROR-Internal Error: Mutex::Mutex %d\n", rc);
    return m;
}

THIS void sy_mutexDtor(void *m)
{
    struct ral_req r;
    int rc;

    r.a = m;
    r.b = m;
    rc = ralRecMutexSemaphoreDelete(&r);
    if (rc)
        sy_complain("ERROR-Internal Error: Mutex::~Mutex %d\n", rc);
}

/* The timeout is taken and ignored, as it is in the original: this waits
   until it has it. */
THIS int32_t sy_mutexWait(void *m, int32_t ms)
{
    struct ral_req r;

    (void)ms;
    r.a = m;
    r.b = m;
    if (ralRecMutexSemaphoreTest(&r) == RAL_GOT)
        return 1;
    sy_complain("ERROR-Internal Error: Mutex::wait failed\n", 0);
    return 0;
}

THIS int32_t sy_mutexRelease(void *m)
{
    struct ral_req r;

    r.a = m;
    r.b = m;
    if (!ralRecMutexSemaphoreSignal(&r))
        return 1;
    sy_complain("ERROR-Internal Error: Mutex::release failed\n", 0);
    return 0;
}

/* ---- the event ------------------------------------------------------- */

typedef struct ETIEvent { unsigned char opaque[0x0c]; } ETIEvent;

/* Signalled stays signalled: whoever waits goes straight through until
   somebody unsignals it. */
THIS ETIEvent *sy_eventCtor(ETIEvent *e, int32_t signalled)
{
    struct ral_req r;
    int rc;

    r.a = (void *)(intptr_t)(signalled ? 1 : 0);
    r.b = e;
    rc = ralEventCreate(&r);
    if (rc)
        sy_complain("ERROR-Internal Error: ETIEvent::ETIEvent %d\n", rc);
    return e;
}

THIS void sy_eventDtor(ETIEvent *e)
{
    struct ral_req r;
    int rc;

    r.a = e;
    r.b = e;
    rc = ralEventDelete(&r);
    if (rc)
        sy_complain("ERROR-Internal Error: ~ETIEvent %d\n", rc);
}

THIS int sy_eventWait(ETIEvent *e, int32_t ms)
{
    struct ral_req r;

    (void)ms;
    r.a = e;
    r.b = e;
    return ralEventWait(&r) == 0;
}

THIS int sy_eventSignal(ETIEvent *e)
{
    struct ral_req r;
    int rc;

    r.a = e;
    r.b = e;
    rc = ralEventSignal(&r);
    if (rc == 0)
        return 1;
    sy_complain("ERROR-Internal Error: ETIEvent::signal %d\n", rc);
    return 0;
}

THIS int sy_eventUnsignal(ETIEvent *e)
{
    struct ral_req r;
    int rc;

    r.a = e;
    r.b = e;
    rc = ralEventUnSignal(&r);
    if (rc == 0)
        return 1;
    sy_complain("ERROR-Internal Error: ETIEvent::unsignal %d\n", rc);
    return 0;
}

/* ---- the semaphore --------------------------------------------------- */

/* Built rather than asked for. The count going below nought is how many
   are waiting. */
typedef struct Semaphore {
    Mutex     guard;        /* +0x00, guards the count */
    ETIEvent  gate;         /* +0x0c, what a waiter sleeps on */
    Mutex     queue;        /* +0x18, only one waiter queues at a time */
    int32_t   count;        /* +0x24 */
} Semaphore;

/* What a caller has to allocate for one. */
const uint32_t sem_bytes = sizeof(Semaphore);

THIS Semaphore *sy_semCtor(Semaphore *s, int32_t count)
{
    sy_mutexCtor(&s->guard, 0);
    sy_eventCtor(&s->gate, 0);
    sy_mutexCtor(&s->queue, 0);
    s->count = count;
    return s;
}

THIS void sy_semDtor(Semaphore *s)
{
    sy_mutexDtor(&s->queue);
    sy_eventDtor(&s->gate);
    sy_mutexDtor(&s->guard);
}

THIS int sy_semWait(Semaphore *s)
{
    sy_mutexWait(&s->queue, -1);
    sy_mutexWait(&s->guard, -1);
    s->count--;
    if (s->count < 0) {
        /* Let go of the count before sleeping, or nobody could add to it. */
        sy_mutexRelease(&s->guard);
        sy_eventWait(&s->gate, -1);
        sy_eventUnsignal(&s->gate);
    } else {
        sy_mutexRelease(&s->guard);
    }
    sy_mutexRelease(&s->queue);
    return 1;
}

THIS int sy_semRelease(Semaphore *s, int32_t n)
{
    sy_mutexWait(&s->guard, -1);
    s->count += n;
    if (s->count <= 0)
        sy_eventSignal(&s->gate);
    sy_mutexRelease(&s->guard);
    return 1;
}

ALIAS("??0Mutex@@QAE@H@Z", "sy_mutexCtor");
ALIAS("??1Mutex@@QAE@XZ", "sy_mutexDtor");
ALIAS("?wait@Mutex@@QAEHJ@Z", "sy_mutexWait");
ALIAS("?release@Mutex@@QAEHXZ", "sy_mutexRelease");

ALIAS("??0ETIEvent@@QAE@H@Z", "sy_eventCtor");
ALIAS("??1ETIEvent@@QAE@XZ", "sy_eventDtor");
ALIAS("?wait@ETIEvent@@QAEHJ@Z", "sy_eventWait");
ALIAS("?signal@ETIEvent@@QAEHXZ", "sy_eventSignal");
ALIAS("?unsignal@ETIEvent@@QAEHXZ", "sy_eventUnsignal");

ALIAS("??0Semaphore@@QAE@J@Z", "sy_semCtor");
ALIAS("??1Semaphore@@QAE@XZ", "sy_semDtor");
ALIAS("?wait@Semaphore@@QAEHXZ", "sy_semWait");
ALIAS("?release@Semaphore@@QAEHJ@Z", "sy_semRelease");
