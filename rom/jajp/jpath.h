/* What JPath is, as a record.
 *
 * JPath is the path search: given every way DictSearch says a stretch of text
 * could be read, it builds the set of ways those readings can be strung
 * together to cover the whole of it. A path is a list of candidate entries,
 * and this record holds the paths, a copy of each entry that appears on one,
 * and an index from an entry to that copy.
 *
 * The size is settled -- TextAnalysis::initialize asks operator new for
 * 0x7cec bytes -- and every region below is settled by the code that fills
 * it, chiefly MakeJrtSubTable, which walks the paths, copies each entry it
 * has not seen into the next free sub-word, and writes the index. The three
 * counts agree with each other and with DictSearch's own: 710 entries, 710
 * possible sub-words, 710 index slots.
 */

#ifndef JPATH_H
#define JPATH_H

#include <stdint.h>

#define JP_BYTES        0x7cec    /* what TextAnalysis allocates */

#define JP_VTABLE       0x0000
#define JP_OWNER        0x0004    /* TextAnalysis * */

/* The paths. Each is a count and the entry indices on it, and the whole array
   runs from here to the sub-words. */
#define JP_PATH         0x0008
#define JP_PATH_N       710       /* (0x26dc - 8) / 14, exactly */
#define JP_PATH_SIZE    0x000e

/* One path, at IBM's own offsets within it. */
#define JPT_COUNT       0x00      /* uint8, how many entries are on it */
#define JPT_SPARE       0x01      /* uint8, nothing read so far writes it */
#define JPT_AT          0x02      /* uint8 [12], the entry indices */
#define JPT_AT_N        12

/* A sub-word: one candidate entry copied out of DictSearch, with the fields
   the phrase buffer wants and nothing else. Written by MakeJrtSubTable, which
   is also what says which field of the entry each one comes from. */
#define JP_SUB          0x26dc
#define JP_SUB_N        710
#define JP_SUB_SIZE     0x001c

#define JS_ENTRY        0x00      /* int16, which entry it was copied from */
#define JS_AT           0x02      /* int16, where in the text, from DE_AT */
#define JS_MARK         0x04      /* int32, from DE_MARK */
#define JS_ACCENT       0x08      /* int16, from DE_ACCENT */
#define JS_KANALEN      0x0a      /* uint8, from DE_KANALEN */
#define JS_CHARS        0x0b      /* uint8, from DE_CHARS */
#define JS_HIRAGANA     0x0c      /* uint8, from DE_HIRAGANA */
#define JS_POS          0x0d      /* uint8, from DE_POS */
#define JS_KANA         0x0e      /* uint8 [9], the first nine of DE_KANA */
#define JS_KANA_N       9
#define JS_ATTR         0x17      /* uint8, what SetWordAttr works out */
#define JS_OFFSET       0x18      /* int16, from DE_OFFSET */
#define JS_UNREAD_1A    0x1a      /* two bytes nobody has read */

/* What SetWordAttr puts in JS_ATTR. */
#define JS_ATTR_HEAD      0x01
#define JS_ATTR_CONT      0x02
#define JS_ATTR_TAIL      0x04
#define JS_ATTR_JOIN      0x08
#define JS_ATTR_PREFIX    0x10
#define JS_ATTR_NUMBER    0x80

/* How many paths there are. */
#define JP_PATH_COUNT   0x7484    /* uint16 */

/* A span between the count and the index that nothing read so far touches.
   It is 726 bytes, which is the number of characters InputChar holds and the
   number of function-word slots DictSearch has, so it is probably one byte a
   character; until something is seen to write it that is a guess and it is
   named as unread. */
#define JP_UNREAD_7486  0x7486
#define JP_UNREAD_N     726

/* Which sub-word each entry became, or minus one for an entry on no path.
   MakeJrtSubTable fills the whole of it with minus one first. */
#define JP_INDEX        0x775c    /* int16 [710] */
#define JP_INDEX_N      710

#define JP_SEARCH       0x7ce8    /* DictSearch *, the owner's own */

/* Three of those four are pointers and cannot stay where IBM put them on a
   build where a pointer is eight bytes wide: the vtable and the owner are
   four bytes apart, and the search sits four bytes from the end of the
   record. They are parked past it, as every other record in this directory
   parks its own. */
#define JP_ROOM         (JP_BYTES + 3 * sizeof(void *))
#define JP_VTABLE_AT    (JP_BYTES + 0 * sizeof(void *))
#define JP_OWNER_AT     (JP_BYTES + 1 * sizeof(void *))
#define JP_SEARCH_AT    (JP_BYTES + 2 * sizeof(void *))

/* Reaching into it. */
#define JP_P(jp, off)   ((uint8_t *)(jp) + (off))
#define JP_B(jp, off)   (*JP_P((jp), (off)))
#define JP_S16(jp, off) (*(int16_t *)JP_P((jp), (off)))
#define JP_U16(jp, off) (*(uint16_t *)JP_P((jp), (off)))

#define JP_PATH_AT(jp, i) JP_P((jp), JP_PATH + (i) * JP_PATH_SIZE)
#define JP_SUB_AT(jp, i)  JP_P((jp), JP_SUB + (i) * JP_SUB_SIZE)
#define JP_INDEX_OF(jp, e) JP_S16((jp), JP_INDEX + (e) * 2)

#endif
