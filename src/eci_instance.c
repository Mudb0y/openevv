/* The object behind an ECI handle.

   It owns four things and does almost nothing itself: the synthesis thread,
   the queue of messages coming back from it, the parameters, and the filter
   that text passes through on the way in. Nearly every entry point below
   hands its arguments straight to one of them, which is why they read as
   they do — the object is a facade, and a facade with any cleverness in it
   would be the wrong shape.

   Written to the original's layout, because the thread and the queue are
   still the original's and they are reached at fixed offsets inside it. */

#include <string.h>
#include <stdint.h>
#include "evv_abi.h"
#include "eci_synththread.h"
#include "eci_instance.h"

/* A thread is thrown away through the first slot of its table, with one
   meaning also give back the memory. */
typedef struct { THIS void *(*destroy)(void *self, int32_t free_it); } ThreadVtbl;

extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");

extern THIS void *aq_ctor(void *q) MANGLED("??0ETIappMessageQueue@@QAE@XZ");
extern THIS void  aq_dtor(void *q) MANGLED("??1ETIappMessageQueue@@QAE@XZ");
extern THIS void *tf_ctor(void *f) MANGLED("??0TextFilter@@QAE@XZ");
extern THIS void  tf_dtor(void *f) MANGLED("??1TextFilter@@QAE@XZ");
extern THIS void *sy_mutexCtor(void *m, int32_t kind) MANGLED("??0Mutex@@QAE@H@Z");
extern THIS void  sy_mutexDtor(void *m) MANGLED("??1Mutex@@QAE@XZ");
extern void cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");

extern THIS void *stl_ctor(void *t, void *queue, ECIstate *s)
    MANGLED("??0SynthThread@@QAE@PAVETIappMessageQueue@@PAVECIstate@@@Z");
extern THIS void *stl_ctorWithLanguage(void *t, void *queue, ECIstate *s,
                                   int32_t lang)
    MANGLED("??0SynthThread@@QAE@PAVETIappMessageQueue@@PAVECIstate@@W4ECILanguageDialect@@@Z");

extern THIS int32_t es_setInitialState(ECIstate *s, void *t, int32_t lang)
    MANGLED("?setInitialState@ECIstate@@QAEJPAVSynthThread@@W4ECILanguageDialect@@@Z");
extern THIS int32_t es_setParam(ECIstate *s, int32_t k, int32_t p, int32_t v,
                                void *t, int32_t extra)
    MANGLED("?setParam@ECIstate@@QAEJJJJPAVSynthThread@@J@Z");
extern THIS int32_t es_getParam(ECIstate *s, int32_t k, int32_t p, int32_t *o)
    MANGLED("?getParam@ECIstate@@QAEJJJPAJ@Z");
extern THIS int32_t es_setStandardVoice(ECIstate *s, int32_t v, void *t)
    MANGLED("?setStandardVoice@ECIstate@@QAEJJPAVSynthThread@@@Z");

extern THIS int32_t tf_addText(void *f, void *a, int32_t b, int32_t c,
                               int32_t d, int32_t e, ECIstate *s, void *t)
    MANGLED("?addText@TextFilter@@QAEJPAXJJJJPAVECIstate@@PAVSynthThread@@@Z");

extern THIS int32_t stw_checkSynthesizing(void *t)
    MANGLED("?checkSynthesizing@SynthThread@@QAEHXZ");
extern THIS int32_t stw_pause(void *t, int32_t on)
    MANGLED("?pause@SynthThread@@QAEJH@Z");
extern THIS int32_t stf_registerPhonemeBuffer(void *t, void *b, int32_t n)
    MANGLED("?registerPhonemeBuffer@SynthThread@@QAEJPAXJ@Z");
extern THIS int32_t stw_registerCallback(void *t, void *inst, void *cb,
                                        void *a, int16_t b, void *c)
    MANGLED("?registerCallback@SynthThread@@QAEJPAXP6AJ0JJ0@Z0F0@Z");

extern THIS int32_t std_activateDict(void *t, void *a)
    MANGLED("?activateDict@SynthThread@@QAEJPAX@Z");
extern THIS int32_t stm_activateFilterById(void *t, int32_t a)
    MANGLED("?activateFilter@SynthThread@@QAEJK@Z");
