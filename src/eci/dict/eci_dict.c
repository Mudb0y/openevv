/* User dictionaries: making them, choosing them, and taking them away.
 *
 * A dictionary belongs to a language, and an instance may have one in force
 * for each language and dialect at once. That is what the table in the
 * middle of the instance is: eighteen families of two dialects, each holding
 * the dictionary currently active for it, or nothing.
 *
 * The table starts at the same offset as the queue the caller may fill,
 * which looks alarming until you notice that families are numbered from one,
 * so the family-nought slot is never touched by anything here and the queue
 * has it to itself.
 *
 * Loading a dictionary from a file and saving one to a file were published
 * and never written; both answer that they could not.
 *
 * Reading a dictionary and writing to it are at the foot, and they are the
 * eight published calls that had no wrapper here until they were written out
 * of `eci.obj': the four questions -- look a key up, find the first entry,
 * find the next, change one -- each in a plain form and in the extended form
 * that carries a part of speech. Everything they lean on was already here;
 * what was missing was the glue.
 *
 * Names are prefixed and the aliases at the foot carry the real ones. */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "eci_synththread.h"
#include "evv_abi.h"
#include "evv_arena.h"
#include "eci_old.h"

/* The dictionary in force for one language and dialect. Families run from
   one to eighteen and dialects are nought or one.

   A slot is four bytes, as it is in the instance the original lays out, so
   what goes in it is a value and not a host pointer. Written as one, an
   eight-byte store on a sixty-four bit host reaches over the slot beside it:
   putting German's dictionary in family four, dialect nought writes across
   family three, dialect one, and the loop below reads that one first and
   hands the engine half a pointer. English never showed it -- family one is
   the first slot the loop looks at, so the slot its store reaches into is
   one nothing ever reads. */
#define ACTIVE_DICT(h, family, dialect) \
    (*(evv_ref *)((char *)(h) + 0x60c + (family) * 8 + (dialect) * 4))
#define DICT_FAMILIES   0x12
#define DICT_DIALECTS   2

#define ENV_LANGUAGE    9

/* What the older interface calls the answer to a dictionary call. */
#define DICT_OK             0
#define DICT_NO_ROOM        2
#define DICT_INTERNAL       3
#define DICT_NO_ENTRY       4
#define DICT_BAD_KEY        5
#define DICT_NOT_SUPPORTED  6
#define DICT_BAD_VOLUME     7

/* The volume that is not one of the three a set holds: it is the extended
   main dictionary, and it is the one the calls with a part of speech are
   for. A language without one answers that the volume is wrong. */
#define DICT_VOLUME_EXT     3

/* What the converters answer with when they could not, which the original
   turns into an error rather than passing on. */
#define DICT_RC_CONVERSION  (-17)

/* Whether an instance is in the middle of speaking. */
#define SYNTH_BUSY      3

extern int32_t STDCALL api_check_synth(void *h2)
    MANGLED("_eciCheckSynthesizing2@4");
extern int32_t STDCALL api_synthesize(void *h2)
    MANGLED("_eciSynthesize2@4");
extern int32_t STDCALL api_synchronize(void *h2)
    MANGLED("_eciSynchronize2@4");
extern int32_t STDCALL api_new_dict(void *h2, int32_t lang, void **out)
    MANGLED("_eciNewDict2@12");
extern int32_t STDCALL api_delete_dict(void *h2, void *dict)
    MANGLED("_eciDeleteDict2@8");
extern int32_t STDCALL api_activate_dict(void *h2, void *dict)
    MANGLED("_eciActivateDict2@8");
extern int32_t STDCALL api_deactivate_dict(void *h2, void *dict)
    MANGLED("_eciDeactivateDict2@8");
extern int32_t STDCALL api_get_active_dict(void *h2, int32_t lang,
                                           void **out)
    MANGLED("_eciGetActiveDict2@12");
extern int32_t STDCALL api_get_dict_language(void *h2, void *dict,
                                             int32_t *lang)
    MANGLED("_eciGetDictLanguage2@12");
extern int32_t STDCALL eo_getParam(OldInst *h, int32_t which)
    MANGLED("_eciGetParam@8");

extern int ev_sendParameters(OldInst *h);

extern int32_t STDCALL api_lookup_dict(void *h2, void *dict, int32_t volume,
                                       void *key, int32_t keylen, void **xlat,
                                       int32_t *xlatlen, int32_t pos)
    MANGLED("_eciLookupDict2@32");
