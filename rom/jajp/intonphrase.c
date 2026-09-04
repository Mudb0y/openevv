/* The intonation: where a speaker breathes, and what pitch each phrase gets.
 *
 * By the time this runs the sentence has been read: the path search picked a
 * way through the dictionary's candidates, PhraseBuf turned that into accent
 * phrases, and PhraseTable holds them as a chain. What is left is the part a
 * listener hears as Japanese rather than as a list of words -- which phrases
 * belong together in one breath, where the intonational phrases end, how
 * long each pause runs, and what pitch each phrase carries.
 *
 * `ThreePhraseParsing' is the whole of it in four lines: decide each phrase's
 * state, group them, and set the intonation, then tell TextAnalysis whether
 * the last of those could be done at all.
 *
 * A breath group is allocated from a pool of 707 -- one per phrase the
 * sentence could have -- through the same link chain JpnUtil::TableFree
 * splices and TextAnalysis holds one of. `TableAllocBG' is this class's own
 * copy of the other half of that, taking one entry off the free list.
 *
 * The record is IBM's and rom/jajp/intonphrase.h is the map, checked against
 * the object by `tools/rom/offsets.py intonphrase'.
 *
 * Held to IBM's answer by test/harness/romprims.sh.
 */

#include <stdint.h>
#include <string.h>
#include "jprom.h"
#include "intonphrase.h"
#include "txtanal.h"
#include "romanizer.h"

/* The five pointers, which are parked past the record. */
#define IP_VTABLE_OF(ip) (*(void **)IP_P((ip), IP_VTABLE_AT))
#define IP_OWNER_OF(ip)  (*(void **)IP_P((ip), IP_OWNER_AT))
#define IP_HEAD_OF(ip)   (*(void **)IP_P((ip), IP_HEAD_AT))
#define IP_CUR_OF(ip)    (*(void **)IP_P((ip), IP_CUR_AT))
#define IP_TABLE_OF(ip)  (*(void **)IP_P((ip), IP_TABLE_AT))

/* A run of a group's phrase reaches the slot before the one named, which is
   how SetAccentualPhrase folds two slots together. */
#define IH_PREV(run, at)  ((run) - 1 + (at))

/* One phrase of the table it is handed, and the next of them. */
#define PT_B(t, off)  (*((uint8_t *)(t) + (off)))
#define PT_P(t, off)  ((uint8_t *)(t) + (off))
#define PT_S16(t, off) (*(int16_t *)((uint8_t *)(t) + (off)))

/* And one breath group, and one phrase inside it. */
#define IG_P(bg, off)     ((uint8_t *)(bg) + (off))
#define IG_B(bg, off)     (*IG_P((bg), (off)))
#define IG_S16(bg, off)   (*(int16_t *)IG_P((bg), (off)))
#define IG_U16(bg, off)   (*(uint16_t *)IG_P((bg), (off)))
#define IG_PHRASE_AT(bg, i) IG_P((bg), IG_PHRASE + (i) * IG_PHRASE_SIZE)
#define IH_B(ph, off)     (*((uint8_t *)(ph) + (off)))
#define IH_S16(ph, off)   (*(int16_t *)((uint8_t *)(ph) + (off)))

/* ---- being made and unmade ------------------------------------------ */

/* IBM's constructor is in jpnrom.obj beside Romanizer's, which is what
   allocates the 432,204 bytes and keeps the answer at RZ_INTON. All it does
   is plant the vtable, and the class has one member in it: the destructor.
   Nothing here dispatches through it, so nought stands in that slot. */
void *ip_ctor(void *ip, void *owner)
{
    IP_VTABLE_OF(ip) = 0;
    IP_OWNER_OF(ip)  = owner;
    return ip;
}

/* The scalar deleting destructor, which is the shape MSVC gives a virtual
   destructor: the flag says whether to give the storage back as well. */
void *ip_destroy(void *ip, int32_t freeIt)
{
    IP_VTABLE_OF(ip) = 0;
    if (freeIt & 1)
        cpp_delete(ip);
    return ip;
}

/* ---- the pool of breath groups -------------------------------------- */

/* One entry off the free list and onto the used one, which is the other half
   of what JpnUtil::TableFree does.
 *
 * Three counters are passed by reference because the caller keeps them in
 * its own record: `count' is how many phrases there are, `last' the one most
 * recently taken, and `at' the next free. `n' is what stands for the end of
 * a list -- 707 rather than minus one, which is how TextAnalysis's chain is
 * initialised too. What comes back is the entry taken, or minus one when
 * there is none. */
#define IL_NEXT_OF(link, i) \
    (*(uint16_t *)((link) + (i) * IP_LINK_SIZE + IL_NEXT))
#define IL_PREV_OF(link, i) \
    (*(uint16_t *)((link) + (i) * IP_LINK_SIZE + IL_PREV))

int16_t ip_TableAllocBG(void *ip, uint16_t *count, uint16_t *last,
                        uint16_t *at, uint8_t *link, uint16_t n)
{
    uint16_t was = *last;
    uint16_t got = *at;

    (void)ip;
    if (got == n)
        return -1;

    *last = got;
    *at   = IL_NEXT_OF(link, got);

    if (was != n) {
        IL_NEXT_OF(link, was) = got;
    } else {
        IL_NEXT_OF(link, got) = n;
        IL_PREV_OF(link, got) = was;
        IL_PREV_OF(link, *at) = n;
    }

    if ((int16_t)IL_PREV_OF(link, *last) == (int16_t)n)
        *count = *last;
    return (int16_t)*last;
}

/* One breath group taken out of the pool and cleared.
 *
 * Three phrases to a group is IBM's own bound: this clears exactly three,
 * and three records of 0xc6 from four end where the group's own fields
 * begin. Two of the runs it clears are filled with minus one rather than
 * nought, which is what says they are indices into something rather than
 * counts. */
void *ip_BreathGroupAlloc(void *ip)
{
    int16_t  at;
    uint8_t *bg;
    int16_t  i, j;

    at = ip_TableAllocBG(ip, (uint16_t *)IP_P(ip, IP_COUNT),
                         (uint16_t *)IP_P(ip, IP_LEFT),
                         (uint16_t *)IP_P(ip, IP_AT),
                         IP_LINK_AT(ip, 0), IP_LINK_N);
    if (at < 0)
        return NULL;

    IG_S16(IP_GROUP_AT(ip, at), IG_INDEX) = at;
    bg = IP_GROUP_AT(ip, at);
    memset(bg, 0, IP_GROUP_SIZE);

    for (i = 0; i < IG_PHRASE_N; i++) {
        uint8_t *ph = IG_PHRASE_AT(bg, i);

        for (j = 0; j < IH_RUN_N; j++) {
            IH_B(ph, IH_LEN + j)       = 0;
            IH_B(ph, IH_A + j)         = 0;
            IH_B(ph, IH_MORAS + j)     = 0;
            IH_B(ph, IH_PITCH + j)     = 0;
            IH_B(ph, IH_MARK + j)      = 0;
        }
        IH_B(ph, IH_FLAG) = 0;
        for (j = 0; j < IH_E_N; j++) {
            IH_S16(ph, IH_E + j * 2) = -1;
            IH_B(ph, IH_F + j)       = 0xff;
        }
    }
    IG_NEXT_SET(bg, NULL);
    return bg;
}

