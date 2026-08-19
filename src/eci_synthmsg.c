/* What the world says to the synthesis thread.

   Every public request on SynthThread -- speak this text, change the speed,
   drop an index mark in -- is a message. The caller takes the thread's lock,
   builds the message, posts it and returns; the thread picks it up later and
   calls the matching Run method. This file is the near half of that: the
   twenty message classes and the twenty-one senders. The far half is in
   eci_synthrun.c.

   The senders look alike but are not interchangeable, and the differences
   are the whole reason this file is written out longhand rather than
   generated. Each one reserves the next slot in the application queue before
   posting and writes it back only if the post was actually taken, so a
   refused message leaves the numbering untouched. Six of them start the
   sound device first and shut it down again if the post then fails. Two
   count the text they carry rather than counting themselves. Seven throw the
   message away when the post fails while the other fourteen only drop their
   reference. Getting any of that wrong is not a crash and not a difference
   in the audio: it shows up much later as an index mark reported against the
   wrong word.

   The tables of virtual functions at the end are read off the original's own
   .rdata, slot for slot. */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "evv_arena.h"
#include "eci_objects.h"

/* What a post can answer. Only a plain "queued" commits the numbering. */
#define POST_FAILED  0
#define POST_QUEUED  1

/* Answers a sender gives back. */
#define OK              0
#define ERR_FAILED     (-2)
#define ERR_NO_DEVICE  (-13)
#define ERR_BAD_LANG   (-14)
#define ERR_NO_SOUND   (-16)

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

/* The kinds. The original numbers them from 0x7d0 in the order they were
   written, not in any order that means anything, and three pairs share a
   number: a change asked for by name and the same change asked for by
   number are the same kind of message with different tables. */
#define MSG_ADD_TEXT        0x7d0
#define MSG_CHANGE_LANGUAGE 0x7d1
#define MSG_INSERT_INDEX    0x7d2
#define MSG_PHONEME_INDEX   0x7d3
#define MSG_BLOCK           0x7d4
#define MSG_SYNTHESIZE      0x7d5
#define MSG_ADD_PARAM       0x7d6
#define MSG_CHANGE_ROM      0x7d7
#define MSG_CHANGE_FILTER   0x7d8
#define MSG_CHANGE_VOLUME   0x7d9
#define MSG_CHANGE_VOICE    0x7da
#define MSG_CHANGE_SPEED    0x7db
#define MSG_CHANGE_PITCH    0x7dc
#define MSG_CHANGE_FLUCT    0x7dd
#define MSG_CHANGE_EMPHASIS 0x7de
#define MSG_STRING_INDEX    0x7df
#define MSG_AUDIO_INDEX     0x7e0

/* The shapes. In nearly all of them the thread the message is for sits at
   +0x20 and the application-queue slot it claimed comes last. */

typedef struct {            /* 0x24, block, which carries nothing else */
    ETImessage base;
    SynthThread *thread;    /* +0x20 */
} MsgPlain;

typedef struct {            /* 0x28 */
    ETImessage base;
    SynthThread *thread;    /* +0x20 */
    int32_t seq;            /* +0x24 */
} MsgSeq;

typedef struct {            /* 0x2c */
    ETImessage base;
    SynthThread *thread;    /* +0x20 */
    int32_t value;          /* +0x24 */
    int32_t seq;            /* +0x28 */
} MsgValue;

typedef struct {            /* 0x2c, the two that name an index mark */
    ETImessage base;
    SynthThread *thread;    /* +0x20 */
    char    *text;          /* +0x24, strdup, so free and not delete */
    int32_t  seq;           /* +0x28 */
} MsgString;

typedef struct {            /* 0x30 */
    ETImessage base;
    SynthThread *thread;    /* +0x20 */
    int32_t a;              /* +0x24 */
    int32_t b;              /* +0x28 */
    int32_t seq;            /* +0x2c */
} MsgPair;

typedef struct {            /* 0x34 */
    ETImessage base;
    SynthThread *thread;    /* +0x20 */
    uint32_t which;         /* +0x24 */
    uint32_t value;         /* +0x28 */
    int32_t  seq;           /* +0x2c */
    uint8_t  flag;          /* +0x30 */
} MsgFilter;

typedef struct {            /* 0x3c */
    ETImessage base;
    SynthThread *thread;    /* +0x20 */
    LangIdentifier lang;    /* +0x24 */
    int32_t seq;            /* +0x38 */
} MsgLanguage;

/* The two that carry text put the thread third instead. That is not a tidy
   layout; it is the order their own constructors write, and their run
   methods read it back from there. */
typedef struct {            /* 0x34 */
    ETImessage base;
    char       *text;       /* +0x20, operator new, not strdup */
    uint32_t    len;        /* +0x24 */
    SynthThread *thread;    /* +0x28 */
    int32_t     seq;        /* +0x2c */
    int32_t     last;       /* +0x30, addText only */
} MsgText;

/* The offsets this file reaches into SynthThread by, and the count the
   application queue keeps, are in eci_synththread.h. */

extern THIS ETImessage *msg_ctor(ETImessage *m, uint32_t type)
    MANGLED("??0ETImessage@@QAE@K@Z");
extern const uint32_t sem_bytes;
extern THIS void *sy_semCtor(void *s, int32_t held)
    MANGLED("??0Semaphore@@QAE@J@Z");

/* The six base-class slots every table below shares. */
extern THIS uint32_t msg_addRef(ETImessage *m)
    MANGLED("?addRef@ETImessage@@UAEKXZ");
extern THIS uint32_t msg_release(ETImessage *m)
    MANGLED("?release@ETImessage@@UAEKXZ");
