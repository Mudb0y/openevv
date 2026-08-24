/* OpenEVV output module for Speech Dispatcher.
 *
 * Speech Dispatcher owns audio playback. This executable receives SSIP module
 * commands on standard input, synthesizes with OpenEVV, and sends 16-bit mono
 * PCM plus events back over the module protocol. It is intentionally linked
 * with the engine instead of starting build/evv for each utterance: one live
 * instance makes cancellation, index marks, and rapid speech practical.
 */

#include <ctype.h>
#include <errno.h>
#include <pthread.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <speech-dispatcher/spd_module_main.h>

#include "evv_abi.h"

enum { FRAME_SAMPLES = 2048, MAX_LANGUAGES = 32, VOICES_PER_LANGUAGE = 8 };
enum { PARAM_TEXT_MODE = 2 };
enum { TEXT_MODE_DEFAULT = 0, TEXT_MODE_ALPHA_SPELL = 1,
       TEXT_MODE_ALL_SPELL = 2 };
enum { VOICE_GENDER, VOICE_HEAD_SIZE, VOICE_PITCH, VOICE_FLUCTUATION,
       VOICE_ROUGHNESS, VOICE_BREATHINESS, VOICE_SPEED, VOICE_VOLUME };
enum ECIMessage { eciWaveformBuffer, eciPhonemeBuffer, eciIndexReply };
enum ECICallbackReturn { eciDataNotProcessed, eciDataProcessed, eciDataAbort };

typedef struct OldInst OldInst;

typedef struct {
    uint32_t id;
    const char *tag;
    const char *locale;
    const char *name;
} LanguageInfo;

typedef struct {
    int rate;
    int pitch;
    int pitchRange;
    int volume;
    SPDPunctuation punctuation;
    SPDSpelling spelling;
    SPDCapitalLetters capitals;
    SPDVoiceType voiceType;
    char *language;
    char *synthesisVoice;
} ModuleSettings;

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
} TextBuffer;

static const LanguageInfo knownLanguages[] = {
    { 0x00010000, "enus", "en-US", "American English" },
    { 0x00010001, "engb", "en-GB", "British English" },
    { 0x00020000, "eses", "es-ES", "Castilian Spanish" },
    { 0x00020001, "esus", "es-MX", "Mexican Spanish" },
    { 0x00030000, "frfr", "fr-FR", "French" },
    { 0x00030001, "frca", "fr-CA", "Canadian French" },
    { 0x00040000, "dede", "de-DE", "German" },
    { 0x00050000, "itit", "it-IT", "Italian" },
};

static const char *const voiceNames[VOICES_PER_LANGUAGE] = {
    "male1", "female1", "child", "male2",
    "male3", "female2", "elderly-female", "elderly-male"
};

static OldInst *engine;
static short audioFrame[FRAME_SAMPLES];
static uint32_t availableLanguages[MAX_LANGUAGES];
static int languageCount;
static uint32_t currentLanguage;
static SPDVoice **speechdVoices;
static char **markNames;
static size_t markCount;
static char *pendingIcon;
static volatile int stopRequested;
static volatile int pauseRequested;
static volatile int pauseIndexReached;
static int engineStarted;
static int debugEnabled;
static int logLevel;
static FILE *debugFile;
static ModuleSettings settings = {
    .rate = 0,
    .pitch = 0,
    .pitchRange = 0,
    .volume = 0,
    .punctuation = SPD_PUNCT_NONE,
    .spelling = SPD_SPELL_OFF,
    .capitals = SPD_CAP_NONE,
    .voiceType = SPD_MALE1,
};

