/* The published interface, for a program that wants to speak.
 *
 * Everything a caller needs is here and nothing else is: the names IBM
 * published, the numbers they take, and the two macros that say how they are
 * called. The engine's own short names -- eo_new, et_addText, ev_setParam --
 * are internal and are not in here, because the whole point of a published
 * interface is that it does not move when the inside does.
 *
 * lib/eci_api.c is built from this, which is what makes the header and the
 * libraries one description rather than two. The other two places in the tree
 * that declare these names keep their own copies for reasons: cli/evv.c links
 * the archive and calls the engine's short names, which are not published and
 * are not in here, and test/lib/dll.c resolves every name at run time on
 * purpose, so that what it proves is the export table rather than the header.
 *
 * The numbers are this engine's, read out of the code that acts on them
 * rather than out of IBM's header, and where the two differ it is said so
 * against the name. docs/api.md is the prose; this is the declaration.
 */

#ifndef ECI_H
#define ECI_H

#ifdef __cplusplus
extern "C" {
#endif

/* How these are called, which is not decoration on thirty-two bit Windows.
 *
 * IBM's own objects export `_eciSetParam@12' and `_eciAddText@8': the byte
 * count is part of a stdcall name on x86, so the published interface is
 * stdcall there and a caller that gets it wrong leaves the stack out by the
 * size of the arguments. On x86-64 there is one convention and the attribute
 * means nothing; off Windows there is nothing to say at all.
 *
 * The library publishes the names undecorated all the same, because that is
 * what a caller asking by name -- ctypes, dlsym, GetProcAddress -- asks for.
 */
#if defined(_WIN32)
#define ECICALL __stdcall
#else
#define ECICALL
#endif

/* Whether a declaration is being read by the library or by its caller.
   Nothing but the library itself defines ECI_BUILDING, and a caller that is
   linking the engine into its own image rather than loading a library
   defines ECI_STATIC, which takes the import decoration off again. */
#if defined(ECI_STATIC)
#define ECIAPI
#elif defined(_WIN32)
#if defined(ECI_BUILDING)
#define ECIAPI __declspec(dllexport)
#else
#define ECIAPI __declspec(dllimport)
#endif
#elif defined(ECI_BUILDING)
#define ECIAPI __attribute__((visibility("default")))
#else
#define ECIAPI
#endif

/* ---- what a caller holds --------------------------------------------- */

/* An engine instance, a user dictionary set and a text filter. Each is
   opaque: what is behind it is the engine's business and changes shape
   between builds. */
typedef void *ECIHand;
typedef void *ECIDictHand;
typedef void *ECIFilterHand;

#define NULL_ECI_HAND    ((ECIHand)0)
#define NULL_DICT_HAND   ((ECIDictHand)0)

/* ---- what the engine says to the caller ------------------------------ */

/* What a callback is being told. Waveform and the index replies are what an
   ordinary caller sees; the phoneme ones arrive only when phonemes were
   asked for. */
typedef enum {
    eciWaveformBuffer    = 0,   /* the buffer holds this many samples */
    eciPhonemeBuffer     = 1,
    eciIndexReply        = 2,   /* the number given to eciInsertIndex */
    eciPhonemeIndexReply = 3,
    eciWordIndexReply    = 4,
    eciStringIndexReply  = 5,
    eciAudioIndexReply   = 6
} ECIMessage;

/* And what it may answer.
 *
 * eciDataNotProcessed means "my buffer is full, offer it again", and the
 * engine waits thirty milliseconds before doing so. A caller that means
 * "throw these samples away" must answer eciDataProcessed: answering not
 * processed costs that wait per buffer and is the trap docs/quirks.md
 * describes.
 *
 * eciDataAbort stops the engine handing over any more buffers. It does not
 * make it abandon the utterance, and nothing can. */
typedef enum {
    eciDataNotProcessed = 0,
    eciDataProcessed    = 1,
    eciDataAbort        = 2
} ECICallbackReturn;

/* The callback itself.
 *
 * IBM's Linux header spells the third argument `long', which is eight bytes
 * on an ordinary sixty-four bit machine and four in everything the engine
 * puts there. It is `int' here so that the declaration says what is passed.
 * A caller porting from IBM's header should change the type rather than keep
 * it: reading eight bytes where four were written reads the register's other
 * half.
 *
 * It runs on the engine's own synthesis thread, not the caller's. What it may
 * do is copy the samples and return; what it may not do is call back into the
 * same instance. */
typedef int (ECICALL *ECICallback)(ECIHand handle, ECIMessage message,
                                   int param, void *data);

/* ---- the engine's settings ------------------------------------------- */

/* The eighteen words an instance keeps, by the number eciGetParam and
 * eciSetParam take. Four of the eighteen are not settings but the shape of a
 * sound device, and one of those is the sample rate.
 *
 * Numbers 11 and 17 are not reachable at all: eciGetParam and eciSetParam
 * both refuse them, which is IBM's own refusal transcribed. Seventeen is the
 * number of the voice being spoken in and eciCopyVoice is what moves it.
 * Numbers 4 and 6 can be set and read and nothing anywhere reads them.
 * None of the four has a name here, because naming a thing nothing reads
 * invites a caller to set it.
 */
typedef enum {
    /* Nought sends text straight down; one queues it with the settings that
       were in force when it arrived, so a program can set a voice, add
       words, set another voice, add more, and have each stretch spoken in
       the voice it was written under. */
    eciSynthMode          = 0,
    /* One turns annotations on, which is what a backtick in the text means.
       docs/api.md lists them. */
    eciInputType          = 1,
    eciTextMode           = 2,
    eciDictionary         = 3,   /* 1 turns the dictionary off, not on */
    eciSampleRate         = 5,
    eciWantPhonemeIndices = 7,
    eciRealWorldUnits     = 8,   /* speed in words a minute, pitch in hertz */
    eciLanguageDialect    = 9,
    eciNumberMode         = 10,
    /* Ours. IBM's romanizer switch sits here and is only reachable in a
       language written in another script. */
    eciRomanizer          = 12,
    /* The four an audio device's format is built from. Nothing in the tree
       knows what any of them means -- they are carried into the format as
       four numbers and no code here reads them again -- so they keep the
       letters the format gives them. Setting one while the samples are going
       to a buffer records the number and rebuilds nothing, which is the
       second deliberate divergence: IBM's engine rebuilds regardless and
       loses the buffer. */
    eciAudioFormatA       = 13,
    eciAudioFormatB       = 14,
    eciAudioFormatC       = 15,
    eciAudioFormatD       = 16,
    eciNumParams          = 18
} ECIParam;

/* A voice's eight, which every one of the eight preset voices has.
 *
 * The range of each is 0 to 100, except gender, which is 0 for a male voice
 * and 1 for a female one, and speed, which runs to 250. With eciRealWorldUnits
 * on, speed is words a minute and pitch is hertz and both have ranges of
 * their own. */
typedef enum {
    eciGender           = 0,
    eciHeadSize         = 1,
    eciPitchBaseline    = 2,
    eciPitchFluctuation = 3,
    eciRoughness        = 4,
    eciBreathiness      = 5,
    eciSpeed            = 6,
    eciVolume           = 7,
    eciNumVoiceParams   = 8
} ECIVoiceParam;

/* The voice numbers, and how much room a name wants.
 *
 * Voice 0 is the one being spoken in and starts as a copy of voice 1.
 * Voices 1 to 8 are the language's own presets and are read-only: Adult Male
 * 1, Adult Female 1, Child 1, Adult Male 2, Adult Male 3, Adult Female 2,
 * Elderly Female 1 and Elderly Male 1. Voices 9 to 16 are the caller's,
 * copied from those eight when the instance is made and all called
 * User-Defined, and are the ones a program wanting a voice of its own should
 * edit. eciCopyVoice will only write to 0 or to 9 through 16.
 */
#define ECI_PRESET_VOICES        8
#define ECI_EDITABLE_VOICES      8
#define ECI_LAST_VOICE           16
#define ECI_VOICE_NAME_LENGTH    30

/* Room for what eciVersion writes, which is four numbers with dots between
   them. It is written into the caller's buffer and the call is never told
   how big that is, so this is the size to give it rather than a size it
   checks. */
#define ECI_VERSION_LENGTH       20

/* The three volumes a user dictionary set holds. */
typedef enum {
    eciMainDict = 0,
    eciRootDict = 1,
    eciAbbvDict = 2,
    eciNumDictVolumes = 3
} ECIDictVolume;

/* ---- languages -------------------------------------------------------- */

/* A language is one word: the family in the top half, the code set in the
 * third byte and the dialect in the bottom one. eciGetAvailableLanguages
 * answers with the ones this build actually has, which is the only honest
 * source -- a build takes as many language modules as it was given, and the
 * constants below name families rather than promise them.
 *
 * The code set travels in that same word rather than in a setting of its
 * own, which is a thing to know rather than to guess at: ORing 0x800 into
 * the language is what says the text is UTF-16 rather than bytes, and it is
 * eciLanguageDialect the engine reads to find out, not eciTextMode.
 */
typedef enum {
    eciGeneralAmericanEnglish = 0x00010000,
    eciBritishEnglish         = 0x00010001,
    eciCastilianSpanish       = 0x00020000,
    eciMexicanSpanish         = 0x00020001,
    eciStandardFrench         = 0x00030000,
    eciCanadianFrench         = 0x00030001,
    eciStandardGerman         = 0x00040000,
    eciStandardItalian        = 0x00050000,
    eciMandarinChinese        = 0x00060000,
    eciTaiwaneseMandarin      = 0x00060001,
    eciBrazilianPortuguese    = 0x00070000,
    eciStandardJapanese       = 0x00080000,
    eciStandardFinnish        = 0x00090000,
    eciStandardKorean         = 0x000a0000,
    eciStandardCantonese      = 0x000b0000,
    eciHongKongCantonese      = 0x000b0001,
    eciStandardDutch          = 0x000c0000,
    eciStandardNorwegian      = 0x000d0000,
    eciStandardSwedish        = 0x000e0000,
    eciStandardDanish         = 0x000f0000,
    /* Family seventeen is Thai in IBM's tables and Polish here, which is the
       twelfth deliberate divergence. There is no Thai in the SDK this engine
       came out of for that to cost anything. */
    eciStandardPolish         = 0x00110000,
    /* The bit that says the text is UTF-16 rather than bytes. */
    eciUnicodeCodeSet         = 0x00000800
} ECILanguageDialect;

/* ---- the calls -------------------------------------------------------- */

/* Making an instance and taking it away. eciNew takes whichever language the
   build names first; eciNewEx takes one eciGetAvailableLanguages answered
   with. Each instance starts a synthesis thread of its own. */
ECIAPI ECIHand ECICALL eciNew(void);
ECIAPI ECIHand ECICALL eciNewEx(int language);
ECIAPI ECIHand ECICALL eciDelete(ECIHand handle);
ECIAPI int     ECICALL eciReset(ECIHand handle);
ECIAPI void    ECICALL eciVersion(char *buffer);
ECIAPI int     ECICALL eciTestPhrase(ECIHand handle);

/* Which languages this build has. Asked with no room it answers how many
   there are; asked with room it fills in that many and says how many it
   wrote. Both times the count travels in the same place. */
ECIAPI int ECICALL eciGetAvailableLanguages(unsigned int *languages,
                                            int *count);

/* Text in. eciAddText adds to what will be said next and eciSynthesize
   starts saying it.

   eciSpeakText and eciSpeakTextEx are IBM's shorthand for both on an
   instance of their own that the caller never sees -- and because that
   instance has no callback and no buffer, its eciAddText refuses and they
   answer nought having said nothing. What they do do is make and destroy a
   synthesis thread and its four megabyte frame stack. */
ECIAPI int ECICALL eciAddText(ECIHand handle, const void *text);
ECIAPI int ECICALL eciInsertIndex(ECIHand handle, int index);
ECIAPI int ECICALL eciSynthesize(ECIHand handle);
/* Published, and empty in IBM's own object: it answers nought and writes
   nothing. eciSetOutputBuffer is the only way to get samples out. */
ECIAPI int ECICALL eciSynthesizeFile(ECIHand handle, const void *filename);
ECIAPI int ECICALL eciClearInput(ECIHand handle);

/* The text as phonemes rather than as sound. The output is switched to the
   caller's buffer, everything queued is put through, and the output goes
   back to wherever it was; the answer here is only whether that worked, the
   phonemes themselves arriving through the callback as eciPhonemeBuffer.

   Two conditions, both IBM's and neither obvious. A callback has to be
   registered, since that is the only way the phonemes can arrive. And
   eciSynthMode has to be one: this walks the queue that mode builds, and in
   mode nought there is nothing in it. */
ECIAPI int ECICALL eciGeneratePhonemes(ECIHand handle, int room, void *buffer);
ECIAPI int ECICALL eciSpeakText(const void *text, int annotations);
ECIAPI int ECICALL eciSpeakTextEx(const void *text, int annotations,
                                  int language);

/* Where the samples go, and the first thing a program must decide.
 *
 * An instance starts out sending its samples to an audio device, and there
 * is no audio device: the platform layer answers that the machine has none,
 * which is what sends the engine down the buffer path. Until a buffer is
 * registered the instance has nowhere to put anything, and eciAddText
 * answers nought -- which is the one call that says so, eciSynthesize and
 * eciSynchronize both answering success on an instance that will never
 * speak. eciSetOutputBuffer is what a program calls first.
 *
 * eciSetOutputBuffer(h, 0, 0) puts it back to the device, which is to say
 * back to nothing. eciSetOutputFilename is empty in IBM's own object and
 * writes no file.
 */
ECIAPI int ECICALL eciSetOutputBuffer(ECIHand handle, int samples,
                                      short *buffer);
ECIAPI int ECICALL eciSetOutputFilename(ECIHand handle, const void *filename);
ECIAPI int ECICALL eciSetOutputDevice(ECIHand handle, int device);

/* And who is told about them. */
ECIAPI void ECICALL eciRegisterCallback(ECIHand handle, ECICallback callback,
                                        void *data);

/* Waiting, stopping and asking. eciSpeaking answers whether anything is
   still outstanding; eciSynchronize waits for all of it. eciStop asks the
   engine to stop handing samples over, which it does at the end of the
   message it is on -- it cannot abandon an utterance. */
ECIAPI int  ECICALL eciSpeaking(ECIHand handle);
ECIAPI int  ECICALL eciStop(ECIHand handle);
ECIAPI int  ECICALL eciPause(ECIHand handle, int pause);
ECIAPI int  ECICALL eciSynchronize(ECIHand handle);
ECIAPI void ECICALL eciSynchronizeSynth(ECIHand handle);
ECIAPI int  ECICALL eciGetIndex(ECIHand handle);

/* The settings. eciSetParam answers what the setting was, or -1 if it
   refused; the default ones are what a new instance starts from. */
ECIAPI int ECICALL eciGetParam(ECIHand handle, int parameter);
ECIAPI int ECICALL eciSetParam(ECIHand handle, int parameter, int value);
ECIAPI int ECICALL eciGetDefaultParam(int parameter);
ECIAPI int ECICALL eciSetDefaultParam(int parameter, int value);

/* The voices. Voice 0 is the one being spoken in and 1 to 8 are the
   language's presets. */
ECIAPI int ECICALL eciCopyVoice(ECIHand handle, int from, int to);
ECIAPI int ECICALL eciGetVoiceName(ECIHand handle, int voice, void *name);
ECIAPI int ECICALL eciSetVoiceName(ECIHand handle, int voice,
                                   const void *name);
ECIAPI int ECICALL eciGetVoiceParam(ECIHand handle, int voice, int parameter);
ECIAPI int ECICALL eciSetVoiceParam(ECIHand handle, int voice, int parameter,
                                    int value);
ECIAPI int ECICALL eciRegisterVoice(ECIHand handle, int voice, void *data,
                                    void *attribute);
ECIAPI int ECICALL eciUnregisterVoice(ECIHand handle, int voice,
                                      void *attribute, void **data);

/* The user dictionary. A set holds three volumes and an instance has one set
   in force at a time. */
ECIAPI ECIDictHand ECICALL eciNewDict(ECIHand handle);
ECIAPI ECIDictHand ECICALL eciGetDict(ECIHand handle);
ECIAPI int         ECICALL eciSetDict(ECIHand handle, ECIDictHand dict);
ECIAPI ECIDictHand ECICALL eciDeleteDict(ECIHand handle, ECIDictHand dict);
ECIAPI int         ECICALL eciLoadDict(ECIHand handle, ECIDictHand dict,
                                       int volume, const void *filename);
ECIAPI int         ECICALL eciSaveDict(ECIHand handle, ECIDictHand dict,
                                       int volume, const void *filename);

/* What went wrong, which none of these will tell you.
 *
 * All five are empty in IBM's own object and are transcribed empty here:
 * eciProgStatus and eciIsBeingReentered always answer nought, eciErrorMessage
 * writes nothing into the buffer it is given, and eciRequestLicense answers
 * nought. The instance really does record what it refused and why; nothing
 * published hands it over.
 *
 * So a caller judges by return values. Every call that answers an int
 * answers nought when it refused, and eciSetParam answers -1. */
ECIAPI void ECICALL eciClearErrors(ECIHand handle);
ECIAPI void ECICALL eciErrorMessage(ECIHand handle, void *buffer);
ECIAPI int  ECICALL eciProgStatus(ECIHand handle);
ECIAPI int  ECICALL eciIsBeingReentered(ECIHand handle);
ECIAPI int  ECICALL eciRequestLicense(ECIHand handle);

/* IBM's logging and its dialogue box, all five empty in its object too.
   They are exported so that a program which calls them links. */
ECIAPI void ECICALL eciStartLogging(int what);
ECIAPI void ECICALL eciStopLogging(int what);
ECIAPI int  ECICALL eciGetLog(void *buffer);
ECIAPI int  ECICALL eciGetIntLog(int which, int *value);
ECIAPI int  ECICALL eciDialogBox(ECIHand handle, void *parent, int which,
                                 void *a, void *b);

/* ---- text filters, which is how SSML is read -------------------------- */

/* The engine carries the SSML reader and never loads it by itself, so
 * turning it on is three calls: register the filter with the entry point,
 * make one, and activate it. Where IBM's caller got that entry point out of
 * a DLL of its own -- ssmlfilter.dll, asking for ssmlFilterGetObject -- ours
 * is in this library under the same name, so there is one fewer file to find.
 *
 * What eciRegisterFilter takes is the address of that entry point, and it
 * reads it through a pointer: hand it the address of a variable holding the
 * function, not the function.
 */
ECIAPI int ECICALL ssmlFilterGetObject(unsigned int idInterface, void **out);

ECIAPI int ECICALL eciRegisterFilter(ECIHand handle, unsigned int id,
                                     void *entry, void *attribute,
                                     int autoload);
ECIAPI int ECICALL eciUnregisterFilter(ECIHand handle, unsigned int id,
                                       void *attribute);
ECIAPI ECIFilterHand ECICALL eciNewFilter(ECIHand handle, int id,
                                          int language);
ECIAPI int ECICALL eciDeleteFilter(ECIHand handle, ECIFilterHand filter);
/* These three take the handle eciNewFilter answered with, not the id it was
   made from. The engine's own transcription of them takes it as a
   thirty-two bit number, which holds because every filter is in the arena
   and the arena is below two gigabytes; it is declared here as what it is. */
ECIAPI int ECICALL eciActivateFilter(ECIHand handle, ECIFilterHand filter);
ECIAPI int ECICALL eciDeactivateFilter(ECIHand handle, ECIFilterHand filter);
ECIAPI int ECICALL eciSetFilter(ECIHand handle, ECIFilterHand filter);
ECIAPI int ECICALL eciUpdateFilter(ECIHand handle, ECIFilterHand filter,
                                   const char *a, const char *b);

/* A document in, the annotations the engine understands out. The answer is
   the reader's own buffer and the caller is given a pointer to it. IBM's
   reader writes a nought over the semicolon that ends a numeric character
   reference, in the caller's own string; ours copies the digits out instead,
   which is the eleventh deliberate divergence. */
ECIAPI int ECICALL eciGetFilteredText(ECIHand handle, ECIFilterHand filter,
                                      const void *text, char **out);

/* What a filter is asked to call itself, which is what eciRegisterFilter
   fills in. */
typedef struct ECIFilterAttribute {
    char eciFilterName[80];
    int  language;
} ECIFilterAttribute;

#ifdef __cplusplus
}
#endif

#endif
