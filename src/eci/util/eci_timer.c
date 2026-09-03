/* Timers, which are a thread rather than anything the machine provides.
 *
 * A timer here is a message and a period: every so many milliseconds the
 * message is posted to a queue. One thread serves all of them. It works out
 * which timer falls due soonest, sleeps on an event for exactly that long,
 * charges the elapsed time against every timer, posts the ones that have
 * come round, and goes back to sleep. Adding or removing a timer signals
 * that event so the sleep is recomputed.
 *
 * The thread only exists while there are timers: setting the first one
 * starts it and killing the last one stops it and waits.
 *
 * There is nothing of any operating system in here. The original lived in a
 * file named for Windows and used Windows timers in a neighbouring one, but
 * this is a list, a mutex and an event.
 *
 * Names are prefixed and the aliases at the foot carry the real ones.
 */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_timer.h"

typedef struct ETIThread ETIThread;
typedef struct ETImessage ETImessage;
typedef struct ETImessageQueueThread ETImessageQueueThread;

/* The thread this derives from, only as far as its size. */

/* A message is counted, and the two slots that do it. */
#define MSG_ADDREF   0x04
#define MSG_RELEASE  0x08
#define MSG_SLOT(m, off) ((*(void ***)(m))[(off) / 4])
typedef void (THIS *MsgRefFn)(ETImessage *m);

/* Waiting with no limit at all. */
#define FOREVER  (-1)

/* What the thread is started at. */
extern int RAL_THREAD_PRIORITY_NORMAL;

extern THIS void *th_ctor(ETIThread *t) MANGLED("??0ETIThread@@IAE@XZ");
extern THIS void  th_dtor(ETIThread *t) MANGLED("??1ETIThread@@UAE@XZ");
extern THIS void  th_terminate(ETIThread *t)
    MANGLED("?terminate@ETIThread@@UAEXXZ");
extern THIS int32_t th_shouldTerminate(const ETIThread *t)
    MANGLED("?shouldTerminate@ETIThread@@QBEHXZ");
extern THIS int32_t th_getStatus(ETIThread *t)
    MANGLED("?getStatus@ETIThread@@QAE?AW4TStatus@1@XZ");
extern THIS int32_t th_start(ETIThread *t, int32_t priority)
    MANGLED("?start@ETIThread@@QAEHH@Z");
extern THIS int32_t th_terminateAndWait(ETIThread *t)
    MANGLED("?terminateAndWait@ETIThread@@QAEHXZ");
extern THIS int32_t th_waitForExit(ETIThread *t)
    MANGLED("?waitForExit@ETIThread@@UAEHXZ");

extern THIS void *sy_mutexCtor(void *m, int32_t recursive);
extern THIS void  sy_mutexDtor(void *m);
extern THIS int   sy_mutexWait(void *m, int32_t ms);
extern THIS int   sy_mutexRelease(void *m);
extern THIS void *sy_eventCtor(void *e, int32_t signalled);
extern THIS void  sy_eventDtor(void *e);
extern THIS int   sy_eventWait(void *e, int32_t ms);
extern THIS int   sy_eventSignal(void *e);
extern THIS int   sy_eventUnsignal(void *e);

/* The four things a thread can be asked to do. Only waiting for it to
   finish is left to the class this derives from. */
typedef struct TimerVtbl {
    THIS void    *(*destroy)(TimerThread *self, int32_t free_it);
    THIS void     (*terminate)(TimerThread *self);
    THIS int32_t  (*waitForExit)(ETIThread *self);
    THIS uint32_t (*run)(TimerThread *self);
} TimerVtbl;

extern const TimerVtbl vtbl_timerthread;

THIS uint32_t tt_run(TimerThread *t);
THIS void     tt_terminate(TimerThread *t);
THIS void     tt_dtor(TimerThread *t);
THIS void    *tt_destroy(TimerThread *t, int32_t free_it);

/* ---- posting ---------------------------------------------------------- */

/* Putting a message on a queue thread already belongs to the sound thread's
   own file, so it is borrowed rather than written twice. */
extern THIS int16_t qt_postMessage(ETImessageQueueThread *q, ETImessage *m)
    MANGLED("?postMessage@ETImessageQueueThread@@QAEFPAVETImessage@@@Z");

THIS void tt_timerPost(TimerThreadTimer *timer)
{
    qt_postMessage(timer->queue, timer->message);
}

