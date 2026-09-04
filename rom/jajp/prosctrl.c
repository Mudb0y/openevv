/* The prosody chain: breath groups in, the ESPR string out.
 *
 * This is the last thing between the analysis and the engine. `IntonPhrase'
 * has decided where the speaker breathes, which phrases belong together and
 * what pitch each mora carries; what is left is to say all of that in the
 * notation the synthesiser reads -- phonemes with a pitch level on each, a
 * stress mark where a word is prominent, a pause of so many milliseconds at
 * each boundary, and the caller's own index marks put back where they were.
 *
 * `GenerateESPR' is the entry point and it does two things. `BG_T2BreathGroups'
 * copies IntonPhrase's chain into a tree of this class's own, four levels
 * deep, rewriting some codes on the way; `WriteESPR2' then walks that tree and
 * writes the text. rom/jajp/prosctrl.h is the map of all four records.
 *
 * IBM keeps this in three objects and thirteen more of the prosody chain are
 * empty, everything having been inlined away.
 *
 * Held to IBM's answer by test/harness/romprims.sh.
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "jprom.h"
#include "prosctrl.h"
#include "intonphrase.h"

/* Reaching into the four records. Every one of them is at IBM's own offsets
   on both builds, because the only pointers in them are handed out by
   operator new rather than sitting where IBM put a four-byte field: a
   BREATHGROUP's phrase array, a phrase's word array and a word's mora array
   all sit at offsets with four bytes of slack behind them, which is what the
   record's own size leaves. */
#define PC_S32(p, off)  (*(int32_t *)((uint8_t *)(p) + (off)))
#define BG_B(p, off)    (*((uint8_t *)(p) + (off)))
#define BG_S16(p, off)  (*(int16_t *)((uint8_t *)(p) + (off)))
#define BG_AT(a, i)     ((uint8_t *)(a) + (long)(i) * BG_SIZE)
#define PH_AT(a, i)     ((uint8_t *)(a) + (long)(i) * PH_SIZE)
#define AP_AT(a, i)     ((uint8_t *)(a) + (long)(i) * AP_SIZE)
#define MO_AT(a, i)     ((uint8_t *)(a) + (long)(i) * MO_SIZE)
#define PTR_OF(p, off)  (*(void **)((uint8_t *)(p) + (off)))

/* And into IntonPhrase's, which this class only reads. */
#define IG_B_(bg, off)  (*((uint8_t *)(bg) + (off)))
#define IG_S16_(bg, off) (*(int16_t *)((uint8_t *)(bg) + (off)))
#define IH_AT(bg, i)    ((uint8_t *)(bg) + IG_PHRASE + (long)(i) * IG_PHRASE_SIZE)
#define IH_B_(ph, off)  (*((uint8_t *)(ph) + (off)))
#define IH_S16_(ph, off) (*(int16_t *)((uint8_t *)(ph) + (off)))

/* ---- being made and unmade ------------------------------------------ */

/* The constructor clears three fields and the destructor does nothing at
   all, which is IBM's own -- the tree this class builds is freed by
   `FreeBreathGroups' on every road out of `GenerateESPR' rather than by the
   destructor, so an instance owns nothing when it is destroyed. */
void *pc_ctor(void *pc)
{
    PC_S32(pc, PC_MODE)      = 0;
    PC_S32(pc, PC_UNREAD_04) = 0;
    PC_S32(pc, PC_ARG)       = 0;
    return pc;
}

void pc_dtor(void *pc)
{
    (void)pc;
}

/* ---- what a phoneme is ---------------------------------------------- */

/* Whether a consonant is a burst, and whether one can carry a doubling.
   Twelve codes and sixteen codes, each asked for one at a time in IBM's own
   order, which is the order kept here. */
int32_t pc_IsBurstCons(void *pc, uint8_t code)
{
    (void)pc;
    return (code == 0x08 || code == 0x05 || code == 0x01 || code == 0x1a
            || code == 0x19 || code == 0x15 || code == 0x09 || code == 0x1c
            || code == 0x02 || code == 0x1b || code == 0x1e || code == 0x16)
           ? 1 : 0;
}

int32_t pc_IsValidConsForSokuOn(void *pc, uint8_t code)
{
    (void)pc;
    return (code == 0x1a || code == 0x06 || code == 0x19 || code == 0x0c
            || code == 0x15 || code == 0x0a || code == 0x18 || code == 0x01
            || code == 0x02 || code == 0x08 || code == 0x09 || code == 0x03
            || code == 0x04 || code == 0x05 || code == 0x07 || code == 0x17)
           ? 1 : 0;
}

