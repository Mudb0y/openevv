/* The thread that pushes finished samples at the audio device.

   Everything the engine wants done to the sound output happens here rather
   than on the caller's thread, and it happens by message: open, write,
   flush, hold, reset, close, and a mark to be reported when the samples
   around it are heard. Each of those is a small class with one run of its
   own, which is why this file is mostly the same eleven lines eleven times.

   Nearly all of them are sent rather than posted, because the caller wants
   the answer; only laying down an index mark is posted, because there is
   nothing to wait for.

   The one piece with a shape of its own is the index callback. Asking for
   one starts a repeating timer that posts a poll message, and that poll is
   what notices a mark has been reached; asking for none stops the timer
   again. A target without a timer thread would put that poll wherever its
   audio buffers are drained. */

#include <stdint.h>
#include <stddef.h>
#include "evv_abi.h"
#include "eci_objects.h"

typedef struct ETImessage ETImessage;

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

struct QueueVtbl {
    THIS int16_t (*sendMessage)(ETImessageQueue *q, ETImessage *m, int32_t a,
                                void *w, int32_t b);
    THIS int16_t (*postMessage)(ETImessageQueue *q, ETImessage *m, int32_t a,
                                void *w, int32_t b);
    THIS int16_t (*popMessage)(ETImessageQueue *q, ETImessage **o, int32_t f,
                               void *e);
    THIS void    (*suspend)(ETImessageQueue *q);
    THIS void    (*resume)(ETImessageQueue *q);
    THIS void    (*signalProcessed)(ETImessageQueue *q, ETImessage *m);
    THIS void    (*clearMessages)(ETImessageQueue *q);
};
typedef struct QueueVtbl QueueVtbl;
struct ETIqueueVtbl {
    THIS void   *(*destroy)(ETIqueue *self, int32_t free_it);
    THIS int32_t (*push)(ETIqueue *self, void *p);
    THIS int32_t (*pop)(ETIqueue *self, void **out);
    THIS int32_t (*peekHead)(ETIqueue *self, void **out);
};
typedef struct ETIqueueVtbl ETIqueueVtbl;

typedef struct ETImessageQueueThread ETImessageQueueThread;
struct ETImqThreadVtbl {
    THIS void    *(*destroy)(void *self, int32_t free_it);
    THIS void     (*terminate)(void *self);
    THIS int32_t  (*waitForExit)(void *self);
    THIS uint32_t (*run)(void *self);
    THIS void     (*setToTerminate)(void *self);
    THIS void     (*translateMessage)(void *self, ETImessage **m);
};
typedef struct ETImqThreadVtbl ThreadVtbl;

/* The device, and the timer that keeps asking it how far it has got. */
typedef struct { uint8_t opaque[0x40]; } SoundOutput;
typedef struct TimerThread TimerThread;
typedef struct TimerThreadTimer TimerThreadTimer;

typedef struct {
    ETImessageQueueThread base;  /* +0x00 */
    SoundOutput out;             /* +0x88 */
    TimerThread *timers;         /* +0xc8 */
    TimerThreadTimer *tick;      /* +0xcc */
} SoundThread;

/* What a caller has to allocate for one. Only this file knows what is in it. */
const uint32_t st_bytes = sizeof(SoundThread);

/* What each message is for. */
#define SND_OPEN    0x3e8
#define SND_WRITE   0x3e9
#define SND_HOLD    0x3ea
#define SND_CLOSE   0x3eb
#define SND_RESET   0x3ec
#define SND_FLUSH   0x3ed
#define SND_INDEX   0x3ee
#define SND_STATUS  0x3ef
#define SND_SETCB   0x3f0
#define SND_SETUP   0x3f1

/* How often the poll message asks. */
#define SND_TICK_MS 30

/* ---- what the original supplies --------------------------------------- */

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");
extern int32_t RAL_THREAD_PRIORITY_NORMAL;

extern THIS ETImessage *msg_ctor(ETImessage *m, uint32_t type)
    MANGLED("??0ETImessage@@QAE@K@Z");
