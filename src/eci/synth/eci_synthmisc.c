/* The thin layers.

   Most of what a caller can ask about filters and voices the synthesis
   thread does not do itself: it hands the question to the filter manager or
   the concatenative side and hands the answer back. There is nothing in
   these but the handing over, and the one thing worth saying about them is
   which ones drop the answer on the way back.

   Six of them do. Activating or deactivating a filter by number, doing so
   with a flag, deactivating all of them, and updating one all say nought
   whatever the manager said. Only the ones that take a filter by pointer,
   the two that ask a question, and the one that loads a new filter pass the
   real answer on. That is the original's and is marked below rather than
   mended, because a caller that has learned to ignore the answer would be
   surprised by a real one.

   The two pairs at the foot belong to other classes: stopping and starting a
   message queue thread, and the timer the application queue runs off. They
   are here because they are the last of their kind rather than because they
   belong together.

   Names are prefixed; the aliases carry the real ones. */

#include <stdint.h>
#include <stddef.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_objects.h"

#define ERR_BAD_ARG   (-3)
#define ERR_NO_VOICE  (-22)

/* How often the application queue is woken to look at itself, in
   milliseconds. */
#define QUEUE_TICK 30

extern THIS int32_t fm_activateById(void *m, uint32_t id, uint8_t flag)
    MANGLED("?activateFilter@FilterManager@@QAEJK_N@Z");
extern THIS int32_t fm_activateByHandle(void *m, void *f)
    MANGLED("?activateFilter@FilterManager@@QAEJPAX@Z");
extern THIS int32_t fm_deactivateById(void *m, uint32_t id, uint8_t flag)
    MANGLED("?deactivateFilter@FilterManager@@QAEJK_N@Z");
extern THIS int32_t fm_deactivateByHandle(void *m, void *f)
    MANGLED("?deactivateFilter@FilterManager@@QAEJPAX@Z");
extern THIS int32_t fm_deactivateAll(void *m)
    MANGLED("?deactivateAllFilters@FilterManager@@QAEJXZ");
extern THIS int32_t fm_deleteById(void *m, int32_t a, int32_t b)
    MANGLED("?deleteFilter@FilterManager@@QAEJJH@Z");
extern THIS int32_t fm_deleteByHandle(void *m, void *f)
    MANGLED("?deleteFilter@FilterManager@@QAEJPAX@Z");
extern THIS int32_t fm_isActiveById(void *m, uint32_t id)
    MANGLED("?isFilterActive@FilterManager@@QAEHI@Z");
extern THIS int32_t fm_isAutoload(void *m, int32_t engine, uint32_t id)
    MANGLED("?isFilterAutoload@FilterManager@@QAEHJI@Z");
extern THIS int32_t fm_loadFilter(void *m, int32_t engine, int32_t which,
                                  void **out)
    MANGLED("?loadFilter@FilterManager@@QAEJJJPAPAX@Z");
extern THIS int32_t fm_updateFilter(void *m, void *f, void *a, int32_t b,
                                    void *c, int32_t d)
    MANGLED("?updateFilter@FilterManager@@QAE?AW4ECIFilterError@@PAX0J0J@Z");
extern THIS void fm_getAvailableFilters(void *m, int32_t engine,
                                        uint32_t *ids, uint32_t *count)
    MANGLED("?getAvailableFilters@FilterManager@@QAEXJPAI0@Z");
extern THIS void fm_getFilterDescription(void *m, int32_t engine, uint32_t id,
                                         char *out)
    MANGLED("?getFilterDescription@FilterManager@@QAEXJIPAD@Z");

extern THIS int32_t cm_registerVoice(void *c, int32_t voice, void *attrib,
                                      void *extra)
    MANGLED("?registerVoice@ConcatenationManager@@QAEJHPAUECIExtendedVoiceAttrib@@PAX@Z");
extern THIS int32_t cm_unregisterVoice(void *c, int32_t voice, void *attrib,
                                        void **out)
    MANGLED("?unregisterVoice@ConcatenationManager@@QAEJHPAUECIVoiceAttrib@@PAPAX@Z");

