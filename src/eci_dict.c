/* User dictionaries: making them, choosing them, and taking them away.

   A dictionary belongs to a language, and an instance may have one in force
   for each language and dialect at once. That is what the table in the
   middle of the instance is: eighteen families of two dialects, each holding
   the dictionary currently active for it, or nothing.

   The table starts at the same offset as the queue the caller may fill,
   which looks alarming until you notice that families are numbered from one,
   so the family-nought slot is never touched by anything here and the queue
   has it to itself.

   Loading a dictionary from a file and saving one to a file were published
   and never written; both answer that they could not.

   Names are prefixed and the aliases at the foot carry the real ones. */

#include <stdint.h>
#include "eci_synththread.h"

typedef struct OldInst OldInst;

#define OI_NEW(h)       (*(void **)((char *)(h) + 0x00c))
#define OI_REFUSEDALL(h) (*(uint32_t *)((char *)(h) + 0x6ac))
#define OI_REFUSED(h)   (*(uint32_t *)((char *)(h) + 0x6b0))

/* The dictionary in force for one language and dialect. Families run from
   one to eighteen and dialects are nought or one. */
#define ACTIVE_DICT(h, family, dialect) \
    (*(void **)((char *)(h) + 0x60c + (family) * 8 + (dialect) * 4))
#define DICT_FAMILIES   0x12
#define DICT_DIALECTS   2

#define ENV_LANGUAGE    9

/* What the older interface calls the answer to a dictionary call. */
#define DICT_OK             0
#define DICT_NO_ROOM        2
#define DICT_NOT_SUPPORTED  6

/* Whether an instance is in the middle of speaking. */
#define SYNTH_BUSY      3

extern int32_t __stdcall eciCheckSynthesizing2(void *h2)
    MANGLED("_eciCheckSynthesizing2@4");
extern int32_t __stdcall eciSynthesize2(void *h2)
    MANGLED("_eciSynthesize2@4");
extern int32_t __stdcall eciSynchronize2(void *h2)
    MANGLED("_eciSynchronize2@4");
extern int32_t __stdcall eciNewDict2(void *h2, int32_t lang, void **out)
    MANGLED("_eciNewDict2@12");
extern int32_t __stdcall eciDeleteDict2(void *h2, void *dict)
    MANGLED("_eciDeleteDict2@8");
extern int32_t __stdcall eciActivateDict2(void *h2, void *dict)
    MANGLED("_eciActivateDict2@8");
extern int32_t __stdcall eciDeactivateDict2(void *h2, void *dict)
    MANGLED("_eciDeactivateDict2@8");
extern int32_t __stdcall eciGetActiveDict2(void *h2, int32_t lang,
                                           void **out)
    MANGLED("_eciGetActiveDict2@12");
extern int32_t __stdcall eciGetDictLanguage2(void *h2, void *dict,
                                             int32_t *lang)
    MANGLED("_eciGetDictLanguage2@12");
extern int32_t __stdcall eciGetParam(OldInst *h, int32_t which)
    MANGLED("_eciGetParam@8");

extern int ev_sendParameters(OldInst *h);

/* ---- turning the engine's answers into the older interface's -------- */

int ed_rc_to_ECIDictError(int32_t rc)
{
    if (rc == -2)
        return DICT_NO_ROOM;
    if (rc >= 0)
        return DICT_OK;
    return DICT_NOT_SUPPORTED;
}

/* ---- the table of what is in force ---------------------------------- */

/* Remember a dictionary as the one in force for its own language. */
int32_t ed_add_active_dict(OldInst *h, void *dict)
{
    int32_t lang;
    int32_t rc = eciGetDictLanguage2(OI_NEW(h), dict, &lang);

    if (rc >= 0)
        ACTIVE_DICT(h, (lang & 0xff0000) >> 16, lang & 0xff) = dict;
    return rc;
}

/* Forget it again, but only if it is still the one recorded there. */
int32_t ed_delete_active_dict(OldInst *h, void *dict)
{
    int32_t lang;
    int32_t rc = eciGetDictLanguage2(OI_NEW(h), dict, &lang);

    if (rc >= 0) {
        int family = (lang & 0xff0000) >> 16;
        int dialect = lang & 0xff;

        if (ACTIVE_DICT(h, family, dialect) == dict)
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
            void *dict = ACTIVE_DICT(h, family, dialect);

            if (dict)
                eciDeactivateDict2(OI_NEW(h), dict);
            ACTIVE_DICT(h, family, dialect) = 0;
        }
    return 0;
}