extern THIS uint32_t msg_getType(const ETImessage *m)
    MANGLED("?getMessageType@ETImessage@@UBEKXZ");
extern THIS int32_t msg_equalsMessage(ETImessage *m, ETImessage *other)
    MANGLED("?equals@ETImessage@@UAEHPAV1@@Z");
extern THIS int32_t msg_equalsType(ETImessage *m, uint32_t type)
    MANGLED("?equals@ETImessage@@UAEHK@Z");
extern const MessageVtbl vtbl_message MANGLED("??_7ETImessage@@6B@");

extern THIS int16_t qt_postMessage(SynthThread *t, ETImessage *m)
    MANGLED("?postMessage@ETImessageQueueThread@@QAEFPAVETImessage@@@Z");

extern THIS void lang_setString(LangIdentifier *l)
    MANGLED("?setString@LangIdentifier@@AAEXXZ");

extern THIS int16_t snd_getStatus(void *s)
    MANGLED("?getStatus@SoundThread@@QAEFXZ");
extern THIS int16_t snd_open(void *s)
    MANGLED("?open@SoundThread@@QAEFXZ");
extern THIS int32_t snd_close(void *s)
    MANGLED("?close@SoundThread@@QAEHXZ");
extern THIS int32_t snd_setIndexCallback(void *s,
                                         void (*cb)(int32_t, void *),
                                         void *param)
    MANGLED("?setIndexCallback@SoundThread@@QAEHP6AXHPAX@Z0@Z");
extern void stb_staticDeviceIndexCallback(int32_t index, void *param)
    MANGLED("?staticDeviceIndexCallback@SynthThread@@CAXHPAX@Z");

/* The far half. These still belong to the original. */
extern THIS void addTextRun(SynthThread *t, char *text, uint32_t len,
                            int32_t seq, int32_t last)
    MANGLED("?addTextRun@SynthThread@@AAEXPADKJH@Z");
extern THIS void addParamRun(SynthThread *t, char *text, uint32_t len,
                             int32_t seq)
    MANGLED("?addParamRun@SynthThread@@AAEXPADKJ@Z");
extern THIS void blockRun(SynthThread *t)
    MANGLED("?blockRun@SynthThread@@AAEXXZ");
extern THIS void synthesizeRun(SynthThread *t, int32_t seq)
    MANGLED("?synthesizeRun@SynthThread@@AAEXJ@Z");
extern THIS void changeLanguageRun(SynthThread *t, LangIdentifier *l,
                                   int32_t seq)
    MANGLED("?changeLanguageRun@SynthThread@@AAEXPAVLangIdentifier@@J@Z");
extern THIS void changeRomParamRun(SynthThread *t, int32_t a, int32_t b,
                                   int32_t seq)
    MANGLED("?changeRomParamRun@SynthThread@@AAEXJJJ@Z");
extern THIS void changeFilterRun(SynthThread *t, uint32_t which,
                                 uint32_t value, int32_t seq, uint8_t flag)
    MANGLED("?changeFilterRun@SynthThread@@QAEXKKJ_N@Z");
extern THIS void changeVolumeRun(SynthThread *t, int32_t v, int32_t seq)
    MANGLED("?changeVolumeRun@SynthThread@@QAEXJJ@Z");
extern THIS void changeSpeedRun(SynthThread *t, int32_t v, int32_t seq)
    MANGLED("?changeSpeedRun@SynthThread@@QAEXJJ@Z");
extern THIS void changeSpeedStringRun(SynthThread *t, int32_t v, int32_t seq)
    MANGLED("?changeSpeedStringRun@SynthThread@@QAEXJJ@Z");
extern THIS void changePitchRun(SynthThread *t, int32_t v, int32_t seq)
    MANGLED("?changePitchRun@SynthThread@@QAEXJJ@Z");
extern THIS void changePitchStringRun(SynthThread *t, int32_t v, int32_t seq)
    MANGLED("?changePitchStringRun@SynthThread@@QAEXJJ@Z");
extern THIS void changeFluctuationRun(SynthThread *t, int32_t v, int32_t seq)
    MANGLED("?changeFluctuationRun@SynthThread@@QAEXJJ@Z");
extern THIS void changeFluctuationStringRun(SynthThread *t, int32_t v,
                                            int32_t seq)
    MANGLED("?changeFluctuationStringRun@SynthThread@@QAEXJJ@Z");
extern THIS void changeVoiceRun(SynthThread *t, int32_t v, int32_t seq)
    MANGLED("?changeVoiceRun@SynthThread@@QAEXJJ@Z");
extern THIS void changeEmphasisRun(SynthThread *t, int32_t seq)
    MANGLED("?changeEmphasisRun@SynthThread@@QAEXJ@Z");
extern THIS void setPhonemeIndiciesRun(SynthThread *t, int32_t v, int32_t seq)
    MANGLED("?setPhonemeIndiciesRun@SynthThread@@AAEXJJ@Z");
extern THIS void insertIndexRun(SynthThread *t, int32_t v, int32_t seq)
    MANGLED("?insertIndexRun@SynthThread@@AAEXJJ@Z");
extern THIS void insertStringIndexRun(SynthThread *t, char *s, int32_t seq)
    MANGLED("?insertStringIndexRun@SynthThread@@QAEXPADJ@Z");
extern THIS void insertAudioIndexRun(SynthThread *t, char *s, int32_t seq)
    MANGLED("?insertAudioIndexRun@SynthThread@@QAEXPADJ@Z");

/* The tables are written out at the foot of the file; the senders and the
   destructors both need to name them before then. */
