/* Where the machine's memory lives, and how a pointer to it fits in a value.
 *
 * The Delta machine keeps every value in a signed 32-bit slot and puts host
 * addresses in some of them. On a 32-bit build that is simply true and the
 * casts below are what the code has always said. On a 64-bit one it is only
 * true if everything a value can point at lives low enough to be named in 32
 * bits, which is what the arena is for: one region, taken below the two
 * gigabyte mark, out of which everything the machine can hold a pointer to is
 * allocated.
 *
 * Nothing here changes what a 32-bit build compiles to. EVV_REF and EVV_AT
 * expand to exactly the casts that were written by hand before, so the whole
 * conversion can be made and proved against both suites before an arena
 * exists at all.
 */

#ifndef EVV_ARENA_H
#define EVV_ARENA_H

#include <stddef.h>
#include <stdint.h>

#if defined(EVV_ARENA) && EVV_ARENA

/* The region, and how far it reaches. Set once, before anything is
   allocated. */
extern unsigned char *evv_arena_base;
extern size_t         evv_arena_size;

/* Take the region and give it back. Answers zero if the system will not put
   it anywhere a 32-bit value can name. */
int  evv_arena_open(size_t bytes);
void evv_arena_close(void);

void *evv_arena_alloc(size_t n);
void *evv_arena_calloc(size_t n, size_t m);
void *evv_arena_realloc(void *p, size_t n);
void  evv_arena_free(void *p);

/* Everything the engine allocates comes from the arena, not because all of it
   ends up in a value but because working out which parts do is guesswork and
   getting it wrong is a wild pointer. What is left outside is what the C
   library allocates for itself, and if one of those ever reaches a value the
   check below says so rather than truncating it. */
#define malloc(n)       evv_arena_alloc(n)
#define calloc(n, m)    evv_arena_calloc(n, m)
#define realloc(p, n)   evv_arena_realloc(p, n)
#define free(p)         evv_arena_free(p)

/* A frame for one rule, and giving it back. These come from the arena and not
   from the thread's stack, because a rule hands the address of its own frame
   to the machine and the stack is nowhere near low enough. */
void *evv_frame_push(size_t n);
void  evv_frame_pop(void *p);

/* Turning a pointer into a value the machine can hold. Anything outside the
   arena cannot be named in 32 bits and is a fault in whoever allocated it,
   not something to truncate quietly. */
int32_t evv_ref_checked(const void *p);

#define EVV_REF(p)      evv_ref_checked(p)
#define EVV_AT(t, r)    ((t)(void *)(uintptr_t)(uint32_t)(r))

#else

/* A 32-bit build: a pointer is already a value and a value is already a
   pointer, which is what every one of these sites said before. */
#define EVV_REF(p)      ((int32_t)(intptr_t)(p))
#define EVV_AT(t, r)    ((t)(intptr_t)(r))

#define evv_arena_alloc(n)  malloc(n)
#define evv_arena_free(p)   free(p)

#endif

#endif
