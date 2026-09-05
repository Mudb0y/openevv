/* What IntonPhrase is, as a record.
 *
 * IntonPhrase is the intonation. PhraseBuf and the path search leave a table
 * of accent phrases; this groups those into breath groups -- the stretches a
 * speaker says in one breath -- decides where the intonational phrases end,
 * and says how long each pause runs and what pitch each phrase carries.
 *
 * The size is settled: `Romanizer''s constructor asks for 0x69a4c bytes and
 * keeps the answer at RZ_INTON. Two of the three regions inside it are
 * settled by arithmetic rather than by any single line, which is the only
 * way a record of this shape can be read. `InitPhraseTable' lays out a link
 * chain of 707 entries at four bytes each from 0x14, which ends at 0xb20;
 * 707 records of 0x260 from 0xb20 end at 0x69a40, which is exactly where the
 * counters it writes sit. Neither number is chosen: 707 is the same count
 * TextAnalysis's own link chain and phrase table hold, so this is one record
 * per phrase of the sentence.
 *
 * tools/rom/offsets.py holds this against IBM's own object.
 */

#ifndef INTONPHRASE_H
#define INTONPHRASE_H

#include <stdint.h>

#define IP_BYTES        0x69a4c   /* 432,204, what Romanizer asks for */

#define IP_VTABLE       0x00000   /* the one slot, a destructor */
#define IP_OWNER        0x00004   /* Romanizer * */
#define IP_HEAD         0x00008   /* the first breath group in use */
#define IP_CUR          0x0000c   /* and the one being worked on */
#define IP_TABLE        0x00010   /* _PHR_TBL_T *, what it is handed */

/* The link chain over the breath groups, two sixteen-bit indices to an
   entry: the same shape JpnUtil::TableFree splices and TextAnalysis holds
   one of. Initialised circular in the same way -- entry nought's back link
   is the count rather than minus one. */
#define IP_LINK         0x00014
#define IP_LINK_N       707
#define IP_LINK_SIZE    4

#define IL_PREV         0x00      /* int16 */
#define IL_NEXT         0x02      /* int16 */

/* The breath groups themselves, one per phrase the sentence could have.
   Chained through their own first four bytes, which is what IP_HEAD walks. */
#define IP_GROUP        0x00b20
#define IP_GROUP_N      707
#define IP_GROUP_SIZE   0x260

/* One breath group: a next pointer, three phrases, and ten bytes after
   them. Three is IBM's own bound -- `BreathGroupAlloc' clears exactly three
   -- and three records of 0xc6 from four end on 0x256, which is where the
   fields the passes read begin. */
#define IG_NEXT         0x000     /* the next in the chain */
#define IG_PHRASE       0x004
#define IG_PHRASE_N     3
#define IG_PHRASE_SIZE  0x0c6
#define IG_TAIL         0x256     /* 0x004 plus three times 0xc6 */
#define IG_PAUSE        0x256     /* int16, how long a pause follows the group,
                                     which is what SetPauseLength works out */
#define IG_INDEX        0x258     /* int16, the group's own place */
#define IG_PHRASES      0x25b     /* uint8, how many phrases are in it */
#define IG_LEVEL        0x25a     /* uint8, the group's own pause weight, and
                                     the term every arm of SetPauseLength's
                                     lookahead is built out of */
#define IG_LEFT         0x25c     /* uint8, the boundary flags on its left */
#define IG_RIGHT        0x25d     /* uint8, and on its right */
#define IG_SPARE_25E    0x25e
#define IG_KIND         0x25f     /* uint8, what kind of boundary closes it:
                                     two takes the pause given from outside,
                                     three a flat hundred and twenty-five, and
                                     four or more a full second */

/* One phrase inside a breath group. What `BreathGroupAlloc' clears is what
   says where the runs are: five of ten bytes each, one byte on its own,
   thirty int16s and thirty bytes. The spans between them are not read by
   anything seen so far and are named as unread. */
#define IH_FIRST        0x00      /* uint8, one on the group's first phrase */
#define IH_COUNT        0x01      /* uint8, how many slots are in use */
#define IH_A            0x02      /* uint8 [10] */
#define IH_MORAS        0x0c      /* uint8 [10] */
#define IH_LEN          0x16      /* uint8 [10], how long the run is, which
                                     SetAccentualPhrase adds up */
#define IH_VAL          0x20      /* int16 [10], from PT_MORA_VAL */

