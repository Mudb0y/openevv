/* What each rule was addressing, measured rather than inferred.
 *
 * A rule reaches into memory through a register and a byte offset -- `ind reg
 * r6 1272' in the notation, `*(int32_t *)(r6 + 1272)' as C -- and nothing
 * written down says what r6 was holding. That is the one thing standing
 * between these rules and code a person can read, and between the machine's
 * layouts and layouts of ours: an offset can only be turned into a field name
 * once the object it is an offset into is known.
 *
 * Reading it out of the rules statically means typing the 278 entry points the
 * machine offers, and their C signatures answer `int' or `int32_t' every time,
 * because a reference is a value here. So the object a call hands back is not
 * recoverable from the types. What is recoverable is where a block came from,
 * because the allocator records that already: every block in the arena carries
 * the return address of whoever asked for it, the language's own stores are
 * registered by src/delta/delta_low.c, and a frame is on the C stack. So the
 * question is put to the allocator at run time rather than guessed at compile
 * time, over the 979 cases of the gate and the twenty thousand words beside
 * it.
 *
 * What this cannot answer is a site no case reaches, and that is exactly the
 * thing worth knowing: an unreached site is where a later change to a layout
 * would break something silently. So a count is kept per site, and the sites
 * nothing reached are counted rather than assumed away.
 *
 * The file it keeps is binary and is added to rather than replaced, because
 * the gate speaks one case per process and the answer wanted is the union over
 * all of them. tools/rules/provenance.py renders it against the site table
 * the decompiler wrote.
 *
 * Off unless asked for: build with -DEVV_PROVENANCE=1 and run the decompiler
 * with EVV_RULE_PROVENANCE=1, which is what emits the site numbers. Without
 * both, none of this is compiled and a rule is the expression it always was.
 */

#if defined(EVV_PROVENANCE) && EVV_PROVENANCE

#define _GNU_SOURCE

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#if !defined(_WIN32)
#include <dlfcn.h>
#include <fcntl.h>
#include <unistd.h>
#endif

#include "evv_arena.h"
#include "delta_prov.h"

/* evv_arena.h turns malloc and its family into the arena's own, which is
   right for everything the engine allocates and wrong for this: the census
   must not appear in the region it is measuring, nor spend room the engine is
   going to want. stdlib.h above declared the real ones before the macros were
   defined, so taking the macros away here gets them back. */
#undef malloc
#undef calloc
#undef realloc
#undef free

/* What a site was seen addressing. A site that only ever addresses one of
   these is one whose offset can be turned into a field name; a site that
   addresses two is polymorphic and stays arithmetic until something finer
   says otherwise. */
enum {
    SEEN_NULL,        /* the reference was nought */
    SEEN_STORE,       /* a store of the language's data, copied out low */
    SEEN_BLOCK,       /* a live block of the arena, and who asked for it */
    SEEN_STACK,       /* the C stack, so a rule's own frame */
    SEEN_ELSEWHERE,   /* in the arena but in no live block */
    SEEN_OUTSIDE,     /* not the arena at all */
    SEEN_KINDS
};

/* How many births one site may be seen to come from before this gives up on
   counting them, how many places altogether, and how much of a file name is
   kept. */
enum { BIRTH_MAX = 1024, BIRTH_FILE = 40, BORN_MAX = 4 };

/* One site, as it is kept and as it is written. Fixed widths, because the file
   is read back by the next process and by a Python tool and both have to agree
   about it without negotiating. */
typedef struct {
    uint64_t count[SEEN_KINDS];
    uint32_t whence;       /* the one allocation site, while there is one */
    uint32_t many_whence;  /* or that there has been more than one */
    /* And how big that block was. The allocator's own name is too coarse on
       its own: delta_lang_alloc serves every record the language keeps, so
       knowing a site addresses one of its blocks says the storage class and
       not the object. A size does most of the rest of the work, since records
       of different shapes are rarely the same length. */
    uint32_t bytes;
    uint32_t many_bytes;
    /* And which births the pointer came from -- indexes into the birth table
       below, one-based so that nought is an empty slot. This is the answer the
       allocator could not give: a birth is a place in the engine's own code
       where the pointer still had a C type.
     *
       A few rather than one, because several places commonly name one type:
       the state reaches the rules through four wrapper functions in a language
       module, each with its own EVV_REF, and a site seeing two of those is
       seeing one type twice. Keeping only the first and a flag would count
       that as ambiguity, which is what the first version did and why this is
       an array. Four is enough for every site measured; the flag says when it
       was not. */
    uint32_t born[BORN_MAX];
    uint32_t many_born;
} site;