extern THIS void sy_mutexDtor(void *m) MANGLED("??1Mutex@@QAE@XZ");
extern THIS void sy_eventCtor(void *e, int32_t held)
    MANGLED("??0ETIEvent@@QAE@H@Z");
extern THIS void sy_eventDtor(void *e) MANGLED("??1ETIEvent@@QAE@XZ");
extern THIS int32_t sy_eventWait(void *e, int32_t ms)
    MANGLED("?wait@ETIEvent@@QAEHJ@Z");
extern THIS int32_t sy_eventSignal(void *e) MANGLED("?signal@ETIEvent@@QAEHXZ");
extern THIS int32_t sy_eventUnsignal(void *e)
    MANGLED("?unsignal@ETIEvent@@QAEHXZ");

extern THIS void *th_ctor(void *t) MANGLED("??0ETIThread@@IAE@XZ");
extern THIS void  th_dtor(void *t) MANGLED("??1ETIThread@@UAE@XZ");
extern THIS int32_t th_start(void *t, int32_t p) MANGLED("?start@ETIThread@@QAEHH@Z");
extern THIS void  th_terminate(void *t) MANGLED("?terminate@ETIThread@@UAEXXZ");
extern THIS int32_t th_waitForExit(void *t)
    MANGLED("?waitForExit@ETIThread@@UAEHXZ");
extern THIS int32_t th_terminateAndWait(void *t)
    MANGLED("?terminateAndWait@ETIThread@@QAEHXZ");
extern THIS int32_t th_getStatus(void *t)
    MANGLED("?getStatus@ETIThread@@QAE?AW4TStatus@1@XZ");
extern THIS int32_t th_shouldTerminate(const void *t)
    MANGLED("?shouldTerminate@ETIThread@@QBEHXZ");

extern THIS ETImessageQueue *mq_ctor(ETImessageQueue *q)
    MANGLED("??0ETImessageQueue@@QAE@XZ");
extern THIS void mq_dtor(ETImessageQueue *q)
    MANGLED("??1ETImessageQueue@@QAE@XZ");
extern THIS uint32_t qt_run(void *t) MANGLED("?run@ETImessageQueueThread@@MAEKXZ");
extern THIS void qt_terminate(void *t)
    MANGLED("?terminate@ETImessageQueueThread@@MAEXXZ");

extern THIS void *pcm_ctor(SoundOutput *o) MANGLED("??0SoundOutput@@QAE@XZ");
extern THIS void  pcm_dtor(SoundOutput *o) MANGLED("??1SoundOutput@@QAE@XZ");
extern THIS int16_t pcm_open(SoundOutput *o)
    MANGLED("?open@SoundOutput@@QAE?AW4SoundFileErrorEnum@@XZ");
extern THIS int32_t pcm_close(SoundOutput *o) MANGLED("?close@SoundOutput@@QAEHXZ");
extern THIS int32_t pcm_reset(SoundOutput *o) MANGLED("?reset@SoundOutput@@QAEHXZ");
extern THIS int32_t pcm_flush(SoundOutput *o)
    MANGLED("?flush@SoundOutput@@QAE?AW4SoundFileErrorEnum@@XZ");
extern THIS int32_t pcm_hold(SoundOutput *o, int32_t on)
    MANGLED("?hold@SoundOutput@@QAEHH@Z");
extern THIS int32_t pcm_write(SoundOutput *o, const int32_t *data, uint32_t n)
    MANGLED("?write@SoundOutput@@QAE?AW4SoundFileErrorEnum@@PBJI@Z");
extern THIS int32_t pcm_insertIndex(SoundOutput *o, int32_t i)
    MANGLED("?insertIndex@SoundOutput@@QAEHJ@Z");
extern THIS int16_t pcm_getStatus(SoundOutput *o)
    MANGLED("?getStatus@SoundOutput@@QAE?AW4SoundFileStatusEnum@@XZ");