/* ---- filters ---- */

/* Marked: the manager's answer is dropped. */
THIS int32_t stm_activateFilterById(SynthThread *t, uint32_t id)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        fm_activateById(ST_FILTERS(t), id, 0);
    return rc;
}

/* Marked: the manager's answer is dropped. */
THIS int32_t stm_activateFilterByIdFlag(SynthThread *t, uint32_t id,
                                        uint8_t flag)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        fm_activateById(ST_FILTERS(t), id, flag);
    return rc;
}

THIS int32_t stm_activateFilter(SynthThread *t, void *f)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        rc = fm_activateByHandle(ST_FILTERS(t), f);
    return rc;
}

/* Marked: the manager's answer is dropped. */
THIS int32_t stm_deactivateFilterById(SynthThread *t, uint32_t id)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        fm_deactivateById(ST_FILTERS(t), id, 0);
    return rc;
}

/* Marked: the manager's answer is dropped. */
THIS int32_t stm_deactivateFilterByIdFlag(SynthThread *t, uint32_t id,
                                          uint8_t flag)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        fm_deactivateById(ST_FILTERS(t), id, flag);
    return rc;
}

THIS int32_t stm_deactivateFilter(SynthThread *t, void *f)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        rc = fm_deactivateByHandle(ST_FILTERS(t), f);
    return rc;
}

/* Marked: the manager's answer is dropped. */
THIS int32_t stm_deactivateAllFilters(SynthThread *t)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        fm_deactivateAll(ST_FILTERS(t));
    return rc;
}

THIS int32_t stm_deleteFilterByNumbers(SynthThread *t, int32_t a, int32_t b)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        rc = fm_deleteById(ST_FILTERS(t), a, b);
    return rc;
}

THIS int32_t stm_deleteFilter(SynthThread *t, void *f)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        rc = fm_deleteByHandle(ST_FILTERS(t), f);
    return rc;
}

THIS int32_t stm_isFilterActive(SynthThread *t, uint32_t id)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        rc = fm_isActiveById(ST_FILTERS(t), id);
    return rc;
}

THIS int32_t stm_isFilterAutoload(SynthThread *t, int32_t engine, uint32_t id)
{
    int32_t rc = 0;

    if (ST_FILTERS(t))
        rc = fm_isAutoload(ST_FILTERS(t), engine, id);
    return rc;
}

/* Marked: the manager's answer is dropped. */
THIS int32_t stm_updateFilter(SynthThread *t, void *f, void *a, int32_t b,
                              void *c, int32_t d, int32_t e)
{
    int32_t rc = 0;

    (void)e;
    if (ST_FILTERS(t))
        fm_updateFilter(ST_FILTERS(t), f, a, b, c, d);
    return rc;
}

/* The one that loads a filter also remembers it as the one in play. It reads
   back through the caller's own pointer to do so, whether or not there was a
   manager to write anything into it. */
THIS int32_t stm_newFilter(SynthThread *t, int32_t engine, int32_t which,
                           void **out)
{
    int32_t rc = ERR_BAD_ARG;

    if (ST_FILTERS(t))
        rc = fm_loadFilter(ST_FILTERS(t), engine, which, out);
    ST_FILTER(t) = *out;
    return rc;
}

THIS void stm_getAvailableFilters(SynthThread *t, int32_t engine,
                                  uint32_t *ids, uint32_t *count)
{
    if (ST_FILTERS(t))
        fm_getAvailableFilters(ST_FILTERS(t), engine, ids, count);
}

THIS void stm_getFilterDescription(SynthThread *t, int32_t engine,
                                   uint32_t id, char *out)
{
    if (ST_FILTERS(t))
        fm_getFilterDescription(ST_FILTERS(t), engine, id, out);
}

/* ---- voices ---- */

/* Voices belong to the concatenative side, so without one there is nothing
   to register with and the caller is told so. */
