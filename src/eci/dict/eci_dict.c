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
#define DICT_NOT_SUPPORTED  6

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

/* ---- the entry points ----------------------------------------------- */

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