/* ---- laying the pool out -------------------------------------------- */

/* The free list over as many breath groups as there are phrases, and the
   four counters the passes read. The chain is circular in the same way
   TextAnalysis's is: entry nought's back link is the count rather than
   minus one, so walking backwards from it runs off the end deliberately
   rather than by accident. */
void ip_InitPhraseTable(void *ip, int16_t n)
{
    int16_t i;

    IL_PREV_OF(IP_LINK_AT(ip, 0), 0) = (uint16_t)n;
    IL_NEXT_OF(IP_LINK_AT(ip, 0), 0) = 1;

    for (i = 1; i < n - 1; i++) {
        IL_PREV_OF(IP_LINK_AT(ip, 0), i) = (uint16_t)(i - 1);
        IL_NEXT_OF(IP_LINK_AT(ip, 0), i) = (uint16_t)(i + 1);
    }
    IL_PREV_OF(IP_LINK_AT(ip, 0), n - 1) = (uint16_t)(n - 2);
    IL_NEXT_OF(IP_LINK_AT(ip, 0), n - 1) = (uint16_t)n;

    IP_S16(ip, IP_COUNT) = n;
    IP_S16(ip, IP_LEFT)  = n;
    IP_S16(ip, IP_AT)    = 0;
    IP_S16(ip, IP_TOP)   = (int16_t)(n - 1);
}

/* ---- what state each phrase is in ----------------------------------- */

/* One phrase's three state bytes tidied. A phrase marked as continuing but
   with nothing to continue into is not continuing, and neither is one whose
   first byte says it stands alone. */
void ip_CheckPhraseToPhrase(void *ip, uint8_t *st)
{
    (void)ip;
    if (st[1] != 0 && st[2] == 0)
        st[1] = 0;
    if (st[0] & 0x10)
        st[1] = 0;
}

/* ---- the whole of the prosody --------------------------------------- */

/* Every phrase asked which breath group it belongs to, then the groups
   regrouped and each one's pitch set. */
void ip_ProsodyControl(void *ip)
{
    void *t;

    for (t = IP_TABLE_OF(ip); t != NULL; t = PT_NEXT_OF(t))
        PT_B(t, PT_GROUP) = ip_CheckBreathGroup(ip, PT_P(t, PT_STATE),
                                                PT_P(t, PT_RIGHT),
                                                PT_B(t, PT_KIND));
    ip_RegroupPhrases(ip);
    ip_SetPitchValues(ip);
}

/* ---- the entry point ------------------------------------------------ */

/* The class in four lines. What the last of them answers goes back to
   TextAnalysis as minus one or nought, which is the two bytes in front of
   its long-reading store and the only thing that writes them. */
void ip_ThreePhraseParsing(void *ip, void *table)
{
    int16_t answered;
    void   *ta;

    IP_TABLE_OF(ip) = table;
    ip_SetPhraseState(ip);
    ip_ProsodyControl(ip);
    answered = ip_SetIntonationalPhrase(ip);
    ta = *(void **)((uint8_t *)IP_OWNER_OF(ip) + RZ_TXTANAL_AT);
    *(int16_t *)((uint8_t *)ta + TA_INTON_FAILED) = (answered < 0) ? -1 : 0;
}

/* ---- what kind of phrase this is ------------------------------------ */

/* Seven states become seven numbers, and the numbers are not the states:
   they are what the pitch table below is indexed by, and IBM's own order.
   Anything else answers nought, which is what a state of one also answers,
   so the two are not distinguishable afterwards. */
int16_t ip_ModifyPType(void *ip, uint8_t type)
{
    (void)ip;
    switch (type) {
    case 0:  return 0x14;
    case 1:  return 0;
    case 2:  return 1;
    case 3:  return 2;
    case 4:  return 0x0a;
    case 5:  return 3;
    case 6:  return 0x0b;
    default: return 0;
    }
}

/* Which breath group a phrase belongs to, from the two state bytes in front
 * of it and the one behind.
 *
 * A phrase whose kind is more than one is not in a group at all and its
 * state is cleared. Otherwise the two bytes are read as a pair: whether each
 * is set, and whether each carries its top bit. The one case that needs both
 * is when they agree on the top bit and disagree on everything else, which
 * is a state of its own.
 *
 * The middle argument is IBM's and IBM never reads it. It is kept in the
 * signature because the mangled name says it is there and because the sweep
 * calls this by that name. */
uint8_t ip_CheckBreathGroup(void *ip, uint8_t *st, uint8_t *right,
                            uint8_t kind)
{
    uint8_t type;

    (void)right;
    if (kind > 1) {
        int16_t i;

        for (i = 0; i < 3; i++)
            st[i] = 0;
    }

    if (st[0] != 0) {
        if (st[1] == 0)
            type = (uint8_t)((st[0] & 0x80) ? 1 : 5);
        else if ((st[0] & 0x80) && (st[1] & 0x80))
            type = 2;
        else if (st[0] & 0x80)
            type = 1;
        else
            type = 4;
    } else if (st[1] != 0) {
        type = (uint8_t)((st[0] & 0x80) ? 4 : 6);
    } else {
        type = 0;
    }

    if (type == 2 && (st[0] ^ st[1]) != 0)
        type = 3;

    return (uint8_t)ip_ModifyPType(ip, type);
}

/* Whether the code at that place in a phrase's reading is a long vowel mark.
   Five codes are, and IBM asks about each of them separately. */
int16_t ip_CheckChoon(void *ip, const uint8_t *t, int16_t at)
{
    uint8_t c = *((const uint8_t *)t + PT_KANA + at);

    (void)ip;
    return (c == 0xf0 || c == 0xf1 || c == 0xf2 || c == 0xf3 || c == 0xf4)
           ? 1 : 0;
}

/* ---- three phrases at a time ---------------------------------------- */

/* Each phrase's three state bytes, decided from the phrase before it and the
 * phrase after. That window of three is what the class is named for: a
 * boundary is not a property of one phrase but of how it sits between its
 * neighbours, so the walk keeps a previous, a current and a next and writes
 * the answer onto the previous one.
 *
 * The first pass does that. The second undoes it wherever a phrase is not the
 * kind that can be in a breath group at all: its own state goes, and so do
 * the last two bytes of the phrase before it.
 */
