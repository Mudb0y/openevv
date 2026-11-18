/* The SSML reader: one handler per element, and what each writes.
 *
 * The XML scanner hands three things over -- an element opened with its
 * attributes, an element closed, and a run of character data -- and this is
 * what does something with them. Everything it does is to write
 * annotations into the answer through `addToFilteredText' and to push and
 * pop the stacks in src/eci/ssml/eci_ssmlstate.c, so that what an element changed
 * is put back when it closes.
 *
 * Three things run through the whole file and are worth knowing before
 * reading any of it.
 *
 * The language decides what is allowed. Not every language IBM shipped
 * supports prosody, or rate, or emphasis, or say-as, and each has its own
 * list; a document asking for one in a language that has not got it is
 * ignored rather than refused. And a language the engine does not have at
 * all blocks text outright -- `canAddText' answers no and every handler
 * falls through -- which is why <speak xml:lang="en-GB"> comes out empty
 * from a build with only US English in it.
 *
 * Closing an element does not undo an annotation, because the engine has
 * no undo. It writes the annotation that puts the parameter back and then
 * re-applies every enclosing one, walking the stack from the bottom. So
 * three nested <prosody rate> elements produce three annotations going in
 * and, on the innermost close, a reset plus two more.
 *
 * An element that cannot be honoured records an error, and an error stops
 * the whole document: `getFilteredText' answers nothing at all once one is
 * set. That is why a document with one bad attribute in it produces
 * silence rather than the parts that were fine.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include "evv_abi.h"
#include "eci_ssml.h"
#include "eci_ssmlstate.h"
#include "eci_ssmlmap.h"
#include "eci_vcinfo.h"

extern int ralStrIcmp(int n, const char *a, const char *b);
extern void *cpp_new(uint32_t n) MANGLED("??2@YAPAXI@Z");
extern void  cpp_delete(void *p) MANGLED("??3@YAXPAX@Z");
extern int32_t IPAToSPR(uint8_t *utf8, uint32_t bytes, char *spr,
                        uint32_t *room, int32_t lang);

/* How much room an annotation gets beyond the value it carries. */
#define ANNOTATION_ROOM  0x32
#define MARK_ROOM        0x1e

/* What a spelt-out character costs at most: the two annotations and the
   character between them. */
#define SPELL_WIDTH      0xd

/* The bell that brackets a narrowed multi-byte character; see
   src/eci/ssml/eci_mbconvert.c. */
#define NARROW_MARK      7

/* ---- which languages have what --------------------------------------- */

static int8_t inLanguages(LanguageId *id, const int32_t *list, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        if (li_compareLanguage(id, list[i]))
            return 1;
    return 0;
}

/* Pitch, range and volume. Polish is on the end of this list and of the two
   below and is not IBM's: what these three turn into is annotations, and an
   annotation means the same thing in every language the engine has. What
   Polish is deliberately not on is `langSupportSayAs' and
   `langSupportIPA' below, and for the opposite reason -- a say-as
   annotation is read by the language's own rules and Polish's are still
   Italian's, which IBM did not list either, and there is no Polish IPA
   converter to route to. Adding it to those two would produce a confident
   wrong reading rather than nothing. */
int8_t langSupportProsody(LanguageId id)
{
    static const int32_t LANGS[] = {
        0x10000, 0x10001, 0x30000, 0x40000, 0x110000
    };

    return inLanguages(&id, LANGS, sizeof LANGS / sizeof LANGS[0]);
}

/* Rate, which Japanese has and the others above do too. */
int8_t langSupportProsodyRate(LanguageId id)
{
    static const int32_t LANGS[] = {
        0x10000, 0x10001, 0x30000, 0x40000, 0x80000, 0x110000
    };

    return inLanguages(&id, LANGS, sizeof LANGS / sizeof LANGS[0]);
}

/* Emphasis is a pause and a slowing, which every language can do; IBM's
   list is every language it shipped, so Polish belongs on it. */
int8_t langSupportEmphasis(LanguageId id)
{
    static const int32_t LANGS[] = {
        0x10000, 0x10001, 0x30000, 0x40000, 0x20000, 0x50000, 0x70000,
        0x90000, 0x20001, 0x30001, 0x110000
    };

    return inLanguages(&id, LANGS, sizeof LANGS / sizeof LANGS[0]);
}

/* Every kind of say-as wants one of these seven, which is why a date in
   Italian is ignored where a date in German is read. */
static int8_t langSupportSayAs(LanguageId id)
{
    static const int32_t LANGS[] = {
        0x10000, 0x10001, 0x30000, 0x40000, 0xa0000, 0x80000, 0x60000
    };

    return inLanguages(&id, LANGS, sizeof LANGS / sizeof LANGS[0]);
}

/* And IPA, which is the six converters' own languages. */
static int8_t langSupportIPA(LanguageId id)
{
    static const int32_t LANGS[] = {
        0x10000, 0x10001, 0x30000, 0x40000, 0x80000, 0xa0000
    };

    return inLanguages(&id, LANGS, sizeof LANGS / sizeof LANGS[0]);
}

/* Whether the engine actually has a language, asked of the engine rather
   than of a table. */
int8_t isLanguageAvailable(LanguageId id)
{
    extern int32_t lg_eciGetAvailableLanguages2(uint32_t *languages,
                                                int32_t *count);
    int32_t   count = 0;
    uint32_t *languages;
    int8_t    found = 0;
    int32_t   i;

    if (lg_eciGetAvailableLanguages2(0, &count) == 0x80)
        return 0;
    if (count == 0)
        return 0;

    languages = cpp_new((uint32_t)count * 4);
    if (lg_eciGetAvailableLanguages2(languages, &count) == 0x80) {
        cpp_delete(languages);
        return 0;
    }

    for (i = 0; i < count; i++)
        if (li_compareLanguage(&id, (int32_t)languages[i])) {
            found = 1;
            break;
        }

    cpp_delete(languages);
    return found;
}

/* ---- putting every enclosing setting back ---------------------------- */

/* Walk a string stack from the bottom and write out every entry that says
   something. This is what an element's close does after resetting a
   parameter, and what a voice change does so that the new voice starts
   with the prosody the document asked for. */
