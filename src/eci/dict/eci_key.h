/* The five classes the stored dictionary is built out of.
 *
 * A Key is bytes and a length, a Translation is what those bytes mean, a
 * ListNode is one of each, a SkipListNode is a ListNode with forward pointers
 * on it, and a SkipList is the store. IBM keeps them in five objects --
 * win_key, win_translation, win_listnode, win_skiplistnode and win_skipstore
 * -- and this is what they share.
 *
 * Nothing in the engine reads any of these by offset, so the layouts here are
 * named fields rather than IBM's. What has to agree with IBM's is what goes
 * into a saved file, and `save' and `load' are where that is decided.
 */

#ifndef ECI_KEY_H
#define ECI_KEY_H

#include <stdint.h>
#include <stdio.h>

/* ---- Key ------------------------------------------------------------- */

typedef struct Key {
    char   *bytes;    /* +0x04 in the original, after its vtable */
    int32_t len;      /* +0x08 */
} Key;

Key    *key_ctor(Key *k, char *bytes, int32_t len);
void    key_dtor(Key *k);
void    key_set(Key *k, char *bytes, int32_t len);
void    key_assign(Key *k, const Key *from);
int32_t key_lessthan(Key *k, Key *other, int32_t len);
int32_t key_match(Key *k, Key *other, int32_t len);
void    key_save(Key *k, FILE *f);
int32_t key_load(Key *k, FILE *f);
void    key_dump(Key *k);

/* ---- Translation ----------------------------------------------------- */

/* Three runs of bytes and a part of speech: what the key is read as, the word
   it stands for, and a third string the dictionary API carries. `ok' is set
   only when all three were copied. */
typedef struct Translation {
    char   *value;      /* +0x00 */
    int32_t valueLen;   /* +0x04 */
    char   *word;       /* +0x08 */
    int32_t wordLen;    /* +0x0c */
    char   *extra;      /* +0x10 */
    int32_t extraLen;   /* +0x14 */
    int32_t pos;        /* +0x18, an ECIPartOfSpeech */
    int32_t ok;         /* +0x1c */
} Translation;

Translation *tr_ctorEmpty(Translation *t);
Translation *tr_ctor(Translation *t, const char *value, int32_t valueLen,
                     const char *word, const char *extra, int32_t pos);
void         tr_dtor(Translation *t);
char        *tr_set(Translation *t, const char *from, int32_t len);
void         tr_assign(Translation *t, const Translation *from);
void         tr_save(Translation *t, FILE *f);
int32_t      tr_load(Translation *t, FILE *f);
void         tr_dump(Translation *t);

/* ---- ListNode -------------------------------------------------------- */

/* One entry: a key and what it means. Both are copied in shallow -- the
   pointers move across and the node owns them afterwards -- which is the
   original's arrangement and is why nothing frees what it handed over. */
typedef struct ListNode {
    Key         key;    /* +0x04 */
    Translation trans;  /* +0x10 */
} ListNode;

ListNode *ln_ctor(ListNode *n, Key *k, Translation *t);
ListNode *ln_ctorEmpty(ListNode *n);
void      ln_save(ListNode *n, FILE *f);
int32_t   ln_load(ListNode *n, FILE *f);
void      ln_dump(ListNode *n);

/* ---- SkipListNode ---------------------------------------------------- */

/* A ListNode with a forward pointer for each level it reaches. The count is
   one more than the level asked for, so a node made at level nought still has
   one pointer. */
typedef struct SkipListNode {
    ListNode              base;     /* +0x00 */
    int32_t               levels;   /* +0x30 */
    struct SkipListNode **forward;  /* +0x34 */
} SkipListNode;

SkipListNode *sln_ctor(SkipListNode *n, int32_t level);
SkipListNode *sln_ctorEmpty(SkipListNode *n);
void          sln_dtor(SkipListNode *n);

/* ---- ArrayListNode --------------------------------------------------- */

/* The same node with indices where the forward pointers were, which is what
   goes into a file: a pointer is one process's own and an index is not. It
   has the same shape as a SkipListNode on purpose -- the save walk builds an
   array of these over the list and writes them out. */
typedef struct ArrayListNode {
    ListNode base;      /* +0x00 */
    int32_t  count;     /* +0x30, how many indices */
    int32_t *forward;   /* +0x34 */
} ArrayListNode;

ArrayListNode *aln_ctorEmpty(ArrayListNode *n);
void           aln_dtor(ArrayListNode *n);
void           aln_save(ArrayListNode *n, FILE *f);
int32_t        aln_load(ArrayListNode *n, FILE *f);
void           aln_dump(ArrayListNode *n);

/* ---- SkipList -------------------------------------------------------- */

/* The store itself: an ordered set of key and translation, with a tower of
   forward pointers over it so that a lookup skips rather than walks.
 *
 * `count' includes the head, so a new list says one. `level' is the highest
 * tower in use. `cursor' is where getFirst and getNext have got to, and
 * `closest' is scratch that findClosest works in -- both are the original's
 * own fields rather than locals, which is why two searches cannot run at
 * once. */
typedef struct SkipList {
    int32_t       count;    /* +0x04 */
    SkipListNode *head;     /* +0x08 */
    SkipListNode *closest;  /* +0x0c */
    SkipListNode *cursor;   /* +0x10 */
    int32_t       level;    /* +0x14 */
    int32_t       bitsLeft; /* +0x18 */
    int32_t       bits;     /* +0x1c */
} SkipList;

/* How high a tower may go, and how many levels the head is made with. */
#define SL_MAX_LEVEL 16

SkipList    *sl_ctor(SkipList *l);
void         sl_dtor(SkipList *l);
int32_t      sl_insert(SkipList *l, Key *key, Translation *t);
Translation *sl_search(SkipList *l, Key *key);
Translation *sl_multiSearch(SkipList *l, Key *key);
void         sl_freeMultiSearch(Translation *found);
int32_t      sl_remove(SkipList *l, Key *key);
int32_t      sl_getFirst(SkipList *l, Key **key, Translation **t);
int32_t      sl_getNext(SkipList *l, Key **key, Translation **t);
int32_t      sl_save(SkipList *l, const char *path);
int32_t      sl_load(SkipList *l, const char *path);

#endif
