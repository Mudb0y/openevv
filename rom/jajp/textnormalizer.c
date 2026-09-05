/* TextNormalizer, which is the front door of the analyser.
 *
 * A caller hands the engine text with annotations in it: a backquote, a name,
 * and the text the name is about in square brackets. This class walks that
 * text, hands each annotated piece to whichever of MakeReadableJP's eight
 * readers the name asks for, and puts what came back in place of the
 * annotation. Everything outside an annotation is copied through byte for
 * byte, two bytes at a time where the byte leads a two-byte character.
 *
 * The names and what each stands for are IBM's own table, lifted into
 * lang/jajp/rom_tables_jajp.c as `aMakeReadableAnnos'. An annotation with a
 * name in none of them is not an annotation at all: the backquote goes into
 * the answer along with the character after it, which is how a backquote in
 * ordinary text survives. An annotation with no name -- a bracket straight
 * after the backquote -- is the synthesiser's own phoneme string, and goes to
 * convertSPR rather than to a reader.
 *
 * The class owns two buffers. The answer is grown as it fills and is handed
 * to the caller at the end, the class forgetting it so that the caller owns
 * it; the working buffer is kept between calls and is what one annotation's
 * reading is built in. Both start at a quarter of a kilobyte over what is
 * needed and grow by that much again.
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "jprom.h"
#include "textnormalizer.h"
#include "makereadable.h"
#include "rom_tables_jajp.h"

#define TN_B(t, o)         (*(uint8_t *)((uint8_t *)(t) + (o)))
#define TN_U32(t, o)       (*(uint32_t *)((uint8_t *)(t) + (o)))
#define TN_READABLE_OF(t)  (*(void **)((uint8_t *)(t) + TN_READABLE_AT))
#define TN_OUT_OF(t)       (*(char **)((uint8_t *)(t) + TN_OUT_AT))
#define TN_WORK_OF(t)      (*(char **)((uint8_t *)(t) + TN_WORK_AT))

/* The C locale is the only one the engine runs in, where isalpha is the two
   runs of letters and nothing else. Written out rather than called, as
   MakeReadableJP's own copy of this test is. */
#define TN_ISALPHA(c) (((c) >= 'A' && (c) <= 'Z') || ((c) >= 'a' && (c) <= 'z'))

void *tn_ctor(void *tn)
{
    TN_READABLE_OF(tn) = NULL;
    TN_OUT_OF(tn)      = NULL;
    TN_WORK_OF(tn)     = NULL;
    TN_U32(tn, TN_OUT_CAP)  = 0;
    TN_U32(tn, TN_WORK_CAP) = 0;
    return tn;
}

void tn_dtor(void *tn)
{
    if (TN_READABLE_OF(tn) != NULL) {
        mr_destroy(TN_READABLE_OF(tn), 1);
        TN_READABLE_OF(tn) = NULL;
    }
    if (TN_WORK_OF(tn) != NULL) {
        cpp_delete(TN_WORK_OF(tn));
        TN_WORK_OF(tn) = NULL;
    }
    if (TN_OUT_OF(tn) != NULL) {
        cpp_delete(TN_OUT_OF(tn));
        TN_OUT_OF(tn) = NULL;
    }
}

/* A bigger buffer with what was in the old one copied over, and the old one
   given back. Minus one rather than one is what this one answers with when
   there is no room, which is not what MakeReadableJP's namesake answers. */
int32_t tn_reallocateBuf(void *tn, char **buf, uint32_t used, uint32_t want)
{
    /* One byte over, for the reason MakeReadableJP's namesake gives: the
       answer's terminator is written at the length reached and that length
       may be the size exactly. */
    char *got = (char *)cpp_new(want + 1);

    (void)tn;
    if (got == NULL)
        return -1;
    memcpy(got, *buf, used);
    cpp_delete(*buf);
    *buf = got;
    return 0;
}

/* What sort of annotation this is, and where its text begins and ends.
 *
 * The text handed in is what follows the backquote. A run of letters and then
 * an opening bracket is a name; anything else in front of the bracket is not,
 * and neither is a bracket that never closes. The name is looked up in IBM's
 * table and what comes back is the entry's number -- or the terminating
 * entry's, which is minus one, for a name in none of them. A bracket straight
 * after the backquote is the phoneme string and comes back as its own number.
 *
 * Two-byte characters inside the brackets are stepped over as pairs, so a
 * byte that looks like a closing bracket but is the tail of a kanji does not
 * end the annotation.
 */
