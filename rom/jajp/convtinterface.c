/* The surface the engine speaks to, and what Romanizer is built on.
 *
 * Everything the engine asks a Japanese instance to do arrives here first:
 * text to speak, a mark to put in it, a parameter to change part way through,
 * and the whole of the user dictionary. Romanizer derives from this class, so
 * one object answers both, and this half is the part that does not care what
 * Japanese is -- it recodes whatever arrived into Shift-JIS, hands text to
 * the InputManager to wait, and passes every dictionary call down to
 * RomUserDict, which is what actually knows how a word is stored.
 *
 * The record is IBM's, because Romanizer's is: the two are the same object
 * and rom/jajp/romanizer.h is the map. The base's own fields are the first
 * eight of it, from the vtable at 0x00 to the busy flag at 0x1c, and six of
 * them are pointers and so are parked past the record on a sixty-four bit
 * host. Nothing here reads one except through its _AT name.
 *
 * Two things in it are worth saying out loud.
 *
 * The buffer a recoded text goes into is thrown away and allocated again for
 * every text, and the size asked for is the length of the text plus one --
 * which is right for seven-bit JIS, where nothing grows, and right for EUC,
 * where a character shrinks or stays, but is a byte-for-byte bet rather than
 * a bound. It holds because both conversions only ever write as many bytes as
 * they read, or fewer.
 *
 * And `resume' spins. It waits on the busy flag by sleeping a tenth of a
 * second at a time until whatever was in processSentence has come out, and
 * only then resets the buffer and clears the stop. That is IBM's arrangement
 * and it is why stop and resume are two calls rather than one.
 *
 * Held to IBM's answer by test/romprims.sh.
 */

#include <stdint.h>
#include <string.h>
#include <sys/stat.h>
#include "jprom.h"
#include "romanizer.h"

/* A block's own fields. The pointers are parked; the two flags are not. */
#define CI_L(c, off)    (*(int32_t *)((uint8_t *)(c) + (off)))
#define CI_UNICODE(c)   (*(UnicodeConverter **)((uint8_t *)(c) + RZ_UNICODE_AT))
#define CI_PARAM(c)     (*(RomInstParam **)((uint8_t *)(c) + RZ_PARAM_AT))
#define CI_INPUT(c)     (*(InputManager **)((uint8_t *)(c) + RZ_INPUT_AT))
#define CI_TRANSBUF(c)  (*(char **)((uint8_t *)(c) + RZ_TRANSBUF_AT))
#define CI_USERDICT(c)  (*(RomUserDict **)((uint8_t *)(c) + RZ_USERDICT_AT))

/* How long resume sleeps between looks, in milliseconds, and what kind of
   sleep it asks for. */
#define CI_SPIN_MS   100
#define CI_SPIN_KIND 0

extern int32_t th_sleep(int32_t ms, int32_t kind);
extern int32_t fileFindInPath(const char *name, char *out);

/* ---- being made and unmade ------------------------------------------- */

/* The parameter block kept, and this converter written into it so that
   anything holding the block can find its way back. Then an InputManager,
   which is the only thing made here; the Unicode converter and the recoding
   buffer wait until something asks for them. */
void ci_initBase(void *c, RomInstParam *param)
{
    InputManager *m;

    CI_PARAM(c) = param;
    CI_PARAM(c)->owner = c;

    m = cpp_new(sizeof *m);
    if (m)
        im_ctor(m, CI_PARAM(c));
    CI_INPUT(c) = m;
    if (CI_INPUT(c) == 0)
        rp_setError(CI_PARAM(c), ROM_ERR_MEMORY);

    CI_TRANSBUF(c)     = 0;
    CI_UNICODE(c)      = 0;
    CI_L(c, RZ_STOPPED) = 0;
    CI_L(c, RZ_BUSY)    = 0;
}

/* And undone in the order they were made, each field cleared as it goes so
   that a second call has nothing left to free. */
void ci_closeBase(void *c)
{
    if (CI_TRANSBUF(c))
        cpp_delete(CI_TRANSBUF(c));
    CI_TRANSBUF(c) = 0;

    if (CI_UNICODE(c)) {
        UnicodeConverter *u = CI_UNICODE(c);

        uc_dtor(u);
        cpp_delete(u);
    }
    CI_UNICODE(c) = 0;

    if (CI_INPUT(c)) {
        InputManager *m = CI_INPUT(c);

        im_dtor(m);
        cpp_delete(m);
    }
    CI_INPUT(c) = 0;
}

