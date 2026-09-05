/* The shape of TextNormalizer, which is twenty bytes and no more.
 *
 * How it was read. The constructor zeroes five words and the destructor gives
 * three of them back, which is what says how many there are and which are
 * owned; normalizeText writes the two lengths and both buffers. Every offset
 * TextNormalizer.obj uses on a `this' pointer is one of these five.
 *
 * The first field is a MakeReadableJP, made the first time an annotation
 * needs one and let go through its own vtable rather than with delete, which
 * is how a class with a virtual destructor is destroyed in this compiler.
 */

#ifndef TEXTNORMALIZER_H
#define TEXTNORMALIZER_H

#include <stdint.h>

#define TN_BYTES     0x14

#define TN_READABLE  0x00   /* MakeReadableJP *, made on demand */
#define TN_OUT       0x04   /* char *, the answer being built */
#define TN_WORK      0x08   /* char *, what one annotation came out as */
#define TN_OUT_CAP   0x0c   /* uint32 */
#define TN_WORK_CAP  0x10   /* uint32 */

/* The three pointers sit four bytes apart, so on a build where a pointer is
   eight bytes wide none of them can stay where IBM put it. They are parked
   past the record, as every other class in the romanizer parks its. */
#define TN_ROOM         (TN_BYTES + 3 * sizeof(void *))
#define TN_READABLE_AT  (TN_BYTES)
#define TN_OUT_AT       (TN_BYTES + 1 * sizeof(void *))
#define TN_WORK_AT      (TN_BYTES + 2 * sizeof(void *))

/* What normalizeText grows a buffer by beyond what it needs, and the size the
   working buffer starts at. */
#define TN_SLACK     0x100

/* The annotation's number, and the part of it that says which reader. */
#define TN_KIND(f)   ((f) & 0xff0000)
#define TN_CARDINAL  0x010000
#define TN_DATE      0x020000
#define TN_TIME      0x030000
#define TN_CURRENCY  0x040000
#define TN_BOOL      0x050000
#define TN_SPR       0xff0000   /* an annotation with no name at all */
#define TN_TEL       0x010300   /* at or above this a cardinal is a number */

#endif