OldInst *STDCALL eo_newEx(int32_t language);
int STDCALL es_delete(OldInst *h);
int STDCALL et_addText(OldInst *h, const char *text);
int STDCALL et_insertIndex(OldInst *h, int32_t index);
int STDCALL et_synthesize(OldInst *h);
int STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf);
int32_t STDCALL ev_setParam(OldInst *h, int32_t which, int32_t value);
void STDCALL eo_registerCallback(OldInst *h, void *callback, void *data);
void STDCALL eo_synchronizeSynth(OldInst *h);
int STDCALL eo_speaking(OldInst *h);
int STDCALL eo_getAvailableLanguages(uint32_t *out, int *count);
int STDCALL vc_copyVoice(OldInst *h, int32_t from, int32_t to);
int STDCALL vc_getVoiceParam(OldInst *h, int32_t voice, int32_t which);
int STDCALL vc_setVoiceParam(OldInst *h, int32_t voice, int32_t which,
                             int32_t value);
void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

static void debug_log(const char *message)
{
    if (!debugEnabled)
        return;
    fprintf(debugFile ? debugFile : stderr, "OpenEVV: %s\n", message);
    fflush(debugFile ? debugFile : stderr);
}

static char *copy_string(const char *value)
{
    char *copy;

    if (!value || strcmp(value, "NULL") == 0)
        return NULL;
    copy = malloc(strlen(value) + 1);
    if (copy)
        strcpy(copy, value);
    return copy;
}

static int parse_number(const char *value, int minimum, int maximum, int *out)
{
    char *end;
    long number;

    errno = 0;
    number = strtol(value, &end, 10);
    if (errno || end == value || *end || number < minimum || number > maximum)
        return -1;
    *out = (int)number;
    return 0;
}

static const LanguageInfo *language_info(uint32_t id)
{
    size_t i;

    for (i = 0; i < sizeof(knownLanguages) / sizeof(knownLanguages[0]); i++)
        if (knownLanguages[i].id == id)
            return &knownLanguages[i];
    return NULL;
}

static int language_matches(const LanguageInfo *info, const char *requested)
{
    size_t baseLength;

    if (!requested || !*requested || strcmp(requested, "NULL") == 0)
        return 0;
    if (!strcasecmp(requested, info->tag)
        || !strcasecmp(requested, info->locale))
        return 1;
    baseLength = strcspn(info->locale, "-");
    return strlen(requested) == baseLength
        && !strncasecmp(requested, info->locale, baseLength);
}

static int language_available(uint32_t id)
{
    int i;

    for (i = 0; i < languageCount; i++)
        if (availableLanguages[i] == id)
            return 1;
    return 0;
}

static uint32_t requested_language(void)
{
    int i;

    if (settings.synthesisVoice) {
        for (i = 0; i < languageCount; i++) {
            const LanguageInfo *info = language_info(availableLanguages[i]);
            size_t tagLength;

            if (!info)
                continue;
            tagLength = strlen(info->tag);
            if (!strncasecmp(settings.synthesisVoice, info->tag, tagLength)
                && settings.synthesisVoice[tagLength] == '-')
                return info->id;
        }
    }
    if (settings.language) {
        for (i = 0; i < languageCount; i++) {
            const LanguageInfo *info = language_info(availableLanguages[i]);

            if (info && language_matches(info, settings.language))
                return info->id;
        }
    }
    return currentLanguage ? currentLanguage : availableLanguages[0];
}

static int requested_voice(void)
{
    int i;

    if (settings.synthesisVoice) {
        const char *suffix = strchr(settings.synthesisVoice, '-');

        if (suffix) {
            suffix++;
            for (i = 0; i < VOICES_PER_LANGUAGE; i++)
                if (!strcasecmp(suffix, voiceNames[i]))
                    return i + 1;
        }
        if (!strncasecmp(settings.synthesisVoice, "voice", 5)
            && parse_number(settings.synthesisVoice + 5, 1, 8, &i) == 0)
            return i;
    }
    switch (settings.voiceType) {
    case SPD_MALE2: return 4;
    case SPD_MALE3: return 5;
    case SPD_FEMALE1: return 2;
    case SPD_FEMALE2: return 6;
    case SPD_FEMALE3: return 7;
    case SPD_CHILD_MALE:
    case SPD_CHILD_FEMALE: return 3;
    case SPD_MALE1:
    default: return 1;
    }
}

