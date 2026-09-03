/* Messages, and the queue a thread takes them off.

   This is how everything above the Delta machine talks to itself. A caller
   builds a message, posts it, and either walks away or waits; a thread sits
   in a loop pulling messages off and running them. Posting takes a
   reference and running gives it back, so a message outlives whichever of
   the two finishes with it first.

   Two kinds of send. Posting returns as soon as the message is on the
   queue. Sending puts it there and then waits on an event the thread
   signals when it has run it, which is how a caller gets an answer back.

   The tables of virtual functions are written out at the end. Their order
   is not a choice: it is read off the original's own tables, and getting a
   slot wrong is a jump into the wrong function rather than a quiet
   difference, so it is worth saying where each one came from. */

#include <stdlib.h>
#include <stdint.h>
#include "evv_abi.h"
#include "eci_objects.h"

/* Answers a post can give. */
#define POST_FAILED    0
#define POST_QUEUED    1
#define POST_REFUSED   2

/* Slot for slot as the original's table has them. */
struct MessageVtbl {
    THIS void    *(*destroy)(ETImessage *self, int32_t free_it);
    THIS uint32_t (*addRef)(ETImessage *self);
    THIS uint32_t (*release)(ETImessage *self);
    THIS uint32_t (*getMessageType)(const ETImessage *self);
    THIS int32_t  (*equalsMessage)(ETImessage *self, ETImessage *other);
    THIS int32_t  (*equalsType)(ETImessage *self, uint32_t type);
    THIS void     (*run)(ETImessage *self);
};
typedef struct MessageVtbl MessageVtbl;

/* The one message the queue itself sends: stop. */
typedef struct {
    ETImessage base;
    void      *thread;      /* +0x20 */
} ETImsgQuit;

struct QueueVtbl {
    THIS int16_t (*sendMessage)(ETImessageQueue *self, ETImessage *m,
                                int32_t a, void *win, int32_t b);
    THIS int16_t (*postMessage)(ETImessageQueue *self, ETImessage *m,
                                int32_t a, void *win, int32_t b);
    THIS int16_t (*popMessage)(ETImessageQueue *self, ETImessage **out,
                               int32_t flag, void *event);
    THIS void    (*suspend)(ETImessageQueue *self);
    THIS void    (*resume)(ETImessageQueue *self);
    THIS void    (*signalProcessed)(ETImessageQueue *self, ETImessage *m);
    THIS void    (*clearMessages)(ETImessageQueue *self);
};
typedef struct QueueVtbl QueueVtbl;

/* The queue of pointers underneath, which is a class of its own. */
struct ETIqueueVtbl {
    THIS void   *(*destroy)(ETIqueue *self, int32_t free_it);
    THIS int32_t (*push)(ETIqueue *self, void *p);
    THIS int32_t (*pop)(ETIqueue *self, void **out);
    THIS int32_t (*peekHead)(ETIqueue *self, void **out);
};
typedef struct ETIqueueVtbl ETIqueueVtbl;

/* A thread with a queue inside it, and the two events it is driven by. */
typedef struct ETImessageQueueThread ETImessageQueueThread;

struct ETImqThreadVtbl {
    THIS void   *(*destroy)(ETImessageQueueThread *self, int32_t free_it);
    THIS void    (*terminate)(ETImessageQueueThread *self);
    THIS int32_t (*waitForExit)(ETImessageQueueThread *self);
    THIS uint32_t (*run)(ETImessageQueueThread *self);
    THIS void    (*setToTerminate)(ETImessageQueueThread *self);
    THIS void    (*translateMessage)(ETImessageQueueThread *self,
                                     ETImessage **m);
};
typedef struct ETImqThreadVtbl ThreadVtbl;

/* ---- what the original supplies -------------------------------------- */

