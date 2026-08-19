/* A thread, as everything above the Delta machine understands one.

   Only two are ever started: one that turns text into samples and one that
   pushes samples at the audio device. Both are this class with a run of
   their own, and both are driven by the message queue underneath them.

   Four semaphores, and it is worth naming them because the handshake is
   the whole of it. Starting takes the gate, so a second start cannot
   overtake the first. The thread body runs, and when it comes back it
   signals `done`, waits on `may_finish` until whoever asked for the exit
   says so, marks itself finished, and gives back both the gate and
   `finished`. Waiting for the exit is the other half: wait on `done`,
   release `may_finish`, then wait on `finished`.

   Everything platform-shaped is in the ral layer underneath, so this file
   is the same on any target that can start a task and hold a semaphore. */

#include <stdint.h>
#include <stddef.h>
#include "evv_abi.h"
#include "eci_objects.h"

struct ETIThreadVtbl {
    THIS void    *(*destroy)(ETIThread *self, int32_t free_it);
    THIS void     (*terminate)(ETIThread *self);
    THIS int32_t  (*waitForExit)(ETIThread *self);
    THIS uint32_t (*run)(ETIThread *self);
};
typedef struct ETIThreadVtbl ThreadVtbl;

/* What getStatus answers. */
#define TH_NEW      0
#define TH_RUNNING  1
#define TH_FINISHED 2
#define TH_FAILED   3

/* What the runtime answers for a semaphore taken successfully. Everything
   else is a failure, which is the opposite way round from the rest of it. */
#define RAL_TAKEN 0x2739

/* The blocks the runtime abstraction is handed. Their shape is the
   runtime's, not ours. */
struct ral_sem {
    uint8_t pad_00[0x0c];
    void   *handle;
    int32_t timeout;
};

struct ral_task {
    uint8_t pad_00[0x0c];
    void   *handle;
    void *(*entry)(void *);
    void   *arg;
    int32_t priority;
    int32_t stack;
    char    name[8];
};

struct ral_delay {
    uint8_t pad_00[0x0c];
    int32_t ms;
    int32_t kind;
};

struct ral_prio {
    uint8_t pad_00[0x0c];
    void   *task;
    int32_t priority;
};

extern int32_t ralBinarySemaphoreCreate(struct ral_sem *s);
extern int32_t ralBinarySemaphoreDelete(struct ral_sem *s);
extern int32_t ralBinarySemaphoreSignal(struct ral_sem *s);
extern int32_t ralBinarySemaphoreTest(struct ral_sem *s);
extern int32_t ralTaskCreate(struct ral_task *t);
extern int32_t ralTaskTerminate(struct ral_prio *t);
extern int32_t ralTaskDelay(struct ral_delay *d);
extern int32_t ralTaskPriorityGet(struct ral_prio *p);
extern int32_t ralTaskPrioritySet(struct ral_prio *p);
extern int32_t ralStrCpy(int32_t a, char *dst, const char *src);

extern void cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");
extern int32_t g_iTTSLogLevel;
extern void elgTraceLog(int32_t level, const char *fmt, ...);

extern const ThreadVtbl vtbl_thread;

/* The original names the thread it starts, and asks for forty thousand
   bytes of stack. Both are its numbers, not ours. */
#define THREAD_STACK 0x9c40
#define THREAD_NAME  "ECIThrd"

/* ---- the small ones -------------------------------------------------- */

THIS int32_t th_shouldTerminate(const ETIThread *t)
{
    return t->asked_to_stop;
}

THIS void th_terminate(ETIThread *t)
{
    t->asked_to_stop = 1;
}

/* A thread that has not finished is running; the original asks the system
   and the system always agrees. */
static THIS int32_t th_checkStatus(const ETIThread *t)
{
    (void)t;
    return TH_RUNNING;
}

THIS int32_t th_getStatus(ETIThread *t)
{
    if (t->status == TH_RUNNING)
        t->status = th_checkStatus(t);
    return t->status;
}

THIS uint32_t th_run(ETIThread *t)
{
    (void)t;
    return 0;
}

int32_t th_sleep(int32_t ms, int32_t kind)
{
    struct ral_delay d;

    d.ms = ms;
    d.kind = kind;
    return ralTaskDelay(&d) ? 1 : 0;
}