static int relative_value(int requested, int baseline, int maximum)
{
    if (requested < 0)
        return (requested + 100) * baseline / 100;
    return baseline + requested * (maximum - baseline) / 100;
}

static enum ECICallbackReturn STDCALL on_message(OldInst *h,
                                                  enum ECIMessage message,
                                                  long parameter, void *data)
{
    (void)h;
    (void)data;

    if (stopRequested || (pauseRequested && pauseIndexReached))
        return eciDataAbort;
    if (message == eciWaveformBuffer) {
        const uint16_t endianProbe = 1;
        AudioTrack track = {
            .bits = 16,
            .num_channels = 1,
            .sample_rate = 11025,
            .num_samples = (int)parameter,
            .samples = audioFrame,
        };

        module_tts_output_server(&track,
            *(const unsigned char *)&endianProbe
                ? SPD_AUDIO_LE : SPD_AUDIO_BE);
    } else if (message == eciIndexReply && parameter > 0
               && (size_t)parameter <= markCount) {
        const char *name = markNames[parameter - 1];

        if (!name) {
            module_report_icon("capital");
        } else {
            module_report_index_mark(name);
        }
        if (name && pauseRequested && !strncmp(name, "__spd_", 6)) {
            pauseIndexReached = 1;
            return eciDataAbort;
        }
    }
    return eciDataProcessed;
}

static int open_engine(uint32_t language)
{
    if (engine) {
        es_delete(engine);
        engine = NULL;
    }
    engine = eo_newEx((int32_t)language);
    if (!engine)
        return -1;
    eo_registerCallback(engine, (void *)on_message, NULL);
    if (!ev_setOutputBuffer(engine, FRAME_SAMPLES, audioFrame)) {
        es_delete(engine);
        engine = NULL;
        return -1;
    }
    currentLanguage = language;
    return 0;
}

static int configure_engine(void)
{
    uint32_t language = requested_language();
    int voice = requested_voice();
    int baseline;

    if (!language_available(language))
        language = availableLanguages[0];
    if ((!engine || language != currentLanguage) && open_engine(language) != 0)
        return -1;
    if (!vc_copyVoice(engine, voice, 0))
        return -1;

    baseline = vc_getVoiceParam(engine, 0, VOICE_SPEED);
    if (!vc_setVoiceParam(engine, 0, VOICE_SPEED,
                          relative_value(settings.rate, baseline, 140)))
        return -1;
    baseline = vc_getVoiceParam(engine, 0, VOICE_PITCH);
    if (!vc_setVoiceParam(engine, 0, VOICE_PITCH,
                          relative_value(settings.pitch, baseline, 100)))
        return -1;
    baseline = vc_getVoiceParam(engine, 0, VOICE_FLUCTUATION);
    if (!vc_setVoiceParam(engine, 0, VOICE_FLUCTUATION,
                          relative_value(settings.pitchRange, baseline, 100)))
        return -1;
    baseline = vc_getVoiceParam(engine, 0, VOICE_VOLUME);
    if (!vc_setVoiceParam(engine, 0, VOICE_VOLUME,
                          relative_value(settings.volume, baseline, 100)))
        return -1;
    return 0;
}

static void clear_marks(void)
{
    size_t i;

    for (i = 0; i < markCount; i++)
        free(markNames[i]);
    free(markNames);
    markNames = NULL;
    markCount = 0;
}

static int add_mark(const char *name, size_t length)
{
    char **newNames;
    char *copy;

    copy = malloc(length + 1);
    if (!copy)
        return -1;
    memcpy(copy, name, length);
    copy[length] = 0;
    newNames = realloc(markNames, (markCount + 1) * sizeof(*markNames));
    if (!newNames) {
        free(copy);
        return -1;
    }
    markNames = newNames;
    markNames[markCount++] = copy;
    return et_insertIndex(engine, (int32_t)markCount) ? 0 : -1;
}