#define IH_FLAG         0x34      /* uint8, the byte in front of the run at
                                     0x35, which BreathGroupAlloc clears on
                                     its own and SetAccentualPhrase reaches as
                                     that run's slot less one */
#define IH_PITCH        0x35      /* uint8 [10] */
#define IH_MARK         0x3f      /* uint8 [10] */
#define IH_KANA_LEN     0x49      /* uint8, how many codes the reading runs */
#define IH_KANA         0x4a      /* uint8 [28], the reading, 0xff-terminated.
                                     Twenty-eight rather than thirty, and that
                                     is settled from outside this class:
                                     ProsCtrl::BG_T2BreathGroups reads an
                                     int16 at 0x66, which is 0x4a plus
                                     twenty-eight. Note SetIntonationalPhrase
                                     can run past it, since what it holds
                                     accumulates over a group's phrases and
                                     nothing bounds it -- IBM's, and kept. */
#define IH_AT66         0x66      /* int16. Nothing in this class writes or
                                     reads it; ProsCtrl copies it into the
                                     accent phrase it builds. */
#define IH_LONG_N       0x68      /* int16, how many of the pair below are set */
#define IH_E            0x6a      /* int16 [30], filled with minus one */
#define IH_F            0xa6      /* uint8 [30], filled with 0xff */
#define IH_UNREAD_C4    0xc4      /* two bytes; 0xa6 plus 30 is 0xc4 */
#define IH_RUN_N        10
#define IH_E_N          30

/* What InitPhraseTable leaves for the passes that follow. */
#define IP_COUNT        0x69a40   /* int16, how many phrases there are */
#define IP_LEFT         0x69a42   /* int16, the same to start with */
#define IP_AT           0x69a44   /* int16, cleared */
#define IP_TOP          0x69a46   /* int16, the count less one */
#define IP_MORE         0x69a48   /* int16, the pause a group of kind two
                                     takes. Nothing in this class writes it,
                                     so it comes from outside. */
#define IP_SPARE        0x69a4a   /* two bytes nobody has read */

/* Three of the five pointers cannot stay where IBM put them on a build where
   a pointer is eight bytes wide: the vtable, the owner and the three that
   follow are four bytes apart. They are parked past the record, as every
   other record in this directory parks its own. */
#define IP_ROOM         (IP_BYTES + 5 * sizeof(void *))
#define IP_VTABLE_AT    (IP_BYTES + 0 * sizeof(void *))
#define IP_OWNER_AT     (IP_BYTES + 1 * sizeof(void *))
#define IP_HEAD_AT      (IP_BYTES + 2 * sizeof(void *))
#define IP_CUR_AT       (IP_BYTES + 3 * sizeof(void *))
#define IP_TABLE_AT     (IP_BYTES + 4 * sizeof(void *))

/* And the record it is handed, which is PhraseTable's rather than this
   class's: a chain of phrases with a next pointer at nought. Only the fields
   IntonPhrase reads are named here, because PhraseTable is not written yet
   and this is what one transcription has to agree with. */
#define PT_ROW_SIZE     0x148     /* what TextAnalysis packs them at */
#define PT_NEXT         0x000     /* the link, four bytes; see below */
#define PT_INDEX        0x004     /* int16, its own place in the table, which
                                     is what JpnUtil::TableFree gives back */
#define PT_STATE        0x006     /* uint8 [3], what CheckPhraseToPhrase and
                                     PhraseParsing work on */
#define PT_GROUP        0x009     /* uint8, what CheckBreathGroup answers */
#define PT_KIND         0x00a     /* uint8. PhraseSeparate writes one here
                                     where it broke a group, and reads it back
                                     on the group's last phrase, taking four or
                                     more to mean the group has been broken
                                     enough times to widen the target run. */
#define PT_LEFT         0x0dd     /* uint8 [], the phrase before this one */
#define PT_RIGHT        0x0e1     /* uint8 [], and this one */
#define PT_LONG         0x0ee     /* int16 [30], minus one where unset */
#define PT_LONG_B       0x12a     /* uint8 [30], and 0x12a plus thirty is the
                                     0x148 a row runs to */
#define PT_MORAS        0x00b     /* uint8, how many codes the reading runs,
                                     which is also what RegroupPhrases adds up
                                     as the length of a breath group */
#define PT_FIRST_WORD   0x00f     /* uint8, which word of the phrase
                                     buffer this row starts at, which is
                                     what PhraseTable writes and
                                     CompoundWord walks from */
