/* What the pieces of SynthThread share.

   SynthThread is still mostly the original's, so this describes it by offset
   rather than as a struct: the fields named here are the ones our own parts
   of it touch, and everything between them is still the original's business.
   The offsets come from the two constructors, which write the whole block
   out in order. */

#ifndef ECI_SYNTHTHREAD_H
#define ECI_SYNTHTHREAD_H

#include <stdint.h>

#define THIS __attribute__((thiscall))
#define MANGLED(name) __asm__("\"" name "\"")

#define ALIAS(mangled, ours) \
    __asm__(".globl \"" mangled "\"\n.set \"" mangled "\", _" ours "\n")

typedef struct ETImessage ETImessage;
typedef struct SynthThread SynthThread;

/* A language name as the rest of the engine passes it about. */
typedef struct {
    uint32_t id;            /* +0x00 */
    uint8_t  rest[0x10];    /* +0x04 */
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

#define ST_AT(t, off)   ((void *)((char *)(t) + (off)))
#define ST_I32(t, off)  (*(int32_t *)((char *)(t) + (off)))
#define ST_U32(t, off)  (*(uint32_t *)((char *)(t) + (off)))
#define ST_PTR(t, off)  (*(void **)((char *)(t) + (off)))

#define ST_ENGINE(t)    ST_PTR(t, 0x2dc)  /* the synthesiser itself */
#define ST_ENGINE_ID(t) ST_U32(t, 0x2e0)  /* which one, packed into a word */
#define ST_LOCK(t)      ST_AT(t, 0x2f4)   /* held around the counts below */
#define ST_POSTED(t)    ST_I32(t, 0x300)  /* something is on the queue */
#define ST_SAMPLES(t)   ST_I32(t, 0x304)  /* how much sound has been made */
#define ST_PENDING(t)   ST_I32(t, 0x308)  /* how much is still to come */
#define ST_LASTMARK(t)  ST_I32(t, 0x30c)  /* where the last index went in */
#define ST_INDEXQ(t)    ST_AT(t, 0x310)   /* IndexQueue */
#define ST_SOUND(t)     ST_PTR(t, 0x330)  /* SoundThread, null before setup */
#define ST_INDEXMGR(t)  ST_AT(t, 0x334)   /* IndexManager */
#define ST_FORMAT(t)    ST_AT(t, 0x324)   /* ECIsampleFormat */
#define ST_APP(t)       ST_PTR(t, 0x370)  /* ETIappMessageQueue */
#define ST_STATE(t)     ST_PTR(t, 0x374)  /* ECIstate */
#define ST_SILENT(t)    ST_I32(t, 0x39c)  /* set when no device is wanted */
#define ST_BLOCKER(t)   ST_PTR(t, 0x3ac)  /* Semaphore, made on first block */
#define ST_ROMAN(t)     ST_PTR(t, 0x3b0)  /* RomanizerManager */
#define ST_MARKS(t)     ST_PTR(t, 0x3b4)  /* where index notes are queued */
#define ST_FLAGS(t)     ST_U32(t, 0x3b8)
#define ST_CONCAT(t)    ST_PTR(t, 0x3c4)  /* ConcatenationManager, may be 0 */
#define ST_FILTER(t)    ST_PTR(t, 0x3d8)  /* the one filter in play */
#define ST_TOLD_CAT(t)  ST_I32(t, 0x3e0)  /* told the caller we went concat */

/* Bits of ST_FLAGS this side reads. */
#define STF_WORD_MARKS  0x100  /* report where each word starts */
#define STF_ROMANIZING  0x200  /* index marks go through the romanizer */

/* The application queue's count of what it has been told about, and the flag
   saying whether anyone is listening. */
#define APP_POSTED(a)   (*(int32_t *)((char *)(a) + 0x58))
#define APP_LISTENING(a) (*(int32_t *)((char *)(a) + 0x4c))

extern THIS int32_t mutex_wait(void *m, int32_t ms)
    MANGLED("?wait@Mutex@@QAEHJ@Z");
extern THIS int32_t mutex_release(void *m)
    MANGLED("?release@Mutex@@QAEHXZ");
extern THIS void mutex_dtor(void *m)
    MANGLED("??1Mutex@@QAE@XZ");

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

#endif
