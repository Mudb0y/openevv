/* Audio formats, and the sound threads that serve them.

   A caller asks for a format -- a rate, a width, a device name, and four
   numbers describing how the device is to be buffered -- and gets back a
   record it can hold on to. Several callers asking for the same thing share
   one record, which is why it is counted rather than owned: the last one to
   let go is the one that takes it down.

   Whether a format is usable at all is not decided here. A sound thread is
   made for it and asked to set itself up, and what comes back is both the
   answer and, where the caller left a field at nought, the value the device
   chose. That is why asking for a format can change the request.

   Names are prefixed and the aliases at the foot carry the real ones. */

#include <stdint.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "eci_timer.h"

/* What a caller describes a format with. The name doubles as the device
   number when it is one, which is why it is a string. */
typedef struct ECIaudioFormat {
    int32_t  kind;              /* +0x00 */
    int32_t  hz;                /* +0x04 */
    int32_t  flags;             /* +0x08 */
    char    *name;              /* +0x0c */
    int32_t  blocks;            /* +0x10 */
    int32_t  blockBytes;        /* +0x14 */
    int32_t  prerollBlocks;     /* +0x18 */
    int32_t  prerollBytes;      /* +0x1c */
} ECIaudioFormat;

/* The record handed back, and the thread that will do the work. */
typedef struct AudioFormat {
    uint32_t        refs;       /* +0x00 */
    void           *thread;     /* +0x04 */
    uint32_t        slot;       /* +0x08, where the manager keeps it */
    ECIaudioFormat  fmt;        /* +0x0c */
} AudioFormat;

extern const uint32_t st_bytes;

/* The manager itself: a lock, a growable array of formats, and the timer
   thread every sound thread shares. */
typedef struct SoundManager {
    uint8_t     lock[0x0c];     /* +0x00, a Mutex */
    AudioFormat **formats;      /* +0x0c */
    uint32_t    count;          /* +0x10 */
    uint32_t    capacity;       /* +0x14 */
    TimerThread timer;
} SoundManager;

/* What a caller has to allocate for one. Only this file knows what is
   in it, so only this file can say. */
const uint32_t sm_bytes = sizeof(SoundManager);

/* What asking for a format can answer. */
#define FORMAT_OK          0
#define FORMAT_NO_ROOM     (-2)
#define FORMAT_NO_REQUEST  (-3)
#define FORMAT_UNSUPPORTED (-4)

/* What a sound thread answers when it has set itself up. */
#define SETUP_OK           1

extern THIS void *sy_mutexCtor(void *m, int32_t recursive)
    MANGLED("??0Mutex@@QAE@H@Z");
extern THIS void sy_mutexDtor(void *m) MANGLED("??1Mutex@@QAE@XZ");
extern THIS int sy_mutexWait(void *m, int32_t ms) MANGLED("?wait@Mutex@@QAEHJ@Z");
extern THIS int sy_mutexRelease(void *m) MANGLED("?release@Mutex@@QAEHXZ");
extern THIS void *tt_ctor(void *t) MANGLED("??0TimerThread@@QAE@XZ");
extern THIS void tt_dtor(void *t) MANGLED("??1TimerThread@@UAE@XZ");
extern THIS void *snd_ctor(void *t, void *timer)
    MANGLED("??0SoundThread@@QAE@PAVTimerThread@@@Z");
extern THIS int32_t snd_close(void *t)
    MANGLED("?close@SoundThread@@QAEHXZ");
extern THIS int16_t snd_setup(void *t, char *name, int32_t *hz,
                                     int32_t *flags, int32_t *kind,
                                     int32_t *blocks, int32_t *blockBytes,
                                     int32_t *prerollBlocks,
                                     int32_t *prerollBytes)
    MANGLED("?setup@SoundThread@@QAEFPADPAJ111111@Z");

/* Waiting on the lock with minus one means waiting as long as it takes. */
#define WAIT_FOREVER  (-1)

/* What a format asked for with nothing filled in comes out as. */
static const ECIaudioFormat DEFAULT_FORMAT = { 0, 0, 0, "0", 10, 2200, 0,
                                               2200 };

THIS AudioFormat *sm_formatCtor(AudioFormat *a, uint32_t slot,
                                ECIaudioFormat *want, void *timer);