extern const MessageVtbl vt_addText, vt_addParam, vt_block, vt_synthesize;
extern const MessageVtbl vt_changeLanguage, vt_changeRomParam;
extern const MessageVtbl vt_changeFilter, vt_changeVolume;
extern const MessageVtbl vt_changeSpeed, vt_changeSpeedString;
extern const MessageVtbl vt_changePitch, vt_changePitchString;
extern const MessageVtbl vt_changeFluctuation, vt_changeFluctuationString;
extern const MessageVtbl vt_changeVoice, vt_changeEmphasis;
extern const MessageVtbl vt_setPhonemeIndicies, vt_insertIndex;
extern const MessageVtbl vt_insertStringIndex, vt_insertAudioIndex;

/* ---- what each kind does when the thread gets to it ---- */

THIS void run_addText(ETImessage *m)
{
    MsgText *x = (MsgText *)m;
    addTextRun(x->thread, x->text, x->len, x->seq, x->last);
}

THIS void run_addParam(ETImessage *m)
{
    MsgText *x = (MsgText *)m;
    addParamRun(x->thread, x->text, x->len, x->seq);
}

THIS void run_block(ETImessage *m)
{
    blockRun(((MsgPlain *)m)->thread);
}

THIS void run_synthesize(ETImessage *m)
{
    MsgSeq *x = (MsgSeq *)m;
    synthesizeRun(x->thread, x->seq);
}

THIS void run_changeEmphasis(ETImessage *m)
{
    MsgSeq *x = (MsgSeq *)m;
    changeEmphasisRun(x->thread, x->seq);
}

THIS void run_changeLanguage(ETImessage *m)
{
    MsgLanguage *x = (MsgLanguage *)m;
    changeLanguageRun(x->thread, &x->lang, x->seq);
}

THIS void run_changeRomParam(ETImessage *m)
{
    MsgPair *x = (MsgPair *)m;
    changeRomParamRun(x->thread, x->a, x->b, x->seq);
}

THIS void run_changeFilter(ETImessage *m)
{
    MsgFilter *x = (MsgFilter *)m;
    changeFilterRun(x->thread, x->which, x->value, x->seq, x->flag);
}

THIS void run_changeVolume(ETImessage *m)
{
    MsgValue *x = (MsgValue *)m;
    changeVolumeRun(x->thread, x->value, x->seq);
}

THIS void run_changeSpeed(ETImessage *m)
{
    MsgValue *x = (MsgValue *)m;
    changeSpeedRun(x->thread, x->value, x->seq);
}

THIS void run_changeSpeedString(ETImessage *m)
{
    MsgValue *x = (MsgValue *)m;
    changeSpeedStringRun(x->thread, x->value, x->seq);
}

THIS void run_changePitch(ETImessage *m)
{
    MsgValue *x = (MsgValue *)m;
    changePitchRun(x->thread, x->value, x->seq);
}

THIS void run_changePitchString(ETImessage *m)
{
    MsgValue *x = (MsgValue *)m;
    changePitchStringRun(x->thread, x->value, x->seq);
}

THIS void run_changeFluctuation(ETImessage *m)
{
    MsgValue *x = (MsgValue *)m;
    changeFluctuationRun(x->thread, x->value, x->seq);
}

THIS void run_changeFluctuationString(ETImessage *m)
{
    MsgValue *x = (MsgValue *)m;
    changeFluctuationStringRun(x->thread, x->value, x->seq);
}

THIS void run_changeVoice(ETImessage *m)
{
    MsgValue *x = (MsgValue *)m;
    changeVoiceRun(x->thread, x->value, x->seq);
}

THIS void run_setPhonemeIndicies(ETImessage *m)
{
    MsgValue *x = (MsgValue *)m;
    setPhonemeIndiciesRun(x->thread, x->value, x->seq);
}

THIS void run_insertIndex(ETImessage *m)
{
    MsgValue *x = (MsgValue *)m;
    insertIndexRun(x->thread, x->value, x->seq);
}

THIS void run_insertStringIndex(ETImessage *m)
{
    MsgString *x = (MsgString *)m;
    insertStringIndexRun(x->thread, x->text, x->seq);
}

THIS void run_insertAudioIndex(ETImessage *m)
{
    MsgString *x = (MsgString *)m;
    insertAudioIndexRun(x->thread, x->text, x->seq);
}

/* ---- tearing one down ---- */

/* Most kinds own nothing, so all there is to undo is the base class: put the
   base table back before running its destructor, then free if asked. */
THIS void *destroy_plain(ETImessage *m, int32_t free_it)
{
    m->vt = &vtbl_message;
    sy_mutexDtor(m->lock);
    if (free_it & 1)
        cpp_delete(m);
    return m;
}

/* Each of the four that own memory sets its own table back first, which
   matters only if the destructor were to make a virtual call, and none of
   them do. It is written out because the original writes it. */
THIS void dtor_addText(ETImessage *m)
{
    MsgText *x = (MsgText *)m;

    m->vt = &vt_addText;
    if (x->text) {
        cpp_delete(x->text);
        x->text = 0;
    }
    m->vt = &vtbl_message;
    sy_mutexDtor(m->lock);
}

THIS void *destroy_addText(ETImessage *m, int32_t free_it)
{
    dtor_addText(m);
    if (free_it & 1)
        cpp_delete(m);
    return m;
}

THIS void dtor_addParam(ETImessage *m)
{
    MsgText *x = (MsgText *)m;

    m->vt = &vt_addParam;
    if (x->text) {
        cpp_delete(x->text);
        x->text = 0;
    }
    m->vt = &vtbl_message;
    sy_mutexDtor(m->lock);
}