/* One place a reference is made, by the file and line of the EVV_REF that made
   it. The source line names the type, which is the whole point: the census
   says a rule site addresses the object born at delta.c:3724 and the source
   says what that object is. */

typedef struct {
    char     file[BIRTH_FILE];
    uint32_t line;
} birth;

static birth births[BIRTH_MAX];
static uint32_t birth_count;

/* The last file name seen, so that the common case costs a pointer compare
   rather than a walk: a run of references from one place is the usual shape. */
static const char *birth_last_file;
static uint32_t    birth_last_line;
static uint32_t    birth_last_index;

static uint32_t birth_index(const char *file, uint32_t line)
{
    const char *base = file;
    const char *p;
    uint32_t i;

    if (file == birth_last_file && line == birth_last_line
        && birth_last_index != 0)
        return birth_last_index;

    for (p = file; *p; p++)
        if (*p == '/' || *p == '\\')
            base = p + 1;

    for (i = 0; i < birth_count; i++)
        if (births[i].line == line && strcmp(births[i].file, base) == 0)
            break;

    if (i == birth_count) {
        if (birth_count == BIRTH_MAX)
            return 0;
        strncpy(births[i].file, base, BIRTH_FILE - 1);
        births[i].file[BIRTH_FILE - 1] = 0;
        births[i].line = line;
        birth_count++;
    }

    birth_last_file = file;
    birth_last_line = line;
    birth_last_index = i + 1;
    return i + 1;
}

/* Which birth an address came from. Open addressing over the address itself,
   the newest write winning, because a block reused for something else is
   something else now. Exact addresses only: a rule holds the value a birth
   handed back, so the base it reaches through is the address that was born,
   and a derived one is a different question this does not pretend to answer. */
enum { SHADOW_BITS = 20, SHADOW_SIZE = 1u << SHADOW_BITS };

static struct {
    uintptr_t at;
    uint32_t  born;
} *shadow;

static uint32_t shadow_at(uintptr_t at)
{
    uint32_t h = (uint32_t)((at >> 4) * 2654435761u) >> (32 - SHADOW_BITS);
    uint32_t i;

    if (shadow == 0)
        return 0;
    for (i = 0; i < 8; i++) {
        uint32_t k = (h + i) & (SHADOW_SIZE - 1);

        if (shadow[k].at == at)
            return shadow[k].born;
        if (shadow[k].at == 0)
            return 0;
    }
    return 0;
}

static void shadow_put(uintptr_t at, uint32_t born)
{
    uint32_t h = (uint32_t)((at >> 4) * 2654435761u) >> (32 - SHADOW_BITS);
    uint32_t i;

    if (shadow == 0) {
        shadow = calloc(SHADOW_SIZE, sizeof *shadow);
        if (shadow == 0)
            return;
    }
    for (i = 0; i < 8; i++) {
        uint32_t k = (h + i) & (SHADOW_SIZE - 1);

        if (shadow[k].at == at || shadow[k].at == 0) {
            shadow[k].at = at;
            shadow[k].born = born;
            return;
        }
    }
    /* Full where this address wants to sit. Take the first slot: a census that
       forgets a birth answers `unknown' for a site, which is honest, where one
       that keeps a stale birth answers wrongly. */
    shadow[h & (SHADOW_SIZE - 1)].at = at;
    shadow[h & (SHADOW_SIZE - 1)].born = born;
}

int32_t evv_prov_born(const char *file, int line, const void *p)
{
    if (p != 0)
        shadow_put((uintptr_t)p, birth_index(file, (uint32_t)line));
    return evv_ref_checked(p);
}