THIS int32_t th_getPriority(const ETIThread *t)
{
    struct ral_prio p;

    p.task = (void *)(size_t)t->task;
    p.priority = 0;
    ralTaskPriorityGet(&p);
    return p.priority;
}

THIS int32_t th_setPriority(ETIThread *t, int32_t priority)
{
    struct ral_prio p;

    p.task = (void *)(size_t)t->task;
    p.priority = priority;
    return ralTaskPrioritySet(&p) ? 0 : 1;
}

THIS int32_t th_terminateAndWait(ETIThread *t)
{
    t->vt->terminate(t);
    return t->vt->waitForExit(t);
}

/* ---- making and unmaking one ------------------------------------------ */

static void *makeSemaphore(int32_t held)
{
    struct ral_sem s;

    s.handle = 0;
    s.timeout = held;
    ralBinarySemaphoreCreate(&s);
    return s.handle;
}

/* Two of the four start held: the gate, so the first start can take it,
   and the one the body waits on before it is allowed to finish. */
THIS ETIThread *th_ctor(ETIThread *t)
{
    t->vt = &vtbl_thread;
    t->task = 0;
    t->status = TH_NEW;
    t->asked_to_stop = 0;
    t->done = makeSemaphore(0);
    t->may_finish = makeSemaphore(0);
    t->gate = makeSemaphore(1);
    t->finished = makeSemaphore(0);
    return t;
}

static void dropSemaphore(void **slot, const char *what)
{
    struct ral_sem s;

    if (*slot == 0)
        return;
    s.handle = *slot;
    s.timeout = 0;
    ralBinarySemaphoreSignal(&s);

    s.handle = *slot;
    s.timeout = -1;
    if (ralBinarySemaphoreTest(&s) != RAL_TAKEN) {
        if (g_iTTSLogLevel >= 4)
            elgTraceLog(8, "ERROR-Internal Error: ~ETIThread %s\n", what);
        return;
    }
    s.handle = *slot;
    s.timeout = 0;
    if (ralBinarySemaphoreDelete(&s) && g_iTTSLogLevel >= 4)
        elgTraceLog(8, "ERROR-Internal Error: ~ETIThread %s\n", what);
    *slot = 0;
}

THIS void th_dtor(ETIThread *t)
{
    t->vt = &vtbl_thread;
    dropSemaphore(&t->done, "done");
    dropSemaphore(&t->may_finish, "may_finish");
    dropSemaphore(&t->gate, "gate");
    dropSemaphore(&t->finished, "finished");
}

THIS void *th_destroy(ETIThread *t, int32_t free_it)
{
    th_dtor(t);
    if (free_it & 1)
        cpp_delete(t);
    return t;
}

/* ---- running ---------------------------------------------------------- */

/* What the runtime actually starts. The body runs to completion, and then
   this waits to be told it may go, so that whoever wants the answer has a
   chance to take it before the thread disappears. */
static void *th_execute(void *arg)
{
    ETIThread *t = arg;
    struct ral_sem s;

    t->vt->run(t);

    s.handle = t->done;
    s.timeout = 0;
    if (ralBinarySemaphoreSignal(&s) && g_iTTSLogLevel >= 4)
        elgTraceLog(8, "ERROR-Internal Error: ETIThread: signal done\n");

    s.handle = t->may_finish;
    s.timeout = -1;
    if (ralBinarySemaphoreTest(&s) != RAL_TAKEN && g_iTTSLogLevel >= 4)
        elgTraceLog(8, "ERROR-Internal Error: ETIThread: wait may_finish\n");

    t->status = TH_FINISHED;

    s.handle = t->gate;
    s.timeout = 0;
    if (ralBinarySemaphoreSignal(&s) && g_iTTSLogLevel >= 4)
        elgTraceLog(8, "ERROR-Internal Error: ETIThread: release gate\n");

    s.handle = t->finished;
    s.timeout = 0;
    if (ralBinarySemaphoreSignal(&s) && g_iTTSLogLevel >= 4)
        elgTraceLog(8, "ERROR-Internal Error: ETIThread: signal finished\n");
    return 0;
}