THIS void *destroy_addParam(ETImessage *m, int32_t free_it)
{
    dtor_addParam(m);
    if (free_it & 1)
        cpp_delete(m);
    return m;
}

/* The two index-mark kinds copied their name with strdup, so it goes back to
   free rather than to operator delete, and neither of them clears the
   pointer afterwards the way the text messages do. */
THIS void dtor_insertStringIndex(ETImessage *m)
{
    MsgString *x = (MsgString *)m;

    m->vt = &vt_insertStringIndex;
    if (x->text)
        free(x->text);
    m->vt = &vtbl_message;
    sy_mutexDtor(m->lock);
}

THIS void *destroy_insertStringIndex(ETImessage *m, int32_t free_it)
{
    dtor_insertStringIndex(m);
    if (free_it & 1)
        cpp_delete(m);
    return m;
}

THIS void dtor_insertAudioIndex(ETImessage *m)
{
    MsgString *x = (MsgString *)m;

    m->vt = &vt_insertAudioIndex;
    if (x->text)
        free(x->text);
    m->vt = &vtbl_message;
    sy_mutexDtor(m->lock);
}

THIS void *destroy_insertAudioIndex(ETImessage *m, int32_t free_it)
{
    dtor_insertAudioIndex(m);
    if (free_it & 1)
        cpp_delete(m);
    return m;
}

/* ---- the two that build themselves ---- */

/* Both copy the caller's bytes and hang a terminator off the end, so the run
   method can treat what it was given as a string even though the caller
   counted it out as a length. A copy that could not be made leaves text null
   and the sender throws the message away rather than posting it. */
THIS MsgText *ctor_addText(MsgText *x, SynthThread *t, char *text,
                           uint32_t len, int32_t seq, int32_t last)
{
    msg_ctor(&x->base, MSG_ADD_TEXT);
    x->base.vt = &vt_addText;
    x->text = 0;
    x->len = len;
    x->thread = t;
    x->seq = seq;
    x->last = last;
    x->text = (char *)cpp_new(len + 1);
    if (x->text) {
        memcpy(x->text, text, len);
        x->text[len] = 0;
    }
    return x;
}

THIS MsgText *ctor_addParam(MsgText *x, SynthThread *t, char *text,
                            uint32_t len, int32_t seq)
{
    msg_ctor(&x->base, MSG_ADD_PARAM);
    x->base.vt = &vt_addParam;
    x->text = 0;
    x->len = len;
    x->thread = t;
    x->seq = seq;
    x->text = (char *)cpp_new(len + 1);
    if (x->text) {
        memcpy(x->text, text, len);
        x->text[len] = 0;
    }
    return x;
}

/* ---- the device ---- */

/* Anything that will make sound has to find the device open first. The
   caller is told whether this call is the one that opened it, because if the
   post then fails it is the one that has to close it again. */
THIS int32_t stg_startUpSound(SynthThread *t, int32_t *opened)
{
    int32_t rc = ERR_NO_SOUND;
    int16_t status, r;

    *opened = 0;
    if (ST_SILENT(t))
        return rc;

    rc = OK;
    status = snd_getStatus(ST_SOUND(t));
    if (status != 0)
        return status == 5 ? ERR_FAILED : rc;

    r = snd_open(ST_SOUND(t));
    if (r == 1) {
        if (snd_setIndexCallback(ST_SOUND(t), stb_staticDeviceIndexCallback, t)) {
            *opened = 1;
        } else {
            rc = ERR_FAILED;
            snd_close(ST_SOUND(t));
        }
    } else if (r == 3) {
        rc = ERR_NO_DEVICE;
    } else if (r == 0) {
        rc = ERR_FAILED;
    }
    return rc;
}

/* ---- posting ---- */

/* The tail every sender shares. The message is already built; take a
   reference so the thread cannot free it out from under us, post it, and on
   a plain "queued" commit the slot it claimed. A refused or failed post
   leaves the application queue's count where it was.

   The count of outstanding work goes up by units rather than by one, because
   a text message stands for as many characters as it carries.

   Whether a failed post drops the reference or destroys the message outright
   is not ours to choose: seven of the senders destroy and the rest release,
   and the two are only interchangeable if the reference count is exactly
   one, which is not something this layer can promise. */
static int32_t postAndCommit(SynthThread *t, ETImessage *m, int32_t seq,
                             int32_t units, int drop_on_failure)
{
    int32_t rc = ERR_FAILED;
    int16_t sent;

    m->vt->addRef(m);
    sent = qt_postMessage(t, m);
    if (sent != POST_FAILED) {
        rc = OK;
        if (sent == POST_QUEUED) {
            APP_POSTED(ST_APP(t)) = seq;
            ST_PENDING(t) += units;
            ST_POSTED(t) = 1;
        }
        m->vt->release(m);
    } else if (drop_on_failure) {
        m->vt->destroy(m, 1);
    } else {
        m->vt->release(m);
    }
    return rc;
}

/* The next slot in the application queue, claimed but not yet committed. */
static int32_t claim(SynthThread *t)
{
    return APP_POSTED(ST_APP(t)) + 1;
}

/* ---- the senders ---- */

/* The plainest shape: one value and the slot it claimed, no device, and the
   reference merely dropped if the post is refused. */
static int32_t sendValue(SynthThread *t, uint32_t type,
                         const MessageVtbl *vt, int32_t value)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_FAILED;
    int32_t seq;
    MsgValue *m;

    sy_mutexWait(lock, -1);
    seq = claim(t);
    m = (MsgValue *)cpp_new(sizeof(MsgValue));
    if (m) {
        msg_ctor(&m->base, type);
        m->base.vt = vt;
        m->thread = t;
        m->value = value;
        m->seq = seq;
        rc = postAndCommit(t, &m->base, seq, 1, 0);
    }
    sy_mutexRelease(lock);
    return rc;
}

