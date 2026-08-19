/* The region the machine's memory comes out of, taken low enough that a
 * pointer into it can be named in a 32-bit value.
 *
 * Nothing here is compiled into a 32-bit build, where a pointer already fits
 * and the ordinary allocator will do. It exists so that a 64-bit build can
 * keep the Delta value model exactly as it is -- a value is 32 bits and some
 * values are addresses -- instead of widening it, which would move every
 * field of every block the compiled rules address by a baked offset.
 *
 * The allocator is a plain first-fit with boundary tags. It is not clever and
 * does not need to be: the Delta heap takes segments in big pieces and gives
 * them back whole, and what else asks is small and long-lived.
 */

#include "evv_arena.h"

#if defined(EVV_ARENA) && EVV_ARENA

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

unsigned char *evv_arena_base;
size_t         evv_arena_size;

/* Where to try. Below two gigabytes so that a reference stays positive when
   it is read back out of a signed slot, and clear of where a program and its
   heap usually land. */
static const uintptr_t candidates[] = {
    0x10000000u, 0x20000000u, 0x30000000u, 0x40000000u,
    0x50000000u, 0x60000000u, 0x08000000u
};

#define ALIGN       16
#define ROUND(n)    (((n) + (ALIGN - 1)) & ~(size_t)(ALIGN - 1))

/* One block: how big it is and whether it is in use. The size is of the whole
   block, header included, so walking is a matter of adding it.

   The mark is not decoration. Everything the engine allocates comes from
   here, and a write that runs past the end of one block lands on the header
   of the next; without a mark the first anyone hears of it is the allocator
   walking a chain of zeros and never coming back. With one, the walk says
   which block was trodden on, which is the block after whoever overran. */
#define HEAD_MARK 0x45565641u   /* EVVA */

typedef struct {
    uint32_t mark;
    uint32_t size;
    uint32_t used;
    uint32_t whence;   /* who asked for it, so a block that was overrun can
                          say which allocation ran over it */
} head;

static head *first;

static void bad_block(const head *b, const char *what)
{
    const head *w = first;

    fprintf(stderr, "evv: arena block at %p %s"
            " (mark=%08x size=%u used=%u)\n",
            (const void *)b, what, b->mark, b->size, b->used);

    /* Whoever wrote past its end is the block in front, so name it: its size
       is usually enough to say which allocation it was. */
    while (w != 0 && w->mark == HEAD_MARK && w->size >= ALIGN) {
        const head *n = (const head *)((const unsigned char *)w + w->size);

        if (n == b) {
            fprintf(stderr, "evv: the block in front is at %p, %u bytes,"
                    " %s, asked for from %08x\n", (const void *)w, w->size,
                    w->used ? "in use" : "free", w->whence);
            break;
        }
        if (n >= b)
            break;
        w = n;
    }
    abort();
}

static void check(const head *b)
{
    if (b->mark != HEAD_MARK)
        bad_block(b, "has lost its mark");
    if (b->size < ALIGN || (b->size & (ALIGN - 1)) != 0)
        bad_block(b, "has an impossible size");
}

