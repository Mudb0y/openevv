/* The queue an application takes its answers off.

   The synthesis thread does not call the application back on its own
   thread; it posts a message, and the application collects it later on its
   own. That collecting is `poll`, which is what eciSpeaking ends up
   calling, and it is why a caller that never polls never hears anything.

   Two counts keep the two sides in step: one of everything posted, one of
   everything seen. A special message carries the posted count across, and
   when the two agree there is nothing outstanding — which is what
   `synchronize` waits for.

   A callback can answer three ways. Nought means it is finished with.
   Minus one means not yet, come back to me: poll holds the message and
   starts a timer, synchronize sleeps thirty milliseconds and asks again.
   Minus eighteen means stop altogether. */

#include <stdint.h>
#include <stddef.h>
#include "evv_abi.h"
#include "eci_objects.h"

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

/* A message that carries one call back to the application. */
typedef struct {
    ETImessage base;
    ETIappMessageQueue *queue;  /* +0x20 */
    int32_t b;                  /* +0x24 */
    int32_t a;                  /* +0x28, seven marks the counting message */
    int32_t answer;             /* +0x2c */
} ETImsgUser;

#define MSG_USER  0xbb8
#define MSG_COUNT 7

struct ETIqueueVtbl {
    THIS void   *(*destroy)(ETIqueue *self, int32_t free_it);
    THIS int32_t (*push)(ETIqueue *self, void *p);
    THIS int32_t (*pop)(ETIqueue *self, void **out);
    THIS int32_t (*peekHead)(ETIqueue *self, void **out);
};
typedef struct ETIqueueVtbl ETIqueueVtbl;

typedef struct ETImessageQueue ETImessageQueue;
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

typedef int32_t (*Callback)(void *inst, int32_t a, int32_t b, void *param);

/* What poll and synchronize answer. */
#define APP_IDLE      1
#define APP_WORKING   2
#define APP_BUSY      3
#define APP_CAUGHT_UP 4
#define APP_STOPPED   6
#define APP_ABORTED   (-18)
#define APP_FAILED    (-2)
#define APP_AGAIN     (-1)

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

extern THIS ETImessage *msg_ctor(ETImessage *m, uint32_t type)
    MANGLED("??0ETImessage@@QAE@K@Z");
extern THIS void sy_mutexCtor(void *m, int32_t k) MANGLED("??0Mutex@@QAE@H@Z");
extern THIS void sy_mutexDtor(void *m) MANGLED("??1Mutex@@QAE@XZ");
extern THIS void sy_eventCtor(void *e, int32_t k) MANGLED("??0ETIEvent@@QAE@H@Z");
extern THIS void sy_eventDtor(void *e) MANGLED("??1ETIEvent@@QAE@XZ");
extern THIS int32_t sy_eventSignal(void *e) MANGLED("?signal@ETIEvent@@QAEHXZ");
extern THIS void *eq_ctor(ETIqueue *q, uint32_t n)
    MANGLED("??0ETIqueue@@QAE@K@Z");
extern THIS void eq_dtor(ETIqueue *q) MANGLED("??1ETIqueue@@UAE@XZ");
extern THIS int32_t eq_push(ETIqueue *q, void *p)
    MANGLED("?push@ETIqueue@@UAEHPAX@Z");
extern THIS int32_t eq_pop(ETIqueue *q, void **out)
    MANGLED("?pop@ETIqueue@@UAEHPAPAX@Z");
extern THIS int32_t eq_peekHead(ETIqueue *q, void **out)
    MANGLED("?peekHead@ETIqueue@@UAEHPAPAX@Z");
extern THIS void q_suspend(ETImessageQueue *q)
    MANGLED("?suspend@ETImessageQueue@@UAEXXZ");
extern THIS void q_resume(ETImessageQueue *q)
    MANGLED("?resume@ETImessageQueue@@UAEXXZ");
