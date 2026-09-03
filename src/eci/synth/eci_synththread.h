/* SynthThread: the thread that turns text into samples, and what it holds.

   This was written out as a list of byte offsets, because for a long time
   most of the block was still the original's and only the fields our own
   parts touched were known. All of it is ours now and every offset has a
   name, so it is a struct. The old names are kept as macros over the fields,
   since a hundred places read them and none of those had to change.

   The comment against each field is where it sat when a pointer was four
   bytes. Nothing depends on that any more; it is there because the original's
   own code is what the names were read out of. */

#ifndef ECI_SYNTHTHREAD_H
#define ECI_SYNTHTHREAD_H

#include <stdint.h>
#include "evv_abi.h"
#include "eci_objects.h"
#include "eci_index.h"
#include "eci_engine.h"

typedef struct ETImessage ETImessage;
typedef struct SynthThread SynthThread;

/* A language name as the rest of the engine passes it about: the packed
   number, and the printable form that follows it. */
typedef struct {
    int32_t packed;         /* +0x00 */
    char    text[12];       /* +0x04 */
    char    pad_10[4];
} LangIdentifier;

/* One index mark waiting to be handed to whatever will report it. The two
   words are the other way round from the record the index manager keeps,
   which is the original's doing and not a slip. */
typedef struct {
    int32_t payload;        /* +0x00, a number, or a name it took a copy of */
    int32_t kind;           /* +0x04, 0 plain, 4 by name, 5 audio */
} IndexNote;

typedef struct {
    int32_t kind;           /* +0x00 */
    int32_t payload;        /* +0x04 */
} Index;

/* Index notes wait in a plain queue between the romanizer putting one in and
   the engine asking for it. Slot for slot as ETIqueue has them. */
typedef struct MarkQueue MarkQueue;
typedef struct {
    THIS void   *(*destroy)(MarkQueue *self, int32_t free_it);
    THIS int32_t (*push)(MarkQueue *self, void *item);
    THIS int32_t (*pop)(MarkQueue *self, void **item);
    THIS int32_t (*peekHead)(MarkQueue *self, void **item);
} MarkQueueVtbl;
struct MarkQueue { const MarkQueueVtbl *vt; };

/* The allocator the index manager is: how big one index is, where they
   start, and a lock. Nothing reads what lies between. */
typedef struct IndexManager {
    int32_t stride;         /* +0x00 */
    char   *base;           /* +0x04 */
    uint8_t unknown_08[0x24 - 0x08];
    uint8_t lock[0x0c];     /* +0x24 */
} IndexManager;

/* How the samples are to come out. */
typedef struct SampleFormat {
    int32_t layout;
    int32_t rate;
    int32_t width;
} SampleFormat;

struct SynthThread {
    ETImessageQueueThread base;    /* 0x000 */
    void         *converter;       /* 0x088, AudioConverter, may be 0 */
    EngineArray   engines;         /* 0x08c, one per language */
    void         *engine;          /* 0x2dc, the synthesiser itself */
    LangIdentifier lang;           /* 0x2e0, which one, and its name */
    uint8_t       lock[0x0c];      /* 0x2f4, held around the counts below */
    int32_t       posted;          /* 0x300, something is on the queue */
    int32_t       samples;         /* 0x304, how much sound has been made */
    int32_t       pending;         /* 0x308, how much is still to come */
    int32_t       lastmark;        /* 0x30c, where the last index went in */
    IndexQueue    indexq;          /* 0x310 */
    void         *outfmt;          /* 0x320, a format handed in from outside */
    SampleFormat  format;          /* 0x324 */
    void         *sound;           /* 0x330, SoundThread, 0 before setup */
    IndexManager  indexmgr;        /* 0x334 */
    uint8_t       synced[0x0c];    /* 0x364, ETIEvent, an internal index */
    void         *app;             /* 0x370, ETIappMessageQueue */
    void         *state;           /* 0x374, ECIstate */
    /* The two buffers the caller registers to be handed results in, each with
       the room it has and how much of it is filled. A null one means the
       caller did not ask for that kind of result. */
    int16_t      *sampbuf;         /* 0x378 */
    int32_t       samproom;        /* 0x37c, in bytes, not samples */
    int32_t       sampheld;        /* 0x380, in samples */
    void         *phonbuf;         /* 0x384 */
    char         *engphon;         /* 0x388, the engine's */
    int32_t       engphonroom;     /* 0x38c */
    int32_t       phonheld;        /* 0x390 */
    /* Each kind of error is reported to the caller once and then kept
       quiet. */
    int32_t       engerr;          /* 0x394 */
    int32_t       romerr;          /* 0x398 */
    int32_t       silent;          /* 0x39c, the device is not to be used;
                                      set for good once it fails */
    /* The last dictionary lookup, kept so that asking the same thing twice
       costs nothing. */
    void         *lastlookup;      /* 0x3a0 */
    void         *lastkey;         /* 0x3a4 */
    void         *lastvalue;       /* 0x3a8 */
    void         *blocker;         /* 0x3ac, Semaphore, made on first block */
    void         *roman;           /* 0x3b0, RomanizerManager */
    MarkQueue    *marks;           /* 0x3b4 */
    uint32_t      flags;           /* 0x3b8 */
    int32_t       status;          /* 0x3bc, what the constructor answered */
    uint32_t      corpora;         /* 0x3c0, which corpora this engine has */
    void         *concat;          /* 0x3c4, ConcatenationManager, may be 0 */
    int32_t       stopped;         /* 0x3c8 */
    int32_t       direct;          /* 0x3cc, set while talking to the engine */
    int32_t       old_engine;      /* 0x3d0, the engine's answer, kept */
    void         *filters;         /* 0x3d4, FilterManager, may be 0 */
    void         *filter;          /* 0x3d8, the one filter in play */
    uint8_t       fresh;           /* 0x3dc, nothing said to the engine since
                                      the reset */
    uint8_t       pad_3dd[3];
    int32_t       told_cat;        /* 0x3e0, told the caller we went concat */
};

