/* A landing place for a backtrack, and putting the machine back on it.
 *
 * The engine backtracks by jumping to a place a rule planted earlier. The
 * buffer it names the place by is the machine's own memory: sixty-four bytes,
 * on whatever boundary 1999 put it, and a landing may be entered from a call
 * that has already returned, since everything a landing needs lives in a block
 * whose address has escaped rather than in the frame.
 *
 * The C library's landing place fits none of that on x86-64. It is 256 bytes,
 * so writing one into a sixty-four byte hole treads on whatever the rule keeps
 * next to it -- which on Linux happened to be harmless and on Windows was not.
 * It wants sixteen byte alignment, because it saves the SSE registers with
 * movaps, and the machine's buffer has no reason to be aligned. And on Windows
 * longjmp is an unwind: it walks the frames between here and there, expects to
 * find them, and faults inside ntdll when they have gone.
 *
 * So the landing place is ours and it is kept outside the machine's memory
 * altogether. The buffer's address is only a name; evv_land.c keeps one place
 * per name. What it saves is the registers the two calling conventions agree
 * belong to the callee, the two more Windows adds, the stack pointer and where
 * to resume. No floating point register, because nothing between planting a
 * landing and entering it holds a value in one: the interpreter and the rules
 * are integer code from end to end.
 */

#ifndef EVV_LAND_H
#define EVV_LAND_H

#include <stdint.h>

#if defined(__x86_64__)

/* Eighty bytes, and eight of them is all the alignment it asks for. */
#define EVV_LAND_WORDS 10

#else

/* Elsewhere the place holds the C library's own buffer, so it is as big as
   that is: 156 bytes on a thirty-two bit Linux, and nowhere near sixty-four,
   which is the other half of why it cannot go in the machine's. */
#include <setjmp.h>
#define EVV_LAND_WORDS ((sizeof(jmp_buf) + 7) / 8)

#endif

/* The landing place that answers to this address, made if there is not one.
   The second is the same lookup for the jump, and it refuses a name this
   thread never planted rather than handing back noughts to jump to. */
void *evv_land_place(uintptr_t name);
void *evv_land_planted(uintptr_t name);
void  evv_land_forget(uintptr_t lo, uintptr_t hi);

int  evv_land_save(void *place) __attribute__((returns_twice));
void evv_land_jump(void *place, int value) __attribute__((noreturn));

#define EVV_LAND_SAVE(p)     evv_land_save(evv_land_place((uintptr_t)(p)))
#define EVV_LAND_JUMP(p, v)  evv_land_jump(evv_land_planted((uintptr_t)(p)), (v))

#endif