extern int32_t STDCALL api_lookup_dict_ext(void *h2, void *dict,
                                           int32_t volume, void *key,
                                           int32_t keylen, void **xlat,
                                           int32_t *xlatlen, int32_t *pos,
                                           int32_t lang)
    MANGLED("_eciLookupDictExt2@36");
extern int32_t STDCALL api_find_first(void *h2, void *dict, int32_t volume,
                                      void **key, int32_t *keylen,
                                      void **xlat, int32_t *xlatlen,
                                      int32_t pos)
    MANGLED("_eciFindFirstDictEntry2@32");
extern int32_t STDCALL api_find_first_ext(void *h2, void *dict, int32_t volume,
                                          void **key, int32_t *keylen,
                                          void **xlat, int32_t *xlatlen,
                                          int32_t *pos, int32_t lang)
    MANGLED("_eciFindFirstDictEntryExt2@36");
extern int32_t STDCALL api_find_next(void *h2, void *dict, int32_t volume,
                                     void **key, int32_t *keylen,
                                     void **xlat, int32_t *xlatlen,
                                     int32_t pos)
    MANGLED("_eciFindNextDictEntry2@32");
extern int32_t STDCALL api_find_next_ext(void *h2, void *dict, int32_t volume,
                                         void **key, int32_t *keylen,
                                         void **xlat, int32_t *xlatlen,
                                         int32_t *pos, int32_t lang)
    MANGLED("_eciFindNextDictEntryExt2@36");
extern int32_t STDCALL api_update_dict(void *h2, void *dict, int32_t volume,
                                       void *key, int32_t keylen, void *xlat,
                                       int32_t xlatlen, int32_t pos)
    MANGLED("_eciUpdateDict2@32");
extern int32_t STDCALL api_update_dict_ext(void *h2, void *dict,
                                           int32_t volume, void *key,
                                           int32_t keylen, void *xlat,
                                           int32_t xlatlen, int32_t pos,
                                           int32_t lang)
    MANGLED("_eciUpdateDictExt2@36");

extern int isUnicodeCodeSet(int32_t bit, int32_t mode)
    MANGLED("_isUnicodeCodeSet");
extern int UnicodeConverter(OldInst *h, const void *in, void *out,
                            int32_t which) MANGLED("_UnicodeConverter");
extern int MBCSConverter(OldInst *h, const void *in, void *out)
    MANGLED("_MBCSConverter");
extern void *cpp_new(uint32_t n);
extern void cpp_delete(void *p);

/* ---- turning the engine's answers into the older interface's --------- */

int ed_rc_to_ECIDictError(int32_t rc)
{
    if (rc == -2)
        return DICT_NO_ROOM;
    if (rc >= 0)
        return DICT_OK;
    return DICT_NOT_SUPPORTED;
}

/* ---- the table of what is in force ----------------------------------- */

/* Remember a dictionary as the one in force for its own language. */
int32_t ed_add_active_dict(OldInst *h, void *dict)
{
    int32_t lang;
    int32_t rc = api_get_dict_language(OI_NEW(h), dict, &lang);

    if (rc >= 0)
        ACTIVE_DICT(h, (lang & 0xff0000) >> 16, lang & 0xff) = EVV_REF(dict);
    return rc;
}

/* Forget it again, but only if it is still the one recorded there. */
int32_t ed_delete_active_dict(OldInst *h, void *dict)
{
    int32_t lang;
    int32_t rc = api_get_dict_language(OI_NEW(h), dict, &lang);

    if (rc >= 0) {
        int family = (lang & 0xff0000) >> 16;
        int dialect = lang & 0xff;

        if (EVV_AT(void *, ACTIVE_DICT(h, family, dialect)) == dict)
            ACTIVE_DICT(h, family, dialect) = 0;
    }
    return rc;
}

/* Turn every one of them off. The engine's answers are collected but not
   looked at; this always reports success. */
int32_t ed_deactivate_all_dicts(OldInst *h)
{
    int family, dialect;

    for (family = 1; family <= DICT_FAMILIES; family++)
        for (dialect = 0; dialect < DICT_DIALECTS; dialect++) {
            void *dict = EVV_AT(void *, ACTIVE_DICT(h, family, dialect));

            if (dict)
                api_deactivate_dict(OI_NEW(h), dict);
            ACTIVE_DICT(h, family, dialect) = 0;
        }
    return 0;
}

