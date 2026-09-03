/* The platform layer on anything with pthreads: Linux, and the BSDs and
 * macOS with it.
 *
 * Both objects here are a mutex, a condition variable and a count, because
 * that is the one shape every pthreads implementation gets right. The
 * timed waits work out an absolute deadline once and then loop, since a
 * condition variable may wake for no reason at all and the caller was
 * promised a timeout, not a nap.
 */

#define _POSIX_C_SOURCE 200809L

#include <pthread.h>
#include <stdlib.h>
#include <time.h>
#include <errno.h>
#include <limits.h>
#include "evv_port.h"
#include "evv_arena.h"

/* Some libraries only define this when a feature test macro asks for it,
   and a stack smaller than a page is not worth setting anyway. */
#ifndef PTHREAD_STACK_MIN
#define PTHREAD_STACK_MIN 16384
#endif

void evv_port_start(void) { }
void evv_port_finish(void) { }

/* ---- telling the time ------------------------------------------------ */

/* Monotonic where it can be, because the engine measures gaps and a clock
   that goes backwards would make a gap negative. */
static void evv_now(struct timespec *ts)
{
#ifdef CLOCK_MONOTONIC
    clock_gettime(CLOCK_MONOTONIC, ts);
#else
    clock_gettime(CLOCK_REALTIME, ts);
#endif
}

unsigned evv_ticks_ms(void)
{
    struct timespec ts;

    evv_now(&ts);
    return (unsigned)((unsigned long long)ts.tv_sec * 1000u
                      + (unsigned)(ts.tv_nsec / 1000000));
}

static void evv_deadline(struct timespec *ts, int ms)
{
    evv_now(ts);
    ts->tv_sec += ms / 1000;
    ts->tv_nsec += (long)(ms % 1000) * 1000000L;
    if (ts->tv_nsec >= 1000000000L) {
        ts->tv_nsec -= 1000000000L;
        ts->tv_sec += 1;
    }
}

/* ---- what both objects are made of ----------------------------------- */

struct evv_gate {
    pthread_mutex_t lock;
    pthread_cond_t  wake;
    int             count;   /* a semaphore's count, or an event's state */
};

static int evv_gate_init(struct evv_gate *g, int count)
{
    pthread_condattr_t attr;

    if (pthread_mutex_init(&g->lock, NULL) != 0)
        return -1;

    pthread_condattr_init(&attr);
#if defined(CLOCK_MONOTONIC) && defined(_POSIX_CLOCK_SELECTION)
    pthread_condattr_setclock(&attr, CLOCK_MONOTONIC);
#endif
    if (pthread_cond_init(&g->wake, &attr) != 0) {
        pthread_condattr_destroy(&attr);
        pthread_mutex_destroy(&g->lock);
        return -1;
    }
    pthread_condattr_destroy(&attr);

    g->count = count;
    return 0;
}

static void evv_gate_fini(struct evv_gate *g)
{
    pthread_cond_destroy(&g->wake);
    pthread_mutex_destroy(&g->lock);
}

/* Wait until the count is worth having. An event leaves it where it is; a
   semaphore takes one away. */
static int evv_gate_wait(struct evv_gate *g, int ms, int take)
{
    struct timespec until;
    int rc = EVV_WAIT_OK;

    if (ms >= 0)
        evv_deadline(&until, ms);

    pthread_mutex_lock(&g->lock);
    while (g->count <= 0) {
        if (ms < 0) {
            pthread_cond_wait(&g->wake, &g->lock);
            continue;
        }
        if (pthread_cond_timedwait(&g->wake, &g->lock, &until) == ETIMEDOUT) {
            rc = (g->count > 0) ? EVV_WAIT_OK : EVV_WAIT_TIMEOUT;
            break;
        }
    }
    if (rc == EVV_WAIT_OK && take)
        g->count--;
    pthread_mutex_unlock(&g->lock);
    return rc;
}

/* ---- semaphores ------------------------------------------------------ */

struct evv_sem { struct evv_gate g; int most; };

evv_sem *evv_sem_create(int initial, int most)
{
    evv_sem *s = malloc(sizeof *s);

    if (s == NULL)
        return NULL;
    if (evv_gate_init(&s->g, initial) != 0) {
        free(s);
        return NULL;
    }
    s->most = most > 0 ? most : 0;
    return s;
}

void evv_sem_destroy(evv_sem *s)
{
    if (s == NULL)
        return;
    evv_gate_fini(&s->g);
    free(s);
}

int evv_sem_wait(evv_sem *s, int ms)
{
    if (s == NULL)
        return EVV_WAIT_FAILED;
    return evv_gate_wait(&s->g, ms, 1);
}