/* PRV2. The layout below is this file's own business and is read back only by
   the next process and by tools/rules/provenance.py, so it carries a mark
   rather than a version: a census written by an older build is refused and
   started again rather than misread a field at a time. */
#define PROV_MAGIC 0x32565250u

/* One allocation site, said as a name. Resolved in the process that saw it,
   because the recorded value is a truncated address in an image loaded
   wherever the system felt like: nothing outside that process can turn it
   back into a name, so the file carries the answer rather than the question. */
enum { WHENCE_MAX = 512, WHENCE_NAME = 48 };

typedef struct {
    uint32_t whence;
    char     name[WHENCE_NAME];
} prov_name;

typedef struct {
    uint32_t magic;
    uint32_t sites;
    uint32_t names;
    uint32_t rows;      /* how many births follow the names */
    /* Where this image was loaded. The recorded allocation site is a truncated
       address, and most of what allocates here is a static function, which
       dladdr cannot name because it is not a dynamic symbol. With the load
       base written down, nm can: it sees local symbols, and the offset into
       the image is what the two have in common. */
    uint64_t base;
} prov_head;

static prov_name names[WHENCE_MAX];
static uint32_t  name_count;

static const char *known_name(uint32_t whence)
{
    uint32_t i;

    for (i = 0; i < name_count; i++)
        if (names[i].whence == whence)
            return names[i].name;
    return 0;
}

static void remember_name(uint32_t whence, const char *what)
{
    if (what == 0 || *what == 0 || known_name(whence) != 0)
        return;
    if (name_count == WHENCE_MAX)
        return;
    names[name_count].whence = whence;
    strncpy(names[name_count].name, what, WHENCE_NAME - 1);
    names[name_count].name[WHENCE_NAME - 1] = 0;
    name_count++;
}

static site    *sites;
static uint32_t site_room;
static uint32_t site_high;   /* one past the highest site ever noted */

/* Where the C stack is, near enough. Taken from the first note rather than
   from anything the system says: all this has to do is tell a frame from a
   heap block, and they are megabytes apart. */
static uintptr_t stack_near;

static int armed;

static const char *prov_out(void)
{
    const char *p = getenv("EVV_PROV_OUT");

    return p && *p ? p : "provenance.bin";
}

static int room_for(uint32_t which)
{
    uint32_t want;
    site *grown;

    if (which < site_room) {
        if (which >= site_high)
            site_high = which + 1;
        return 1;
    }

    want = site_room ? site_room : 1024;
    while (want <= which)
        want *= 2;

    grown = realloc(sites, (size_t)want * sizeof *sites);
    if (grown == 0)
        return 0;
    memset(grown + site_room, 0, (size_t)(want - site_room) * sizeof *grown);
    sites = grown;
    site_room = want;
    site_high = which + 1;
    return 1;
}

/* The census is one file that many processes add to: the gate speaks a case
   per process and test/words.sh speaks twenty thousand words several at a
   time. So the whole read-add-write happens at the end, under a lock over the
   file, rather than being read at the start and written at the end -- between
   those two another process's work would be lost.
 *
   An advisory lock is enough because every writer here takes it. Where there
   is no fcntl to take one, the runs are serial or the answer is the last
   writer's, and the report says which by the counts being lower than the
   cases run. */
static int lock_census(const char *path, FILE **out)
{
#if !defined(_WIN32)
    int fd = open(path, O_RDWR | O_CREAT, 0666);
    struct flock l;

    if (fd < 0)
        return -1;
    memset(&l, 0, sizeof l);
    l.l_type = F_WRLCK;
    l.l_whence = SEEK_SET;
    while (fcntl(fd, F_SETLKW, &l) < 0)
        ;
    *out = fdopen(fd, "r+b");
    if (*out == 0) {
        close(fd);
        return -1;
    }
    return fd;
#else
    *out = fopen(path, "r+b");
    if (*out == 0)
        *out = fopen(path, "w+b");
    return *out ? 0 : -1;
#endif
}

