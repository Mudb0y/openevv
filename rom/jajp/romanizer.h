/* What Romanizer is, as a record, as far as anything has read it.
 *
 * Romanizer's own object is not transcribed yet, but its base class is:
 * `ConverterInterface' is rom/jajp/convtinterface.c, and because Romanizer
 * derives from it the two share one record. So the head of this map is now
 * settled rather than guessed -- every field from 0x00 to 0x1f belongs to the
 * base and convtinterface.obj says what each one is -- and the tail is still
 * the partial reading it always was: the size is known, since RomInstance
 * asks operator new for 0x78 bytes, and what is understood between 0x20 and
 * 0x77 is what other classes reach in for.
 *
 * Two other classes do reach in, which is why the layout is not ours to
 * choose: InputChar goes up through TextAnalysis to ask the parameter block
 * whether annotations are in the text and to find the user dictionary, and
 * DictSearch reads two settings out of it directly.
 *
 * The evidence for the head being complete is mechanical. Every displacement
 * on a pointer anywhere in convtinterface.obj is one of 0x04, 0x08, 0x0c,
 * 0x10, 0x14, 0x18, 0x1c, 0x20 and 0x24, and the last two occur only on the
 * frame pointer, where they are arguments. So the base class has no field
 * above 0x1c and none between the ones named here.
 */

#ifndef ROMANIZER_H
#define ROMANIZER_H

#include <stdint.h>

#define RZ_BYTES         0x78    /* what RomInstance allocates */

/* ---- the ConverterInterface half ------------------------------------- */

#define RZ_VTABLE        0x00    /* the seven slots jprom.h lists */
#define RZ_UNICODE       0x04    /* UnicodeConverter *, made on first use */
#define RZ_PARAM         0x08    /* RomInstParam * */
#define RZ_INPUT         0x0c    /* InputManager * */
#define RZ_STOPPED       0x10    /* int32; stop sets it, resume clears it */
#define RZ_TRANSBUF      0x14    /* char *, where a recoded text is put */
#define RZ_USERDICT      0x18    /* RomUserDict * */
#define RZ_BUSY          0x1c    /* int32; resume waits for it to fall */

/* ---- Romanizer's own ------------------------------------------------- */

#define RZ_UNREAD_MID    0x20
#define RZ_NUMBER_MODE   0x34    /* uint16; two refuses a bare place word */
#define RZ_UNREAD_MID2   0x36

/* The five an annotation may set, which is what rz_GetParameter is for. The
   letters are Eloquence's own: b is the baseline pitch, f the pitch
   fluctuation, s the speed and v the volume, and a number on its own picks
   one of the two voices and resets the other four to that voice's own. */
#define RZ_VOICE         0x50    /* int32, one or two */
#define RZ_BASELINE      0x54    /* int32 */
#define RZ_FLUENCY       0x58    /* int32 */
#define RZ_SPEED         0x5c    /* int32 */
#define RZ_VOLUME        0x60    /* int32 */
#define RZ_UNREAD_MID3   0x64
#define RZ_SPELL_ENGLISH 0x68    /* int32; above nought spells English out */
#define RZ_UNREAD_TAIL   0x6c

/* Six of the fields above are pointers and none of them can stay where IBM
   put it on a build where a pointer is eight bytes wide: laid out four apiece
   they would each run over the word after. They are parked past the record,
   as DictSearch's and InputChar's are, and every one of them is reached
   through the _AT name rather than the offset. */
#define RZ_ROOM          (RZ_BYTES + 6 * sizeof(void *))
#define RZ_VTABLE_AT     (RZ_BYTES + 0 * sizeof(void *))
#define RZ_UNICODE_AT    (RZ_BYTES + 1 * sizeof(void *))
#define RZ_PARAM_AT      (RZ_BYTES + 2 * sizeof(void *))
#define RZ_INPUT_AT      (RZ_BYTES + 3 * sizeof(void *))
#define RZ_TRANSBUF_AT   (RZ_BYTES + 4 * sizeof(void *))
#define RZ_USERDICT_AT   (RZ_BYTES + 5 * sizeof(void *))

#endif