#define PT_HOLD         0x00c     /* uint8, added into the group phrase's
                                     IH_FLAG as each row arrives */
/* The moras of the phrase, as parallel runs of fifteen. Settled, and settled
   by tiling rather than by any one line: four byte runs from 0x010, a run of
   fifteen int16s, four more byte runs, a second run of fifteen int16s, and
   the reading. That is 0x010 to 0x0c4 with nothing left over and nothing
   overlapping, and it is the only arrangement of what the code touches that
   comes out even.
 *
 * The low four runs and the high four are two halves of one run of thirty,
 * which is what SetPitchValues' test `is the high half empty' is for: it is
 * how the pass finds which half the phrase's last mora is in. Thirty is also
 * what a breath group's own phrase holds thirty of, at IH_E and IH_F. */
#define PT_MORA         0x010     /* uint8 [15], the mora codes */
#define PT_MORA_ACC     0x01f     /* uint8 [15], read against the code */
#define PT_MORA_PITCH   0x02e     /* uint8 [15], three or four */
#define PT_MORA_MARK    0x03d     /* uint8 [15], one, nought, or two on the
                                     phrase's last mora */
#define PT_MORA_VAL     0x04c     /* int16 [15] */
#define PT_MORA_HI      0x06a     /* and the high half of the thirty */
#define PT_MORA_HI_ACC  0x079
#define PT_MORA_HI_PITCH 0x088
#define PT_MORA_HI_MARK 0x097
#define PT_MORA_HI_VAL  0x0a6     /* int16 [15] */
#define PT_MORA_N       15

#define PT_KANA         0x0c4     /* uint8 [], the reading, which CheckChoon
                                     reads one code of */

/* ---- the two chains -------------------------------------------------- */

/* A breath group's chain link sits at IBM's offset nought and the group's
 * first phrase begins at four; a phrase-table row's link sits at nought and
 * the row's own index at four. So on a build where a pointer is eight bytes
 * wide neither link can hold a pointer without running over the field behind
 * it, and neither record has slack to park one in: groups are 707 entries of
 * 0x260 packed inside this record, and rows are 707 entries of 0x148 packed
 * inside TextAnalysis's.
 *
 * What goes in each link instead is how many records forward the next one is,
 * signed, which is thirty-two bits on every build. Nought is the end of the
 * chain exactly as IBM's null pointer is, since a record cannot link to
 * itself, so nothing has to be spelt differently for the two builds. It needs
 * only that the records of one chain come out of one strided array, which is
 * where both of these live anyway.
 *
 * PhraseTable is not written yet. When it is, this is the shape its writer
 * has to produce. */
#define IG_LINK(bg)     (*(int32_t *)(bg))
#define IG_NEXT_OF(bg)  (IG_LINK(bg) == 0 ? NULL \
                         : (void *)((uint8_t *)(bg) \
                                    + (long)IG_LINK(bg) * IP_GROUP_SIZE))
#define IG_NEXT_SET(bg, p) \
    (IG_LINK(bg) = ((p) == NULL ? 0 \
                    : (int32_t)(((uint8_t *)(p) - (uint8_t *)(bg)) \
                                / IP_GROUP_SIZE)))

#define PT_LINK(t)      (*(int32_t *)(t))
#define PT_NEXT_OF(t)   (PT_LINK(t) == 0 ? NULL \
                         : (void *)((uint8_t *)(t) \
                                    + (long)PT_LINK(t) * PT_ROW_SIZE))
#define PT_NEXT_SET(t, p) \
    (PT_LINK(t) = ((p) == NULL ? 0 \
                   : (int32_t)(((uint8_t *)(p) - (uint8_t *)(t)) \
                               / PT_ROW_SIZE)))

/* Reaching into it. */
#define IP_P(ip, off)     ((uint8_t *)(ip) + (off))
#define IP_B(ip, off)     (*IP_P((ip), (off)))
#define IP_S16(ip, off)   (*(int16_t *)IP_P((ip), (off)))
#define IP_U16(ip, off)   (*(uint16_t *)IP_P((ip), (off)))
#define IP_LINK_AT(ip, i) IP_P((ip), IP_LINK + (i) * IP_LINK_SIZE)
#define IP_GROUP_AT(ip, i) IP_P((ip), IP_GROUP + (i) * IP_GROUP_SIZE)

#endif