/* ---- the entry points ----------------------------------------------- */

/* Make an empty dictionary for whatever language is in force.

   Everything queued is spoken and waited for first. A dictionary changes how
   words are pronounced, so anything already on its way has to come out under
   the old rules before the new dictionary can exist. */
void *__stdcall ed_newDict(OldInst *h)
{
    OldInst *inst = h;
    void *dict = 0;
    int32_t rc = -1;
    int32_t lang;

    if (!h)
        return 0;

    if (eciCheckSynthesizing2(OI_NEW(inst)) == SYNTH_BUSY) {
        OI_REFUSED(inst) = 0x2000;
        OI_REFUSEDALL(inst) |= 0x2000;
        return 0;
    }

    ev_sendParameters(inst);
    eciSynthesize2(OI_NEW(inst));
    eciSynchronize2(OI_NEW(inst));

    lang = eciGetParam(h, ENV_LANGUAGE);
    if (lang >= 0)
        rc = eciNewDict2(OI_NEW(inst), lang, &dict);

    return (rc >= 0) ? dict : 0;
}

/* Which dictionary is in force for the language in force. */
void *__stdcall ed_getDict(OldInst *h)
{
    void *dict = 0;
    int32_t rc = -1;
    int32_t lang;

    if (!h)
        return 0;

    lang = eciGetParam(h, ENV_LANGUAGE);
    if (lang >= 0)
        rc = eciGetActiveDict2(OI_NEW(h), lang, &dict);

    return (rc >= 0) ? dict : 0;
}

/* Put one in force, or with nothing named, take all of them out. */
int __stdcall ed_setDict(OldInst *h, void *dict)
{
    int32_t rc = -1;

    if (!h)
        return DICT_NOT_SUPPORTED;

    if (!dict) {
        rc = ed_deactivate_all_dicts(h);
    } else {
        rc = eciActivateDict2(OI_NEW(h), dict);
        if (rc >= 0)
            rc = ed_add_active_dict(h, dict);
    }
    return ed_rc_to_ECIDictError(rc);
}

/* Take one away for good. Answers nought whatever happens. */
int __stdcall ed_deleteDict(OldInst *h, void *dict)
{
    int32_t rc;

    if (!h)
        return 0;

    rc = ed_delete_active_dict(h, dict);
    if (rc >= 0)
        eciDeleteDict2(OI_NEW(h), dict);
    return 0;
}

/* Reading a dictionary from a file and writing one to a file were published
   and never written. */
int __stdcall ed_loadDict(OldInst *h, void *dict, int32_t kind,
                          const char *name)
{
    (void)h;
    (void)dict;
    (void)kind;
    (void)name;
    return DICT_NOT_SUPPORTED;
}

int __stdcall ed_saveDict(OldInst *h, void *dict, int32_t kind,
                          const char *name)
{
    (void)h;
    (void)dict;
    (void)kind;
    (void)name;
    return DICT_NOT_SUPPORTED;
}

ALIAS("?rc_to_ECIDictError@@YA?AW4ECIDictError@@J@Z",
      "ed_rc_to_ECIDictError");
ALIAS("?add_active_dict@@YAJPAUoldECIInstData@@PAX@Z", "ed_add_active_dict");
ALIAS("?delete_active_dict@@YAJPAUoldECIInstData@@PAX@Z",
      "ed_delete_active_dict");
ALIAS("?deactivate_all_dicts@@YAJPAUoldECIInstData@@@Z",
      "ed_deactivate_all_dicts");

#define ALIAS_N(mangled, ours, n) \
    __asm__(".globl \"" mangled "\"\n.set \"" mangled "\", _" ours "@" #n "\n")

ALIAS_N("_eciNewDict@4", "ed_newDict", 4);
ALIAS_N("_eciGetDict@4", "ed_getDict", 4);
ALIAS_N("_eciSetDict@8", "ed_setDict", 8);
ALIAS_N("_eciDeleteDict@8", "ed_deleteDict", 8);
ALIAS_N("_eciLoadDict@16", "ed_loadDict", 16);
ALIAS_N("_eciSaveDict@16", "ed_saveDict", 16);