extern THIS int16_t q_sendMessage(ETImessageQueue *q, ETImessage *m,
                                     int32_t a, void *w, int32_t b)
    MANGLED("?sendMessage@ETImessageQueue@@UAEFPAVETImessage@@JPAX1@Z");
extern THIS int16_t q_postMessage(ETImessageQueue *q, ETImessage *m,
                                     int32_t a, void *w, int32_t b)
    MANGLED("?postMessage@ETImessageQueue@@UAEFPAVETImessage@@JPAX1@Z");
extern THIS int16_t q_popMessage(ETImessageQueue *q, ETImessage **out,
                                    int32_t f, void *e)
    MANGLED("?popMessage@ETImessageQueue@@UAEFPAPAVETImessage@@W4MessageQueueFlag@@PAVETIEvent@@@Z");
extern THIS void q_clearMessages(ETImessageQueue *q)
    MANGLED("?clearMessages@ETImessageQueue@@EAEXXZ");
extern int32_t th_sleep(int32_t ms, int32_t kind)
    MANGLED("?sleep@ETIThread@@SA?AW4TSleepReturn@1@JH@Z");

extern const QueueVtbl vtbl_queue;
extern const QueueVtbl vtbl_appqueue;
extern const ETIqueueVtbl vtbl_inner;
extern const MessageVtbl vtbl_message;
extern const MessageVtbl vtbl_msguser;

/* Windows tells an application that something arrived by putting a message
   in its window's own queue and, when a callback asks to be called again
   later, by setting a timer. Neither has a meaning without windows, so both
   are here and both do nothing when there is no window. */
#ifdef _WIN32
__attribute__((dllimport, stdcall)) int PeekMessageA(void *, void *, unsigned,
                                                     unsigned, unsigned);
__attribute__((dllimport, stdcall)) unsigned SetTimer(void *, unsigned,
                                                      unsigned, void *);
__attribute__((dllimport, stdcall)) int KillTimer(void *, unsigned);
#else
static int PeekMessageA(void *a, void *b, unsigned c, unsigned d, unsigned e)
{ (void)a; (void)b; (void)c; (void)d; (void)e; return 0; }
static unsigned SetTimer(void *a, unsigned b, unsigned c, void *d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }
static int KillTimer(void *a, unsigned b) { (void)a; (void)b; return 0; }
#endif

/* ---- making one ------------------------------------------------------ */

THIS ETImessageQueue *mq_ctor(ETImessageQueue *q)
{
    q->vt = &vtbl_queue;
    eq_ctor(&q->queue, 0x40);
    q->queue.vt = &vtbl_inner;
    sy_mutexCtor(q->lock, 0);
    sy_eventCtor(q->ready, 0);
    q->suspended = 0;
    sy_eventCtor(q->done, 0);
    sy_mutexCtor(q->send_lock, 0);
    return q;
}

THIS void mq_dtor(ETImessageQueue *q)
{
    q->vt = &vtbl_queue;
    sy_mutexDtor(q->send_lock);
    sy_eventDtor(q->done);
    sy_eventDtor(q->ready);
    sy_mutexDtor(q->lock);
    eq_dtor(&q->queue);
}

THIS void *inner_destroy(ETIqueue *q, int32_t free_it)
{
    eq_dtor(q);
    if (free_it & 1)
        cpp_delete(q);
    return q;
}

/* The callback and its parameter are deliberately not cleared here; the
   original leaves them as they were and registerCallback is the only thing
   that ever sets them. */
THIS ETIappMessageQueue *aq_ctor(ETIappMessageQueue *q)
{
    mq_ctor(&q->base);
    q->base.vt = &vtbl_appqueue;
    q->cb = 0;
    q->posted = 0;
    q->seen = 0;
    q->stopping = 0;
    q->held = 0;
    q->win = 0;
    q->post_flag = 0;
    return q;
}

THIS void aq_dtor(ETIappMessageQueue *q)
{
    q->base.vt = &vtbl_appqueue;
    mq_dtor(&q->base);
}

