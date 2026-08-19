/* The instance the older interface hands out, and the one place its shape is
   written down.
 *
 * Three files reached into it by the byte each field sat at in the original,
 * which stops being one description the moment a pointer grows. The offsets
 * are against each field because that is what they were read off.
 */

#ifndef ECI_OLD_H
#define ECI_OLD_H

#include <stdint.h>

/* How many words of environment there are, and how long one voice's record
   is. Both are counts of things, not of bytes, so they hold either way. */
#define OI_ENV_WORDS    0x12
#define VOICE_BYTES     0x50
#define OLD_VOICES      8

typedef struct QueueElement QueueElement;

typedef struct OldInst {
    uint8_t  unknown_00[0x0c];
    void    *fresh;               /* +0x00c, the newer instance underneath */
    void    *callback;            /* +0x010 */
    void    *cbdata;              /* +0x014 */
    int32_t  env[OI_ENV_WORDS];   /* +0x018, every parameter */
    int32_t  env_saved[OI_ENV_WORDS]; /* +0x060 */
    char     voice[0x50];         /* +0x0a8 */
    char     voice_saved[0x50];   /* +0x0f8 */
    char     voices[OLD_VOICES * VOICE_BYTES]; /* +0x148 */
    uint8_t  unknown_388[0x3c8 - 0x148 - OLD_VOICES * VOICE_BYTES];
    void    *sampbuf;             /* +0x3c8 */
    void    *sampbuf_saved;       /* +0x3cc */
    int32_t  samproom;            /* +0x3d0 */
    int32_t  samproom_saved;      /* +0x3d4 */
    int32_t  lastindex;           /* +0x3d8 */
    int32_t  where;               /* +0x3dc, device, buffer or phonemes */
    void    *direct;              /* +0x3e0 */
    void    *direct2;             /* +0x3e4 */
    uint8_t  unknown_3e8[0x3f4 - 0x3e8];
    char     filename[0x100];     /* +0x3f4 */
    char     filename_saved[0x100]; /* +0x4f4 */
    /* The index report handed back, mode and all: it is one record and the
       caller is given a pointer into it, so the mode lives inside it at ten
       rather than beside it. */
    char     report[0x60c - 0x5f4]; /* +0x5f4 */
    QueueElement *qhead;          /* +0x60c */
    QueueElement *qtail;          /* +0x610 */
    uint8_t  unknown_614[0x6a4 - 0x614];
    int32_t  ready;               /* +0x6a4, and stopped: the same word */
    int32_t  ready2;              /* +0x6a8 */
    uint32_t refused_all;         /* +0x6ac */
    uint32_t refused;             /* +0x6b0 */
    int32_t  busy;                /* +0x6b4 */
    void    *rommgr;              /* +0x6b8 */
    void    *filtermgr;           /* +0x6bc */
    void    *owned1;              /* +0x6c0 */
    void    *owned2;              /* +0x6c4 */
    void    *concat;              /* +0x6c8 */
} OldInst;

#define OI_NEW(h)            ((h)->fresh)
#define OI_CALLBACK(h)       ((h)->callback)
#define OI_CBDATA(h)         ((h)->cbdata)
#define OI_ENV(h)            ((h)->env)
#define OI_ENV_SAVED(h)      ((h)->env_saved)
/* Three of the environment words have names of their own. */
#define OI_RATE(h)           ((h)->env[5])
#define OI_DEVICE(h)         ((h)->env[5])
#define OI_LANG(h)           ((h)->env[9])
#define OI_VOICENO(h)        ((h)->env[(0x05c - 0x018) / 4])
#define OI_PREV_VOICENO(h)   ((h)->env_saved[(0x0a4 - 0x060) / 4])
#define OI_VOICE(h)          ((h)->voice)
#define OI_VOICE_SAVED(h)    ((h)->voice_saved)
#define OI_VOICES(h)         ((h)->voices)
#define OI_SAMPBUF(h)        ((h)->sampbuf)
#define OI_SAMPBUF_SAVED(h)  ((h)->sampbuf_saved)
#define OI_SAMPROOM(h)       ((h)->samproom)
#define OI_SAMPROOM_SAVED(h) ((h)->samproom_saved)
#define OI_LASTINDEX(h)      ((h)->lastindex)
#define OI_WHERE(h)          ((h)->where)
#define OI_DIRECT(h)         ((h)->direct)
#define OI_DIRECT2(h)        ((h)->direct2)
#define OI_FILENAME(h)       ((h)->filename)
#define OI_FILENAME_SAVED(h) ((h)->filename_saved)
#define OI_REPORT(h)         ((h)->report)
#define OI_REPORT_MODE(h)    (*(int32_t *)((h)->report + 10))
#define OI_QHEAD(h)          ((h)->qhead)
#define OI_QTAIL(h)          ((h)->qtail)
#define OI_READY(h)          ((h)->ready)
#define OI_STOPPED(h)        ((h)->ready)
#define OI_READY2(h)         ((h)->ready2)
#define OI_REFUSEDALL(h)     ((h)->refused_all)
#define OI_REFUSED(h)        ((h)->refused)
#define OI_BUSY(h)           ((h)->busy)
#define OI_ROMMGR(h)         ((h)->rommgr)
#define OI_FILTERMGR(h)      ((h)->filtermgr)
#define OI_OWNED1(h)         ((h)->owned1)
#define OI_OWNED2(h)         ((h)->owned2)
#define OI_CONCAT(h)         ((h)->concat)

#endif
