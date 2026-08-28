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
 *   the function words  memset of 0x27b4 in FzkParsingReverse, a stride of 14
 *                       in LookupFuncWordDict, and a bound of 0x2d6 there --
 *                       726 times 14 is 0x27b4 to the byte
 *   the three records   indexed with a shift of four, and three of them reach
 *                       the count that follows exactly
 *   the tankan table    memset of 0x258 in GenerateKanaString and a stride of
 *                       0x14 in SearchTankanTable -- thirty entries
 *   the entries         Do clears 0x58c0 bytes from offset eight and then
 *                       writes a marker into 710 entries of thirty-two, and
 *                       710 times thirty-two is 0x58c0 to the byte
 *   the kana buffers    four memsets of 0x1e in GenerateKanaString, and seven
 *                       of thirty bytes reach the word after them exactly
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

/* Settled: the constructor writes these three and nothing else. The last is a
   copy of the owner's own InputChar pointer, taken once. */
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

/* Not resolved. */
#define DS_UNREAD_MID       0x80ae
#define DS_UNREAD_MID_END   0x8150

/* Settled: cleared whole and indexed with a stride of twenty. */
#define DS_TANKAN       0x8150
#define DS_TANKAN_N     30
#define DS_TANKAN_SIZE  0x14     /* 30 times 20 is the memset exactly */

/* Settled by adjacency, the same argument the phrase buffers rest on.
   GenerateKanaString clears four of these -- the first, the second, the
   fourth and the sixth -- and seven of thirty bytes reach exactly the word
   that follows them. */
#define DS_KANA         0x83a8
#define DS_KANA_N       7
#define DS_KANA_SIZE    0x1e

/* Settled as a scalar: GenerateKanaString clears it first thing. */
#define DS_W_847A       0x847a   /* int16 */

/* Not resolved. */
#define DS_UNREAD_KANA      0x847c
#define DS_UNREAD_KANA_END  0x84f4

/* Not resolved: two byte areas GenerateWord writes into, each indexed by two
   things at once. */
#define DS_WORK         0x84f4
#define DS_WORK2        0x84fe
#define DS_WORK_END     0x8508

/* Settled as scalars, not as meanings: each is written and read as a word and
   none is indexed. */
#define DS_W_8508       0x8508
#define DS_W_850A       0x850a
#define DS_W_850C       0x850c
#define DS_W_850E       0x850e
#define DS_W_8510       0x8510
#define DS_W_8512       0x8512

/* Settled: the constructor's third write. */
#define DS_INPUTCHAR    0x8514   /* InputChar *, the owner's own */

/* Not resolved. */
#define DS_UNREAD_TAIL      0x8518
#define DS_UNREAD_TAIL_END  0x8900

/* Settled as scalars: Do writes the first and tests it against one. */
#define DS_L_8900       0x8900   /* int32 */
#define DS_L_8904       0x8904   /* int32 */

#endif