/* ---- telling the application ----------------------------------------- */

THIS void aq_registerCallback(ETIappMessageQueue *q, void *inst, Callback cb,
                              void *param, int16_t flag, void *win)
{
    q->cb_inst = inst;
    q->cb = cb;
    q->cb_param = param;
    q->post_flag = flag;
    q->win = win;
}

static THIS int32_t aq_callCallback(ETIappMessageQueue *q, int32_t a, int32_t b)
{
    if (q->cb == 0)
        return 0;
    return q->cb(q->cb_inst, a, b, q->cb_param);
}

THIS void msguser_run(ETImessage *m)
{
    ETImsgUser *u = (ETImsgUser *)m;

    u->answer = aq_callCallback(u->queue, u->a, u->b);
}

static THIS void aq_doTimer(ETIappMessageQueue *q, int32_t on)
{
    if (on)
        SetTimer(q->win, (unsigned)(size_t)q->cb_param, 30, 0);
    else
        KillTimer(q->win, (unsigned)(size_t)q->cb_param);
}

static THIS void aq_cleanUpWindowMessages(ETIappMessageQueue *q)
{
    char msg[0x1c];

    while (q->win != 0 && PeekMessageA(msg, q->win, 0, 0, 1))
        ;
}

/* ---- putting one on -------------------------------------------------- */

static ETImsgUser *makeUser(ETIappMessageQueue *q, int32_t a, int32_t b)
{
    ETImsgUser *u = cpp_new(sizeof *u);

    if (u == 0)
        return 0;
    msg_ctor(&u->base, MSG_USER);
    u->base.vt = &vtbl_msguser;
    u->queue = q;
    u->b = b;
    u->a = a;
    u->answer = 0;
    return u;
}

THIS int32_t aq_postUser(ETIappMessageQueue *q, int32_t a, int32_t b)
{
    ETImsgUser *u = makeUser(q, a, b);
    ETImessage *m = (ETImessage *)u;
    int32_t rc = 0;

    if (m == 0)
        return rc;
    m->vt->addRef(m);
    if (q->base.vt->postMessage(&q->base, m, q->post_flag, q->win,
                                (int32_t)(size_t)q->cb_param))
        rc = 1;
    m->vt->release(m);
    return rc;
}

THIS int32_t aq_sendUser(ETIappMessageQueue *q, int32_t a, int32_t b)
{
    ETImsgUser *u = makeUser(q, a, b);
    ETImessage *m = (ETImessage *)u;
    int32_t rc = 0;

    if (m == 0)
        return rc;
    m->vt->addRef(m);
    if (q->base.vt->sendMessage(&q->base, m, q->post_flag, q->win,
                                (int32_t)(size_t)q->cb_param))
        rc = 1;
    m->vt->release(m);
    return rc;
}

/* ---- taking them off ------------------------------------------------- */

/* A message the application has finished with is answered and let go. */
static THIS void aq_finish(ETIappMessageQueue *q, ETImessage *m)
{
    q->base.vt->signalProcessed(&q->base, m);
    m->vt->release(m);
}

THIS void aq_suspend(ETIappMessageQueue *q)
{
    q_suspend(&q->base);
    if (q->held != 0) {
        aq_doTimer(q, 0);
        aq_finish(q, q->held);
        q->held = 0;
    }
    aq_cleanUpWindowMessages(q);
}

THIS void aq_resume(ETIappMessageQueue *q)
{
    q_resume(&q->base);
}

/* One turn of the application's own loop. Whatever is waiting is run here,
   on the caller's thread, which is the whole point of the queue. */