/* ---- Unicode --------------------------------------------------------- */

/* The converter is made on the first call that wants it, and a failure to
   make it is answered as one rather than remembered. */
static int32_t needUnicode(void *c, void **out)
{
    UnicodeConverter *u;

    if (CI_UNICODE(c))
        return 1;

    u = cpp_new(sizeof *u);
    if (u)
        uc_ctor(u, CI_PARAM(c));
    CI_UNICODE(c) = u;
    if (CI_UNICODE(c))
        return 1;

    *(void **)out = 0;
    rp_setError(CI_PARAM(c), ROM_ERR_MEMORY);
    return 0;
}

int32_t ci_UCS2ToMBCS(void *c, const uint16_t *in, char **out, int32_t yen)
{
    if (!needUnicode(c, (void **)out))
        return 1;
    return uc_UCS2ToMBCS(CI_UNICODE(c), in, out, yen);
}

int32_t ci_MBCSToUCS2(void *c, const char *in, uint16_t **out)
{
    if (!needUnicode(c, (void **)out))
        return 1;
    return uc_MBCSToUCS2(CI_UNICODE(c), in, out);
}

/* ---- text and marks on their way in ---------------------------------- */

int32_t ci_insertIndex(void *c)
{
    return im_insertIndex(CI_INPUT(c));
}

int32_t ci_addParam(void *c, const char *text, int32_t len)
{
    return im_addParam(CI_INPUT(c), text, len);
}

/* Every mark and parameter that belongs at or before the given point,
 * appended to the caller's string.
 *
 * The queue is walked head first, and each element that belongs no further
 * along than `at' is written out with a space before it and taken off. The
 * answer says whether the walk stopped because it ran out of elements, which
 * is nought, or because the next one belongs further along, which is one.
 *
 * Nothing bounds what is written, so the caller's buffer has to be large
 * enough for everything the queue is holding. */
int32_t ci_outputIndexOrParam(void *c, char *out, int32_t at)
{
    int32_t next;

    if (!im_hasMoreElement(CI_INPUT(c)))
        return 0;

    next = im_getNextOffset(CI_INPUT(c));
    while (at >= next) {
        const char *data;
        int32_t     len;

        strcat(out, " ");
        len = im_getNextData(CI_INPUT(c), &data);
        strncat(out, data, len);
        im_removeElement(CI_INPUT(c));

        if (!im_hasMoreElement(CI_INPUT(c)))
            return 0;
        next = im_getNextOffset(CI_INPUT(c));
    }
    return 1;
}

/* Text handed over to wait, recoded first if it did not arrive in Shift-JIS.
   A text of no length, or none at all, is refused with the same answer a
   successful call gives, which is IBM's. */
int32_t ci_addText(void *c, const char *text, int32_t len, int32_t inputType)
{
    int32_t     codeset;
    const char *recoded;
    uint32_t    n;

    if (text == 0 || len == 0)
        return 1;

    rp_setInputType(CI_PARAM(c), inputType);
    codeset = rp_getCodeSet(CI_PARAM(c));

    n = ci_trans2defaultCodeset(c, (void *)text, len, codeset, &recoded);
    if (n == 0)
        return 0;
    return im_addText(CI_INPUT(c), recoded, n, codeset);
}

/* Whatever arrived, as Shift-JIS, and how many bytes that came to.
 *
 * A text that is already Shift-JIS is handed straight back without a copy;
 * everything else goes through a buffer of this converter's own, thrown away
 * and made again each time. A codeset this does not know is answered with
 * nought and the caller's out pointer left alone, which is the one path that
 * does not set it. */
uint32_t ci_trans2defaultCodeset(void *c, void *text, int32_t len,
                                 int32_t codeset, const char **out)
{
    uint32_t n = 0;

    switch (codeset) {
    case 0:
    case 0x80000:
    case 0x80800:
        n    = (uint32_t)len;
        *out = text;
        break;

    case 0x80100:                       /* EUC-JP */
        if (CI_TRANSBUF(c))
            cpp_delete(CI_TRANSBUF(c));
        CI_TRANSBUF(c) = cpp_new((uint32_t)len + 1);
        if (CI_TRANSBUF(c) == 0) {
            rp_setError(CI_PARAM(c), ROM_ERR_MEMORY);
            return 0;
        }
        CI_TRANSBUF(c)[0] = 0;
        n    = (uint32_t)ju_euc2shift(text, len, CI_TRANSBUF(c), 0);
        *out = CI_TRANSBUF(c);
        break;

    case 0x80200:                       /* the three seven-bit JIS ones */
    case 0x80300:
    case 0x80400:
        if (CI_TRANSBUF(c))
            cpp_delete(CI_TRANSBUF(c));
        CI_TRANSBUF(c) = cpp_new((uint32_t)len + 1);
        if (CI_TRANSBUF(c) == 0) {
            rp_setError(CI_PARAM(c), ROM_ERR_MEMORY);
            return 0;
        }
        CI_TRANSBUF(c)[0] = 0;
        n    = (uint32_t)ju_seven2shift(text, len, CI_TRANSBUF(c));
        *out = CI_TRANSBUF(c);
        break;

    default:
        break;
    }

    return n;
}