extern THIS int32_t stm_activateFilter(void *t, void *a)
    MANGLED("?activateFilter@SynthThread@@QAEJPAX@Z");
extern THIS int32_t st_block(void *t)
    MANGLED("?block@SynthThread@@QAEJXZ");
extern THIS int32_t stw_clearErrors(void *t)
    MANGLED("?clearErrors@SynthThread@@QAEJXZ");
extern THIS int32_t std_deactivateDict(void *t, void *a)
    MANGLED("?deactivateDict@SynthThread@@QAEJPAX@Z");
extern THIS int32_t stm_deactivateFilterById(void *t, int32_t a)
    MANGLED("?deactivateFilter@SynthThread@@QAEJK@Z");
extern THIS int32_t stm_deactivateFilter(void *t, void *a)
    MANGLED("?deactivateFilter@SynthThread@@QAEJPAX@Z");
extern THIS int32_t stf_deleteAudioFormat(void *t)
    MANGLED("?deleteAudioFormat@SynthThread@@QAEJXZ");
extern THIS int32_t std_deleteDict(void *t, void *a)
    MANGLED("?deleteDict@SynthThread@@QAEJPAX@Z");
extern THIS int32_t stm_deleteFilter(void *t, void *a)
    MANGLED("?deleteFilter@SynthThread@@QAEJPAX@Z");
extern THIS int32_t std_findFirstDictEntry(void *t, void *a, int32_t b, void **c, int32_t *d, void **e, int32_t *f, int32_t g)
    MANGLED("?findFirstDictEntry@SynthThread@@QAEJPAXJPAPAXPAJ12J@Z");