/* ---- the entry points ------------------------------------------------ */

/* Make an empty dictionary for whatever language is in force.

   Everything queued is spoken and waited for first. A dictionary changes how
   words are pronounced, so anything already on its way has to come out under
   the old rules before the new dictionary can exist. */
void *STDCALL ed_newDict(OldInst *h)
{
    OldInst *inst = h;
    void *dict = 0;
    int32_t rc = -1;
    int32_t lang;

    if (!h)
        return 0;

    if (api_check_synth(OI_NEW(inst)) == SYNTH_BUSY) {
        OI_REFUSED(inst) = 0x2000;
        OI_REFUSEDALL(inst) |= 0x2000;
        return 0;
    }

    ev_sendParameters(inst);
    api_synthesize(OI_NEW(inst));
    api_synchronize(OI_NEW(inst));

    lang = eo_getParam(h, ENV_LANGUAGE);
    if (lang >= 0)
        rc = api_new_dict(OI_NEW(inst), lang, &dict);

    return (rc >= 0) ? dict : 0;
}

/* Which dictionary is in force for the language in force. */
void *STDCALL ed_getDict(OldInst *h)
{
    void *dict = 0;
    int32_t rc = -1;
    int32_t lang;

    if (!h)
        return 0;

    lang = eo_getParam(h, ENV_LANGUAGE);
    if (lang >= 0)
        rc = api_get_active_dict(OI_NEW(h), lang, &dict);

    return (rc >= 0) ? dict : 0;
}

/* Put one in force, or with nothing named, take all of them out. */
int STDCALL ed_setDict(OldInst *h, void *dict)
{
    int32_t rc = -1;

    if (!h)
        return DICT_NOT_SUPPORTED;

    if (!dict) {
        rc = ed_deactivate_all_dicts(h);
    } else {
        rc = api_activate_dict(OI_NEW(h), dict);
        if (rc >= 0)
            rc = ed_add_active_dict(h, dict);
    }
    return ed_rc_to_ECIDictError(rc);
}

/* Take one away for good. Answers nought whatever happens. */
int STDCALL ed_deleteDict(OldInst *h, void *dict)
{
    int32_t rc;

    if (!h)
        return 0;

    rc = ed_delete_active_dict(h, dict);
    if (rc >= 0)
        api_delete_dict(OI_NEW(h), dict);
    return 0;
}

/* Reading a dictionary from a file and writing one to a file were published
   and never written. */
int STDCALL ed_loadDict(OldInst *h, void *dict, int32_t kind,
                          const char *name)
{
    (void)h;
    (void)dict;
    (void)kind;
    (void)name;
    return DICT_NOT_SUPPORTED;
}

int STDCALL ed_saveDict(OldInst *h, void *dict, int32_t kind,
                          const char *name)
{
    (void)h;
    (void)dict;
    (void)kind;
    (void)name;
    return DICT_NOT_SUPPORTED;
}


/* ---- reading the dictionary and writing to it ------------------------ */

/* Which of three roads a language takes through the eight calls below.
 *
 * The original writes this switch out inside every one of the eight, and all
 * eight hold the same one -- checked by comparing the constants their
 * compares carry, which agree name for name rather than being assumed to.
 * It is one function here.
 *
 * What it comes to is that the extended calls, the ones carrying a part of
 * speech, are for the languages written in another script: Chinese in four
 * code sets, Korean in two, Japanese in two. Every other language takes the
 * plain road, and asking it for the extended volume answers that the volume
 * is wrong.
 *
 * One thing in it is IBM's own slip and is kept. Chinese code set two
 * reaches the extended road in its second dialect and not its first --
 * 0x60201 is in the switch and 0x60200 is not -- so one language in one code
 * set answers differently by dialect. No build of this tree can show it:
 * this SDK has no family six at all.
 */
#define DICT_ROAD_PLAIN  0     /* the plain calls only */
#define DICT_ROAD_EXT    1     /* both, whichever the volume asks for */
#define DICT_ROAD_WIDE   2     /* the extended calls only */