THIS int32_t aq_poll(ETIappMessageQueue *q)
{
    ETImessage *m = 0;
    int32_t rc = APP_IDLE;
    int32_t answer;

    if (q->stopping != 0) {
        if (q->posted == q->seen)
            rc = APP_STOPPED;
        return rc;
    }

    rc = APP_WORKING;
    if (q->posted == q->seen)
        return rc;

    /* Anything deferred last time gets first go. */
    if (q->held != 0) {
        ETImessage *h = q->held;

        rc = APP_BUSY;
        h->vt->run(h);
        h->result = 1;
        answer = ((ETImsgUser *)h)->answer;
        if (answer == APP_ABORTED) {
            rc = APP_ABORTED;
            aq_finish(q, h);
            q->held = 0;
            aq_doTimer(q, 0);
        } else if (answer == 0) {
            aq_finish(q, h);
            q->held = 0;
            aq_doTimer(q, 0);
        }
    }

    while (rc == APP_WORKING) {
        if (!q->base.vt->popMessage(&q->base, &m, 1, 0)) {
            rc = APP_FAILED;
            continue;
        }
        if (m == 0) {
            rc = APP_BUSY;
            continue;
        }
        if (q->win != 0) {
            char junk[0x1c];

            PeekMessageA(junk, q->win, 0, 0, 1);
        }
        if (((ETImsgUser *)m)->a == MSG_COUNT) {
            q->seen = ((ETImsgUser *)m)->b;
            if (q->seen == q->posted) {
                rc = APP_CAUGHT_UP;
                aq_cleanUpWindowMessages(q);
            }
            aq_finish(q, m);
            continue;
        }
        m->vt->run(m);
        m->result = 1;
        answer = ((ETImsgUser *)m)->answer;
        if (answer == APP_ABORTED) {
            aq_finish(q, m);
            rc = APP_ABORTED;
        } else if (answer == APP_AGAIN) {
            rc = APP_BUSY;
            q->held = m;
            aq_doTimer(q, 1);
        } else {
            aq_finish(q, m);
        }
    }
    return rc;
}

/* Run everything outstanding and do not come back until the two counts
   agree. A callback that asks to be called again is slept on rather than
   deferred, because there is nobody else to come back later. */
THIS int32_t aq_synchronize(ETIappMessageQueue *q)
{
    ETImessage *m = 0;
    int32_t rc = APP_IDLE;
    int32_t answer = 0;

    if (q->stopping != 0)
        return rc;
    rc = APP_WORKING;
    if (q->posted == q->seen)
        return rc;

    while (rc != APP_CAUGHT_UP) {
        if (!q->base.vt->popMessage(&q->base, &m, 0, 0)) {
            rc = APP_FAILED;
            continue;
        }
        if (m == 0)
            continue;
        if (q->win != 0) {
            char junk[0x1c];

            PeekMessageA(junk, q->win, 0, 0, 1);
        }
        if (((ETImsgUser *)m)->a == MSG_COUNT) {
            q->seen = ((ETImsgUser *)m)->b;
            if (q->seen == q->posted) {
                rc = APP_CAUGHT_UP;
                aq_cleanUpWindowMessages(q);
            }
        } else {
            m->vt->run(m);
            m->result = 1;
            answer = ((ETImsgUser *)m)->answer;
            while (answer == APP_AGAIN) {
                th_sleep(30, 0);
                m->vt->run(m);
                m->result = 1;
                answer = ((ETImsgUser *)m)->answer;
            }
        }
        aq_finish(q, m);
        if (answer == APP_ABORTED)
            return answer;
    }
    return rc;
}

/* ---- the tables ------------------------------------------------------ */

extern THIS void *msg_destroy(ETImessage *m, int32_t f)
    MANGLED("??_GETImessage@@UAEPAXI@Z");
extern THIS uint32_t msg_addRef(ETImessage *m)
    MANGLED("?addRef@ETImessage@@UAEKXZ");
extern THIS uint32_t msg_release(ETImessage *m)
    MANGLED("?release@ETImessage@@UAEKXZ");
extern THIS uint32_t msg_getType(const ETImessage *m)
    MANGLED("?getMessageType@ETImessage@@UBEKXZ");