extern THIS int32_t pcm_setup(SoundOutput *o, char *a, int32_t *b, int32_t *c,
                             int32_t *d, int32_t *e, int32_t *f, int32_t *g,
                             int32_t *h)
    MANGLED("?setup@SoundOutput@@QAEHPADPAJ111111@Z");

extern void soundFilePoll(SoundOutput *o);
extern void soundFileSetIndexCallback(SoundOutput *o, void *cb, void *param);

extern THIS TimerThreadTimer *tt_setTimer(TimerThread *t,
                                          ETImessageQueueThread *who,
                                          ETImessage *m, uint32_t ms)
    MANGLED("?setTimer@TimerThread@@QAEPAVTimerThreadTimer@@PAVETImessageQueueThread@@PAVETImessage@@K@Z");
extern THIS void tt_killTimer(TimerThread *t, TimerThreadTimer *h)
    MANGLED("?killTimer@TimerThread@@QAEXPAVTimerThreadTimer@@@Z");

extern const MessageVtbl vtbl_message;
extern const MessageVtbl vtbl_open, vtbl_close, vtbl_reset, vtbl_flush;
extern const MessageVtbl vtbl_hold, vtbl_write, vtbl_setup, vtbl_index;
extern const MessageVtbl vtbl_status, vtbl_poll, vtbl_setcb;
extern const ThreadVtbl vtbl_mqthread;
extern const ThreadVtbl vtbl_sound;

/* Every one of these messages carries its answer back in the same place. */
typedef struct {
    ETImessage base;
    int32_t answer;   /* +0x20 */
    void   *subject;  /* +0x24, the device, or the thread when it needs one */
    int32_t a;        /* +0x28 */
    int32_t b;        /* +0x2c */
    int32_t c[6];     /* +0x30 */
} SndMsg;

/* Except the two that answer the other way round. */
typedef struct {
    ETImessage base;
    SoundOutput *out;  /* +0x20 */
    int16_t answer;    /* +0x24 */
} StatusMsg;

/* ---- the thread with a queue in it ------------------------------------ */

THIS ETImessageQueueThread *qt_ctor(ETImessageQueueThread *t)
{
    th_ctor(t);
    t->vt = &vtbl_mqthread;
    mq_ctor(&t->queue);
    sy_eventCtor(t->turn, 1);
    sy_eventCtor(t->gate, 1);
    t->asked_to_stop = 0;
    return t;
}

THIS void qt_dtor(ETImessageQueueThread *t)
{
    t->vt = &vtbl_mqthread;
    th_terminateAndWait(t);
    sy_eventDtor(t->gate);
    sy_eventDtor(t->turn);
    mq_dtor(&t->queue);
    th_dtor(t);
}

THIS void *qt_destroy(ETImessageQueueThread *t, int32_t free_it)
{
    qt_dtor(t);
    if (free_it & 1)
        cpp_delete(t);
    return t;
}

THIS void qt_setToTerminate(ETImessageQueueThread *t)
{
    th_terminate(t);
}

/* Nothing to translate; a thread with messages of its own overrides this. */
THIS void qt_translateMessage(ETImessageQueueThread *t, ETImessage **m)
{
    (void)t;
    (void)m;
}

THIS int32_t qt_waitForExit(ETImessageQueueThread *t)
{
    int32_t rc = 0;

    sy_eventWait(t->gate, -1);
    sy_eventUnsignal(t->gate);
    if (t->asked_to_stop != 0)
        rc = th_waitForExit(t);
    sy_eventSignal(t->gate);
    return rc;
}

/* A message only goes on if the thread is running and has not been asked to
   stop, or it would sit there for ever. */
THIS int16_t qt_postMessage(ETImessageQueueThread *t, ETImessage *m)
{
    int16_t rc = 0;

    sy_eventWait(t->gate, -1);
    if (th_getStatus(t) == 1 && !th_shouldTerminate(t))
        rc = t->queue.vt->postMessage(&t->queue, m, 0, 0, 0);
    return rc;
}