/* ---- one timer -------------------------------------------------------- */

THIS TimerThreadTimer *tt_timerCtor(TimerThreadTimer *timer,
                                    ETImessageQueueThread *queue,
                                    ETImessage *message, uint32_t period,
                                    uint32_t sofar, uint32_t index)
{
    timer->queue = queue;
    timer->message = message;
    timer->period = period;
    timer->sofar = sofar;
    timer->index = index;
    ((MsgRefFn)MSG_SLOT(timer->message, MSG_ADDREF))(timer->message);
    return timer;
}

THIS void tt_timerDtor(TimerThreadTimer *timer)
{
    if (timer->message) {
        ((MsgRefFn)MSG_SLOT(timer->message, MSG_RELEASE))(timer->message);
        timer->message = 0;
    }
}

/* How long until this one comes round again. */
THIS uint32_t tt_timerNextDue(TimerThreadTimer *timer)
{
    return timer->period - timer->sofar;
}

/* Charge time against it, say whether it has come round, and leave it with
   only the remainder so a long sleep does not lose a beat. */
THIS int tt_timerIsDue(TimerThreadTimer *timer, uint32_t slept)
{
    int due;

    timer->sofar += slept;
    due = timer->sofar >= timer->period;
    timer->sofar %= timer->period;
    return due;
}

/* ---- the thread ------------------------------------------------------- */

THIS TimerThread *tt_ctor(TimerThread *t)
{
    th_ctor((ETIThread *)t);
    *(const void **)t = &vtbl_timerthread;
    sy_mutexCtor(t->guard, 0);
    sy_mutexCtor(t->oneAtATime, 0);
    sy_eventCtor(t->wake, 0);
    t->elapsed = 0;
    t->count = 0;
    t->capacity = 0;
    t->timers = 0;
    return t;
}

THIS void tt_dtor(TimerThread *t)
{
    *(const void **)t = &vtbl_timerthread;

    /* The array only goes back if nothing is still in it. */
    if (t->count == 0 && t->timers != 0) {
        t->count = 0;
        t->capacity = 0;
        cpp_delete(t->timers);
        t->timers = 0;
    }

    sy_eventDtor(t->wake);
    sy_mutexDtor(t->oneAtATime);
    sy_mutexDtor(t->guard);
    th_dtor((ETIThread *)t);
}

THIS void *tt_destroy(TimerThread *t, int32_t free_it)
{
    tt_dtor(t);
    if (free_it & 1)
        cpp_delete(t);
    return t;
}

/* Stopping means telling the thread and then waking it, or it would sleep
   out the rest of its interval first. */
THIS void tt_terminate(TimerThread *t)
{
    th_terminate((ETIThread *)t);
    sy_eventSignal(t->wake);
}

/* Room for one more, doubling when there is not. */
THIS int tt_ensureArraySize(TimerThread *t)
{
    uint32_t bigger;
    TimerThreadTimer **grown;

    if (t->capacity >= t->count)
        return 1;

    bigger = t->count * 2;
    grown = cpp_new(bigger * (uint32_t)sizeof *t->timers);
    if (!grown)
        return 0;

    if (t->timers) {
        memcpy(grown, t->timers, t->capacity * sizeof *t->timers);
        cpp_delete(t->timers);
    }
    t->timers = grown;
    t->capacity = bigger;
    return 1;
}

/* Ask for a message every so often. The new timer starts part way through
   its first round, so that timers of the same period do not all fall due on
   the same tick. */
THIS TimerThreadTimer *tt_setTimer(TimerThread *t,
                                   ETImessageQueueThread *queue,
                                   ETImessage *message, uint32_t period)
{
    TimerThreadTimer *timer;
    void *room;

    sy_mutexWait(t->oneAtATime, FOREVER);
    sy_mutexWait(t->guard, FOREVER);

    ((MsgRefFn)MSG_SLOT(message, MSG_ADDREF))(message);

    room = cpp_new(sizeof(TimerThreadTimer));
    timer = room ? tt_timerCtor(room, queue, message, period,
                                t->elapsed % period, t->count)
                 : 0;

    if (timer) {
        t->count++;
        if (tt_ensureArraySize(t)) {
            t->timers[t->count - 1] = timer;
            sy_eventSignal(t->wake);
        } else {
            tt_timerDtor(timer);
            cpp_delete(timer);
            timer = 0;
            t->count--;
        }
    }

    ((MsgRefFn)MSG_SLOT(message, MSG_RELEASE))(message);
    sy_mutexRelease(t->guard);

    /* The first timer is what brings the thread to life. */
    if (t->count == 1)
        th_start((ETIThread *)t, RAL_THREAD_PRIORITY_NORMAL);

    sy_mutexRelease(t->oneAtATime);
    return timer;
}

