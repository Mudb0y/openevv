/* The user dictionary: words the caller wants said its own way.
 *
 * Three dictionaries are kept, one per volume, and a small object holds
 * the three together. Every method of that holder is the same shape: if
 * the holder is broken, answer with what broke it; otherwise hand the
 * question to the volume asked for. It does not check the volume number,
 * so a fourth one would read past the end of the three, which is what the
 * original does.
 *
 * A dictionary is a hash table from word to replacement, read from a
 * plain text file: a word, a tab, and what to say instead. Both halves
 * are checked character by character against what the stream's first
 * field can name, so a line with anything unsayable in it is dropped
 * whole rather than half-read.
 *
 * A lookup does not just answer; it rewrites the spine in place, taking
 * the word out and putting the replacement in. The word it took out is
 * kept so that the undo can put it back, which is the only reason a
 * dictionary carries any state between calls.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <sys/stat.h>
#include "delta.h"
#include "eci_objects.h"
#include "eci_synththread.h"
#include "evv_abi.h"
#include "klatt_lang.h"

/* The language module's own handle, which is where the current dictionary
   and the stream it works in are kept. */

#define DT_LANG(d)      EVV_AT(DeltaLang *, (d)->dlang)
#define DL_CURRENT(l)   ((l)->current)
#define DL_STREAM(l)    ((l)->stream)

/* Which side of the machine wants to hear that the spine moved. */
#define OWNER_MOVED(d) (EVV_AT(delta_owner *, (d)->owner)->changed)

/* What comes back. Nought is the only good answer; the rest say what went
   wrong, and the holder hands its own out in place of asking a volume. */
#define UD_OK        0
#define UD_NO_FILE   1
#define UD_NO_MEMORY 2
#define UD_NO_SPINE  3
#define UD_NO_TABLE  4
#define UD_NOT_FOUND 5

/* How the state word reads: one when a table has been made and nothing
   put in it, two once something has. */
#define UD_FRESH   1
#define UD_CHANGED 2

/* How big a table to ask for when nothing better is known, and how many
   bytes of file one entry is reckoned to take. */
#define UD_DEFAULT_BUCKETS 0x100
#define UD_BYTES_PER_ENTRY 15

/* What one line, one word and one replacement are allowed to run to. */
#define UD_LINE_ROOM 0x2c0
#define UD_WORD_MAX  0x80
#define UD_XLAT_MAX  0x200

/* How much of a word is kept for the undo. */
#define UD_LAST_ROOM 0x50

/* How many volumes the holder makes. */
#define UD_VOLUMES 3
#define UD_MAIN    0
#define UD_ROOT    1

typedef struct UserDict {
    char     path[0x108];   /* +0x000, what it was told to load */
    void    *hash;          /* +0x108 */
    int32_t  state;         /* +0x10c */
    HashIter iter;          /* +0x110, one walk at a time, so it lives here.
                               Sized by eci_objects.h rather than by IBM's
                               twelve bytes, which is short by half where a
                               pointer is eight and wrote over `last'. */
    char     last[UD_LAST_ROOM];  /* +0x11c, the word a lookup took out */
    int32_t  unknown_16c;
} UserDict;

typedef struct DictionarySet {
    UserDict             *vol[UD_VOLUMES];  /* +0x00 */
    struct DictionarySet **home;            /* +0x0c, where the current one
                                               is recorded */
    delta_state          *machine;          /* +0x10 */
    int32_t               status;           /* +0x14 */
} DictionarySet;

/* What a caller has to allocate for one. Only this file knows what is in it. */
const uint32_t ds_bytes = sizeof(DictionarySet);

/* Whether it managed to open its volumes. */
int32_t ds_failed(const DictionarySet *s)
{
    return s->status;
}

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

extern int32_t fileFindInPath(const char *name, char *out);

extern void    *hashNew(int32_t want, int32_t ownsKeys, int32_t wantPrime);
extern int32_t  hashDelete(void *table, int32_t freeKeys, int32_t freeValues);
extern int32_t  hashInsertString(void *table, char *key, void *value);
extern void    *hashLookupString(void *table, const char *key);
extern int32_t  hashDeleteString(void *table, const char *key,
                                 int32_t freeKey, int32_t freeValue);
extern int32_t  hashIterConstruct(void *iter, void *table);
extern int32_t  hashIterNext(void *iter);
extern const char *hashIterString(void *iter);
extern void    *hashIterRef(void *iter);

extern int         num_streams(delta_state *d);
extern const char *stream_name(int8_t f);
extern int         single_letter_stream(int8_t f);
extern int   sync_in_stm(delta_state *d, int8_t f, int32_t at);
extern int   del_two_point(delta_state *d, int8_t f, int32_t l, int32_t r);
extern char *extract_string(delta_state *d, int8_t f, int32_t l, int32_t r,
                            char *out, int32_t max);
extern int   insert_string(delta_state *d, int8_t f, int32_t at,
                           const char *s);
extern int   non_unique_value(delta_state *d, int8_t f, int32_t fld,
                              const char *s, const char **out_name,
                              void **out_value);

/* A reading that ends in a letter said by name is put in with a space after
   it. The rules take a lone letter before a full stop for an initial and say
   nothing for the stop, so nginx.conf was engine X conf and kubectl.yaml kube
   C T L yaml; with the space the stop is the dot it is. Before a stop that
   ends a sentence, or before a comma, the space changes nothing. */