THIS int16_t qt_sendMessage(ETImessageQueueThread *t, ETImessage *m)
{
    int16_t rc = 0;

    sy_eventWait(t->gate, -1);
    if (th_getStatus(t) == 1 && !th_shouldTerminate(t))
        rc = t->queue.vt->sendMessage(&t->queue, m, 0, 0, 0);
    return rc;
}

/* ---- the messages ----------------------------------------------------- */

static THIS void *snd_destroy(ETImessage *m, int32_t free_it)
{
    m->vt = &vtbl_message;
    sy_mutexDtor(m->lock);
    if (free_it & 1)
        cpp_delete(m);
    return m;
}

static THIS void run_open(ETImessage *m)
{
    SndMsg *s = (SndMsg *)m;

    s->answer = pcm_open(s->subject);
}

static THIS void run_close(ETImessage *m)
{
    SndMsg *s = (SndMsg *)m;
    SoundThread *t = s->subject;

    if (t->tick != 0) {
        tt_killTimer(t->timers, t->tick);
        t->tick = 0;
    }
    s->answer = pcm_close(&t->out);
}

static THIS void run_reset(ETImessage *m)
{
    SndMsg *s = (SndMsg *)m;

    s->answer = pcm_reset(s->subject);
}

static THIS void run_flush(ETImessage *m)
{
    SndMsg *s = (SndMsg *)m;

    s->answer = pcm_flush(s->subject);
}

static THIS void run_hold(ETImessage *m)
{
    SndMsg *s = (SndMsg *)m;

    s->answer = pcm_hold(s->subject, s->a);
}

static THIS void run_write(ETImessage *m)
{
    SndMsg *s = (SndMsg *)m;

    s->answer = pcm_write(s->subject, (const int32_t *)(size_t)s->a,
                         (uint32_t)s->b);
}

static THIS void run_setup(ETImessage *m)
{
    SndMsg *s = (SndMsg *)m;

    s->answer = pcm_setup(s->subject, (char *)(size_t)s->a,
                         (int32_t *)(size_t)s->b,
                         (int32_t *)(size_t)s->c[0],
                         (int32_t *)(size_t)s->c[1],
                         (int32_t *)(size_t)s->c[2],
                         (int32_t *)(size_t)s->c[3],
                         (int32_t *)(size_t)s->c[4],
                         (int32_t *)(size_t)s->c[5]);
}

/* Laying down a mark is the one that is posted, so its answer is set before
   anyone could read it and the mark itself is dropped if the device has
   already gone. */
static THIS void run_insertIndex(ETImessage *m)
{
    SndMsg *s = (SndMsg *)m;
    SoundThread *t = s->subject;

    s->answer = 1;
    if (t->tick != 0)
        pcm_insertIndex(&t->out, s->a);
}

static THIS void run_status(ETImessage *m)
{
    StatusMsg *s = (StatusMsg *)m;

    s->answer = pcm_getStatus(s->out);
}

static THIS void run_poll(ETImessage *m)
{
    StatusMsg *s = (StatusMsg *)m;

    soundFilePoll(s->out);
}

/* Asking for a callback starts the ticking; asking for none stops it. */
static THIS void run_setIndexCallback(ETImessage *m)
{
    SndMsg *s = (SndMsg *)m;
    SoundThread *t = (SoundThread *)(size_t)s->answer;
    soundFileSetIndexCallback(&t->out, (void *)(size_t)s->subject,
                              (void *)(size_t)s->a);
    s->b = 1;

    if (s->subject != 0) {
        StatusMsg *tick = cpp_new(sizeof *tick);

        if (tick != 0) {
            msg_ctor(&tick->base, SND_STATUS);
            tick->base.vt = &vtbl_poll;
            tick->out = &t->out;
            tick->base.vt->addRef(&tick->base);
            t->tick = tt_setTimer(t->timers, &t->base, &tick->base,
                                  SND_TICK_MS);
            tick->base.vt->release(&tick->base);
        }
        s->b = (t->tick != 0);
    } else if (t->tick != 0) {
        tt_killTimer(t->timers, t->tick);
        t->tick = 0;
    }
}

