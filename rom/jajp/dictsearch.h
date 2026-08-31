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

/* And what ours has to ask for. Three of this record's fields hold pointers,
   which are eight bytes here and were four in IBM's, so each of them uses the
   four bytes after it as well. Two of the three get away with it: DS_INPUTCHAR
   has unread space behind it, and DS_USERDICT_WORD is four bytes from the end,
   which the extra room below covers.
 *
 * The owner does not. It sits at offset four and the candidate array starts at
 * offset eight, so on a sixty-four bit host the pointer's upper half and the
 * first entry are the same bytes and each destroys the other -- writing one
 * candidate makes the owner unreadable, which is a fault that only shows when
 * something writes an entry and then asks the owner for anything. So ours
 * keeps the owner past the end of IBM's record instead, at DS_OWNER_AT. The
 * offsets IBM's own code uses are untouched, which is what matters for the
 * map and for the sweep; only our own code knows where the owner really is,
 * and test/romprims.c sets it through a macro so that each side writes its
 * own place. See docs/japanese.md on what a byte offset costs when a pointer
 * grows. */
#define DS_ROOM         (DS_BYTES + 3 * sizeof(void *))
#define DS_OWNER_AT     (DS_BYTES + sizeof(void *))

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

/* And what is in one, which PhraseBuf::SetPhrasePart is what says: it walks
   the chain by PF_LINK, copies five of these fields into the phrase it is
   building, and reads PF_FLAGS to decide whether the word may start a chain
   at all and whether it costs an extra three moras. */
#define PF_LINK         0x00     /* int8, the next word in the chain, or
                                    negative where the chain ends */
#define PF_KANALEN      0x01     /* uint8, codes of reading */
#define PF_MORAS        0x02     /* uint8 */
#define PF_UNREAD_03    0x03     /* uint8 */
#define PF_CODE         0x04     /* uint8 */
#define PF_UNREAD_05    0x05     /* uint8 */
#define PF_AT           0x06     /* int16, into the function-word dictionary */
#define PF_ACCENT       0x08     /* int16 */
#define PF_FLAGS        0x0a     /* uint8; bit nought says the word may end a
                                    phrase, bit one that it is two moras */
#define PF_UNREAD_0B    0x0b     /* uint8 */
#define PF_OFFSET       0x0c     /* int16 */

#define DS_FZK_P(d, i, off) \
    ((uint8_t *)(d) + DS_FZK + (i) * DS_FZK_SIZE + (off))
#define DS_FZK_B(d, i, off)   (*DS_FZK_P((d), (i), (off)))
#define DS_FZK_S16(d, i, off) (*(int16_t *)DS_FZK_P((d), (i), (off)))

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

/* Settled. The first is a mode Do writes and tests against one; when it is
   one, a user dictionary entry has to match the word the second points at
   before it is taken. The record is a _SNLK_TABLE, which is a reading the
   caller gave for a stretch of this very text: Do asks ic_GetSnlkTableAt for
   the one at the character it has reached and puts the answer here, so the
   two yomi codes RomUserDict wants at +0x10 are SN_CHARS and SN_YOMI_N and
   the string at +0x04 is inside the pointers rom/jajp/inputchar.h moves. */
#define DS_USERDICT_MODE 0x8900  /* int32 */
#define DS_USERDICT_WORD 0x8904  /* the record it must agree with */

/* ---- what InputChar holds that DictSearch reads --------------------- */

/* rom/jajp/inputchar.h is the whole of that record, and the twelve kinds a
   character can be along with it. It is included rather than copied because
   this class reads five of its fields and every one of the kinds. */
#include "inputchar.h"

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
#define DE_LINK         0x1a     /* int16; nothing read so far writes it. The
                                    comment here used to say Do sets it to
                                    minus one, which was a displacement read
                                    without allowing for DS_ENTRY sitting at
                                    eight: what Do sets is DE_AT */
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

/* A node of the word dictionary, keyed by a whole two-byte character. The
   child delta is packed oddly -- seven bits shifted by four, plus a whole
   byte -- and the flag that says words end here is the bit it shares with. */
#define NH_KEY          0        /* two bytes, most significant first */
#define NH_FLAGS        2        /* uint8, 0x80 set if words end here and the
                                    low seven bits are the child delta's high */
#define NH_CHILD        3        /* uint8, and its low */
#define NH_SIBLING      4        /* two bytes, most significant first */
#define NH_COUNT        6        /* uint8, how many words, when 0x80 is set */
#define NH_WORD         7        /* and the words themselves */

/* One of those words, which is the same shape as a page of the kanji
   dictionary holds. */
#define NW_HEAD         0        /* uint8, high nibble accent, low the length */
#define NW_POS          1
#define NW_ATTR         2        /* and 3 */
#define NW_KANA         4

/* A record of the supplement dictionary, and of the English one, which are
   the same shape: a whole word in one record rather than a trie. The reading,
   the part of speech and the two attributes follow the written form, so where
   each of them starts depends on how long that is. */
#define UH_LEN          0        /* uint8, bytes to the next record */
#define UH_CHARS        1        /* uint8, characters of written form */
#define UH_KANALEN      2        /* uint8, codes of reading */
#define UH_ACCENT       3        /* uint8 */
#define UH_TEXT         4        /* the written form, two bytes each */

/* One word of the function-word dictionary. The fourth byte carries two
   things: its top two bits are flags and the rest, less one, is which row of
   the kakari table the word belongs to. */
#define FW_LEN          0        /* uint8, bytes to the next, kana plus six */
#define FW_KEY          1        /* uint8, and 0x5e ends the list */
#define FW_ACCENT       2        /* uint8, the accent table's row, from one */
#define FW_KAKARI       3        /* uint8, flags 0x40 and 0x80, and the row */
#define FW_PHRVEC       4        /* uint8, the phrase vector's row, from one */
#define FW_PENALTY      5        /* uint8, the penalty table's row, from one */
#define FW_KANA         6        /* and the reading */

/* A node of the trie over it, keyed by a whole two-byte character, with its
   words hanging off the sixth byte. */
#define FN_KEY          0        /* two bytes, most significant first */
#define FN_CHILD        2        /* uint8, bytes to the next node down */
#define FN_FLAGS        3        /* uint8, high nibble how many words, low
                                    nibble the sibling delta's high */
#define FN_SIBLING      4        /* uint8, and its low */
#define FN_WORD         5        /* the words themselves */

/* And what one match leaves behind in the function-word array, which is what
   the pass above reads back. */
#define FZ_MARK         0        /* uint8, 0xff where nothing was written */
#define FZ_CHARS        1        /* uint8, characters of text it covers */
#define FZ_HIRAGANA     2        /* uint8, how many of those are hiragana */
#define FZ_KEY          4        /* int16, the word's own key byte */
#define FZ_WORD         6        /* int16, where the word is in the blob */
#define FZ_AT           8        /* int16, where in the text it ends */
#define FZ_FLAGS        0x0a     /* uint8 */
#define FZ_OFFSET       0x0c     /* int16, the byte offset in the raw text */

/* ---- what Romanizer holds that DictSearch reads ---------------------- */

/* rom/jajp/romanizer.h is that record. Two settings are read out of it here:
   whether an English word is spelled out letter by letter, and which number
   mode is in force. */
#include "romanizer.h"

/* What the user-dictionary context is has been read since: it is a _SNLK_TABLE,
   and rom/jajp/inputchar.h names the whole of it. The two fields wanted here
   are SN_KEY_AT, the written form to match, and SN_CHARS, how many characters
   it has -- the first parked past the record in our build, because IBM has it
   at +4 where a pointer no longer fits. */

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