static int ed_road(int32_t lang)
{
    switch (lang) {
    case 0x60000: case 0x60001:          /* Chinese, code set nought */
    case 0x60100: case 0x60101:          /* and one */
    case 0x60201:                        /* and two, second dialect only */
    case 0x60800: case 0x60801:          /* and wide */
    case 0xa0000: case 0xa0800:          /* Korean, narrow and wide */
        return DICT_ROAD_EXT;

    case 0x80000: case 0x80800:          /* Japanese */
        return DICT_ROAD_WIDE;

    default:
        return DICT_ROAD_PLAIN;
    }
}

/* The part of speech the plain calls hand down, which is not the caller's
   business: the language decides it and only Korean asks for one. The
   dictionary's own language is asked rather than the instance's, since a set
   may hold a dictionary for a language the instance is not speaking. */
static int32_t ed_plainPos(OldInst *h, void *dict)
{
    int32_t lang = 0;

    api_get_dict_language(OI_NEW(h), dict, &lang);
    return (((lang & 0xff0000) >> 16) == 0x0a) ? 0x0a : 0;
}

/* Copy the narrow string the layer left into memory the instance owns, and
   give back what it owned before. The caller is handed a pointer into this,
   which is why it cannot be a local: it has to outlive the call and be gone
   by the next one.
 *
 * The length is the caller's own code set: a wide one needs two bytes a
 * character and a nought of two bytes after them. One thing here is the
 * original's and is not corrected -- the wide arm copies the whole of what
 * it allocated rather than what the source holds, so it reads two bytes past
 * the string. It is left because it is what the original does and nothing in
 * this tree can reach it. */
static int ed_ownCopy(void **owned, const void *from, int32_t len, int wide)
{
    uint32_t room = wide ? (uint32_t)len * 2 + 2 : (uint32_t)len + 1;

    if (*owned) {
        cpp_delete(*owned);
        *owned = 0;
    }
    *owned = cpp_new(room);
    if (*owned == 0)
        return 0;
    memset(*owned, 0, room);
    memcpy(*owned, from, wide ? room : (size_t)len);
    return 1;
}

/* Look a key up. What comes back is the translation in the caller's own code
   set, or nothing.
 *
 * The copy of the converted key is never given back, which is the original's
 * and is left: it leaks a string per call whenever the conversion made one,
 * and the extended form below frees its own. */
static const char *ed_lookup(OldInst *h, void *dict, int32_t volume,
                             const void *key)
{
    const char *answer = 0;
    void   *wide = (void *)(intptr_t)key;
    void   *own;
    int32_t len;
    int32_t room;

    if (UnicodeConverter(h, key, &wide, 1))
        return 0;

    if (key != wide) {
        own = cpp_new((uint32_t)strlen(wide) + 1);
        if (own == 0)
            return 0;
        strcpy(own, wide);
    } else {
        own = wide;
    }

    len = own ? (int32_t)strlen(own) : 0;
    room = OI_DICT_XLATLEN(h);

    if (api_lookup_dict(OI_NEW(h), dict, volume, own, len,
                        &OI_DICT_XLAT(h), &room,
                        ed_plainPos(h, dict)) != 0)
        return 0;
    if (room == 0)
        return 0;
    if (MBCSConverter(h, OI_DICT_XLAT(h), &answer))
        return 0;
    return answer;
}

/* And the same with a part of speech, which is what the extended volume
   holds. The caller is given the translation and the part of speech it was
   found under. */
static int32_t ed_lookupExt(OldInst *h, void *dict, int32_t volume,
                            const void *key, const char **out, int32_t *pos)
{
    void   *wide = (void *)(intptr_t)key;
    int32_t rc;
    int32_t len;

    if (UnicodeConverter(h, key, &wide, 1))
        return ed_rc_to_ECIDictError(DICT_RC_CONVERSION);

    OI_DICT_XLAT(h) = 0;
    OI_DICT_XLATLEN(h) = 0;
    len = wide ? (int32_t)strlen(wide) : 0;

    rc = api_lookup_dict_ext(OI_NEW(h), dict, volume, wide, len,
                             &OI_DICT_XLAT(h), &OI_DICT_XLATLEN(h),
                             &OI_DICT_POS(h), OI_LANG(h));
    if (rc == 0) {
        if (OI_DICT_XLATLEN(h) == 0) {
            *out = 0;
            return DICT_NO_ENTRY;
        }
    } else if (rc == DICT_BAD_KEY) {
        *out = 0;
        *pos = 0;
        return DICT_NO_ENTRY;
    }

    if (MBCSConverter(h, OI_DICT_XLAT(h), out))
        return DICT_INTERNAL;
    *pos = OI_DICT_POS(h);
    return ed_rc_to_ECIDictError(rc);
}