static int add_icon_mark(void)
{
    char **newNames = realloc(markNames,
                              (markCount + 1) * sizeof(*markNames));

    if (!newNames)
        return -1;
    markNames = newNames;
    markNames[markCount++] = NULL;
    return et_insertIndex(engine, (int32_t)markCount) ? 0 : -1;
}

static int buffer_reserve(TextBuffer *buffer, size_t extra)
{
    size_t needed = buffer->length + extra + 1;
    size_t capacity;
    char *data;

    if (needed <= buffer->capacity)
        return 0;
    capacity = buffer->capacity ? buffer->capacity : 128;
    while (capacity < needed)
        capacity *= 2;
    data = realloc(buffer->data, capacity);
    if (!data)
        return -1;
    buffer->data = data;
    buffer->capacity = capacity;
    return 0;
}

static int buffer_append(TextBuffer *buffer, const char *text, size_t length)
{
    if (buffer_reserve(buffer, length) != 0)
        return -1;
    memcpy(buffer->data + buffer->length, text, length);
    buffer->length += length;
    buffer->data[buffer->length] = 0;
    return 0;
}

/* Every language OpenEVV currently ships uses IBM's ISO-8859-1 input path.
 * SSIP text is UTF-8, so convert it before handing it to the engine. Unknown
 * or malformed characters become question marks, matching the established
 * IBM Speech Dispatcher module's conversion fallback. */
static char *utf8_to_latin1(const char *text)
{
    TextBuffer converted = { 0 };
    const unsigned char *at = (const unsigned char *)text;

    while (*at) {
        uint32_t codepoint;
        size_t length;

        if (*at < 0x80) {
            codepoint = *at;
            length = 1;
        } else if ((*at & 0xe0) == 0xc0 && (at[1] & 0xc0) == 0x80) {
            codepoint = ((uint32_t)(at[0] & 0x1f) << 6)
                      | (uint32_t)(at[1] & 0x3f);
            length = codepoint >= 0x80 ? 2 : 1;
        } else if ((*at & 0xf0) == 0xe0 && (at[1] & 0xc0) == 0x80
                   && (at[2] & 0xc0) == 0x80) {
            codepoint = ((uint32_t)(at[0] & 0x0f) << 12)
                      | ((uint32_t)(at[1] & 0x3f) << 6)
                      | (uint32_t)(at[2] & 0x3f);
            length = codepoint >= 0x800 ? 3 : 1;
        } else if ((*at & 0xf8) == 0xf0 && (at[1] & 0xc0) == 0x80
                   && (at[2] & 0xc0) == 0x80
                   && (at[3] & 0xc0) == 0x80) {
            codepoint = 0x100;
            length = 4;
        } else {
            codepoint = 0x100;
            length = 1;
        }
        {
            char output = codepoint <= 0xff ? (char)codepoint : '?';

            if (buffer_append(&converted, &output, 1) != 0) {
                free(converted.data);
                return NULL;
            }
        }
        at += length;
    }
    if (!converted.data)
        return copy_string("");
    return converted.data;
}

static int latin1_uppercase(unsigned char value)
{
    return (value >= 'A' && value <= 'Z')
        || (value >= 0xc0 && value <= 0xd6)
        || (value >= 0xd8 && value <= 0xde);
}