THIS int32_t stm_registerVoice(SynthThread *t, int32_t voice, void *attrib,
                               void *extra)
{
    int32_t rc = ERR_NO_VOICE;

    if (ST_CONCAT(t))
        rc = cm_registerVoice(ST_CONCAT(t), voice, attrib, extra);
    return rc;
}

THIS int32_t stm_unregisterVoice(SynthThread *t, int32_t voice, void *attrib,
                                 void **out)
{
    int32_t rc = ERR_NO_VOICE;

    if (ST_CONCAT(t))
        rc = cm_unregisterVoice(ST_CONCAT(t), voice, attrib, out);
    return rc;
}

/* ---- stopping and starting a message queue thread ---- */

typedef ETImessageQueueThread QueueThread;
typedef ETImessageQueue MsgQueue;

struct QueueVtbl {
    THIS int16_t (*sendMessage)(MsgQueue *, void *, int32_t, void *, void *);
    THIS int16_t (*postMessage)(MsgQueue *, void *, int32_t, void *, void *);
    THIS int16_t (*popMessage)(MsgQueue *, void **, int32_t, void *);
    THIS void    (*suspend)(MsgQueue *);
    THIS void    (*resume)(MsgQueue *);
};

#define QT_QUEUE(t)    (&(t)->queue)
#define QT_TURN(t)     ((void *)(t)->turn)
#define QT_QUITTING(t) ((void *)(t)->gate)

/* The thread is running. */
#define THREAD_RUNNING 1

extern THIS int32_t sy_eventWait(void *e, int32_t ms)
    MANGLED("?wait@ETIEvent@@QAEHJ@Z");
extern THIS int32_t th_getStatus(void *t)
    MANGLED("?getStatus@ETIThread@@QAE?AW4TStatus@1@XZ");
extern THIS int32_t th_shouldTerminate(const void *t)
    MANGLED("?shouldTerminate@ETIThread@@QBEHXZ");

/* Stop the thread taking anything else off its queue, and wait until the one
   it is on has been finished with. A thread that is not running, or is on
   its way out, is left alone. */
THIS int16_t stm_qtSuspend(QueueThread *t)
{
    int16_t ok = 0;

    sy_eventWait(QT_QUITTING(t), -1);
    if (th_getStatus(t) == THREAD_RUNNING && !th_shouldTerminate(t)) {
        MsgQueue *q = QT_QUEUE(t);

        q->vt->suspend(q);
        sy_eventWait(QT_TURN(t), -1);
        ok = 1;
    }
    return ok;
}

/* And let it go on again. Nothing to wait for here: the thread finds out by
   itself. */
THIS int16_t stm_qtResume(QueueThread *t)
{
    int16_t ok = 0;

    sy_eventWait(QT_QUITTING(t), -1);
    if (th_getStatus(t) == THREAD_RUNNING && !th_shouldTerminate(t)) {
        MsgQueue *q = QT_QUEUE(t);

        q->vt->resume(q);
        ok = 1;
    }
    return ok;
}

/* ---- the application queue's timer ---- */

/* Fields of the application queue this pair reaches. */
#define AQ_PARAM(q)    (((ETIappMessageQueue *)(q))->cb_param)
#define AQ_STOPPING(q) (((ETIappMessageQueue *)(q))->stopping)
#define AQ_HELD(q)     (((ETIappMessageQueue *)(q))->held)
#define AQ_WINDOW(q)   (((ETIappMessageQueue *)(q))->win)
#define AQ_MESSAGE(q)  (((ETIappMessageQueue *)(q))->post_flag)

#ifdef _WIN32
__attribute__((dllimport, stdcall)) unsigned SetTimer(void *, unsigned,
                                                      unsigned, void *);
__attribute__((dllimport, stdcall)) int KillTimer(void *, unsigned);
__attribute__((dllimport, stdcall)) int PostMessageA(void *, unsigned,
                                                     void *, void *);
#else
/* Off Windows there is no window to hang a timer on. Whatever drives the
   queue there drives it another way, and this pair does nothing. */