/* The same, but the device has to be running first and has to be stopped
   again if the message never got on the queue. */
static int32_t sendValueWithSound(SynthThread *t, uint32_t type,
                                  const MessageVtbl *vt, int32_t value)
{
    void *lock = ST_LOCK(t);
    int32_t rc = OK, opened = 0, seq;
    MsgValue *m;

    sy_mutexWait(lock, -1);
    if (ST_SOUND(t))
        rc = stg_startUpSound(t, &opened);
    if (rc == OK) {
        seq = claim(t);
        rc = ERR_FAILED;
        m = (MsgValue *)cpp_new(sizeof(MsgValue));
        if (m) {
            msg_ctor(&m->base, type);
            m->base.vt = vt;
            m->thread = t;
            m->value = value;
            m->seq = seq;
            rc = postAndCommit(t, &m->base, seq, 1, 1);
        }
        if (rc != OK && opened)
            snd_close(ST_SOUND(t));
    }
    sy_mutexRelease(lock);
    return rc;
}

/* An index mark named by a string. The name is copied because the caller is
   free to reuse its buffer the moment this returns. */
static int32_t sendString(SynthThread *t, uint32_t type,
                          const MessageVtbl *vt, char *text)
{
    void *lock = ST_LOCK(t);
    int32_t rc = OK, opened = 0, seq;
    MsgString *m;

    sy_mutexWait(lock, -1);
    if (ST_SOUND(t))
        rc = stg_startUpSound(t, &opened);
    if (rc == OK) {
        seq = claim(t);
        rc = ERR_FAILED;
        m = (MsgString *)cpp_new(sizeof(MsgString));
        if (m) {
            msg_ctor(&m->base, type);
            m->base.vt = vt;
            m->thread = t;
            m->text = 0;
            m->seq = seq;
            m->text = strdup(text);
            rc = postAndCommit(t, &m->base, seq, 1, 1);
        }
        if (rc != OK && opened)
            snd_close(ST_SOUND(t));
    }
    sy_mutexRelease(lock);
    return rc;
}

/* Text to speak. The length is also what the pending count goes up by, so
   the layer above can tell how much is still unspoken. */
THIS int32_t st_addText(SynthThread *t, char *text, uint32_t len,
                        int32_t last)
{
    void *lock = ST_LOCK(t);
    int32_t rc = OK, opened = 0, seq = 0;
    MsgText *m;

    sy_mutexWait(lock, -1);
    if (ST_SOUND(t))
        rc = stg_startUpSound(t, &opened);
    if (rc == OK) {
        seq = claim(t);
        rc = ERR_FAILED;
        m = (MsgText *)cpp_new(sizeof(MsgText));
        if (m)
            ctor_addText(m, t, text, len, seq, last);
        if (m && m->text)
            rc = postAndCommit(t, &m->base, seq, (int32_t)len, 1);
        if (rc != OK && opened)
            snd_close(ST_SOUND(t));
    }
    sy_mutexRelease(lock);
    return rc;
}

THIS int32_t st_addParam(SynthThread *t, char *text, uint32_t len)
{
    void *lock = ST_LOCK(t);
    int32_t rc = OK, opened = 0, seq = 0;
    MsgText *m;

    sy_mutexWait(lock, -1);
    if (ST_SOUND(t))
        rc = stg_startUpSound(t, &opened);
    if (rc == OK) {
        seq = claim(t);
        rc = ERR_FAILED;
        m = (MsgText *)cpp_new(sizeof(MsgText));
        if (m)
            ctor_addParam(m, t, text, len, seq);
        if (m && m->text)
            rc = postAndCommit(t, &m->base, seq, (int32_t)len, 1);
        if (rc != OK && opened)
            snd_close(ST_SOUND(t));
    }
    sy_mutexRelease(lock);
    return rc;
}

/* Stand still until the thread has caught up. The semaphore the thread
   signals is made on the first call and kept afterwards. Nothing is claimed
   in the application queue: this asks a question rather than adding anything
   the application will be told about. */
THIS int32_t st_block(SynthThread *t)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_FAILED;
    MsgPlain *m;

    sy_mutexWait(lock, -1);
    if (!ST_BLOCKER(t)) {
        void *s = cpp_new(sem_bytes);
        if (s)
            sy_semCtor(s, 0);
        ST_BLOCKER(t) = s;
    }
    if (ST_BLOCKER(t)) {
        m = (MsgPlain *)cpp_new(sizeof(MsgPlain));
        if (m) {
            msg_ctor(&m->base, MSG_BLOCK);
            m->base.vt = &vt_block;
            m->thread = t;
            m->base.vt->addRef(&m->base);
            if (qt_postMessage(t, &m->base) != POST_FAILED) {
                rc = OK;
                m->base.vt->release(&m->base);
            } else {
                m->base.vt->destroy(&m->base, 1);
            }
        }
    }
    sy_mutexRelease(lock);
    return rc;
}

THIS int32_t st_synthesize(SynthThread *t)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_FAILED, seq;
    MsgSeq *m;

    sy_mutexWait(lock, -1);
    seq = claim(t);
    m = (MsgSeq *)cpp_new(sizeof(MsgSeq));
    if (m) {
        msg_ctor(&m->base, MSG_SYNTHESIZE);
        m->base.vt = &vt_synthesize;
        m->thread = t;
        m->seq = seq;
        rc = postAndCommit(t, &m->base, seq, 1, 1);
    }
    sy_mutexRelease(lock);
    return rc;
}