static int add_text_with_capitals(const char *text)
{
    char *converted = utf8_to_latin1(text);
    char *segment;
    char *at;
    int result = 0;

    if (!converted)
        return -1;
    segment = converted;
    for (at = converted; *at && result == 0; at++) {
        char letter[2];

        if (settings.capitals == SPD_CAP_NONE
            || !latin1_uppercase((unsigned char)*at))
            continue;
        letter[0] = *at;
        letter[1] = 0;
        *at = 0;
        if (*segment && !et_addText(engine, segment))
            result = -1;
        if (result == 0 && settings.capitals == SPD_CAP_SPELL
            && !et_addText(engine, " capital "))
            result = -1;
        if (result == 0 && settings.capitals == SPD_CAP_ICON)
            result = add_icon_mark();
        *at = letter[0];
        if (result == 0 && !et_addText(engine, letter))
            result = -1;
        segment = at + 1;
    }
    if (result == 0 && *segment && !et_addText(engine, segment))
        result = -1;
    free(converted);
    return result;
}

static int flush_text(TextBuffer *buffer)
{
    int result;

    if (!buffer->length)
        return 0;
    result = add_text_with_capitals(buffer->data);
    buffer->length = 0;
    if (buffer->data)
        buffer->data[0] = 0;
    return result;
}

static int append_entity(TextBuffer *buffer, const char *entity, size_t length)
{
    struct Entity { const char *name; const char *value; };
    static const struct Entity entities[] = {
        { "amp", "&" }, { "lt", "<" }, { "gt", ">" },
        { "quot", "\"" }, { "apos", "'" },
    };
    size_t i;

    for (i = 0; i < sizeof(entities) / sizeof(entities[0]); i++)
        if (strlen(entities[i].name) == length
            && !strncmp(entity, entities[i].name, length))
            return buffer_append(buffer, entities[i].value, 1);
    return buffer_append(buffer, "?", 1);
}

static const char *find_attribute(const char *tag, size_t length,
                                  const char *attribute, size_t *valueLength)
{
    const char *at = tag;
    const char *end = tag + length;
    size_t nameLength = strlen(attribute);

    while (at + nameLength < end) {
        if (at > tag && isspace((unsigned char)at[-1])
            && !strncasecmp(at, attribute, nameLength)
            && (at + nameLength == end
                || at[nameLength] == '='
                || isspace((unsigned char)at[nameLength]))) {
            const char *value = at + nameLength;
            char quote;

            while (value < end && isspace((unsigned char)*value))
                value++;
            if (value >= end || *value != '=') {
                at++;
                continue;
            }
            value++;
            while (value < end && isspace((unsigned char)*value))
                value++;
            if (value >= end || (*value != '\'' && *value != '"')) {
                at++;
                continue;
            }
            quote = *value++;
            at = memchr(value, quote, (size_t)(end - value));
            if (!at)
                return NULL;
            *valueLength = (size_t)(at - value);
            return value;
        }
        at++;
    }
    return NULL;
}

static int tag_matches(const char *tag, size_t length, const char *name)
{
    size_t nameLength = strlen(name);
    char next;

    if (length < nameLength + 2 || tag[0] != '<' || tag[1] == '/')
        return 0;
    if (strncasecmp(tag + 1, name, nameLength))
        return 0;
    next = tag[nameLength + 1];
    return next == '>' || next == '/' || isspace((unsigned char)next);
}

static int contains_tag(const char *text, const char *name)
{
    const char *tag = text;

    while ((tag = strchr(tag, '<')) != NULL) {
        const char *end = strchr(tag, '>');

        if (!end)
            return 0;
        if (tag_matches(tag, (size_t)(end - tag + 1), name))
            return 1;
        tag = end + 1;
    }
    return 0;
}

