/* The voices the engine has, as the SSML reader asks about them.
 *
 * src/eci_vcinfo.c is the whole of it. One record per voice a language
 * declares a dataset for, and a narrowing question over them.
 */

#ifndef ECI_VCINFO_H
#define ECI_VCINFO_H

#include <stdint.h>

/* How many voices the table may hold, and how many languages the engine
   may report. Both are the original's own bounds. */
#define CVI_VOICES 0x100
#define CVI_LANGS  0x20

/* A hundred and sixty-eight bytes, and the name is the first hundred and
   twenty-eight of them. `tag' is a field the reader never sets and the
   narrowing question can still be asked about. */
typedef struct {
    char    name[0x80];
    int32_t language;
    int32_t voice;
    int32_t dataset;
    int32_t rate;
    int32_t ageBand;
    int32_t variant;
    int32_t unused;
    char   *owned1;
    char   *owned2;
    int32_t tag;
} VOICE_INFO;

typedef struct {
    int32_t     count;
    int32_t     nlangs;
    uint32_t    langs[CVI_LANGS];
    VOICE_INFO *voices[CVI_VOICES];
} CVoicesInfo;

void    cvi_ctor(CVoicesInfo *self);
void    cvi_dtor(CVoicesInfo *self);
void    cvi_delete(CVoicesInfo *self);
void    cvi_initVoicesInfo(CVoicesInfo *self);
int32_t cvi_getVoiceInfo(CVoicesInfo *self, VOICE_INFO *info, int32_t mask);
void    cvi_getScansoftVoices(CVoicesInfo *self);
int32_t cvi_isSsftVoicesAvailables(CVoicesInfo *self);

#endif