/* Only the first word of the language record is copied; setString rebuilds
   the rest of it in the copy. */
THIS int32_t st_changeLanguage(SynthThread *t, LangIdentifier *lang)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_FAILED, seq;
    MsgLanguage *m;

    sy_mutexWait(lock, -1);
    seq = claim(t);
    m = (MsgLanguage *)cpp_new(sizeof(MsgLanguage));
    if (m) {
        msg_ctor(&m->base, MSG_CHANGE_LANGUAGE);
        m->base.vt = &vt_changeLanguage;
        m->thread = t;
        m->lang.packed = lang->packed;
        lang_setString(&m->lang);
        m->seq = seq;
        rc = postAndCommit(t, &m->base, seq, 1, 0);
    }
    sy_mutexRelease(lock);
    return rc;
}

THIS int32_t st_changeRomParam(SynthThread *t, int32_t a, int32_t b)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_FAILED, seq;
    MsgPair *m;

    sy_mutexWait(lock, -1);
    seq = claim(t);
    m = (MsgPair *)cpp_new(sizeof(MsgPair));
    if (m) {
        msg_ctor(&m->base, MSG_CHANGE_ROM);
        m->base.vt = &vt_changeRomParam;
        m->thread = t;
        m->a = a;
        m->b = b;
        m->seq = seq;
        rc = postAndCommit(t, &m->base, seq, 1, 0);
    }
    sy_mutexRelease(lock);
    return rc;
}

/* The first argument names a filter the run method looks up for itself, so
   it never reaches the message. */
THIS int32_t st_changeFilter(SynthThread *t, int32_t unused, uint32_t which,
                             uint32_t value, uint8_t flag)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_FAILED, seq;
    MsgFilter *m;

    (void)unused;
    sy_mutexWait(lock, -1);
    seq = claim(t);
    m = (MsgFilter *)cpp_new(sizeof(MsgFilter));
    if (m) {
        msg_ctor(&m->base, MSG_CHANGE_FILTER);
        m->base.vt = &vt_changeFilter;
        m->thread = t;
        m->which = which;
        m->value = value;
        m->seq = seq;
        m->flag = flag;
        rc = postAndCommit(t, &m->base, seq, 1, 0);
    }
    sy_mutexRelease(lock);
    return rc;
}

/* Each of these is handed the parameter it is setting as well as the value,
   and each of them ignores the parameter: the kind of message already says
   which one it is. */
THIS int32_t st_changeVolume(SynthThread *t, int32_t p, int32_t v)
{
    (void)p;
    return sendValue(t, MSG_CHANGE_VOLUME, &vt_changeVolume, v);
}

THIS int32_t st_changeSpeed(SynthThread *t, int32_t p, int32_t v)
{
    (void)p;
    return sendValue(t, MSG_CHANGE_SPEED, &vt_changeSpeed, v);
}

THIS int32_t st_changeSpeedString(SynthThread *t, int32_t p, int32_t v)
{
    (void)p;
    return sendValue(t, MSG_CHANGE_SPEED, &vt_changeSpeedString, v);
}

THIS int32_t st_changePitch(SynthThread *t, int32_t p, int32_t v)
{
    (void)p;
    return sendValue(t, MSG_CHANGE_PITCH, &vt_changePitch, v);
}

/* Asked for by name, this one is numbered as a speed change rather than as a
   pitch change. That is the original's own numbering, not a slip here. */
THIS int32_t st_changePitchString(SynthThread *t, int32_t p, int32_t v)
{
    (void)p;
    return sendValue(t, MSG_CHANGE_SPEED, &vt_changePitchString, v);
}

THIS int32_t st_changeFluctuation(SynthThread *t, int32_t p, int32_t v)
{
    (void)p;
    return sendValue(t, MSG_CHANGE_FLUCT, &vt_changeFluctuation, v);
}

THIS int32_t st_changeFluctuationString(SynthThread *t, int32_t p, int32_t v)
{
    (void)p;
    return sendValue(t, MSG_CHANGE_FLUCT, &vt_changeFluctuationString, v);
}

THIS int32_t st_changeVoice(SynthThread *t, int32_t v)
{
    return sendValue(t, MSG_CHANGE_VOICE, &vt_changeVoice, v);
}

THIS int32_t st_setPhonemeIndicies(SynthThread *t, int32_t v)
{
    return sendValue(t, MSG_PHONEME_INDEX, &vt_setPhonemeIndicies, v);
}

THIS int32_t st_changeETIEmphasis(SynthThread *t)
{
    void *lock = ST_LOCK(t);
    int32_t rc = ERR_FAILED, seq;
    MsgSeq *m;

    sy_mutexWait(lock, -1);
    seq = claim(t);
    m = (MsgSeq *)cpp_new(sizeof(MsgSeq));
    if (m) {
        msg_ctor(&m->base, MSG_CHANGE_EMPHASIS);
        m->base.vt = &vt_changeEmphasis;
        m->thread = t;
        m->seq = seq;
        rc = postAndCommit(t, &m->base, seq, 1, 0);
    }
    sy_mutexRelease(lock);
    return rc;
}

/* Index marks. Anything that reports a position needs the device running,
   because the position is reported as the sound goes out. */
THIS int32_t st_insertIndex(SynthThread *t, int32_t index)
{
    return sendValueWithSound(t, MSG_INSERT_INDEX, &vt_insertIndex, index);
}

THIS int32_t st_insertStringIndex(SynthThread *t, char *name)
{
    return sendString(t, MSG_STRING_INDEX, &vt_insertStringIndex, name);
}

THIS int32_t st_insertAudioIndex(SynthThread *t, char *name)
{
    return sendString(t, MSG_AUDIO_INDEX, &vt_insertAudioIndex, name);
}

