/* The shape of DictSearch, as far as it has been read.
 *
 * This is a partial map and says so field by field. `TextAnalysis' is fully
 * mapped in txtanal.h because its own code lays every region out; DictSearch
 * is not, because most of its 35,080 bytes are working buffers reached by
 * arithmetic rather than by a constant, and the arithmetic only pins down the
 * regions its own code clears or indexes with a known bound. What is here is
 * what the code proves; what is not is marked unresolved with its exact
 * bounds, so the next reader knows where to look and the checker has
 * somewhere to put an offset.
 *
 * Where each resolved region came from:
 *
 *   the head            the constructor, which writes three fields and stops
 *   the entries         Do clears 0x58c0 bytes from offset eight and then
 *                       writes a marker into 710 entries of thirty-two, and
 *                       710 times thirty-two is 0x58c0 to the byte
 *   the function words  memset of 0x27b4 in FzkParsingReverse, a stride of 14
 *                       in LookupFuncWordDict, and a bound of 0x2d6 there --
 *                       726 times 14 is 0x27b4 to the byte
 *   the three records   indexed with a shift of four, and three of them reach
 *                       the count that follows exactly
 *   the readings        memset of 0x258 in GenerateKanaString and a stride of
 *                       0x14 in SearchTankanTable -- thirty of twenty bytes
 *   the four arrays     their strides in GenerateKanaString, and the four of
 *                       them reach the count after them exactly
 *
 * tools/rom-offsets.py checks it the way it checks the spine: every offset
 * IBM's own code uses on one of these has to fall inside a region named here,
 * and the regions have to tile the object exactly.
 */

#ifndef DICTSEARCH_H
#define DICTSEARCH_H

#include <stdint.h>

#define DS_BYTES        0x8908   /* 35,080, what TextAnalysis::initialize asks */

/* ---- the head -------------------------------------------------------- */

/* Settled: the constructor writes these two and the input reader at the far
   end, and nothing else. */
#define DS_VTABLE       0x0000
#define DS_OWNER        0x0004   /* TextAnalysis * */

/* Settled, and by two arguments that agree. Do clears 0x58c0 bytes from
   offset eight, which is where this ends; and the loop after it writes minus
   one into a field at +0x1a of 710 entries of thirty-two bytes, and 710 times
   thirty-two is 0x58c0 to the byte. This is where the candidate words for the
   stretch of text being analysed are built. */
#define DS_ENTRY        0x0008
#define DS_ENTRY_N      710      /* 0x2c6, the bound in Do */
#define DS_ENTRY_SIZE   32       /* the shift of five that indexes it */
#define DS_ENTRY_MARK   0x1a     /* the field Do sets to minus one */

/* Settled. One entry per function word the parse is carrying. */
#define DS_FZK          0x58c8
#define DS_FZK_N        726      /* 0x2d6, the bound in LookupFuncWordDict */
#define DS_FZK_SIZE     14       /* 0xe; 726 times 14 is the memset exactly */

/* Settled: indexed with a shift of four, and three of them reach the count. */
#define DS_REC          0x807c
#define DS_REC_N        3
#define DS_REC_SIZE     16

/* Settled: TextAnalysis reads this one directly, in SetNextPhraseBuffer. */
#define DS_COUNT        0x80ac   /* int16 */

/* Two bytes between the count and the text buffer that nothing touches --
   alignment, most likely, since what follows is copied two bytes at a time. */
#define DS_W_80AE       0x80ae

/* The text GetTextBuf copies out for a lookup: up to five two-byte characters
   and a terminator. Its extent is not settled -- only the first eleven bytes
   are ever touched -- so the rest of the span up to the readings is
   unresolved and named as one. */
#define DS_TEXT             0x80b0
#define DS_UNREAD_MID       0x80bc
#define DS_UNREAD_MID_END   0x8150

/* Settled: cleared whole and indexed with a stride of twenty. One reading per
   candidate, twenty bytes of kana each, which is what GenerateKanaString
   builds and SearchTankanTable looks up. */
#define DS_READING      0x8150
#define DS_READING_N    30
#define DS_READING_SIZE 0x14     /* 30 times 20 is the memset exactly */

/* Four arrays with one slot per candidate, thirty of each, which
   GenerateKanaString fills in as it walks the text. Settled by their strides
   and by the fact that the four reach exactly the count that follows them:
   thirty bytes and three times sixty is 0xd2, and 0x83a8 plus 0xd2 is 0x847a.
 *
 * A caution for whoever transcribes that function. It clears each of the four
 * with a memset of thirty bytes, which is the whole of the first and half of
 * each of the others. Reproduce that rather than tidying it: a slot past the
 * fifteenth starts out holding whatever was there before, and only the count
 * below keeps the reads inside what was cleared. */
#define DS_MARK         0x83a8   /* uint8 [30], a flag per candidate */
#define DS_CHARS        0x83c6   /* int16 [30], how many characters */
#define DS_LEN          0x8402   /* int16 [30], how many bytes */
#define DS_TAKEN        0x843e   /* int16 [30], set to one when used */
#define DS_CAND_N       30

/* How many candidates there are, which is what bounds the four above. */
#define DS_NCAND        0x847a   /* int16 */

/* Settled: what LookupKanaDict found for one kanji, before it is spread over
   the candidates. WriteKanaData writes at most five readings from a base of
   nought or five, so there are ten slots; the three arrays reach exactly as
   far as the word below them, which is what says twelve bytes and ten of
   them. */
