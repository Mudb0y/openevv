/* What the SSML reader's own files share.
 *
 * SSML arrives as text and leaves as text: the reader turns every tag into
 * the annotations this engine already understands -- ` `vs50 ' for a rate,
 * ` `card[123] ' for a number to be read as a number, ` `[hEHlOW] ' for a
 * pronunciation given as phonemes -- and hands the result back to the text
 * path as though the caller had written the annotations itself. So nothing
 * below the text filter knows this layer exists, and adding it changed no
 * line of the synthesiser.
 *
 * The objects are IBM's decomposition and the file names are its object
 * names, as everywhere else in src. Only the layouts are ours: nothing of
 * IBM's ever constructs one of these, so a field can sit where it reads
 * best rather than where its compiler put it. Where a size is load-bearing
 * -- an array of them stepped over by hand in the original -- the comment
 * says so.
 */

#ifndef ECI_SSML_H
#define ECI_SSML_H

#include <stdint.h>
#include "evv_abi.h"

/* ---- a language, as the reader carries one --------------------------- */

/* The packed word is the language in its top half, the code set in the
   third byte and the dialect in the bottom one, which is the same word the
   published interface calls an ECILanguageDialect. Beside it the same thing
   as text three ways: "1.0", then the two halves as the names a document
   writes -- "En" and "US".

   Forty-eight bytes, and that size is load-bearing: SSMLLangStack keeps an
   array of them and the original steps over it by hand. */
typedef struct {
    int32_t packed;        /* +0x00 */
    char    full[13];      /* +0x04  "%u.%u"    */
    char    major[13];     /* +0x11  "En"       */
    char    minor[14];     /* +0x1e  "US"       */
    int32_t available;     /* +0x2c             */
} LanguageId;

THIS void       li_setLanguageBytes(LanguageId *l, uint8_t lang, uint8_t dialect,
                               uint8_t codeset);
THIS void       li_setLanguagePacked(LanguageId *l, int32_t packed);
THIS void       li_setLanguageString(LanguageId *l, const char *s);
THIS void       li_init(LanguageId *l);
THIS void       li_initPacked(LanguageId *l, int32_t packed);
THIS void       li_initBytes(LanguageId *l, uint8_t lang, uint8_t dialect,
                        uint8_t codeset);
THIS void       li_initString(LanguageId *l, const char *s);
THIS void       li_initNames(LanguageId *l, const char *major, const char *minor);
THIS int32_t    li_equals(LanguageId *l, LanguageId *other);
THIS int32_t    li_codeSetEquals(LanguageId *l, LanguageId *other);
THIS uint8_t    li_getLanguage(const LanguageId *l);
THIS uint8_t    li_getDialect(const LanguageId *l);
THIS uint8_t    li_getCodeSet(const LanguageId *l);
THIS int32_t    li_getPackedInt(const LanguageId *l);
THIS const char *li_getString(const LanguageId *l);
THIS const char *li_getMajorString(const LanguageId *l);
THIS const char *li_getMinorString(const LanguageId *l);
THIS int32_t    li_getIsLanguageAvailable(const LanguageId *l);
THIS void       li_setIsLanguageAvailable(LanguageId *l, int32_t yes);
THIS int32_t    li_compareLanguage(const LanguageId *l, int32_t packed);
THIS void       li_setString(LanguageId *l);
THIS void       li_setPackedInt(LanguageId *l);

/* ---- the three stacks ------------------------------------------------ */

/* All three are run-length stacks: pushing what is already on top bumps a
   count rather than taking another slot, and popping takes the count back
   down. SSML nests, and a document that opens six <prosody> elements with
   the same rate in a row costs one slot rather than six.
 *
 * The exception is the string stack, which has no run length: every push
 * takes a slot and keeps its own copy.
 *
 * Each starts at twenty slots and doubles. `top' indexes the slot in play
 * and `count' is how many pushes are outstanding, which is not the same
 * number once a run has been folded up. */

#define SSML_STACK_SLOTS 0x14

typedef struct {
    int32_t  top;
    int32_t  slots;
    int32_t  count;
    int32_t *values;
    int32_t *runs;
} SSMLIntStack;

THIS void     sis_ctor(SSMLIntStack *s);
THIS void     sis_dtor(SSMLIntStack *s);
THIS void     sis_delete(SSMLIntStack *s);
THIS void     sis_push(SSMLIntStack *s, int32_t v);
THIS int32_t  sis_pop(SSMLIntStack *s);
THIS int32_t  sis_peek(SSMLIntStack *s);
THIS int8_t   sis_isValid(SSMLIntStack *s);
THIS int8_t   sis_isEmpty(SSMLIntStack *s);
THIS int32_t  sis_stackSize(SSMLIntStack *s);

/* The string stack keeps a copy of what is pushed and hands ownership of
   it back on a pop; whatever is still on it when it goes is freed. */
typedef struct {
    int32_t  count;
    int32_t  slots;
    char   **items;
} SSMLStrStack;

THIS void     sss_ctor(SSMLStrStack *s);
THIS void     sss_dtor(SSMLStrStack *s);
THIS void     sss_delete(SSMLStrStack *s);
THIS void     sss_push(SSMLStrStack *s, char *v);
THIS char    *sss_pop(SSMLStrStack *s);
THIS char    *sss_peek(SSMLStrStack *s);
THIS char    *sss_peekAt(SSMLStrStack *s, int32_t which);
THIS int8_t   sss_isValid(SSMLStrStack *s);
THIS int8_t   sss_isEmpty(SSMLStrStack *s);
THIS int32_t  sss_stackSize(SSMLStrStack *s);

typedef struct {
    int32_t     top;
    int32_t     slots;
    int32_t     count;
    LanguageId *values;
    int32_t    *runs;
} SSMLLangStack;

THIS void       sls_ctor(SSMLLangStack *s);
THIS void       sls_dtor(SSMLLangStack *s);
THIS void       sls_delete(SSMLLangStack *s);
THIS void       sls_push(SSMLLangStack *s, LanguageId v);
THIS LanguageId sls_pop(SSMLLangStack *s);
THIS LanguageId sls_peek(SSMLLangStack *s);
THIS int8_t     sls_isValid(SSMLLangStack *s);
THIS int8_t     sls_isEmpty(SSMLLangStack *s);
THIS int32_t    sls_stackSize(SSMLLangStack *s);

/* ---- odds the reader's own files share -------------------------------- */

char *stripspaces(char *s, int32_t *len);
char *getAttributeValue(const char **atts, const char *name);

/* Text narrowed so the scanner can read it, and widened again. */
int32_t Mbcs2Sbcs(char *in, char *out);
int32_t Sbcs2Mbcs(char *in, char *out);
int32_t getCharByteCount(const uint8_t *s);

/* A pronunciation given in IPA, turned into the engine's own spelling. */
int32_t IPAToSPR(uint8_t *utf8, uint32_t bytes, char *spr, uint32_t *room,
                 int32_t lang);

#endif