/* Take one away. The last in the array is moved into the gap so the array
   stays packed, and the last timer going takes the thread with it. */
THIS void tt_killTimer(TimerThread *t, TimerThreadTimer *timer)
{
    uint32_t at;

    sy_mutexWait(t->oneAtATime, FOREVER);
    sy_mutexWait(t->guard, FOREVER);

    at = timer->index;
    t->timers[t->count - 1]->index = at;
    t->timers[at] = t->timers[t->count - 1];

    tt_timerDtor(timer);
    cpp_delete(timer);

    t->count--;
    t->timers[t->count] = 0;

    sy_mutexRelease(t->guard);

    if (t->count == 0)
        th_terminateAndWait((ETIThread *)t);

    sy_mutexRelease(t->oneAtATime);
}

/* The body. Sleep exactly as long as the soonest timer has left, charge
   that against all of them, and post whatever came round. */
THIS uint32_t tt_run(TimerThread *t)
{
    uint32_t slept = 0;

    sy_mutexWait(t->guard, FOREVER);

    while (!th_shouldTerminate((ETIThread *)t)) {
        uint32_t soonest = 0xffffffffu;
        uint32_t i;

        for (i = 0; i < t->count; i++) {
            if (tt_timerIsDue(t->timers[i], slept))
                tt_timerPost(t->timers[i]);
            if (soonest >= tt_timerNextDue(t->timers[i]))
                soonest = tt_timerNextDue(t->timers[i]);
        }

        sy_mutexRelease(t->guard);
        sy_eventWait(t->wake, (int32_t)soonest);
        sy_eventUnsignal(t->wake);
        sy_mutexWait(t->guard, FOREVER);

        t->elapsed += soonest;
        slept = soonest;
    }

    sy_mutexRelease(t->guard);
    return 0;
}

const TimerVtbl vtbl_timerthread = {
    tt_destroy, tt_terminate, th_waitForExit, tt_run
};

/* ---- the sleep cycle -------------------------------------------------- */

/* The original kept a list of periodic callbacks driven by a Windows
   multimedia timer. Nothing in the engine ever makes one, and the only
   caller asks to delete a handle that is never anything but minus one, so
   this is the whole of it. */
int32_t deleteSleepCycle(int32_t handle)
{
    (void)handle;
    return -1;
}

ALIAS("??_7TimerThread@@6B@", "vtbl_timerthread");
ALIAS("??0TimerThread@@QAE@XZ", "tt_ctor");
ALIAS("??1TimerThread@@UAE@XZ", "tt_dtor");
ALIAS("??_GTimerThread@@UAEPAXI@Z", "tt_destroy");
ALIAS("?terminate@TimerThread@@UAEXXZ", "tt_terminate");
ALIAS("?run@TimerThread@@EAEKXZ", "tt_run");
ALIAS("?ensureTimerArraySize@TimerThread@@AAEHXZ", "tt_ensureArraySize");
ALIAS("?setTimer@TimerThread@@QAEPAVTimerThreadTimer@@PAVETImessageQueueThread@@PAVETImessage@@K@Z",
      "tt_setTimer");
ALIAS("?killTimer@TimerThread@@QAEXPAVTimerThreadTimer@@@Z", "tt_killTimer");
ALIAS("??0TimerThreadTimer@@QAE@PAVETImessageQueueThread@@PAVETImessage@@KKK@Z",
      "tt_timerCtor");
ALIAS("??1TimerThreadTimer@@QAE@XZ", "tt_timerDtor");
ALIAS("?isDue@TimerThreadTimer@@QAEHK@Z", "tt_timerIsDue");
ALIAS("?nextDue@TimerThreadTimer@@QAEKXZ", "tt_timerNextDue");
ALIAS("?postMessage@TimerThreadTimer@@QAEXXZ", "tt_timerPost");