void ip_SetPhraseState(void *ip)
{
    uint8_t  st[3];
    int32_t  odd = 0;
    void    *prev = IP_TABLE_OF(ip);
    void    *cur;
    void    *next;
    void    *t;
    int16_t  i;

    cur = PT_NEXT_OF(prev);
    if (cur != NULL) {
        next = PT_NEXT_OF(cur);
        if (PT_B(cur, PT_KIND) > 1)
            odd = 1;
        if (PT_B(prev, PT_KIND) == 1)
            st[0] = ip_PhraseParsing(ip, PT_P(prev, PT_RIGHT),
                                     PT_P(cur, PT_LEFT),
                                     PT_B(prev, PT_GROUP), odd);
        else
            st[0] = 1;

        while (next != NULL) {
            if (PT_B(next, PT_KIND) > 1)
                odd = 1;
            if (PT_B(cur, PT_KIND) == 1) {
                st[1] = ip_PhraseParsing(ip, PT_P(prev, PT_RIGHT),
                                         PT_P(next, PT_LEFT),
                                         PT_B(prev, PT_GROUP), odd);
                st[2] = ip_PhraseParsing(ip, PT_P(cur, PT_RIGHT),
                                         PT_P(next, PT_LEFT),
                                         PT_B(cur, PT_GROUP), odd);
            } else {
                st[1] = 0;
                st[2] = 0;
            }
            odd = 0;
            ip_CheckPhraseToPhrase(ip, st);
            for (i = 0; i < 3; i++)
                PT_B(prev, PT_STATE + i) = st[i];
            st[0] = st[2];

            prev = cur;
            cur  = next;
            next = PT_NEXT_OF(next);
        }

        st[1] = 0;
        st[2] = 0;
        for (i = 0; i < 3; i++)
            PT_B(prev, PT_STATE + i) = st[i];
        if (cur != NULL)
            for (i = 0; i < 3; i++)
                PT_B(cur, PT_STATE + i) = 0;
    }

    for (t = IP_TABLE_OF(ip); t != NULL; t = PT_NEXT_OF(t)) {
        if (PT_B(t, PT_KIND) > 1)
            for (i = 0; i < 3; i++)
                PT_B(t, PT_STATE + i) = 0;
        if (PT_NEXT_OF(t) != NULL
            && PT_B(PT_NEXT_OF(t), PT_KIND) > 1) {
            PT_B(t, PT_STATE + 1) = 0;
            PT_B(t, PT_STATE + 2) = 0;
        }
    }
}

/* What lies between two phrases, as one byte of flags.
 *
 * The two arguments are the tail of the phrase on the left and the head of
 * the one on the right -- four bytes each, out of PhraseTable's record. What
 * comes back has two halves. The top two bits say whether a boundary belongs
 * here at all, one for each of the two ways of asking: the two heads agreeing
 * in their top six bits, and the left head agreeing with the right one's
 * second byte. The low bits say what kind of boundary, which is chosen from
 * the group number the phrase carries and is IBM's own table of magic
 * numbers rather than anything derivable.
 *
 * `odd' is set when some phrase in the window is not the ordinary kind, and
 * it stands in for the agreement test where that test cannot be trusted.
 */
uint8_t ip_PhraseParsing(void *ip, uint8_t *l, uint8_t *r, uint8_t group,
                         int32_t odd)
{
    uint8_t out = 0;
    uint8_t both, mixed;
    int16_t which;

    (void)ip;
    if (r[0] == 2)
        r[0] = 0xe4;

    /* One head is answered outright and nothing else is looked at. */
    if (l[0] == 0x21) {
        out = 0x80;
        out = (uint8_t)(out | 0x10);
        out = (uint8_t)(out | 0x07);
        return out;
    }

    both = (uint8_t)((l[0] & 0xfc) & (r[0] & 0xfc));
    if (both != 0) {
        if (l[2] & 0x01) {
            if ((l[1] & r[2]) != 0 || odd == 1)
                out = (uint8_t)(out | 0x80);
        } else {
            out = (uint8_t)(out | 0x80);
        }
    } else if (((l[3] & 0x20) || (l[3] & 0x08)) && (r[3] & 0x40)) {
        out  = (uint8_t)(out | 0x80);
        both = 0x20;
    }

    mixed = (uint8_t)((l[0] & 0xfc) & (r[1] & 0xfc));
    if (mixed != 0) {
        if (l[2] & 0x01) {
            if ((l[1] & r[2]) != 0 || odd == 1)
                out = (uint8_t)(out | 0x40);
        } else {
            out = (uint8_t)(out | 0x40);
        }
    }

    which = 0;
    if (out == 0)
        return 0;

    /* Which bit agreed decides the kind, and the last one to agree wins. */
    if (out & 0x40) {
        if (mixed & 0x80) which = 8;
        if (mixed & 0x40) which = 7;
        if (mixed & 0x20) which = 4;
        if (mixed & 0x10) which = 2;
        if (mixed & 0x04) which = 9;
    }
    if (out & 0x80) {
        if (both & 0x80) which = 8;
        if (both & 0x40) which = 7;
        if (both & 0x20) which = 4;
        if (both & 0x10) which = 2;
        if (both & 0x04) which = 9;
    }

    if (which == 8 || which == 7) {
        out = (uint8_t)(out | 0x08);
        if (group < 0x1a || group > 0x44) {
            if (l[3] != 0)
                out = (uint8_t)(out | ((l[3] & 0x80) ? 0x06 : 0x01));
            else if (group == 0)
                out = (uint8_t)(out | 0x02);
            else if (group == 0x55 || group == 0x56)
                out = (uint8_t)(out | 0x03);
            else if (group >= 0x57 && group <= 0x59)
                out = (uint8_t)(out | 0x04);
            else if (l[2] & 0x10)
                out = (uint8_t)(out | 0x05);
        }
        if (l[2] & 0x80)
            out = (uint8_t)(out | 0x07);
    } else if (which == 4 || which == 3 || which == 0x0a || which == 2) {
        if (group == 0x4c || group == 0x4d || which == 0x0a) {
            /* Nothing: those three take the boundary as it stands. */
        } else if (group == 0) {
            out = (uint8_t)(out | 0x01);
        } else if (group >= 0x1a && group <= 0x44) {
            out = (uint8_t)(out | 0x02);
        } else if (group == 0x16 || group == 0x51 || group == 0x56) {
            out = (uint8_t)(out | 0x03);
        } else if (group == 0x58 || group == 0x59) {
            out = (uint8_t)(out | 0x04);
        } else {
            out = (uint8_t)(out | 0x07);
        }
    } else if (which == 9) {
        out = (uint8_t)(out | 0x10);
        out = (uint8_t)(out | 0x07);
    } else {
        out = 0;
    }
    return out;
}

/* ---- regrouping ------------------------------------------------------ */

/* Breath groups that came out too short or too long, put right.
 *
 * Two passes over the same shape. Each walks forward from a phrase until it
 * finds one that closes a group, adding up the moras as it goes, and then
 * asks two questions of the run: too few moras and the group is dissolved
 * into the next, too many and `PhraseSeparate' is asked to break it. What
 * differs between the passes is which marks close a run, what a dissolved
 * run is marked as, and which of the four bounds is used.
 *
 * Those four bounds are locals of IBM's, set to twenty-five, nought,
 * twenty-five and nought, and nothing changes them. The second pass tests
 * against one pair and passes the other to `PhraseSeparate', which cannot
 * matter while all four hold what they hold; it is written out as it stands
 * rather than folded, because folding it would hide the asymmetry.
 *
 * A one-mora run whose reading begins with the code for a long vowel is
 * dissolved as well, however many moras the arithmetic said: a single long
 * vowel is not a breath group.
 */