/* ---- stopping and starting again ------------------------------------- */

int32_t ci_stop(void *c)
{
    CI_L(c, RZ_STOPPED) = 1;
    return 1;
}

/* Waits for whatever is inside processSentence to come out, then throws away
   what it had collected and lets text through again. */
int32_t ci_resume(void *c)
{
    while (CI_L(c, RZ_BUSY))
        th_sleep(CI_SPIN_MS, CI_SPIN_KIND);

    CI_VT(c)->ResetBuffer(c);
    CI_L(c, RZ_STOPPED) = 0;
    return 1;
}

/* ---- the user dictionary --------------------------------------------- */

/* A store to put entries in, or nothing if it could not be made whole. A
   skip list whose head could not be allocated is destroyed rather than handed
   back, since nothing would work on it. */
void *ci_newDict(void *c)
{
    SkipList *l = cpp_new(sizeof *l);

    (void)c;
    if (l)
        sl_ctor(l);

    if (l && l->head == 0) {
        sl_dtor(l);
        cpp_delete(l);
        l = 0;
    }
    return l;
}

/* One thrown away, and this converter told to stop using it if it was the one
   in force. It is told that whether or not it was, which is IBM's: a caller
   that deletes a dictionary it never set clears the one that is set. */
void ci_deleteDict(void *c, void *dict)
{
    SkipList *l = dict;

    if (l) {
        sl_dtor(l);
        cpp_delete(l);
    }
    CI_USERDICT(c)->dict = 0;
}

void ci_setDict(void *c, void *dict)
{
    CI_USERDICT(c)->dict = dict;
}

/* Where a dictionary file is and how big it is, or minus one if it is not
   there to be found. */
long ci_findDictFile(void *c, const char *name, char *out)
{
    struct stat st;

    (void)c;
    if (!fileFindInPath(name, out))
        return -1;
    if (stat(out, &st) != 0)
        return -1;
    return (long)st.st_size;
}

/* A store filled from a file. The three answers the skip list gives are
   turned into the three the dictionary API has: nothing there, nothing
   readable, and anything else. */
int32_t ci_loadDict(void *c, void *dict, int32_t which, const char *name)
{
    /* The room IBM's frame gives it, and the only bound on what
       fileFindInPath writes. */
    char      found[268];
    SkipList *l   = dict;
    int32_t   got = ECI_DICT_ERROR;
    int32_t   n;

    (void)which;
    if (ci_findDictFile(c, name, found) <= 0)
        return got;

    /* The file was looked for along the path and found, and then loaded by
       the name the caller gave rather than by the one that was found. That is
       IBM's, and it means a dictionary that is only on the path is located
       and then not opened. */
    n = sl_load(l, name);
    if (n >= 0)
        return 0;
    if (n == -1)
        return ECI_DICT_NO_MEMORY;
    if (n == -2)
        return -1;
    return got;
}

/* And written back out. Nothing looks for the file first: a save names the
   file it is given. */
int32_t ci_saveDict(void *c, void *dict, int32_t which, const char *name)
{
    SkipList *l = dict;
    int32_t   n;

    (void)c;
    (void)which;
    n = sl_save(l, name);
    if (n >= 0)
        return 0;
    if (n == -1)
        return ECI_DICT_NO_MEMORY;
    if (n == -2)
        return -1;
    return ECI_DICT_ERROR;
}

/* One word looked up, in whatever codeset the caller says it wrote it in.
 *
 * Where the word had to be recoded the recoded form is copied out of the
 * shared buffer first, because RomUserDict may recode something else before
 * it is done with this one; that copy is freed on the way out. Where nothing
 * had to be recoded the buffer is not touched and there is nothing to free. */