int evv_sem_post(evv_sem *s, int n)
{
    if (s == NULL)
        return EVV_WAIT_FAILED;

    pthread_mutex_lock(&s->g.lock);
    if (s->most > 0 && s->g.count + n > s->most) {
        pthread_mutex_unlock(&s->g.lock);
        return EVV_WAIT_FAILED;
    }
    s->g.count += n;
    if (n == 1)
        pthread_cond_signal(&s->g.wake);
    else
        pthread_cond_broadcast(&s->g.wake);
    pthread_mutex_unlock(&s->g.lock);
    return EVV_WAIT_OK;
}

/* ---- events ---------------------------------------------------------- */

/* Signalled stays signalled until it is unsignalled, so waiting does not
   take anything away and every waiter goes through at once. */
struct evv_event { struct evv_gate g; };

evv_event *evv_event_create(int signalled)
{
    evv_event *e = malloc(sizeof *e);

    if (e == NULL)
        return NULL;
    if (evv_gate_init(&e->g, signalled ? 1 : 0) != 0) {
        free(e);
        return NULL;
    }
    return e;
}

void evv_event_destroy(evv_event *e)
{
    if (e == NULL)
        return;
    evv_gate_fini(&e->g);
    free(e);
}

int evv_event_wait(evv_event *e, int ms)
{
    if (e == NULL)
        return EVV_WAIT_FAILED;
    return evv_gate_wait(&e->g, ms, 0);
}

static void evv_event_set(evv_event *e, int state)
{
    if (e == NULL)
        return;
    pthread_mutex_lock(&e->g.lock);
    e->g.count = state;
    if (state)
        pthread_cond_broadcast(&e->g.wake);
    pthread_mutex_unlock(&e->g.lock);
}

void evv_event_signal(evv_event *e)   { evv_event_set(e, 1); }
void evv_event_unsignal(evv_event *e) { evv_event_set(e, 0); }

/* Letting everyone waiting through without leaving it open behind them. */
void evv_event_pulse(evv_event *e)
{
    if (e == NULL)
        return;
    pthread_mutex_lock(&e->g.lock);
    e->g.count = 1;
    pthread_cond_broadcast(&e->g.wake);
    e->g.count = 0;
    pthread_mutex_unlock(&e->g.lock);
}

/* ---- threads --------------------------------------------------------- */

struct evv_task { pthread_t id; };

struct evv_start {
    void (*entry)(void *);
    void  *arg;
};

static void *evv_trampoline(void *p)
{
    struct evv_start *s = p;
    void (*entry)(void *) = s->entry;
    void *arg = s->arg;

    free(s);
    entry(arg);
    /* The frame stack this thread took for the rules it ran. Nothing else will
       give it back, and an instance is a thread.

       Not in the reference shim: that is this porting layer under IBM's own
       engine, which keeps its frames its own way and never asks ours for one,
       and it links none of evv_arena.c to answer with. */
#ifndef EVV_IBM_NAMES
    evv_frame_done();
#endif
    return NULL;
}

evv_task *evv_task_start(void (*entry)(void *), void *arg, int stack_bytes)
{
    pthread_attr_t attr;
    struct evv_start *s;
    evv_task *t;
    int rc;

    if (entry == NULL)
        return NULL;

    t = malloc(sizeof *t);
    if (t == NULL)
        return NULL;

    s = malloc(sizeof *s);
    if (s == NULL) {
        free(t);
        return NULL;
    }
    s->entry = entry;
    s->arg = arg;

    pthread_attr_init(&attr);
    if (stack_bytes < PTHREAD_STACK_MIN)
        stack_bytes = PTHREAD_STACK_MIN;
    /* The system puts the stack where it likes, which is usually far above
       what a 32-bit value can name. That is allowed to be true: nothing the
       machine holds a pointer to lives on a C stack any more -- a rule's frame
       comes from evv_frame_push and the one field address the runtime parks
       comes from there too. It is deliberately not placed low, because Windows
       will not let a stack be placed at all, and a platform difference here is
       a bug this suite could not see. */
    pthread_attr_setstacksize(&attr, (size_t)stack_bytes);
    /* Nothing joins these, so let the system reclaim them. */
    pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);

    rc = pthread_create(&t->id, &attr, evv_trampoline, s);
    pthread_attr_destroy(&attr);

    if (rc != 0) {
        free(s);
        free(t);
        return NULL;
    }
    return t;
}

void evv_task_stop(evv_task *t) { (void)t; }

int evv_task_priority_get(evv_task *t, int *out)
{
    (void)t;
    if (out != NULL)
        *out = 0;
    return 0;
}

int evv_task_priority_set(evv_task *t, int priority)
{
    (void)t;
    (void)priority;
    return 0;
}

unsigned evv_task_self(void)
{
    pthread_t me = pthread_self();
    unsigned key = 0;
    unsigned i;

    /* A thread identifier is not required to be a number, so make one out
       of whatever it is. */
    for (i = 0; i < sizeof me; i++)
        key = key * 31u + ((const unsigned char *)&me)[i];
    return key ? key : 1u;
}

void evv_sleep_ms(int ms)
{
    struct timespec ts;

    if (ms < 0)
        ms = 0;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}