extern THIS int32_t msg_equalsMessage(ETImessage *m, ETImessage *o)
    MANGLED("?equals@ETImessage@@UAEHPAV1@@Z");
extern THIS int32_t msg_equalsType(ETImessage *m, uint32_t t)
    MANGLED("?equals@ETImessage@@UAEHK@Z");
extern THIS void mq_signalProcessed(ETImessageQueue *q, ETImessage *m)
    MANGLED("?signalProcessed@ETImessageQueue@@UAEXPAVETImessage@@@Z");

THIS void *msguser_destroy(ETImessage *m, int32_t free_it)
{
    m->vt = &vtbl_message;
    sy_mutexDtor(m->lock);
    if (free_it & 1)
        cpp_delete(m);
    return m;
}

THIS void mq_signalProcessed(ETImessageQueue *q, ETImessage *m)
{
    if (m != 0 && m->is_send != 0)
        sy_eventSignal(q->done);
}

const ETIqueueVtbl vtbl_inner = {
    inner_destroy, eq_push, eq_pop, eq_peekHead
};

const QueueVtbl vtbl_queue = {
    q_sendMessage, q_postMessage, q_popMessage,
    q_suspend, q_resume, mq_signalProcessed, q_clearMessages
};

const QueueVtbl vtbl_appqueue = {
    q_sendMessage, q_postMessage, q_popMessage,
    (void *)aq_suspend, (void *)aq_resume, mq_signalProcessed,
    q_clearMessages
};

const MessageVtbl vtbl_msguser = {
    msguser_destroy, msg_addRef, msg_release, msg_getType,
    msg_equalsMessage, msg_equalsType, msguser_run
};

ALIAS("??_7ETImessageQueue@@6B@", "vtbl_queue");
ALIAS("??_7ETIappMessageQueue@@6B@", "vtbl_appqueue");
ALIAS("??_7Queue@ETImessageQueue@@6B@", "vtbl_inner");
ALIAS("??_7ETImsgUser@@6B@", "vtbl_msguser");
ALIAS("??0ETImessageQueue@@QAE@XZ", "mq_ctor");
ALIAS("??1ETImessageQueue@@QAE@XZ", "mq_dtor");
ALIAS("??0ETIappMessageQueue@@QAE@XZ", "aq_ctor");
ALIAS("??1ETIappMessageQueue@@QAE@XZ", "aq_dtor");
ALIAS("??_GQueue@ETImessageQueue@@UAEPAXI@Z", "inner_destroy");
ALIAS("??_GETImsgUser@@UAEPAXI@Z", "msguser_destroy");
ALIAS("?run@ETImsgUser@@UAEXXZ", "msguser_run");
ALIAS("?signalProcessed@ETImessageQueue@@UAEXPAVETImessage@@@Z",
      "mq_signalProcessed");
ALIAS("?suspend@ETIappMessageQueue@@UAEXXZ", "aq_suspend");
ALIAS("?resume@ETIappMessageQueue@@UAEXXZ", "aq_resume");
ALIAS("?poll@ETIappMessageQueue@@QAEJXZ", "aq_poll");
ALIAS("?synchronize@ETIappMessageQueue@@QAEJXZ", "aq_synchronize");
ALIAS("?postUser@ETIappMessageQueue@@QAEHJJ@Z", "aq_postUser");
ALIAS("?sendUser@ETIappMessageQueue@@QAEHJJ@Z", "aq_sendUser");
ALIAS("?registerCallback@ETIappMessageQueue@@QAEXPAXP6AJ0JJ0@Z0F0@Z",
      "aq_registerCallback");
ALIAS("?callCallback@ETIappMessageQueue@@AAEJJJ@Z", "aq_callCallback");
ALIAS("?doTimer@ETIappMessageQueue@@AAEXH@Z", "aq_doTimer");
ALIAS("?cleanUpWindowMessages@ETIappMessageQueue@@AAEXXZ",
      "aq_cleanUpWindowMessages");
