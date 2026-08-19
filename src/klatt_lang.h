/* The two blocks the synthesiser keeps per language: where the sound is
   going, and what the language has said so far.
 *
 * They are here rather than in klatt_run.c because the user dictionary
 * reaches into the second one as well, and two files that each know only
 * where a field sits are two files that stop agreeing the moment a pointer
 * grows.
 */

#ifndef KLATT_LANG_H
#define KLATT_LANG_H

#include <stdint.h>

typedef struct SynthDevice SynthDevice;
typedef struct DeltaLang DeltaLang;
struct DictionarySet;

/* Where the sound is going, and what is to be reported about it. The queue
   is the index marks waiting to be handed back as the sound they stand in
   front of is played. */
struct SynthDevice {
    void      *sample_cb;     /* +0x00 */
    void      *sample_data;   /* +0x04 */
    void      *dur_cb;        /* +0x08 */
    void      *dur_data;      /* +0x0c */
    char      *filename;      /* +0x10 */
    uint8_t    queue[0x10];   /* +0x14 */
    int32_t    sleepcycle;    /* +0x24 */
    int32_t    playing;       /* +0x28 */
    int32_t    open;          /* +0x2c */
    int32_t    interrupted;   /* +0x30 */
    int32_t    lazy_write;    /* +0x34 */
    int32_t    pending;       /* +0x38 */
    int32_t    last_clock;    /* +0x3c */
    int32_t    hold;          /* +0x40 */
    void      *index_cb;      /* +0x44 */
    void      *index_data;    /* +0x48 */
    void      *phoneme_cb;    /* +0x4c */
    void      *phoneme_data;  /* +0x50 */
};

/* The language record. Its three running totals are the whole of the mark
   timing, and the rest is memory it owns. */
struct DeltaLang {
    uint8_t      unknown_00[8];   /* +0x00 */
    const char  *extension;       /* +0x08 */
    const char  *voice_file;      /* +0x0c */
    SynthDevice *device;          /* +0x10 */
    void        *buf_100;         /* +0x14 */
    int32_t      flag_18;         /* +0x18 */
    void        *buf_140;         /* +0x1c */
    void        *klatt;           /* +0x20 */
    uint8_t      unknown_24[0x30 - 0x24];
    void        *buf_4;           /* +0x30 */
    uint8_t      unknown_34[4];   /* +0x34 */
    struct DictionarySet *current; /* +0x38, the dictionary in play */
    int8_t       stream;          /* +0x3c, which stream it is read from */
    uint8_t      unknown_3d[0x44 - 0x3d];
    int32_t      spoken;          /* +0x44 */
    int32_t      marked;          /* +0x48 */
    int32_t      queued;          /* +0x4c */
    int32_t      rate;            /* +0x50 */
};

#endif
