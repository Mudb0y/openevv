/* The platform layer on Windows, which is what the differential harness
 * runs under.
 *
 * This is the shortest of the three implementations because Win32 has an
 * object for each thing the contract asks for. It exists to keep the
 * harness honest: every claim about this port being correct rests on
 * running it beside IBM's own code, and that only happens here.
 */

#include <windows.h>
#include <stdlib.h>
#include "evv_port.h"
#include "evv_arena.h"

void evv_port_start(void) { }
void evv_port_finish(void) { }

/* ---- semaphores ------------------------------------------------------ */

struct evv_sem { HANDLE h; };

evv_sem *evv_sem_create(int initial, int most)
{
    evv_sem *s = malloc(sizeof *s);

    if (s == NULL)
        return NULL;
    s->h = CreateSemaphoreA(NULL, initial, most > 0 ? most : 0x7fffffff,
                            NULL);
    if (s->h == NULL) {
        free(s);
        return NULL;
    }
    return s;
}

void evv_sem_destroy(evv_sem *s)
{
    if (s == NULL)
        return;
    CloseHandle(s->h);
    free(s);
}

static int evv_from_win(DWORD rc)
{
    if (rc == WAIT_OBJECT_0)
        return EVV_WAIT_OK;
    if (rc == WAIT_TIMEOUT)
        return EVV_WAIT_TIMEOUT;
    return EVV_WAIT_FAILED;
}

int evv_sem_wait(evv_sem *s, int ms)
{
    if (s == NULL)
        return EVV_WAIT_FAILED;
    return evv_from_win(WaitForSingleObject(s->h,
                                            ms < 0 ? INFINITE : (DWORD)ms));
}

int evv_sem_post(evv_sem *s, int n)
{
    if (s == NULL)
        return EVV_WAIT_FAILED;
    return ReleaseSemaphore(s->h, n, NULL) ? EVV_WAIT_OK : EVV_WAIT_FAILED;
}

/* ---- events ---------------------------------------------------------- */

struct evv_event { HANDLE h; };

evv_event *evv_event_create(int signalled)
{
    evv_event *e = malloc(sizeof *e);

    if (e == NULL)
        return NULL;
    /* Manual reset: the engine unsignals for itself. */
    e->h = CreateEventA(NULL, TRUE, signalled ? TRUE : FALSE, NULL);
    if (e->h == NULL) {
        free(e);
        return NULL;
    }
    return e;
}

void evv_event_destroy(evv_event *e)
{
    if (e == NULL)
        return;
    CloseHandle(e->h);
    free(e);
}

int evv_event_wait(evv_event *e, int ms)
{
    if (e == NULL)
        return EVV_WAIT_FAILED;
    return evv_from_win(WaitForSingleObject(e->h,
                                            ms < 0 ? INFINITE : (DWORD)ms));
}

void evv_event_signal(evv_event *e)   { if (e) SetEvent(e->h); }
void evv_event_unsignal(evv_event *e) { if (e) ResetEvent(e->h); }
void evv_event_pulse(evv_event *e)    { if (e) PulseEvent(e->h); }

/* ---- threads --------------------------------------------------------- */

struct evv_task { HANDLE h; };

struct evv_start {
    void (*entry)(void *);
    void  *arg;
};

static DWORD WINAPI evv_trampoline(LPVOID p)
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
    return 0;
}

evv_task *evv_task_start(void (*entry)(void *), void *arg, int stack_bytes)
{
    struct evv_start *s;
    evv_task *t;

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

    t->h = CreateThread(NULL, (SIZE_T)(stack_bytes > 0 ? stack_bytes : 0),
                        evv_trampoline, s, 0, NULL);
    if (t->h == NULL) {
        free(s);
        free(t);
        return NULL;
    }
    return t;
}

/* Nothing is ever stopped from outside: the engine signals a thread and
   waits for it to end of its own accord. */
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

unsigned evv_task_self(void) { return (unsigned)GetCurrentThreadId(); }

void evv_sleep_ms(int ms) { Sleep((DWORD)(ms < 0 ? 0 : ms)); }

unsigned evv_ticks_ms(void) { return (unsigned)GetTickCount(); }