THIS int32_t th_start(ETIThread *t, int32_t priority)
{
    struct ral_sem s;
    struct ral_task task;

    t->asked_to_stop = 0;

    s.handle = t->gate;
    s.timeout = -1;
    if (ralBinarySemaphoreTest(&s) != RAL_TAKEN && g_iTTSLogLevel >= 4)
        elgTraceLog(8, "ERROR-Internal Error: ETIThread: take gate\n");

    t->task = -1;
    task.handle = 0;
    task.entry = th_execute;
    task.arg = t;
    task.priority = priority;
    task.stack = THREAD_STACK;
    ralStrCpy(0, task.name, THREAD_NAME);

    if (ralTaskCreate(&task) && g_iTTSLogLevel >= 4)
        elgTraceLog(8, "ERROR-Internal Error: ETIThread: create\n");
    t->task = (int32_t)(size_t)task.handle;

    if (t->task == -1) {
        t->status = TH_FAILED;
        return 0;
    }
    t->status = TH_RUNNING;
    return 1;
}

/* Called by the body itself when it wants to stop early. */
THIS void th_exit(ETIThread *t, uint32_t code)
{
    struct ral_sem s;
    struct ral_prio p;

    (void)code;
    t->status = TH_FINISHED;
    s.handle = t->done;
    s.timeout = 0;
    ralBinarySemaphoreSignal(&s);
    p.task = (void *)(size_t)t->task;
    p.priority = 0;
    ralTaskTerminate(&p);
}

/* The other half of the handshake in execute. A thread that never started,
   or has already finished, is nothing to wait for. */
THIS int32_t th_waitForExit(ETIThread *t)
{
    struct ral_sem s;
    int32_t took;

    if (t->status != TH_RUNNING)
        return 1;

    s.handle = t->done;
    s.timeout = -1;
    took = ralBinarySemaphoreTest(&s);
    if (took != RAL_TAKEN && g_iTTSLogLevel >= 4)
        elgTraceLog(8, "ERROR-Internal Error: ETIThread: wait done\n");

    s.handle = t->may_finish;
    s.timeout = 0;
    if (ralBinarySemaphoreSignal(&s) && g_iTTSLogLevel >= 4)
        elgTraceLog(8, "ERROR-Internal Error: ETIThread: release may_finish\n");

    if (took != RAL_TAKEN) {
        if (g_iTTSLogLevel >= 4)
            elgTraceLog(8, "ERROR-Internal Error: ETIThread: wait done\n");
        return 0;
    }

    s.handle = t->finished;
    s.timeout = -1;
    if (ralBinarySemaphoreTest(&s) != RAL_TAKEN && g_iTTSLogLevel >= 4)
        elgTraceLog(8, "ERROR-Internal Error: ETIThread: wait finished\n");
    return 1;
}

/* ---- the table -------------------------------------------------------- */

const ThreadVtbl vtbl_thread = {
    th_destroy, th_terminate, th_waitForExit, th_run
};

ALIAS("??_7ETIThread@@6B@", "vtbl_thread");
ALIAS("??0ETIThread@@IAE@XZ", "th_ctor");
ALIAS("??1ETIThread@@UAE@XZ", "th_dtor");
ALIAS("??_GETIThread@@UAEPAXI@Z", "th_destroy");
ALIAS("?start@ETIThread@@QAEHH@Z", "th_start");
ALIAS("?terminate@ETIThread@@UAEXXZ", "th_terminate");
ALIAS("?waitForExit@ETIThread@@UAEHXZ", "th_waitForExit");
ALIAS("?terminateAndWait@ETIThread@@QAEHXZ", "th_terminateAndWait");
ALIAS("?run@ETIThread@@EAEKXZ", "th_run");
ALIAS("?exit@ETIThread@@IAEXK@Z", "th_exit");
ALIAS("?execute@ETIThread@@CAPAXPAX@Z", "th_execute");
ALIAS("?shouldTerminate@ETIThread@@QBEHXZ", "th_shouldTerminate");
ALIAS("?getStatus@ETIThread@@QAE?AW4TStatus@1@XZ", "th_getStatus");
ALIAS("?checkStatus@ETIThread@@ABE?AW4TStatus@1@XZ", "th_checkStatus");
ALIAS("?getPriority@ETIThread@@QBE?AW4TPriority@1@XZ", "th_getPriority");
ALIAS("?setPriority@ETIThread@@QAEHW4TPriority@1@@Z", "th_setPriority");
ALIAS("?sleep@ETIThread@@SA?AW4TSleepReturn@1@JH@Z", "th_sleep");