/* ---- the tables ----

   Six of the seven slots are the base class's and never change. Only the
   destructor and the run differ, which is why the whole set comes out of one
   macro. The order is the original's, read off its .rdata; a slot in the
   wrong place is a jump into the wrong function rather than a quiet
   difference. */

#define TABLE(name, dtor, runner)     \
    const MessageVtbl name = {        \
        dtor,                         \
        msg_addRef,                  \
        msg_release,                 \
        msg_getType,          \
        msg_equalsMessage,           \
        msg_equalsType,              \
        runner,                       \
    }

TABLE(vt_addText, destroy_addText, run_addText);
TABLE(vt_addParam, destroy_addParam, run_addParam);
TABLE(vt_insertStringIndex, destroy_insertStringIndex, run_insertStringIndex);
TABLE(vt_insertAudioIndex, destroy_insertAudioIndex, run_insertAudioIndex);

TABLE(vt_block, destroy_plain, run_block);
TABLE(vt_synthesize, destroy_plain, run_synthesize);
TABLE(vt_changeLanguage, destroy_plain, run_changeLanguage);
TABLE(vt_changeRomParam, destroy_plain, run_changeRomParam);
TABLE(vt_changeFilter, destroy_plain, run_changeFilter);
TABLE(vt_changeVolume, destroy_plain, run_changeVolume);
TABLE(vt_changeSpeed, destroy_plain, run_changeSpeed);
TABLE(vt_changeSpeedString, destroy_plain, run_changeSpeedString);
TABLE(vt_changePitch, destroy_plain, run_changePitch);
TABLE(vt_changePitchString, destroy_plain, run_changePitchString);
TABLE(vt_changeFluctuation, destroy_plain, run_changeFluctuation);
TABLE(vt_changeFluctuationString, destroy_plain,
      run_changeFluctuationString);
TABLE(vt_changeVoice, destroy_plain, run_changeVoice);
TABLE(vt_changeEmphasis, destroy_plain, run_changeEmphasis);
TABLE(vt_setPhonemeIndicies, destroy_plain, run_setPhonemeIndicies);
TABLE(vt_insertIndex, destroy_plain, run_insertIndex);

/* The names the rest of the engine reaches all of this by. */
ALIAS("??_7ETImsgAddText@@6B@", "vt_addText");
ALIAS("??_7ETImsgAddParam@@6B@", "vt_addParam");
ALIAS("??_7ETImsgBlock@@6B@", "vt_block");
ALIAS("??_7ETImsgSynthesize@@6B@", "vt_synthesize");
ALIAS("??_7ETImsgChangeLanguage@@6B@", "vt_changeLanguage");
ALIAS("??_7ETImsgChangeRomParam@@6B@", "vt_changeRomParam");
ALIAS("??_7ETImsgChangeFilter@@6B@", "vt_changeFilter");
ALIAS("??_7ETImsgChangeVolume@@6B@", "vt_changeVolume");
ALIAS("??_7ETImsgChangeSpeed@@6B@", "vt_changeSpeed");
ALIAS("??_7ETImsgChangeSpeedString@@6B@", "vt_changeSpeedString");
ALIAS("??_7ETImsgChangePitch@@6B@", "vt_changePitch");
ALIAS("??_7ETImsgChangePitchBaselineString@@6B@", "vt_changePitchString");
ALIAS("??_7ETImsgChangeFluctuation@@6B@", "vt_changeFluctuation");
ALIAS("??_7ETImsgChangeFluctuationString@@6B@",
      "vt_changeFluctuationString");
ALIAS("??_7ETImsgChangeVoice@@6B@", "vt_changeVoice");
ALIAS("??_7ETImsgChangeEmphasis@@6B@", "vt_changeEmphasis");
ALIAS("??_7ETImsgSetPhonemeIndicies@@6B@", "vt_setPhonemeIndicies");
ALIAS("??_7ETImsgInsertIndex@@6B@", "vt_insertIndex");
ALIAS("??_7ETImsgInsertStringIndex@@6B@", "vt_insertStringIndex");
ALIAS("??_7ETImsgInsertAudioIndex@@6B@", "vt_insertAudioIndex");

ALIAS("?run@ETImsgAddText@@UAEXXZ", "run_addText");
ALIAS("?run@ETImsgAddParam@@UAEXXZ", "run_addParam");
ALIAS("?run@ETImsgBlock@@UAEXXZ", "run_block");
ALIAS("?run@ETImsgSynthesize@@UAEXXZ", "run_synthesize");
ALIAS("?run@ETImsgChangeLanguage@@UAEXXZ", "run_changeLanguage");
ALIAS("?run@ETImsgChangeRomParam@@UAEXXZ", "run_changeRomParam");
ALIAS("?run@ETImsgChangeFilter@@UAEXXZ", "run_changeFilter");
ALIAS("?run@ETImsgChangeVolume@@UAEXXZ", "run_changeVolume");
ALIAS("?run@ETImsgChangeSpeed@@UAEXXZ", "run_changeSpeed");
ALIAS("?run@ETImsgChangeSpeedString@@UAEXXZ", "run_changeSpeedString");
ALIAS("?run@ETImsgChangePitch@@UAEXXZ", "run_changePitch");
ALIAS("?run@ETImsgChangePitchBaselineString@@UAEXXZ",
      "run_changePitchString");
ALIAS("?run@ETImsgChangeFluctuation@@UAEXXZ", "run_changeFluctuation");
ALIAS("?run@ETImsgChangeFluctuationString@@UAEXXZ",
      "run_changeFluctuationString");