/* ---- putting text in the caller's buffer ---------------------------- */

/* One string appended, and the only place this class touches the answer.
   What it refuses is an overrun, which is the minus three every writer above
   it passes back, and it refuses at the buffer's own size rather than one
   short of it, so the terminator always has somewhere to go. */
int32_t pc_WriteToOutBuf(void *pc, const char *what, char *out,
                         uint32_t cap, uint32_t *len)
{
    size_t n = strlen(what);

    (void)pc;
    if (*len + n >= cap)
        return -3;
    *len = (uint32_t)(*len + n);
    strcat(out, what);
    return 0;
}

/* The caller's index marks, one escape each. They go out in batches of
   sixty-two, which is what fits in the buffer IBM builds them in. */
int32_t pc_WriteUserIndex(void *pc, int32_t n, char *out, uint32_t cap,
                          uint32_t *len)
{
    char    buf[256];
    int32_t left = n;
    int32_t batch;
    int32_t i;

    while (left > 0) {
        batch = (int32_t)(256u / PC_USER_IDX_LEN) - 2;
        if (left < batch)
            batch = left;
        buf[0] = '\0';
        for (i = 0; i < batch; i++)
            strcat(buf, PC_USER_IDX);
        if (pc_WriteToOutBuf(pc, buf, out, cap, len) != 0)
            return -3;
        left -= batch;
    }
    return 0;
}

/* A pause, as the boundary that closes a breath group. Which of the four
   forms it takes says what kind of boundary it was: a question, an
   exclamation, a full stop, or a comma where the group is not the last. */
int32_t pc_WriteBGInfo(void *pc, int32_t ms, int32_t kind, int32_t last,
                       char *out, uint32_t cap, uint32_t *len)
{
    char buf[0x20];

    if (last != 0) {
        if (kind == 5)
            sprintf(buf, "%s? %d%s ", "{", (int)ms, "$");
        else if (kind == 6)
            sprintf(buf, "%s! %d%s ", "{", (int)ms, "$");
        else
            sprintf(buf, "%s. %d%s ", "{", (int)ms, "$");
    } else {
        sprintf(buf, "%s, %d ", "{", (int)ms);
    }
    if (pc_WriteToOutBuf(pc, buf, out, cap, len) != 0)
        return -3;
    return 0;
}

/* Whether this word is stressed. A word is unstressed where it is the first
   of its phrase or past the last, and stressed everywhere else -- unless the
   flag the caller passes says to stress it whatever its place. */
int32_t pc_WriteStressLevel(void *pc, int32_t at, int32_t from, int32_t to,
                            char *out, uint32_t cap, uint32_t *len,
                            int32_t force)
{
    int32_t rc;

    if (force != 0 || from > at || at >= to)
        rc = pc_WriteToOutBuf(pc, ".1 ", out, cap, len);
    else
        rc = pc_WriteToOutBuf(pc, ".0 ", out, cap, len);
    if (rc != 0)
        return -3;
    return 0;
}

/* As many pitch pairs as the caller asks for, all of them the same one. */
int32_t pc_WriteDummyF0Pair(void *pc, int32_t n, char *out, uint32_t cap,
                            uint32_t *len)
{
    int32_t i;

    for (i = 0; i < n; i++)
        if (pc_WriteToOutBuf(pc, PC_DUMMY_F0, out, cap, len) != 0)
            return -3;
    return 0;
}

/* A word's prominence, tidied. Three of the values a part of speech can have
   mean the word takes the prominence of whatever is around it, and a fourth
   means it takes none at all; and a prominence of five is written back as
   one, which is the only value this rewrites rather than clears. */
int32_t pc_ModifyWordProminence(void *pc, int32_t *prom, int32_t pos,
                                int32_t flag)
{
    (void)pc;
    if (pos == 9 || pos == -3 || pos == -2) {
        if (flag != 0)
            *prom = -2;
        else
            *prom = -1;
    } else if (pos == -4) {
        *prom = -1;
    }
    if (*prom == 5)
        *prom = 1;
    return 0;
}

/* ---- copying IntonPhrase's chain into a tree of this class's own ---- */