/* What an earlier process left, added into what this one saw. */
/* What an earlier process left, added into what this one saw.
 *
 * Read out of order on purpose. Every process numbers its births in the order
 * it happened to see them, so an index of one run means nothing in another and
 * has to be translated through the table that run wrote. That table sits after
 * the sites in the file, so it is read first and the sites second. */
static void merge_prior(FILE *f)
{
    prov_head h;
    long sites_at;
    uint32_t prior[BIRTH_MAX + 1];
    uint32_t i;
    int k;

    rewind(f);
    if (fread(&h, sizeof h, 1, f) != 1 || h.magic != PROV_MAGIC
        || h.sites == 0 || !room_for(h.sites - 1))
        return;

    sites_at = ftell(f);
    if (h.names > WHENCE_MAX)
        h.names = WHENCE_MAX;
    if (h.rows > BIRTH_MAX)
        h.rows = BIRTH_MAX;

    /* The names it resolved, kept because it may have seen an allocation site
       this process never reaches. */
    if (fseek(f, sites_at + (long)h.sites * (long)sizeof(site), SEEK_SET) != 0)
        return;
    for (i = 0; i < h.names; i++) {
        prov_name was;

        if (fread(&was, sizeof was, 1, f) != 1)
            return;
        remember_name(was.whence, was.name);
    }

    /* Then its births, each turned into this process's own number for the
       same place. */
    memset(prior, 0, sizeof prior);
    for (i = 0; i < h.rows; i++) {
        birth was;

        if (fread(&was, sizeof was, 1, f) != 1)
            return;
        prior[i + 1] = birth_index(was.file, was.line);
    }

    if (fseek(f, sites_at, SEEK_SET) != 0)
        return;
    for (i = 0; i < h.sites; i++) {
        site was;

        if (fread(&was, sizeof was, 1, f) != 1)
            return;
        for (k = 0; k < SEEN_KINDS; k++)
            sites[i].count[k] += was.count[k];

        {
            int j;

            for (j = 0; j < BORN_MAX && was.born[j] != 0; j++) {
                uint32_t mine = prior[was.born[j]];
                int k;

                if (mine == 0)
                    continue;
                for (k = 0; k < BORN_MAX; k++) {
                    if (sites[i].born[k] == mine)
                        break;
                    if (sites[i].born[k] == 0) {
                        sites[i].born[k] = mine;
                        break;
                    }
                }
                if (k == BORN_MAX)
                    sites[i].many_born = 1;
            }
        }
        sites[i].many_born |= was.many_born;

        /* Two runs disagreeing about the allocator or the length is the same
           answer as one run seeing two. */
        if (was.count[SEEN_BLOCK] != 0) {
            if (sites[i].count[SEEN_BLOCK] == was.count[SEEN_BLOCK]) {
                sites[i].whence = was.whence;
                sites[i].bytes = was.bytes;
            } else {
                if (sites[i].whence != was.whence)
                    sites[i].many_whence = 1;
                if (sites[i].bytes != was.bytes)
                    sites[i].many_bytes = 1;
            }
            sites[i].many_whence |= was.many_whence;
            sites[i].many_bytes |= was.many_bytes;
        }
    }
}

static const char *whence_name(uint32_t whence);

/* Where this image starts, so that a truncated allocation site can be turned
   back into an offset into it. */
static uint64_t image_base(void)
{
#if !defined(_WIN32)
    Dl_info here;

    if (dladdr((void *)(uintptr_t)(void *)image_base, &here))
        return (uint64_t)(uintptr_t)here.dli_fbase;
#endif
    return 0;
}

static void save(void)
{
    FILE *f;
    prov_head h;
    uint32_t i;

    if (sites == 0 || site_high == 0)
        return;

    /* Name every allocation site this run saw, while there is still a process
       able to answer. */
    for (i = 0; i < site_high; i++)
        if (sites[i].count[SEEN_BLOCK] != 0)
            remember_name(sites[i].whence, whence_name(sites[i].whence));

    if (lock_census(prov_out(), &f) < 0 || f == 0)
        return;
    merge_prior(f);
    rewind(f);
    h.magic = PROV_MAGIC;
    h.sites = site_high;
    h.names = name_count;
    h.rows = birth_count;
    h.base = image_base();
    fwrite(&h, sizeof h, 1, f);
    fwrite(sites, sizeof *sites, site_high, f);
    if (name_count != 0)
        fwrite(names, sizeof *names, name_count, f);
    if (birth_count != 0)
        fwrite(births, sizeof *births, birth_count, f);
    fflush(f);
#if !defined(_WIN32)
    if (ftruncate(fileno(f), ftell(f)) != 0)
        ;   /* a census longer than it should be is read by its header */
#endif
    fclose(f);
}

