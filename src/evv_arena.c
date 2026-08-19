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
   block, header included, so walking is a matter of adding it. */
typedef struct {
    uint32_t size;
    uint32_t used;
} head;

static head *first;

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
    unsigned char *p = (unsigned char *)b + b->size;

    if (p >= evv_arena_base + evv_arena_size)
        return 0;
    return (head *)p;
}

void *evv_arena_alloc(size_t n)
{
    size_t want = ROUND(n) + ALIGN;
    head *b;

    if (evv_arena_base == 0 || n == 0)
        return 0;

    for (b = first; b != 0; b = next_block(b)) {
        head *rest;

        if (b->used || b->size < want)
            continue;

        if (b->size >= want + ALIGN * 2) {
            rest = (head *)((unsigned char *)b + want);
            rest->size = (uint32_t)(b->size - want);
            rest->used = 0;
            b->size = (uint32_t)want;
        }
        b->used = 1;
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

/* ---- one rule's frame ------------------------------------------------- */

/* A rule hands the address of its own frame to the machine, so the frame
   cannot be on the thread's stack, which is nowhere near low enough. They
   nest strictly, so a stack of its own inside the arena is all that is
   wanted. */

#define FRAME_STACK (1024u * 1024u)

static __thread unsigned char *fs_base, *fs_top, *fs_end;

void *evv_frame_push(size_t n)
{
    unsigned char *p;

    if (fs_base == 0) {
        fs_base = evv_arena_alloc(FRAME_STACK);
        if (fs_base == 0)
            return 0;
        fs_top = fs_base;
        fs_end = fs_base + FRAME_STACK;
    }

    n = ROUND(n);
    if (fs_top + n > fs_end)
        return 0;

    p = fs_top;
    fs_top += n;
    return p;
}

void evv_frame_pop(void *p)
{
    if (p != 0)
        fs_top = (unsigned char *)p;
}

int32_t evv_ref_checked(const void *p)
{
    uintptr_t v = (uintptr_t)p;

    if (p == 0)
        return 0;
    if (v < (uintptr_t)evv_arena_base
        || v >= (uintptr_t)evv_arena_base + evv_arena_size) {
        /* Truncating this would hand the machine an address that is not the
           one asked for, and the fault is in whoever allocated it outside the
           arena rather than here. */
        fprintf(stderr, "evv: %p is not in the arena and cannot be a value\n",
                p);
        abort();
    }
    return (int32_t)(uint32_t)v;
}

#endif