#define REAPPLY_STR(state, kind)                                          \
    do {                                                                  \
        int32_t at;                                                       \
                                                                          \
        if (ss_size##kind(state) > 0)                                     \
            for (at = 1; at <= ss_size##kind(state); at++) {              \
                char *had = ss_peek##kind##At(state, at);                 \
                                                                          \
                if (had != 0 && ralStrIcmp(0, had, "") != 0)              \
                    ss_addToFilteredText(state, had,                      \
                                         (int32_t)strlen(had));           \
            }                                                             \
    } while (0)

/* Everything the document has asked for, written out again. Called when
   the voice changes, because a voice carries its own parameters and the
   annotations that set them do not survive it. */
void applyProsody(SSMLState *s)
{
    if (langSupportProsody(ss_peekLang(s))) {
        REAPPLY_STR(s, VoicePitch);
        REAPPLY_STR(s, VoiceRange);

        if (ss_sizeVoiceVolume(s) > 1) {
            char one[ANNOTATION_ROOM];

            sprintf(one, " `vv%d ", ss_peekVoiceVolume(s));
            ss_addToFilteredText(s, one, (int32_t)strlen(one));
        }
    }

    if (langSupportProsodyRate(ss_peekLang(s)))
        REAPPLY_STR(s, VoiceSpeed);
}

/* ---- xml:lang -------------------------------------------------------- */

/* A language given on an element. Answers whether an annotation was
   written, which is what tells the caller the language actually changed.

   A language the engine has not got is pushed all the same, with its
   availability cleared, so that everything inside the element is silent
   and the close still has something to pop. */
int8_t xmllangOpen(SSMLState *s, char *value)
{
    int8_t     changed = 0;
    int32_t    length = (int32_t)strlen(value);
    int32_t    packed;
    LanguageId id;

    if (length < 2 || length > 5) {
        li_initPacked(&id, 0);
        li_setIsLanguageAvailable(&id, 0);
        ss_pushLang(s, id);
        return 0;
    }

    packed = mapToIBMlang(value, length);
    li_initPacked(&id, packed);

    if (packed == 0 || !isLanguageAvailable(id)) {
        li_setIsLanguageAvailable(&id, 0);
        ss_pushLang(s, id);
        return 0;
    }

    li_setIsLanguageAvailable(&id, 1);

    {
        LanguageId now = ss_peekLang(s);

        if (!li_compareLanguage(&now, packed)) {
            char one[ANNOTATION_ROOM];

            strcpy(one, " `l");
            strcat(one, li_getString(&id));
            strcat(one, " ");
            ss_addToFilteredText(s, one, (int32_t)strlen(one));
            changed = 1;
        }
    }

    ss_pushLang(s, id);
    return changed;
}

/* And the language coming back off. An annotation is only written when
   both languages are ones the engine has, since otherwise nothing inside
   either element was said. */
int8_t xmllangClose(SSMLState *s)
{
    LanguageId was = ss_popLang(s);
    LanguageId now = ss_peekLang(s);

    if (!li_equals(&was, &now) && li_getIsLanguageAvailable(&now)
        && li_getIsLanguageAvailable(&was)) {
        char one[ANNOTATION_ROOM];

        strcpy(one, " `l");
        strcat(one, li_getString(&now));
        strcat(one, " ");
        ss_addToFilteredText(s, one, (int32_t)strlen(one));
        return 1;
    }

    return 0;
}

/* ---- the small elements ---------------------------------------------- */

/* <mark name="..."> becomes the index annotation, which the engine reports
   back to the caller when it reaches that point in the speech. */
void markOpen(SSMLState *s, char *name)
{
    char *one = malloc(strlen(name) + MARK_ROOM);

    sprintf(one, " `ui\"%s\" ", name);
    ss_addToFilteredText(s, one, (int32_t)strlen(one));
    if (one != 0)
        free(one);
}

/* <audio src="..."> becomes a pair of marks round its fallback text, so
   that a caller can play the file itself. The source is pushed whether or
   not an annotation was written, because the close pops it either way. */
void audioOpen(SSMLState *s, char *src)
{
    char *one = malloc(strlen(src) + MARK_ROOM);

    if (ralStrIcmp(0, src, "") != 0) {
        sprintf(one, " `aud\"BEGIN_AUDIO:%s\" ", src);
        ss_addToFilteredText(s, one, (int32_t)strlen(one));
    }

    ss_pushAudio(s, src);

    if (one != 0)
        free(one);
}

void audioClose(SSMLState *s)
{
    char *src = ss_popAudio(s);

    if (src == 0)
        return;

    if (ralStrIcmp(0, src, "") != 0) {
        char *one = malloc(strlen(src) + MARK_ROOM);

        sprintf(one, " `aud\"CLOSE_AUDIO:%s\" ", src);
        ss_addToFilteredText(s, one, (int32_t)strlen(one));
        free(src);
        if (one != 0)
            free(one);
        return;
    }

    free(src);
}

/* ---- emphasis -------------------------------------------------------- */

/* Emphasis is a pause and, for `strong', a slowing as well -- the engine
   has no emphasis of its own. An unknown level is `moderate' rather than
   an error. */
void openEmphasis(SSMLState *s, const char **atts)
{
    char *level = getAttributeValue(atts, "LEVEL");
    char  one[ANNOTATION_ROOM];

    if (level != 0) {
        if (mapToIBMlevel(level, one) == -1)
            mapToIBMlevel("moderate", one);
    } else {
        mapToIBMlevel("moderate", one);
    }

    ss_pushEmphasis(s, one);
    ss_addToFilteredText(s, one, (int32_t)strlen(one));
}

/* Closing it puts back what it changed. The pause needs no undoing beyond
   another pause; the slowing does, and then every enclosing rate and
   emphasis has to be written again. */
void closeEmphasis(SSMLState *s)
{
    char *had;

    if (ss_sizeEmphasis(s) <= 0)
        return;

    had = ss_popEmphasis(s);
    if (had == 0)
        return;

    if (strstr(had, "`p1") != 0)
        ss_addToFilteredText(s, " `p1 ", (int32_t)strlen(" `p1 "));

    if (strstr(had, "`vs") != 0) {
        ss_addToFilteredText(s, " `vsmed ", (int32_t)strlen(" `vsmed "));
        REAPPLY_STR(s, VoiceSpeed);
        REAPPLY_STR(s, Emphasis);
    }

    free(had);
}

/* ---- prosody --------------------------------------------------------- */

/* Every attribute is pushed whether or not it was given, so that the close
   pops the same number of things it pushed: an attribute that was not
   there pushes the empty string, which the close knows to ignore.

   Volume is the one that is not like the others. It is a number rather
   than a string, the engine's own setting is needed to work out what a
   percentage means, and the annotation is only written when the answer
   differs from what was already in force. */
void openProsody(SSMLState *s, const char **atts)
{
    int8_t any = 0;
    int8_t support;
    char  *value;

    support = langSupportProsody(ss_peekLang(s));

    value = getAttributeValue(atts, "PITCH");
    if (value != 0) {
        any = 1;
        if (support) {
            char *one = malloc(strlen(value) + ANNOTATION_ROOM);

            if (mapToIBMetiPitch(value, one) == -1) {
                ss_setErrorSyntax(s);
            } else {
                ss_pushVoicePitch(s, one);
                ss_addToFilteredText(s, one, (int32_t)strlen(one));
            }
            if (one != 0)
                free(one);
        } else {
            ss_pushVoicePitch(s, "");
        }
    } else {
        ss_pushVoicePitch(s, "");
    }

    value = getAttributeValue(atts, "RANGE");
    if (value != 0) {
        any = 1;
        if (support) {
            char *one = malloc(strlen(value) + ANNOTATION_ROOM);

            if (mapToIBMetiRange(value, one) == -1) {
                ss_setErrorSyntax(s);
            } else {
                ss_pushVoiceRange(s, one);
                ss_addToFilteredText(s, one, (int32_t)strlen(one));
            }
            if (one != 0)
                free(one);
        } else {
            ss_pushVoiceRange(s, "");
        }
    } else {
        ss_pushVoiceRange(s, "");
    }

    support = langSupportProsodyRate(ss_peekLang(s));

    value = getAttributeValue(atts, "RATE");
    if (value != 0) {
        any = 1;
        if (support) {
            char *one = malloc(strlen(value) + ANNOTATION_ROOM);

            if (mapToIBMetiSpeed(value, one) == -1) {
                ss_setErrorSyntax(s);
            } else {
                ss_pushVoiceSpeed(s, one);
                ss_addToFilteredText(s, one, (int32_t)strlen(one));
            }
            if (one != 0)
                free(one);
        } else {
            ss_pushVoiceSpeed(s, "");
        }
    } else {
        ss_pushVoiceSpeed(s, "");
    }

    value = getAttributeValue(atts, "VOLUME");
    if (value != 0) {
        char    one[ANNOTATION_ROOM];
        int32_t want;

        any = 1;
        want = mapToIBMetiVolume(value, one, ss_peekVoiceVolume(s));
        if (want == -1) {
            ss_pushVoiceVolume(s, ss_peekVoiceVolume(s));
            ss_setErrorSyntax(s);
        } else {
            if (ss_peekVoiceVolume(s) != want)
                ss_addToFilteredText(s, one, (int32_t)strlen(one));
            ss_pushVoiceVolume(s, want);
        }
    } else {
        ss_pushVoiceVolume(s, ss_peekVoiceVolume(s));
    }

    /* Two the engine has nothing to say with, and one element that asked
       for nothing at all. */
    if (getAttributeValue(atts, "DURATION") != 0) {
        any = 1;
        ss_setErrorSyntax(s);
    }
    if (getAttributeValue(atts, "CONTOUR") != 0) {
        any = 1;
        ss_setErrorSyntax(s);
    }
    if (!any)
        ss_setErrorSyntax(s);
}

/* Reset whichever of the three said something, then write every enclosing
   one again. Volume is a number and goes back to what is under it. */
#define CLOSE_STR(state, kind, reset)                                     \
    do {                                                                  \
        if (ss_size##kind(state) > 0) {                                   \
            char *had = ss_pop##kind(state);                              \
                                                                          \
            if (had == 0) {                                               \
                ss_setErrorSyntax(state);                                 \
            } else if (ralStrIcmp(0, had, "") != 0) {                     \
                ss_addToFilteredText(state, reset,                        \
                                     (int32_t)strlen(reset));             \
                REAPPLY_STR(state, kind);                                 \
            }                                                             \
            if (had != 0)                                                 \
                free(had);                                                \
        }                                                                 \
    } while (0)

void closeProsody(SSMLState *s)
{
    CLOSE_STR(s, VoicePitch, " `vbmed ");
    CLOSE_STR(s, VoiceRange, " `vfmed ");
    CLOSE_STR(s, VoiceSpeed, " `vsmed ");

    if (ss_sizeVoiceVolume(s) > 0) {
        int32_t was = ss_popVoiceVolume(s);
        int32_t now = ss_peekVoiceVolume(s);

        if (now != was && was != -1 && now != -1) {
            char one[ANNOTATION_ROOM];

            sprintf(one, " `vv%d ", now);
            ss_addToFilteredText(s, one, (int32_t)strlen(one));
        }
    }
}

/* ---- the voice ------------------------------------------------------- */

/* Write the annotation for a voice, if it is not the one already in force,
   and push it either way. */
static int8_t voiceBecomes(SSMLState *s, int32_t want, int32_t current)
{
    int8_t changed = 0;

    if (want == -1) {
        want = current;
    } else if (want != current) {
        char one[ANNOTATION_ROOM];

        sprintf(one, " `v%d ", want);
        ss_addToFilteredText(s, one, (int32_t)strlen(one));
        changed = 1;
    }

    ss_pushVoiceNumber(s, want);
    return changed;
}

/* A variant asks for another voice of the same kind. Where the voice in
   force is already one of the variants -- four or five under voice one,
   six under voice two -- the variant is taken against the voice in force
   rather than against the one the age asked for, which is IBM's own
   arrangement and is what lets a document step between them. */
static int32_t variantOf(const char *variant, int32_t want, int32_t current)
{
    if (((current == 4 || current == 5) && want == 1)
        || (current == 6 && want == 2))
        return mapToIBMvariant(variant, current);
    return mapToIBMvariant(variant, want);
}

/* <voice>. Its attributes are a name, a gender, an age and a variant, in
   seven combinations, and each says a voice number a different way.

   The name is the odd one out: it is looked up in the voice table rather
   than worked out, and on this extraction that table has no names in it --
   see src/eci/lang/eci_vcinfo.c. A name is split on spaces and each word offered in
   turn, so a name of several words matches a voice named by any of them. */
void openVoice(SSMLState *s, const char **atts)
{
    int8_t  changed = 0;
    char   *lang;
    char   *gender;
    char   *age;
    char   *variant;
    char   *name;
    int32_t current;

    lang = getAttributeValue(atts, "XML:LANG");
    if (lang != 0)
        changed = xmllangOpen(s, lang);
    else
        ss_pushLang(s, ss_peekLang(s));

    gender  = getAttributeValue(atts, "GENDER");
    age     = getAttributeValue(atts, "AGE");
    variant = getAttributeValue(atts, "VARIANT");
    name    = getAttributeValue(atts, "NAME");

    current = ss_peekVoiceNumber(s);

    if (name != 0) {
        int32_t want = -1;
        char   *word;

        word = strtok(name, " ");
        while (word != 0) {
            VOICE_INFO info;

            memset(&info, 0, sizeof info);
            strcpy(info.name, word);

            if (ss_getVoiceInfo(s, &info, 1) != 0) {
                /* Nothing of that name; keep looking. */
                word = strtok(0, " ");
                if (word == 0)
                    want = current;
                continue;
            }

            if (info.voice == current) {
                want = current;
                word = 0;
                continue;
            }

            want = info.voice;
            {
                char one[ANNOTATION_ROOM];

                sprintf(one, " `v%d ", info.voice);
                ss_addToFilteredText(s, one, (int32_t)strlen(one));
            }
            changed = 1;
            word = 0;
        }

        ss_pushVoiceNumber(s, want);
    } else if (gender != 0 && age != 0 && variant != 0) {
        int32_t g = mapToIBMgender(gender);
        int32_t want;

        if (g == -1)
            g = getVoiceGender(ss_peekVoiceNumber(s));
        want = mapToIBMage(age, g);
        current = ss_peekVoiceNumber(s);
        want = variantOf(variant, want, current);
        changed |= voiceBecomes(s, want, current);
    } else if (age != 0 && variant != 0) {
        int32_t g = getVoiceGender(ss_peekVoiceNumber(s));
        int32_t want = mapToIBMage(age, g);

        current = ss_peekVoiceNumber(s);
        want = variantOf(variant, want, current);
        changed |= voiceBecomes(s, want, current);
    } else if (gender != 0 && variant != 0) {
        int32_t g = mapToIBMgender(gender);
        int32_t want;

        current = ss_peekVoiceNumber(s);
        want = mapGenderToVoice(current, g);
        want = variantOf(variant, want, current);
        changed |= voiceBecomes(s, want, current);
    } else if (gender != 0 && age != 0) {
        int32_t g = mapToIBMgender(gender);
        int32_t want;

        if (g == -1)
            g = getVoiceGender(ss_peekVoiceNumber(s));
        want = mapToIBMage(age, g);
        current = ss_peekVoiceNumber(s);
        changed |= voiceBecomes(s, want, current);
    } else if (age != 0) {
        int32_t g = getVoiceGender(ss_peekVoiceNumber(s));
        int32_t want = mapToIBMage(age, g);

        current = ss_peekVoiceNumber(s);
        changed |= voiceBecomes(s, want, current);
    } else if (gender != 0) {
        int32_t g = mapToIBMgender(gender);
        int32_t want;

        current = ss_peekVoiceNumber(s);
        want = mapGenderToVoice(current, g);
        changed |= voiceBecomes(s, want, current);
    } else if (variant != 0) {
        int32_t want;

        current = ss_peekVoiceNumber(s);
        want = mapToIBMvariant(variant, current);
        changed |= voiceBecomes(s, want, current);
    } else if (lang != 0) {
        /* A voice element that only changed the language still has to
           push something for the close to pop. */
        ss_pushVoiceNumber(s, ss_peekVoiceNumber(s));
    } else {
        ss_setErrorSyntax(s);
    }

    if (changed)
        applyProsody(s);
}

/* And the voice coming back off. A voice carries its own parameters, so
   everything the document asked for has to be written again. */
void closeVoice(SSMLState *s)
{
    int8_t  changed;
    int32_t was;
    int32_t now;

    changed = xmllangClose(s);

    was = ss_popVoiceNumber(s);
    now = ss_peekVoiceNumber(s);

    if (was == -1 || now == -1) {
        ss_setErrorSyntax(s);
    } else if (was != now) {
        char one[ANNOTATION_ROOM];

        sprintf(one, " `v%d ", now);
        ss_addToFilteredText(s, one, (int32_t)strlen(one));
        changed = 1;
    }

    if (changed)
        applyProsody(s);
}

/* ---- a pronunciation in IPA ------------------------------------------ */

/* The phonemes arrive narrowed, because they came through the scanner, so
   they are widened, converted a word at a time, and narrowed again on the
   way into the answer. Each word becomes one ` `[...] ' annotation. */
void ipaPhonemeConvert(SSMLState *s, char *ph)
{
    char *wide = cpp_new((uint32_t)strlen(ph) + 6);
    char *spr  = cpp_new((uint32_t)strlen(ph) * 6 + 6);
    char *at;

    if (wide == 0 || spr == 0) {
        ss_setErrorMalloc(s);
        if (wide != 0)
            cpp_delete(wide);
        if (spr != 0)
            cpp_delete(spr);
        return;
    }

    if (Sbcs2Mbcs(ph, wide) == -1) {
        cpp_delete(wide);
        cpp_delete(spr);
        return;
    }

    at = wide;
    while (*at != 0) {
        char    *end;
        char     narrow[0x200];
        uint32_t room = 0x100;
        char     out[0x100];

        while (*at == ' ' || *at == '\t')
            at++;
        if (*at == 0)
            break;

        end = strchr(at, ' ');
        if (end != 0)
            *end = 0;

        {
            LanguageId now = ss_peekLang(s);

            room = 0x100;
            memset(out, 0, sizeof out);
            if (IPAToSPR((uint8_t *)at, (uint32_t)strlen(at), out, &room,
                         li_getPackedInt(&now)) == 0 && out[0] != 0) {
                char one[0x220];

                if (Mbcs2Sbcs(out, narrow) != -1) {
                    strcpy(one, " `[");
                    strncat(one, narrow, sizeof one - 8);
                    strcat(one, "]");
                    ss_addToFilteredText(s, one, (int32_t)strlen(one));
                }
            }
        }

        if (end == 0)
            break;
        at = end + 1;
    }

    cpp_delete(wide);
    cpp_delete(spr);
}

/* ---- say-as ---------------------------------------------------------- */

/* What kind of reading is wanted. Most of these open an annotation that
   the text handler fills and `closeSayAs' closes with a bracket; two --
   letters and digits -- are spelt out character by character instead, and
   one -- the VoiceXML date -- is built whole by the text handler because
   the annotation names which fields are present. */
void openSayAs(SSMLState *s, const char **atts)
{
    char  *what = getAttributeValue(atts, "INTERPRET-AS");
    int8_t support;

    if (what == 0) {
        ss_setErrorSyntax(s);
        return;
    }

    if (ralStrIcmp(0, what, "LETTERS") == 0
        || ralStrIcmp(0, what, "DIGITS") == 0
        || ralStrIcmp(0, what, "VXML:DIGITS") == 0) {
        ss_setSpellOut(s);
        return;
    }

    support = langSupportSayAs(ss_peekLang(s));

    /* A date wants a format, and one the writer recognises. Without both
       the element does nothing at all -- not even the bracket, since
       nothing was opened for the close to shut. */
    if (ralStrIcmp(0, what, "DATE") == 0) {
        if (support) {
            char *format = getAttributeValue(atts, "FORMAT");

            if (format != 0) {
                char one[ANNOTATION_ROOM];

                if (mapToIBMdate(format, one) != -1) {
                    ss_addToFilteredText(s, one, (int32_t)strlen(one));
                    ss_setSayAsDate(s);
                }
            }
        }
        return;
    }

    if (ralStrIcmp(0, what, "ORDINAL") == 0) {
        if (support) {
            ss_addToFilteredText(s, " `ord[", (int32_t)strlen(" `ord["));
            ss_setSayAsNumber(s);
        }
        return;
    }

    if (ralStrIcmp(0, what, "CARDINAL") == 0) {
        if (support) {
            ss_addToFilteredText(s, " `card[", (int32_t)strlen(" `card["));
            ss_setSayAsNumber(s);
        }
        return;
    }

    /* `number' with no format is the ordinal annotation, not the cardinal
       one, so `123' comes out as an ordinal. That is IBM's own routing and
       it is kept. */
    if (ralStrIcmp(0, what, "NUMBER") == 0) {
        if (support) {
            char       *format = getAttributeValue(atts, "FORMAT");
            const char *one = " `ord[";

            if (format != 0) {
                if (ralStrIcmp(0, format, "ORDINAL") == 0) {
                    one = " `ord[";
                } else if (ralStrIcmp(0, format, "CARDINAL") == 0) {
                    one = " `card[";
                } else if (ralStrIcmp(0, format, "TELEPHONE") == 0) {
                    char *detail = getAttributeValue(atts, "DETAIL");

                    if (detail != 0
                        && ralStrIcmp(0, detail, "PUNCTUATION") == 0)
                        one = " `telpunc[";
                    else
                        one = " `tel[";
                }
            }

            ss_addToFilteredText(s, one, (int32_t)strlen(one));
            ss_setSayAsNumber(s);
        }
        return;
    }

    if (ralStrIcmp(0, what, "VXML:BOOLEAN") == 0) {
        if (support) {
            ss_addToFilteredText(s, " `bool[", (int32_t)strlen(" `bool["));
            ss_setSayAsBoolean(s);
        }
        return;
    }

    if (ralStrIcmp(0, what, "VXML:DATE") == 0) {
        if (support)
            ss_setSayAsVXMLdate(s);
        return;
    }

    if (ralStrIcmp(0, what, "VXML:CURRENCY") == 0) {
        if (support) {
            ss_addToFilteredText(s, " `cur[", (int32_t)strlen(" `cur["));
            ss_setSayAsVXMLcurrency(s);
        }
        return;
    }

    if (ralStrIcmp(0, what, "VXML:PHONE") == 0) {
        if (support) {
            ss_addToFilteredText(s, " `telpunc[",
                                 (int32_t)strlen(" `telpunc["));
            ss_setSayAsNumber(s);
        }
        return;
    }

    if (ralStrIcmp(0, what, "VXML:TIME") == 0) {
        if (support) {
            ss_addToFilteredText(s, " `time[", (int32_t)strlen(" `time["));
            ss_setSayAsNumber(s);
        }
        return;
    }

    if (ralStrIcmp(0, what, "VXML:NUMBER") == 0) {
        if (support) {
            ss_addToFilteredText(s, " `card[", (int32_t)strlen(" `card["));
            ss_setSayAsNumber(s);
        }
        return;
    }

    ss_setErrorSyntax(s);
}

/* And closing one: the bracket that ends whichever annotation was opened.
   The VoiceXML date is not here, because the text handler released it. */
void closeSayAs(SSMLState *s)
{
    int8_t support;

    if (ss_isSpellOut(s))
        ss_relSpellOut(s);

    support = langSupportSayAs(ss_peekLang(s));

    if (ss_isSayAsDate(s) && support) {
        ss_addToFilteredText(s, "]", 1);
        ss_relSayAsDate(s);
    }
    if (ss_isSayAsNumber(s) && support) {
        ss_addToFilteredText(s, "]", 1);
        ss_relSayAsNumber(s);
    }
    if (ss_isSayAsBoolean(s) && support) {
        ss_addToFilteredText(s, "]", 1);
        ss_relSayAsBoolean(s);
    }
    if (ss_isSayAsVXMLcurrency(s) && support) {
        ss_addToFilteredText(s, "]", 1);
        ss_relSayAsVXMLcurrency(s);
    }
}

/* ---- the three handlers the parser calls ----------------------------- */

/* An element opened. Almost every arm has the same shape: if text may be
   added at all, do the element's own work; if it may not but the enclosing
   text is not blocked either, the document is wrong. */
void OpenTagHandler(void *data, const char *name, const char **atts)
{
    SSMLState *s = data;
    char      *value;

    if (ss_getErrorSet(s))
        return;

    if (ralStrIcmp(0, name, "SPEAK") == 0) {
        if (ss_canAddTextBlock(s)) {
            value = getAttributeValue(atts, "XML:LANG");
            if (value != 0)
                xmllangOpen(s, value);
            else
                ss_setErrorSyntax(s);

            value = getAttributeValue(atts, "VERSION");
            if (value != 0) {
                if (ralStrIcmp(0, value, "1.0") != 0)
                    ss_setErrorSyntax(s);
            } else {
                ss_setErrorSyntax(s);
            }
        } else {
            ss_setErrorSyntax(s);
        }
        return;
    }

    if (ralStrIcmp(0, name, "PROMPT") == 0) {
        if (ss_canAddTextBlock(s)) {
            value = getAttributeValue(atts, "XML:LANG");
            if (value != 0)
                xmllangOpen(s, value);
            else
                ss_pushLang(s, ss_peekLang(s));
        } else {
            ss_setErrorSyntax(s);
        }
        return;
    }

    if (ralStrIcmp(0, name, "P") == 0
        || ralStrIcmp(0, name, "PARAGRAPH") == 0
        || ralStrIcmp(0, name, "S") == 0
        || ralStrIcmp(0, name, "SENTENCE") == 0) {
        if (ss_canAddTextBlock(s)) {
            if (ss_getEndStruct(s))
                ss_relEndStruct(s);

            value = getAttributeValue(atts, "XML:LANG");
            if (value != 0)
                xmllangOpen(s, value);
            else
                ss_pushLang(s, ss_peekLang(s));
        } else {
            ss_setErrorSyntax(s);
        }
        return;
    }

    if (ralStrIcmp(0, name, "BREAK") == 0) {
        if (ss_canAddText(s)) {
            char one[ANNOTATION_ROOM];

            value = getAttributeValue(atts, "TIME");
            if (value != 0) {
                if (mapToIBMtime(value, one) == -1)
                    ss_setErrorSyntax(s);
                ss_addToFilteredText(s, one, (int32_t)strlen(one));
            } else {
                value = getAttributeValue(atts, "STRENGTH");
                if (value != 0) {
                    if (mapToIBMtime(value, one) == -1)
                        ss_setErrorSyntax(s);
                    ss_addToFilteredText(s, one, (int32_t)strlen(one));
                } else {
                    char medium[] = "Medium";

                    mapToIBMtime(medium, one);
                    ss_addToFilteredText(s, one, (int32_t)strlen(one));
                }
            }
            ss_setSpellAddSpace(s, 0);
        } else if (!ss_canAddTextBlock(s)) {
            ss_setErrorSyntax(s);
        }
        return;
    }

    if (ralStrIcmp(0, name, "AUDIO") == 0) {
        if (ss_canAddText(s)) {
            value = getAttributeValue(atts, "SRC");
            if (value != 0)
                audioOpen(s, value);
            else
                ss_setErrorSyntax(s);
        } else if (!ss_canAddTextBlock(s)) {
            ss_setErrorSyntax(s);
        }
        return;
    }

    if (ralStrIcmp(0, name, "MARK") == 0) {
        if (ss_canAddText(s)) {
            value = getAttributeValue(atts, "NAME");
            if (value != 0)
                markOpen(s, value);
            else
                ss_setErrorSyntax(s);
        } else if (!ss_canAddTextBlock(s)) {
            ss_setErrorSyntax(s);
        }
        return;
    }

    if (ralStrIcmp(0, name, "DESC") == 0) {
        if (!ss_canAddTextBlock(s))
            ss_setErrorSyntax(s);
        ss_setBlockAddText(s);
        return;
    }

    if (ralStrIcmp(0, name, "SAY-AS") == 0) {
        if (ss_canAddText(s))
            openSayAs(s, atts);
        else if (!ss_canAddTextBlock(s))
            ss_setErrorSyntax(s);
        return;
    }

    if (ralStrIcmp(0, name, "SUB") == 0) {
        if (ss_canAddText(s)) {
            value = getAttributeValue(atts, "ALIAS");
            if (value != 0) {
                ss_addToFilteredText(s, value, (int32_t)strlen(value));
                ss_setBlockAddText(s);
            } else {
                ss_setErrorSyntax(s);
            }
        } else if (!ss_canAddTextBlock(s)) {
            ss_setErrorSyntax(s);
        }
        return;
    }

    if (ralStrIcmp(0, name, "PHONEME") == 0) {
        if (ss_canAddText(s)) {
            char *alphabet = getAttributeValue(atts, "ALPHABET");

            if (alphabet == 0)
                alphabet = (char *)"IBM";

            if (ralStrIcmp(0, alphabet, "IBM") == 0) {
                char *ph = getAttributeValue(atts, "PH");

                if (ph != 0) {
                    char *one = cpp_new((uint32_t)strlen(ph)
                                        + ANNOTATION_ROOM);

                    if (one == 0) {
                        ss_setErrorMalloc(s);
                    } else {
                        mapToIBMph(ph, one);
                        ss_addToFilteredText(s, one, (int32_t)strlen(one));
                        cpp_delete(one);
                    }
                } else {
                    ss_setErrorPhoneme(s);
                }
            } else if (ralStrIcmp(0, alphabet, "IPA") == 0) {
                if (langSupportIPA(ss_peekLang(s))) {
                    char *ph = getAttributeValue(atts, "PH");

                    if (ph != 0)
                        ipaPhonemeConvert(s, ph);
                    else
                        ss_setErrorPhoneme(s);
                } else {
                    ss_setErrorPhoneme(s);
                }
            } else {
                ss_setErrorPhoneme(s);
            }
        } else if (!ss_canAddTextBlock(s)) {
            ss_setErrorSyntax(s);
        }
        ss_setBlockAddText(s);
        return;
    }

    if (ralStrIcmp(0, name, "METADATA") == 0) {
        ss_setBlockAddText(s);
        return;
    }

    if (ralStrIcmp(0, name, "VOICE") == 0) {
        if (ss_canAddTextBlock(s))
            openVoice(s, atts);
        else
            ss_setErrorSyntax(s);
        return;
    }

    if (ralStrIcmp(0, name, "EMPHASIS") == 0) {
        if (ss_canAddText(s)) {
            if (langSupportEmphasis(ss_peekLang(s)))
                openEmphasis(s, atts);
        } else if (!ss_canAddTextBlock(s)) {
            ss_setErrorSyntax(s);
        }
        return;
    }

    if (ralStrIcmp(0, name, "PROSODY") == 0) {
        if (ss_canAddText(s))
            openProsody(s, atts);
        else if (!ss_canAddTextBlock(s))
            ss_setErrorSyntax(s);
        return;
    }
}

/* Whether the answer already ends in something that reads as a stop. What
   is looked at is the last character that is not white, and the six that
   count are the ones a reader would pause on anyway. */
static int8_t endsWithStop(SSMLState *s)
{
    char   *text = ss_getFilteredText(s);
    int32_t at = ss_getFilteredTextLength(s) - 1;

    if (text == 0)
        return 0;

    while (at >= 0) {
        char c = text[at];

        if (c == '.' || c == ',' || c == ';' || c == '!' || c == '?'
            || c == ':')
            return 1;
        if (c == ' ' || c == '\n' || c == '\r' || c == '\t') {
            at--;
            continue;
        }
        break;
    }

    return 0;
}

/* An element closed. */
void CloseTagHandler(void *data, const char *name)
{
    SSMLState *s = data;

    if (ss_getErrorSet(s))
        return;

    if (ralStrIcmp(0, name, "SUB") == 0
        || ralStrIcmp(0, name, "PHONEME") == 0
        || ralStrIcmp(0, name, "METADATA") == 0
        || ralStrIcmp(0, name, "DESC") == 0) {
        ss_relBlockAddText(s);
        return;
    }

    if (ralStrIcmp(0, name, "PARAGRAPH") == 0
        || ralStrIcmp(0, name, "P") == 0
        || ralStrIcmp(0, name, "SENTENCE") == 0
        || ralStrIcmp(0, name, "S") == 0) {
        if (ss_canAddText(s)) {
            if (ss_getEndStruct(s))
                ss_relEndStruct(s);

            /* A paragraph or a sentence that did not end in a stop of its
               own gets a pause, so that the next one does not run into
               it. */
            if (ss_getFilteredTextLength(s) >= 1 && !endsWithStop(s)) {
                char medium[] = "Medium";
                char one[ANNOTATION_ROOM];

                ss_setEndStruct(s);
                mapToIBMtime(medium, one);
                ss_addToFilteredText(s, one, (int32_t)strlen(one));
            }
            xmllangClose(s);
        } else {
            LanguageId now = ss_peekLang(s);

            if (ss_canAddTextBlock(s) && !li_getIsLanguageAvailable(&now))
                xmllangClose(s);
        }
        return;
    }

    if (ralStrIcmp(0, name, "SAY-AS") == 0) {
        if (ss_canAddText(s))
            closeSayAs(s);
        return;
    }

    if (ralStrIcmp(0, name, "VOICE") == 0) {
        if (ss_canAddText(s)) {
            closeVoice(s);
        } else {
            LanguageId now = ss_peekLang(s);

            if (ss_canAddTextBlock(s) && !li_getIsLanguageAvailable(&now))
                closeVoice(s);
        }
        return;
    }

    if (ralStrIcmp(0, name, "PROSODY") == 0) {
        if (ss_canAddText(s))
            closeProsody(s);
        return;
    }

    if (ralStrIcmp(0, name, "AUDIO") == 0) {
        if (ss_canAddText(s))
            audioClose(s);
        return;
    }

    if (ralStrIcmp(0, name, "MARK") == 0
        || ralStrIcmp(0, name, "BREAK") == 0)
        return;

    if (ralStrIcmp(0, name, "SPEAK") == 0
        || ralStrIcmp(0, name, "PROMPT") == 0) {
        if (ss_canAddText(s))
            xmllangClose(s);
        return;
    }

    if (ralStrIcmp(0, name, "EMPHASIS") == 0) {
        if (ss_canAddText(s) && langSupportEmphasis(ss_peekLang(s)))
            closeEmphasis(s);
        return;
    }
}

/* A run of character data.

   Which of six things happens to it depends on what say-as is open. A
   number is put through the Roman-numeral reader, which passes digits
   through and turns numerals into the number they are. A VoiceXML date
   becomes the whole annotation here, because the annotation names which of
   its fields are present. A boolean, a currency and a date are lowered and
   trimmed. And anything being spelt out is taken apart character by
   character.

   Spelling out is the intricate one. Four characters -- `a', `A', `-' and
   `&' -- are things the engine would read as a word or a name rather than
   as a letter, so each is wrapped in the two annotations that make the
   engine spell exactly one character; everything else gets a space in
   front of it, which is enough. An entity that survived as its own
   spelling is written out as that spelling. And a run with any multi-byte
   character in it is spelt whole in one pair of annotations rather than
   character by character, because a narrowed character is several bytes
   and cannot be taken apart.
   */
void CharDataHandler(void *data, char *text, int32_t length)
{
    SSMLState *s = data;
    char      *buf;

    if (ss_getErrorSet(s))
        return;
    if (length <= 0 || text == 0)
        return;
    if (!ss_canAddText(s))
        return;

    /* Thirteen bytes a character, which is what spelling one out costs. */
    if (length * SPELL_WIDTH + 1 > ss_getTmpBufferSize(s)) {
        if (!ss_reallocTmpBuffer(s, length * SPELL_WIDTH + 1)) {
            ss_setErrorMalloc(s);
            return;
        }
    }
    buf = ss_getTmpBuffer(s);

    if (ss_isSayAsNumber(s)) {
        mapToIBMroman(text, buf, length);
        length = (int32_t)strlen(buf);
    } else if (ss_isSayAsVXMLdate(s) && langSupportSayAs(ss_peekLang(s))) {
        if (mapToIBMvxmldate(text, buf, length) != -1) {
            length = (int32_t)strlen(buf);
        } else {
            strncpy(buf, text, (size_t)length);
            buf[length] = 0;
        }
        ss_relSayAsVXMLdate(s);
    } else if (ss_isSayAsBoolean(s) && langSupportSayAs(ss_peekLang(s))) {
        char *trimmed = stripspaces(text, &length);

        tolowerstr(trimmed, buf, length);
    } else if ((ss_isSayAsVXMLcurrency(s) || ss_isSayAsDate(s))
               && langSupportSayAs(ss_peekLang(s))) {
        char *trimmed = stripspaces(text, &length);

        strncpy(buf, trimmed, (size_t)length);
        buf[length] = 0;
    } else if (ss_isSpellOut(s)) {
        char   *p = stripspaces(text, &length);
        int32_t count = length;
        int32_t out = 0;
        int32_t i;

        length = 2 * length + 1;
        buf[0] = 0;

        for (i = 0; i < count; i++) {
            static const struct { const char *entity; const char *spelt; }
            ENTITIES[] = {
                { "&amp;",  " &amp;"  },
                { "&lt;",   " &lt;"   },
                { "&gt;",   " &gt;"   },
                { "&apos;", " &apos;" },
                { "&quot;", " &quot;" }
            };
            size_t e;
            int8_t matched = 0;

            for (e = 0; e < sizeof ENTITIES / sizeof ENTITIES[0]; e++) {
                size_t n = strlen(ENTITIES[e].entity);

                if (p[i] != '&' || (size_t)(count - i) < n)
                    continue;
                if (strncmp(p + i, ENTITIES[e].entity, n) != 0)
                    continue;
                strcat(buf, ENTITIES[e].spelt);
                out += (int32_t)strlen(ENTITIES[e].spelt);
                i += (int32_t)n - 1;
                matched = 1;
                break;
            }
            if (matched)
                continue;

            /* The four the engine would read as something other than a
               letter. */
            if (p[i] == 'a' || p[i] == 'A' || p[i] == '-' || p[i] == '&') {
                buf[out + 0] = ' ';
                buf[out + 1] = '`';
                buf[out + 2] = 't';
                buf[out + 3] = 's';
                buf[out + 4] = '2';
                buf[out + 5] = ' ';
                buf[out + 6] = p[i];
                buf[out + 7] = ' ';
                buf[out + 8] = '`';
                buf[out + 9] = 't';
                buf[out + 10] = 's';
                buf[out + 11] = '0';
                buf[out + 12] = ' ';
                buf[out + 13] = 0;
                out += SPELL_WIDTH;
                continue;
            }

            /* A multi-byte character: the whole run goes in one pair. */
            if (p[i] == NARROW_MARK) {
                strcpy(buf, " `ts2 ");
                strncat(buf, p, (size_t)count);
                strcat(buf, " `ts0");
                length = count + 12;
                break;
            }

            buf[out + 0] = ' ';
            buf[out + 1] = p[i];
            buf[out + 2] = 0;
            out += 2;
        }

        length = (int32_t)strlen(buf);
    } else {
        strncpy(buf, text, (size_t)length);
        buf[length] = 0;
    }

    ss_addToFilteredText(s, buf, length);

    if (ss_isSpellOut(s))
        ss_setSpellAddSpace(s, 1);
}

/* ---- what the filter reaches all of this through --------------------- */

void *CreateSSMLState(void)
{
    SSMLState *s = cpp_new(sizeof *s);

    if (s == 0)
        return 0;
    ss_ctor(s);
    return s;
}

void DeleteSSMLState(void *state)
{
    if (state == 0)
        return;
    ss_delete(state);
}

char *GetProcessedText(void *state)
{
    if (state == 0)
        return 0;
    return ss_getFilteredText(state);
}

int8_t ResetSSMLState(void *state)
{
    if (state == 0)
        return 0;
    return ss_resetFilterState(state);
}

void ResetSSMLStateText(void *state)
{
    ss_resetFilterText(state);
}

void SetSSMLStateEnv(void *state, void *env)
{
    ss_setEnvironment(state, env);
}

void SetSSMLStateValues(void *state, int32_t voice, int32_t lang,
                        int32_t volume)
{
    ss_setFilterEnvValues(state, voice, lang, volume);
}

ALIAS("?OpenTagHandler@@YAXPAXPBDPAPBD@Z", "OpenTagHandler");
ALIAS("?CloseTagHandler@@YAXPAXPBD@Z", "CloseTagHandler");
ALIAS("?CharDataHandler@@YAXPAXPADH@Z", "CharDataHandler");
ALIAS("?xmllangOpen@@YA_NPAVSSMLState@@PAD@Z", "xmllangOpen");
ALIAS("?xmllangClose@@YA_NPAVSSMLState@@@Z", "xmllangClose");
ALIAS("?ipaPhonemeConvert@@YAXPAVSSMLState@@PAD@Z", "ipaPhonemeConvert");
ALIAS("?audioOpen@@YAXPAVSSMLState@@PAD@Z", "audioOpen");
ALIAS("?audioClose@@YAXPAVSSMLState@@@Z", "audioClose");
ALIAS("?markOpen@@YAXPAVSSMLState@@PAD@Z", "markOpen");
ALIAS("?openProsody@@YAXPAVSSMLState@@PAPBD@Z", "openProsody");
ALIAS("?closeProsody@@YAXPAVSSMLState@@@Z", "closeProsody");
ALIAS("?openVoice@@YAXPAVSSMLState@@PAPBD@Z", "openVoice");
ALIAS("?closeVoice@@YAXPAVSSMLState@@@Z", "closeVoice");
ALIAS("?openSayAs@@YAXPAVSSMLState@@PAPBD@Z", "openSayAs");
ALIAS("?closeSayAs@@YAXPAVSSMLState@@@Z", "closeSayAs");
ALIAS("?openEmphasis@@YAXPAVSSMLState@@PAPBD@Z", "openEmphasis");
ALIAS("?closeEmphasis@@YAXPAVSSMLState@@@Z", "closeEmphasis");
ALIAS("?isLanguageAvailable@@YA_NVLanguageId@@@Z", "isLanguageAvailable");
ALIAS("?langSupportProsody@@YA_NVLanguageId@@@Z", "langSupportProsody");
ALIAS("?langSupportProsodyRate@@YA_NVLanguageId@@@Z",
      "langSupportProsodyRate");
ALIAS("?langSupportEmphasis@@YA_NVLanguageId@@@Z", "langSupportEmphasis");
ALIAS("?CreateSSMLState@@YAPAXXZ", "CreateSSMLState");
ALIAS("?GetProcessedText@@YAPADPAX@Z", "GetProcessedText");
ALIAS("?DeleteSSMLState@@YAXPAX@Z", "DeleteSSMLState");
ALIAS("?ResetSSMLState@@YA_NPAX@Z", "ResetSSMLState");
ALIAS("?ResetSSMLStateText@@YAXPAX@Z", "ResetSSMLStateText");
ALIAS("?SetSSMLStateEnv@@YAXPAX0@Z", "SetSSMLStateEnv");
ALIAS("?SetSSMLStateValues@@YAXPAXHW4ECILanguageDialect@@H@Z",
      "SetSSMLStateValues");
ALIAS("?applyProsody@@YAXPAVSSMLState@@@Z", "applyProsody");