void ip_RegroupPhrases(void *ip)
{
    const int16_t limit1 = 0x19;
    const int16_t floor1 = 0;
    const int16_t limit2 = 0x19;
    const int16_t floor2 = 0;
    void   *cur;
    void   *start;
    void   *back;
    int16_t moras;

    cur = IP_TABLE_OF(ip);
    while (cur != NULL) {
        start = cur;
        back  = cur;
        moras = 0;
        while (PT_NEXT_OF(cur) != NULL && PT_B(cur, PT_GROUP) != 0x14) {
            moras = (int16_t)(moras + PT_B(cur, PT_MORAS));
            cur = PT_NEXT_OF(cur);
        }
        moras = (int16_t)(moras + PT_B(cur, PT_MORAS));

        if (moras < floor1) {
            PT_B(cur, PT_GROUP) = 0x0a;
            cur = back;
        } else if (moras == 1 && PT_B(cur, PT_KANA) == 0xfd) {
            PT_B(cur, PT_GROUP) = 0x0a;
            cur = back;
        } else {
            if (moras > limit1)
                ip_PhraseSeparate(ip, start, cur, moras, limit1, 0, 0x14);
            cur = PT_NEXT_OF(cur);
        }
    }

    cur = IP_TABLE_OF(ip);
    while (cur != NULL) {
        start = cur;
        back  = cur;
        moras = 0;
        while (PT_NEXT_OF(cur) != NULL
               && PT_B(cur, PT_GROUP) != 0x14
               && PT_B(cur, PT_GROUP) != 0x0a
               && PT_B(cur, PT_GROUP) != 0x0b) {
            moras = (int16_t)(moras + PT_B(cur, PT_MORAS));
            cur = PT_NEXT_OF(cur);
        }
        moras = (int16_t)(moras + PT_B(cur, PT_MORAS));

        if (moras < floor2) {
            PT_B(cur, PT_GROUP) = 4;
            cur = back;
        } else {
            if (moras > limit2)
                ip_PhraseSeparate(ip, start, cur, moras, limit1, floor1,
                                  0x0a);
            cur = PT_NEXT_OF(cur);
        }
    }
}

/* Where to break a group that came out too long.
 *
 * Given a run of phrases from `start' up to but not including `end', and how
 * many moras the whole run holds, this scores every phrase in it as a place to
 * put a boundary and marks the best one. The score is a goodness of fit times
 * a weight, and the two come from different places.
 *
 * The fit is where the break falls against a target. `per' is the moras the
 * run ought to be broken into -- the run divided by how many pieces of about
 * four fifths of the limit it holds, plus one -- and a third more than that
 * when the group has already been broken four times and this is a full break.
 * A break at exactly the target scores ninety; either side of it the score
 * ramps down to `ninety less chunk' at the edges, and a break within the floor
 * or beyond the limit scores nothing at all. `chunk' is how steep that ramp
 * is: sixty for a full break, ninety for a lesser one, so a lesser break has
 * to fit the target much better to be worth making.
 *
 * The weight is how willing the phrase itself is to be broken after, and comes
 * from the phrase data table at 0x2ae indexed by the phrase's own state, times
 * three, five or six depending on what the phrase is, or a flat sixty, seventy
 * or ninety for the three kinds that carry their own. A weight of nought is
 * raised to one, so a phrase the table refuses is still a candidate rather
 * than an impossibility.
 *
 * Having marked the best, it calls itself on what is left of the run if that
 * is still over the limit, so one long group comes apart into as many pieces
 * as it needs.
 *
 * Two smaller things. A run of one phrase is not broken; it is marked and the
 * answer is one, which is the only non-zero this returns. And the switch has
 * no default, so IBM's weight is whatever the previous phrase left there --
 * unreachable, since the loop stops before the phrase that closes the group
 * and every kind that can appear inside one has an arm; ours starts at nought
 * so the unreachable case is defined rather than the stack.
 */
int16_t ip_PhraseSeparate(void *ip, void *start, void *end, int16_t moras,
                          int16_t limit, int16_t floor_, int16_t mark)
{
    const uint8_t *pd;
    void   *p;
    void   *bestp = NULL;
    int16_t n;
    int16_t chunk;
    int16_t per;
    int16_t span;
    int16_t run = 0;
    int16_t best = -1;
    int16_t bestrun = 0;
    int16_t weight = 0;
    int16_t fit;
    int16_t state;
    uint8_t kind;
    int32_t take;

    p = start;
    n = 0;
    while (p != end) {
        p = PT_NEXT_OF(p);
        n = (int16_t)(n + 1);
    }
    if (n < 2) {
        PT_B(start, PT_GROUP) = (uint8_t)mark;
        return 1;
    }

    chunk = (int16_t)(mark == 0x14 ? 0x3c : 0x5a);

    if (PT_B(end, PT_KIND) >= 4 && mark == 0x14)
        per = (int16_t)(moras / (moras / (limit * 4 / 5) + 1) * 4 / 3);
    else if (mark == 0x0a || mark == 0x0b)
        per = (int16_t)(moras / 2);
    else
        per = (int16_t)(moras / (moras / (limit * 4 / 5) + 1));

    span = (int16_t)(per - floor_);

    for (p = start; p != end; p = PT_NEXT_OF(p)) {
        run = (int16_t)(run + PT_B(p, PT_MORAS));
        state = (int16_t)(PT_B(p, PT_STATE) & 0x1f);
        kind = PT_B(p, PT_GROUP);
        switch (kind) {
        case 0x00:
            pd = dm_GetPhraseDataPtr();
            weight = (int16_t)(pd[0x2ae + state] * 3);
            break;
        case 0x01:
            pd = dm_GetPhraseDataPtr();
            weight = (int16_t)(pd[0x2ae + state] * 5);
            break;
        case 0x02:
            pd = dm_GetPhraseDataPtr();
            weight = (int16_t)(pd[0x2ae + state] * 6);
            break;
        case 0x03:
            pd = dm_GetPhraseDataPtr();
            weight = (int16_t)(pd[0x2ae + state] * 5);
            break;
        case 0x04:
            weight = 0x3c;
            break;
        case 0x0a:
            weight = 0x46;
            break;
        case 0x0b:
            weight = 0x5a;
            break;
        default:
            break;
        }
        if (weight == 0)
            weight = 1;

        if (run <= floor_ || run >= limit) {
            fit = 0;
        } else if (run <= per) {
            fit = (int16_t)((chunk * run - chunk * floor_) / span
                            + 0x5a - chunk);
        } else if (run <= span + per) {
            if (span * chunk < chunk * (moras - run - floor_))
                take = span * chunk;
            else
                take = chunk * (moras - run - floor_);
            fit = (int16_t)((int16_t)take / span + 0x5a - chunk);
        } else {
            fit = 0;
        }

        if (best < (int16_t)(fit * weight)) {
            best = (int16_t)(fit * weight);
            bestrun = run;
            bestp = p;
        }
    }

    PT_B(bestp, PT_GROUP) = (uint8_t)mark;
    PT_B(bestp, PT_KIND) = 1;

    n = 0;
    for (p = bestp; p != end; p = PT_NEXT_OF(p))
        n = (int16_t)(n + 1);

    if (moras - bestrun > limit && n > 0)
        ip_PhraseSeparate(ip, PT_NEXT_OF(bestp), end,
                          (int16_t)(moras - bestrun), limit, 0, mark);
    return 0;
}

