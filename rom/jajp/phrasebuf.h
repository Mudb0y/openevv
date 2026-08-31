/* What PhraseBuf is, as a record, and what a phrase in it looks like.
 *
 * PhraseBuf is where the path search's answers are turned into phrases: one
 * slot per way the sentence can be broken into accent phrases, each holding
 * the words that make it up with their readings, their accents and what kind
 * of phrase each word makes. It is almost all buffer -- 235,984 bytes of it,
 * which is exactly one of TextAnalysis's own three phrase buffers, and Copy
 * is what fills it from one of those.
 *
 * The size is settled: TextAnalysis::initialize asks operator new for
 * 0x399e4 bytes, and the last field sits four from the end of that.
 *
 * `tools/rom-offsets.py phrasebuf' holds this against the object, and what
 * that check is worth here is worth saying plainly. It proves the regions
 * tile the record exactly and that nothing in the object reaches past the
 * end of it. It does not separate a displacement on this record from one on
 * the dictionary search, whose own fields are at 0x58c8 and above and so
 * fall inside the working copy by arithmetic rather than by meaning. The
 * same bluntness applies to the other maps and the tool's own head says so.
 *
 * The phrase record is IBM's `_W_PHRASE_T' and is 0x158 bytes, the same
 * stride as a slot of TextAnalysis's buffers, which is what says the two are
 * the same thing. Its head is eight bytes about the phrase and the rest is up
 * to eighteen words of eighteen bytes each. What is not yet read is named as
 * a span so that the whole still tiles.
 */

#ifndef PHRASEBUF_H
#define PHRASEBUF_H

#include <stdint.h>

#define PB_BYTES        0x399e4   /* what TextAnalysis allocates */

#define PB_VTABLE       0x00000
#define PB_OWNER        0x00004   /* TextAnalysis * */

/* The working copy of one of the owner's three phrase buffers. Copy memcpys
   0x399d0 bytes into here, which is that buffer to the byte. */
#define PB_BUFFER       0x00008
#define PB_BUFFER_SIZE  0x399d0
#define PB_SLOT_N       686       /* 0x2ae, the bound every writer checks */
#define PB_SLOT_SIZE    0x158

#define PB_TAIL         0x399d8   /* four bytes nobody has read */
#define PB_SEARCH       0x399dc   /* DictSearch *, the owner's own */
#define PB_JPATH        0x399e0   /* JPath *, the owner's own */

/* Four pointers, none of which can stay where IBM put it once a pointer is
   eight bytes wide: the vtable and the owner are four apart, and so are the
   search and the path, which also sits four from the end of the record. */
#define PB_ROOM         (PB_BYTES + 4 * sizeof(void *))
#define PB_VTABLE_AT    (PB_BYTES + 0 * sizeof(void *))
#define PB_OWNER_AT     (PB_BYTES + 1 * sizeof(void *))
#define PB_SEARCH_AT    (PB_BYTES + 2 * sizeof(void *))
#define PB_JPATH_AT     (PB_BYTES + 3 * sizeof(void *))

/* ---- one phrase ------------------------------------------------------ */

/* The head. WP_MORAS is what SetJrt counts up over the words on the path and
   SetPhrasePart writes; WP_TYPE is what GetSpecialPhraseType decides. */
#define WP_MORAS        0x00      /* uint16 */
#define WP_CHARS        0x02      /* uint8 */
#define WP_TYPE         0x03      /* uint8, the phrase kind */
#define WP_WORDS        0x04      /* uint8, how many words are in it */
#define WP_KANALEN      0x05      /* uint8, the reading over all of them */
#define WP_UNREAD_06    0x06      /* uint8 */
#define WP_ACCENT       0x07      /* uint8, which mora carries it */
#define WP_COST         0x08      /* int32, taken off the sub-word */

/* And the words, eighteen bytes each. */
#define WP_WORD         0x0c
#define WP_WORD_N       18        /* (0x158 - 0xc) / 0x12, rounded down */
#define WP_WORD_SIZE    0x12

#define WW_KANALEN      0x00      /* uint8, from JS_KANALEN */
#define WW_UNREAD_01    0x01      /* uint8 */
#define WW_ACCENT       0x02      /* int16, from JS_ACCENT */
#define WW_CHARS        0x04      /* uint8, from JS_CHARS */
#define WW_ATTR         0x05      /* uint8, from JS_ATTR */
#define WW_KANA         0x06      /* uint8 [9], from JS_KANA */
#define WW_KANA_N       9
#define WW_POS          0x0f      /* uint8, the part of speech or a stand-in */
#define WW_OFFSET       0x10      /* int16, from JS_OFFSET */

/* The function words that follow a phrase, ten bytes each, written by
   SetPhrasePart out of DictSearch's own function-word table. */
#define WP_FZK          0x0c0
#define WP_FZK_SIZE     0x00a

/* Where each of these comes from is SetPhrasePart, and what WF_AT means is
   ChkTTELink, which is the only other reader: it uses it as an index into the
   function-word dictionary. The two agreeing is what settles the record --
   read from SetPhrasePart alone the second field would have been put at six,
   which is where its source sits in DictSearch's own table rather than where
   it lands here. */
#define WF_CODE         0x00      /* uint8, from the table's own +4; the top
                                     bit marks the first of a run */
#define WF_AT           0x02      /* int16, from +6, into the dictionary */
#define WF_KANALEN      0x04      /* uint8, from +1 */
#define WF_ACCENT       0x06      /* int16, from +8 */
#define WF_OFFSET       0x08      /* int16, from +0x0c */

/* Reaching into it. */
#define PB_P(pb, off)   ((uint8_t *)(pb) + (off))
#define PB_S16(pb, off) (*(int16_t *)PB_P((pb), (off)))

/* The accessors are _SLOT rather than _AT because WF_AT is a field of the
   record and a function-like macro of the same name would shadow it. */
#define WP_SLOT(base, i) ((uint8_t *)(base) + (i) * PB_SLOT_SIZE)
#define WW_SLOT(w, i)    ((uint8_t *)(w) + WP_WORD + (i) * WP_WORD_SIZE)
#define WF_SLOT(w, i)    ((uint8_t *)(w) + WP_FZK + (i) * WP_FZK_SIZE)

#endif