void evv_prov_note(uint32_t which, const void *p)
{
    uintptr_t at = (uintptr_t)p;
    site *s;

    if (!armed) {
        armed = 1;
        stack_near = (uintptr_t)(void *)&at;
        atexit(save);
    }
    if (!room_for(which))
        return;
    s = &sites[which];

    if (p == 0) {
        s->count[SEEN_NULL]++;
        return;
    }

    /* Which birth this pointer came from, which is the answer the allocator
       could not give. Kept beside the storage kind rather than instead of it:
       a site whose births disagree is one the rules reach with more than one
       sort of object, and that is worth knowing whatever the storage says. */
    {
        uint32_t born = shadow_at(at);

        if (born != 0) {
            int k;

            for (k = 0; k < BORN_MAX; k++) {
                if (s->born[k] == born)
                    break;
                if (s->born[k] == 0) {
                    s->born[k] = born;
                    break;
                }
            }
            if (k == BORN_MAX)
                s->many_born = 1;
        }
    }

    /* A frame first, because it is the commonest and the cheapest to answer:
       the C stack is nowhere near the arena. */
    {
        uintptr_t d = at > stack_near ? at - stack_near : stack_near - at;

        if (d < (uintptr_t)(8u * 1024u * 1024u)) {
            s->count[SEEN_STACK]++;
            return;
        }
    }

    if (delta_low_store_of(p) != 0) {
        s->count[SEEN_STORE]++;
        return;
    }

    if (evv_arena_base != 0
        && at >= (uintptr_t)evv_arena_base
        && at < (uintptr_t)evv_arena_base + evv_arena_size) {
        uint32_t whence = 0;
        uint32_t bytes = 0;

        if (evv_arena_whence_of2(p, &whence, &bytes)) {
            if (s->count[SEEN_BLOCK] == 0) {
                s->whence = whence;
                s->bytes = bytes;
            } else {
                if (s->whence != whence)
                    s->many_whence = 1;
                if (s->bytes != bytes)
                    s->many_bytes = 1;
            }
            s->count[SEEN_BLOCK]++;
        } else {
            s->count[SEEN_ELSEWHERE]++;
        }
        return;
    }

    s->count[SEEN_OUTSIDE]++;
}

/* Who asked for a block, by name.
 *
 * The allocator keeps the return address in thirty-two bits, so the top half
 * has to be put back before anyone can be asked who it is -- and an image is
 * not loaded at a multiple of four gigabytes, so the top half is not simply
 * this function's own. What is true is that the full address agrees with the
 * recorded one in its low thirty-two bits and lies in this image, which leaves
 * three candidates and one of them right.
 *
 * Printed by the report rather than kept, so it is called once per distinct
 * allocation site and the walk costs nothing. */
static const char *whence_name(uint32_t whence)
{
#if !defined(_WIN32)
    Dl_info here;
    int i;

    if (!dladdr((void *)(uintptr_t)(void *)whence_name, &here))
        return 0;

    for (i = -1; i <= 1; i++) {
        uintptr_t base = (uintptr_t)here.dli_fbase;
        uintptr_t at = ((base & ~(uintptr_t)0xffffffffu) + whence)
                     + (uintptr_t)((intptr_t)i * (intptr_t)0x100000000);
        Dl_info info;

        if (dladdr((void *)at, &info) && info.dli_fbase == here.dli_fbase
            && info.dli_sname != 0)
            return info.dli_sname;
    }
#else
    (void)whence;
#endif
    return 0;
}

#endif /* EVV_PROVENANCE */
