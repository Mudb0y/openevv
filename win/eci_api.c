/* The published ECI names, exported from a library.
 *
 * The engine implements the whole of the interface already -- fifty-odd
 * entry points -- but under our own short names, because the differential
 * build had to be able to say them in assembly and IBM's own names are
 * MSVC-manglings that no assembler will take. This file is the other half:
 * one wrapper per published name, so that a program which expects IBM's
 * eci.dll can load ours instead and never know.
 *
 * That program is usually a screen reader add-on. The one most people have
 * calls seventeen of these, loads the library with ctypes' windll, and hands
 * in a callback made with WINFUNCTYPE.
 *
 * None of the calling convention business matters here, which is worth saying
 * because it matters enormously in a thirty-two bit build. On x86-64 there is
 * one convention: __stdcall and __cdecl are the same thing, a stdcall name
 * carries no @N to strip, and a callback made as stdcall is callable as
 * anything. So these are plain functions, exported under plain names.
 *
 * What is not here: the filter interface, which the engine does not implement,
 * and eciGeneratePhonemes and the dictionary find, lookup and update calls,
 * which exist inside the engine but have no public wrapper in our tree yet. A
 * caller asking for one of those gets nothing rather than something wrong.
 */

#include <stdint.h>

#include "evv_abi.h"

typedef struct OldInst OldInst;

#define API __declspec(dllexport)

/* Ours, as the engine defines them. */
OldInst *STDCALL eo_new(void);
OldInst *STDCALL eo_newEx(int32_t language);
int      STDCALL es_delete(OldInst *h);
int      STDCALL es_reset(OldInst *h);
void     STDCALL eo_version(char *out);
int      STDCALL es_testPhrase(OldInst *h);
int      STDCALL es_speakText(const void *text, int32_t annotations);
int      STDCALL es_speakTextEx(const void *text, int32_t annotations,
                                int32_t language);
int32_t  STDCALL eo_getParam(OldInst *h, int32_t which);
int32_t  STDCALL ev_setParam(OldInst *h, int32_t which, int32_t value);
int32_t  STDCALL es_getDefaultParam(int32_t which);
int32_t  STDCALL es_setDefaultParam(int32_t which, int32_t value);
int      STDCALL eo_getAvailableLanguages(uint32_t *out, int *count);
int      STDCALL vc_copyVoice(OldInst *h, int32_t from, int32_t to);
int      STDCALL vc_getVoiceName(OldInst *h, int32_t voice, void *out);
int      STDCALL vc_setVoiceName(OldInst *h, int32_t voice, const void *name);
int32_t  STDCALL vc_getVoiceParam(OldInst *h, int32_t voice, int32_t which);
int      STDCALL vc_setVoiceParam(OldInst *h, int32_t voice, int32_t which,
                                  int32_t value);
int      STDCALL et_addText(OldInst *h, const void *text);
int      STDCALL et_insertIndex(OldInst *h, int32_t index);
int      STDCALL et_synthesize(OldInst *h);
int      STDCALL es_synthesizeFile(OldInst *h, const void *name);
int      STDCALL eo_clearInput(OldInst *h);
int32_t  STDCALL eo_getIndex(OldInst *h);
int      STDCALL eo_stop(OldInst *h);
int      STDCALL eo_speaking(OldInst *h);
int      STDCALL es_synchronize(OldInst *h);
void     STDCALL eo_synchronizeSynth(OldInst *h);
int      STDCALL ev_setOutputBuffer(OldInst *h, int32_t samples, void *buffer);
int      STDCALL es_setOutputFilename(OldInst *h, const void *name);
int      STDCALL ev_setOutputDevice(OldInst *h, int32_t device);
int      STDCALL es_pause(OldInst *h, int32_t on);
void     STDCALL eo_registerCallback(OldInst *h, void *callback, void *data);
void    *STDCALL ed_newDict(OldInst *h);
void    *STDCALL ed_getDict(OldInst *h);
int      STDCALL ed_setDict(OldInst *h, void *dict);
int      STDCALL ed_deleteDict(OldInst *h, void *dict);
int      STDCALL ed_loadDict(OldInst *h, void *dict, int32_t volume,
                             const void *name);
int      STDCALL ed_saveDict(OldInst *h, void *dict, int32_t volume,
                             const void *name);
int      STDCALL vc_registerVoice(OldInst *h, int32_t voice, void *data,
                                  void *attrib);
int      STDCALL vc_unregisterVoice(OldInst *h, int32_t voice, void *attrib,
                                    void **data);
void     STDCALL eo_clearErrors(OldInst *h);
void     STDCALL es_errorMessage(OldInst *h, void *out);
int32_t  STDCALL es_progStatus(OldInst *h);
int      STDCALL es_isBeingReentered(OldInst *h);
int      STDCALL es_requestLicense(OldInst *h);
void     STDCALL es_startLogging(int32_t what);
void     STDCALL es_stopLogging(int32_t what);
int      STDCALL es_getLog(void *out);
int      STDCALL es_getIntLog(int32_t which, int32_t *out);
int      STDCALL es_dialogBox(OldInst *h, void *parent, int32_t which,
                              void *a, void *b);
int      STDCALL es_getFilteredText(OldInst *h, const void *text, void *out,
                                    int32_t room);

/* What the engine needs of the platform before anything is asked of it. The
   original's DLL did this in its entry point, and so does this one, below. */
void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

/* ---- the published names -------------------------------------------- */

API void *eciNew(void) { return eo_new(); }
API void *eciNewEx(int language) { return eo_newEx(language); }