extern THIS void *sy_mutexCtor(void *m, int32_t kind) MANGLED("??0Mutex@@QAE@H@Z");
extern THIS void  sy_mutexDtor(void *m) MANGLED("??1Mutex@@QAE@XZ");
extern THIS int32_t sy_mutexWait(void *m, int32_t ms) MANGLED("?wait@Mutex@@QAEHJ@Z");
extern THIS int32_t sy_mutexRelease(void *m) MANGLED("?release@Mutex@@QAEHXZ");

extern THIS int32_t sy_eventSignal(void *e) MANGLED("?signal@ETIEvent@@QAEHXZ");
extern THIS int32_t sy_eventUnsignal(void *e) MANGLED("?unsignal@ETIEvent@@QAEHXZ");
extern THIS int32_t sy_eventWait(void *e, int32_t ms) MANGLED("?wait@ETIEvent@@QAEHJ@Z");

extern THIS int32_t eq_isEmpty(ETIqueue *q) MANGLED("?isEmpty@ETIqueue@@QAEHXZ");
extern THIS int32_t th_shouldTerminate(const void *t)
    MANGLED("?shouldTerminate@ETIThread@@QBEHXZ");

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

extern const MessageVtbl vtbl_message;
extern const MessageVtbl vtbl_quit;

/* Waking a window is how the original tells an application thread that a
   message has arrived. Nothing in this library ever supplies a window —
   every caller passes nothing — so the branch is dead here, and it is the
   one place a Windows host would want its own answer. */
#ifdef _WIN32
__attribute__((dllimport, stdcall)) int PostMessageA(void *, unsigned,
                                                     unsigned, long);
static int wakeWindow(void *win, int32_t a, int32_t b)
{
    return PostMessageA(win, (unsigned)a, (unsigned)b, 0) ? 1 : 0;
}
#else
static int wakeWindow(void *win, int32_t a, int32_t b)
{
    (void)win; (void)a; (void)b;
    return 0;
}
#endif

/* ---- a message ------------------------------------------------------- */

THIS ETImessage *msg_ctor(ETImessage *m, uint32_t type)
{
    m->vt = &vtbl_message;
    m->type = type;
    m->result = 0;
    m->refs = 0;
    m->is_send = 0;
    sy_mutexCtor(m->lock, 0);
    return m;
}

THIS uint32_t msg_addRef(ETImessage *m)
{
    uint32_t now;

    sy_mutexWait(m->lock, -1);
    m->refs++;
    now = (uint32_t)m->refs;
    sy_mutexRelease(m->lock);
    return now;
}

/* The last one out throws it away, and the lock is given up first because
   the object is about to stop existing. */
THIS uint32_t msg_release(ETImessage *m)
{
    uint32_t left;

    sy_mutexWait(m->lock, -1);
    m->refs--;
    left = (uint32_t)m->refs;
    if (m->refs != 0) {
        sy_mutexRelease(m->lock);
        return left;
    }
    sy_mutexRelease(m->lock);
    m->vt->destroy(m, 1);
    return 0;
}

THIS uint32_t msg_getType(const ETImessage *m)
{
    return m->type;
}

/* Both of these ask through the table rather than reading the field, so a
   message that computes its type answers for itself. */
THIS int32_t msg_equalsType(ETImessage *m, uint32_t type)
{
    return m->vt->getMessageType(m) == type;
}

THIS int32_t msg_equalsMessage(ETImessage *m, ETImessage *other)
{
    return m->vt->getMessageType(m) == other->vt->getMessageType(other);
}

THIS void *msg_destroy(ETImessage *m, int32_t free_it)
{
    m->vt = &vtbl_message;
    sy_mutexDtor(m->lock);
    if (free_it & 1)
        cpp_delete(m);
    return m;
}

THIS void *quit_destroy(ETImessage *m, int32_t free_it)
{
    m->vt = &vtbl_message;
    sy_mutexDtor(m->lock);
    if (free_it & 1)
        cpp_delete(m);
    return m;
}

