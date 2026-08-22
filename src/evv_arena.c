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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(EVV_ARENA) && EVV_ARENA

/* One lock over the whole arena. The engine allocates from three threads at
   once, and the ordinary allocator this stands in for is thread-safe; a
   first-fit walk that two threads are in at the same time is not.

   Both locks are ones that can be initialised where they are declared. The
   first allocation is what opens the arena, so a lock needing a call before
   its first use would need that call from somewhere earlier than any of
   this. */
#if defined(_WIN32)

#include <windows.h>

static SRWLOCK arena_lock = SRWLOCK_INIT;

static void arena_take(void) { AcquireSRWLockExclusive(&arena_lock); }
static void arena_drop(void) { ReleaseSRWLockExclusive(&arena_lock); }

#else

#include <pthread.h>
#include <sys/mman.h>
#include <unistd.h>

static pthread_mutex_t arena_lock = PTHREAD_MUTEX_INITIALIZER;

static void arena_take(void) { pthread_mutex_lock(&arena_lock); }
static void arena_drop(void) { pthread_mutex_unlock(&arena_lock); }

#endif

unsigned char *evv_arena_base;
size_t         evv_arena_size;

/* Where to try. Below two gigabytes, so that a reference stays positive when it
   is read back out of a signed slot, and clear of where a program and its heap
   usually land.

   The ask is exact and the walk is wide, and both were learned the hard way.
   Asking for an address and taking whatever the system felt like giving could
   land the region above two gigabytes, where a value cannot name it. Asking at
   only seven places meant a machine with something at all seven -- a CI runner,
   and a real Windows rather than Wine -- got no arena, and then a null pointer
   with no explanation. The walk starts above where a program's own heap grows
   and runs to just under two gigabytes. */
#define ARENA_FIRST 0x10000000u
#define ARENA_LAST  0x78000000u
#define ARENA_STEP  0x01000000u

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

/* One try, at exactly the address asked for. Answers the region or nothing:
   somewhere else is no use, since the whole point of the address is that a
   thirty-two bit value can name it. */