ALIAS("?run@ETImsgChangeVoice@@UAEXXZ", "run_changeVoice");
ALIAS("?run@ETImsgChangeEmphasis@@UAEXXZ", "run_changeEmphasis");
ALIAS("?run@ETImsgSetPhonemeIndicies@@UAEXXZ", "run_setPhonemeIndicies");
ALIAS("?run@ETImsgInsertIndex@@UAEXXZ", "run_insertIndex");
ALIAS("?run@ETImsgInsertStringIndex@@UAEXXZ", "run_insertStringIndex");
ALIAS("?run@ETImsgInsertAudioIndex@@UAEXXZ", "run_insertAudioIndex");

/* Both spellings of the deleting destructor. The original's tables name the
   vector form and its code calls the scalar form, and the two are the same
   function. */
#define DTOR_ALIAS(cls, ours)                          \
    ALIAS("??_G" cls "@@UAEPAXI@Z", ours);             \
    ALIAS("??_E" cls "@@UAEPAXI@Z", ours)

DTOR_ALIAS("ETImsgAddText", "destroy_addText");
DTOR_ALIAS("ETImsgAddParam", "destroy_addParam");
DTOR_ALIAS("ETImsgInsertStringIndex", "destroy_insertStringIndex");
DTOR_ALIAS("ETImsgInsertAudioIndex", "destroy_insertAudioIndex");

ALIAS("??1ETImsgAddText@@UAE@XZ", "dtor_addText");
ALIAS("??1ETImsgAddParam@@UAE@XZ", "dtor_addParam");
ALIAS("??1ETImsgInsertStringIndex@@UAE@XZ", "dtor_insertStringIndex");
ALIAS("??1ETImsgInsertAudioIndex@@UAE@XZ", "dtor_insertAudioIndex");

/* Sixteen classes share one destructor because none of them own anything,
   but each still needs its own name for the linker. */
DTOR_ALIAS("ETImsgBlock", "destroy_plain");
DTOR_ALIAS("ETImsgSynthesize", "destroy_plain");
DTOR_ALIAS("ETImsgChangeLanguage", "destroy_plain");
DTOR_ALIAS("ETImsgChangeRomParam", "destroy_plain");
DTOR_ALIAS("ETImsgChangeFilter", "destroy_plain");
DTOR_ALIAS("ETImsgChangeVolume", "destroy_plain");
DTOR_ALIAS("ETImsgChangeSpeed", "destroy_plain");
DTOR_ALIAS("ETImsgChangeSpeedString", "destroy_plain");
DTOR_ALIAS("ETImsgChangePitch", "destroy_plain");
DTOR_ALIAS("ETImsgChangePitchBaselineString", "destroy_plain");
DTOR_ALIAS("ETImsgChangeFluctuation", "destroy_plain");
DTOR_ALIAS("ETImsgChangeFluctuationString", "destroy_plain");
DTOR_ALIAS("ETImsgChangeVoice", "destroy_plain");
DTOR_ALIAS("ETImsgChangeEmphasis", "destroy_plain");
DTOR_ALIAS("ETImsgSetPhonemeIndicies", "destroy_plain");
DTOR_ALIAS("ETImsgInsertIndex", "destroy_plain");

ALIAS("??0ETImsgAddText@@QAE@PAVSynthThread@@PADKJH@Z", "ctor_addText");
ALIAS("??0ETImsgAddParam@@QAE@PAVSynthThread@@PADKJ@Z", "ctor_addParam");

ALIAS("?startUpSound@SynthThread@@AAEJPAH@Z", "stg_startUpSound");
ALIAS("?addText@SynthThread@@QAEJPADKH@Z", "st_addText");
ALIAS("?addParam@SynthThread@@QAEJPADK@Z", "st_addParam");
ALIAS("?block@SynthThread@@QAEJXZ", "st_block");
ALIAS("?synthesize@SynthThread@@QAEJXZ", "st_synthesize");
ALIAS("?changeLanguage@SynthThread@@QAEJPAVLangIdentifier@@@Z",
      "st_changeLanguage");
ALIAS("?changeRomParam@SynthThread@@QAEJJJ@Z", "st_changeRomParam");
ALIAS("?changeFilter@SynthThread@@QAEJJJJ_N@Z", "st_changeFilter");
ALIAS("?changeVolume@SynthThread@@QAEJJJ@Z", "st_changeVolume");
ALIAS("?changeSpeed@SynthThread@@QAEJJJ@Z", "st_changeSpeed");
ALIAS("?changeSpeedString@SynthThread@@QAEJJJ@Z", "st_changeSpeedString");
ALIAS("?changePitch@SynthThread@@QAEJJJ@Z", "st_changePitch");
ALIAS("?changePitchString@SynthThread@@QAEJJJ@Z", "st_changePitchString");
ALIAS("?changeFluctuation@SynthThread@@QAEJJJ@Z", "st_changeFluctuation");
ALIAS("?changeFluctuationString@SynthThread@@QAEJJJ@Z",
      "st_changeFluctuationString");
ALIAS("?changeVoice@SynthThread@@QAEJJ@Z", "st_changeVoice");
ALIAS("?changeETIEmphasis@SynthThread@@QAEJXZ", "st_changeETIEmphasis");
ALIAS("?setPhonemeIndicies@SynthThread@@QAEJJ@Z", "st_setPhonemeIndicies");
ALIAS("?insertIndex@SynthThread@@QAEJJ@Z", "st_insertIndex");
ALIAS("?insertIndex@SynthThread@@QAEJPAD@Z", "st_insertStringIndex");
ALIAS("?insertAudioIndex@SynthThread@@QAEJPAD@Z", "st_insertAudioIndex");