/* ---- putting one together --------------------------------------------- */

static SndMsg *newSnd(uint32_t size, uint32_t type, const MessageVtbl *vt,
                      void *subject)
{
    SndMsg *s = cpp_new(size);

    if (s == 0)
        return 0;
    msg_ctor(&s->base, type);
    s->base.vt = vt;
    s->answer = 0;
    s->subject = subject;
    return s;
}

/* Send it, take the answer, let it go. */
static int32_t askThread(SoundThread *t, SndMsg *s, int32_t on_failure)
{
    int32_t rc = on_failure;
    int16_t sent;

    if (s == 0)
        return rc;
    s->base.vt->addRef(&s->base);
    sent = qt_sendMessage(&t->base, &s->base);
    if (sent != 0) {
        rc = 1;
        if (sent == 1)
            rc = s->answer;
    }
    s->base.vt->release(&s->base);
    return rc;
}

THIS int16_t snd_open(SoundThread *t)
{
    return (int16_t)askThread(t, newSnd(sizeof(SndMsg), SND_OPEN, &vtbl_open, &t->out),
                              0);
}

THIS int32_t snd_close(SoundThread *t)
{
    return askThread(t, newSnd(sizeof(SndMsg), SND_CLOSE, &vtbl_close, t), 0);
}

THIS int32_t snd_reset(SoundThread *t)
{
    return askThread(t, newSnd(sizeof(SndMsg), SND_RESET, &vtbl_reset, &t->out), 0);
}

THIS int32_t snd_flush(SoundThread *t)
{
    return askThread(t, newSnd(sizeof(SndMsg), SND_FLUSH, &vtbl_flush, &t->out), 0);
}

THIS int32_t snd_hold(SoundThread *t, int32_t on)
{
    SndMsg *s = newSnd(sizeof(SndMsg), SND_HOLD, &vtbl_hold, &t->out);

    if (s != 0)
        s->a = on;
    return askThread(t, s, 0);
}

THIS int32_t snd_write(SoundThread *t, const int32_t *data, uint32_t n)
{
    SndMsg *s = newSnd(sizeof(SndMsg), SND_WRITE, &vtbl_write, &t->out);

    if (s != 0) {
        s->a = (int32_t)(size_t)data;
        s->b = (int32_t)n;
    }
    return askThread(t, s, 0);
}

THIS int16_t snd_setup(SoundThread *t, char *a, int32_t *b, int32_t *c,
                       int32_t *d, int32_t *e, int32_t *f, int32_t *g,
                       int32_t *h)
{
    SndMsg *s = newSnd(sizeof(SndMsg), SND_SETUP, &vtbl_setup, &t->out);

    if (s != 0) {
        s->a = (int32_t)(size_t)a;
        s->b = (int32_t)(size_t)b;
        s->c[0] = (int32_t)(size_t)c;
        s->c[1] = (int32_t)(size_t)d;
        s->c[2] = (int32_t)(size_t)e;
        s->c[3] = (int32_t)(size_t)f;
        s->c[4] = (int32_t)(size_t)g;
        s->c[5] = (int32_t)(size_t)h;
    }
    return (int16_t)askThread(t, s, 0);
}

/* Posted rather than sent: nobody is waiting for a mark to be laid down. */
THIS int32_t snd_insertIndex(SoundThread *t, int32_t index)
{
    SndMsg *s = newSnd(sizeof(SndMsg), SND_INDEX, &vtbl_index, t);
    int32_t rc = 0;

    if (s == 0)
        return rc;
    s->a = index;
    s->base.vt->addRef(&s->base);
    if (qt_postMessage(&t->base, &s->base) != 0)
        rc = 1;
    s->base.vt->release(&s->base);
    return rc;
}