extern THIS int32_t std_findFirstDictEntryExt(void *t, void *a, int32_t b, void **c, int32_t *d, void **e, int32_t *f, int32_t *g, int32_t h)
    MANGLED("?findFirstDictEntryExt@SynthThread@@QAEJPAXJPAPAXPAJ12PAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t std_findNextDictEntry(void *t, void *a, int32_t b, void **c, int32_t *d, void **e, int32_t *f, int32_t g)
    MANGLED("?findNextDictEntry@SynthThread@@QAEJPAXJPAPAXPAJ12J@Z");
extern THIS int32_t std_findNextDictEntryExt(void *t, void *a, int32_t b, void **c, int32_t *d, void **e, int32_t *f, int32_t *g, int32_t h)
    MANGLED("?findNextDictEntryExt@SynthThread@@QAEJPAXJPAPAXPAJ12PAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t std_getActiveDict(void *t, int32_t a, void **b)
    MANGLED("?getActiveDict@SynthThread@@QAEJJPAPAX@Z");
extern THIS int32_t std_getDictLanguage(void *t, void *a, int32_t *b)
    MANGLED("?getDictLanguage@SynthThread@@QAEJPAXPAJ@Z");
extern THIS void *stw_getFilterMngr(void *t)
    MANGLED("?getFilterMngr@SynthThread@@QAEPAXXZ");
extern THIS void *stw_getRomMngr(void *t)
    MANGLED("?getRomMngr@SynthThread@@QAEPAXXZ");
extern THIS int32_t st_insertIndex(void *t, int32_t a)
    MANGLED("?insertIndex@SynthThread@@QAEJJ@Z");
extern THIS int32_t stm_isFilterActive(void *t, uint32_t a)
    MANGLED("?isFilterActive@SynthThread@@QAEHI@Z");
extern THIS int32_t std_loadDictVolume(void *t, void *a, int32_t b, const char *c)
    MANGLED("?loadDictVolume@SynthThread@@QAEJPAXJPBD@Z");
extern THIS int32_t std_lookupDict(void *t, void *a, int32_t b, void *c, int32_t d, void **e, int32_t *f, int32_t g)
    MANGLED("?lookupDict@SynthThread@@QAEJPAXJ0JPAPAXPAJJ@Z");
extern THIS int32_t std_lookupDictExt(void *t, void *a, int32_t b, void *c, int32_t d, void **e, int32_t *f, int32_t *g, int32_t h)
    MANGLED("?lookupDictExt@SynthThread@@QAEJPAXJ0JPAPAXPAJPAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t stf_newAudioFormat(void *t, void *a)
    MANGLED("?newAudioFormat@SynthThread@@QAEJPAUECIaudioFormat@@@Z");
extern THIS int32_t std_newDict(void *t, int32_t a, void **b)
    MANGLED("?newDict@SynthThread@@QAEJJPAPAX@Z");
extern THIS int32_t stm_newFilter(void *t, int32_t a, int32_t b, void **c)
    MANGLED("?newFilter@SynthThread@@QAEJJJPAPAX@Z");
extern THIS int32_t stw_poll(void *t)
    MANGLED("?poll@SynthThread@@QAEJXZ");
extern THIS int32_t stf_registerSampleBuffer(void *t, int16_t *a, int32_t b, void *c)
    MANGLED("?registerSampleBuffer@SynthThread@@QAEJPAFJPAUECIsampleFormat@@@Z");
extern THIS int32_t stm_registerVoice(void *t, int32_t a, void *b, void *c)
    MANGLED("?registerVoice@SynthThread@@QAEJHPAUECIExtendedVoiceAttrib@@PAX@Z");
extern THIS int32_t std_saveDictVolume(void *t, void *a, int32_t b, const char *c)
    MANGLED("?saveDictVolume@SynthThread@@QAEJPAXJPBD@Z");
extern THIS int32_t stl_stop(void *t)
    MANGLED("?stop@SynthThread@@QAEJXZ");
extern THIS int32_t stw_synchronize(void *t)
    MANGLED("?synchronize@SynthThread@@QAEJXZ");
extern THIS int32_t st_synthesize(void *t)
    MANGLED("?synthesize@SynthThread@@QAEJXZ");
extern THIS int32_t stw_unblock(void *t)
    MANGLED("?unblock@SynthThread@@QAEJXZ");
extern THIS int32_t stm_unregisterVoice(void *t, int32_t a, void *b, void **c)
    MANGLED("?unregisterVoice@SynthThread@@QAEJHPAUECIVoiceAttrib@@PAPAX@Z");
extern THIS int32_t std_updateDict(void *t, void *a, int32_t b, void *c, int32_t d, void *e, int32_t f, int32_t g)
    MANGLED("?updateDict@SynthThread@@QAEJPAXJ0J0JJ@Z");
extern THIS int32_t std_updateDictExt(void *t, void *a, int32_t b, void *c, int32_t d, void *e, int32_t f, int32_t g, int32_t h)
    MANGLED("?updateDictExt@SynthThread@@QAEJPAXJ0J0JW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z");
extern THIS int32_t stm_updateFilter(void *t, void *a, void *b, int32_t c, void *d, int32_t e, int32_t f)
    MANGLED("?updateFilter@SynthThread@@QAEJPAX0J0JJ@Z");
extern THIS void stm_getAvailableFilters(void *t, int32_t a, uint32_t *b, uint32_t *c)
    MANGLED("?getAvailableFilters@SynthThread@@QAEXJPAI0@Z");
extern THIS void stm_getFilterDescription(void *t, int32_t a, uint32_t b, char *c)
    MANGLED("?getFilterDescription@SynthThread@@QAEXJIPAD@Z");

/* ---- making one and taking it apart ------------------------------------ */

THIS ECIstate *es_ctor(ECIstate *s)
{
    int i;

    s->spr = 0;
    s->lang = 0;
    sy_mutexCtor(s->mutex, 0);
    memset(s, 0, 4);
    for (i = 0; i < 20; i++)
        s->param[i] = 0;
    return s;
}

THIS void es_dtor(ECIstate *s)
{
    if (s->lang != 0) {
        cpp_delete(s->lang);
        s->lang = 0;
    }
    memset(s, 0, 4);
    sy_mutexDtor(s->mutex);
}

/* Without a language the thread picks its own, and only then is it asked
   for the state; with one, both are told at once. */
THIS ECIinstance *ei_ctor(ECIinstance *self)
{
    void *raw;

    aq_ctor(&self->queue);
    es_ctor(&self->state);
    tf_ctor(&self->filter);
    raw = cpp_new(sizeof(SynthThread));
    self->thread = raw ? stl_ctor(raw, &self->queue, &self->state) : 0;
    if (self->thread == 0) {
        self->error = -2;
        return self;
    }
    self->error = ST_STATUS((SynthThread *)self->thread);
    if (self->error == 0)
        self->error = es_setInitialState(&self->state, self->thread, 0);
    return self;
}

THIS ECIinstance *ei_ctor_lang(ECIinstance *self, int32_t lang)
{
    void *raw;

    aq_ctor(&self->queue);
    es_ctor(&self->state);
    tf_ctor(&self->filter);
    raw = cpp_new(sizeof(SynthThread));
    self->thread = raw ? stl_ctorWithLanguage(raw, &self->queue, &self->state, lang) : 0;
    if (self->thread == 0) {
        self->error = -2;
        return self;
    }
    self->error = es_setInitialState(&self->state, self->thread, lang);
    return self;
}

THIS void ei_dtor(ECIinstance *self)
{
    if (self->thread != 0) {
        (*(ThreadVtbl *const *)self->thread)->destroy(self->thread, 1);
        self->thread = 0;
    }
    tf_dtor(&self->filter);
    es_dtor(&self->state);
    aq_dtor(&self->queue);
}

/* ---- the ones that are not plain forwarding ---------------------------- */

/* Two means idle, three means still going. */
THIS int32_t ei_check_synthesizing(ECIinstance *self)
{
    return stw_checkSynthesizing(self->thread) ? 3 : 2;
}

THIS int32_t ei_pause(ECIinstance *self, int32_t on)
{
    return stw_pause(self->thread, on != 0);
}

/* The third argument is the caller's idea of a size and the thread has no
   use for it. */
THIS int32_t ei_reg_phonemes(ECIinstance *self, void *b, int32_t n, int32_t unused)
{
    (void)unused;
    return stf_registerPhonemeBuffer(self->thread, b, n);
}

/* The thread is told which instance to name when it calls back. */
THIS int32_t ei_reg_callback(ECIinstance *self, void *cb, void *a, int16_t b,
                             void *c)
{
    return stw_registerCallback(self->thread, self, cb, a, b, c);
}

THIS int32_t ei_add_text(ECIinstance *self, void *a, int32_t b, int32_t c,
                         int32_t d, int32_t f)
{
    return tf_addText(&self->filter, a, b, c, d, f, &self->state,
                      self->thread);
}

THIS int32_t ei_set_param(ECIinstance *self, int32_t k, int32_t p, int32_t v)
{
    return es_setParam(&self->state, k, p, v, self->thread, 0);
}

THIS int32_t ei_get_param(ECIinstance *self, int32_t k, int32_t p, int32_t *o)
{
    return es_getParam(&self->state, k, p, o);
}

THIS int32_t ei_set_std_voice(ECIinstance *self, int32_t v)
{
    return es_setStandardVoice(&self->state, v, self->thread);
}

/* Stopping first, so that the state is set on a thread that has nothing in
   hand. */
THIS int32_t ei_reset(ECIinstance *self, int32_t lang)
{
    int32_t rc = stl_stop(self->thread);

    if (rc == 0)
        rc = es_setInitialState(&self->state, self->thread, lang);
    return rc;
}

/* ---- the ones that are ------------------------------------------------- */

THIS int32_t ei_act_dict(ECIinstance *self, void *a)
{
    return std_activateDict(self->thread, a);
}

THIS int32_t ei_act_filter_n(ECIinstance *self, int32_t a)
{
    return stm_activateFilterById(self->thread, a);
}

THIS int32_t ei_act_filter_p(ECIinstance *self, void *a)
{
    return stm_activateFilter(self->thread, a);
}

THIS int32_t ei_block(ECIinstance *self)
{
    return st_block(self->thread);
}

THIS int32_t ei_clear_errors(ECIinstance *self)
{
    return stw_clearErrors(self->thread);
}

THIS int32_t ei_deact_dict(ECIinstance *self, void *a)
{
    return std_deactivateDict(self->thread, a);
}

THIS int32_t ei_deact_filter_n(ECIinstance *self, int32_t a)
{
    return stm_deactivateFilterById(self->thread, a);
}

THIS int32_t ei_deact_filter_p(ECIinstance *self, void *a)
{
    return stm_deactivateFilter(self->thread, a);
}

THIS int32_t ei_del_audio_fmt(ECIinstance *self)
{
    return stf_deleteAudioFormat(self->thread);
}

THIS int32_t ei_del_dict(ECIinstance *self, void *a)
{
    return std_deleteDict(self->thread, a);
}

THIS int32_t ei_del_filter(ECIinstance *self, void *a)
{
    return stm_deleteFilter(self->thread, a);
}

THIS int32_t ei_find_first(ECIinstance *self, void *a, int32_t b, void **c, int32_t *d, void **ee, int32_t *f, int32_t g)
{
    return std_findFirstDictEntry(self->thread, a, b, c, d, ee, f, g);
}

THIS int32_t ei_find_first_ext(ECIinstance *self, void *a, int32_t b, void **c, int32_t *d, void **ee, int32_t *f, int32_t *g, int32_t h)
{
    return std_findFirstDictEntryExt(self->thread, a, b, c, d, ee, f, g, h);
}

THIS int32_t ei_find_next(ECIinstance *self, void *a, int32_t b, void **c, int32_t *d, void **ee, int32_t *f, int32_t g)
{
    return std_findNextDictEntry(self->thread, a, b, c, d, ee, f, g);
}

THIS int32_t ei_find_next_ext(ECIinstance *self, void *a, int32_t b, void **c, int32_t *d, void **ee, int32_t *f, int32_t *g, int32_t h)
{
    return std_findNextDictEntryExt(self->thread, a, b, c, d, ee, f, g, h);
}

THIS int32_t ei_get_active_dict(ECIinstance *self, int32_t a, void **b)
{
    return std_getActiveDict(self->thread, a, b);
}

THIS int32_t ei_get_dict_lang(ECIinstance *self, void *a, int32_t *b)
{
    return std_getDictLanguage(self->thread, a, b);
}

THIS void *ei_get_filter_mngr(ECIinstance *self)
{
    return stw_getFilterMngr(self->thread);
}

THIS void *ei_get_rom_mngr(ECIinstance *self)
{
    return stw_getRomMngr(self->thread);
}

THIS int32_t ei_insert_index(ECIinstance *self, int32_t a)
{
    return st_insertIndex(self->thread, a);
}

THIS int32_t ei_is_filter_active(ECIinstance *self, uint32_t a)
{
    return stm_isFilterActive(self->thread, a);
}

THIS int32_t ei_load_dict_vol(ECIinstance *self, void *a, int32_t b, const char *c)
{
    return std_loadDictVolume(self->thread, a, b, c);
}

THIS int32_t ei_lookup_dict(ECIinstance *self, void *a, int32_t b, void *c, int32_t d, void **ee, int32_t *f, int32_t g)
{
    return std_lookupDict(self->thread, a, b, c, d, ee, f, g);
}

THIS int32_t ei_lookup_dict_ext(ECIinstance *self, void *a, int32_t b, void *c, int32_t d, void **ee, int32_t *f, int32_t *g, int32_t h)
{
    return std_lookupDictExt(self->thread, a, b, c, d, ee, f, g, h);
}

THIS int32_t ei_new_audio_fmt(ECIinstance *self, void *a)
{
    return stf_newAudioFormat(self->thread, a);
}

THIS int32_t ei_new_dict(ECIinstance *self, int32_t a, void **b)
{
    return std_newDict(self->thread, a, b);
}

THIS int32_t ei_new_filter(ECIinstance *self, int32_t a, int32_t b, void **c)
{
    return stm_newFilter(self->thread, a, b, c);
}

THIS int32_t ei_poll(ECIinstance *self)
{
    return stw_poll(self->thread);
}

THIS int32_t ei_reg_samples(ECIinstance *self, int16_t *a, int32_t b, void *c)
{
    return stf_registerSampleBuffer(self->thread, a, b, c);
}

THIS int32_t ei_reg_voice(ECIinstance *self, int32_t a, void *b, void *c)
{
    return stm_registerVoice(self->thread, a, b, c);
}

THIS int32_t ei_save_dict_vol(ECIinstance *self, void *a, int32_t b, const char *c)
{
    return std_saveDictVolume(self->thread, a, b, c);
}

THIS int32_t ei_stop(ECIinstance *self)
{
    return stl_stop(self->thread);
}

THIS int32_t ei_synchronize(ECIinstance *self)
{
    return stw_synchronize(self->thread);
}

THIS int32_t ei_synthesize(ECIinstance *self)
{
    return st_synthesize(self->thread);
}

THIS int32_t ei_unblock(ECIinstance *self)
{
    return stw_unblock(self->thread);
}

THIS int32_t ei_unreg_voice(ECIinstance *self, int32_t a, void *b, void **c)
{
    return stm_unregisterVoice(self->thread, a, b, c);
}

THIS int32_t ei_update_dict(ECIinstance *self, void *a, int32_t b, void *c, int32_t d, void *ee, int32_t f, int32_t g)
{
    return std_updateDict(self->thread, a, b, c, d, ee, f, g);
}

THIS int32_t ei_update_dict_ext(ECIinstance *self, void *a, int32_t b, void *c, int32_t d, void *ee, int32_t f, int32_t g, int32_t h)
{
    return std_updateDictExt(self->thread, a, b, c, d, ee, f, g, h);
}

THIS int32_t ei_update_filter(ECIinstance *self, void *a, void *b, int32_t c, void *d, int32_t ee, int32_t f)
{
    return stm_updateFilter(self->thread, a, b, c, d, ee, f);
}

THIS void ei_get_avail_filters(ECIinstance *self, int32_t a, uint32_t *b, uint32_t *c)
{
    stm_getAvailableFilters(self->thread, a, b, c);
}

THIS void ei_get_filter_desc(ECIinstance *self, int32_t a, uint32_t b, char *c)
{
    stm_getFilterDescription(self->thread, a, b, c);
}

ALIAS("??0ECIinstance@@QAE@XZ", "ei_ctor");
ALIAS("??0ECIinstance@@QAE@W4ECILanguageDialect@@@Z", "ei_ctor_lang");
ALIAS("??1ECIinstance@@QAE@XZ", "ei_dtor");
ALIAS("??0ECIstate@@QAE@XZ", "es_ctor");
ALIAS("??1ECIstate@@QAE@XZ", "es_dtor");
ALIAS("?eciCheckSynthesizing@ECIinstance@@QAEJXZ", "ei_check_synthesizing");
ALIAS("?eciPause@ECIinstance@@QAEJJ@Z", "ei_pause");
ALIAS("?eciRegisterPhonemeBuffer@ECIinstance@@QAEJPAXJJ@Z", "ei_reg_phonemes");
ALIAS("?eciRegisterCallback@ECIinstance@@QAEJP6AJPAXJJ0@Z0F0@Z",
      "ei_reg_callback");
ALIAS("?eciAddText@ECIinstance@@QAEJPAXJJJJ@Z", "ei_add_text");
ALIAS("?eciSetParam@ECIinstance@@QAEJJJJ@Z", "ei_set_param");
ALIAS("?eciGetParam@ECIinstance@@QAEJJJPAJ@Z", "ei_get_param");
ALIAS("?eciSetStandardVoice@ECIinstance@@QAEJJ@Z", "ei_set_std_voice");
ALIAS("?eciReset@ECIinstance@@QAEJW4ECILanguageDialect@@@Z", "ei_reset");
ALIAS("?eciActivateDict@ECIinstance@@QAEJPAX@Z", "ei_act_dict");
ALIAS("?eciActivateFilter@ECIinstance@@QAEJJ@Z", "ei_act_filter_n");
ALIAS("?eciActivateFilter@ECIinstance@@QAEJPAX@Z", "ei_act_filter_p");
ALIAS("?eciBlock@ECIinstance@@QAEJXZ", "ei_block");
ALIAS("?eciClearErrors@ECIinstance@@QAEJXZ", "ei_clear_errors");
ALIAS("?eciDeactivateDict@ECIinstance@@QAEJPAX@Z", "ei_deact_dict");
ALIAS("?eciDeactivateFilter@ECIinstance@@QAEJJ@Z", "ei_deact_filter_n");
ALIAS("?eciDeactivateFilter@ECIinstance@@QAEJPAX@Z", "ei_deact_filter_p");
ALIAS("?eciDeleteAudioFormat@ECIinstance@@QAEJXZ", "ei_del_audio_fmt");
ALIAS("?eciDeleteDict@ECIinstance@@QAEJPAX@Z", "ei_del_dict");
ALIAS("?eciDeleteFilter@ECIinstance@@QAEJPAX@Z", "ei_del_filter");
ALIAS("?eciFindFirstDictEntry@ECIinstance@@QAEJPAXJPAPAXPAJ12J@Z", "ei_find_first");
ALIAS("?eciFindFirstDictEntryExt@ECIinstance@@QAEJPAXJPAPAXPAJ12PAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z", "ei_find_first_ext");
ALIAS("?eciFindNextDictEntry@ECIinstance@@QAEJPAXJPAPAXPAJ12J@Z", "ei_find_next");
ALIAS("?eciFindNextDictEntryExt@ECIinstance@@QAEJPAXJPAPAXPAJ12PAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z", "ei_find_next_ext");
ALIAS("?eciGetActiveDict@ECIinstance@@QAEJJPAPAX@Z", "ei_get_active_dict");
ALIAS("?eciGetDictLanguage@ECIinstance@@QAEJPAXPAJ@Z", "ei_get_dict_lang");
ALIAS("?eciGetFilterMngr@ECIinstance@@QAEPAXXZ", "ei_get_filter_mngr");
ALIAS("?eciGetRomMngr@ECIinstance@@QAEPAXXZ", "ei_get_rom_mngr");
ALIAS("?eciInsertIndex@ECIinstance@@QAEJJ@Z", "ei_insert_index");
ALIAS("?eciIsFilterActive@ECIinstance@@QAEHI@Z", "ei_is_filter_active");
ALIAS("?eciLoadDictVolume@ECIinstance@@QAEJPAXJPBD@Z", "ei_load_dict_vol");
ALIAS("?eciLookupDict@ECIinstance@@QAEJPAXJ0JPAPAXPAJJ@Z", "ei_lookup_dict");
ALIAS("?eciLookupDictExt@ECIinstance@@QAEJPAXJ0JPAPAXPAJPAW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z", "ei_lookup_dict_ext");
ALIAS("?eciNewAudioFormat@ECIinstance@@QAEJPAUECIaudioFormat@@@Z", "ei_new_audio_fmt");
ALIAS("?eciNewDict@ECIinstance@@QAEJJPAPAX@Z", "ei_new_dict");
ALIAS("?eciNewFilter@ECIinstance@@QAEJJJPAPAX@Z", "ei_new_filter");
ALIAS("?eciPoll@ECIinstance@@QAEJXZ", "ei_poll");
ALIAS("?eciRegisterSampleBuffer@ECIinstance@@QAEJPAFJPAUECIsampleFormat@@@Z", "ei_reg_samples");
ALIAS("?eciRegisterVoice@ECIinstance@@QAEJHPAUECIExtendedVoiceAttrib@@PAX@Z", "ei_reg_voice");
ALIAS("?eciSaveDictVolume@ECIinstance@@QAEJPAXJPBD@Z", "ei_save_dict_vol");
ALIAS("?eciStop@ECIinstance@@QAEJXZ", "ei_stop");
ALIAS("?eciSynchronize@ECIinstance@@QAEJXZ", "ei_synchronize");
ALIAS("?eciSynthesize@ECIinstance@@QAEJXZ", "ei_synthesize");
ALIAS("?eciUnblock@ECIinstance@@QAEJXZ", "ei_unblock");
ALIAS("?eciUnregisterVoice@ECIinstance@@QAEJHPAUECIVoiceAttrib@@PAPAX@Z", "ei_unreg_voice");
ALIAS("?eciUpdateDict@ECIinstance@@QAEJPAXJ0J0JJ@Z", "ei_update_dict");
ALIAS("?eciUpdateDictExt@ECIinstance@@QAEJPAXJ0J0JW4ECIPartOfSpeech@@W4ECILanguageDialect@@@Z", "ei_update_dict_ext");
ALIAS("?eciUpdateFilter@ECIinstance@@QAEJPAX0J0JJ@Z", "ei_update_filter");
ALIAS("?eciGetAvailableFilters@ECIinstance@@QAEXJPAI0@Z", "ei_get_avail_filters");
ALIAS("?eciGetFilterDescription@ECIinstance@@QAEXJIPAD@Z", "ei_get_filter_desc");