/* How long a pause follows each breath group.
 *
 * One pass over the chain of groups. Each group's pause is its own weight,
 * plus six tenths of a lookahead into the groups after it, all multiplied by
 * one and three tenths, then held between fifteen and sixty -- and then
 * stretched by a fifth, because the number that comes out of that arithmetic
 * is not yet the pause the synthesiser is given.
 *
 * The lookahead is where the work is. It compares the flags on this group's
 * right with those on the next group's left, and on the group after that, and
 * takes the bits the two have in common. Where the first pair have nothing in
 * common the pause looks two groups ahead and adds both weights; where they
 * have something in common but the second pair also do, it adds the second
 * weight halved; and where only the first pair do, it takes the next weight
 * alone. Bits three to five on this group's right override the lot with a flat
 * twenty.
 *
 * Four things are settled without arithmetic. The last group in the chain
 * takes a full second, nothing, a hundred and twenty-five or sixty, by its
 * kind. A group of kind three takes a hundred and twenty-five whatever the
 * arithmetic said. A group of kind two takes the pause given from outside. And
 * a group whose current phrase is bounded on both sides by exactly one is
 * skipped altogether.
 *
 * Two pieces of IBM's are dead and are kept as they stand rather than folded
 * away, because the fold would hide that they were there. The skip walks a
 * loop with an empty body before leaving, and the term for the group behind is
 * read out of a pointer that nothing in the function ever sets, so it is
 * always nought and the back weight is always nought with it.
 */
void ip_SetPauseLength(void *ip)
{
    void   *prev = NULL;
    void   *cur;
    void   *g;
    int16_t i;
    int16_t back;
    int16_t ahead;
    int16_t depth;
    int16_t base;
    int16_t v;
    uint8_t both[3];

    cur = IP_CUR_OF(ip);
    for (g = IP_HEAD_OF(ip); g != NULL; g = IG_NEXT_OF(g)) {
        if (IG_B(cur, IG_LEFT) == 1 && IG_B(cur, IG_RIGHT) == 1) {
            for (i = 0; i < IH_B(IG_PHRASE_AT(cur, 0), IH_KANA_LEN);
                 i = (int16_t)(i + 1))
                ;
            continue;
        }

        back  = 0;
        ahead = 0x14;
        if (prev != NULL && IG_S16(prev, IG_PAUSE) < 0x1e)
            back = IG_B(prev, IG_LEVEL);

        for (i = 0; i < 3; i = (int16_t)(i + 1))
            both[i] = 0;

        depth = 3;
        if (IG_NEXT_OF(g) != NULL) {
            void *n1 = IG_NEXT_OF(g);
            both[0] = (uint8_t)((IG_B(g, IG_RIGHT) & 0xfc)
                                & (IG_B(n1, IG_LEFT) & 0xfc));
            if (IG_NEXT_OF(n1) != NULL) {
                void *n2 = IG_NEXT_OF(n1);
                both[1] = (uint8_t)((IG_B(g, IG_RIGHT) & 0xfc)
                                    & (IG_B(n2, IG_LEFT) & 0xfc));
                both[2] = (uint8_t)((IG_B(n1, IG_RIGHT) & 0xfc)
                                    & (IG_B(n2, IG_LEFT) & 0xfc));
            } else {
                depth = 2;
            }
        } else {
            depth = 1;
        }

        if (both[0] == 0) {
            if ((IG_B(g, IG_RIGHT) & 0x38) != 0)
                ahead = 0x14;
            else if (depth == 3)
                ahead = (int16_t)(IG_B(IG_NEXT_OF(g), IG_LEVEL)
                                  + IG_B(IG_NEXT_OF(IG_NEXT_OF(g)),
                                         IG_LEVEL));
            else if (depth == 2)
                ahead = IG_B(IG_NEXT_OF(g), IG_LEVEL);
        } else if (both[1] != 0) {
            if (depth == 3)
                ahead = (int16_t)(IG_B(IG_NEXT_OF(g), IG_LEVEL)
                                  + IG_B(IG_NEXT_OF(IG_NEXT_OF(g)),
                                         IG_LEVEL) / 2);
            else if (depth == 2)
                ahead = IG_B(IG_NEXT_OF(g), IG_LEVEL);
        } else {
            if (depth != 1)
                ahead = IG_B(IG_NEXT_OF(g), IG_LEVEL);
        }

        if (IG_NEXT_OF(g) == NULL) {
            if (IG_B(g, IG_KIND) >= 4)
                IG_S16(g, IG_PAUSE) = 0x3e8;
            else if (IG_B(g, IG_KIND) == 2)
                IG_S16(g, IG_PAUSE) = 0;
            else if (IG_B(g, IG_KIND) == 3)
                IG_S16(g, IG_PAUSE) = 0x7d;
            else
                IG_S16(g, IG_PAUSE) = 0x3c;
        } else {
            base = IG_B(g, IG_LEVEL);
            v = (int16_t)((base + (back * 10 + 5) / 10
                           + (ahead * 6 + 5) / 10) * 13 + 0x37);
            IG_S16(g, IG_PAUSE) = (int16_t)(v / 10);
            if (IG_S16(g, IG_PAUSE) >= 0x3c)
                IG_S16(g, IG_PAUSE) = 0x3c;
            if (IG_S16(g, IG_PAUSE) <= 0x0f)
                IG_S16(g, IG_PAUSE) = 0x0f;
            if (IG_B(g, IG_KIND) == 3)
                IG_S16(g, IG_PAUSE) = 0x7d;
        }

        if (IG_B(g, IG_KIND) == 2)
            IG_S16(g, IG_PAUSE) = IP_S16(ip, IP_MORE);
        else if (IG_S16(g, IG_PAUSE) == 0x3e8)
            IG_S16(g, IG_PAUSE) = (int16_t)(IG_S16(g, IG_PAUSE) * 4 / 10
                                            + 0x58);
        else
            IG_S16(g, IG_PAUSE) = (int16_t)(IG_S16(g, IG_PAUSE) * 6 / 5);
    }
}

/* ---- pitch ----------------------------------------------------------- */

