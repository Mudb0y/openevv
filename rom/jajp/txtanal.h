/* The shape of TextAnalysis, which is the spine of the Japanese analyser.
 *
 * Why this file exists before the code that uses it. `Romanizer' allocates one
 * of these in a single lump of 946,216 bytes, and every other class in the
 * analyser is handed a reference to it: `DictSearch', `InputChar', `JPath',
 * `PhraseBuf', `Annotation' and `RomUserDict' all take a `TextAnalysis&' in
 * their constructors and read its fields directly. So not one of them can be
 * written -- or even constructed in a test harness -- until this record is
 * known. Mapping it was the whole of the work this file records.
 *
 * How it was read. The constructor and `initialize' lay out the head: they name
 * the six sub-objects and how big each is. `InitPhraseTable' lays out the tail,
 * because it fills the link chain in and the arithmetic in it says where that
 * chain begins and how long it is. The middle came from the arithmetic in
 * `CheckPhraseLink', which reaches a phrase buffer as `this + 0x900 +
 * buffer * 0x399d0 + slot * 0x158' -- three buffers of 686 slots each, and
 * three times 0x399d0 from 0x900 lands exactly on the next named field, which
 * is what says there are three and not two or four.
 *
 * What is settled and what is not is marked field by field below.
 * tools/rom-offsets.py holds this against IBM's own object: it pulls every
 * offset txtanal.obj uses on a TextAnalysis pointer and refuses any that does
 * not fall inside a region named here. Run it after adding a field.
 *
 * Nothing here is a transcription yet. It is what a transcription has to
 * agree with.
 */

#ifndef TXTANAL_H
#define TXTANAL_H

#include <stdint.h>

/* ---- the sizes IBM's own code allocates ------------------------------ */

/* Each is the argument to operator new in TextAnalysis::initialize, except the
   first, which is what Romanizer::Romanizer asks for. */
#define TA_BYTES            0xe7228   /* 946,216 */
#define TA_INPUTCHAR_BYTES  0x027b8   /*  10,168 */
#define TA_ANNOTATION_BYTES 0x0050c   /*   1,292 */
#define TA_DICTSEARCH_BYTES 0x08908   /*  35,080 */
#define TA_JPATH_BYTES      0x07cec   /*  31,980 */
#define TA_PHRASEBUF_BYTES  0x399e4   /* 235,996 */
#define TA_PHRASETABLE_BYTES 0x00014  /*      20 */
#define TA_NORMALIZER_BYTES 0x00014   /*      20 */

/* ---- where everything sits ------------------------------------------- */

/* The head: a vtable, the romanizer that owns it, the text as it arrives, and
   the six objects it makes. Settled: the constructor and initialize write
   every one of these and nothing else writes them. */
#define TA_VTABLE       0x00000
#define TA_OWNER        0x00004   /* Romanizer * */
#define TA_FORMATTED    0x00008   /* char *, what FormatAddText left */
#define TA_DONE         0x0000c   /* int32, cleared after text arrives */
#define TA_UNKNOWN_10   0x00010   /* nothing writes or reads it */
#define TA_INPUTCHAR    0x00014   /* InputChar * */
#define TA_ANNOTATION   0x00018   /* Annotation * */
#define TA_DICTSEARCH   0x0001c   /* DictSearch * */
#define TA_JPATH        0x00020   /* JPath * */
#define TA_PHRASEBUF    0x00024   /* PhraseBuf * */
#define TA_PHRASETABLE  0x00028   /* PhraseTable * */

/* The parse's own marks, cleared at the top of TextParsing and read at
   several widths -- bytes at 0x2c and 0x302, words and longs between. Not
   settled: what the fields inside it are. It is one region here so that the
   checker has somewhere to put them. */
#define TA_MARKS        0x0002c
#define TA_MARKS_END    0x005d8

/* Three of something, eight bytes each, one per phrase buffer:
   SetPhraseMakeTable walks it with a bound of three and a scale of eight.
   Not settled: what the eight bytes are. */
#define TA_PERBUF       0x005d8
#define TA_PERBUF_N     3
#define TA_PERBUF_SIZE  8

/* Between that and the buffers, and settled by DictSearch::SetLongWord and
   TextAnalysis::AddLongWord together. A reading too long for the ten bytes a
   candidate entry holds is put here instead and the entry keeps its number:
   thirty of twenty-six bytes, and thirty of them reach the count that follows
   exactly. Two bytes in front of it and one behind are unaccounted for. */
#define TA_SPARE        0x005f0
#define TA_LONGWORD     0x005f2
#define TA_LONGWORD_N   30        /* 0x1e, what SetLongWord refuses to pass */
#define TA_LONGWORD_SIZE 0x1a     /* 30 times 26 from 0x5f2 is 0x8fe */
#define TA_LONGWORDS    0x008fe   /* uint8, how many are in use */
#define TA_SPARE_8FF    0x008ff
#define TA_SPARE_END    0x00900

