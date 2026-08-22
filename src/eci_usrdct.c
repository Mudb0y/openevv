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

typedef struct UserDict {
    char     path[0x108];   /* +0x000, what it was told to load */
    void    *hash;          /* +0x108 */
    int32_t  state;         /* +0x10c */
    char     iter[0xc];     /* +0x110, one walk at a time, so it lives here */
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


/* ---- one dictionary ---- */

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

/* Where the file is and how big it is, or minus one if it is not there. */
THIS long ud_findDictFile(UserDict *u, const char *name, char *out)
{
    struct stat st;

    (void)u;
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
    FILE *f = fopen(name, "wt");
    char  iter[0xc];

    if (f == 0)
        return UD_NO_FILE;

    if (u->hash == 0) {
        fclose(f);
        return UD_OK;
    }

    if (!hashIterConstruct(iter, u->hash)) {
        fclose(f);
        return UD_OK;
    }

    do {
        const char *s = hashIterString(iter);

        fwrite(s, 1, strlen(s), f);
        fwrite("\t", 1, 1, f);
        s = hashIterRef(iter);
        fwrite(s, 1, strlen(s), f);
        fwrite("\n", 1, 1, f);
    } while (hashIterNext(iter));

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
    if (!hashIterConstruct(u->iter, u->hash))
        return UD_NO_TABLE;

    *word = hashIterString(u->iter);
    *xlat = hashIterRef(u->iter);
    return UD_OK;
}

THIS int32_t ud_findNext(UserDict *u, const char **word, const char **xlat)
{
    if (!hashIterNext(u->iter))
        return UD_NO_TABLE;

    *word = hashIterString(u->iter);
    *xlat = hashIterRef(u->iter);
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

    if (!insert_string(d, stream, r, xlat))
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


/* ---- the three of them together ---- */

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


/* ---- what the rules call ---- */

/* Both of these answer one when they did nothing, which is what the rules
   read as "carry on as you were". */
int32_t callUserDictLookup(delta_state *d, const void *volume,
                           const delta_token *l, const delta_token *r)
{
    DictionarySet *s = DL_CURRENT(DT_LANG(d));

    if (s == 0)
        return 1;

    return ds_lookupAndTranslate(s, *(const int16_t *)((const char *)volume + 2),
                                 l->value, r->value) != 0;
}

int32_t callInsertLastDictString(delta_state *d, const void *volume,
                                 const delta_token *l, const delta_token *r)
{
    DictionarySet *s = DL_CURRENT(DT_LANG(d));

    if (s == 0)
        return 1;

    return ds_lookupUndo(s, *(const int16_t *)((const char *)volume + 2),
                         l->value, r->value) != 0;
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