/* Walk the dictionary: the first entry, and then each next one. The two are
   one body in the original as well, bar which of the two layer calls it
   makes -- their disassemblies agree instruction for instruction and differ
   in that one relocation.
 *
 * Both strings come back in memory the instance owns, so a caller keeps them
 * only until it asks again. */
static int32_t ed_findEntry(OldInst *h, void *dict, int32_t volume,
                            const char **keyOut, const char **xlatOut,
                            int first)
{
    const char *key = 0;
    const char *xlat = 0;
    int32_t rc;
    int32_t keylen  = OI_DICT_KEYLEN(h);
    int32_t xlatlen = OI_DICT_XLATLEN(h);
    int32_t pos = ed_plainPos(h, dict);
    int     wide;

    rc = first
       ? api_find_first(OI_NEW(h), dict, volume, &OI_DICT_KEY(h), &keylen,
                        &OI_DICT_XLAT(h), &xlatlen, pos)
       : api_find_next(OI_NEW(h), dict, volume, &OI_DICT_KEY(h), &keylen,
                       &OI_DICT_XLAT(h), &xlatlen, pos);

    *keyOut = 0;
    *xlatOut = 0;
    if (rc >= 0 && (keylen == 0 || xlatlen == 0))
        return DICT_NO_ENTRY;
    if (rc != 0)
        return ed_rc_to_ECIDictError(rc);

    if (MBCSConverter(h, OI_DICT_KEY(h), &key))
        return DICT_NOT_SUPPORTED;

    wide = isUnicodeCodeSet(0x800, eo_getParam(h, ENV_LANGUAGE)) != 0;

    if (!ed_ownCopy(&OI_OWNED2(h), key, keylen, wide))
        return DICT_NO_ROOM;
    *keyOut = OI_OWNED2(h);

    if (MBCSConverter(h, OI_DICT_XLAT(h), &xlat))
        return DICT_NOT_SUPPORTED;

    if (!ed_ownCopy(&OI_OWNED1(h), xlat, xlatlen, wide))
        return DICT_NO_ROOM;
    *xlatOut = OI_OWNED1(h);

    return ed_rc_to_ECIDictError(rc);
}

/* The same walk with a part of speech. It clears the five slots the layer
   answers into first, where the plain form seeds two of them from whatever
   was left there, and it takes the lengths back out of those slots rather
   than out of locals. Both are the original's.
 *
 * The two forms differ once more and it comes to nothing: the first reads
 * the instance's language field where the next asks eciGetParam for it,
 * which is the same word by a politer road. */
static int32_t ed_findEntryExt(OldInst *h, void *dict, int32_t volume,
                               const char **keyOut, const char **xlatOut,
                               int32_t *pos, int first)
{
    const char *key = 0;
    const char *xlat = 0;
    int32_t rc;
    int     wide;

    OI_DICT_KEY(h) = 0;
    OI_DICT_KEYLEN(h) = 0;
    OI_DICT_XLAT(h) = 0;
    OI_DICT_XLATLEN(h) = 0;
    OI_DICT_POS(h) = 0;

    rc = first
       ? api_find_first_ext(OI_NEW(h), dict, volume, &OI_DICT_KEY(h),
                            &OI_DICT_KEYLEN(h), &OI_DICT_XLAT(h),
                            &OI_DICT_XLATLEN(h), &OI_DICT_POS(h),
                            OI_LANG(h))
       : api_find_next_ext(OI_NEW(h), dict, volume, &OI_DICT_KEY(h),
                           &OI_DICT_KEYLEN(h), &OI_DICT_XLAT(h),
                           &OI_DICT_XLATLEN(h), &OI_DICT_POS(h),
                           eo_getParam(h, ENV_LANGUAGE));

    *keyOut = 0;
    *xlatOut = 0;
    *pos = 0;
    if (rc >= 0 && (OI_DICT_KEYLEN(h) == 0 || OI_DICT_XLATLEN(h) == 0))
        return DICT_NO_ENTRY;
    if (rc != 0)
        return ed_rc_to_ECIDictError(rc);

    if (MBCSConverter(h, OI_DICT_KEY(h), &key))
        return DICT_NOT_SUPPORTED;

    wide = isUnicodeCodeSet(0x800, eo_getParam(h, ENV_LANGUAGE)) != 0;

    if (!ed_ownCopy(&OI_OWNED2(h), key, OI_DICT_KEYLEN(h), wide))
        return DICT_NO_ROOM;
    *keyOut = OI_OWNED2(h);

    if (MBCSConverter(h, OI_DICT_XLAT(h), &xlat))
        return DICT_NOT_SUPPORTED;

    if (!ed_ownCopy(&OI_OWNED1(h), xlat, OI_DICT_XLATLEN(h), wide))
        return DICT_NO_ROOM;
    *xlatOut = OI_OWNED1(h);
    *pos = OI_DICT_POS(h);

    return ed_rc_to_ECIDictError(rc);
}