#define ST_ENGINES(t)   (&(t)->engines)
#define ST_CONVERTER(t) ((t)->converter)
#define ST_ENGINE(t)    ((t)->engine)
#define ST_ENGINE_ID(t) (*(uint32_t *)&(t)->lang.packed)
/* The second byte of that word is the dialect. Two ids that differ only
   there are the same engine, so the change path compares them without it. */
#define LANG_ENGINE_MASK 0xffff00ffu
/* And one bit of it says the engine names its phonemes in wide characters
   rather than bytes. */
#define LANG_WIDE_PHONEMES 0x800u
#define ST_LOCK(t)      ((void *)(t)->lock)
#define ST_POSTED(t)    ((t)->posted)
#define ST_SAMPLES(t)   ((t)->samples)
#define ST_PENDING(t)   ((t)->pending)
#define ST_LASTMARK(t)  ((t)->lastmark)
#define ST_INDEXQ(t)    (&(t)->indexq)
#define ST_OUTFMT(t)    ((t)->outfmt)
#define ST_SOUND(t)     ((t)->sound)
#define ST_INDEXMGR(t)  (&(t)->indexmgr)
#define ST_FORMAT(t)    (&(t)->format)
#define ST_APP(t)       ((t)->app)
#define ST_SYNCED(t)    ((void *)(t)->synced)
#define ST_STATE(t)     ((t)->state)
#define ST_SAMPBUF(t)   ((t)->sampbuf)
#define ST_SAMPROOM(t)  ((t)->samproom)
#define ST_SAMPHELD(t)  ((t)->sampheld)
#define ST_PHONBUF(t)   ((t)->phonbuf)
#define ST_ENGPHON(t)   ((t)->engphon)
#define ST_ENGPHONROOM(t) ((t)->engphonroom)
#define ST_PHONHELD(t)  ((t)->phonheld)
#define ST_ENGERR(t)    ((t)->engerr)
#define ST_ROMERR(t)    ((t)->romerr)
#define ST_SILENT(t)    ((t)->silent)
#define ST_LASTLOOKUP(t) ((t)->lastlookup)
#define ST_LASTKEY(t)   ((t)->lastkey)
#define ST_LASTVALUE(t) ((t)->lastvalue)
#define ST_BLOCKER(t)   ((t)->blocker)
#define ST_ROMAN(t)     ((t)->roman)
#define ST_MARKS(t)     ((t)->marks)
#define ST_FLAGS(t)     ((t)->flags)
#define ST_STATUS(t)    ((t)->status)
#define ST_CORPORA(t)   ((t)->corpora)
#define ST_CONCAT(t)    ((t)->concat)
#define ST_STOPPED(t)   ((t)->stopped)
#define ST_DIRECT(t)    ((t)->direct)
#define ST_OLD_ENGINE(t) ((t)->old_engine)
#define ST_FILTERS(t)   ((t)->filters)
#define ST_FILTER(t)    ((t)->filter)
#define ST_FRESH(t)     ((t)->fresh)
#define ST_TOLD_CAT(t)  ((t)->told_cat)

/* Bits of ST_FLAGS this side reads. */
#define STF_WORD_MARKS  0x100  /* report where each word starts */
#define STF_ROMANIZING  0x200  /* index marks go through the romanizer */
#define STF_WORD_STARTS 0x001  /* the plainer of the two word reports */

/* The application queue's two counts -- what it has been told about and what
   it has collected -- and whether anyone is listening, which is whether a
   callback has been registered. The two counts are only ever equal when
   there is nothing outstanding, so they have to be put back together. */
#define APP_POSTED(a)    (((ETIappMessageQueue *)(a))->posted)
#define APP_SEEN(a)      (((ETIappMessageQueue *)(a))->seen)
#define APP_LISTENING(a) (((ETIappMessageQueue *)(a))->cb != 0)

extern THIS int32_t sy_mutexWait(void *m, int32_t ms)
    MANGLED("?wait@Mutex@@QAEHJ@Z");
extern THIS int32_t sy_mutexRelease(void *m)
    MANGLED("?release@Mutex@@QAEHXZ");
extern THIS void sy_mutexDtor(void *m)
    MANGLED("??1Mutex@@QAE@XZ");

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

#endif