static int queue_ssml(const char *data)
{
    TextBuffer text = { 0 };
    const char *at = data;
    int result = 0;

    while (*at && result == 0) {
        if (*at == '<') {
            const char *end = strchr(at, '>');
            size_t length;

            if (!end) {
                result = buffer_append(&text, at, strlen(at));
                break;
            }
            length = (size_t)(end - at + 1);
            if (tag_matches(at, length, "mark")) {
                size_t nameLength;
                const char *name = find_attribute(at, length, "name",
                                                  &nameLength);

                if (name) {
                    result = flush_text(&text);
                    if (result == 0)
                        result = add_mark(name, nameLength);
                }
            } else if (tag_matches(at, length, "break")) {
                result = buffer_append(&text, ". ", 2);
            }
            at = end + 1;
        } else if (*at == '&') {
            const char *end = strchr(at + 1, ';');

            if (end) {
                result = append_entity(&text, at + 1,
                                       (size_t)(end - at - 1));
                at = end + 1;
            } else {
                result = buffer_append(&text, at++, 1);
            }
        } else {
            const char *end = strpbrk(at, "<&");
            size_t length = end ? (size_t)(end - at) : strlen(at);

            result = buffer_append(&text, at, length);
            at += length;
        }
    }
    if (result == 0)
        result = flush_text(&text);
    free(text.data);
    return result;
}

static char *key_text(const char *data)
{
    TextBuffer text = { 0 };
    const char *at = data;

    if (!strcmp(data, " "))
        return copy_string("space");
    if (!strncasecmp(at, "KP_", 3)) {
        if (buffer_append(&text, "keypad ", 7) != 0)
            goto failed;
        at += 3;
    }
    while (*at) {
        char value = *at++;

        if (value == '_' || value == '-')
            value = ' ';
        if (buffer_append(&text, &value, 1) != 0)
            goto failed;
    }
    return text.data;

failed:
    free(text.data);
    return NULL;
}

static int queue_message(const char *data, SPDMessageType messageType)
{
    char *prepared = NULL;
    const char *message = data;
    int textMode = TEXT_MODE_DEFAULT;
    int result;

    if (messageType == SPD_MSGTYPE_SOUND_ICON) {
        pendingIcon = copy_string(data);
        prepared = key_text(data);
        message = prepared;
    } else if (messageType == SPD_MSGTYPE_KEY) {
        prepared = key_text(data);
        message = prepared;
    } else if (messageType == SPD_MSGTYPE_CHAR) {
        if (!strcmp(data, " ")) {
            prepared = copy_string("space");
            message = prepared;
        } else {
            textMode = TEXT_MODE_ALL_SPELL;
        }
    }
    if (settings.spelling == SPD_SPELL_ON)
        textMode = messageType == SPD_MSGTYPE_TEXT
                 ? TEXT_MODE_ALPHA_SPELL : TEXT_MODE_ALL_SPELL;
    if (!message || (messageType == SPD_MSGTYPE_SOUND_ICON && !pendingIcon)
        || ev_setParam(engine, PARAM_TEXT_MODE, textMode) < 0) {
        free(prepared);
        return -1;
    }

    if (contains_tag(message, "speak")
        || contains_tag(message, "mark")
        || contains_tag(message, "break"))
        result = queue_ssml(message);
    else
        result = add_text_with_capitals(message);
    free(prepared);
    return result;
}

static int allocate_voice_list(void)
{
    size_t total = (size_t)languageCount * VOICES_PER_LANGUAGE;
    size_t at = 0;
    int languageIndex;

    speechdVoices = calloc(total + 1, sizeof(*speechdVoices));
    if (!speechdVoices)
        return -1;
    for (languageIndex = 0; languageIndex < languageCount; languageIndex++) {
        const LanguageInfo *info = language_info(availableLanguages[languageIndex]);
        int voice;

        if (!info)
            continue;
        for (voice = 0; voice < VOICES_PER_LANGUAGE; voice++) {
            size_t nameLength = strlen(info->tag) + strlen(voiceNames[voice]) + 2;
            SPDVoice *entry = calloc(1, sizeof(*entry));

            if (!entry)
                return -1;
            entry->name = malloc(nameLength);
            entry->language = copy_string(info->locale);
            entry->variant = copy_string(voiceNames[voice]);
            if (!entry->name || !entry->language || !entry->variant) {
                free(entry->name);
                free(entry->language);
                free(entry->variant);
                free(entry);
                return -1;
            }
            snprintf(entry->name, nameLength, "%s-%s", info->tag,
                     voiceNames[voice]);
            speechdVoices[at++] = entry;
        }
    }
    speechdVoices[at] = NULL;
    return 0;
}