int evv_arena_open(size_t bytes)
{
    size_t i;

    if (evv_arena_base != 0)
        return 1;

    bytes = ROUND(bytes);
    for (i = 0; i < sizeof candidates / sizeof candidates[0]; i++) {
        void *at = mmap((void *)candidates[i], bytes,
                        PROT_READ | PROT_WRITE,
                        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (at == MAP_FAILED)
            continue;
        if ((uintptr_t)at + bytes < 0x80000000u) {
            evv_arena_base = at;
            evv_arena_size = bytes;
            break;
        }
        /* The system put it out of reach of a 32-bit value; no use. */
        munmap(at, bytes);
    }

    if (evv_arena_base == 0)
        return 0;

    /* The first eight bytes are never handed out, so that a reference of
       nought goes on meaning nothing, which is what every test for an empty
       value in the machine assumes. */
    first = (head *)(evv_arena_base + ALIGN);
    first->mark = HEAD_MARK;
    first->size = (uint32_t)(evv_arena_size - ALIGN);
    first->used = 0;
    return 1;
}

void evv_arena_close(void)
{
    if (evv_arena_base != 0)
        munmap(evv_arena_base, evv_arena_size);
    evv_arena_base = 0;
    evv_arena_size = 0;
    first = 0;
}

static head *next_block(head *b)
{
    unsigned char *p;

    check(b);
    p = (unsigned char *)b + b->size;
    if (p >= evv_arena_base + evv_arena_size)
        return 0;
    return (head *)p;
}

/* How much to take when nobody said. The pages are not touched until they are
   used, so asking for a lot costs nothing but address space, and the whole
   point of the region is that it has to sit at an address a 32-bit value can
   name, which is not somewhere to be short of room. */
#define ARENA_DEFAULT (256u * 1024u * 1024u)

void *evv_arena_alloc(size_t n)
{
    size_t want = ROUND(n) + ALIGN;
    head *b;

    if (n == 0)
        return 0;

    /* Opened on the first allocation rather than by whoever starts the engine,
       so that nothing can allocate before it exists. The first allocation is
       on the thread that sets the engine up, before it starts any other. */
    if (evv_arena_base == 0 && !evv_arena_open(ARENA_DEFAULT))
        return 0;

    for (b = first; b != 0; b = next_block(b)) {
        head *rest;

        if (b->used || b->size < want)
            continue;

        if (b->size >= want + ALIGN * 2) {
            rest = (head *)((unsigned char *)b + want);
            rest->mark = HEAD_MARK;
            rest->size = (uint32_t)(b->size - want);
            rest->used = 0;
            b->size = (uint32_t)want;
        }
        b->used = 1;
        b->whence = (uint32_t)(uintptr_t)__builtin_return_address(1);
        return (unsigned char *)b + ALIGN;
    }
    return 0;
}

void evv_arena_free(void *p)
{
    head *b, *w;

    if (p == 0)
        return;

    b = (head *)((unsigned char *)p - ALIGN);
    check(b);
    b->used = 0;

    /* Join what is free, from the front, so that the big pieces the heap
       gives back can be handed out again. */
    for (w = first; w != 0; w = next_block(w)) {
        head *n = next_block(w);

        while (!w->used && n != 0 && !n->used) {
            w->size += n->size;
            n = next_block(w);
        }
    }
}

char *evv_arena_strdup(const char *s)
{
    size_t n;
    char *p;

    if (s == 0)
        return 0;
    n = strlen(s) + 1;
    p = evv_arena_alloc(n);
    if (p != 0)
        memcpy(p, s, n);
    return p;
}

void *evv_arena_calloc(size_t n, size_t m)
{
    size_t want = n * m;
    void *p;

    if (n != 0 && want / n != m)
        return 0;
    p = evv_arena_alloc(want);
    if (p != 0)
        memset(p, 0, want);
    return p;
}

void *evv_arena_realloc(void *p, size_t n)
{
    head *b;
    void *out;
    size_t had;

    if (p == 0)
        return evv_arena_alloc(n);
    if (n == 0) {
        evv_arena_free(p);
        return 0;
    }

    b = (head *)((unsigned char *)p - ALIGN);
    check(b);
    had = b->size - ALIGN;
    if (had >= ROUND(n))
        return p;

    out = evv_arena_alloc(n);
    if (out == 0)
        return 0;
    memcpy(out, p, had);
    evv_arena_free(p);
    return out;
}

/* ---- a thread's stack ------------------------------------------------- */

void *evv_arena_stack(size_t n)
{
    unsigned char *p = evv_arena_alloc(n + 4096);

    if (p == 0)
        return 0;
    return (void *)(((uintptr_t)p + 4095) & ~(uintptr_t)4095);
}

int32_t evv_ref_checked(const void *p)
{
    uintptr_t v = (uintptr_t)p;

    if (p == 0)
        return 0;
    if (v >= 0x80000000u) {
        /* Truncating this would hand the machine an address that is not the
           one asked for. Everything the machine can hold a pointer to is meant
           to be low: the heap and the thread stacks come from the arena, and
           the language's own tables are in the program, which is linked below
           two gigabytes for exactly this reason. Something that got here came
           from neither. */
        fprintf(stderr, "evv: %p is too high to be a value\n", p);
        abort();
    }
    return (int32_t)(uint32_t)v;
}

#endif