/* Five is "no idea"; four is "asked and was not told". */
THIS int16_t snd_getStatus(SoundThread *t)
{
    StatusMsg *s = cpp_new(sizeof *s);
    int16_t rc = 5;
    int16_t sent;

    if (s == 0)
        return rc;
    msg_ctor(&s->base, SND_STATUS);
    s->base.vt = &vtbl_status;
    s->out = &t->out;
    s->answer = 0;

    s->base.vt->addRef(&s->base);
    sent = qt_sendMessage(&t->base, &s->base);
    if (sent != 0) {
        rc = 4;
        if (sent == 1)
            rc = s->answer;
    }
    s->base.vt->release(&s->base);
    return rc;
}

THIS int32_t snd_setIndexCallback(SoundThread *t, void *cb, void *param)
{
    SndMsg *s = newSnd(sizeof(SndMsg), SND_SETCB, &vtbl_setcb, cb);
    int32_t rc = 0;
    int16_t sent;

    if (s == 0)
        return rc;
    s->answer = (int32_t)(size_t)t;
    s->a = (int32_t)(size_t)param;
    s->b = 0;
    s->base.vt->addRef(&s->base);
    sent = qt_sendMessage(&t->base, &s->base);
    if (sent != 0) {
        rc = 1;
        if (sent == 1)
            rc = s->b;
    }
    s->base.vt->release(&s->base);
    return rc;
}

/* The three that do not go through a message at all, because the thread is
   already the one asking. */
THIS int32_t snd_closeDirect(SoundThread *t)
{
    if (t->tick != 0) {
        tt_killTimer(t->timers, t->tick);
        t->tick = 0;
    }
    return pcm_close(&t->out);
}

THIS int32_t snd_resetDirect(SoundThread *t)
{
    return pcm_reset(&t->out);
}

THIS int16_t snd_getStatusDirect(SoundThread *t)
{
    return pcm_getStatus(&t->out);
}

/* ---- the thread itself ------------------------------------------------ */

THIS SoundThread *snd_ctor(SoundThread *t, TimerThread *timers)
{
    qt_ctor(&t->base);
    t->base.vt = &vtbl_sound;
    pcm_ctor(&t->out);
    t->timers = timers;
    t->tick = 0;
    th_start(t, RAL_THREAD_PRIORITY_NORMAL);
    return t;
}

THIS void snd_dtor(SoundThread *t)
{
    t->base.vt = &vtbl_sound;
    th_terminateAndWait(t);
    if (t->tick != 0) {
        tt_killTimer(t->timers, t->tick);
        t->tick = 0;
    }
    pcm_dtor(&t->out);
    qt_dtor(&t->base);
}

THIS void *snd_destroy_thread(SoundThread *t, int32_t free_it)
{
    snd_dtor(t);
    if (free_it & 1)
        cpp_delete(t);
    return t;
}

/* ---- the tables ------------------------------------------------------- */

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

#define MSG_TABLE(name, runner) \
    const MessageVtbl name = { snd_destroy, msg_addRef, msg_release, \
        msg_getType, msg_equalsMessage, msg_equalsType, runner }

MSG_TABLE(vtbl_open, run_open);
MSG_TABLE(vtbl_close, run_close);
MSG_TABLE(vtbl_reset, run_reset);
MSG_TABLE(vtbl_flush, run_flush);
MSG_TABLE(vtbl_hold, run_hold);
MSG_TABLE(vtbl_write, run_write);
MSG_TABLE(vtbl_setup, run_setup);
MSG_TABLE(vtbl_index, run_insertIndex);
MSG_TABLE(vtbl_status, run_status);
MSG_TABLE(vtbl_poll, run_poll);
MSG_TABLE(vtbl_setcb, run_setIndexCallback);

const ThreadVtbl vtbl_mqthread = {
    (void *)qt_destroy, qt_terminate, (void *)qt_waitForExit, qt_run,
    (void *)qt_setToTerminate, (void *)qt_translateMessage
};