/* Teach the dictionary a word. Both strings are converted on the way down,
   and the copy of the key is given back on the one road the original gives
   it back on -- the second conversion failing -- and not on the road that
   succeeds. That leak is the original's, and the extended form below does
   free its own. */
static int32_t ed_update(OldInst *h, void *dict, int32_t volume,
                         const void *key, const void *xlat)
{
    void   *widekey = (void *)(intptr_t)key;
    void   *widexlat;
    void   *own;
    int32_t keylen, xlatlen;

    if (UnicodeConverter(h, key, &widekey, 1))
        return ed_rc_to_ECIDictError(DICT_RC_CONVERSION);

    if (key != widekey) {
        own = cpp_new((uint32_t)strlen(widekey) + 1);
        if (own == 0)
            return ed_rc_to_ECIDictError(-2);
        strcpy(own, widekey);
    } else {
        own = widekey;
    }

    widexlat = (void *)(intptr_t)xlat;
    if (UnicodeConverter(h, xlat, &widexlat, 1)) {
        if (key != widekey)
            cpp_delete(own);
        return ed_rc_to_ECIDictError(DICT_RC_CONVERSION);
    }

    keylen  = own ? (int32_t)strlen(own) : 0;
    xlatlen = widexlat ? (int32_t)strlen(widexlat) : 0;

    return ed_rc_to_ECIDictError(
        api_update_dict(OI_NEW(h), dict, volume, own, keylen,
                        widexlat, xlatlen, ed_plainPos(h, dict)));
}

/* And with a part of speech the caller supplies. This one takes the language
   off the instance's own field and gives the copy back afterwards. */
static int32_t ed_updateExt(OldInst *h, void *dict, int32_t volume,
                            const void *key, const void *xlat, int32_t pos)
{
    void   *widekey = (void *)(intptr_t)key;
    void   *widexlat;
    void   *own;
    int32_t keylen, xlatlen, rc;

    if (UnicodeConverter(h, key, &widekey, 1))
        return ed_rc_to_ECIDictError(DICT_RC_CONVERSION);

    if (key != widekey) {
        own = cpp_new((uint32_t)strlen(widekey) + 1);
        if (own == 0)
            return ed_rc_to_ECIDictError(-2);
        strcpy(own, widekey);
    } else {
        own = widekey;
    }

    widexlat = (void *)(intptr_t)xlat;
    if (UnicodeConverter(h, xlat, &widexlat, 1)) {
        if (key != widekey)
            cpp_delete(own);
        return ed_rc_to_ECIDictError(DICT_RC_CONVERSION);
    }

    keylen  = own ? (int32_t)strlen(own) : 0;
    xlatlen = widexlat ? (int32_t)strlen(widexlat) : 0;

    rc = api_update_dict_ext(OI_NEW(h), dict, volume, own, keylen,
                             widexlat, xlatlen, pos, OI_LANG(h));
    if (key != widekey)
        cpp_delete(own);
    return ed_rc_to_ECIDictError(rc);
}

/* ---- and the eight the caller sees ----------------------------------- */

/* Every one of the eight is the same shape: ask the instance what language
   it is in, take that language's road, and then dispatch on whether the
   volume asked for is the extended one. What differs is only the argument
   list and, in the plain forms, that a part of speech of nought is handed to
   the extended call where the extended forms hand the caller's own. */