/* The three phrase buffers, which is where the candidate words for a stretch
   of text are built. Settled: CheckPhraseLink's own arithmetic gives the base,
   both strides and, by landing exactly on TA_USED, the count.

       this + 0x900 + buffer * 0x399d0 + slot * 0x158                       */
#define TA_BUFFERS      0x00900
#define TA_BUFFER_N     3
#define TA_BUFFER_SIZE  0x399d0   /* 235,984 */
#define TA_SLOT_N       686       /* 0x399d0 / 0x158, exactly */
#define TA_SLOT_SIZE    0x158     /* 344 */

/* How much of each buffer is used, and the total. Settled: the loop that
   clears the first has a bound of three, and the second is written once with
   a running count. */
#define TA_USED         0xad670   /* int16 [3] */
#define TA_COUNT        0xad676   /* int16 */

/* A working area CheckPhraseLink takes the address of and walks. Not settled:
   its shape or its length. It runs to the link chain. */
#define TA_WORK         0xad678
#define TA_WORK_END     0xadd30

/* The link chain over the phrase table: two sixteen-bit indices to an entry,
   the same shape JpnUtil::TableFree splices. Settled: InitPhraseTable fills
   every entry and the arithmetic says where it starts and stops.

   Note the chain is one entry per phrase-table row and is initialised
   circular: entry nought's back link is the count rather than minus one. */
#define TA_LINK         0xadd30
#define TA_LINK_N       707       /* 0x2c3, what ClearPhraseTable asks for */
#define TA_LINK_SIZE    4

/* The phrase table proper, which is what the analysis leaves behind and what
   Romanizer reads to make the output. Settled: initialize memsets exactly
   0x389d8 bytes here, which is 707 times 0x148 to the byte. */
#define TA_PHRASE       0xae83c
#define TA_PHRASE_N     707
#define TA_PHRASE_SIZE  0x148     /* 344 -- the same stride as a buffer slot */

/* The tail. Settled: InitPhraseTable writes the first four and SetText and
   AppendText the rest. */
#define TA_FIRST        0xe7214   /* int16, the count as given */
#define TA_LAST         0xe7216   /* int16, the same */
#define TA_SPARE_18     0xe7218   /* int16, zeroed */
#define TA_TOP          0xe721a   /* int16, the count less one */
#define TA_RAW_LEN      0xe721c   /* int32 */
#define TA_RAW          0xe7220   /* char *, memset to 0xff then filled */
#define TA_NORMALIZER   0xe7224   /* TextNormalizer * */

/* ---- what the sub-objects hold that this class reads ----------------- */

/* Offsets into InputChar that TextAnalysis reads directly rather than through
   a call. rom/jajp/inputchar.h names the whole of that record; it is included
   because the checker sees those offsets on a different base and would
   otherwise flag them. */
#include "inputchar.h"

/* And into DictSearch. */
#define DS_COUNT        0x080ac   /* int16, what SetNextPhraseBuffer reads */

/* And into PhraseBuf. */
#define PB_TAIL         0x399d8

/* ---- where our build keeps the pointers ------------------------------ */

/* Every one of the sub-object pointers sits four bytes from the next, so on a
   build where a pointer is eight bytes wide no two of them can stay at IBM's
   offsets: writing the annotation would run over the dictionary search, and
   the raw text over the normalizer. They are parked past the record, as
   DictSearch's, InputChar's and Romanizer's are.
 *
 * Nothing had noticed because the harness only ever set two or three of them
 * at a time; setting a third overwrote the second's upper half and DictSearch
 * followed a wild pointer on its first call. */
#define TA_ROOM           (TA_BYTES + 10 * sizeof(void *))
#define TA_VTABLE_AT      (TA_BYTES)
#define TA_OWNER_AT       (TA_BYTES + 1 * sizeof(void *))
#define TA_FORMATTED_AT   (TA_BYTES + 2 * sizeof(void *))
#define TA_INPUTCHAR_AT   (TA_BYTES + 3 * sizeof(void *))
#define TA_ANNOTATION_AT  (TA_BYTES + 4 * sizeof(void *))
#define TA_DICTSEARCH_AT  (TA_BYTES + 5 * sizeof(void *))
#define TA_JPATH_AT       (TA_BYTES + 6 * sizeof(void *))
#define TA_PHRASEBUF_AT   (TA_BYTES + 7 * sizeof(void *))
#define TA_PHRASETABLE_AT (TA_BYTES + 8 * sizeof(void *))
#define TA_RAW_AT         (TA_BYTES + 9 * sizeof(void *))

#endif