/* Running the quit message is what actually stops the thread. */
THIS void quit_run(ETImessage *m)
{
    ETImessageQueueThread *t = (ETImessageQueueThread *)((ETImsgQuit *)m)->thread;

    t->vt->setToTerminate(t);
}

/* ---- the queue ------------------------------------------------------- */

THIS int16_t q_postMessage(ETImessageQueue *q, ETImessage *m, int32_t a,
                           void *win, int32_t b)
{
    int16_t rc = POST_REFUSED;
    int32_t ok;

    sy_mutexWait(q->lock, -1);
    m->result = 0;
    if (q->suspended == 0) {
        m->vt->addRef(m);
        ok = q->queue.vt->push(&q->queue, m);
        if (ok) {
            ok = sy_eventSignal(q->ready);
            if (ok && win != 0)
                ok = wakeWindow(win, a, b);
            if (!ok)
                q->queue.vt->pop(&q->queue, (void **)&m);
        }
        if (!ok) {
            m->vt->release(m);
            rc = POST_FAILED;
        } else {
            rc = POST_QUEUED;
        }
    }
    sy_mutexRelease(q->lock);
    return rc;
}

/* Post it and wait for the thread to say it has run it. The extra
   reference is ours for the duration, so the message cannot be thrown away
   underneath us while we wait. */
THIS int16_t q_sendMessage(ETImessageQueue *q, ETImessage *m, int32_t a,
                           void *win, int32_t b)
{
    int16_t rc;

    sy_mutexWait(q->send_lock, -1);
    m->vt->addRef(m);
    m->is_send = 1;
    rc = q->vt->postMessage(q, m, a, win, b);
    if (rc == POST_QUEUED) {
        if (!sy_eventWait(q->done, -1)) {
            rc = POST_FAILED;
        } else {
            if (m->result == 0)
                rc = POST_REFUSED;
            sy_eventUnsignal(q->done);
        }
    }
    sy_mutexRelease(q->send_lock);
    m->vt->release(m);
    return rc;
}

/* Take the next message off. Asked to wait, it sleeps until one arrives;
   asked not to, it looks once. An empty queue answers yes with nothing in
   it rather than no. */
THIS int16_t q_popMessage(ETImessageQueue *q, ETImessage **out, int32_t flag,
                          void *event)
{
    int16_t rc = 0;
    int32_t ok = 1;

    *out = 0;
    if (flag == 0)
        ok = sy_eventWait(q->ready, -1);
    if (!ok)
        return rc;

    sy_mutexWait(q->lock, -1);
    if (eq_isEmpty(&q->queue)) {
        rc = 1;
        sy_eventUnsignal(q->ready);
    } else if (q->queue.vt->pop(&q->queue, (void **)out)) {
        rc = 1;
        if (event != 0)
            sy_eventUnsignal(event);
        if (eq_isEmpty(&q->queue))
            sy_eventUnsignal(q->ready);
    }
    sy_mutexRelease(q->lock);
    return rc;
}

/* A suspended queue throws away what it is holding and refuses more. */
THIS void q_suspend(ETImessageQueue *q)
{
    sy_mutexWait(q->lock, -1);
    q->vt->clearMessages(q);
    q->suspended = 1;
    sy_mutexRelease(q->lock);
}

THIS void q_resume(ETImessageQueue *q)
{
    sy_mutexWait(q->lock, -1);
    q->suspended = 0;
    sy_mutexRelease(q->lock);
}

/* Anyone waiting on a thrown-away message is told it is done with, or they
   would wait for ever. */
THIS void q_clearMessages(ETImessageQueue *q)
{
    ETImessage *m = 0;
    int32_t more = 1;

    while (more) {
        if (eq_isEmpty(&q->queue))
            break;
        more = q->queue.vt->pop(&q->queue, (void **)&m);
        if (!more)
            break;
        q->vt->signalProcessed(q, m);
        m->vt->release(m);
    }
    sy_eventUnsignal(q->ready);
}

/* ---- the thread that runs them --------------------------------------- */