/* Where the two records meet, and the reason this file confirms
   rom/jajp/intonphrase.h rather than merely trusting it: every field of a
   breath group and of a phrase inside one is read here, on a base this class
   did not choose. Eleven of them agree with what that map says, and the one
   that did not was the length of the reading -- the int16 read at 0x66 of a
   phrase is where a twenty-ninth code would have been.
 *
 * Two codes become two codes each on the way through. The mark for a
   devoiced vowel becomes an accent and a glottal, and the mark for a nasal
   becomes its own code and the same glottal; each of those makes the phrase
   one mora longer and the group one code longer, which is why three counts
   are bumped rather than one. And a full stop becomes either itself, where
   the code after it is one of a set of phoneme classes, or a pause -- with a
   long vowel after it taking a second code and eating the one that followed.
 */

/* The set of phoneme classes that let a full stop through as itself. IBM asks
   for each of them separately, in this order, and the order is kept. */
static int32_t pc_stopStands(int32_t klass)
{
    if (klass <= 0)
        return 0;
    if (klass <= 0x0a)
        return 1;
    if (klass == 0x0c || klass == 0x15)
        return 1;
    if (klass <= 0x16)
        return 0;
    if (klass <= 0x1a)
        return 1;
    return 0;
}

int32_t pc_BG_T2BreathGroups(void *pc, const void *bgt, void **outGroups,
                             int32_t *outCount)
{
    uint8_t *groups;
    uint8_t *bg;
    uint8_t *ph;
    uint8_t *ap;
    uint8_t *mo;
    const uint8_t *t;
    const uint8_t *src;
    int32_t n = 0;
    int32_t g, p, w, k, i, j;
    int32_t codes;
    int32_t extra;
    int32_t hold;
    int32_t prevHold;
    int32_t ahead;
    int32_t longAt;
    int32_t scan;
    int32_t subCount;
    int32_t ins;
    uint8_t cur;
    uint8_t next;

    *outCount  = 0;
    *outGroups = NULL;

    for (t = (const uint8_t *)bgt; t != NULL; t = (const uint8_t *)IG_NEXT_OF(t))
        n++;
    if (n == 0)
        return -1;

    groups = (uint8_t *)cpp_new((uint32_t)(n * BG_SIZE));
    *outGroups = groups;
    if (groups == NULL)
        return -2;
    for (i = 0; i < n; i++)
        *(int32_t *)BG_AT(groups, i) = 0;
    *outCount = n;

    t = (const uint8_t *)bgt;
    for (g = 0; g < n; g++) {
        bg = BG_AT(groups, g);
        BG_B(bg, BG_LEVEL)   = IG_B_(t, IG_LEVEL);
        BG_B(bg, BG_PHRASES) = IG_B_(t, IG_PHRASES);
        BG_S16(bg, BG_PAUSE) = IG_S16_(t, IG_PAUSE);
        BG_B(bg, BG_KIND)    = IG_B_(t, IG_KIND);

        PTR_OF(bg, BG_PHRASE) =
            cpp_new((uint32_t)(BG_B(bg, BG_PHRASES) * PH_SIZE));
        if (PTR_OF(bg, BG_PHRASE) == NULL)
            return -2;
        for (i = 0; i < BG_B(bg, BG_PHRASES); i++)
            PTR_OF(PH_AT(PTR_OF(bg, BG_PHRASE), i), PH_WORD) = NULL;

        for (p = 0; p < IG_B_(t, IG_PHRASES); p++) {
            ph  = PH_AT(PTR_OF(bg, BG_PHRASE), p);
            src = IH_AT(t, p);

            BG_B(ph, PH_MORAS) = IH_B_(src, IH_KANA_LEN);
            BG_B(ph, PH_FLAG)  = IH_B_(src, IH_FLAG);
            BG_S16(ph, PH_AT4) = IH_S16_(src, IH_AT66);
            BG_B(ph, PH_FIRST) = IH_B_(src, IH_FIRST);
            BG_B(ph, PH_WORDS) = IH_B_(src, IH_COUNT);

            hold     = 0;
            prevHold = 0;
            ahead    = 0;
            longAt   = 0;

            PTR_OF(ph, PH_WORD) =
                cpp_new((uint32_t)(BG_B(ph, PH_WORDS) * AP_SIZE));
            if (PTR_OF(ph, PH_WORD) == NULL)
                return -2;
            for (i = 0; i < BG_B(ph, PH_WORDS); i++)
                PTR_OF(AP_AT(PTR_OF(ph, PH_WORD), i), AP_MORA) = NULL;

            for (w = 0; w < BG_B(ph, PH_WORDS); w++) {
                ap    = AP_AT(PTR_OF(ph, PH_WORD), w);
                codes = IH_B_(src, IH_LEN + w);
                BG_B(ap, AP_CODES) = (uint8_t)codes;
                extra = 0;
                cur   = 0;

                if (w == BG_B(ph, PH_WORDS) - 1)
                    BG_B(ap, AP_LAST) = BG_B(ph, PH_FLAG);
                else
                    BG_B(ap, AP_LAST) = 0;

                for (j = 0; j < codes + extra; j++) {
                    cur = IH_B_(src, IH_KANA + hold + j);
                    if (j + 1 < codes + extra)
                        next = IH_B_(src, IH_KANA + 1 + hold + j);
                    else if (hold + j + 1 < IH_B_(src, IH_KANA_LEN))
                        next = IH_B_(src, IH_KANA + 1 + hold + j);
                    else
                        next = 0xfd;

                    if (cur == 0xda || cur == 0xe2) {
                        if (cur == 0xda) {
                            BG_B(ap, AP_CODE + j + extra) = 0x21;
                            extra++;
                            BG_B(ap, AP_CODE + j + extra) = 0x92;
                        } else if (cur == 0xe2) {
                            BG_B(ap, AP_CODE + j + extra) = 0x5a;
                            extra++;
                            BG_B(ap, AP_CODE + j + extra) = 0x92;
                        }
                        BG_B(ap, AP_CODES)++;
                        BG_B(ph, PH_MORAS)++;
                        BG_B(bg, BG_LEVEL)++;
                    } else if (cur == 0xfd) {
                        if (pc_stopStands((int32_t)(((next & 0xf8) >> 3) + 1))) {
                            BG_B(ap, AP_CODE + j + extra) = cur;
                        } else if (next >= 0xf0 && next <= 0xf4) {
                            BG_B(ap, AP_CODE + j + extra)     = 0x32;
                            BG_B(ap, AP_CODE + j + extra + 1) = 0xf2;
                            j++;
                        } else {
                            BG_B(ap, AP_CODE + j + extra) = 0x32;
                        }
                    } else {
                        BG_B(ap, AP_CODE + j + extra) = cur;
                    }
                }

                /* A word whose reading runs up to a full stop gives the stop
                   back, and the phrase and the group with it. */
                if (IH_B_(src, IH_KANA_LEN + codes + hold) == 0xfd
                    && w == BG_B(ph, PH_WORDS) - 1
                    && BG_B(ap, AP_CODES) > 1) {
                    if (BG_B(ap, AP_CODES) == codes)
                        codes--;
                    BG_B(ap, AP_CODES)--;
                    BG_B(ph, PH_MORAS)--;
                    BG_B(bg, BG_LEVEL)--;
                }

                BG_B(ap, AP_MORAS)  = IH_B_(src, IH_MORAS + w);
                BG_S16(ap, AP_LONG) = IH_B_(src, IH_F + longAt);
                BG_B(ap, AP_HEAD)   = IH_B_(src, IH_A + w);
                BG_B(ap, AP_LEN)    = IH_B_(src, IH_MORAS + w);
                BG_B(ap, AP_PITCH)  = IH_B_(src, IH_PITCH + w);

                if (BG_B(ap, AP_MORAS) > 1
                    && BG_B(ap, AP_CODE + BG_B(ap, AP_MORAS) - 1) == 0xfd) {
                    BG_B(ap, AP_MORAS)--;
                    BG_B(ap, AP_LEN)--;
                }

                prevHold = hold;
                hold    += codes;

                /* How many of the phrase's long entries belong to this word:
                   as many as start before the word's reading ends. */
                scan = longAt;
                while (BG_B(ap, AP_CODES) + prevHold
                           > IH_S16_(src, IH_E + scan * 2)
                       && IH_S16_(src, IH_E + scan * 2) != -1)
                    scan++;
                subCount = scan - longAt;
                BG_B(ap, AP_MORA_N) = (uint8_t)subCount;
                if (subCount == 0)
                    continue;

                PTR_OF(ap, AP_MORA) =
                    cpp_new((uint32_t)(subCount * MO_SIZE));
                if (PTR_OF(ap, AP_MORA) == NULL)
                    return -2;

                for (k = 0; k < subCount; k++) {
                    mo = MO_AT(PTR_OF(ap, AP_MORA), k);
                    BG_S16(mo, MO_KIND) = IH_B_(src, IH_F + longAt);

                    if (longAt < IH_E_N - 1)
                        ahead = IH_S16_(src, IH_E + 2 + longAt * 2);
                    else
                        ahead = -1;

                    if (ahead != -1)
                        BG_B(mo, MO_CODES) = (uint8_t)(ahead - prevHold);
                    else
                        BG_B(mo, MO_CODES) = (uint8_t)(hold - prevHold);

                    if (BG_B(mo, MO_CODES) == 0) {
                        if (BG_B(ap, AP_MORA_N) > 0) {
                            BG_B(ap, AP_MORA_N)--;
                            subCount--;
                        }
                        if (k > 0)
                            k--;
                        longAt++;
                        continue;
                    }

                    ins = 0;
                    cur = 0;
                    for (j = 0; j < BG_B(mo, MO_CODES); j++) {
                        cur  = IH_B_(src, IH_KANA + prevHold + j);
                        next = IH_B_(src, IH_KANA + 1 + prevHold + j);

                        if (cur == 0xda || cur == 0xe2) {
                            if (cur == 0xda) {
                                BG_B(mo, MO_CODE + j + ins) = 0x21;
                                ins++;
                                BG_B(mo, MO_CODE + j + ins) = 0x92;
                            }
                            if (cur == 0xe2) {
                                BG_B(mo, MO_CODE + j + ins) = 0x5a;
                                ins++;
                                BG_B(mo, MO_CODE + j + ins) = 0x92;
                            }
                            BG_B(mo, MO_CODES)++;
                        } else if (cur == 0xfd) {
                            if (pc_stopStands((int32_t)(((next & 0xf8) >> 3)
                                                        + 1))) {
                                BG_B(mo, MO_CODE + j + ins) = cur;
                                if (j + 1 >= BG_B(mo, MO_CODES)) {
                                    BG_B(mo, MO_CODE + j + ins + 1) = next;
                                    BG_B(mo, MO_CODES)++;
                                    j++;
                                    ahead++;
                                }
                            } else if (next >= 0xf0 && next <= 0xf4) {
                                BG_B(mo, MO_CODE + j + ins)     = 0x32;
                                BG_B(mo, MO_CODE + j + ins + 1) = 0xf2;
                                j++;
                            } else {
                                BG_B(mo, MO_CODE + j + ins) = 0x32;
                            }
                        } else {
                            BG_B(mo, MO_CODE + j + ins) = cur;
                        }
                    }

                    if (w == BG_B(ph, PH_WORDS) - 1
                        && k == BG_B(ap, AP_MORA_N) - 1
                        && cur == 0xfd
                        && BG_B(mo, MO_CODES) > 1)
                        BG_B(mo, MO_CODES)--;

                    prevHold = ahead;
                    longAt++;
                }
            }
        }
        t = (const uint8_t *)IG_NEXT_OF(t);
    }
    return 0;
}

