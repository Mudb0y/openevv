/* The plain C face of an ECI instance.

   Every entry here takes the handle first and hands the rest straight to the
   object behind it. They exist because the object is C++ and the callers are
   not; there is nothing else to them, and the three that do more are the
   three at the end. */

#include <string.h>
#include <stdint.h>
#include "evv_abi.h"
#include "eci_instance.h"

#define STD  __attribute__((stdcall))

/* Only the last field is read from here: how the constructor went. */

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");
extern THIS ECIinstance *ei_ctor_lang(void *p, int32_t lang)
    MANGLED("??0ECIinstance@@QAE@W4ECILanguageDialect@@@Z");
extern THIS void ei_dtor(void *p) MANGLED("??1ECIinstance@@QAE@XZ");

/* Four numbers the library says about itself: version seven and nothing
   after it. */
const int32_t g_aiVersion[4] = { 7, 0, 0, 0 };

extern THIS int32_t ei_reg_voice(void *self, int32_t a, void *b, void *c)
    MANGLED("?eciRegisterVoice@ECIinstance@@QAEJHPAUECIExtendedVoiceAttrib@@PAX@Z");
extern THIS int32_t ei_act_dict(void *self, void *a)
    MANGLED("?eciActivateDict@ECIinstance@@QAEJPAX@Z");
extern THIS int32_t ei_act_filter_p(void *self, void *a)
    MANGLED("?eciActivateFilter@ECIinstance@@QAEJPAX@Z");
extern THIS int32_t ei_act_filter_n(void *self, int32_t a)
    MANGLED("?eciActivateFilter@ECIinstance@@QAEJJ@Z");
extern THIS int32_t ei_add_text(void *self, void *a, int32_t b, int32_t c, int32_t d, int32_t f)
    MANGLED("?eciAddText@ECIinstance@@QAEJPAXJJJJ@Z");
extern THIS int32_t ei_block(void *self)
    MANGLED("?eciBlock@ECIinstance@@QAEJXZ");
extern THIS int32_t ei_check_synthesizing(void *self)
    MANGLED("?eciCheckSynthesizing@ECIinstance@@QAEJXZ");
extern THIS int32_t ei_clear_errors(void *self)
    MANGLED("?eciClearErrors@ECIinstance@@QAEJXZ");
extern THIS int32_t ei_deact_dict(void *self, void *a)
    MANGLED("?eciDeactivateDict@ECIinstance@@QAEJPAX@Z");
extern THIS int32_t ei_deact_filter_p(void *self, void *a)
    MANGLED("?eciDeactivateFilter@ECIinstance@@QAEJPAX@Z");
extern THIS int32_t ei_deact_filter_n(void *self, int32_t a)
    MANGLED("?eciDeactivateFilter@ECIinstance@@QAEJJ@Z");
extern THIS int32_t ei_del_audio_fmt(void *self)
    MANGLED("?eciDeleteAudioFormat@ECIinstance@@QAEJXZ");
extern THIS int32_t ei_del_dict(void *self, void *a)
    MANGLED("?eciDeleteDict@ECIinstance@@QAEJPAX@Z");
extern THIS int32_t ei_del_filter(void *self, void *a)
    MANGLED("?eciDeleteFilter@ECIinstance@@QAEJPAX@Z");
extern THIS int32_t ei_find_first(void *self, void *a, int32_t b, void **c, int32_t *d, void **f, int32_t *g, int32_t h)
    MANGLED("?eciFindFirstDictEntry@ECIinstance@@QAEJPAXJPAPAXPAJ12J@Z");