/* What pitch each mora of each phrase carries.
 *
 * Two passes over the phrase chain. The first sets every mora: a mora is
 * marked one where the mora after it is not nought, or where the phrase's
 * high half is in use at all, and nought otherwise; and it takes pitch four
 * where its accent slot holds one, two or three and differs from the mora
 * code, and pitch three otherwise. The high half's moras get the same mark
 * and a flat one in place of a pitch.
 *
 * The second pass puts a two on the last mora of some phrases, which is what
 * a listener hears as the end of an intonational phrase. Which phrases get it
 * is the whole of the pass: a phrase whose state is nought, or is one with a
 * state code of 0x8f. Of those, a phrase with something before it is refused
 * where its first mora already agrees with its accent slot, where its code is
 * 0x8b, and where its code is 0x82 or 0x88 and the phrase before it had state
 * nought. A phrase with nothing before it is refused only on 0x8b.
 *
 * The last mora is found by counting up to the first nought and stepping one
 * back, so a phrase whose first mora is already nought has the mark written
 * one byte in front of the run -- the fifteenth pitch. IBM's, and kept.
 */
void ip_SetPitchValues(void *ip)
{
    void   *p;
    void   *prev;
    int16_t i;
    uint8_t acc;

    for (p = IP_TABLE_OF(ip); p != NULL; p = PT_NEXT_OF(p)) {
        for (i = 0; i < PT_MORA_N; i = (int16_t)(i + 1)) {
            if (PT_B(p, PT_MORA + i) == 0)
                break;
            if (PT_B(p, PT_MORA + 1 + i) != 0)
                PT_B(p, PT_MORA_MARK + i) = 1;
            else if (PT_B(p, PT_MORA_HI) != 0)
                PT_B(p, PT_MORA_MARK + i) = 1;
            else
                PT_B(p, PT_MORA_MARK + i) = 0;

            if (PT_B(p, PT_MORA_ACC + i) == PT_B(p, PT_MORA + i)) {
                PT_B(p, PT_MORA_PITCH + i) = 3;
            } else {
                acc = PT_B(p, PT_MORA_ACC + i);
                if (acc > 0 && acc <= 3)
                    PT_B(p, PT_MORA_PITCH + i) = 4;
                else
                    PT_B(p, PT_MORA_PITCH + i) = 3;
            }
        }
        for (i = 0; i < PT_MORA_N; i = (int16_t)(i + 1)) {
            if (PT_B(p, PT_MORA_HI + i) == 0)
                break;
            if (PT_B(p, PT_MORA_HI + 1 + i) != 0)
                PT_B(p, PT_MORA_HI_MARK + i) = 1;
            else
                PT_B(p, PT_MORA_HI_MARK + i) = 0;
            PT_B(p, PT_MORA_HI_PITCH + i) = 1;
        }
    }

    prev = NULL;
    for (p = IP_TABLE_OF(ip); p != NULL; prev = p, p = PT_NEXT_OF(p)) {
        if (PT_B(p, PT_GROUP) != 0
            && !(PT_B(p, PT_GROUP) == 1 && PT_B(p, PT_STATE) == 0x8f))
            continue;

        if (PT_B(p, PT_STATE) == 0x8f)
            PT_B(p, PT_MORA_ACC) = 0;

        if (prev == NULL) {
            if (PT_B(p, PT_STATE) != 0x8f && PT_B(p, PT_STATE) == 0x8b)
                continue;
        } else if (PT_B(p, PT_STATE) != 0x8f) {
            if (PT_B(p, PT_MORA) == PT_B(p, PT_MORA_ACC))
                continue;
            if (PT_B(p, PT_STATE) == 0x82 && PT_B(prev, PT_GROUP) == 0)
                continue;
            if (PT_B(p, PT_STATE) == 0x8b)
                continue;
            if (PT_B(p, PT_STATE) == 0x88 && PT_B(prev, PT_GROUP) == 0)
                continue;
        }

        if (PT_B(p, PT_MORA_HI) == 0) {
            for (i = 0; i < PT_MORA_N && PT_B(p, PT_MORA + i) != 0;
                 i = (int16_t)(i + 1))
                ;
            PT_B(p, PT_MORA_MARK + i - 1) = 2;
        } else {
            for (i = 0; i < PT_MORA_N && PT_B(p, PT_MORA_HI + i) != 0;
                 i = (int16_t)(i + 1))
                ;
            PT_B(p, PT_MORA_HI_MARK + i - 1) = 2;
        }
    }
}

/* ---- intonational phrases -------------------------------------------- */

/* The pass that turns the phrase chain into breath groups.
 *
 * One walk over the chain. Each row's moras are copied into the group's
 * current phrase, slot by slot, and each row's reading is appended to it; a
 * row whose group mark reaches ten closes that phrase, and a mark of twenty or
 * a third phrase closes the group. Closing a group fills in its own fields
 * from the row that closed it, adds up how long its phrases run, corrects the
 * accent on its last phrase, and allocates the next group. The rows consumed
 * are given back to TextAnalysis's own free list as the walk passes them, so
 * the chain shortens behind it, and the answer is how many groups came out.
 *
 * Three things in it are worth naming.
 *
 * A phrase holds ten slots. When they are full the mora is not dropped: its
 * length is added into the last slot, so the phrase gets longer rather than
 * shorter. The same is done for a high-half mora the pass refuses.
 *
 * The two halves of a row's thirty moras are not treated alike, and this is
 * IBM's rather than a slip of ours. In the low half a mora is marked nought
 * only where its accent slot holds one, and one otherwise. In the high half it
 * is marked nought where the accent slot holds one and also where the mora is
 * longer than one code and the code after it is a long vowel -- the case the
 * low half marks one. The low half also reaches that byte through the current
 * group rather than the group in hand, which is the same object either way.
 *
 * A reading that ends in the code for a long vowel is shortened by one and the
 * slot it belonged to with it, and a long entry that pointed at exactly that
 * code is dropped. That is the only place the pass takes something back.
 *
 * Three of IBM's locals are set and never read: two counters cleared at the
 * top and a one written twice. They are left out, because there is nothing to
 * leave in.
 *
 * The first of the four reading rules writes a code and is then overwritten by
 * the last, because the test that would have skipped the last cannot be true
 * at the same time. It is transcribed as it stands.
 */