THIS void sm_formatDtor(AudioFormat *a);
THIS uint32_t sm_release(AudioFormat *a);

/* ---- deleting through a vtable -------------------------------------- */

/* A sound thread is taken down through the first slot of its own table,
   which is the scalar deleting destructor, so that its own class decides
   how. */
typedef void *(THIS *DeleteFn)(void *self, int32_t flags);

static void sm_deleteThread(void *t)
{
    if (t)
        ((DeleteFn)(*(void ***)t)[0])(t, 1);
}

/* ---- one format ------------------------------------------------------ */

THIS AudioFormat *sm_formatCtor(AudioFormat *a, uint32_t slot,
                                ECIaudioFormat *want, void *timer)
{
    void *thread;
    char *name;

    a->refs = 0;
    a->thread = 0;
    a->slot = slot;
    memcpy(&a->fmt, want, sizeof a->fmt);

    /* The name is copied, because the caller's may not outlive us. */
    name = cpp_new(strlen(want->name) + 1);
    a->fmt.name = name;
    if (!name)
        return a;
    strcpy(name, want->name);

    thread = cpp_new(st_bytes);
    a->thread = thread ? snd_ctor(thread, timer) : 0;
    return a;
}

THIS void sm_formatDtor(AudioFormat *a)
{
    if (a->fmt.name) {
        cpp_delete(a->fmt.name);
        a->fmt.name = 0;
    }
    if (a->thread) {
        sm_deleteThread(a->thread);
        a->thread = 0;
    }
}

THIS uint32_t sm_addRef(AudioFormat *a)
{
    return ++a->refs;
}

/* Let go of one hold. The last one takes the whole thing down and answers
   nought; any other answers what is left. */
THIS uint32_t sm_release(AudioFormat *a)
{
    if (--a->refs != 0)
        return a->refs;

    if (a->thread) {
        snd_close(a->thread);
        sm_deleteThread(a->thread);
        a->thread = 0;
    }
    sm_formatDtor(a);
    cpp_delete(a);
    return 0;
}

/* Whether the device will have this format. Asking is the only way to find
   out, and the asking fills in whatever the caller left blank. */
THIS int sm_isSupported(AudioFormat *a)
{
    if (!a->thread)
        return 0;
    return snd_setup(a->thread, a->fmt.name, &a->fmt.hz,
                            &a->fmt.flags, &a->fmt.kind, &a->fmt.blocks,
                            &a->fmt.blockBytes, &a->fmt.prerollBlocks,
                            &a->fmt.prerollBytes) == SETUP_OK;
}

/* ---- the manager ----------------------------------------------------- */

THIS SoundManager *sm_ctor(SoundManager *m)
{
    sy_mutexCtor(m, 0);
    m->formats = 0;
    m->count = 0;
    m->capacity = 0;
    tt_ctor(&m->timer);
    return m;
}

THIS void sm_dtor(SoundManager *m)
{
    if (m->formats) {
        m->count = 0;
        cpp_delete(m->formats);
        m->formats = 0;
    }
    tt_dtor(&m->timer);
    sy_mutexDtor(m);
}

/* Make sure there is room for one more. The array doubles, which means a
   run of formats costs a handful of copies rather than one per format. */
THIS int sm_ensureArraySize(SoundManager *m)
{
    uint32_t bigger;
    AudioFormat **grown;

    if (m->count <= m->capacity)
        return 1;

    bigger = m->count * 2;
    grown = cpp_new(bigger * (uint32_t)sizeof *m->formats);
    if (!grown)
        return 0;

    if (m->formats) {
        memcpy(grown, m->formats, m->capacity * sizeof *m->formats);
        cpp_delete(m->formats);
    }
    m->formats = grown;
    m->capacity = bigger;
    return 1;
}

/* Fill in whatever the caller left at nought with the default. A name that
   is there but empty counts as not there. */
THIS void sm_replaceDefaultFieldsWithValues(SoundManager *m,
                                            ECIaudioFormat *f)
{
    (void)m;
    if (!f->kind)
        f->kind = DEFAULT_FORMAT.kind;
    if (!f->hz)
        f->hz = DEFAULT_FORMAT.hz;
    if (!f->flags)
        f->flags = DEFAULT_FORMAT.flags;
    if (!f->name || !f->name[0])
        f->name = DEFAULT_FORMAT.name;
}