static void *arena_map(uintptr_t at, size_t bytes)
{
#if defined(_WIN32)
    /* Committed rather than only reserved, which charges the commit but still
       costs no memory until a page is touched. A base that is not free fails
       rather than moving, which is what is wanted. */
    return VirtualAlloc((void *)at, bytes, MEM_RESERVE | MEM_COMMIT,
                        PAGE_READWRITE);
#else
    void *got;

#ifdef MAP_FIXED_NOREPLACE
    /* Linux 4.17 and later: this address or nothing, and nothing rather than
       somebody else's mapping. */
    got = mmap((void *)at, bytes, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED_NOREPLACE, -1, 0);
    if (got != MAP_FAILED) {
        if ((uintptr_t)got == at)
            return got;
        /* An older kernel took the flag as a hint. Give it back. */
        munmap(got, bytes);
    }
#endif
    got = mmap((void *)at, bytes, PROT_READ | PROT_WRITE,
               MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (got == MAP_FAILED)
        return 0;
    if ((uintptr_t)got + bytes < 0x80000000u)
        return got;
    munmap(got, bytes);
    return 0;
#endif
}

int evv_arena_open(size_t bytes)
{
    size_t want;

    if (evv_arena_base != 0)
        return 1;

    /* Less room is worse than plenty and better than nothing, so a machine
       whose low addresses are too crowded for the whole region is offered
       halves rather than refused. */
    for (want = ROUND(bytes); want >= (32u * 1024u * 1024u); want /= 2) {
        uintptr_t at;

        for (at = ARENA_FIRST; at <= ARENA_LAST; at += ARENA_STEP) {
            void *got;

            if (at + want > 0x80000000u)
                break;
            got = arena_map(at, want);
            if (got == 0)
                continue;
            evv_arena_base = got;
            evv_arena_size = want;
            break;
        }
        if (evv_arena_base != 0)
            break;
    }

    if (evv_arena_base == 0) {
        /* There is nothing sensible to answer. Everything the engine allocates
           comes from this region, so without it the next thing to happen is a
           null pointer with no explanation, which is exactly how this was
           found. Say it instead. */
        fprintf(stderr, "evv: nowhere below two gigabytes to put the arena;"
                " the engine cannot run on this machine\n");
        abort();
    }

    if (getenv("EVV_ARENA_TRACE") != 0)
        fprintf(stderr, "evv: arena at %p, %lu bytes\n",
                (void *)evv_arena_base, (unsigned long)evv_arena_size);

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
#if defined(_WIN32)
        VirtualFree(evv_arena_base, 0, MEM_RELEASE);
#else
        munmap(evv_arena_base, evv_arena_size);
#endif
    evv_arena_base = 0;
    evv_arena_size = 0;
    first = 0;
}

/* Whether something came out of here at all. */
static int in_arena(const void *p)
{
    const unsigned char *c = p;

    return c >= evv_arena_base && c < evv_arena_base + evv_arena_size;
}

static head *next_block(head *b)
{
    unsigned char *p;

    check(b);
    p = (unsigned char *)b + b->size;
    /* The whole header, not just its first byte: a block ending within
       sizeof(head) of the end would otherwise be read past the region, which
       is a fault rather than an answer. */
    if (p + sizeof(head) > evv_arena_base + evv_arena_size)
        return 0;
    return (head *)p;
}

/* How much to take when nobody said. The pages are not touched until they are
   used, so asking for a lot costs nothing but address space, and the whole
   point of the region is that it has to sit at an address a 32-bit value can
   name, which is not somewhere to be short of room. */
#ifndef ARENA_DEFAULT
#define ARENA_DEFAULT (256u * 1024u * 1024u)
#endif

/* Who asked, as a number, for a block that is overrun to be able to name the
   allocation in front of it.
 *
   Taken from the caller rather than worked out here. It used to be
   __builtin_return_address(1), which GCC's own manual says may have
   unpredictable effects including a crash, and it did: with one compiler's
   inlining the frame above was walkable and with another's it was not, so the
   engine built by Ubuntu's gcc faulted inside the allocator on the first
   sentence while the same source built here was fine. */
/* ---- the guard ---------------------------------------------------------
 *
 * Off unless asked for: build with -DEVV_ARENA_GUARD=1. What it is for is one
 * kind of fault, which has now been found four times in this engine and will
 * be found again: a structure that IBM's code allocated by a constant byte
 * count, or that our port measures with sizeof, being written past its end
 * because something else believes it is longer. Everything the engine
 * allocates comes from here, so a write past one block lands on the next
 * block's header, and the first anyone hears of it is the allocator walking a
 * size that cannot be right -- twenty engine instances later, nowhere near
 * whoever did it.
 *
 * So: remember what each block was asked for, fill the rest of its payload
 * with a byte nobody would write, and check every used block on every call in
 * or out. Then the report names the block, what it was asked for, the
 * allocation that asked, how far past the end the write went, and what the
 * bytes there are -- which is the part that says whether it was one store or a
 * run, and how long a run.
 *
 * Three things about it are deliberate, and each was learned by getting it
 * wrong.
 *
 * What was asked for is kept beside the arena and not in the header. Widening
 * the header moves every payload and changes the alignment of everything, and
 * then the program being debugged is not the program that faults.
 *
 * Every block gets slack even when the request is already a multiple of the
 * alignment, or an aligned request has no poison window at all and its
 * overrun goes straight onto the next header, which is the very thing this
 * exists to avoid.
 *
 * Growing a block in place has to say so. evv_arena_realloc answers the same
 * pointer whenever the payload it already has is big enough, and a payload is
 * often bigger than what was asked for, so without a fresh note the caller
 * writes legitimately into what the guard still believes is poison. That
 * reads exactly like a bug and is not one.
 */
#if defined(EVV_ARENA_GUARD) && EVV_ARENA_GUARD

#define GUARD_SLOTS  65536u
#define GUARD_BYTE   0xa5
#define GUARD_SLACK  64

static struct {
    const void *at;
    uint32_t    asked;
    uint32_t    whence;
} guard[GUARD_SLOTS];

static size_t guard_at(const void *p)
{
    size_t h = (size_t)(((uintptr_t)p >> 4) & (GUARD_SLOTS - 1));
    size_t i;

    for (i = 0; i < GUARD_SLOTS; i++) {
        size_t k = (h + i) & (GUARD_SLOTS - 1);

        if (guard[k].at == 0 || guard[k].at == p)
            return k;
    }
    fprintf(stderr, "evv: the arena guard has run out of room\n");
    abort();
}

static void guard_took(head *b, void *p, size_t n)
{
    unsigned char *from = (unsigned char *)p + n;
    unsigned char *to = (unsigned char *)b + b->size;
    size_t k = guard_at(p);

    guard[k].at = p;
    guard[k].asked = (uint32_t)n;
    guard[k].whence = b->whence;
    if (to > from)
        memset(from, GUARD_BYTE, (size_t)(to - from));
}

/* A block given back stops being watched, and this is not a nicety. The arena
   joins what is free and hands the same address out again for something else,
   often something bigger; a note left behind says that block is 144 bytes when
   it is now twenty thousand, and then every legitimate write into it is
   reported as an overrun. That cost an hour of chasing the wrong memcpy. */
static void guard_gave_back(void *p)
{
    size_t k = guard_at(p);

    if (guard[k].at == p)
        guard[k].at = 0;
}

static void guard_check(const char *doing)
{
    head *b;
    int n = 0;

    for (b = first; b != 0; b = next_block(b), n++) {
        unsigned char *p = (unsigned char *)b + ALIGN;
        unsigned char *to = (unsigned char *)b + b->size;
        size_t k, i, asked;

        if (!b->used)
            continue;
        k = guard_at(p);
        if (guard[k].at != p)
            continue;
        asked = guard[k].asked;
        for (i = asked; p + i < to; i++) {
            if (p[i] == GUARD_BYTE)
                continue;
            fprintf(stderr, "evv: while %s, block %d at %p, asked for %u"
                    " from %08x, was written at %u with %02x\n",
                    doing, n, (void *)p, (unsigned)asked, guard[k].whence,
                    (unsigned)i, p[i]);
            fprintf(stderr, "evv: from the end of what it asked for:");
            for (i = asked; p + i < to && i < asked + 64; i++)
                fprintf(stderr, " %02x", p[i]);
            fprintf(stderr, "\n");
            /* Whether anything else believes it owns what was written. A
               second block over the same address is the allocator handing out
               what it had already given away, which is a different fault
               entirely from somebody writing past their own end. */
            {
                head *w;
                int m = 0;

                for (w = first; w != 0; w = next_block(w), m++) {
                    unsigned char *q = (unsigned char *)w + ALIGN;

                    if (w == b || !w->used)
                        continue;
                    if (p + i >= q && p + i < (unsigned char *)w + w->size)
                        fprintf(stderr, "evv: block %d at %p, %u bytes from"
                                " %08x, covers that address as well\n",
                                m, (void *)q, w->size, w->whence);
                }
            }
            abort();
        }
    }
}

#else
#define guard_took(b, p, n)  ((void)0)
#define guard_gave_back(p)   ((void)0)
#define guard_check(doing)   ((void)0)
#define GUARD_SLACK          0
#endif

static void *arena_alloc(size_t n, uint32_t whence)
{
    size_t want = ROUND(n) + ALIGN + GUARD_SLACK;
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
        b->whence = whence;
        guard_took(b, (unsigned char *)b + ALIGN, n);
        return (unsigned char *)b + ALIGN;
    }
    return 0;
}

