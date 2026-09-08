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

/* A pointer the machine holds, as it holds it. A field of this type is
   thirty-two bits wide whatever the host is, which is what keeps the block
   layouts the compiled rules address from moving. */
typedef int32_t evv_ref;

#if defined(EVV_ARENA) && EVV_ARENA

/* The region, and how far it reaches. Set once, before anything is
   allocated. */
extern unsigned char *evv_arena_base;
extern size_t         evv_arena_size;

/* Take the region and give it back. It says so and stops if the system will
   not put it anywhere a 32-bit value can name, since nothing sensible can be
   answered to that. */
int  evv_arena_open(size_t bytes);
void evv_arena_close(void);

/* Which block an address is in, and the return address the allocator recorded
   for whoever asked for it. For the provenance census in
   src/delta/delta_prov.c, which is the first thing to read `whence' while
   nothing is wrong. Answers nought where the address is in no live block. */
int  evv_arena_whence_of(const void *p, uint32_t *whence);

/* The same, and how many bytes the block holds. The allocator's name is too
   coarse on its own -- one allocator serves many sorts of record -- and a
   length separates most of them. */
int  evv_arena_whence_of2(const void *p, uint32_t *whence, uint32_t *bytes);

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
char *evv_arena_strdup(const char *s);
#define strdup(s)       evv_arena_strdup(s)

/* Turning a pointer into a value the machine can hold. Everything the machine
   can hold a pointer to comes out of the arena: the heap, the frames, and the
   language's own data, which src/delta/delta_low.c copies out of the program at
   startup for exactly this reason. Anything else cannot be named in 32 bits
   and is a fault in whoever allocated it, not something to truncate. */
int32_t evv_ref_checked(const void *p);

/* Where a reference was made, which is the one place its type is still known.
 *
 * A reference is a bare value once it exists, and the rules address memory
 * through one and a byte offset, so what a rule is looking at cannot be
 * recovered from the rule. But it can be recovered from here: every reference
 * is born at one of these, in code where the pointer still has a C type, and
 * the file and line are enough to find that type in the source. So under the
 * census a reference is recorded as it is made and the rules are asked which
 * birth it came from. src/delta/delta_prov.c says what is done with it.
 *
 * Nothing else changes: the value handed back is the value evv_ref_checked
 * answers either way, and without the census the call is not there at all. */
#if defined(EVV_PROVENANCE) && EVV_PROVENANCE
int32_t evv_prov_born(const char *file, int line, const void *p);
#define EVV_REF(p)      evv_prov_born(__FILE__, __LINE__, (p))
#else
#define EVV_REF(p)      evv_ref_checked(p)
#endif
/* And back again. A reference is a distance from the region's base, so this
   is an addition rather than a cast, and nought stays nothing because the
   region's first eight bytes are never handed out. */
/* And back again. A reference is a distance from the region's base, so this
   is an addition rather than a cast, and nought stays nothing because the
   region's first eight bytes are never handed out.

   A function and not a macro, because five sites pass `va_arg(ap, int32_t)'
   as the reference and a macro that tests it and then adds to it reads the
   argument twice -- taking two words off the list where one was meant. That
   cost a day: the phonemes came out right, the first quarter of the waveform
   was byte-identical, and callSynthesizeArray read every frame parameter
   from the wrong word after that. Under absolute addressing the macro used
   its argument once, so nothing could see it until the day a reference
   stopped being an address. */
static inline void *evv_at(int32_t r)
{
    return r ? (void *)(evv_arena_base + (uint32_t)r) : 0;
}
#define EVV_AT(t, r)    ((t)evv_at(r))

#else

/* A 32-bit build: a pointer is already a value and a value is already a
   pointer, which is what every one of these sites said before. */
#define EVV_REF(p)      ((int32_t)(intptr_t)(p))
#define EVV_AT(t, r)    ((t)(intptr_t)(r))

#define evv_arena_alloc(n)  malloc(n)
#define evv_arena_free(p)   free(p)

#endif

/* One rule's frame, and giving it back. A rule hands the machine the address
   of its own frame, so the frame cannot be an ordinary local: where a value
   is 32 bits and an address is not, the thread that sets the engine up is the
   process's own and nothing can move its stack somewhere a value could name.
   They nest strictly, so a stack of them is all that is wanted, and it comes
   from the same place as everything else. */
/* What the arena is still holding, grouped by the allocation that asked, most
   bytes first. A leak is a group that grows with every instance made and
   thrown away. */
void evv_arena_outstanding(const char *when);

void *evv_frame_push(size_t n);
void  evv_frame_pop(void *p);
/* Called by whatever runs a thread, once its body has returned. */
void  evv_frame_done(void);

#endif