/* Ask for a format.

   With nothing asked for, the defaults stand in. The record is made, the
   device is asked whether it will have it, and if it will the record is kept
   and the caller's request is written back with whatever the device settled
   on. If it will not, or there is no room to remember it, the record is
   taken down again and the caller is told which of the two happened. */
THIS int32_t sm_requestAudioFormat(SoundManager *m, ECIaudioFormat *want,
                                   AudioFormat **out)
{
    ECIaudioFormat defaults;
    int32_t rc = FORMAT_NO_REQUEST;

    sy_mutexWait(m, WAIT_FOREVER);

    if (!out)
        goto done;

    *out = 0;
    defaults = DEFAULT_FORMAT;
    if (!want)
        want = &defaults;
    sm_replaceDefaultFieldsWithValues(m, want);

    {
        AudioFormat *a = cpp_new(sizeof(AudioFormat));

        *out = a ? sm_formatCtor(a, m->count, want, &m->timer) : 0;
    }

    if (!*out) {
        rc = FORMAT_NO_ROOM;
        goto done;
    }

    if (!sm_isSupported(*out)) {
        sm_formatDtor(*out);
        cpp_delete(*out);
        *out = 0;
        rc = FORMAT_UNSUPPORTED;
        goto done;
    }

    m->count++;
    if (!sm_ensureArraySize(m)) {
        sm_formatDtor(*out);
        cpp_delete(*out);
        *out = 0;
        rc = FORMAT_NO_ROOM;
        goto done;
    }

    m->formats[m->count - 1] = *out;
    sm_addRef(*out);

    /* What the device settled on goes back to the caller. */
    want->hz = (*out)->fmt.hz;
    want->flags = (*out)->fmt.flags;
    want->kind = (*out)->fmt.kind;
    want->blocks = (*out)->fmt.blocks;
    want->blockBytes = (*out)->fmt.blockBytes;
    want->prerollBlocks = (*out)->fmt.prerollBlocks;
    want->prerollBytes = (*out)->fmt.prerollBytes;
    rc = FORMAT_OK;

done:
    sy_mutexRelease(m);
    return rc;
}

/* Give one back. Only when the last hold goes does it leave the array, and
   the gap is filled from the end so the array stays packed. */
THIS void sm_removeAudioFormat(SoundManager *m, AudioFormat *a)
{
    sy_mutexWait(m, WAIT_FOREVER);

    if (a) {
        uint32_t slot = a->slot;

        if (sm_release(m->formats[slot]) == 0) {
            m->formats[slot] = 0;
            if (m->formats[m->count - 1]) {
                m->formats[m->count - 1]->slot = slot;
                m->formats[slot] = m->formats[m->count - 1];
            }
            m->count--;
        }
    }

    sy_mutexRelease(m);
}

ALIAS("??0AudioFormat@@QAE@KPAUECIaudioFormat@@PAVTimerThread@@@Z",
      "sm_formatCtor");
ALIAS("??1AudioFormat@@QAE@XZ", "sm_formatDtor");
ALIAS("?addRef@AudioFormat@@QAEKXZ", "sm_addRef");
ALIAS("?release@AudioFormat@@QAEKXZ", "sm_release");
ALIAS("?isSupported@AudioFormat@@QAEHXZ", "sm_isSupported");
ALIAS("??0SoundManager@@QAE@XZ", "sm_ctor");
ALIAS("??1SoundManager@@QAE@XZ", "sm_dtor");
ALIAS("?ensureArraySize@SoundManager@@AAEHXZ", "sm_ensureArraySize");
ALIAS("?replaceDefaultFieldsWithValues@SoundManager@@QAEXPAUECIaudioFormat@@@Z",
      "sm_replaceDefaultFieldsWithValues");
ALIAS("?requestAudioFormat@SoundManager@@QAEJPAUECIaudioFormat@@PAPAVAudioFormat@@@Z",
      "sm_requestAudioFormat");
ALIAS("?removeAudioFormat@SoundManager@@QAEXPAVAudioFormat@@@Z",
      "sm_removeAudioFormat");