const char *STDCALL ed_dictLookup(OldInst *h, void *dict, int32_t volume,
                                  const void *key)
{
    const char *answer = 0;
    int32_t     lang = eo_getParam(h, ENV_LANGUAGE);
    int32_t     pos = 0;

    /* Alone among the eight this one refuses a wide code set outright,
       before it has looked at the language's road at all -- so the two roads
       that only a wide language takes are unreachable from here. */
    if (isUnicodeCodeSet(0x800, lang))
        return 0;

    switch (ed_road(lang)) {
    case DICT_ROAD_EXT:
        if (volume == DICT_VOLUME_EXT) {
            if (ed_lookupExt(h, dict, volume, key, &answer, &pos) != 0)
                answer = 0;
        } else {
            answer = ed_lookup(h, dict, volume, key);
        }
        break;

    case DICT_ROAD_WIDE:
        if (volume == DICT_VOLUME_EXT) {
            if (ed_lookupExt(h, dict, volume, key, &answer, &pos) != 0)
                answer = 0;
        } else {
            answer = 0;
        }
        break;

    default:
        answer = (volume == DICT_VOLUME_EXT)
               ? 0 : ed_lookup(h, dict, volume, key);
        break;
    }
    return answer;
}

/* The extended form answers an error rather than the string: what it found
   goes into the caller's own pointer, and the answer says how it went.
 *
 * It ends by turning an empty answer into `no entry', and that test reads
 * the caller's pointer on the two roads that never write it -- where the
 * volume is wrong and the answer is seven. So a caller that has not cleared
 * its own variable can see four where seven was meant. That is the
 * original's and it is the caller's own memory being read, so it is
 * reproduced rather than tidied.
 *
 * Unlike the plain form it does not refuse a wide code set. */
int32_t STDCALL ed_dictLookupA(OldInst *h, void *dict, int32_t volume,
                               const void *key, const char **out,
                               int32_t *pos)
{
    int32_t lang = eo_getParam(h, ENV_LANGUAGE);
    int32_t rc = 0;

    switch (ed_road(lang)) {
    case DICT_ROAD_EXT:
        if (volume == DICT_VOLUME_EXT)
            rc = ed_lookupExt(h, dict, volume, key, out, pos);
        else
            *out = ed_lookup(h, dict, volume, key);
        break;

    case DICT_ROAD_WIDE:
        if (volume == DICT_VOLUME_EXT)
            rc = ed_lookupExt(h, dict, volume, key, out, pos);
        else
            rc = DICT_BAD_VOLUME;
        break;

    default:
        if (volume == DICT_VOLUME_EXT)
            rc = DICT_BAD_VOLUME;
        else
            *out = ed_lookup(h, dict, volume, key);
        break;
    }

    if (*out == 0)
        rc = DICT_NO_ENTRY;
    return rc;
}

static int32_t ed_findPublic(OldInst *h, void *dict, int32_t volume,
                             const char **keyOut, const char **xlatOut,
                             int32_t *pos, int first)
{
    int32_t lang = eo_getParam(h, ENV_LANGUAGE);
    int32_t here = 0;

    if (pos == 0)
        pos = &here;

    switch (ed_road(lang)) {
    case DICT_ROAD_EXT:
        return (volume == DICT_VOLUME_EXT)
             ? ed_findEntryExt(h, dict, volume, keyOut, xlatOut, pos, first)
             : ed_findEntry(h, dict, volume, keyOut, xlatOut, first);

    case DICT_ROAD_WIDE:
        return (volume == DICT_VOLUME_EXT)
             ? ed_findEntryExt(h, dict, volume, keyOut, xlatOut, pos, first)
             : DICT_BAD_VOLUME;

    default:
        return (volume == DICT_VOLUME_EXT)
             ? DICT_BAD_VOLUME
             : ed_findEntry(h, dict, volume, keyOut, xlatOut, first);
    }
}

int32_t STDCALL ed_dictFindFirst(OldInst *h, void *dict, int32_t volume,
                                 const char **keyOut, const char **xlatOut)
{
    return ed_findPublic(h, dict, volume, keyOut, xlatOut, 0, 1);
}

int32_t STDCALL ed_dictFindFirstA(OldInst *h, void *dict, int32_t volume,
                                  const char **keyOut, const char **xlatOut,
                                  int32_t *pos)
{
    return ed_findPublic(h, dict, volume, keyOut, xlatOut, pos, 1);
}