static int insert_reading(delta_state *d, int8_t stream, int32_t at,
                          const char *text)
{
    char   spaced[0x240];
    size_t n = strlen(text);
    char   c = n ? text[n - 1] : 0;

    if (!((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'))
        || (n > 1 && text[n - 2] != ' ') || n + 2 > sizeof spaced)
        return insert_string(d, stream, at, text);
    memcpy(spaced, text, n);
    spaced[n] = ' ';
    spaced[n + 1] = 0;
    return insert_string(d, stream, at, spaced);
}

THIS UserDict *ud_ctor(UserDict *u);
THIS void      ud_dtor(UserDict *u);
THIS int32_t   ud_buildHashTable(UserDict *u, uint32_t want);
THIS int32_t   ud_addOneEntry(UserDict *u, const char *word,
                              const char *xlat);
THIS int32_t   ud_loadDictionary(UserDict *u, delta_state *d,
                                 const char *name);
THIS int32_t   ud_saveDictionary(UserDict *u, const char *name);
THIS int32_t   ud_update(UserDict *u, const char *word, const char *xlat);
THIS const char *ud_lookup(UserDict *u, const char *word);
THIS int32_t   ud_findFirst(UserDict *u, const char **word,
                            const char **xlat);
THIS int32_t   ud_findNext(UserDict *u, const char **word,
                           const char **xlat);
THIS int32_t   ud_lookupAndTranslate(UserDict *u, delta_state *d,
                                     int32_t l, int32_t r);
THIS int32_t   ud_lookupUndo(UserDict *u, delta_state *d,
                             int32_t l, int32_t r);


/* ---- one dictionary -------------------------------------------------- */

/* Nothing loaded, no table, nothing to undo. */
THIS UserDict *ud_ctor(UserDict *u)
{
    u->hash    = 0;
    u->state   = 0;
    u->path[0] = 0;
    u->last[0] = 0;
    return u;
}

THIS UserDict *ud_ctor_sized(UserDict *u, uint32_t want)
{
    ud_ctor(u);
    ud_buildHashTable(u, want);
    return u;
}

THIS UserDict *ud_ctor_load(UserDict *u, delta_state *d, const char *name)
{
    ud_ctor(u);
    ud_loadDictionary(u, d, name);
    return u;
}

THIS void ud_dtor(UserDict *u)
{
    hashDelete(u->hash, 1, 1);
}

/* A table of the size asked for, or a default when nothing was asked. */
THIS int32_t ud_buildHashTable(UserDict *u, uint32_t want)
{
    int32_t n = want ? (int32_t)want : UD_DEFAULT_BUCKETS;

    u->hash = hashNew(n, 1, 1);
    if (u->hash == 0)
        return 0;

    u->state = UD_FRESH;
    return 1;
}

/* Where the file is and how big it is, or minus one if it is not there.

   A name too long for the caller's buffer, or for the one the name is kept
   in afterwards, is not there either, which is ours rather than IBM's: the
   search copies a name it finds straight into a buffer of 0x108 bytes, so a
   longer path handed to eciLoadDict wrote past the end of the stack. Windows
   allows a path far longer than that, and a screen reader's profile folder
   is not something its user chose the length of. */
THIS long ud_findDictFile(UserDict *u, const char *name, char *out)
{
    struct stat st;

    if (name == 0 || strlen(name) >= sizeof u->path)
        return -1;
    if (!fileFindInPath(name, out))
        return -1;
    if (stat(out, &st) != 0)
        return -1;
    return (long)st.st_size;
}

/* One line, or nothing once the file has run out. A line read on the way
   to the end of file is dropped rather than used, which loses a last line
   that has no newline after it. */
THIS int32_t ud_readNextLine(UserDict *u, FILE *f, char *line)
{
    (void)u;
    if (feof(f))
        return 0;
    fgets(line, UD_LINE_ROOM, f);
    if (feof(f))
        return 0;
    return line[0] != 0;
}

/* Split one line into the word and what to say instead. Every character
   of both has to be one the stream's first field can name; the first that
   is not throws the whole line away. */
THIS int32_t ud_parseNextLine(UserDict *u, delta_state *d, char *line,
                              char *word, char *xlat)
{
    char       *wordStart = word;
    char       *xlatStart = xlat;
    char       *p         = line;
    int8_t      stream    = DL_STREAM(DT_LANG(d));
    int32_t     room;

    (void)u;

    while (*p != 0 && isspace((unsigned char)*p))
        p++;
    if (*p == 0)
        return 0;

    room = UD_LINE_ROOM;
    while (*p != 0 && *p != '\t' && room != 0) {
        char        one[2];
        const char *name;
        void       *value;

        one[0] = *p;
        one[1] = 0;
        if (!non_unique_value(d, stream, 0, one, &name, &value))
            return 0;

        *word++ = *p++;
        room--;
    }

    *word = 0;
    while (word > wordStart && isspace((unsigned char)word[-1]))
        word--;
    *word = 0;

    while (*p != 0 && (*p == '\t' || *p == ' '))
        p++;
    if (*p == 0)
        return 0;

    room = UD_LINE_ROOM;
    while (*p != 0 && room != 0) {
        if (isspace((unsigned char)*p)) {
            /* Any run of white space in the replacement becomes one
               space, one character at a time. */
            *xlat++ = ' ';
            p++;
        } else {
            char        one[2];
            const char *name;
            void       *value;

            one[0] = *p;
            one[1] = 0;
            if (!non_unique_value(d, stream, 0, one, &name, &value))
                return 0;

            *xlat++ = *p++;
        }
        room--;
    }

    xlat--;
    while (*xlat == ' ' && xlat != xlatStart)
        xlat--;
    xlat[1] = 0;

    return 1;
}

/* Put one pair in, each half copied into memory of its own and cut to
   length if it is too long. The table owns both from here on. */
THIS int32_t ud_addOneEntry(UserDict *u, const char *word, const char *xlat)
{
    char *key, *value;

    if (strlen(word) > UD_WORD_MAX)
        key = malloc(UD_WORD_MAX + 1);
    else
        key = malloc(strlen(word) + 1);
    if (key == 0)
        return UD_NO_MEMORY;

    if (strlen(word) > UD_WORD_MAX) {
        strncpy(key, word, UD_WORD_MAX);
        if (strlen(word) >= UD_WORD_MAX)
            key[UD_WORD_MAX] = 0;
    } else {
        strcpy(key, word);
    }

    if (strlen(xlat) > UD_XLAT_MAX)
        value = malloc(UD_XLAT_MAX + 1);
    else
        value = malloc(strlen(xlat) + 1);
    if (value == 0)
        return UD_NO_MEMORY;

    if (strlen(xlat) > UD_XLAT_MAX) {
        strncpy(value, xlat, UD_XLAT_MAX);
        if (strlen(xlat) >= UD_XLAT_MAX)
            value[UD_XLAT_MAX] = 0;
    } else {
        strcpy(value, xlat);
    }

    if (!hashInsertString(u->hash, key, value))
        return UD_NO_MEMORY;

    u->state = UD_CHANGED;
    return UD_OK;
}

/* Read the file in. The caller has already shown the file is there, which
   is why nothing here looks at what fopen answered. */
THIS int32_t ud_loadHashTable(UserDict *u, delta_state *d, const char *path)
{
    FILE *f = fopen(path, "r");
    char  line[0x2cc];
    char  word[0x88];
    char  xlat[0x208];

    while (ud_readNextLine(u, f, line))
        if (ud_parseNextLine(u, d, line, word, xlat))
            ud_addOneEntry(u, word, xlat);

    fclose(f);
    return 1;
}

/* An empty file counts as loaded and leaves the name unrecorded, because
   there is nothing in it to lose. */
THIS int32_t ud_loadDictionary(UserDict *u, delta_state *d, const char *name)
{
    char path[0x108];
    long size = ud_findDictFile(u, name, path);

    if (size == -1)
        return UD_NO_FILE;

    if (size != 0) {
        if (u->hash == 0
         && !ud_buildHashTable(u, (uint32_t)(size / UD_BYTES_PER_ENTRY)))
            return UD_NO_MEMORY;
        if (!ud_loadHashTable(u, d, path))
            return UD_OK;
        strcpy(u->path, name);
    }

    return UD_OK;
}

/* Write it back out in the form it was read: word, tab, replacement,
   newline. An unloaded dictionary writes an empty file rather than
   refusing. */
THIS int32_t ud_saveDictionary(UserDict *u, const char *name)
{
    FILE    *f = fopen(name, "wt");
    HashIter iter;

    if (f == 0)
        return UD_NO_FILE;

    if (u->hash == 0) {
        fclose(f);
        return UD_OK;
    }

    if (!hashIterConstruct(&iter, u->hash)) {
        fclose(f);
        return UD_OK;
    }

    do {
        const char *s = hashIterString(&iter);

        fwrite(s, 1, strlen(s), f);
        fwrite("\t", 1, 1, f);
        s = hashIterRef(&iter);
        fwrite(s, 1, strlen(s), f);
        fwrite("\n", 1, 1, f);
    } while (hashIterNext(&iter));

    fclose(f);
    return UD_OK;
}

/* Put a pair in, change one, or take one out when no replacement is
   given. */
THIS int32_t ud_update(UserDict *u, const char *word, const char *xlat)
{
    if (u->hash == 0 && !ud_buildHashTable(u, UD_DEFAULT_BUCKETS))
        return UD_NO_MEMORY;

    if (hashLookupString(u->hash, word) != 0) {
        hashDeleteString(u->hash, word, 1, 1);
        if (xlat != 0)
            return ud_addOneEntry(u, word, xlat);
        u->state = UD_CHANGED;
        return UD_OK;
    }

    if (xlat == 0)
        return UD_OK;
    return ud_addOneEntry(u, word, xlat);
}

THIS const char *ud_lookup(UserDict *u, const char *word)
{
    if (word == 0 || *word == 0)
        return 0;
    if (u->hash == 0)
        return 0;
    return hashLookupString(u->hash, word);
}

/* Walking the whole dictionary. The walk lives in the dictionary, so
   only one can be going at a time. */
THIS int32_t ud_findFirst(UserDict *u, const char **word, const char **xlat)
{
    if (u->hash == 0)
        return UD_NO_TABLE;
    if (!hashIterConstruct(&u->iter, u->hash))
        return UD_NO_TABLE;

    *word = hashIterString(&u->iter);
    *xlat = hashIterRef(&u->iter);
    return UD_OK;
}

THIS int32_t ud_findNext(UserDict *u, const char **word, const char **xlat)
{
    if (!hashIterNext(&u->iter))
        return UD_NO_TABLE;

    *word = hashIterString(&u->iter);
    *xlat = hashIterRef(&u->iter);
    return UD_OK;
}

/* Take the word between the two marks, look it up, and if it is there put
   the replacement in its place. What came out is kept for the undo. */
THIS int32_t ud_lookupAndTranslate(UserDict *u, delta_state *d,
                                   int32_t l, int32_t r)
{
    int8_t      stream = DL_STREAM(DT_LANG(d));
    const char *xlat;

    if (u->hash == 0)
        return UD_NO_MEMORY;

    if (!sync_in_stm(d, stream, l))
        return UD_NO_SPINE;
    if (!sync_in_stm(d, stream, r))
        return UD_NO_SPINE;

    if (!extract_string(d, stream, l, r, u->last, UD_LAST_ROOM))
        return UD_NO_SPINE;

    xlat = hashLookupString(u->hash, u->last);
    if (xlat == 0)
        return UD_NOT_FOUND;

    del_two_point(d, stream, l, r);
    OWNER_MOVED(d) = 1;

    if (!insert_reading(d, stream, r, xlat))
        return UD_NO_SPINE;

    return UD_OK;
}

/* Put back what the last lookup replaced. */
THIS int32_t ud_lookupUndo(UserDict *u, delta_state *d, int32_t l, int32_t r)
{
    int8_t stream = DL_STREAM(DT_LANG(d));

    if (u->last[0] == 0)
        return UD_NOT_FOUND;

    del_two_point(d, stream, l, r);
    OWNER_MOVED(d) = 1;

    if (!insert_string(d, stream, r, u->last))
        return UD_NO_SPINE;

    return UD_OK;
}


/* ---- the three of them together -------------------------------------- */

/* The three are made the same way whichever constructor is used; only what
   each one is handed differs. If any of the three could not be made the
   whole holder is marked out of memory and every method answers with that
   instead of asking a volume. */
static void ds_finish(DictionarySet *s)
{
    if (s->vol[0] != 0 && s->vol[1] != 0 && s->vol[2] != 0)
        s->status = UD_OK;
    else
        s->status = UD_NO_MEMORY;

    s->home = &DL_CURRENT(DT_LANG(s->machine));
}

THIS DictionarySet *ds_ctor(DictionarySet *s, delta_state *d)
{
    int32_t i;

    s->machine = d;
    for (i = 0; i < UD_VOLUMES; i++) {
        void *p = cpp_new(sizeof(UserDict));

        s->vol[i] = p ? ud_ctor(p) : 0;
    }
    ds_finish(s);
    return s;
}

THIS DictionarySet *ds_ctor_named(DictionarySet *s, delta_state *d,
                                  const char *a, const char *b,
                                  const char *c)
{
    const char *name[UD_VOLUMES];
    int32_t     i;

    name[0] = a;
    name[1] = b;
    name[2] = c;

    s->machine = d;
    for (i = 0; i < UD_VOLUMES; i++) {
        void *p = cpp_new(sizeof(UserDict));

        s->vol[i] = p ? ud_ctor_load(p, d, name[i]) : 0;
    }
    ds_finish(s);
    return s;
}

THIS DictionarySet *ds_ctor_sized(DictionarySet *s, delta_state *d,
                                  uint32_t a, uint32_t b, uint32_t c)
{
    uint32_t want[UD_VOLUMES];
    int32_t  i;

    want[0] = a;
    want[1] = b;
    want[2] = c;

    s->machine = d;
    for (i = 0; i < UD_VOLUMES; i++) {
        void *p = cpp_new(sizeof(UserDict));

        s->vol[i] = p ? ud_ctor_sized(p, want[i]) : 0;
    }
    ds_finish(s);
    return s;
}

/* The last line compares the current-dictionary slot with itself, so it
   always clears it, whether or not this holder was the current one. That
   is what the original does. */
THIS void ds_dtor(DictionarySet *s)
{
    int32_t i;

    for (i = 0; i < UD_VOLUMES; i++)
        if (s->vol[i] != 0) {
            ud_dtor(s->vol[i]);
            cpp_delete(s->vol[i]);
        }

    if (*s->home == DL_CURRENT(DT_LANG(s->machine)))
        DL_CURRENT(DT_LANG(s->machine)) = 0;
}

THIS int32_t ds_load(DictionarySet *s, int32_t vol, const char *name)
{
    if (s->status)
        return s->status;
    return ud_loadDictionary(s->vol[vol], s->machine, name);
}

THIS int32_t ds_save(DictionarySet *s, int32_t vol, const char *name)
{
    if (s->status)
        return s->status;
    return ud_saveDictionary(s->vol[vol], name);
}

THIS int32_t ds_updateEntry(DictionarySet *s, int32_t vol, const char *word,
                            const char *xlat)
{
    if (s->status)
        return s->status;
    return ud_update(s->vol[vol], word, xlat);
}

THIS int32_t ds_findFirst(DictionarySet *s, int32_t vol, const char **word,
                          const char **xlat)
{
    if (s->status)
        return s->status;
    return ud_findFirst(s->vol[vol], word, xlat);
}

THIS int32_t ds_findNext(DictionarySet *s, int32_t vol, const char **word,
                         const char **xlat)
{
    if (s->status)
        return s->status;
    return ud_findNext(s->vol[vol], word, xlat);
}

/* The only one that answers nothing rather than the status, because what
   it answers is a string. */
THIS const char *ds_lookup(DictionarySet *s, int32_t vol, const char *word)
{
    if (s->status)
        return 0;
    return ud_lookup(s->vol[vol], word);
}

THIS int32_t ds_lookupAndTranslate(DictionarySet *s, int32_t vol,
                                   int32_t l, int32_t r)
{
    if (s->status)
        return s->status;
    return ud_lookupAndTranslate(s->vol[vol], s->machine, l, r);
}

THIS int32_t ds_lookupUndo(DictionarySet *s, int32_t vol,
                           int32_t l, int32_t r)
{
    if (s->status)
        return s->status;
    return ud_lookupUndo(s->vol[vol], s->machine, l, r);
}

/* Which stream the dictionary rewrites in. It has to be one the language
   declared, and it has to be one written a character at a time, because
   that is the only kind a replacement can be put back into. */
int32_t setUserDictInputStream(delta_state *d, const char *name)
{
    int32_t i;

    if (name == 0 || *name == 0)
        return UD_NO_SPINE;

    for (i = 0; i < num_streams(d); i++)
        if (strcmp(stream_name((int8_t)i), name) == 0)
            break;

    if (i == num_streams(d))
        return UD_NO_SPINE;

    DL_STREAM(DT_LANG(d)) = (int8_t)i;

    if (!single_letter_stream(DL_STREAM(DT_LANG(d))))
        return UD_NO_SPINE;

    return UD_OK;
}

DictionarySet *getCurrentUserDict(delta_state *d)
{
    return DL_CURRENT(DT_LANG(d));
}

int32_t setCurrentUserDict(delta_state *d, DictionarySet *s)
{
    DL_CURRENT(DT_LANG(d)) = s;
    return 0;
}


/* ---- the dictionary a language carries -------------------------------- */

/* Ours. A language may carry a dictionary of its own in the image, written
   by tools/module/builtindict.py out of lang/<tag>/dictionary: the same three
   volumes as a caller's, each entry a line of the kind a caller's file has,
   sorted so that a lookup is a search by halves and nothing is built when an
   instance starts. It is consulted only where the caller's dictionary did
   not answer, so a word the caller has put in its own is said the caller's
   way, and eciBuiltInDict turns it off. */

static const delta_dict_entry *builtin_find(const delta_builtin_dict *b,
                                            int32_t vol, const char *word)
{
    int32_t lo = 0, hi;

    if (b == 0 || vol < 0 || vol >= UD_VOLUMES)
        return 0;
    hi = b->count[vol] - 1;
    while (lo <= hi) {
        int32_t mid = lo + (hi - lo) / 2;
        int     c   = strcmp(b->volume[vol][mid].word, word);

        if (c == 0)
            return &b->volume[vol][mid];
        if (c < 0)
            lo = mid + 1;
        else
            hi = mid - 1;
    }
    return 0;
}

/* The first word of a sentence arrives with a capital nobody wrote into its
   entry, so a main entry written in small letters is found for a word whose
   only capital is its first letter: Kubectl starting a sentence is kubectl.
   An entry written with the capital is asked for first, and a capital inside
   a word is left to the split, so Qt is still not qt nor MATE mate. A
   caller's dictionary is matched as IBM's was. */
static const delta_dict_entry *builtin_find_main(const delta_builtin_dict *b,
                                                 const char *word)
{
    const delta_dict_entry *e = builtin_find(b, UD_MAIN, word);
    char                    low[UD_LAST_ROOM];
    size_t                  n;

    if (e != 0 || word[0] < 'A' || word[0] > 'Z')
        return e;
    for (n = 1; word[n] != 0; n++)
        if ((word[n] >= 'A' && word[n] <= 'Z') || n + 1 >= sizeof low)
            return 0;
    memcpy(low, word, n + 1);
    low[0] = (char)(low[0] - 'A' + 'a');
    return builtin_find(b, UD_MAIN, low);
}

/* Whether every character is one the stream's first field can name, which
   is what a line of a caller's file has to pass to be kept at all. Asked of
   an entry when it is found rather than when the image was built, since the
   names belong to the running machine. */
static int builtin_sayable(delta_state *d, int8_t stream, const char *s)
{
    for (; *s; s++) {
        char        one[2];
        const char *name;
        void       *value;

        if (*s == ' ')
            continue;
        one[0] = *s;
        one[1] = 0;
        if (!non_unique_value(d, stream, 0, one, &name, &value))
            return 0;
    }
    return 1;
}

static int32_t builtin_lookupAndTranslate(delta_state *d, int32_t vol,
                                          int32_t l, int32_t r)
{
    DeltaLang                *lang = DT_LANG(d);
    const delta_language     *of   = delta_lang_of(d);
    int8_t                    stream = DL_STREAM(lang);
    const delta_dict_entry   *e;

    if (lang->builtin_off || of == 0 || of->builtin_dict == 0
        || vol < 0 || vol >= UD_VOLUMES || of->builtin_dict->count[vol] == 0)
        return UD_NOT_FOUND;

    if (!sync_in_stm(d, stream, l) || !sync_in_stm(d, stream, r))
        return UD_NO_SPINE;
    if (!extract_string(d, stream, l, r, lang->builtin_last[vol],
                        UD_LAST_ROOM))
        return UD_NO_SPINE;

    e = vol == UD_MAIN
        ? builtin_find_main(of->builtin_dict, lang->builtin_last[vol])
        : builtin_find(of->builtin_dict, vol, lang->builtin_last[vol]);
    if (e == 0 || !builtin_sayable(d, stream, e->says))
        return UD_NOT_FOUND;

    del_two_point(d, stream, l, r);
    OWNER_MOVED(d) = 1;
    if (!insert_reading(d, stream, r, e->says))
        return UD_NO_SPINE;

    lang->builtin_took[vol] = 1;
    return UD_OK;
}

/* Ours as well, and for a caller's dictionary too. A main entry is a whole
   token, and where the whole of a possessive is not held the rules ask for
   it again without the 's. An entry in words then takes the ending back as
   any word does, but one in phones is shut in its brackets, and the ending
   was left standing on its own and spelled out: Frey's as fry ess. So where
   a main volume holds the word without its ending and the entry ends in
   phones, the possessive is read here as the entry with the ending's own
   sound inside its last brackets -- z, s after a voiceless sound, and a
   syllable of its own after a hissing one, the way the rules write boss's.
   The caller's entry for the word is the one asked first, as it would be in
   the lookup the rules make next, and one ending in words is left to
   them. */

#define UD_SAYS_ROOM 0x220

static const char *main_says(delta_state *d, const char *word)
{
    DeltaLang            *lang = DT_LANG(d);
    const delta_language *of   = delta_lang_of(d);
    DictionarySet        *s    = DL_CURRENT(lang);
    const char           *says;

    if (s != 0 && s->status == 0 && s->vol[UD_MAIN] != 0
        && (says = ud_lookup(s->vol[UD_MAIN], word)) != 0)
        return says;
    if (!lang->builtin_off && of != 0 && of->builtin_dict != 0) {
        const delta_dict_entry *e = builtin_find_main(of->builtin_dict,
                                                      word);

        if (e != 0)
            return e->says;
    }
    return 0;
}

static int32_t main_possessive(delta_state *d, int32_t l, int32_t r)
{
    DeltaLang            *lang   = DT_LANG(d);
    const delta_language *of     = delta_lang_of(d);
    int8_t                stream = DL_STREAM(lang);
    char                 *w      = lang->builtin_last[UD_MAIN];
    char                  stem[UD_LAST_ROOM];
    char                  out[UD_SAYS_ROOM];
    const char           *says, *p, *q;
    size_t                n, k;
    char                  last;

    /* The ending's phones are English's; the same letters are other sounds
       in another language's alphabet. */
    if (of == 0 || of->tag == 0 || strncmp(of->tag, "en", 2) != 0)
        return UD_NOT_FOUND;
    if (!sync_in_stm(d, stream, l) || !sync_in_stm(d, stream, r))
        return UD_NO_SPINE;
    if (!extract_string(d, stream, l, r, w, UD_LAST_ROOM))
        return UD_NO_SPINE;

    n = strlen(w);
    if (n < 3 || w[n - 2] != '\'' || w[n - 1] != 's')
        return UD_NOT_FOUND;
    memcpy(stem, w, n - 2);
    stem[n - 2] = 0;

    says = main_says(d, stem);
    if (says == 0)
        return UD_NOT_FOUND;
    k = strlen(says);
    for (p = 0, q = says; (q = strstr(q, "`[")) != 0; q++)
        p = q;
    if (p == 0 || k + 8 > sizeof out || says[k - 1] != ']'
        || says + k - 1 - (p + 2) < 1
        || strpbrk(p + 2, "`[") != 0 || strchr(p, ']') != says + k - 1)
        return UD_NOT_FOUND;

    k--;
    last = says[k - 1];
    memcpy(out, says, k);
    if (strchr("szSZCJ", last) != 0) {
        k--;
        memcpy(out + k, ".0", 2);
        out[k + 2] = last;
        memcpy(out + k + 3, "Xz", 2);
        k += 5;
    } else
        out[k++] = strchr("ptkfT", last) != 0 ? 's' : 'z';
    out[k++] = ']';
    out[k] = 0;
    if (!builtin_sayable(d, stream, out))
        return UD_NOT_FOUND;

    del_two_point(d, stream, l, r);
    OWNER_MOVED(d) = 1;
    if (!insert_reading(d, stream, r, out))
        return UD_NO_SPINE;

    lang->builtin_took[UD_MAIN] = 1;
    return UD_OK;
}

/* Ours as well. Where the language asks for it, a word in the main volume's
   place that neither dictionary holds and that has a capital inside it is
   read as the words it is made of -- iPhone as i Phone, NixOS as Nix OS,
   HTMLElement as HTML Element -- by putting a space before each capital that
   starts one, the same way an entry saying so would. A capital starts a word
   after a small letter, and the last of a run of capitals does when small
   letters follow it, unless all that follows is a plural s, so that APIs and
   IDs are left as they are. Three kinds of word are left whole: one
   beginning with a beginning the language lists and a capital, which is a
   name -- McDonald, LeBron, DiCaprio -- one a root volume holds in small
   letters, as it is or without a plural or possessive ending, which the
   root lookup will find later and say as the entry says, and a possessive
   whose word a main volume holds. */

/* Only the letters of ASCII count. Text in UTF-8 handed over as the Western
   set arrives with each accented letter as a capital A with a mark and a sign
   after it, so counting the Western set's capitals cut cafe with its accent
   in two. */
static int split_upper(unsigned char c)
{
    return c >= 'A' && c <= 'Z';
}

static int split_lower(unsigned char c)
{
    return c >= 'a' && c <= 'z';
}

static int split_starts(const char *w, size_t i)
{
    size_t m;

    if (!split_upper((unsigned char)w[i]))
        return 0;
    if (split_lower((unsigned char)w[i - 1]))
        return 1;
    if (!split_upper((unsigned char)w[i - 1])
        || !split_lower((unsigned char)w[i + 1]))
        return 0;
    for (m = 0; split_lower((unsigned char)w[i + 1 + m]); m++)
        ;
    return !(m == 1 && w[i + 1] == 's'
             && (w[i + 2] == 0 || w[i + 2] == '\''));
}

static int split_rooted(delta_state *d, const delta_builtin_dict *b,
                        const char *low)
{
    DictionarySet *s = DL_CURRENT(DT_LANG(d));

    if (s != 0 && s->status == 0 && s->vol[UD_ROOT] != 0
        && ud_lookup(s->vol[UD_ROOT], low) != 0)
        return 1;
    return builtin_find(b, UD_ROOT, low) != 0;
}

static int split_held(delta_state *d, const delta_builtin_dict *b,
                      const char *w)
{
    char   low[UD_LAST_ROOM];
    size_t n;

    /* The rules ask the main volume for a possessive's word next, so TeX's
       is TeX's entry and PostgreSQL's PostgreSQL's. */
    n = strlen(w);
    if (n > 2 && n < sizeof low && w[n - 2] == '\'' && w[n - 1] == 's') {
        memcpy(low, w, n - 2);
        low[n - 2] = 0;
        if (main_says(d, low) != 0)
            return 1;
    }

    for (n = 0; w[n] && n + 1 < sizeof low; n++)
        low[n] = (char)(split_upper((unsigned char)w[n]) ? w[n] + 0x20 : w[n]);
    low[n] = 0;
    if (split_rooted(d, b, low))
        return 1;
    if (n > 2 && low[n - 2] == '\'' && low[n - 1] == 's') {
        low[n - 2] = 0;
        return split_rooted(d, b, low);
    }
    if (n > 1 && low[n - 1] == 's') {
        low[n - 1] = 0;
        return split_rooted(d, b, low);
    }
    return 0;
}

static int32_t builtin_split(delta_state *d, int32_t l, int32_t r)
{
    DeltaLang                *lang = DT_LANG(d);
    const delta_language     *of   = delta_lang_of(d);
    const delta_builtin_dict *b    = of ? of->builtin_dict : 0;
    int8_t                    stream = DL_STREAM(lang);
    char                     *w = lang->builtin_last[UD_MAIN];
    char                      out[UD_LAST_ROOM * 2];
    const char *const        *p;
    size_t                    i, k, n;
    int                       cuts = 0;

    if (lang->builtin_off || b == 0 || !b->split)
        return UD_NOT_FOUND;
    if (!sync_in_stm(d, stream, l) || !sync_in_stm(d, stream, r))
        return UD_NO_SPINE;
    if (!extract_string(d, stream, l, r, w, UD_LAST_ROOM))
        return UD_NO_SPINE;

    for (p = b->whole; p != 0 && *p != 0; p++) {
        n = strlen(*p);
        if (strncmp(w, *p, n) == 0 && split_upper((unsigned char)w[n]))
            return UD_NOT_FOUND;
    }

    /* An abbreviation that ends in a hissing letter takes es or s for its
       plural, SMSes and OSs, and the cut below took the last capital and
       the es for a word, S M sess, or the rules left the s unsaid. Its
       letters are written out as an entry spells them, the last with the
       possessive the rules read as the syllable the plural adds. One a
       main volume holds is said as a word or as its entry has it, BIOS and
       DOS among them, and is left to the cut as before. */
    n = strlen(w);
    for (k = 0; k < n && split_upper((unsigned char)w[k]); k++)
        ;
    if (k >= 2 && k * 4 + 3 <= sizeof out && strchr("SXZ", w[k - 1]) != 0
        && (strcmp(w + k, "es") == 0 || strcmp(w + k, "s") == 0)) {
        char run[UD_LAST_ROOM];
        size_t m, o = 0;

        memcpy(run, w, k);
        run[k] = 0;
        if (main_says(d, run) == 0) {
            for (m = 0; m < k; m++) {
                const char *say = w[m] == 'A' ? "ay" : w[m] == 'I' ? "igh" : 0;

                if (m > 0)
                    out[o++] = ' ';
                if (say != 0) {
                    memcpy(out + o, say, strlen(say));
                    o += strlen(say);
                } else
                    out[o++] = w[m];
            }
            memcpy(out + o, "'s", 3);
            goto cut;
        }
    }

    for (i = k = 0; i < n && k + 2 < sizeof out; i++) {
        if (i > 0 && split_starts(w, i)) {
            out[k++] = ' ';
            cuts++;
        }
        out[k++] = w[i];
    }
    out[k] = 0;
    if (cuts == 0 || i < n || split_held(d, b, w))
        return UD_NOT_FOUND;

cut:
    del_two_point(d, stream, l, r);
    OWNER_MOVED(d) = 1;
    if (!insert_reading(d, stream, r, out))
        return UD_NO_SPINE;

    lang->builtin_took[UD_MAIN] = 1;
    return UD_OK;
}

/* Ours as well. A word in small letters that no dictionary holds goes to the
   letter rules, which read anything as a word, so usrdct, cgroup and
   tmpfiles came out as single invented words. Where the language carries
   what tells one that can be said from one that cannot -- the consonants an
   English word may begin and end with, which tools/module/sayable.py takes
   from word lists -- such a word is first asked whether it could be said:
   whether it begins and ends as a word may, and whether each run of
   consonants between its vowels divides into an ending and a beginning. One
   that could is left to the rules, however it is spelled, so qemu, env and
   sudo are read as they were. One that could not is cut into known words,
   chunks that could be said and letters said by name, whichever costs least,
   the way eSpeak NG reads a word it cannot pronounce: cgroup as C group,
   tmpfiles as T M P files, usrdct as U S R D C T. Left whole whatever they
   look like: a word the root volumes hold, a word the language keeps as a
   word because it fails the test and is one (algorithm, Edinburgh,
   couldnt), a word it may be cut into, a Roman numeral, and a name
   beginning Mc. */

#define SAY_MAX 32

static int ws_cmp(const char *key, size_t n, const char *word)
{
    size_t i;

    for (i = 0; i < n; i++) {
        unsigned char a = (unsigned char)key[i];
        unsigned char b = (unsigned char)word[i];

        if (b == 0)
            return 1;
        if (a != b)
            return a < b ? -1 : 1;
    }
    return word[n] == 0 ? 0 : -1;
}

static int ws_has(const delta_wordset *s, const char *key, size_t n)
{
    int32_t     lo = 0, hi, i;
    const char *p;

    if (s->words == 0 || s->count == 0)
        return 0;
    hi = (s->count + 15) / 16 - 1;
    while (lo < hi) {
        int32_t mid = (lo + hi + 1) / 2;

        if (ws_cmp(key, n, s->words + s->index[mid]) >= 0)
            lo = mid;
        else
            hi = mid - 1;
    }
    p = s->words + s->index[lo];
    for (i = lo * 16; i < s->count && i < lo * 16 + 16; i++) {
        int c = ws_cmp(key, n, p);

        if (c == 0)
            return 1;
        if (c < 0)
            return 0;
        p += strlen(p) + 1;
    }
    return 0;
}

static int say_plain_vowel(char c)
{
    return c == 'a' || c == 'e' || c == 'i' || c == 'o' || c == 'u';
}

/* y is a vowel after a consonant and a consonant anywhere else. */
static int say_vowel(const char *w, size_t i)
{
    return say_plain_vowel(w[i])
        || (w[i] == 'y' && i > 0 && !say_plain_vowel(w[i - 1]));
}

static int say_part(const delta_wordset *s, const char *w, size_t n)
{
    return n == 0 || ws_has(s, w, n);
}

/* A doubled letter is one sound and a q alone after the vowel is said as
   k, so comm ends as com does and freq as wreck does; postgresql's sql is
   still no ending. */
static int say_coda(const delta_builtin_dict *b, const char *w, size_t n)
{
    char   c[SAY_MAX];
    size_t i, m = 0;
    int    moved = 0;

    if (say_part(&b->coda, w, n))
        return 1;
    for (i = 0; i < n; i++) {
        char x = w[i] == 'q' && n == 1 ? 'k' : w[i];

        if (m > 0 && c[m - 1] == x)
            moved = 1;
        else
            c[m++] = x;
        moved |= x != w[i];
    }
    return moved && say_part(&b->coda, c, m);
}

static int say_able(const delta_builtin_dict *b, const char *w, size_t n)
{
    size_t i = 0, j, k;

    while (i < n && !say_vowel(w, i))
        i++;
    if (i == n || !say_part(&b->onset, w, i))
        return 0;
    k = n;
    while (!say_vowel(w, k - 1))
        k--;
    if (!say_coda(b, w + k, n - k))
        return 0;
    while (i < k) {
        size_t m, split_ok = 0;

        while (i < k && say_vowel(w, i))
            i++;
        if (i == k)
            break;
        j = i;
        while (!say_vowel(w, j))
            j++;
        for (m = 0; m <= j - i && !split_ok; m++)
            split_ok = say_coda(b, w + i, m)
                       && say_part(&b->onset, w + i + m, j - i - m);
        if (!split_ok)
            return 0;
        i = j;
    }
    return 1;
}

/* Whether the stem is a piece, as written, with a doubled last letter
   undone (runn), or with the e an ending took off (bounc). */
static int say_stem(const delta_builtin_dict *b, const char *w, size_t n)
{
    char e[SAY_MAX + 1];

    if (n < 2 || n >= SAY_MAX)
        return 0;
    if (ws_has(&b->pieces, w, n))
        return 1;
    if (n > 3 && w[n - 1] == w[n - 2] && !say_plain_vowel(w[n - 1])
        && ws_has(&b->pieces, w, n - 1))
        return 1;
    memcpy(e, w, n);
    e[n] = 'e';
    return ws_has(&b->pieces, e, n + 1);
}

/* A piece, or a piece with an ending English adds to words: tmpfiles is
   T M P files, not file S, and gstreamer G streamer. */
static int say_word(const delta_builtin_dict *b, const char *w, size_t n)
{
    static const char *const end[] = { "ings", "ing", "ers", "er", "ed",
                                       "es", "s", 0 };
    size_t i;

    if (ws_has(&b->pieces, w, n))
        return 1;
    for (i = 0; end[i] != 0; i++) {
        size_t k = strlen(end[i]);

        if (n < k + 3 || memcmp(w + n - k, end[i], k) != 0)
            continue;
        if (end[i][0] == 's' && w[n - 2] == 's')
            continue;
        if (strcmp(end[i], "es") == 0
            && !strchr("sxzh", w[n - 3]))
            continue;
        if (say_stem(b, w, n - k))
            return 1;
    }
    return 0;
}

/* Whether the language would leave the word to the rules. */
static int say_leave(delta_state *d, const delta_builtin_dict *b,
                     const char *w, size_t n)
{
    char   low[SAY_MAX + 1];
    size_t i;
    int    vowel = 0, roman = 1;

    if (n < 2 || n > SAY_MAX)
        return 1;
    for (i = 0; i < n; i++) {
        if (w[i] < 'a' || w[i] > 'z')
            return 1;
        vowel |= say_vowel(w, i);
        roman &= strchr("ivxlc", w[i]) != 0;
    }
    if (!vowel || roman || (w[0] == 'm' && w[1] == 'c'))
        return 1;
    if (ws_has(&b->kept, w, n) || ws_has(&b->pieces, w, n))
        return 1;
    memcpy(low, w, n);
    low[n] = 0;
    if (split_rooted(d, b, low))
        return 1;
    if (n > 2 && w[n - 1] == 's') {
        low[n - 1] = 0;
        if (split_rooted(d, b, low))
            return 1;
    }
    return say_able(b, w, n);
}

/* The cheapest cut of the word, with the end of each piece written into
   ends. A word costs more than a letter and less than two, so
   cvsexportcommit is C V S export commit rather than anything with sex in
   it, and fdopen F D open rather than F do pen. A chunk no list holds costs
   more than four letters, so that getpwuid is get P W U I D rather than get
   P wuid; in a build without the pieces a chunk is all there is to say a
   part with, and costs what a word does. And each run of letters costs a
   little more, enough to settle a
   tie and nothing else, since an abbreviation's letters belong together:
   gnutls is gnu T L S, not G nut L S, and dlopen D L open, not D lope N.
   So the cheapest cut is kept twice from each place on, one that begins
   with a letter and one that does not, as what a letter costs depends on
   what follows it. */
#define SAY_LETTER 30
#define SAY_WORD   39
#define SAY_CHUNK  132
#define SAY_NONE   0x3fffffff

static size_t say_cut(const delta_builtin_dict *b, const char *w, size_t n,
                      size_t *ends)
{
    int32_t letter[SAY_MAX + 1], other[SAY_MAX + 1];
    size_t  to[SAY_MAX + 1];
    size_t  i, j, pieces = 0;
    int     by_letter;

    letter[n] = SAY_NONE;
    other[n]  = 0;
    for (i = n; i-- > 0;) {
        int32_t next = other[i + 1] + 1;

        letter[i] = SAY_LETTER + (letter[i + 1] < next ? letter[i + 1] : next);
        other[i]  = SAY_NONE;
        for (j = i + 2; j <= n; j++) {
            int32_t c, rest = letter[j] < other[j] ? letter[j] : other[j];

            if (say_word(b, w + i, j - i))
                c = SAY_WORD;
            else if (j - i >= 3 && say_able(b, w + i, j - i))
                c = b->pieces.count ? SAY_CHUNK : SAY_WORD;
            else
                continue;
            if (rest + c < other[i]) {
                other[i] = rest + c;
                to[i]    = j;
            }
        }
    }
    by_letter = letter[0] <= other[0];
    for (i = 0; i < n; i = ends[i], pieces++) {
        if (by_letter) {
            ends[i]   = i + 1;
            by_letter = letter[i + 1] < other[i + 1] + 1;
        } else {
            ends[i]   = to[i];
            by_letter = letter[to[i]] < other[to[i]];
        }
    }
    return pieces;
}

static size_t say_write(const char *w, size_t n, const size_t *ends,
                        char *out, size_t room)
{
    size_t i, o = 0;

    for (i = 0; i < n; i = ends[i]) {
        size_t len = ends[i] - i;
        const char *say = w + i;

        if (o + len + 5 >= room)
            return 0;
        if (o > 0)
            out[o++] = ' ';
        if (len == 1 && w[i] == 'a')
            say = "ay", len = 2;
        else if (len == 1 && w[i] == 'i')
            say = "igh", len = 3;
        memcpy(out + o, say, len);
        if (len == 1)
            out[o] = (char)(w[i] - 'a' + 'A');
        o += len;
    }
    out[o] = 0;
    return o;
}

/* Whether the s after a stem is a plural: cpus is C P U's and vcpus V C P
   U's, but gnutls is gnu T L S and procfs proc F S. The letters the stem's
   cut ends in have to end in an abbreviation the main volume holds, since
   nothing else tells the plural of CPU from the S of TLS. */
static int say_plural(delta_state *d, const char *w, size_t n,
                      const size_t *ends)
{
    size_t run = n, i, k;
    char   low[SAY_MAX + 1], up[SAY_MAX + 1];

    for (i = 0; i < n; i = ends[i])
        if (ends[i] - i != 1)
            run = n;
        else if (run == n)
            run = i;
    if (run == n || n - run < 2)
        return 0;
    for (k = run; k + 2 <= n; k++) {
        for (i = k; i < n; i++) {
            low[i - k] = w[i];
            up[i - k]  = (char)(w[i] - 'a' + 'A');
        }
        low[n - k] = up[n - k] = 0;
        if (main_says(d, low) != 0 || main_says(d, up) != 0)
            return 1;
    }
    return 0;
}

static int32_t builtin_unsayable(delta_state *d, int32_t l, int32_t r)
{
    DeltaLang                *lang = DT_LANG(d);
    const delta_language     *of   = delta_lang_of(d);
    const delta_builtin_dict *b    = of ? of->builtin_dict : 0;
    int8_t                    stream = DL_STREAM(lang);
    char                     *w = lang->builtin_last[UD_MAIN];
    char                      out[UD_LAST_ROOM * 4];
    size_t                    ends[SAY_MAX + 1];
    size_t                    n, o, last;

    if (lang->builtin_off || b == 0 || b->onset.count == 0)
        return UD_NOT_FOUND;
    if (!sync_in_stm(d, stream, l) || !sync_in_stm(d, stream, r))
        return UD_NO_SPINE;
    if (!extract_string(d, stream, l, r, w, UD_LAST_ROOM))
        return UD_NO_SPINE;
    n = strlen(w);
    if (say_leave(d, b, w, n))
        return UD_NOT_FOUND;

    say_cut(b, w, n, ends);
    for (last = 0; ends[last] < n; last = ends[last])
        ;
    if (n > 3 && w[n - 1] == 's' && w[n - 2] != 's'
        && !say_word(b, w + last, n - last) && !say_leave(d, b, w, n - 1)) {
        size_t stem[SAY_MAX + 1];

        say_cut(b, w, n - 1, stem);
        if (say_plural(d, w, n - 1, stem)) {
            o = say_write(w, n - 1, stem, out, sizeof out - 2);
            if (o == 0)
                return UD_NOT_FOUND;
            memcpy(out + o, "'s", 3);
            goto cut;
        }
    }
    if (say_write(w, n, ends, out, sizeof out) == 0)
        return UD_NOT_FOUND;

cut:
    del_two_point(d, stream, l, r);
    OWNER_MOVED(d) = 1;
    if (!insert_reading(d, stream, r, out))
        return UD_NO_SPINE;

    lang->builtin_took[UD_MAIN] = 1;
    return UD_OK;
}

void builtin_dict_enable(delta_state *d, int32_t on)
{
    if (d != 0 && d->dlang != 0)
        DT_LANG(d)->builtin_off = !on;
}


/* ---- what the rules call --------------------------------------------- */

/* Both of these answer one when they did nothing, which is what the rules
   read as "carry on as you were". The caller's dictionary is asked first;
   where it does not hold the word, has nothing in that volume -- which a
   volume never loaded answers as no memory -- or is not there at all, the
   language's own is asked. */
int32_t callUserDictLookup(delta_state *d, const void *volume,
                           const delta_token *l, const delta_token *r)
{
    DictionarySet *s   = DL_CURRENT(DT_LANG(d));
    int32_t        vol = *(const int16_t *)((const char *)volume + 2);
    int32_t        rc  = UD_NOT_FOUND;

    if (vol >= 0 && vol < UD_VOLUMES)
        DT_LANG(d)->builtin_took[vol] = 0;

    if (s != 0) {
        rc = ds_lookupAndTranslate(s, vol, l->value, r->value);
        if (rc != UD_NOT_FOUND && rc != UD_NO_MEMORY)
            return rc != 0;
    }

    rc = builtin_lookupAndTranslate(d, vol, l->value, r->value);
    if (rc == UD_NOT_FOUND && vol == UD_MAIN)
        rc = main_possessive(d, l->value, r->value);
    if (rc == UD_NOT_FOUND && vol == UD_MAIN)
        rc = builtin_split(d, l->value, r->value);
    if (rc == UD_NOT_FOUND && vol == UD_MAIN)
        rc = builtin_unsayable(d, l->value, r->value);
    return rc != 0;
}

int32_t callInsertLastDictString(delta_state *d, const void *volume,
                                 const delta_token *l, const delta_token *r)
{
    DictionarySet *s   = DL_CURRENT(DT_LANG(d));
    int32_t        vol = *(const int16_t *)((const char *)volume + 2);

    if (vol >= 0 && vol < UD_VOLUMES && DT_LANG(d)->builtin_took[vol]) {
        DeltaLang *lang = DT_LANG(d);
        int8_t     stream = DL_STREAM(lang);

        del_two_point(d, stream, l->value, r->value);
        OWNER_MOVED(d) = 1;
        return !insert_string(d, stream, r->value, lang->builtin_last[vol]);
    }

    if (s == 0)
        return 1;

    return ds_lookupUndo(s, vol, l->value, r->value) != 0;
}

ALIAS("??0UserDict@@QAE@XZ", "ud_ctor");
ALIAS("??0UserDict@@QAE@K@Z", "ud_ctor_sized");
ALIAS("??0UserDict@@QAE@PAUDelta_This_Struct@@PBD@Z", "ud_ctor_load");
ALIAS("??1UserDict@@QAE@XZ", "ud_dtor");
ALIAS("?buildHashTable@UserDict@@AAEHK@Z", "ud_buildHashTable");
ALIAS("?findDictFile@UserDict@@AAEJPBDPAD@Z", "ud_findDictFile");
ALIAS("?readNextLine@UserDict@@AAEHPAU_iobuf@@PAD@Z", "ud_readNextLine");
ALIAS("?parseNextLine@UserDict@@AAEHPAUDelta_This_Struct@@PAD11@Z",
      "ud_parseNextLine");
ALIAS("?addOneEntry@UserDict@@AAEHPBD0@Z", "ud_addOneEntry");
ALIAS("?loadHashTable@UserDict@@AAEHPAUDelta_This_Struct@@PBD@Z",
      "ud_loadHashTable");
ALIAS("?loadDictionary@UserDict@@QAEHPAUDelta_This_Struct@@PBD@Z",
      "ud_loadDictionary");
ALIAS("?saveDictionary@UserDict@@QAEHPBD@Z", "ud_saveDictionary");
ALIAS("?update@UserDict@@QAEHPBD0@Z", "ud_update");
ALIAS("?lookup@UserDict@@QAEPBDPBD@Z", "ud_lookup");
ALIAS("?findFirst@UserDict@@QAEHAAPBD0@Z", "ud_findFirst");
ALIAS("?findNext@UserDict@@QAEHAAPBD0@Z", "ud_findNext");
ALIAS("?lookupAndTranslate@UserDict@@QAEHPAUDelta_This_Struct@@USyncMark@@1@Z",
      "ud_lookupAndTranslate");
ALIAS("?lookupUndo@UserDict@@QAEHPAUDelta_This_Struct@@USyncMark@@1@Z",
      "ud_lookupUndo");

ALIAS("??0DictionarySet@@QAE@PAUDelta_This_Struct@@@Z", "ds_ctor");
ALIAS("??0DictionarySet@@QAE@PAUDelta_This_Struct@@PBD11@Z", "ds_ctor_named");
ALIAS("??0DictionarySet@@QAE@PAUDelta_This_Struct@@III@Z", "ds_ctor_sized");
ALIAS("??1DictionarySet@@QAE@XZ", "ds_dtor");
ALIAS("?load@DictionarySet@@QAEHW4DictVolume@@PBD@Z", "ds_load");
ALIAS("?save@DictionarySet@@QAEHW4DictVolume@@PBD@Z", "ds_save");
ALIAS("?updateEntry@DictionarySet@@QAEHW4DictVolume@@PBD1@Z",
      "ds_updateEntry");
ALIAS("?findFirst@DictionarySet@@QAEHW4DictVolume@@AAPBD1@Z", "ds_findFirst");
ALIAS("?findNext@DictionarySet@@QAEHW4DictVolume@@AAPBD1@Z", "ds_findNext");
ALIAS("?lookup@DictionarySet@@QAEPBDW4DictVolume@@PBD@Z", "ds_lookup");
ALIAS("?lookupAndTranslate@DictionarySet@@QAEHW4DictVolume@@USyncMark@@1@Z",
      "ds_lookupAndTranslate");
ALIAS("?lookupUndo@DictionarySet@@QAEHW4DictVolume@@USyncMark@@1@Z",
      "ds_lookupUndo");
ALIAS("?getCurrentUserDict@@YAPAVDictionarySet@@PAUDelta_This_Struct@@@Z",
      "getCurrentUserDict");
ALIAS("?setCurrentUserDict@@YAHPAUDelta_This_Struct@@PAVDictionarySet@@@Z",
      "setCurrentUserDict");