static unsigned SetTimer(void *a, unsigned b, unsigned c, void *d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }
static int KillTimer(void *a, unsigned b) { (void)a; (void)b; return 0; }
static int PostMessageA(void *a, unsigned b, void *c, void *d)
{ (void)a; (void)b; (void)c; (void)d; return 0; }
#endif

/* Start or stop the tick that wakes the queue to look at itself. */
THIS void stm_doTimer(void *q, int32_t on)
{
    if (on)
        SetTimer(AQ_WINDOW(q), (unsigned)(size_t)AQ_PARAM(q), QUEUE_TICK, 0);
    else
        KillTimer(AQ_WINDOW(q), (unsigned)(size_t)AQ_PARAM(q));
}

/* Hold the queue, or let it go. Holding it stops the tick; letting it go
   starts the tick if there is something waiting and, either way, gives the
   queue one immediate nudge.

   The field the tick is decided by is the same one the queue uses to hold a
   message its caller asked to see later, so the tick runs exactly while
   there is something held. */
THIS void stm_pauseMessageQueue(void *q, int32_t how)
{
    AQ_STOPPING(q) = how;
    if (how && AQ_HELD(q)) {
        stm_doTimer(q, 0);
        return;
    }
    if (AQ_HELD(q))
        stm_doTimer(q, 1);
    PostMessageA(AQ_WINDOW(q), (unsigned)AQ_MESSAGE(q), AQ_PARAM(q), 0);
}

ALIAS("?activateFilter@SynthThread@@QAEJK@Z", "stm_activateFilterById");
ALIAS("?activateFilter@SynthThread@@QAEJK_N@Z",
      "stm_activateFilterByIdFlag");
ALIAS("?activateFilter@SynthThread@@QAEJPAX@Z", "stm_activateFilter");
ALIAS("?deactivateFilter@SynthThread@@QAEJK@Z", "stm_deactivateFilterById");
ALIAS("?deactivateFilter@SynthThread@@QAEJK_N@Z",
      "stm_deactivateFilterByIdFlag");
ALIAS("?deactivateFilter@SynthThread@@QAEJPAX@Z", "stm_deactivateFilter");
ALIAS("?deactivateAllFilters@SynthThread@@QAEJXZ",
      "stm_deactivateAllFilters");
ALIAS("?deleteFilter@SynthThread@@QAEJJJ@Z", "stm_deleteFilterByNumbers");
ALIAS("?deleteFilter@SynthThread@@QAEJPAX@Z", "stm_deleteFilter");
ALIAS("?isFilterActive@SynthThread@@QAEHI@Z", "stm_isFilterActive");
ALIAS("?isFilterAutoload@SynthThread@@QAEHJI@Z", "stm_isFilterAutoload");
ALIAS("?updateFilter@SynthThread@@QAEJPAX0J0JJ@Z", "stm_updateFilter");
ALIAS("?newFilter@SynthThread@@QAEJJJPAPAX@Z", "stm_newFilter");
ALIAS("?getAvailableFilters@SynthThread@@QAEXJPAI0@Z",
      "stm_getAvailableFilters");
ALIAS("?getFilterDescription@SynthThread@@QAEXJIPAD@Z",
      "stm_getFilterDescription");
ALIAS("?registerVoice@SynthThread@@QAEJHPAUECIExtendedVoiceAttrib@@PAX@Z",
      "stm_registerVoice");
ALIAS("?unregisterVoice@SynthThread@@QAEJHPAUECIVoiceAttrib@@PAPAX@Z",
      "stm_unregisterVoice");
ALIAS("?suspend@ETImessageQueueThread@@QAEFXZ", "stm_qtSuspend");
ALIAS("?resume@ETImessageQueueThread@@QAEFXZ", "stm_qtResume");
ALIAS("?doTimer@ETIappMessageQueue@@AAEXH@Z", "stm_doTimer");
ALIAS("?pauseMessageQueue@ETIappMessageQueue@@QAEXH@Z",
      "stm_pauseMessageQueue");
