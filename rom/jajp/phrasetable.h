/* What PhraseTable is, and the records it works over.
 *
 * The class itself is twenty bytes and holds almost nothing: the analysis it
 * belongs to, the two ends of the chain of phrase-table rows it has filled in,
 * and a `NumRead' of its own. Everything it does is over records that belong
 * to other classes -- `PhraseBuf''s phrase, which is what the path search
 * produced, and `TextAnalysis''s phrase-table row, which is what the
 * intonation reads -- so `rom/jajp/phrasebuf.h' and `rom/jajp/intonphrase.h'
 * are as much this class's map as this file is.
 *
 * How it was read. `TextAnalysis::initialize' constructs it inline: it asks
 * operator new for twenty bytes, writes the vtable and then the analysis at
 * four, which is what says how big it is and what the second field is.
 * `PhraseTable::initialize' allocates the `NumRead' and puts it at sixteen;
 * `GeneratePhraseTable' is the only writer of the two in between and treats
 * them as the head and the tail of a chain over the phrase-table rows.
 */

#ifndef PHRASETABLE_H
#define PHRASETABLE_H

#include <stdint.h>

/* The prefix is PTB and not PT because `rom/jajp/intonphrase.h' already
   spells the phrase-table row PT_, and the row is the thing this class fills
   in rather than the class itself. */
#define PTB_BYTES        0x14

#define PTB_VTABLE       0x00
#define PTB_OWNER        0x04   /* TextAnalysis * */
#define PTB_HEAD         0x08   /* _PHR_TBL_T *, the first row of the chain */
#define PTB_TAIL         0x0c   /* _PHR_TBL_T *, the last one */
#define PTB_NUMREAD      0x10   /* NumRead * */

/* Four pointers four bytes apart, so none of them can stay where IBM put it
   on a build where a pointer is eight bytes wide. Parked past the record, as
   every other record in this directory parks its own. */
#define PTB_ROOM         (PTB_BYTES + 4 * sizeof(void *))
#define PTB_VTABLE_AT    (PTB_BYTES + 0 * sizeof(void *))
#define PTB_OWNER_AT     (PTB_BYTES + 1 * sizeof(void *))
#define PTB_HEAD_AT      (PTB_BYTES + 2 * sizeof(void *))
#define PTB_TAIL_AT      (PTB_BYTES + 3 * sizeof(void *))
#define PTB_NUMREAD_AT   (PTB_BYTES + 4 * sizeof(void *))

/* ---- what FzkAccent is handed and what it answers --------------------- */

/* The two records the accent of a run of function words is worked out over.
   Both are the caller's and neither is allocated here, so what is known of
   them is what this one method reads and writes.

   The in-record: the phrase's own moras and accent, how many function words
   follow it and what kind of run they make, then one byte a word saying how
   many moras it runs, one more saying whether it ends a phrase, a pointer a
   word to the three-byte rule that says what it does to the accent, the
   reading of the whole thing, and one sixteen-bit value a word which is
   carried through to the answer untouched. */
#define AI_MORAS        0x00      /* uint8 */
#define AI_ACCENT       0x01      /* uint8 */
#define AI_WORDS        0x02      /* uint8 */
#define AI_KIND         0x03      /* uint8 */
#define AI_LEN          0x04      /* uint8 [], how long each word is */
#define AI_ENDS         0x13      /* uint8 [], whether each ends a phrase */
#define AI_RULE         0x24      /* uint8 *[], three bytes apiece; see the
                                     note below, since this is where IBM puts
                                     them and not where ours are */
#define AI_KANA         0x5f      /* uint8 [], the reading */
#define AI_AT79         0x79      /* uint8, three where the last word is a
                                     particle the accent runs back over */
#define AI_MARK         0x7a      /* int16 [], carried through untouched */

/* The out-record: the moras and the accent of each group the run broke into,
   the first pair being the phrase itself. */
#define AO_MORAS        0x00      /* uint8 */
#define AO_ACCENT       0x01      /* uint8 */
#define AO_LEN          0x02      /* uint8 [15] */
#define AO_ACC          0x11      /* uint8 [15], never past the length */
#define AO_MARK         0x20      /* int16 [15] */

/* How many groups either record holds, which is what every walk over them
   stops at. */
#define AI_N            16

/* Sixteen pointers four bytes apart at 0x24 would reach 0x64 and the reading
   starts at 0x5f, so on a build where a pointer is eight bytes wide they
   cannot stay there and cannot be widened in place either. They are parked
   past the record, as every other record in this directory parks its own.
   Where they are parked is not the record's own end but a good way past it.
   The named fields run to 0x98, but the reading at 0x5f is as long as the
   phrase and its function words and the accent is looked up in it by index,
   so the reader reads past 0x98 as a matter of course. A quarter of a
   kilobyte is left for that before the pointers begin. */
#define AI_BYTES        0x9c      /* what the named fields reach */
#define AI_TAIL         0x200     /* and how far the reading may run */
#define AI_ROOM         (AI_TAIL + AI_N * sizeof(void *))
#define AI_RULE_AT      (AI_TAIL)
#define AI_RULE_OF(in, i) \
    (((uint8_t **)((uint8_t *)(in) + AI_RULE_AT))[i])

#endif