THIS uint32_t qt_run(ETImessageQueueThread *t)
{
    ETImessage *m = 0;

    while (!th_shouldTerminate(t)) {
        if (!t->queue.vt->popMessage(&t->queue, &m, 0, t->turn))
            break;
        if (m != 0) {
            ETImessage *ran;

            t->vt->translateMessage(t, &m);
            ran = m;
            ran->vt->run(ran);
            ran->result = 1;
            t->queue.vt->signalProcessed(&t->queue, m);
            m->vt->release(m);
            m = 0;
        }
        sy_eventSignal(t->turn);
    }
    return 0;
}

/* Stopping means sending the thread a message it will act on, so that it
   stops between two messages rather than in the middle of one. */
THIS void qt_terminate(ETImessageQueueThread *t)
{
    sy_eventWait(t->gate, -1);
    sy_eventUnsignal(t->gate);

    if (t->asked_to_stop == 0) {
        ETImsgQuit *q = cpp_new(sizeof *q);
        ETImessage *m;

        if (q != 0) {
            msg_ctor(&q->base, 0);
            q->base.vt = &vtbl_quit;
            q->thread = t;
        }
        m = (ETImessage *)q;
        if (m != 0) {
            m->vt->addRef(m);
            if (t->queue.vt->sendMessage(&t->queue, m, 0, 0, 0) == POST_QUEUED)
                t->asked_to_stop = 1;
            m->vt->release(m);
        }
    }
    sy_eventSignal(t->gate);
}

/* ---- the tables ------------------------------------------------------ */

/* A message with no run of its own cannot be run; the original puts the
   compiler's own complaint in that slot and so does this. */
extern void purecall(void) MANGLED("__purecall");

const MessageVtbl vtbl_message = {
    msg_destroy, msg_addRef, msg_release, msg_getType,
    msg_equalsMessage, msg_equalsType, (void *)purecall
};

const MessageVtbl vtbl_quit = {
    quit_destroy, msg_addRef, msg_release, msg_getType,
    msg_equalsMessage, msg_equalsType, quit_run
};

ALIAS("??_7ETImessage@@6B@", "vtbl_message");
ALIAS("??_7ETImsgQuit@@6B@", "vtbl_quit");
ALIAS("??0ETImessage@@QAE@K@Z", "msg_ctor");
ALIAS("?addRef@ETImessage@@UAEKXZ", "msg_addRef");
ALIAS("?release@ETImessage@@UAEKXZ", "msg_release");
ALIAS("?getMessageType@ETImessage@@UBEKXZ", "msg_getType");
ALIAS("?equals@ETImessage@@UAEHK@Z", "msg_equalsType");
ALIAS("?equals@ETImessage@@UAEHPAV1@@Z", "msg_equalsMessage");
ALIAS("??_GETImessage@@UAEPAXI@Z", "msg_destroy");
ALIAS("??_GETImsgQuit@@UAEPAXI@Z", "quit_destroy");
ALIAS("?run@ETImsgQuit@@UAEXXZ", "quit_run");
ALIAS("?postMessage@ETImessageQueue@@UAEFPAVETImessage@@JPAX1@Z",
      "q_postMessage");
ALIAS("?sendMessage@ETImessageQueue@@UAEFPAVETImessage@@JPAX1@Z",
      "q_sendMessage");
ALIAS("?popMessage@ETImessageQueue@@UAEFPAPAVETImessage@@W4MessageQueueFlag@@PAVETIEvent@@@Z",
      "q_popMessage");
ALIAS("?suspend@ETImessageQueue@@UAEXXZ", "q_suspend");
ALIAS("?resume@ETImessageQueue@@UAEXXZ", "q_resume");
ALIAS("?clearMessages@ETImessageQueue@@EAEXXZ", "q_clearMessages");
ALIAS("?run@ETImessageQueueThread@@MAEKXZ", "qt_run");
ALIAS("?terminate@ETImessageQueueThread@@MAEXXZ", "qt_terminate");