static void arena_free(void *p)
{
    head *b, *w;

    if (p == 0)
        return;

    /* Reading the header of something that came from somewhere else is how a
       wild pointer turns into a fault with nothing said. Say it. */
    if (!in_arena(p)) {
        fprintf(stderr, "evv: %p was given back to the arena and did not come"
                " from it\n", p);
        abort();
    }

    b = (head *)((unsigned char *)p - ALIGN);
    check(b);
    b->used = 0;
    guard_gave_back(p);

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

void *evv_arena_alloc(size_t n)
{
    uint32_t whence = (uint32_t)(uintptr_t)__builtin_return_address(0);
    void *p;

    arena_take();
    guard_check("taking a block");
    p = arena_alloc(n, whence);
    arena_drop();
    return p;
}

void evv_arena_free(void *p)
{
    arena_take();
    guard_check("giving a block back");
    arena_free(p);
    arena_drop();
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

    arena_take();
    guard_check("growing a block");
    if (!in_arena(p)) {
        fprintf(stderr, "evv: %p was handed to the arena to grow and did not"
                " come from it\n", p);
        abort();
    }
    b = (head *)((unsigned char *)p - ALIGN);
    check(b);
    had = b->size - ALIGN;
    if (had >= ROUND(n)) {
        /* The same block, and now used further into its payload than the
           guard was told: say so, or a legitimate write reads as an overrun. */
        guard_took(b, p, n);
        arena_drop();
        return p;
    }
    out = arena_alloc(n, (uint32_t)(uintptr_t)__builtin_return_address(0));
    if (out != 0) {
        memcpy(out, p, had);
        arena_free(p);
    }
    arena_drop();
    return out;
}

int32_t evv_ref_checked(const void *p)
{
    uintptr_t v = (uintptr_t)p;

    if (p == 0)
        return 0;
    if (v >= 0x80000000u) {
        /* Truncating this would hand the machine an address that is not the
           one asked for. Everything the machine can hold a pointer to comes
           out of the arena: the heap, and the language's own data, which
           src/delta_low.c copies out of the program at startup. Something that
           got here came from neither. */
        fprintf(stderr, "evv: %p is too high to be a value\n", p);
        abort();
    }
    return (int32_t)(uint32_t)v;
}

/* ---- what is still held ------------------------------------------------
 *
 * Everything the engine allocates comes from here, so anything an instance
 * keeps after it has been deleted is a used block still on this list. Grouped
 * by the allocation that asked and the size it got, a leak is the group whose
 * count goes up by the same amount every time an instance is made and thrown
 * away -- which is what tells the two apart from the blocks the process holds
 * for its own life and rightly never gives back.
 *
 * `whence' is the low thirty-two bits of a return address. To turn one into a
 * line, build without position independence -- `make CFLAGS=-no-pie' -- and
 * hand it to addr2line; the engine has run that way and still does.
 */
void evv_arena_outstanding(const char *when)
{
    enum { GROUPS = 256 };
    struct { uint32_t whence, size; long n; } g[GROUPS];
    int used = 0, i, j;
    unsigned long blocks = 0, bytes = 0;
    head *b;

    for (b = first; b != 0; b = next_block(b)) {
        if (!b->used)
            continue;
        blocks++;
        bytes += b->size;
        for (i = 0; i < used; i++)
            if (g[i].whence == b->whence && g[i].size == b->size)
                break;
        if (i < used) {
            g[i].n++;
        } else if (used < GROUPS) {
            g[used].whence = b->whence;
            g[used].size = b->size;
            g[used].n = 1;
            used++;
        }
    }

    fprintf(stderr, "[arena] %s: %lu blocks, %lu bytes\n", when, blocks, bytes);

    /* The heaviest groups first, since a leak of any size worth finding is
       near the top of that list. */
    for (j = 0; j < 20 && j < used; j++) {
        int top = -1;
        long most = -1;

        for (i = 0; i < used; i++)
            if (g[i].n >= 0 && (long)g[i].n * g[i].size > most) {
                most = (long)g[i].n * g[i].size;
                top = i;
            }
        if (top < 0)
            break;
        fprintf(stderr, "[arena]   %6ld x %8u from %08x = %ld bytes\n",
                g[top].n, g[top].size, g[top].whence, most);
        g[top].n = -1;
    }
}

#else

void evv_arena_outstanding(const char *when)
{
    fprintf(stderr, "[arena] %s: this build has no arena\n", when);
}

#endif

/* ---- one rule's frame ------------------------------------------------- */

#define FRAME_STACK (4u * 1024u * 1024u)
#define FRAME_ALIGN 16
#define FRAME_ROUND(n) (((n) + (FRAME_ALIGN - 1)) & ~(size_t)(FRAME_ALIGN - 1))

static __thread unsigned char *fs_base, *fs_top, *fs_end;

/* A thread's frame stack, given back when the thread is done with it.
 *
 * Four megabytes, taken on the first rule the thread runs and kept for as long
 * as the thread lives, which is right: the frames nest strictly and the stack is
 * reused. What was wrong was that nothing gave it back. Every engine instance
 * starts a synthesis thread, so every instance made and thrown away kept four
 * megabytes of the arena, and the arena is 256 by default -- which is why the
 * engine went quiet on the 63rd instance and reported nothing.
 *
 * Safe here and only here: the thread body has returned, so no rule of that
 * thread is running and nothing holds a frame. */
void evv_frame_done(void)
{
    if (fs_base != 0) {
        evv_arena_free(fs_base);
        fs_base = 0;
        fs_top = 0;
        fs_end = 0;
    }
}

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

    n = FRAME_ROUND(n);
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