static void free_voice_list(void)
{
    size_t i;

    if (!speechdVoices)
        return;
    for (i = 0; speechdVoices[i]; i++) {
        free(speechdVoices[i]->name);
        free(speechdVoices[i]->language);
        free(speechdVoices[i]->variant);
        free(speechdVoices[i]);
    }
    free(speechdVoices);
    speechdVoices = NULL;
}

int module_config(const char *configFile)
{
    FILE *file;
    char line[512];

    if (!configFile || !*configFile)
        return 0;
    file = fopen(configFile, "r");
    if (!file)
        return -1;
    while (fgets(line, sizeof(line), file)) {
        char *at = line;
        char *value;

        while (isspace((unsigned char)*at))
            at++;
        if (!*at || *at == '#')
            continue;
        value = at + strcspn(at, " \t\r\n");
        if (*value)
            *value++ = 0;
        while (isspace((unsigned char)*value))
            value++;
        value[strcspn(value, "\r\n")] = 0;
        if (!strcasecmp(at, "Debug")) {
            int enabled;

            if (parse_number(value, 0, 1, &enabled) != 0) {
                fclose(file);
                return -1;
            }
            debugEnabled = enabled;
        } else {
            fclose(file);
            return -1;
        }
    }
    fclose(file);
    return 0;
}

int module_init(char **statusInfo)
{
    languageCount = MAX_LANGUAGES;
    module_audio_set_server();
    evv_port_start();
    engineStarted = 1;
    evvRunStaticInitialisers();
    if (eo_getAvailableLanguages(availableLanguages, &languageCount)
        || languageCount < 1) {
        *statusInfo = copy_string("OpenEVV has no built language modules");
        return -1;
    }
    if (allocate_voice_list() != 0
        || open_engine(availableLanguages[0]) != 0) {
        *statusInfo = copy_string("OpenEVV could not initialize its engine");
        return -1;
    }
    *statusInfo = copy_string("OpenEVV initialized");
    debug_log("initialized");
    return 0;
}

SPDVoice **module_list_voices(void)
{
    return speechdVoices;
}

void module_speak_sync(const char *data, size_t bytes,
                       SPDMessageType messageType)
{
    char *message;

    stopRequested = 0;
    pauseRequested = 0;
    pauseIndexReached = 0;
    clear_marks();
    free(pendingIcon);
    pendingIcon = NULL;
    if (bytes == SIZE_MAX || !(message = malloc(bytes + 1))) {
        module_speak_error();
        return;
    }
    memcpy(message, data, bytes);
    message[bytes] = 0;
    if (configure_engine() != 0 || queue_message(message, messageType) != 0) {
        free(message);
        module_speak_error();
        return;
    }
    free(message);
    module_speak_ok();
    module_report_event_begin();
    if (pendingIcon)
        module_report_icon(pendingIcon);
    if (!et_synthesize(engine)) {
        module_report_event_stop();
        return;
    }
    while (eo_speaking(engine)) {
        struct timespec delay = { .tv_sec = 0, .tv_nsec = 1000000L };

        module_process(STDIN_FILENO, 0);
        nanosleep(&delay, NULL);
    }
    eo_synchronizeSynth(engine);
    if (pauseRequested)
        module_report_event_pause();
    else if (stopRequested)
        module_report_event_stop();
    else
        module_report_event_end();
}

int module_stop(void)
{
    stopRequested = 1;
    return 0;
}

size_t module_pause(void)
{
    pauseRequested = 1;
    return 0;
}

