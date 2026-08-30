/* What Romanizer is, as a record, as far as anything has read it.
 *
 * Romanizer itself is not transcribed. This header exists because two other
 * classes reach into it: InputChar goes up through TextAnalysis to ask the
 * parameter block whether annotations are in the text and to find the user
 * dictionary, and DictSearch reads two settings out of it directly. So the
 * layout is not ours to choose after all, and the fields below are IBM's own
 * offsets.
 *
 * It is a partial map and says so. The size is settled -- RomInstance asks
 * operator new for 0x78 bytes -- and four fields in it are known; everything
 * else is named as a span nobody has read, so that the whole still tiles.
 */

#ifndef ROMANIZER_H
#define ROMANIZER_H

#include <stdint.h>

#define RZ_BYTES         0x78    /* what RomInstance allocates */

#define RZ_UNREAD_HEAD   0x00    /* a vtable and whatever else */
#define RZ_PARAM         0x08    /* RomInstParam * */
#define RZ_UNREAD_MID    0x0c
#define RZ_USERDICT      0x18    /* RomUserDict * */
#define RZ_UNREAD_MID2   0x1c
#define RZ_NUMBER_MODE   0x34    /* uint16; two refuses a bare place word */
#define RZ_UNREAD_MID3   0x36

/* The five an annotation may set, which is what rz_GetParameter is for. The
   letters are Eloquence's own: b is the baseline pitch, f the pitch
   fluctuation, s the speed and v the volume, and a number on its own picks
   one of the two voices and resets the other four to that voice's own. */
#define RZ_VOICE         0x50    /* int32, one or two */
#define RZ_BASELINE      0x54    /* int32 */
#define RZ_FLUENCY       0x58    /* int32 */
#define RZ_SPEED         0x5c    /* int32 */
#define RZ_VOLUME        0x60    /* int32 */
#define RZ_UNREAD_MID4   0x64
#define RZ_SPELL_ENGLISH 0x68    /* int32; above nought spells English out */
#define RZ_UNREAD_TAIL   0x6c

/* The two pointers cannot stay at IBM's offsets on a build where a pointer is
   eight bytes wide: +8 would run over the word after it and +0x18 likewise.
   They are parked past the record, as DictSearch's and InputChar's are. */
#define RZ_ROOM          (RZ_BYTES + 2 * sizeof(void *))
#define RZ_PARAM_AT      (RZ_BYTES)
#define RZ_USERDICT_AT   (RZ_BYTES + sizeof(void *))

#endif