const ThreadVtbl vtbl_sound = {
    (void *)snd_destroy_thread, qt_terminate, (void *)qt_waitForExit, qt_run,
    (void *)qt_setToTerminate, (void *)qt_translateMessage
};

ALIAS("??_7ETImessageQueueThread@@6B@", "vtbl_mqthread");
ALIAS("??_7SoundThread@@6B@", "vtbl_sound");
ALIAS("??_7ETImsgSoundFileOpen@@6B@", "vtbl_open");
ALIAS("??_7ETImsgSoundFileClose@@6B@", "vtbl_close");
ALIAS("??_7ETImsgSoundFileReset@@6B@", "vtbl_reset");
ALIAS("??_7ETImsgSoundFileFlush@@6B@", "vtbl_flush");
ALIAS("??_7ETImsgSoundFileHold@@6B@", "vtbl_hold");
ALIAS("??_7ETImsgSoundFileWrite@@6B@", "vtbl_write");
ALIAS("??_7ETImsgSoundFileSetup@@6B@", "vtbl_setup");
ALIAS("??_7ETImsgSoundFileInsertIndex@@6B@", "vtbl_index");
ALIAS("??_7ETImsgSoundFileStatus@@6B@", "vtbl_status");
ALIAS("??_7ETImsgSoundFilePoll@@6B@", "vtbl_poll");
ALIAS("??_7ETImsgSoundFileSetIndexCallback@@6B@", "vtbl_setcb");

ALIAS("??0ETImessageQueueThread@@QAE@XZ", "qt_ctor");
ALIAS("??1ETImessageQueueThread@@UAE@XZ", "qt_dtor");
ALIAS("??_GETImessageQueueThread@@UAEPAXI@Z", "qt_destroy");
ALIAS("?waitForExit@ETImessageQueueThread@@MAEHXZ", "qt_waitForExit");
ALIAS("?setToTerminate@ETImessageQueueThread@@MAEXXZ", "qt_setToTerminate");
ALIAS("?translateMessage@ETImessageQueueThread@@MAEXPAPAVETImessage@@@Z",
      "qt_translateMessage");
ALIAS("?postMessage@ETImessageQueueThread@@QAEFPAVETImessage@@@Z",
      "qt_postMessage");
ALIAS("?sendMessage@ETImessageQueueThread@@QAEFPAVETImessage@@@Z",
      "qt_sendMessage");

ALIAS("??0SoundThread@@QAE@PAVTimerThread@@@Z", "snd_ctor");
ALIAS("??1SoundThread@@UAE@XZ", "snd_dtor");
ALIAS("??_GSoundThread@@UAEPAXI@Z", "snd_destroy_thread");
ALIAS("?open@SoundThread@@QAEFXZ", "snd_open");
ALIAS("?close@SoundThread@@QAEHXZ", "snd_close");
ALIAS("?reset@SoundThread@@QAEHXZ", "snd_reset");
ALIAS("?flush@SoundThread@@QAEHXZ", "snd_flush");
ALIAS("?hold@SoundThread@@QAEHH@Z", "snd_hold");
ALIAS("?write@SoundThread@@QAEHPBJI@Z", "snd_write");
ALIAS("?setup@SoundThread@@QAEFPADPAJ111111@Z", "snd_setup");
ALIAS("?insertIndex@SoundThread@@QAEHJ@Z", "snd_insertIndex");
ALIAS("?getStatus@SoundThread@@QAEFXZ", "snd_getStatus");
ALIAS("?setIndexCallback@SoundThread@@QAEHP6AXHPAX@Z0@Z",
      "snd_setIndexCallback");
ALIAS("?closeDirect@SoundThread@@QAEHXZ", "snd_closeDirect");
ALIAS("?resetDirect@SoundThread@@QAEHXZ", "snd_resetDirect");
ALIAS("?getStatusDirect@SoundThread@@QAEFXZ", "snd_getStatusDirect");
ALIAS("??_GETImsgSoundFileOpen@@UAEPAXI@Z", "snd_destroy");