int32_t tn_getAnnoType(void *tn, const char *text, const char **argStart,
                       const char **after)
{
    const char *p = text;
    const char *open_ = NULL;
    const char *close = NULL;
    int32_t     ok = 0;
    int32_t     type = -1;
    int32_t     nameLen;
    int32_t     i;

    (void)tn;
    while (*p != '\0') {
        if (TN_ISALPHA(*p)) {
            p++;
            continue;
        }
        if (*p == '[') {
            open_ = p;
            ok    = 1;
        } else {
            ok = 0;
        }
        break;
    }
    if (ok == 0 || *p == '\0')
        return -1;

    ok = 0;
    p++;
    while (*p != '\0') {
        if (ju_IsDBCSLeadByte(p[0])) {
            p += 2;
            continue;
        }
        if (*p == ']') {
            close = p;
            ok    = 1;
            break;
        }
        p++;
    }
    if (ok == 0)
        return -1;

    nameLen = (int32_t)(open_ - text);
    if (nameLen == 0) {
        type = TN_SPR;
    } else {
        for (i = 0; jajp_aMakeReadableAnnos[i].how != NULL; i++) {
            size_t n = strlen(jajp_aMakeReadableAnnos[i].how);

            if (n == (size_t)nameLen
                && strncmp(text, jajp_aMakeReadableAnnos[i].how, n) == 0)
                break;
        }
        type = jajp_aMakeReadableAnnos[i].what;
    }
    *argStart = open_ + 1;
    *after    = close + 1;
    return type;
}

/* One annotation read, by whichever of the eight readers its number names.
 *
 * The reader is reached through MakeReadableJP's vtable in the original, and
 * the number picks the slot: a cardinal or an ordinal is the number reader, a
 * telephone number the telephone reader, and the rest one apiece. Two of the
 * eight -- the digit reader and the literal reader -- are in the vtable and
 * no number reaches them, which is IBM's and is left as it is.
 */
int32_t tn_makeReadable(void *tn, const char *text, int32_t n, char **buf,
                        uint32_t *cap, int32_t flag)
{
    int32_t rc = 0;

    if (TN_READABLE_OF(tn) == NULL) {
        void *got = cpp_new(MR_ROOM);

        TN_READABLE_OF(tn) = got != NULL ? mr_ctor(got) : NULL;
        if (TN_READABLE_OF(tn) == NULL)
            return -1;
    }

    switch (TN_KIND(flag)) {
    case TN_CARDINAL:
        if (flag >= TN_TEL)
            rc = mr_normalizePhone(TN_READABLE_OF(tn), text, (uint32_t)n,
                                   buf, cap, flag);
        else
            rc = mr_normalizeNumber(TN_READABLE_OF(tn), text, (uint32_t)n,
                                    buf, cap, flag);
        break;
    case TN_DATE:
        rc = mr_normalizeDate(TN_READABLE_OF(tn), text, (uint32_t)n,
                              buf, cap, flag);
        break;
    case TN_TIME:
        rc = mr_normalizeTime(TN_READABLE_OF(tn), text, (uint32_t)n,
                              buf, cap, flag);
        break;
    case TN_CURRENCY:
        rc = mr_normalizeCurrency(TN_READABLE_OF(tn), text, (uint32_t)n,
                                  buf, cap, flag);
        break;
    case TN_BOOL:
        rc = mr_normalizeBool(TN_READABLE_OF(tn), text, (uint32_t)n,
                              buf, cap, flag);
        break;
    case TN_SPR:
        rc = mr_convertSPR(TN_READABLE_OF(tn), text, (uint32_t)n, buf, cap);
        break;
    default:
        break;
    }
    return rc != 0 ? -1 : 0;
}

/* The whole text, with every annotation in it read.
 *
 * The answer's buffer is made here rather than kept: a text shorter than a
 * quarter of a kilobyte gets that much over its length, a longer one gets
 * twice its length, and either grows further as it fills. At the end the
 * caller is given the buffer and its length and the class forgets it, so a
 * second call makes another; a call that fails part way keeps it, and the
 * destructor is what gives that one back.
 *
 * Note the walk is over the text's own terminator rather than over the length
 * the caller passed, which is only ever read to decide the first size.
 */