int32_t STDCALL ed_dictFindNext(OldInst *h, void *dict, int32_t volume,
                                const char **keyOut, const char **xlatOut)
{
    return ed_findPublic(h, dict, volume, keyOut, xlatOut, 0, 0);
}

int32_t STDCALL ed_dictFindNextA(OldInst *h, void *dict, int32_t volume,
                                 const char **keyOut, const char **xlatOut,
                                 int32_t *pos)
{
    return ed_findPublic(h, dict, volume, keyOut, xlatOut, pos, 0);
}

static int32_t ed_updatePublic(OldInst *h, void *dict, int32_t volume,
                               const void *key, const void *xlat, int32_t pos)
{
    int32_t lang = eo_getParam(h, ENV_LANGUAGE);

    switch (ed_road(lang)) {
    case DICT_ROAD_EXT:
        return (volume == DICT_VOLUME_EXT)
             ? ed_updateExt(h, dict, volume, key, xlat, pos)
             : ed_update(h, dict, volume, key, xlat);

    case DICT_ROAD_WIDE:
        return (volume == DICT_VOLUME_EXT)
             ? ed_updateExt(h, dict, volume, key, xlat, pos)
             : DICT_BAD_VOLUME;

    default:
        return (volume == DICT_VOLUME_EXT)
             ? DICT_BAD_VOLUME
             : ed_update(h, dict, volume, key, xlat);
    }
}

int32_t STDCALL ed_updateDict(OldInst *h, void *dict, int32_t volume,
                              const void *key, const void *xlat)
{
    return ed_updatePublic(h, dict, volume, key, xlat, 0);
}

int32_t STDCALL ed_updateDictA(OldInst *h, void *dict, int32_t volume,
                               const void *key, const void *xlat, int32_t pos)
{
    return ed_updatePublic(h, dict, volume, key, xlat, pos);
}

/* Wide characters off a stream, which is IBM's own helper for reading a
   dictionary file and is reached by nothing: the two calls that would read
   one are empty in its object as well as in this tree.
 *
 * One divergence, and it is the platform rather than a choice. The original
 * asks the stream's own flag word whether it is at the end or in error, by
 * the bits Microsoft's runtime puts there; feof and ferror are the same two
 * questions asked the way C asks them. */
void *ed_fgetUCS2(uint16_t *buf, int32_t count, FILE *f)
{
    size_t got;

    if (feof(f) || ferror(f))
        return 0;

    got = fread(buf, 2, (size_t)(count - 1), f);
    if (ferror(f))
        return 0;

    buf[got] = 0;
    return buf;
}

ALIAS("?rc_to_ECIDictError@@YA?AW4ECIDictError@@J@Z",
      "ed_rc_to_ECIDictError");
ALIAS("?add_active_dict@@YAJPAUoldECIInstData@@PAX@Z", "ed_add_active_dict");
ALIAS("?delete_active_dict@@YAJPAUoldECIInstData@@PAX@Z",
      "ed_delete_active_dict");
ALIAS("?deactivate_all_dicts@@YAJPAUoldECIInstData@@@Z",
      "ed_deactivate_all_dicts");

ALIAS_N("_eciNewDict@4", "ed_newDict", 4);
ALIAS_N("_eciGetDict@4", "ed_getDict", 4);
ALIAS_N("_eciSetDict@8", "ed_setDict", 8);
ALIAS_N("_eciDeleteDict@8", "ed_deleteDict", 8);
ALIAS_N("_eciLoadDict@16", "ed_loadDict", 16);
ALIAS_N("_eciSaveDict@16", "ed_saveDict", 16);

ALIAS("_fgetUCS2", "ed_fgetUCS2");
ALIAS_N("_eciDictLookup@16", "ed_dictLookup", 16);
ALIAS_N("_eciDictLookupA@24", "ed_dictLookupA", 24);
ALIAS_N("_eciDictFindFirst@20", "ed_dictFindFirst", 20);
ALIAS_N("_eciDictFindFirstA@24", "ed_dictFindFirstA", 24);
ALIAS_N("_eciDictFindNext@20", "ed_dictFindNext", 20);
ALIAS_N("_eciDictFindNextA@24", "ed_dictFindNextA", 24);
ALIAS_N("_eciUpdateDict@20", "ed_updateDict", 20);
ALIAS_N("_eciUpdateDictA@24", "ed_updateDictA", 24);