#define DS_KANA         0x847c   /* uint8 [10][12], the reading itself */
#define DS_KANA_N       10
#define DS_KANA_SIZE    12       /* 0xc, the stride in WriteKanaData */
#define DS_KANA_CHARS   0x84f4   /* uint8 [10], characters of text each took */
#define DS_KANA_LEN     0x84fe   /* uint8 [10], bytes of kana each is */

/* Words, and what six of them are for: GenerateWord uses the first as the
   cursor into the entries, GetTextBuf writes how many characters it copied
   and where in the text it started and stopped, and GenerateKanaString keeps
   a running total and counts the hiragana runs. */
#define DS_CURSOR       0x8508   /* int16, the next candidate entry to write */
#define DS_COPIED       0x850a   /* int16, what GetTextBuf copied */
#define DS_RUNS         0x850c   /* int16, hiragana runs this word */
#define DS_TOTAL        0x850e   /* int16, characters accounted for so far */
#define DS_FROM         0x8510   /* int16, where the lookup starts */
#define DS_TO           0x8512   /* int16, and where it stops */

/* Settled: the constructor's third write. */
#define DS_INPUTCHAR    0x8514   /* InputChar *, the owner's own */

/* Not resolved. */
#define DS_UNREAD_TAIL      0x8518
#define DS_UNREAD_TAIL_END  0x8900

/* Settled as scalars: Do writes the first and tests it against one. */
#define DS_L_8900       0x8900   /* int32 */
#define DS_L_8904       0x8904   /* int32 */

/* ---- what InputChar holds that DictSearch reads --------------------- */

/* Its own file will name these properly. They are here because whoever reads
   DictSearch meets them at once and would otherwise have to work them out
   again. */
#define IC_TEXT         0x00004  /* the characters, two bytes each */
#define IC_KIND         0x00b1c  /* int32 [], what each character is */
#define IC_OFFSET       0x01674  /* int16 [], where each one starts */
#define IC_MARK         0x01c24  /* int32 [], what a candidate carries away */
#define IC_COUNT        0x0277c  /* int16, how many there are */

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
#define KIND_DIGIT      5        /* full width, and the kanji numerals */
#define KIND_GREEK      6
#define KIND_ROMAN      7        /* the 0xfa40 extension */
#define KIND_CHOON      8        /* the long vowel bar, 0x815b */
#define KIND_KANJI      9        /* and anything unrecognised */
#define KIND_BRACKET    10       /* 0x816d */
#define KIND_NAKAGURO   11       /* the middle dot, 0x8145 */
#define KIND_OTHER      12

/* ---- the records the dictionary is made of --------------------------- */

/* A candidate word. The Process pair build one on the stack and WriteGWDict
   writes one into DS_ENTRY, and the two are the same thirty-two bytes -- IBM
   calls the first _DICTENT_T and the second is what DS_ENTRY_SIZE measures. */
#define DE_ACCENT       0x00     /* int16, which mora carries the accent */
#define DE_KANALEN      0x02     /* uint8, bytes of reading */
#define DE_CHARS        0x03     /* uint8, characters of text it covers */
#define DE_HIRAGANA     0x04     /* uint8, how many of those are hiragana */
#define DE_POS          0x05     /* uint8, the part of speech */
#define DE_ATTR         0x06     /* uint8 */
#define DE_ATTR2        0x07     /* uint8 */
#define DE_KANA         0x08     /* uint8 [10], and the rest via SetLongWord */
#define DE_AT           0x12     /* int16, where in the text it starts */
#define DE_MARK         0x14     /* int32, copied out of IC_MARK */
#define DE_OFFSET       0x18     /* int16, copied out of IC_OFFSET */
#define DE_LINK         0x1a     /* int16, the field Do sets to minus one */
#define DE_COST         0x1c     /* int32 */

/* A node of the kanji trie, keyed by one byte of the reading. The entries
   that hang off it are _DCTB_ENT, as many as the high nibble of the third
   byte says. */
#define DH_BYTE         0        /* uint8, the reading byte this node is */
#define DH_CHILD        1        /* uint8, bytes to the next node down */
#define DH_FLAGS        2        /* uint8, high nibble set if words end here,
                                    low nibble is the sibling delta's high */
#define DH_SIBLING      3        /* uint8, and its low */
#define DH_ENTRY        4        /* the _DCTB_ENT list */

/* One of those entries: which page of the word dictionary, where in it, and
   the kanji themselves. */
#define DB_COUNT        0        /* uint8, high nibble how many kanji,
                                    low nibble the page's high bits */
#define DB_PAGE         1        /* uint8, and its low */
#define DB_OFFSET       2        /* int16, where in the page */
#define DB_KANJI        4        /* the characters, two bytes each */

/* And a word in that page. The kana run on from the fourth byte, as many as
   the low nibble of the first says. */
#define DW_HEAD         0        /* uint8, high nibble accent, low the length */
#define DW_POS          1        /* uint8 */
#define DW_ATTR         2        /* uint8, and 3 */
#define DW_KANA         4

/* A node of the kana dictionary, keyed by a whole two-byte character. Its
   readings run on from the sixth byte, each a length in its own low nibble,
   three bytes of something, then that many bytes of kana. */
#define TH_KEY          0        /* two bytes, most significant first */
#define TH_CHILD        2        /* uint8, bytes to the next node down */
#define TH_FLAGS        3        /* uint8, high nibble set if readings here,
                                    low nibble is the sibling delta's high */
#define TH_SIBLING      4        /* uint8, and its low */
#define TH_READING      5        /* the readings */
#define TR_LEN          0        /* uint8, in the low nibble */
#define TR_KANA         4

#endif
