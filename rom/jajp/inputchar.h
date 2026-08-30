/* What InputChar is, as a record.
 *
 * InputChar is the front of the Japanese analyser. Text arrives at
 * TextAnalysis as bytes; InputChar splits it into characters, says what each
 * one is, remembers where each began, and hands the result to DictSearch and
 * to JPath as three parallel arrays of seven hundred and twenty-six. Nearly
 * everything the analyser does is an index into those arrays, so the record is
 * read by more classes than write it, and this is the header those readings
 * come from.
 *
 * Two things about it are worth stating before the numbers. The arrays are
 * bigger than the constructor clears -- see the comment on IC_KIND -- and the
 * class holds a chain of its own beside them, the SNLK table, which is how a
 * caller's own reading for a word already in the text is carried alongside the
 * text rather than in it.
 *
 * The offsets are IBM's, and rom/jajp/inputchar.c keeps them, because
 * DictSearch and RomUserDict reach into this record by offset rather than
 * through a call. Where a field is a pointer it cannot stay at IBM's offset in
 * a build where a pointer is eight bytes wide, so the three that are get
 * parked past the record; IC_OWNER_AT and the two after it are that, and
 * rom/jajp/dictsearch.h says at length why.
 */

#ifndef INPUTCHAR_H
#define INPUTCHAR_H

#include <stdint.h>

/* ---- the record ------------------------------------------------------ */

#define IC_BYTES        0x027b8  /* what TextAnalysis::initialize allocates */

#define IC_OWNER        0x00000  /* TextAnalysis *, the only field at nought */
#define IC_TEXT         0x00004  /* the characters, two bytes each */
#define IC_TEXT_N       726
#define IC_SCRATCH      0x005b0  /* where the unknown-kanji walk collects */
#define IC_SCRATCH_N    694
#define IC_KIND         0x00b1c  /* int32 [], what each character is */
#define IC_OFFSET       0x01674  /* int16 [], where each one starts */
#define IC_RAWPOS       0x01c20  /* int16, how far into the text the caller
                                    sent the reader has got -- IC_OFFSET is
                                    filled from it, and the two bytes of pad
                                    after it */
#define IC_MARK         0x01c24  /* int32 [], what a candidate carries away */
#define IC_COUNT        0x0277c  /* int16, how many characters there are */
#define IC_POS          0x02780  /* int32, the byte the reader has reached */
#define IC_ENDED        0x02784  /* int32, whether the sentence just read ended
                                    on something rather than running out */
#define IC_TEXTP        0x02788  /* const char *, the bytes themselves */
#define IC_RESUME       0x0278c  /* int32; nought means start a fresh sentence
                                    at character nought. Nothing in this class
                                    writes it -- TextAnalysis does */
#define IC_ENGRUN       0x02790  /* int32, a run of letters is open */
#define IC_NUMRUN       0x02794  /* int32, a run of digits is open */
#define IC_NUMJOIN      0x02798  /* int32, a number carries on across the
                                    break just found */
#define IC_BRACKET_AT   0x0279c  /* int16, where the last closing bracket was */
#define IC_ENDMARK      0x0279e  /* four bytes: the punctuation the sentence
                                    ended on, as a string */
#define IC_UNREAD_27A2  0x027a2  /* int16 nothing in the module touches */
#define IC_PAUSE        0x027a4  /* int32, the pauses the annotations asked
                                    for, added up */
#define IC_AT_END       0x027a8  /* int32, what IsEndOfInput answers */
#define IC_SNLK_TABLE   0x027ac  /* _SNLK_TABLE *, the head of the chain */
#define IC_LENGTH       0x027b0  /* int16, characters consumed before the
                                    text now in hand, and the pad after it */
#define IC_MORE         0x027b4  /* int32, the buffer ran out mid-sentence */

/* Where our build keeps the three pointers, since none of them can stay where
   IBM has it once a pointer is eight bytes wide: IC_OWNER would run over the
   start of the characters, IC_TEXTP over IC_SPARE_278C, and IC_SNLK_TABLE
   over IC_LENGTH -- which is a field DictSearch reads. */
#define IC_ROOM         (IC_BYTES + 3 * sizeof(void *))
#define IC_OWNER_AT     (IC_BYTES)
#define IC_TEXTP_AT     (IC_BYTES + sizeof(void *))
#define IC_SNLK_AT      (IC_BYTES + 2 * sizeof(void *))