int32_t ci_lookupDictExt(void *c, void *dict, int32_t which, uint8_t *word,
                         int32_t wordLen, void **value, int32_t *valueLen,
                         int32_t *pos, int32_t codeset)
{
    const char *recoded;
    char       *own;
    uint8_t     n;
    int32_t     got;

    /* The length is cut to a byte on its way down, so a word longer than
       two hundred and fifty-five bytes is looked up by a prefix of itself.
       IBM's, and it costs nothing here: a key that long is refused further
       down anyway. */
    n = (uint8_t)ci_trans2defaultCodeset(c, word, wordLen, codeset, &recoded);

    if ((const char *)word != recoded) {
        own = cpp_new((uint32_t)strlen(recoded) + 1);
        if (own == 0) {
            rp_setError(CI_PARAM(c), ROM_ERR_MEMORY);
            return ECI_DICT_NO_MEMORY;
        }
        strcpy(own, recoded);
    } else {
        own = (char *)(uintptr_t)recoded;
    }

    got = rud_lookupDictExt(CI_USERDICT(c), dict, which, (uint8_t *)own, n,
                            value, valueLen, pos);

    if ((const char *)word != recoded)
        cpp_delete(own);
    return got;
}

/* The first entry in a store, and the one after it, read out field by field.
 * The two are the same walk with a different starting point. Neither reads
 * which store the caller named or what codeset it wanted, so both hand back
 * whatever is in the list in whatever it was stored as.
 *
 * When there is nothing to give, the three later fields are cleared and the
 * two earlier ones are left as the caller had them, which is IBM's. */
static int32_t readEntry(int32_t got, Translation *t, void **word,
                         int32_t *wordLen, void **extra, int32_t *extraLen,
                         int32_t *pos)
{
    if (!got) {
        *extra    = 0;
        *extraLen = 0;
        *pos      = 0;
        return ECI_DICT_NO_ENTRY;
    }

    *word     = t->word;
    *wordLen  = t->wordLen;
    *extra    = t->extra;
    *extraLen = t->extraLen;
    *pos      = t->pos;
    return 0;
}

int32_t ci_findFirstDictEntryExt(void *c, void *dict, int32_t which,
                                 void **word, int32_t *wordLen,
                                 void **extra, int32_t *extraLen,
                                 int32_t *pos, int32_t codeset)
{
    SkipList    *l = dict;
    Key         *k = 0;
    Translation *t = 0;
    int32_t      got;

    (void)c;
    (void)which;
    (void)codeset;
    /* The walk first and the entry after it. In one expression the order is
       the compiler's, and it read the entry before the call that sets it. */
    got = sl_getFirst(l, &k, &t);
    return readEntry(got, t, word, wordLen, extra, extraLen, pos);
}

int32_t ci_findNextDictEntryExt(void *c, void *dict, int32_t which,
                                void **word, int32_t *wordLen,
                                void **extra, int32_t *extraLen,
                                int32_t *pos, int32_t codeset)
{
    SkipList    *l = dict;
    Key         *k = 0;
    Translation *t = 0;
    int32_t      got;

    (void)c;
    (void)which;
    (void)codeset;
    got = sl_getNext(l, &k, &t);
    return readEntry(got, t, word, wordLen, extra, extraLen, pos);
}

/* One entry put in, changed or taken out. Both the word and the reading may
   have arrived in a codeset that is not Shift-JIS, so both are recoded; only
   the word gets a copy of its own, because the reading is used before
   anything can recode over it. */
int32_t ci_updateDictExt(void *c, void *dict, int32_t which, uint8_t *word,
                         int32_t wordLen, char *kana, int32_t kanaLen,
                         int32_t pos, int32_t codeset)
{
    const char *recoded;
    const char *kanaOut;
    char       *own;
    uint32_t    n;
    uint32_t    kanaN;
    int32_t     got;

    n = ci_trans2defaultCodeset(c, word, wordLen, codeset, &recoded);

    if ((const char *)word != recoded) {
        own = cpp_new((uint32_t)strlen(recoded) + 1);
        if (own == 0) {
            rp_setError(CI_PARAM(c), ROM_ERR_MEMORY);
            return ECI_DICT_NO_MEMORY;
        }
        strcpy(own, recoded);
    } else {
        own = (char *)(uintptr_t)recoded;
    }

    kanaN = ci_trans2defaultCodeset(c, kana, kanaLen, codeset, &kanaOut);

    got = rud_updateDictExt(CI_USERDICT(c), dict, which, (uint8_t *)own, n,
                            (char *)(uintptr_t)kanaOut, kanaN, pos);

    if ((const char *)word != recoded)
        cpp_delete(own);
    return got;
}