/* ---- and giving it back --------------------------------------------- */

/* Three levels down and back up. Every array is freed and its pointer
   cleared, so a second call on the same tree is harmless -- which matters,
   because every road out of `GenerateESPR' that fails calls this. */
void pc_FreeBreathGroups(void *pc, void *groups, int32_t count)
{
    int32_t g, p, w;

    (void)pc;
    for (g = 0; g < count; g++) {
        uint8_t *bg = BG_AT(groups, g);

        for (p = 0; p < BG_B(bg, BG_PHRASES); p++) {
            uint8_t *ph = PH_AT(PTR_OF(bg, BG_PHRASE), p);

            for (w = 0; w < BG_B(ph, PH_WORDS); w++) {
                uint8_t *ap = AP_AT(PTR_OF(ph, PH_WORD), w);

                if (PTR_OF(ap, AP_MORA) != NULL) {
                    cpp_delete(PTR_OF(ap, AP_MORA));
                    PTR_OF(ap, AP_MORA) = NULL;
                }
            }
            if (PTR_OF(ph, PH_WORD) != NULL) {
                cpp_delete(PTR_OF(ph, PH_WORD));
                PTR_OF(ph, PH_WORD) = NULL;
            }
        }
        if (PTR_OF(bg, BG_PHRASE) != NULL) {
            cpp_delete(PTR_OF(bg, BG_PHRASE));
            PTR_OF(bg, BG_PHRASE) = NULL;
        }
    }
    if (groups != NULL)
        cpp_delete(groups);
}