int16_t ip_SetIntonationalPhrase(void *ip)
{
    void   *bg;
    void   *p;
    void   *q;
    void   *next;
    void   *back;
    void   *ta;
    uint8_t *ph;
    uint8_t pIdx  = 0;
    uint8_t slot  = 0;
    uint8_t hold  = 0;
    uint8_t count;
    uint8_t last;
    int16_t i;
    int16_t j;
    int16_t at;
    int16_t accum  = 0;
    int16_t longAt = 0;
    int16_t groups = 0;

    bg = ip_BreathGroupAlloc(ip);
    if (bg == NULL)
        return -1;
    IP_HEAD_OF(ip) = bg;
    IP_CUR_OF(ip)  = bg;
    IG_NEXT_SET(bg, NULL);

    for (p = IP_TABLE_OF(ip); p != NULL; p = PT_NEXT_OF(p)) {
        count = 0;
        ph = IG_PHRASE_AT(bg, pIdx);
        IH_B(ph, IH_FLAG) = (uint8_t)(IH_B(ph, IH_FLAG) + PT_B(p, PT_HOLD));
        accum = 0;

        for (i = 0; i < PT_MORA_N; i = (int16_t)(i + 1)) {
            if (PT_B(p, PT_MORA + i) == 0)
                break;
            if (slot >= IH_RUN_N) {
                IH_B(ph, IH_PREV(IH_LEN, slot)) =
                    (uint8_t)(IH_B(ph, IH_PREV(IH_LEN, slot))
                              + PT_B(p, PT_MORA + i));
            } else {
                IH_B(ph, IH_LEN + slot) = PT_B(p, PT_MORA + i);
                IH_S16(ph, IH_VAL + slot * 2) =
                    (int16_t)(PT_S16(p, PT_MORA_VAL + i * 2) + accum);

                if (PT_B(p, PT_MORA_ACC + i) == 1)
                    IH_B(ph, IH_A + slot) = 0;
                else if (PT_B(p, PT_MORA + i) != 1
                         && ip_CheckChoon(ip, p, (int16_t)(count + 1)) > 0)
                    IH_B(ph, IH_A + slot) = 1;
                else
                    IH_B(IG_PHRASE_AT(IP_CUR_OF(ip), pIdx), IH_A + slot) = 1;

                if (PT_B(p, PT_MORA_ACC + i) == 0)
                    IH_B(ph, IH_MORAS + slot) = PT_B(p, PT_MORA + i);
                else
                    IH_B(ph, IH_MORAS + slot) = PT_B(p, PT_MORA_ACC + i);
                IH_B(ph, IH_PITCH + slot) = PT_B(p, PT_MORA_PITCH + i);
                IH_B(ph, IH_MARK + slot)  = PT_B(p, PT_MORA_MARK + i);
                slot = (uint8_t)(slot + 1);
            }
            count = (uint8_t)(count + PT_B(p, PT_MORA + i));
        }

        accum = IH_S16(ph, IH_VAL + slot * 2);
        for (i = 0; i < PT_MORA_N; i = (int16_t)(i + 1)) {
            if (PT_B(p, PT_MORA_HI + i) == 0)
                break;
            if (slot >= IH_RUN_N || count == 0
                || PT_B(p, PT_KANA - 1 + count) == 0xfd) {
                IH_B(ph, IH_PREV(IH_LEN, slot)) =
                    (uint8_t)(IH_B(ph, IH_PREV(IH_LEN, slot))
                              + PT_B(p, PT_MORA_HI + i));
            } else {
                IH_B(ph, IH_LEN + slot) = PT_B(p, PT_MORA_HI + i);
                IH_S16(ph, IH_VAL + slot * 2) =
                    PT_S16(p, PT_MORA_HI_VAL + i * 2);

                if (PT_B(p, PT_MORA_HI_ACC + i) == 1)
                    IH_B(ph, IH_A + slot) = 0;
                else if (PT_B(p, PT_MORA_HI + i) != 1
                         && ip_CheckChoon(ip, p, (int16_t)(count + 1)) > 0)
                    IH_B(ph, IH_A + slot) = 0;
                else
                    IH_B(ph, IH_A + slot) = 1;

                if (PT_B(p, PT_MORA_HI_ACC + i) == 0)
                    IH_B(ph, IH_MORAS + slot) = PT_B(p, PT_MORA_HI + i);
                else
                    IH_B(ph, IH_MORAS + slot) = PT_B(p, PT_MORA_HI_ACC + i);
                IH_B(ph, IH_MARK + slot)  = PT_B(p, PT_MORA_HI_MARK + i);
                IH_B(ph, IH_PITCH + slot) = PT_B(p, PT_MORA_HI_PITCH + i);
                slot = (uint8_t)(slot + 1);
            }
            count = (uint8_t)(count + PT_B(p, PT_MORA_HI + i));
        }

        for (i = 0; i < IH_E_N; i = (int16_t)(i + 1)) {
            if (PT_S16(p, PT_LONG + i * 2) < 0)
                break;
            IH_S16(ph, IH_E + longAt * 2) =
                (int16_t)(PT_S16(p, PT_LONG + i * 2) + hold);
            IH_B(ph, IH_F + longAt) = PT_B(p, PT_LONG_B + i);
            longAt = (int16_t)(longAt + 1);
        }
        IH_S16(ph, IH_LONG_N) = longAt;

        last = 0;
        for (i = 0; i < PT_B(p, PT_MORAS);
             i = (int16_t)(i + 1), hold = (uint8_t)(hold + 1)) {
            if (pIdx == 0 && hold == 0 && i == 0
                && PT_B(p, PT_KANA + i) == 0xfe)
                IH_B(ph, IH_KANA + hold) = 0xfa;

            if (pIdx == 0 && hold == 0 && i == 0
                && PT_B(p, PT_KANA + i) == 0xfd)
                IH_B(ph, IH_KANA + hold) = 0x32;
            else if (last == 0xfd && PT_B(p, PT_KANA + i) == 0x50)
                IH_B(ph, IH_KANA + hold) = 0x40;
            else
                IH_B(ph, IH_KANA + hold) = PT_B(p, PT_KANA + i);
            last = PT_B(p, PT_KANA + i);
        }

        if (last == 0xfd) {
            hold = (uint8_t)(hold - 1);
            IH_B(ph, IH_PREV(IH_LEN, slot)) =
                (uint8_t)(IH_B(ph, IH_PREV(IH_LEN, slot)) - 1);
            if (longAt > 0
                && IH_S16(ph, IH_E + (longAt - 1) * 2) == hold)
                longAt = (int16_t)(longAt - 1);
        }
        IH_B(ph, IH_KANA + hold) = 0xff;

        if (PT_B(p, PT_GROUP) < 0x0a)
            continue;

        IH_B(ph, IH_COUNT)    = slot;
        IH_B(ph, IH_KANA_LEN) = hold;
        IH_B(ph, IH_FIRST)    = (uint8_t)(pIdx == 0 ? 1 : 0);

        slot   = 0;
        hold   = 0;
        pIdx   = (uint8_t)(pIdx + 1);
        accum  = 0;
        longAt = 0;

        if (PT_B(p, PT_GROUP) < 0x14 && pIdx < IG_PHRASE_N)
            continue;

        IG_B(bg, IG_PHRASES) = pIdx;
        IG_B(bg, IG_LEFT)    = PT_B(p, PT_LEFT);
        IG_B(bg, IG_RIGHT)   = PT_B(p, PT_RIGHT);
        IG_B(bg, IG_KIND)    = PT_B(p, PT_KIND);
        IG_B(bg, IG_LEVEL)   = 0;
        for (j = 0; j < IG_B(bg, IG_PHRASES); j = (int16_t)(j + 1))
            IG_B(bg, IG_LEVEL) =
                (uint8_t)(IG_B(bg, IG_LEVEL)
                          + IH_B(IG_PHRASE_AT(bg, j), IH_KANA_LEN));

        ph = IG_PHRASE_AT(bg, pIdx - 1);
        at = (int16_t)(IH_B(ph, IH_COUNT) - 1);
        while (IH_B(ph, IH_PITCH + at) == 6)
            at = (int16_t)(at - 1);
        if (IH_B(ph, IH_MORAS + at) - IH_B(ph, IH_A + at) < 7) {
            switch (IH_B(ph, IH_PITCH + at)) {
            case 1: IH_B(ph, IH_PITCH + at) = 5; break;
            case 2: IH_B(ph, IH_PITCH + at) = 1; break;
            case 3: IH_B(ph, IH_PITCH + at) = 2; break;
            case 4: IH_B(ph, IH_PITCH + at) = 3; break;
            default: break;
            }
        }

        groups = (int16_t)(groups + 1);

        if (PT_NEXT_OF(p) != NULL) {
            bg = ip_BreathGroupAlloc(ip);
            if (bg == NULL)
                return -1;
            IG_NEXT_SET(IP_CUR_OF(ip), bg);
            IP_CUR_OF(ip)             = bg;
            IG_NEXT_SET(bg, NULL);
        }
        pIdx = 0;

        q    = IP_TABLE_OF(ip);
        next = NULL;
        while (q != p) {
            next = PT_NEXT_OF(q);
            ta = *(void **)((uint8_t *)IP_OWNER_OF(ip) + RZ_TXTANAL_AT);
            ju_TableFree((uint16_t *)((uint8_t *)ta + TA_LAST),
                         (uint16_t *)((uint8_t *)ta + TA_SPARE_18),
                         (uint16_t *)((uint8_t *)ta + TA_TOP),
                         (uint8_t *)ta + TA_LINK,
                         TA_LINK_N, (uint16_t)PT_S16(q, PT_INDEX));
            q = next;
        }
        IP_TABLE_OF(ip) = q;
    }

    /* The row the walk stopped on is given back too, and then any group that
       came out holding one phrase of no length at all is unlinked and given
       back; where such a group is dropped from the middle its kind is carried
       back to the group in front of it, but only where it is the greater. */
    ta = *(void **)((uint8_t *)IP_OWNER_OF(ip) + RZ_TXTANAL_AT);
    ju_TableFree((uint16_t *)((uint8_t *)ta + TA_LAST),
                 (uint16_t *)((uint8_t *)ta + TA_SPARE_18),
                 (uint16_t *)((uint8_t *)ta + TA_TOP),
                 (uint8_t *)ta + TA_LINK,
                 TA_LINK_N, (uint16_t)PT_S16(IP_TABLE_OF(ip), PT_INDEX));

    back = IP_HEAD_OF(ip);
    bg   = IP_HEAD_OF(ip);
    while (bg != NULL) {
        if (IG_B(bg, IG_PHRASES) == 1 && IG_B(bg, IG_LEVEL) == 0) {
            if (bg == IP_HEAD_OF(ip)) {
                IP_HEAD_OF(ip) = IG_NEXT_OF(bg);
            } else {
                IG_NEXT_SET(back, IG_NEXT_OF(bg));
                if (IG_B(bg, IG_KIND) > IG_B(back, IG_KIND))
                    IG_B(back, IG_KIND) = IG_B(bg, IG_KIND);
            }
            ju_TableFree((uint16_t *)IP_P(ip, IP_LEFT),
                         (uint16_t *)IP_P(ip, IP_AT),
                         (uint16_t *)IP_P(ip, IP_TOP),
                         IP_P(ip, IP_LINK),
                         IP_LINK_N, IG_U16(bg, IG_INDEX));
            bg = back;
        } else {
            back = bg;
        }
        bg = IG_NEXT_OF(bg);
    }

    ip_SetPauseLength(ip);
    return groups;
}