int32_t tn_normalizeText(void *tn, const char *text, uint32_t n, char **buf,
                         uint32_t *len)
{
    const char *p;
    const char *argStart;
    const char *after;
    uint32_t    out = 0;
    uint32_t    partLen;
    int32_t     rc = -1;
    int32_t     type;
    int32_t     inAnno = 0;
    int32_t     argLen;

    if (n < TN_SLACK)
        TN_U32(tn, TN_OUT_CAP) = n + TN_SLACK;
    else
        TN_U32(tn, TN_OUT_CAP) = n * 2;

    TN_OUT_OF(tn) = (char *)cpp_new(TN_U32(tn, TN_OUT_CAP) + 1);
    if (TN_OUT_OF(tn) == NULL)
        return -1;

    if (TN_WORK_OF(tn) == NULL) {
        TN_U32(tn, TN_WORK_CAP) = TN_SLACK;
        TN_WORK_OF(tn) = (char *)cpp_new(TN_U32(tn, TN_WORK_CAP) + 1);
        if (TN_WORK_OF(tn) == NULL)
            return -1;
    }

    p = text;
    while (*p != '\0') {
        if (inAnno != 0) {
            type = tn_getAnnoType(tn, p, &argStart, &after);
            if (type != -1) {
                argLen = (int32_t)(after - 1 - argStart);
                rc = tn_makeReadable(tn, argStart, argLen, &TN_WORK_OF(tn),
                                     &TN_U32(tn, TN_WORK_CAP), type);
                if (rc != 0)
                    return rc;
                partLen = (uint32_t)strlen(TN_WORK_OF(tn));
                if (out + partLen > TN_U32(tn, TN_OUT_CAP)) {
                    rc = tn_reallocateBuf(tn, &TN_OUT_OF(tn),
                                          TN_U32(tn, TN_OUT_CAP),
                                          out + partLen + TN_SLACK);
                    if (rc == -1)
                        return rc;
                    TN_U32(tn, TN_OUT_CAP) = out + partLen + TN_SLACK;
                }
                strcpy(TN_OUT_OF(tn) + out, TN_WORK_OF(tn));
                out += partLen;
                p = after;
            } else if (ju_IsDBCSLeadByte(p[0])) {
                /* Not an annotation after all, so the backquote is text. */
                if (out + 3 > TN_U32(tn, TN_OUT_CAP)) {
                    rc = tn_reallocateBuf(tn, &TN_OUT_OF(tn),
                                          TN_U32(tn, TN_OUT_CAP),
                                          out + TN_SLACK + 3);
                    if (rc == -1)
                        return rc;
                    TN_U32(tn, TN_OUT_CAP) = out + TN_SLACK + 3;
                }
                TN_OUT_OF(tn)[out++] = '`';
                TN_OUT_OF(tn)[out++] = *p++;
                TN_OUT_OF(tn)[out++] = *p++;
            } else {
                if (out + 2 > TN_U32(tn, TN_OUT_CAP)) {
                    rc = tn_reallocateBuf(tn, &TN_OUT_OF(tn),
                                          TN_U32(tn, TN_OUT_CAP),
                                          out + TN_SLACK + 2);
                    if (rc == -1)
                        return rc;
                    TN_U32(tn, TN_OUT_CAP) = out + TN_SLACK + 2;
                }
                TN_OUT_OF(tn)[out++] = '`';
                TN_OUT_OF(tn)[out++] = *p++;
            }
            inAnno = 0;
        } else if (*p == '`') {
            inAnno = 1;
            p++;
        } else if (ju_IsDBCSLeadByte(p[0])) {
            if (out + 2 > TN_U32(tn, TN_OUT_CAP)) {
                rc = tn_reallocateBuf(tn, &TN_OUT_OF(tn),
                                      TN_U32(tn, TN_OUT_CAP),
                                      out + TN_SLACK + 2);
                if (rc == -1)
                    return rc;
                TN_U32(tn, TN_OUT_CAP) = out + TN_SLACK + 2;
            }
            TN_OUT_OF(tn)[out++] = *p++;
            TN_OUT_OF(tn)[out++] = *p++;
        } else {
            if (out + 1 > TN_U32(tn, TN_OUT_CAP)) {
                rc = tn_reallocateBuf(tn, &TN_OUT_OF(tn),
                                      TN_U32(tn, TN_OUT_CAP),
                                      out + TN_SLACK + 1);
                if (rc == -1)
                    return rc;
                TN_U32(tn, TN_OUT_CAP) = out + TN_SLACK + 1;
            }
            TN_OUT_OF(tn)[out++] = *p++;
        }
    }

    TN_OUT_OF(tn)[out] = '\0';
    *len = out;
    *buf = TN_OUT_OF(tn);
    TN_OUT_OF(tn) = NULL;
    return 0;
}