/* The three arrays hold seven hundred and twenty-six characters and only
   IC_TEXT is cleared in full. The other three memsets pass 0x2d6 -- the count
   of characters -- where the size was wanted, so IC_OFFSET is cleared to the
   half and IC_KIND and IC_MARK to the quarter, and the rest is whatever the
   allocator left. It costs nothing while a sentence is short and it is IBM's,
   so it is kept. IC_KIND's fill is a byte for the same reason: it means to
   write KIND_OTHER into each int32 and writes 0x0c0c0c0c.
 *
 * IC_SCRATCH is smaller than the others -- six hundred and ninety-four
 * two-byte characters -- and the walk that fills it stops one entry too late,
 * so a text of that many collectable characters writes the last of them over
 * the first two bytes of IC_KIND. See ic_GetUnknownKanji. */

/* ---- what a character is --------------------------------------------- */

/* What InputChar::GetCharType answers, which is what IC_KIND holds. Read off
   that method rather than guessed: it is the only place the numbering is
   stated, and getting it wrong reads a katakana test as a kanji one.
 *
 * Nine is the default -- anything the classifier does not recognise is taken
 * for a kanji -- and it is also what an index before the start of the text
 * answers, so a walk that runs backwards off the beginning sees a kanji. */
#define KIND_KATAKANA   1
#define KIND_PUNCT      2        /* 0x8143 to 0x81ac */
#define KIND_LATIN      3        /* full width A-Z and a-z */
#define KIND_HIRAGANA   4
#define KIND_DIGIT      5        /* full width, the kanji numerals, and
                                    0x815a, which is none of those */
#define KIND_GREEK      6
#define KIND_ROMAN      7        /* the 0xfa40 extension */
#define KIND_CHOON      8        /* the long vowel bar, 0x815b */
#define KIND_KANJI      9        /* and anything unrecognised */
#define KIND_BRACKET    10       /* 0x816d */
#define KIND_NAKAGURO   11       /* the middle dot, 0x8145 */
#define KIND_OTHER      12
#define KIND_ENGWORD    13       /* tested by the English-word walk, and
                                    ic_CheckContext is what writes it: a run
                                    of full-width letters begun after a space
                                    is marked as an English word rather than
                                    left as KIND_LATIN. GetCharType cannot
                                    answer it, and ic_CheckContextForNum uses
                                    it as a value of its own without ever
                                    putting it in the array */

/* ---- the SNLK table -------------------------------------------------- */

/* One reading a caller gave for a stretch of the text it is about to send.
 * IBM calls the struct _SNLK_TABLE. The chain is kept in the order it was
 * added and is looked up by position, so it is walked rather than searched,
 * and DictSearch::Do reads SN_TRANS, SN_CHARS, SN_YOMI_N and SN_YOMI out of
 * whatever ic_GetSnlkTableAt hands back.
 *
 * Everything DictSearch reads is at or above SN_TRANS, which is what lets the
 * three pointers move to the end in our build without any of it shifting. */
#define SN_AT           0x0c     /* int16, which character it sits at */
#define SN_TRANS        0x0e     /* two bytes makeTransValue fills; 0xff until
                                    then, and Do tests for exactly that */
#define SN_CHARS        0x10     /* uint8, characters of the written form */
#define SN_YOMI_N       0x11     /* uint8, yomi codes, clamped to 25 */
#define SN_YOMI         0x12     /* uint8 [26] */
#define SN_BYTES        0x2c

#define SN_ROOM         (SN_BYTES + 3 * sizeof(void *))
#define SN_NEXT_AT      (SN_BYTES)
#define SN_KEY_AT       (SN_BYTES + sizeof(void *))
#define SN_VALUE_AT     (SN_BYTES + 2 * sizeof(void *))

/* Reaching one, which DictSearch does as well as InputChar. */
#define SN_P(n, off)    ((uint8_t *)(n) + (off))
#define SN_B(n, off)    (*SN_P(n, off))
#define SN_WORD(n, off) (*(int16_t *)SN_P(n, off))
#define SN_NEXT(n)      (*(void **)SN_P(n, SN_NEXT_AT))
#define SN_KEY(n)       (*(char **)SN_P(n, SN_KEY_AT))
#define SN_VALUE(n)     (*(char **)SN_P(n, SN_VALUE_AT))

/* ---- what InputChar reaches through ---------------------------------- */

/* Two of its methods go up to TextAnalysis, on to the romanizer above it, and
   then down again -- for the parameter block, to ask whether annotations are
   in the text, and for the user dictionary, to turn a caller's reading into
   the engine's codes. rom/jajp/romanizer.h is where that record is mapped, and
   it has to be IBM's own offsets because DictSearch reads two settings out of
   the same object. */
#include "romanizer.h"

#endif
