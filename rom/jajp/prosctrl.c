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
#include "rom_tables_jajp.h"

/* Reaching into the four records. Every field is at IBM's own offset; the
   three pointers are parked past the record, which rom/jajp/prosctrl.h says
   why, so the strides are the parked sizes rather than IBM's. */
#define PC_S32(p, off)  (*(int32_t *)((uint8_t *)(p) + (off)))
#define BG_B(p, off)    (*((uint8_t *)(p) + (off)))
#define BG_S16(p, off)  (*(int16_t *)((uint8_t *)(p) + (off)))
#define BG_AT(a, i)     ((uint8_t *)(a) + (long)(i) * (long)BG_ROOM)
#define PH_AT(a, i)     ((uint8_t *)(a) + (long)(i) * (long)PH_ROOM)
#define AP_AT(a, i)     ((uint8_t *)(a) + (long)(i) * (long)AP_ROOM)
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

/* Whether this mora is stressed. It is where it falls inside the word's own
   run of moras -- at or after where the word starts and before where it ends
   -- and where the caller's flag says so whatever its place. */
int32_t pc_WriteStressLevel(void *pc, int32_t at, int32_t from, int32_t to,
                            char *out, uint32_t cap, uint32_t *len,
                            int32_t force)
{
    int32_t rc;

    if (force != 0 || (from <= at && at < to))
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

    groups = (uint8_t *)cpp_new((uint32_t)(n * (long)BG_ROOM));
    *outGroups = groups;
    if (groups == NULL)
        return -2;
    for (i = 0; i < n; i++)
        PTR_OF(BG_AT(groups, i), BG_PHRASE_AT) = NULL;
    *outCount = n;

    t = (const uint8_t *)bgt;
    for (g = 0; g < n; g++) {
        bg = BG_AT(groups, g);
        BG_B(bg, BG_LEVEL)   = IG_B_(t, IG_LEVEL);
        BG_B(bg, BG_PHRASES) = IG_B_(t, IG_PHRASES);
        BG_S16(bg, BG_PAUSE) = IG_S16_(t, IG_PAUSE);
        BG_B(bg, BG_KIND)    = IG_B_(t, IG_KIND);

        PTR_OF(bg, BG_PHRASE_AT) =
            cpp_new((uint32_t)(BG_B(bg, BG_PHRASES) * (long)PH_ROOM));
        if (PTR_OF(bg, BG_PHRASE_AT) == NULL)
            return -2;
        for (i = 0; i < BG_B(bg, BG_PHRASES); i++)
            PTR_OF(PH_AT(PTR_OF(bg, BG_PHRASE_AT), i), PH_WORD_AT) = NULL;

        for (p = 0; p < IG_B_(t, IG_PHRASES); p++) {
            ph  = PH_AT(PTR_OF(bg, BG_PHRASE_AT), p);
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

            PTR_OF(ph, PH_WORD_AT) =
                cpp_new((uint32_t)(BG_B(ph, PH_WORDS) * (long)AP_ROOM));
            if (PTR_OF(ph, PH_WORD_AT) == NULL)
                return -2;
            for (i = 0; i < BG_B(ph, PH_WORDS); i++)
                PTR_OF(AP_AT(PTR_OF(ph, PH_WORD_AT), i), AP_MORA_AT) = NULL;

            for (w = 0; w < BG_B(ph, PH_WORDS); w++) {
                ap    = AP_AT(PTR_OF(ph, PH_WORD_AT), w);
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

                PTR_OF(ap, AP_MORA_AT) =
                    cpp_new((uint32_t)(subCount * MO_SIZE));
                if (PTR_OF(ap, AP_MORA_AT) == NULL)
                    return -2;

                for (k = 0; k < subCount; k++) {
                    mo = MO_AT(PTR_OF(ap, AP_MORA_AT), k);
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
            uint8_t *ph = PH_AT(PTR_OF(bg, BG_PHRASE_AT), p);

            for (w = 0; w < BG_B(ph, PH_WORDS); w++) {
                uint8_t *ap = AP_AT(PTR_OF(ph, PH_WORD_AT), w);

                if (PTR_OF(ap, AP_MORA_AT) != NULL) {
                    cpp_delete(PTR_OF(ap, AP_MORA_AT));
                    PTR_OF(ap, AP_MORA_AT) = NULL;
                }
            }
            if (PTR_OF(ph, PH_WORD_AT) != NULL) {
                cpp_delete(PTR_OF(ph, PH_WORD_AT));
                PTR_OF(ph, PH_WORD_AT) = NULL;
            }
        }
        if (PTR_OF(bg, BG_PHRASE_AT) != NULL) {
            cpp_delete(PTR_OF(bg, BG_PHRASE_AT));
            PTR_OF(bg, BG_PHRASE_AT) = NULL;
        }
    }
    if (groups != NULL)
        cpp_delete(groups);
}

/* ---- one accent phrase's moras, as phonemes ------------------------- */

/* Every code of one mora written out, as a consonant, a vowel and the pitch
 * pairs between them.
 *
 * A code is one byte and holds both halves: divided by eight and one added it
 * is the consonant, one of thirty-two in `s_aszCname'; the remainder is the
 * vowel, one of eight in `s_aszVname'. Five of those eight are the plain
 * vowels; the other three are the doubled consonant, the syllabic nasal and
 * the long vowel, and each of those is written differently. A consonant of
 * thirty-one is the mark that says the vowel is long, and then it is spelled
 * out of `s_aszLVname' instead.
 *
 * Two things run across codes. A doubled consonant writes the consonant of
 * the code after it as well as its own vowel, and sets a flag so that the
 * next code writes neither its consonant nor its stress -- the doubling has
 * said both already. And a burst consonant takes three pitch pairs where
 * anything else takes two, which is what `IsBurstCons' is for.
 *
 * Everything goes into two places at once: a scratch line that is handed to
 * the output a mora at a time, and the caller's own running buffer, which
 * keeps the phonemes without any of the pitch or stress marks.
 */
int32_t pc_WriteGokiInfo(void *pc, const uint8_t *ap, int32_t which,
                         int32_t kind, int32_t *at, char *buf,
                         char *out, uint32_t cap, uint32_t *len)
{
    const uint8_t *mo = MO_AT(PTR_OF((void *)ap, AP_MORA_AT), which);
    char     line[256];
    int32_t  held = 0;
    int32_t  lastWord = 0;
    int32_t  i;
    int32_t  force;
    int32_t  vowel;
    uint8_t  cons;
    uint8_t  next = 0;   /* IBM leaves this unset until a code has one after
                            it, so a mora whose last code is a doubling reads
                            whatever the stack held. That cannot be
                            reproduced and cannot arise in a real reading: a
                            doubling exists to double the consonant that
                            follows it, so it is never last. */
    uint8_t  phon;

    if (kind == 5 && which == BG_B((void *)ap, AP_MORA_N) - 1)
        lastWord = 1;

    for (i = 0; i < BG_B(mo, MO_CODES); i++) {
        line[0] = '\0';
        cons = (uint8_t)(BG_B(mo, MO_CODE + i) / 8 + 1);
        phon = (uint8_t)(BG_B(mo, MO_CODE + i) % 8 + 0x29);

        if (held == 0) {
            force = 0;
            if (lastWord != 0) {
                if (i == BG_B(mo, MO_CODES) - 1)
                    force = 1;
                else if (phon == 0x2e && i == BG_B(mo, MO_CODES) - 2)
                    force = 1;
            }
            if (pc_WriteStressLevel(pc, *at + i, BG_B((void *)ap, AP_HEAD),
                                    BG_B((void *)ap, AP_LEN), out, cap, len,
                                    force) != 0)
                return -3;
        }

        vowel = phon - 0x29;
        if (i < BG_B(mo, MO_CODES) - 1)
            next = (uint8_t)(BG_B(mo, MO_CODE + i + 1) / 8 + 1);

        if (phon >= 0x29 && phon <= 0x2d) {
            if (cons >= 1 && cons <= 0x1e && held == 0) {
                strcat(line, (const char *)jajp_s_aszCname + cons * 3);
                strcat(buf,  (const char *)jajp_s_aszCname + cons * 3);
                if (pc_IsBurstCons(pc, cons))
                    strcat(line, PC_F0_THREE);
                else
                    strcat(line, PC_F0_TWO);
                strcat(line, (const char *)jajp_s_aszVname + vowel * 3);
                strcat(buf,  (const char *)jajp_s_aszVname + vowel * 3);
                strcat(line, PC_F0_TWO);
            } else if (cons == 0x1f) {
                strcat(line, (const char *)jajp_s_aszLVname + vowel * 3);
                strcat(buf,  (const char *)jajp_s_aszLVname + vowel * 3);
                strcat(line, PC_F0_TWO);
            } else {
                strcat(line, (const char *)jajp_s_aszVname + vowel * 3);
                strcat(buf,  (const char *)jajp_s_aszVname + vowel * 3);
                strcat(line, PC_F0_TWO);
            }
            held = 0;
        } else if (phon == 0x2e) {
            held = 1;
            strcat(line, (const char *)jajp_s_aszVname + vowel * 3);
            strcat(buf,  (const char *)jajp_s_aszVname + vowel * 3);
            strcat(line, (const char *)jajp_s_aszCname + next * 3);
            strcat(buf,  (const char *)jajp_s_aszCname + next * 3);
            /* The doubling asks a different question of the consonant after
               it than an ordinary consonant asks of itself: not whether it is
               a burst but whether it can carry a doubling at all, which is
               what the two predicates are named for. */
            if (pc_IsValidConsForSokuOn(pc, next))
                strcat(line, PC_F0_THREE);
            else
                strcat(line, PC_F0_TWO);
        } else if (phon == 0x2f) {
            strcat(line, (const char *)jajp_s_aszVname + vowel * 3);
            strcat(buf,  (const char *)jajp_s_aszVname + vowel * 3);
            strcat(line, PC_F0_TWO);
        }

        /* The last mora of the word takes a mark of its own in the running
           buffer, and nothing in the line. */
        if (*at + i + 1 == BG_B((void *)ap, AP_LEN))
            strcat(buf, "'");

        if (pc_WriteToOutBuf(pc, line, out, cap, len) != 0)
            return -3;
    }

    *at += BG_B(mo, MO_CODES);
    return 0;
}

/* ---- which moras go out as one word -------------------------------- */

/* Given a place in an accent phrase's moras, how far the word that starts
 * there runs, and what to call it.
 *
 * A mora's kind is one of ten and the ten do not all behave alike: four of
 * them are a word on their own, three run forward over every mora whose kind
 * is nine or more, one runs forward over everything until a ten, one takes the
 * kind of the mora after it and may take one more besides, and one is simply
 * renamed to nine. What comes back is where the word ends, how many codes it
 * holds -- capped at nine, which is as many as the notation can say -- and
 * the name of its part of speech out of one of two tables.
 *
 * A boundary at the end of the phrase overrides the kind with the negative of
 * itself, and a negative kind is looked up in the second table: that is how a
 * comma, a full stop, a question and an exclamation are named. Four becomes
 * minus four whichever way round, since a kind of six also answers minus
 * four.
 *
 * IBM works out whether this is the last mora of an unstressed phrase and
 * never reads the answer. It is left out.
 */
int32_t pc_GetGokiInfoToWrite(void *pc, const uint8_t *ap, int32_t *m,
                              int32_t *first, int32_t *upto, int32_t *pos,
                              const char **name, int32_t stress,
                              int32_t kind)
{
    const uint8_t *mo;
    const uint8_t *next;
    int32_t where;
    int32_t moras = 0;
    int32_t which;
    int32_t scan;

    (void)pc;
    (void)stress;

    *first = *m;
    where  = *first;
    mo     = MO_AT(PTR_OF((void *)ap, AP_MORA_AT), where);
    if (where < BG_B((void *)ap, AP_MORA_N) - 1)
        next = MO_AT(PTR_OF((void *)ap, AP_MORA_AT), where + 1);
    else
        next = NULL;

    moras += BG_B(mo, MO_CODES);
    which = BG_S16(mo, MO_KIND);

    switch (which) {
    case 1:
    case 5:
    case 6:
    case 7:
        *upto = where;
        break;

    case 2:
    case 3:
    case 4:
        scan = where + 1;
        while (scan < BG_B((void *)ap, AP_MORA_N)) {
            if (BG_S16(next, MO_KIND) < 9)
                break;
            where++;
            moras += BG_B(next, MO_CODES);
            if (BG_S16(next, MO_KIND) == 0x0a)
                break;
            if (scan < BG_B((void *)ap, AP_MORA_N))
                next = MO_AT(PTR_OF((void *)ap, AP_MORA_AT), scan);
            scan++;
        }
        *upto = where;
        break;

    case 9:
        scan = where + 1;
        while (scan < BG_B((void *)ap, AP_MORA_N)) {
            where++;
            moras += BG_B(next, MO_CODES);
            if (BG_S16(next, MO_KIND) == 0x0a)
                break;
            if (scan < BG_B((void *)ap, AP_MORA_N))
                next = MO_AT(PTR_OF((void *)ap, AP_MORA_AT), scan);
            scan++;
        }
        *upto = where;
        break;

    case 8:
        if (next == NULL || BG_S16(next, MO_KIND) >= 9) {
            *upto = where;
            break;
        }
        which = BG_S16(next, MO_KIND);
        *upto = where + 1;
        where++;
        moras += BG_B(next, MO_CODES);
        if (which == 2 || which == 3 || which == 4) {
            if (BG_B((void *)ap, AP_MORA_N) > where + 1
                && BG_S16(MO_AT(PTR_OF((void *)ap, AP_MORA_AT), where + 1),
                          MO_KIND) >= 9) {
                *upto = where + 1;
                where++;
                mo = MO_AT(PTR_OF((void *)ap, AP_MORA_AT), where);
                moras += BG_B(mo, MO_CODES);
            }
        }
        break;

    case 0x0a:
        which = 9;
        *upto = where;
        break;

    default:
        *upto = where;
        break;
    }

    *m   = *upto;
    *pos = BG_B((void *)ap, AP_PITCH);

    if (kind > 0 && where == BG_B((void *)ap, AP_MORA_N) - 1) {
        switch (kind) {
        case 3:
        case 5:
            which = -kind;
            break;
        case 4:
            which = -kind;
            break;
        case 6:
            which = -4;
            break;
        default:
            which = -3;
            break;
        }
    }

    *pos = (moras < 9) ? moras : 9;

    if (which >= 0 && which < jajp_s_aszPosInfo_n)
        *name = jajp_s_aszPosInfo[which];
    else if (which < 0 && -which < jajp_s_aszSpecialPosInfo_n)
        *name = jajp_s_aszSpecialPosInfo[-which];
    else
        *name = "undef";
    return 0;
}

/* ---- the whole tree as one string ----------------------------------- */

/* Four loops and a great deal of punctuation.
 *
 * A group opens with the caller's index marks, then its own prominence where
 * it has one, then the pause and boundary that belong to it, and -- for the
 * first group only -- whatever parameter `GenerateESPR' was handed. Then
 * every phrase, every word of it and every mora of that: a word is bracketed
 * by its part of speech and closed by its prominence, and its moras go
 * through `WriteGokiInfo'. The group closes with its pause in hundredths, a
 * pitch pair for every code it holds, and the phonemes of the whole group
 * again with no marks in them, which is what the second buffer has been
 * collecting.
 *
 * The length handed to `WriteToOutBuf' starts at one rather than nought, so a
 * byte of the caller's buffer is held back; and nothing here writes the
 * terminator the first `strcat' needs, so the caller's buffer arrives already
 * a string. Both are IBM's.
 *
 * The parameter is cleared after the first group, so a second call in the
 * same utterance does not state it again.
 */
int32_t pc_WriteESPR2(void *pc, void *groups, int32_t count, int32_t ms,
                      char *out, uint32_t cap)
{
    char     line[256];
    uint32_t len = 1;
    char    *buf = NULL;
    uint8_t *bg;
    uint8_t *ph;
    uint8_t *ap;
    int32_t  g, p, w, m, i, k;
    int32_t  phrases;
    int32_t  room;
    int32_t  marks;
    int32_t  prom;
    int32_t  last;
    int32_t  stress;
    int32_t  kind;
    int32_t  at;
    int32_t  first;
    int32_t  upto;
    int32_t  pos = 0;
    const char *name;

    line[0] = '\0';
    for (g = 0; g < count; g++) {
        bg      = BG_AT(groups, g);
        phrases = BG_B(bg, BG_PHRASES);
        room    = BG_B(bg, BG_LEVEL) * 5 + 1;

        buf = (char *)cpp_new((uint32_t)room);
        if (buf == NULL)
            return -2;
        buf[0] = '\0';

        marks = 0;
        prom  = 0;
        for (i = 0; i < phrases; i++) {
            uint8_t *one = PH_AT(PTR_OF(bg, BG_PHRASE_AT), i);

            marks += (uint16_t)BG_S16(one, PH_AT4);
            prom  += BG_B(one, PH_FLAG);
        }

        if (pc_WriteUserIndex(pc, marks, out, cap, &len) != 0)
            goto refuse;

        /* The prominence is stated only where there is one, but the brace
           after it and the write of the line are not conditional -- and the
           line is cleared once, before the first group, and never between
           them. So a group with no prominence of its own sends out whatever
           the group before it last put in that line, with the brace on the
           end. IBM's, and it shows in every group after the first. */
        if (prom > 0)
            sprintf(line, "%s%d%s", " `g", (int)prom, "_ ");
        strcat(line, "`{ ");
        if (pc_WriteToOutBuf(pc, line, out, cap, &len) != 0)
            goto refuse;

        last = (g == count - 1) ? 1 : 0;
        if (pc_WriteBGInfo(pc, g + 1, BG_B(bg, BG_KIND), last, out, cap,
                           &len) != 0)
            goto refuse;

        if (g == 0 && PC_S32(pc, PC_ARG) != 0) {
            line[0] = '\0';
            sprintf(line, "%s%d%s w0 ", "#(p", (int)PC_S32(pc, PC_ARG), ")");
            if (pc_WriteToOutBuf(pc, line, out, cap, &len) != 0) {
                line[0] = '\0';
                goto refuse;
            }
            line[0] = '\0';
        }

        for (p = 0; p < BG_B(bg, BG_PHRASES); p++) {
            ph = PH_AT(PTR_OF(bg, BG_PHRASE_AT), p);

            for (w = 0; w < BG_B(ph, PH_WORDS); w++) {
                ap    = AP_AT(PTR_OF(ph, PH_WORD_AT), w);
                at    = 0;
                upto  = -1;
                first = upto;

                for (m = 0; m < BG_B(ap, AP_MORA_N); m++) {
                    if (last != 0 && BG_B(bg, BG_KIND) <= 4
                        && p == BG_B(bg, BG_PHRASES) - 1
                        && w == BG_B(ph, PH_WORDS) - 1)
                        stress = 0;
                    else
                        stress = 1;

                    if (p == BG_B(bg, BG_PHRASES) - 1
                        && w == BG_B(ph, PH_WORDS) - 1)
                        kind = BG_B(bg, BG_KIND);
                    else
                        kind = 0;

                    name = NULL;
                    pc_GetGokiInfoToWrite(pc, ap, &m, &first, &upto, &pos,
                                          &name, stress, kind);

                    if (name != NULL)
                        sprintf(line, "%s%s,%d %s", "<", name, (int)pos, "[");
                    else
                        sprintf(line, "%s%s,%d %s", "<", "undef", (int)pos,
                                "[");
                    if (pc_WriteToOutBuf(pc, line, out, cap, &len) != 0)
                        goto refuse;

                    for (k = first; k <= upto; k++)
                        if (pc_WriteGokiInfo(pc, ap, k, kind, &at, buf, out,
                                             cap, &len) != 0)
                            goto refuse;

                    line[0] = '\0';
                    if (w == BG_B(ph, PH_WORDS) - 1
                        && m == BG_B(ap, AP_MORA_N) - 1)
                        sprintf(line, "%s %s%d%s ", "]", "w",
                                (int)BG_B(PH_AT(PTR_OF(bg, BG_PHRASE_AT), p),
                                          PH_FLAG), ">");
                    else
                        sprintf(line, "%s %s0%s ", "]", "w", ">");
                    if (pc_WriteToOutBuf(pc, line, out, cap, &len) != 0)
                        goto refuse;
                }
                strcat(buf, " ");
            }
        }

        sprintf(line, "%s%d%s %s ", "#(p",
                (int)(BG_S16(bg, BG_PAUSE) * ms / 100.0), ")", "}");
        if (pc_WriteToOutBuf(pc, line, out, cap, &len) != 0)
            goto refuse;
        if (pc_WriteDummyF0Pair(pc, BG_B(bg, BG_LEVEL), out, cap, &len) != 0)
            goto refuse;
        if (pc_WriteToOutBuf(pc, " % ", out, cap, &len) != 0)
            goto refuse;

        /* The phonemes of the whole group, and then the buffer goes back
           whether that succeeded or not. */
        {
            int32_t rc = pc_WriteToOutBuf(pc, buf, out, cap, &len);

            cpp_delete(buf);
            buf = NULL;
            if (rc != 0)
                return -3;
        }
        if (pc_WriteToOutBuf(pc, "% / ", out, cap, &len) != 0)
            return -3;
        PC_S32(pc, PC_ARG) = 0;
    }
    return 0;

refuse:
    if (buf != NULL) {
        cpp_delete(buf);
        buf = NULL;
    }
    return -3;
}

/* ---- the entry point ------------------------------------------------ */

/* Two lines of work and four refusals. What comes in is the environment,
 * whose first field says which of two modes this is, and either a string or a
 * chain of breath groups; what goes out is the ESPR text.
 *
 * The string road is not implemented in the object as shipped: given text
 * rather than groups, IBM sets its error to minus one unconditionally, frees
 * the tree it has not built and refuses. It is transcribed as it stands.
 *
 * Note the tree is not freed on the road that succeeds. Whether ours diverges
 * there is a question for whoever writes `Romanizer', which is the only
 * caller and may free it itself; until that is read, this does what IBM does.
 */
int32_t pc_GenerateESPR(void *pc, const void *env, int32_t param,
                        const char *text, const void *bgt, char *out,
                        uint32_t cap)
{
    void   *groups = NULL;
    int32_t count  = 0;

    if (env == NULL)
        return -1;
    if (text == NULL && bgt == NULL)
        return -1;
    if (out == NULL)
        return -1;
    if (cap == 0)
        return -1;

    PC_S32(pc, PC_ARG) = param;
    switch (*(const int32_t *)env) {
    case 1:
        PC_S32(pc, PC_MODE) = 1;
        break;
    case 2:
        PC_S32(pc, PC_MODE) = 2;
        break;
    default:
        return -1;
    }

    if (text != NULL) {
        pc_FreeBreathGroups(pc, groups, count);
        return -1;
    }
    if (pc_BG_T2BreathGroups(pc, bgt, &groups, &count) != 0) {
        pc_FreeBreathGroups(pc, groups, count);
        return -1;
    }
    return pc_WriteESPR2(pc, groups, count, 0x64, out, cap);
}