/* ---- accent phrases -------------------------------------------------- */

/* Fold one accent phrase of a breath group into the one before it.
 *
 * Given a group's phrase record and a slot in its five parallel runs, this
 * decides whether the slot can join the slot in front of it and does the join
 * if it can, answering the slot to carry on from -- one less where the join
 * happened, and the slot given back unchanged where it did not.
 *
 * Three things refuse the join outright: slot nought, which has nothing in
 * front of it; a slot whose predecessor's last run holds nought; and a slot
 * whose three first runs all hold one, which is a phrase of one mora that is
 * already as small as it goes. Beyond those, the join needs the two lengths
 * to agree and the state not to be one or six.
 *
 * The join itself adds the lengths, makes the earlier slot's second run the
 * old length plus the later slot's, marks the state three, carries the later
 * slot's last run back, and clears the later slot's five runs. Where it is
 * refused, the later slot's own state is marked two unless it already holds
 * one or two.
 *
 * IBM reads the state into a local before overwriting it and never uses the
 * local. It is left out, because there is nothing to leave in.
 */
uint8_t ip_SetAccentualPhrase(void *ip, void *ph, uint8_t at)
{
    int16_t len;
    int16_t moras;

    (void)ip;
    if (at == 0)
        return at;
    if (IH_B(ph, IH_PREV(IH_MARK, at)) == 0)
        return at;
    if (IH_B(ph, IH_PREV(IH_A, at)) == 1
        && IH_B(ph, IH_PREV(IH_MORAS, at)) == 1
        && IH_B(ph, IH_PREV(IH_LEN, at)) == 1)
        return at;

    if (IH_B(ph, IH_PREV(IH_MORAS, at)) == IH_B(ph, IH_PREV(IH_LEN, at))
        && IH_B(ph, IH_PREV(IH_PITCH, at)) != 6
        && IH_B(ph, IH_PREV(IH_PITCH, at)) != 1) {
        len   = (int16_t)(IH_B(ph, IH_PREV(IH_LEN, at)) + IH_B(ph, IH_LEN + at));
        moras = (int16_t)(IH_B(ph, IH_PREV(IH_LEN, at)) + IH_B(ph, IH_MORAS + at));

        IH_B(ph, IH_PREV(IH_LEN, at)) = (uint8_t)len;
        IH_B(ph, IH_PREV(IH_MORAS, at))   = (uint8_t)moras;
        IH_B(ph, IH_PREV(IH_PITCH, at))   = 3;
        IH_B(ph, IH_PREV(IH_MARK, at))   = IH_B(ph, IH_MARK + at);
        IH_B(ph, IH_LEN + at)      = 0;
        IH_B(ph, IH_MORAS + at)        = 0;
        IH_B(ph, IH_A + at)        = 0;
        IH_B(ph, IH_MARK + at)        = 0;
        IH_B(ph, IH_PITCH + at)        = 0;
        at = (uint8_t)(at - 1);
        return at;
    }

    if (IH_B(ph, IH_PITCH + at) != 1 && IH_B(ph, IH_PITCH + at) != 2)
        IH_B(ph, IH_PITCH + at) = 2;
    return at;
}