extern THIS int32_t ei_find_first_ext(void *self, void *a, int32_t b, void **c, int32_t *d, void **f, int32_t *g, int32_t *h, int32_t i)
    MANGLED("?eciFindFirstDictEntryExt@ECIinstance@@QAEJPAXJPAPAXPAJ12PAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t ei_find_next(void *self, void *a, int32_t b, void **c, int32_t *d, void **f, int32_t *g, int32_t h)
    MANGLED("?eciFindNextDictEntry@ECIinstance@@QAEJPAXJPAPAXPAJ12J@Z");
extern THIS int32_t ei_find_next_ext(void *self, void *a, int32_t b, void **c, int32_t *d, void **f, int32_t *g, int32_t *h, int32_t i)
    MANGLED("?eciFindNextDictEntryExt@ECIinstance@@QAEJPAXJPAPAXPAJ12PAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t ei_get_active_dict(void *self, int32_t a, void **b)
    MANGLED("?eciGetActiveDict@ECIinstance@@QAEJJPAPAX@Z");
extern THIS int32_t ei_get_dict_lang(void *self, void *a, int32_t *b)
    MANGLED("?eciGetDictLanguage@ECIinstance@@QAEJPAXPAJ@Z");
extern THIS int32_t ei_get_param(void *self, int32_t a, int32_t b, int32_t *c)
    MANGLED("?eciGetParam@ECIinstance@@QAEJJJPAJ@Z");
extern THIS int32_t ei_insert_index(void *self, int32_t a)
    MANGLED("?eciInsertIndex@ECIinstance@@QAEJJ@Z");
extern THIS int32_t ei_load_dict_vol(void *self, void *a, int32_t b, const char *c)
    MANGLED("?eciLoadDictVolume@ECIinstance@@QAEJPAXJPBD@Z");
extern THIS int32_t ei_lookup_dict(void *self, void *a, int32_t b, void *c, int32_t d, void **f, int32_t *g, int32_t h)
    MANGLED("?eciLookupDict@ECIinstance@@QAEJPAXJ0JPAPAXPAJJ@Z");
extern THIS int32_t ei_lookup_dict_ext(void *self, void *a, int32_t b, void *c, int32_t d, void **f, int32_t *g, int32_t *h, int32_t i)
    MANGLED("?eciLookupDictExt@ECIinstance@@QAEJPAXJ0JPAPAXPAJPAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t ei_new_audio_fmt(void *self, void *a)
    MANGLED("?eciNewAudioFormat@ECIinstance@@QAEJPAUECIaudioFormat@@@Z");
extern THIS int32_t ei_new_dict(void *self, int32_t a, void **b)
    MANGLED("?eciNewDict@ECIinstance@@QAEJJPAPAX@Z");
extern THIS int32_t ei_new_filter(void *self, int32_t a, int32_t b, void **c)
    MANGLED("?eciNewFilter@ECIinstance@@QAEJJJPAPAX@Z");
extern THIS int32_t ei_pause(void *self, int32_t a)
    MANGLED("?eciPause@ECIinstance@@QAEJJ@Z");
extern THIS int32_t ei_poll(void *self)
    MANGLED("?eciPoll@ECIinstance@@QAEJXZ");
extern THIS int32_t ei_reg_callback(void *self, void *a, void *b, int16_t c, void *d)
    MANGLED("?eciRegisterCallback@ECIinstance@@QAEJP6AJPAXJJ0@Z0F0@Z");
extern THIS int32_t ei_reg_phonemes(void *self, void *a, int32_t b, int32_t c)
    MANGLED("?eciRegisterPhonemeBuffer@ECIinstance@@QAEJPAXJJ@Z");
extern THIS int32_t ei_reg_samples(void *self, int16_t *a, int32_t b, void *c)
    MANGLED("?eciRegisterSampleBuffer@ECIinstance@@QAEJPAFJPAUECIsampleFormat@@@Z");
extern THIS int32_t ei_reset(void *self, int32_t a)
    MANGLED("?eciReset@ECIinstance@@QAEJW4ECILanguageDialect@@@Z");
extern THIS int32_t ei_save_dict_vol(void *self, void *a, int32_t b, const char *c)
    MANGLED("?eciSaveDictVolume@ECIinstance@@QAEJPAXJPBD@Z");
extern THIS int32_t ei_set_param(void *self, int32_t a, int32_t b, int32_t c)
    MANGLED("?eciSetParam@ECIinstance@@QAEJJJJ@Z");
extern THIS int32_t ei_set_std_voice(void *self, int32_t a)
    MANGLED("?eciSetStandardVoice@ECIinstance@@QAEJJ@Z");
extern THIS int32_t ei_stop(void *self)
    MANGLED("?eciStop@ECIinstance@@QAEJXZ");
extern THIS int32_t ei_synchronize(void *self)
    MANGLED("?eciSynchronize@ECIinstance@@QAEJXZ");
extern THIS int32_t ei_synthesize(void *self)
    MANGLED("?eciSynthesize@ECIinstance@@QAEJXZ");
extern THIS int32_t ei_unblock(void *self)
    MANGLED("?eciUnblock@ECIinstance@@QAEJXZ");
extern THIS int32_t ei_unreg_voice(void *self, int32_t a, void *b, void **c)
    MANGLED("?eciUnregisterVoice@ECIinstance@@QAEJHPAUECIVoiceAttrib@@PAPAX@Z");
extern THIS int32_t ei_update_dict(void *self, void *a, int32_t b, void *c, int32_t d, void *f, int32_t g, int32_t h)
    MANGLED("?eciUpdateDict@ECIinstance@@QAEJPAXJ0J0JJ@Z");
extern THIS int32_t ei_update_dict_ext(void *self, void *a, int32_t b, void *c, int32_t d, void *f, int32_t g, int32_t h, int32_t i)
    MANGLED("?eciUpdateDictExt@ECIinstance@@QAEJPAXJ0J0JW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t ei_update_filter(void *self, void *a, void *b, int32_t c, void *d, int32_t f, int32_t g)
    MANGLED("?eciUpdateFilter@ECIinstance@@QAEJPAX0J0JJ@Z");
extern THIS int32_t ei_is_filter_active(void *self, uint32_t a)
    MANGLED("?eciIsFilterActive@ECIinstance@@QAEHI@Z");
extern THIS void * ei_get_filter_mngr(void *self)
    MANGLED("?eciGetFilterMngr@ECIinstance@@QAEPAXXZ");
extern THIS void * ei_get_rom_mngr(void *self)
    MANGLED("?eciGetRomMngr@ECIinstance@@QAEPAXXZ");
extern THIS void ei_get_avail_filters(void *self, int32_t a, uint32_t *b, uint32_t *c)
    MANGLED("?eciGetAvailableFilters@ECIinstance@@QAEXJPAI0@Z");
extern THIS void ei_get_filter_desc(void *self, int32_t a, uint32_t b, char *c)
    MANGLED("?eciGetFilterDescription@ECIinstance@@QAEXJIPAD@Z");

/* What the original answers when it is handed nowhere to put the answer. */
#define ECI_NO_ROOM (-3)
#define ECI_FAILED  (-2)

/* Make one, and take it apart again if it could not settle on a voice. */
STD int32_t api_new(void **out, int32_t lang)
{
    int32_t rc = ECI_NO_ROOM;
    void *raw;

    if (out == 0)
        return rc;
    raw = cpp_new(sizeof(ECIinstance));
    *out = raw ? (void *)ei_ctor_lang(raw, lang) : 0;
    if (*out == 0)
        return ECI_FAILED;
    rc = ((ECIinstance *)*out)->error;
    if (rc != 0) {
        ei_dtor(*out);
        cpp_delete(*out);
        *out = 0;
    }
    return rc;
}

STD int32_t api_delete(void *self)
{
    if (self == 0)
        return ECI_NO_ROOM;
    ei_dtor(self);
    cpp_delete(self);
    return 0;
}

/* Any of the four left out is refused rather than skipped. */
STD int32_t api_version(int32_t *a, int32_t *b, int32_t *c, int32_t *d)
{
    if (a == 0)
        return ECI_NO_ROOM;
    *a = g_aiVersion[0];
    if (b == 0)
        return ECI_NO_ROOM;
    *b = g_aiVersion[1];
    if (c == 0)
        return ECI_NO_ROOM;
    *c = g_aiVersion[2];
    if (d == 0)
        return ECI_NO_ROOM;
    *d = g_aiVersion[3];
    return 0;
}

STD int32_t api_activate_dict(void *self, void *a)
{
    return ei_act_dict(self, a);
}

STD int32_t api_activate_filter(void *self, void *a)
{
    return ei_act_filter_p(self, a);
}

STD int32_t api_activate_filter_ex(void *self, int32_t a)
{
    return ei_act_filter_n(self, a);
}

STD int32_t api_add_text(void *self, void *a, int32_t b, int32_t c, int32_t d, int32_t f)
{
    return ei_add_text(self, a, b, c, d, f);
}

STD int32_t api_block(void *self)
{
    return ei_block(self);
}

STD int32_t api_check_synth(void *self)
{
    return ei_check_synthesizing(self);
}

STD int32_t api_clear_errors(void *self)
{
    return ei_clear_errors(self);
}

STD int32_t api_deactivate_dict(void *self, void *a)
{
    return ei_deact_dict(self, a);
}

STD int32_t api_deactivate_filter(void *self, void *a)
{
    return ei_deact_filter_p(self, a);
}

STD int32_t api_deactivate_filter_ex(void *self, int32_t a)
{
    return ei_deact_filter_n(self, a);
}

STD int32_t api_delete_audio_format(void *self)
{
    return ei_del_audio_fmt(self);
}

STD int32_t api_delete_dict(void *self, void *a)
{
    return ei_del_dict(self, a);
}

STD int32_t api_delete_filter(void *self, void *a)
{
    return ei_del_filter(self, a);
}

STD int32_t api_find_first(void *self, void *a, int32_t b, void **c, int32_t *d, void **f, int32_t *g, int32_t h)
{
    return ei_find_first(self, a, b, c, d, f, g, h);
}

STD int32_t api_find_first_ext(void *self, void *a, int32_t b, void **c, int32_t *d, void **f, int32_t *g, int32_t *h, int32_t i)
{
    return ei_find_first_ext(self, a, b, c, d, f, g, h, i);
}

STD int32_t api_find_next(void *self, void *a, int32_t b, void **c, int32_t *d, void **f, int32_t *g, int32_t h)
{
    return ei_find_next(self, a, b, c, d, f, g, h);
}

STD int32_t api_find_next_ext(void *self, void *a, int32_t b, void **c, int32_t *d, void **f, int32_t *g, int32_t *h, int32_t i)
{
    return ei_find_next_ext(self, a, b, c, d, f, g, h, i);
}

STD int32_t api_get_active_dict(void *self, int32_t a, void **b)
{
    return ei_get_active_dict(self, a, b);
}

STD int32_t api_get_dict_language(void *self, void *a, int32_t *b)
{
    return ei_get_dict_lang(self, a, b);
}

STD int32_t api_get_param(void *self, int32_t a, int32_t b, int32_t *c)
{
    return ei_get_param(self, a, b, c);
}

STD int32_t api_insert_index(void *self, int32_t a)
{
    return ei_insert_index(self, a);
}

STD int32_t api_load_dict_volume(void *self, void *a, int32_t b, const char *c)
{
    return ei_load_dict_vol(self, a, b, c);
}

STD int32_t api_lookup_dict(void *self, void *a, int32_t b, void *c, int32_t d, void **f, int32_t *g, int32_t h)
{
    return ei_lookup_dict(self, a, b, c, d, f, g, h);
}

STD int32_t api_lookup_dict_ext(void *self, void *a, int32_t b, void *c, int32_t d, void **f, int32_t *g, int32_t *h, int32_t i)
{
    return ei_lookup_dict_ext(self, a, b, c, d, f, g, h, i);
}

STD int32_t api_new_audio_format(void *self, void *a)
{
    return ei_new_audio_fmt(self, a);
}

STD int32_t api_new_dict(void *self, int32_t a, void **b)
{
    return ei_new_dict(self, a, b);
}

STD int32_t api_new_filter(void *self, int32_t a, int32_t b, void **c)
{
    return ei_new_filter(self, a, b, c);
}

STD int32_t api_pause(void *self, int32_t a)
{
    return ei_pause(self, a);
}

STD int32_t api_poll(void *self)
{
    return ei_poll(self);
}

STD int32_t api_register_callback(void *self, void *a, void *b, int16_t c, void *d)
{
    return ei_reg_callback(self, a, b, c, d);
}

STD int32_t api_register_phonemes(void *self, void *a, int32_t b, int32_t c)
{
    return ei_reg_phonemes(self, a, b, c);
}

STD int32_t api_register_samples(void *self, int16_t *a, int32_t b, void *c)
{
    return ei_reg_samples(self, a, b, c);
}

STD int32_t api_reset(void *self, int32_t a)
{
    return ei_reset(self, a);
}

STD int32_t api_save_dict_volume(void *self, void *a, int32_t b, const char *c)
{
    return ei_save_dict_vol(self, a, b, c);
}

STD int32_t api_set_param(void *self, int32_t a, int32_t b, int32_t c)
{
    return ei_set_param(self, a, b, c);
}

STD int32_t api_set_standard_voice(void *self, int32_t a)
{
    return ei_set_std_voice(self, a);
}

STD int32_t api_stop(void *self)
{
    return ei_stop(self);
}

STD int32_t api_synchronize(void *self)
{
    return ei_synchronize(self);
}

STD int32_t api_synthesize(void *self)
{
    return ei_synthesize(self);
}

STD int32_t api_unblock(void *self)
{
    return ei_unblock(self);
}

STD int32_t api_unregister_voice(void *self, int32_t a, void *b, void **c)
{
    return ei_unreg_voice(self, a, b, c);
}

STD int32_t api_update_dict(void *self, void *a, int32_t b, void *c, int32_t d, void *f, int32_t g, int32_t h)
{
    return ei_update_dict(self, a, b, c, d, f, g, h);
}

STD int32_t api_update_dict_ext(void *self, void *a, int32_t b, void *c, int32_t d, void *f, int32_t g, int32_t h, int32_t i)
{
    return ei_update_dict_ext(self, a, b, c, d, f, g, h, i);
}

STD int32_t api_update_filter(void *self, void *a, void *b, int32_t c, void *d, int32_t f, int32_t g)
{
    return ei_update_filter(self, a, b, c, d, f, g);
}

STD int32_t api_is_filter_active(void *self, uint32_t a)
{
    return ei_is_filter_active(self, a);
}

__attribute__((cdecl)) void *api_get_filter_mngr(void *self)
{
    return ei_get_filter_mngr(self);
}

__attribute__((cdecl)) void *api_get_rom_mngr(void *self)
{
    return ei_get_rom_mngr(self);
}

STD void api_get_available_filters(void *self, int32_t a, uint32_t *b, uint32_t *c)
{
    ei_get_avail_filters(self, a, b, c);
}

STD void api_get_filter_description(void *self, int32_t a, uint32_t b, char *c)
{
    ei_get_filter_desc(self, a, b, c);
}

/* Say a whole string with a voice made for the purpose, at whichever of
   three sample rates the machine will give. Nothing in the engine uses
   this; it is here for a caller that wants one line of speech and no
   bookkeeping. */
STD int32_t api_speak_text(void *text, int32_t b, int32_t c, int32_t d)
{
    struct { int32_t a, rate, e; const char *name;
             int32_t f, g, h, i; } fmt;
    void *self = 0;
    int32_t rc = api_new(&self, 0);

    if (rc < 0)
        return rc;

    memset(&fmt, 0, sizeof fmt);
    fmt.a = 0;
    fmt.rate = 0;
    fmt.e = 0;
    fmt.name = "0";
    fmt.f = 10;
    fmt.g = 0x898;
    fmt.h = 0;
    fmt.i = 0x898;

    rc = api_new_audio_format(self, &fmt);
    if (rc < 0) {
        fmt.rate = 0x2b11;
        rc = api_new_audio_format(self, &fmt);
    }
    if (rc < 0) {
        fmt.rate = 0x1f40;
        rc = api_new_audio_format(self, &fmt);
    }
    if (rc >= 0)
        rc = api_add_text(self, text, b, c, d, 0);
    if (rc >= 0 || rc == -14) {
        api_synthesize(self);
        api_synchronize(self);
    }
    api_delete(self);
    return rc;
}

/* The one wrapper that does not pass its arguments straight through: the
   last two go over in the other order. */
STD int32_t api_register_voice(void *self, int32_t a, void *b, void *c)
{
    return ei_reg_voice(self, a, c, b);
}

ALIAS_N("_eciRegisterVoice2@16", "api_register_voice", 16);
ALIAS_N("_eciNew2@8", "api_new", 8);
ALIAS_N("_eciDelete2@4", "api_delete", 4);
ALIAS_N("_eciVersion2@16", "api_version", 16);
ALIAS_N("_eciSpeakText2@16", "api_speak_text", 16);
ALIAS_N("_eciActivateDict2@8", "api_activate_dict", 8);
ALIAS_N("_eciActivateFilter2@8", "api_activate_filter", 8);
ALIAS_N("_eciActivateFilterEx2@8", "api_activate_filter_ex", 8);
ALIAS_N("_eciAddText2@24", "api_add_text", 24);
ALIAS_N("_eciBlock2@4", "api_block", 4);
ALIAS_N("_eciCheckSynthesizing2@4", "api_check_synth", 4);
ALIAS_N("_eciClearErrors2@4", "api_clear_errors", 4);
ALIAS_N("_eciDeactivateDict2@8", "api_deactivate_dict", 8);
ALIAS_N("_eciDeactivateFilter2@8", "api_deactivate_filter", 8);
ALIAS_N("_eciDeactivateFilterEx2@8", "api_deactivate_filter_ex", 8);
ALIAS_N("_eciDeleteAudioFormat2@4", "api_delete_audio_format", 4);
ALIAS_N("_eciDeleteDict2@8", "api_delete_dict", 8);
ALIAS_N("_eciDeleteFilter2@8", "api_delete_filter", 8);
ALIAS_N("_eciFindFirstDictEntry2@32", "api_find_first", 32);
ALIAS_N("_eciFindFirstDictEntryExt2@36", "api_find_first_ext", 36);
ALIAS_N("_eciFindNextDictEntry2@32", "api_find_next", 32);
ALIAS_N("_eciFindNextDictEntryExt2@36", "api_find_next_ext", 36);
ALIAS_N("_eciGetActiveDict2@12", "api_get_active_dict", 12);
ALIAS_N("_eciGetDictLanguage2@12", "api_get_dict_language", 12);
ALIAS_N("_eciGetParam2@16", "api_get_param", 16);
ALIAS_N("_eciInsertIndex2@8", "api_insert_index", 8);
ALIAS_N("_eciLoadDictVolume2@16", "api_load_dict_volume", 16);
ALIAS_N("_eciLookupDict2@32", "api_lookup_dict", 32);
ALIAS_N("_eciLookupDictExt2@36", "api_lookup_dict_ext", 36);
ALIAS_N("_eciNewAudioFormat2@8", "api_new_audio_format", 8);
ALIAS_N("_eciNewDict2@12", "api_new_dict", 12);
ALIAS_N("_eciNewFilter2@16", "api_new_filter", 16);
ALIAS_N("_eciPause2@8", "api_pause", 8);
ALIAS_N("_eciPoll2@4", "api_poll", 4);
ALIAS_N("_eciRegisterCallback2@20", "api_register_callback", 20);
ALIAS_N("_eciRegisterPhonemeBuffer2@16", "api_register_phonemes", 16);
ALIAS_N("_eciRegisterSampleBuffer2@16", "api_register_samples", 16);
ALIAS_N("_eciReset2@8", "api_reset", 8);
ALIAS_N("_eciSaveDictVolume2@16", "api_save_dict_volume", 16);
ALIAS_N("_eciSetParam2@16", "api_set_param", 16);
ALIAS_N("_eciSetStandardVoice2@8", "api_set_standard_voice", 8);
ALIAS_N("_eciStop2@4", "api_stop", 4);
ALIAS_N("_eciSynchronize2@4", "api_synchronize", 4);
ALIAS_N("_eciSynthesize2@4", "api_synthesize", 4);
ALIAS_N("_eciUnblock2@4", "api_unblock", 4);
ALIAS_N("_eciUnregisterVoice2@16", "api_unregister_voice", 16);
ALIAS_N("_eciUpdateDict2@32", "api_update_dict", 32);
ALIAS_N("_eciUpdateDictExt2@36", "api_update_dict_ext", 36);
ALIAS_N("_eciUpdateFilter2@28", "api_update_filter", 28);
ALIAS_N("_eciIsFilterActive2@8", "api_is_filter_active", 8);
ALIAS("_eciGetFilterMngr2", "api_get_filter_mngr");
ALIAS("_eciGetRomMngr2", "api_get_rom_mngr");
ALIAS_N("_eciGetAvailableFilters2@16", "api_get_available_filters", 16);
ALIAS_N("_eciGetFilterDescription2@16", "api_get_filter_description", 16);
