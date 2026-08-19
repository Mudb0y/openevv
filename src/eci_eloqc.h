/* The block the layers above the Delta machine share, and the one place its
   shape is written down.
 *
 * Five files used to reach into it, four of them by the byte each field sat
 * at in the original and one of them by a struct with padding between the two
 * fields it cared about. Those agree only while every field is four bytes.
 */

#ifndef ECI_ELOQC_H
#define ECI_ELOQC_H

#include <stdint.h>
#include "eci_io.h"
#include "eci_link.h"

typedef struct {
    int32_t      want_phonemes; /* +0x00 */
    int32_t      started;       /* +0x04 */
    int32_t      ended;         /* +0x08 */
    int32_t      flushing;      /* +0x0c */
    int32_t      busy;          /* +0x10 */
    /* Six callbacks, each a function and something to hand it. */
    void        *cb[12];        /* +0x14 */
    PhysicalFile link_class;    /* +0x44 */
    PhysicalFile dialog_class;  /* +0x64 */
    uint8_t      pad_84[4];
    int32_t      error_from;    /* +0x88, what the last complaint was about */
    int32_t      error_to;      /* +0x8c */
    int32_t      error_code;    /* +0x90 */
    int32_t      streams;       /* +0x94, how many the language declared */
    int8_t       unknown_98;    /* +0x98, starts at minus one */
    uint8_t      pad_99[7];
    EciLink     *main_link;     /* +0xa0, the text and the answers */
    EciLink     *error_link;    /* +0xa4 */
    EciLink     *cons_link;     /* +0xa8 */
    int32_t      io_done;       /* +0xac */
} Eloqc;

#define ELOQC(d)              EVV_AT(Eloqc *, (d)->eloqc)
#define ELOQ_WANT_PHONEMES(d) (ELOQC(d)->want_phonemes)
#define ELOQ_STARTED(d)       (ELOQC(d)->started)
#define ELOQ_ENDED(d)         (ELOQC(d)->ended)
#define ELOQ_FLUSHING(d)      (ELOQC(d)->flushing)
#define ELOQ_BUSY(d)          (ELOQC(d)->busy)
/* By the byte the slot sat at, since that is how it was read off. */
#define ELOQ_CB(d, off)       (ELOQC(d)->cb[((off) - 0x14) / 4])
#define ELOQ_MAINLINK(d)      (ELOQC(d)->main_link)
#define ELOQ_ERRLINK(d)       (ELOQC(d)->error_link)
#define ELOQ_CONSLINK(d)      (ELOQC(d)->cons_link)
#define ERROR_FROM(d)         (ELOQC(d)->error_from)
#define ERROR_TO(d)           (ELOQC(d)->error_to)
#define ERROR_CODE(d)         (ELOQC(d)->error_code)
#define ELOQ_STREAMS(d)       (ELOQC(d)->streams)

#endif