/* The original answers a handle, which is always nothing. */
API void *eciDelete(void *h) { return (void *)(intptr_t)es_delete(h); }

API int  eciReset(void *h) { return es_reset(h); }
API void eciVersion(char *out) { eo_version(out); }
API int  eciTestPhrase(void *h) { return es_testPhrase(h); }

API int eciSpeakText(const void *text, int annotations)
{
    return es_speakText(text, annotations);
}

API int eciSpeakTextEx(const void *text, int annotations, int language)
{
    return es_speakTextEx(text, annotations, language);
}

API int eciGetParam(void *h, int which) { return eo_getParam(h, which); }
API int eciSetParam(void *h, int which, int v) { return ev_setParam(h, which, v); }
API int eciGetDefaultParam(int which) { return es_getDefaultParam(which); }
API int eciSetDefaultParam(int which, int v) { return es_setDefaultParam(which, v); }

API int eciGetAvailableLanguages(unsigned int *langs, int *count)
{
    return eo_getAvailableLanguages((uint32_t *)langs, count);
}

API int eciCopyVoice(void *h, int from, int to) { return vc_copyVoice(h, from, to); }
API int eciGetVoiceName(void *h, int voice, void *out) { return vc_getVoiceName(h, voice, out); }
API int eciSetVoiceName(void *h, int voice, const void *name) { return vc_setVoiceName(h, voice, name); }
API int eciGetVoiceParam(void *h, int voice, int which) { return vc_getVoiceParam(h, voice, which); }

API int eciSetVoiceParam(void *h, int voice, int which, int value)
{
    return vc_setVoiceParam(h, voice, which, value);
}

API int eciAddText(void *h, const void *text) { return et_addText(h, text); }
API int eciInsertIndex(void *h, int index) { return et_insertIndex(h, index); }
API int eciSynthesize(void *h) { return et_synthesize(h); }
API int eciSynthesizeFile(void *h, const void *name) { return es_synthesizeFile(h, name); }
API int eciClearInput(void *h) { return eo_clearInput(h); }
API int eciGetIndex(void *h) { return eo_getIndex(h); }
API int eciStop(void *h) { return eo_stop(h); }
API int eciSpeaking(void *h) { return eo_speaking(h); }
API int eciSynchronize(void *h) { return es_synchronize(h); }
API void eciSynchronizeSynth(void *h) { eo_synchronizeSynth(h); }

API int eciSetOutputBuffer(void *h, int samples, short *buffer)
{
    return ev_setOutputBuffer(h, samples, buffer);
}

API int eciSetOutputFilename(void *h, const void *name) { return es_setOutputFilename(h, name); }
API int eciSetOutputDevice(void *h, int device) { return ev_setOutputDevice(h, device); }
API int eciPause(void *h, int on) { return es_pause(h, on); }

API void eciRegisterCallback(void *h, void *callback, void *data)
{
    eo_registerCallback(h, callback, data);
}

API void *eciNewDict(void *h) { return ed_newDict(h); }
API void *eciGetDict(void *h) { return ed_getDict(h); }
API int   eciSetDict(void *h, void *dict) { return ed_setDict(h, dict); }
API void *eciDeleteDict(void *h, void *dict) { return (void *)(intptr_t)ed_deleteDict(h, dict); }

API int eciLoadDict(void *h, void *dict, int volume, const void *name)
{
    return ed_loadDict(h, dict, volume, name);
}

API int eciSaveDict(void *h, void *dict, int volume, const void *name)
{
    return ed_saveDict(h, dict, volume, name);
}

API int eciRegisterVoice(void *h, int voice, void *data, void *attrib)
{
    return vc_registerVoice(h, voice, data, attrib);
}

API int eciUnregisterVoice(void *h, int voice, void *attrib, void **data)
{
    return vc_unregisterVoice(h, voice, attrib, data);
}

API void eciClearErrors(void *h) { eo_clearErrors(h); }
API void eciErrorMessage(void *h, void *out) { es_errorMessage(h, out); }
API int  eciProgStatus(void *h) { return es_progStatus(h); }
API int  eciIsBeingReentered(void *h) { return es_isBeingReentered(h); }
API int  eciRequestLicense(void *h) { return es_requestLicense(h); }
API void eciStartLogging(int what) { es_startLogging(what); }
API void eciStopLogging(int what) { es_stopLogging(what); }
API int  eciGetLog(void *out) { return es_getLog(out); }
API int  eciGetIntLog(int which, int *out) { return es_getIntLog(which, (int32_t *)out); }

API int eciDialogBox(void *h, void *parent, int which, void *a, void *b)
{
    return es_dialogBox(h, parent, which, a, b);
}

API int eciGetFilteredText(void *h, const void *text, void *out, int room)
{
    return es_getFilteredText(h, text, out, room);
}

/* ---- coming and going ------------------------------------------------ */

/* The drivers do this for themselves; a library's callers cannot be asked to.
 * The original's DLL ran its static initialisers from its entry point too.
 * Nothing here starts a thread or takes a lock the loader is holding: the
 * platform layer's start is empty on Windows, the initialisers only construct,
 * and the engine's own threads are made later, on the caller's thread, when it
 * asks for an instance.
 */
int __stdcall DllMain(void *module, unsigned long why, void *reserved)
{
    (void)module;
    (void)reserved;

    if (why == 1) {             /* DLL_PROCESS_ATTACH */
        evv_port_start();
        evvRunStaticInitialisers();
    } else if (why == 0) {      /* DLL_PROCESS_DETACH */
        evv_port_finish();
    }
    return 1;
}
