/* What NumRead is, as a record.
 *
 * NumRead is how a number becomes words. TextAnalysis hands it a phrase whose
 * words are digits and punctuation, and it decides how the digits are read --
 * by place, digit by digit, as a decimal or as a fraction -- and writes the
 * reading out as codes.
 *
 * The record has no constructor. `Init' is what clears it, and what it clears
 * is what says how long the two arrays are; the rest is settled by the code
 * that writes it. The regions below tile the whole of what IBM's own object
 * touches, which `tools/rom/offsets.py numread' is what checks.
 *
 * Which array is which comes from IBM's own name for one of them. The four
 * ApplySRuleTo methods take a `_substr_t *' and what they are handed is a
 * seventy-byte block, so a substring here is a run of digits read one way --
 * and the forty-four byte records are the readings those runs turn into.
 */

#ifndef NUMREAD_H
#define NUMREAD_H

#include <stdint.h>

#define NR_OWNER        0x0000    /* TextAnalysis * */

/* The readings, one per substring that survives. Eight of forty-four bytes,
   and the fields fill all forty-four. Each holds a run of codes and, beside
   them, five pairs of numbers the rules adjust as they go. */
#define NR_READ         0x0004
#define NR_READ_N       8
#define NR_READ_SIZE    0x2c

#define RD_COUNT        0x00      /* uint8, how many codes are in it */
#define RD_LEN          0x01      /* uint8, how long the reading runs */
#define RD_A            0x02      /* int16 [5], stride 4 from here */
#define RD_B            0x04      /* int16 [5], stride 4 from here */
#define RD_PAIR_N       5
#define RD_PAIR_SIZE    4
#define RD_CODES        0x16      /* uint8 [22]; 0x16 plus 22 is 0x2c */
#define RD_CODES_N      22

/* The substrings a number is cut into, forty records of seventy bytes in two
   halves of twenty. `SegmentYomiBlock' cuts the digits up in the first
   twenty and copies the ones it keeps into the second; `Init' clears the
   second and `GenerateStdForm' builds the standard form there.
 *
 * The base is 0x164 and not 0x186 or 0x6dc, both of which the code uses.
 * What settles it is one copy in `SegmentYomiBlock': it moves
 * this[0x186 + i * 0x46] into this[0x6fe + j * 0x46], and 0x6fe less 0x186
 * is exactly twenty strides, so the two are the same field of entry i and of
 * entry j plus twenty of one array. Forty strides from 0x164 lands on the
 * answers below to the byte. */
#define NR_SUBSTR       0x0164
#define NR_SUBSTR_N     40
#define NR_SUBSTR_HALF  20
#define NR_SUBSTR_SIZE  0x46

#define SS_KIND         0x00      /* uint8, which of the eight readings */
#define SS_COUNT        0x01      /* uint8, how many codes are in it */
#define SS_CODES        0x02      /* uint8 [32] */
#define SS_FROM         0x22      /* int16, where in the digits it starts */
#define SS_TO           0x24      /* int16, and where it ends */
#define SS_MORE         0x26      /* uint8 [32], a second run beside them */
#define SS_CODES_N      32        /* 2 + 32 + 2 + 2 + 32 is 0x46 to the byte */

/* Both arrays are thirty-two long and `Init' clears thirty-one of each, so
   the last byte of either keeps whatever was there. Nothing read so far
   reads one without having written it first. */
#define SS_CLEARED      31

/* How many readings one number can ask for.
 *
 * The record holds eight and IBM's `Do' keeps eight on its stack to count
 * the pairs in each, but nothing in `ApplySRule' stops it making more: every
 * substring may start one, and the digit-by-digit road starts one every two
 * digits. Twenty substrings of thirty-two codes can therefore ask for
 * hundreds, and IBM then walks off the end of its own frame -- which is what
 * stopped the harness driving its object dead, its own loop counter gone.
 *
 * Ours keeps as many as can be asked for and copies the first eight into the
 * record, which is what IBM copies. Past the eighth the two do not agree,
 * and cannot: IBM is reading and writing its caller's stack there. */
#define NR_READINGS_MOST (NR_SUBSTR_HALF * SS_CODES_N)

/* What Do leaves for its caller, and the digits it was given. */
#define NR_ANSWER       0x0c54    /* int16 [8] */
#define NR_ANSWER_N     8
#define NR_COUNT        0x0c64    /* uint8, how many digits */
#define NR_DIGITS       0x0c65    /* uint8 [], the digits and punctuation */

/* How long the whole of it is. Nothing in numread.obj allocates a NumRead:
   whichever class holds one holds it as a member, so the size has to come
   from there rather than from an operator new here.
 *
 * That class is `PhraseTable', which is not transcribed yet, and its
 * `initialize' asks operator new for 0xc84 -- so the digits run from 0xc65
 * to 0xc84 and there are thirty-one of them, not the two hundred and
 * fifty-six a count of one byte allows. Shrinking this is a thing to do
 * with that class rather than on its own: our own code and the harness both
 * take the room from here, and a count above thirty-one is IBM walking past
 * its own object. Until then this is the generous reading.
 *
 * The owner is a pointer at nought with the readings starting at four, so it
 * cannot stay there where a pointer is eight bytes wide. It is parked past
 * the record, as every other record in this directory parks its own. */
#define NR_BYTES        0x0d65    /* NR_DIGITS plus 256 */
#define NR_ROOM         (NR_BYTES + sizeof(void *))
#define NR_OWNER_AT     NR_BYTES

/* Reaching into it. */
#define NR_P(nr, off)       ((uint8_t *)(nr) + (off))
#define NR_B(nr, off)       (*NR_P((nr), (off)))
#define NR_S16(nr, off)     (*(int16_t *)NR_P((nr), (off)))
#define NR_READ_AT(nr, i)   NR_P((nr), NR_READ + (i) * NR_READ_SIZE)
#define NR_SUBSTR_AT(nr, i) NR_P((nr), NR_SUBSTR + (i) * NR_SUBSTR_SIZE)
#define NR_DIGIT_AT(nr, i)  NR_B((nr), NR_DIGITS + (i))
#define NR_ANSWER_AT(nr, i) NR_S16((nr), NR_ANSWER + (i) * 2)

/* One reading's own fields, which are reached off its own base rather than
   off the record's. */
#define RD_B8(rd, off)  (*((uint8_t *)(rd) + (off)))
#define RD_A_AT(rd, i)  (*(int16_t *)((uint8_t *)(rd) + RD_A + (i) * RD_PAIR_SIZE))
#define RD_B_AT(rd, i)  (*(int16_t *)((uint8_t *)(rd) + RD_B + (i) * RD_PAIR_SIZE))

/* And one substring's. */
#define SS_B8(ss, off)  (*((uint8_t *)(ss) + (off)))
#define SS_S16(ss, off) (*(int16_t *)((uint8_t *)(ss) + (off)))

#endif