int module_close(void)
{
    if (engine) {
        es_delete(engine);
        engine = NULL;
    }
    clear_marks();
    free(pendingIcon);
    pendingIcon = NULL;
    free_voice_list();
    free(settings.language);
    settings.language = NULL;
    free(settings.synthesisVoice);
    settings.synthesisVoice = NULL;
    if (debugFile) {
        fclose(debugFile);
        debugFile = NULL;
    }
    if (engineStarted) {
        evv_port_finish();
        engineStarted = 0;
    }
    return 0;
}

int module_set(const char *variable, const char *value)
{
    int parsed;

    if (!strcmp(variable, "rate"))
        return parse_number(value, -100, 100, &settings.rate);
    if (!strcmp(variable, "pitch"))
        return parse_number(value, -100, 100, &settings.pitch);
    if (!strcmp(variable, "pitch_range"))
        return parse_number(value, -100, 100, &settings.pitchRange);
    if (!strcmp(variable, "volume"))
        return parse_number(value, -100, 100, &settings.volume);
    if (!strcmp(variable, "punctuation_mode")) {
        if (!strcmp(value, "all")) settings.punctuation = SPD_PUNCT_ALL;
        else if (!strcmp(value, "none")) settings.punctuation = SPD_PUNCT_NONE;
        else if (!strcmp(value, "some")) settings.punctuation = SPD_PUNCT_SOME;
        else if (!strcmp(value, "most")) settings.punctuation = SPD_PUNCT_MOST;
        else return -1;
        return 0;
    }
    if (!strcmp(variable, "spelling_mode")) {
        if (!strcmp(value, "on")) settings.spelling = SPD_SPELL_ON;
        else if (!strcmp(value, "off")) settings.spelling = SPD_SPELL_OFF;
        else return -1;
        return 0;
    }
    if (!strcmp(variable, "cap_let_recogn")) {
        if (!strcmp(value, "none")) settings.capitals = SPD_CAP_NONE;
        else if (!strcmp(value, "spell")) settings.capitals = SPD_CAP_SPELL;
        else if (!strcmp(value, "icon")) settings.capitals = SPD_CAP_ICON;
        else return -1;
        return 0;
    }
    if (!strcmp(variable, "voice")) {
        static const char *const names[] = {
            "male1", "male2", "male3", "female1", "female2", "female3",
            "child_male", "child_female"
        };

        for (parsed = 0; parsed < 8; parsed++)
            if (!strcmp(value, names[parsed])) {
                settings.voiceType = (SPDVoiceType)(parsed + 1);
                return 0;
            }
        return -1;
    }
    if (!strcmp(variable, "language")) {
        char *copy = copy_string(value);

        if (strcmp(value, "NULL") && !copy)
            return -1;
        free(settings.language);
        settings.language = copy;
        return 0;
    }
    if (!strcmp(variable, "synthesis_voice")) {
        char *copy = copy_string(value);

        if (strcmp(value, "NULL") && !copy)
            return -1;
        free(settings.synthesisVoice);
        settings.synthesisVoice = copy;
        return 0;
    }
    return -1;
}

int module_audio_set(const char *variable, const char *value)
{
    return !strcmp(variable, "audio_output_method")
        && !strcmp(value, "server") ? 0 : -1;
}

int module_audio_init(char **statusInfo)
{
    (void)statusInfo;
    return 0;
}

int module_loglevel_set(const char *variable, const char *value)
{
    if (strcmp(variable, "log_level"))
        return -1;
    return parse_number(value, 0, 5, &logLevel);
}

int module_debug(int enable, const char *filename)
{
    FILE *file = NULL;

    if (enable) {
        if (!filename)
            return -1;
        file = fopen(filename, "w");
        if (!file)
            return -1;
    }
    if (debugFile)
        fclose(debugFile);
    debugFile = file;
    debugEnabled = enable;
    return 0;
}

int module_loop(void)
{
    return module_process(STDIN_FILENO, 1);
}
