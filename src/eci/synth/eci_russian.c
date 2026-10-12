/* Russian, said by the English module from phonemes.
 *
 * Not one of IBM's objects: ours, like eci_synthtext.c's code point table.
 * There is no Russian module in the tree and nothing to lift one from, and no
 * module reads Russian spelling for itself. What the US English module has is
 * the fullest phonology and prosody of the eleven, and an annotation that
 * takes a pronunciation in its own phoneme alphabet with each syllable's
 * stress marked -- `[.0x.1bWt] is about. lang/ruru is that module under
 * Russian's own number, and this writes each Russian word as one of those
 * annotations before the module sees it.
 *
 * What a word goes through, in order:
 *
 *   its stress: the mark over a vowel if the text has one, ё if it has one,
 *   the table in eci_russian_stress.h, which tools/russian/stress.py writes
 *   out of a list of words, and then rules by ending -- the adjectives and
 *   the nouns in -ие and -ия on the last vowel of the stem, the infinitive
 *   and the past tense on the vowel of their suffix, -ист, -изм, -ант, -ок
 *   and the like on themselves -- and the penultimate syllable when nothing
 *   says otherwise. Prepositions, conjunctions and particles among other
 *   words are not stressed at all;
 *
 *   the spellings Russian does not say as written: что is што, the genitive
 *   -ого and -его are -ово and -ево, -тся and -ться are ца, сч is щ, and the
 *   consonants Russian drops in здравствуйте, солнце and поздно;
 *
 *   its sounds: unstressed о and а reduced, to the module's H in the
 *   syllable before the stress and at the start of a word and to its schwa
 *   elsewhere, unstressed е and я to I, ы and и after ж, ш and ц to X -- the
 *   reduced vowel of roses, which is ы -- and a consonant softened by е, ё, ю
 *   or я given the glide y after it, which is how an English voice comes
 *   nearest to a soft consonant; voicing assimilated from the right, so
 *   сделать is zdelat and вода at the end of a phrase keeps its voice where
 *   код loses it; and the result cut into syllables with the stress marked.
 *
 * And before any word, numbers are said in Russian words, cardinals and the
 * ordinals written with a hyphen and an ending (1-го, 5-й); a word that is one
 * consonant on its own is said by its Russian name, which is what a reader
 * moving a letter at a time wants, except в, к and с before a word, which are
 * prepositions and are said as part of it; and a word with no vowel, СССР, is
 * spelled.
 *
 * Everything not Cyrillic and not a digit goes through as it came, so Latin
 * text inside Russian is read as the English it most likely is, and so does an
 * annotation from its backquote to the next space, digits and all. The
 * caller has to have annotations on, which every screen reader driver does.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#ifdef _WIN32
#include <windows.h>
#endif

#include "eci_russian.h"

/* ---- the alphabet ------------------------------------------------------ */

enum {
    A = 0x430, BE, VE, GE, DE, IE, ZHE, ZE, I, IK, KA, EL, EM, EN, O, PE,
    ER, ES, TE, U, EF, HA, TSE, CHE, SHA, SHCHA, HARD, YERY, SOFT, E, YU, YA,
    YO = 0x451
};

static int isCyrillic(uint32_t c)
{
    return (c >= 0x410 && c <= 0x44f) || c == 0x401 || c == 0x451
        /* Ukrainian and Belarusian letters, read as their nearest Russian. */
        || c == 0x404 || c == 0x454 || c == 0x406 || c == 0x456
        || c == 0x407 || c == 0x457 || c == 0x490 || c == 0x491
        || c == 0x40e || c == 0x45e;
}

static int isUpper(uint32_t c)
{
    return (c >= 0x410 && c <= 0x42f) || (c >= 0x400 && c <= 0x40f)
        || c == 0x490;
}

static uint32_t lower(uint32_t c)
{
    if (c >= 0x410 && c <= 0x42f)
        return c + 0x20;
    if (c >= 0x400 && c <= 0x40f)
        c += 0x50;
    switch (c) {
    case 0x454: return IE;      /* є */
    case 0x456: return I;       /* і */
    case 0x457: return I;       /* ї */
    case 0x491: return GE;      /* ґ */
    case 0x45e: return U;       /* ў */
    case 0x490: return GE;
    }
    return c;
}

static int isVowel(uint32_t c)
{
    return c == A || c == IE || c == YO || c == I || c == O || c == U
        || c == YERY || c == E || c == YU || c == YA;
}

static int isIotated(uint32_t c)
{
    return c == IE || c == YO || c == YU || c == YA;
}

static int isConsonant(uint32_t c)
{
    return c >= A && c <= YA && !isVowel(c) && c != HARD && c != SOFT;
}

/* ---- a growing buffer of code points ----------------------------------- */

typedef struct {
    uint32_t *at;
    size_t    n;
    size_t    room;
    int       failed;
} Buf;

static void put(Buf *b, uint32_t c)
{
    if (b->failed)
        return;
    if (b->n == b->room) {
        size_t room = b->room ? b->room * 2 : 256;
        uint32_t *at = (uint32_t *)realloc(b->at, room * sizeof *at);

        if (at == 0) {
            b->failed = 1;
            return;
        }
        b->at = at;
        b->room = room;
    }
    b->at[b->n++] = c;
}

/* A string of UTF-8, which is how every word below is written. */
static void putUtf8(Buf *b, const char *s)
{
    const unsigned char *p = (const unsigned char *)s;

    while (*p) {
        uint32_t c = *p++;

        if (c >= 0xf0) {
            c = (c & 0x07) << 18;
            c |= (uint32_t)(*p++ & 0x3f) << 12;
            c |= (uint32_t)(*p++ & 0x3f) << 6;
            c |= (uint32_t)(*p++ & 0x3f);
        } else if (c >= 0xe0) {
            c = (c & 0x0f) << 12;
            c |= (uint32_t)(*p++ & 0x3f) << 6;
            c |= (uint32_t)(*p++ & 0x3f);
        } else if (c >= 0xc0) {
            c = (c & 0x1f) << 6;
            c |= (uint32_t)(*p++ & 0x3f);
        }
        put(b, c);
    }
}

/* ---- numbers ----------------------------------------------------------- */

static const char *const UNITS[] = {
    "ноль", "один", "два", "три", "четыре", "пять", "шесть", "семь",
    "восемь", "девять"
};
static const char *const UNITS_F[] = {
    "ноль", "одна", "две", "три", "четыре", "пять", "шесть", "семь",
    "восемь", "девять"
};
static const char *const TEENS[] = {
    "десять", "одиннадцать", "двенадцать", "тринадцать", "четырнадцать",
    "пятнадцать", "шестнадцать", "семнадцать", "восемнадцать",
    "девятнадцать"
};
static const char *const TENS[] = {
    "", "", "двадцать", "тридцать", "сорок", "пятьдесят", "шестьдесят",
    "семьдесят", "восемьдесят", "девяносто"
};
static const char *const HUNDREDS[] = {
    "", "сто", "двести", "триста", "четыреста", "пятьсот", "шестьсот",
    "семьсот", "восемьсот", "девятьсот"
};

/* Each scale's one, few and many, and whether it counts in the feminine. */
static const struct {
    const char *forms[3];
    int feminine;
} SCALES[] = {
    { { "", "", "" }, 0 },
    { { "тысяча", "тысячи", "тысяч" }, 1 },
    { { "миллион", "миллиона", "миллионов" }, 0 },
    { { "миллиард", "миллиарда", "миллиардов" }, 0 },
};

static int pluralForm(unsigned n)
{
    unsigned t = n % 100;

    if (t >= 11 && t <= 19)
        return 2;
    if (n % 10 == 1)
        return 0;
    if (n % 10 >= 2 && n % 10 <= 4)
        return 1;
    return 2;
}

static void space(Buf *b)
{
    if (b->n > 0 && b->at[b->n - 1] != ' ')
        put(b, ' ');
}

static void word(Buf *b, const char *w)
{
    if (!*w)
        return;
    space(b);
    putUtf8(b, w);
}

/* Below a thousand, which is what every scale counts in. */
static void hundreds(Buf *b, unsigned n, int feminine)
{
    word(b, HUNDREDS[n / 100]);
    n %= 100;
    if (n >= 10 && n < 20) {
        word(b, TEENS[n - 10]);
        return;
    }
    word(b, TENS[n / 10]);
    if (n % 10)
        word(b, (feminine ? UNITS_F : UNITS)[n % 10]);
}

static void cardinal(Buf *b, unsigned long long n)
{
    unsigned groups[4];
    int g;

    if (n == 0) {
        word(b, UNITS[0]);
        return;
    }
    for (g = 0; g < 4; g++) {
        groups[g] = (unsigned)(n % 1000);
        n /= 1000;
    }
    for (g = 3; g >= 0; g--) {
        if (groups[g] == 0)
            continue;
        hundreds(b, groups[g], SCALES[g].feminine);
        if (g > 0)
            word(b, SCALES[g].forms[pluralForm(groups[g])]);
    }
}

/* The ordinal stems, and which of them take a stressed ending: второй
   rather than второый. Третий is its own word throughout and is below. */
static const char *const ORD_UNITS[] = {
    "нулев", "перв", "втор", "трет", "четвёрт", "пят", "шест", "седьм",
    "восьм", "девят"
};
static const char *const ORD_TEENS[] = {
    "десят", "одиннадцат", "двенадцат", "тринадцат", "четырнадцат",
    "пятнадцат", "шестнадцат", "семнадцат", "восемнадцат", "девятнадцат"
};
static const char *const ORD_TENS[] = {
    "", "", "двадцат", "тридцат", "сороков", "пятидесят", "шестидесят",
    "семидесят", "восьмидесят", "девяност"
};
static const char *const ORD_HUNDREDS[] = {
    "", "сот", "двухсот", "трёхсот", "четырёхсот", "пятисот", "шестисот",
    "семисот", "восьмисот", "девятисот"
};

/* What an ordinal's ending is, as the text wrote it after the hyphen, and
   the ending the word takes: an ordinary one, a stressed one and the soft one
   третий wants. */
static const struct {
    const char *written[4];
    const char *plain, *stressed, *third;
} ENDINGS[] = {
    { { "й", "ый", "ий", "ой" }, "ый", "ой", "ий" },
    { { "я", "ая", "ья", 0 }, "ая", "ая", "ья" },
    { { "е", "ое", "ье", 0 }, "ое", "ое", "ье" },
    { { "го", "ого", "его", 0 }, "ого", "ого", "ьего" },
    { { "му", "ому", "ему", 0 }, "ому", "ому", "ьему" },
    { { "м", "ом", "ем", 0 }, "ом", "ом", "ьем" },
    { { "х", "ых", "их", 0 }, "ых", "ых", "ьих" },
    { { "ми", "ыми", "ими", 0 }, "ыми", "ыми", "ьими" },
    { { "ю", "ую", "ью", 0 }, "ую", "ую", "ью" },
};

static int stressedStem(const char *stem)
{
    return !strcmp(stem, "втор") || !strcmp(stem, "шест")
        || !strcmp(stem, "седьм") || !strcmp(stem, "восьм")
        || !strcmp(stem, "сороков");
}

/* An ending written as code points, matched against one written in UTF-8. */
static int sameWord(const uint32_t *w, size_t n, const char *s)
{
    Buf tmp = { 0, 0, 0, 0 };
    int same;

    putUtf8(&tmp, s);
    same = !tmp.failed && tmp.n == n
        && (n == 0 || memcmp(tmp.at, w, n * sizeof *w) == 0);
    free(tmp.at);
    return same;
}

static int endingFor(const uint32_t *w, size_t n)
{
    size_t e;
    int k;

    for (e = 0; e < sizeof ENDINGS / sizeof ENDINGS[0]; e++)
        for (k = 0; k < 4 && ENDINGS[e].written[k]; k++)
            if (sameWord(w, n, ENDINGS[e].written[k]))
                return (int)e;
    return -1;
}

/* Everything but the last word is a cardinal; the last is the ordinal, and
   a round thousand is тысячный. */
static void ordinal(Buf *b, unsigned long long n, int ending)
{
    unsigned long long head;
    unsigned last;
    const char *stem;
    char made[64];

    if (n % 1000 == 0 && n != 0) {
        head = n / 1000;
        if (head > 1)
            cardinal(b, head);
        word(b, "тысячн");
        putUtf8(b, ENDINGS[ending].plain);
        return;
    }
    last = (unsigned)(n % 100);
    head = n - last;
    if (last == 0) {
        last = (unsigned)(n % 1000);
        head = n - last;
        if (head)
            cardinal(b, head);
        stem = ORD_HUNDREDS[last / 100];
    } else {
        if (head)
            cardinal(b, head);
        if (last < 10)
            stem = ORD_UNITS[last];
        else if (last < 20)
            stem = ORD_TEENS[last - 10];
        else if (last % 10 == 0)
            stem = ORD_TENS[last / 10];
        else {
            word(b, TENS[last / 10]);
            stem = ORD_UNITS[last % 10];
        }
    }
    if (!strcmp(stem, "трет")) {
        strcpy(made, "трет");
        strcat(made, ENDINGS[ending].third);
    } else {
        strcpy(made, stem);
        strcat(made, stressedStem(stem) ? ENDINGS[ending].stressed
                                        : ENDINGS[ending].plain);
    }
    word(b, made);
}

static void digits(Buf *b, const uint32_t *d, size_t n)
{
    size_t i;

    for (i = 0; i < n; i++)
        word(b, UNITS[d[i] - '0']);
}

static int isDigit(uint32_t c)
{
    return c >= '0' && c <= '9';
}

/* A thousands separator Russian writes inside a number: the no-break space,
   its two narrower kin, and an ordinary space -- which is also what a list
   of numbers is written with, so it only counts before a group of exactly
   three digits after a lead of at most three. */
static int isGroupSpace(uint32_t c)
{
    return c == 0xa0 || c == 0x202f || c == 0x2009 || c == ' ';
}

/* The number starting at in[i], said in words into b. Answers how far it
   read. */
static size_t number(Buf *b, const uint32_t *in, size_t n, size_t i)
{
    uint32_t d[64];
    size_t nd = 0;
    size_t j = i;
    unsigned long long value = 0;
    size_t k;

    while (j < n && isDigit(in[j]) && nd < sizeof d / sizeof d[0])
        d[nd++] = in[j++];
    /* 1 000 000, grouped the Russian way. */
    while (j + 3 < n && isGroupSpace(in[j]) && in[i] != '0'
           && (in[j] != ' ' || j - i <= 3 || in[j - 4] == ' ')
           && isDigit(in[j + 1])
           && isDigit(in[j + 2]) && isDigit(in[j + 3])
           && (j + 4 >= n || !isDigit(in[j + 4]))
           && nd + 3 <= sizeof d / sizeof d[0]) {
        d[nd++] = in[j + 1];
        d[nd++] = in[j + 2];
        d[nd++] = in[j + 3];
        j += 4;
    }

    /* A leading nought, or more than a billion can say, is a code rather
       than a quantity, and is read a digit at a time. */
    /* Except the nought of a two-digit day or month: 01.05 is один, пять. */
    if (nd == 2 && d[0] == '0'
        && ((j < n && (in[j] == '.' || in[j] == '/'))
            || (i > 0 && (in[i - 1] == '.' || in[i - 1] == '/')))) {
        cardinal(b, d[1] - '0');
        return j - i;
    }
    if ((nd > 1 && d[0] == '0') || nd > 12) {
        digits(b, d, nd);
        return j - i;
    }
    for (k = 0; k < nd; k++)
        value = value * 10 + (d[k] - '0');

    /* 5-й, 1-го: an ordinal, its ending after a hyphen. */
    if (j + 1 < n && (in[j] == '-' || in[j] == 0x2011)
        && isCyrillic(in[j + 1])) {
        uint32_t w[8];
        size_t nw = 0, m = j + 1;
        int ending;

        while (m < n && isCyrillic(in[m]) && nw < 8)
            w[nw++] = lower(in[m++]);
        if (m >= n || !isCyrillic(in[m])) {
            ending = endingFor(w, nw);
            if (ending >= 0) {
                ordinal(b, value, ending);
                return m - i;
            }
        }
    }

    /* 3,14 and 2.5: a decimal, the fraction read as a number if it is a
       short one and a digit at a time if it is not. Two or more of them in a
       row -- 01.05.2026, 1.2.3 -- is a date or a version and each part is a
       number of its own. */
    if (j + 1 < n && (in[j] == ',' || in[j] == '.') && isDigit(in[j + 1])) {
        size_t f = j + 1, nf = 0;

        while (f < n && isDigit(in[f]))
            f++, nf++;
        if (!(f + 1 < n && in[f] == in[j] && isDigit(in[f + 1]))) {
            cardinal(b, value);
            word(b, in[j] == ',' ? "запятая" : "точка");
            if (nf <= 3 && in[j + 1] != '0') {
                unsigned long long frac = 0;

                for (k = j + 1; k < f; k++)
                    frac = frac * 10 + (in[k] - '0');
                cardinal(b, frac);
            } else {
                digits(b, in + j + 1, nf);
            }
            return f - i;
        }
    }

    cardinal(b, value);
    /* 5% is пять процентов, agreeing with the number. */
    if (j < n && in[j] == '%') {
        static const char *const PERCENT[] = { "процент", "процента", "процентов" };

        word(b, PERCENT[pluralForm((unsigned)(value % 1000))]);
        j++;
    }
    return j - i;
}

/* ---- the names of the letters ------------------------------------------ */

static const char *const NAMES[] = {
    "а", "бэ", "вэ", "гэ", "дэ", "е", "жэ", "зэ", "и", "и краткое", "ка",
    "эль", "эм", "эн", "о", "пэ", "эр", "эс", "тэ", "у", "эф", "ха", "цэ",
    "чэ", "ша", "ща", "твёрдый знак", "ы", "мягкий знак", "э", "ю", "я"
};

/* ---- respellings ------------------------------------------------------- */

/* Whole words Russian does not say as they are written. */
static const struct { const char *written, *said; } WORDS[] = {
    { "что", "што" },
    { "чтобы", "штобы" },
    { "ничто", "ништо" },
    { "конечно", "конешно" },
    { "скучно", "скушно" },
    { "нарочно", "нарошно" },
    { "яичница", "яишница" },
    { "его", "ево" },
    { "сегодня", "севодня" },
    { "сегодняшний", "севодняшний" },
    { "солнце", "сонце" },
    { "лёгкий", "лёхкий" },
    { "мягкий", "мяхкий" },
    { "легко", "лехко" },
    { "мягко", "мяхко" },
};

/* Words in -ого whose г is the root's own and is said. */
static const char *const KEEP_OGO[] = {
    "много", "немного", "строго", "дорого", "недорого", "убого", "полого",
    "отлого", "нестрого"
};

/* Inside a word: what is written, and what is said. */
static const struct { const char *written, *said; } CLUSTERS[] = {
    { "вств", "ств" },  /* здравствуйте, чувство */
    { "стн", "сн" },    /* известный, честно */
    { "здн", "зн" },    /* поздно, праздник */
    { "рдц", "рц" },    /* сердце */
    { "лнц", "нц" },    /* солнце */
    { "дц", "ц" },      /* двадцать, молодцы */
    { "тц", "ц" },      /* отца */
    { "сч", "щ" },      /* счастье, считать */
    { "зч", "щ" },      /* извозчик */
    { "жч", "щ" },      /* мужчина */
};

/* Replace every `from' in w[0..*n) with `to', in place. `to' is never
   longer than `from', so it fits. */
static void respell(uint32_t *w, size_t *n, const char *from, const char *to)
{
    Buf f = { 0, 0, 0, 0 }, t = { 0, 0, 0, 0 };
    size_t i, o;

    putUtf8(&f, from);
    putUtf8(&t, to);
    if (f.failed || t.failed || f.n == 0 || t.n > f.n)
        goto done;
    for (i = 0, o = 0; i < *n;) {
        if (i + f.n <= *n && memcmp(w + i, f.at, f.n * sizeof *w) == 0) {
            memcpy(w + o, t.at, t.n * sizeof *w);
            o += t.n;
            i += f.n;
        } else {
            w[o++] = w[i++];
        }
    }
    *n = o;
done:
    free(f.at);
    free(t.at);
}

static int endsWith(const uint32_t *w, size_t n, const char *s)
{
    Buf e = { 0, 0, 0, 0 };
    int yes;

    putUtf8(&e, s);
    yes = !e.failed && e.n <= n
        && memcmp(w + n - e.n, e.at, e.n * sizeof *w) == 0;
    free(e.at);
    return yes;
}

/* The respellings, on a lowercase word. Answers its new length. */
static size_t russianSpelling(uint32_t *w, size_t n)
{
    size_t k;

    for (k = 0; k < sizeof WORDS / sizeof WORDS[0]; k++) {
        if (sameWord(w, n, WORDS[k].written)) {
            Buf s = { 0, 0, 0, 0 };

            putUtf8(&s, WORDS[k].said);
            if (!s.failed && s.n <= n) {
                memcpy(w, s.at, s.n * sizeof *w);
                n = s.n;
            }
            free(s.at);
            return n;
        }
    }

    /* The genitive -ого and -его, whose г is a в: нового, синего, кого. */
    if (n >= 3 && (endsWith(w, n, "ого") || endsWith(w, n, "его"))) {
        int keep = 0;

        for (k = 0; k < sizeof KEEP_OGO / sizeof KEEP_OGO[0]; k++)
            if (sameWord(w, n, KEEP_OGO[k]))
                keep = 1;
        if (!keep)
            w[n - 2] = VE;
    }

    /* -тся and -ться, which are a ц: учится, учиться. */
    if (endsWith(w, n, "ться")) {
        n -= 4;
        w[n++] = TSE;
        w[n++] = A;
    } else if (endsWith(w, n, "тся")) {
        n -= 3;
        w[n++] = TSE;
        w[n++] = A;
    }

    for (k = 0; k < sizeof CLUSTERS / sizeof CLUSTERS[0]; k++)
        respell(w, &n, CLUSTERS[k].written, CLUSTERS[k].said);

    /* лёгкий: г before к or ч is the х Russian says. */
    for (k = 0; k + 1 < n; k++)
        if (w[k] == GE && (w[k + 1] == KA || w[k + 1] == CHE))
            w[k] = HA;
    return n;
}

/* ---- stress ------------------------------------------------------------ */

#include "eci_russian_stress.h"

static size_t vowelCount(const uint32_t *w, size_t n)
{
    size_t i, v = 0;

    for (i = 0; i < n; i++)
        if (isVowel(w[i]))
            v++;
    return v;
}

/* Which vowel, counting from nought, the k-th letter is; or the letter the
   k-th vowel is. */
static int vowelAt(const uint32_t *w, size_t n, size_t k)
{
    size_t i, v = 0;

    for (i = 0; i < n; i++)
        if (isVowel(w[i])) {
            if (v == k)
                return (int)i;
            v++;
        }
    return -1;
}

static int vowelNumber(const uint32_t *w, size_t at)
{
    size_t i;
    int v = 0;

    for (i = 0; i < at; i++)
        if (isVowel(w[i]))
            v++;
    return v;
}

/* A letter as the table numbers it: а is one, ё comes after е, я is
   thirty-three. Nought for anything else. */
static int letterCode(uint32_t c)
{
    if (c == YO)
        return 7;
    if (c >= A && c <= IE)
        return (int)(c - A) + 1;
    if (c > IE && c <= YA)
        return (int)(c - A) + 2;
    return 0;
}

/* Compare a packed word with the word being looked for. */
static int compareCodes(const unsigned char *a, size_t na,
                        const unsigned char *b, size_t nb)
{
    size_t k, m = na < nb ? na : nb;

    for (k = 0; k < m; k++)
        if (a[k] != b[k])
            return a[k] < b[k] ? -1 : 1;
    return na < nb ? -1 : na > nb ? 1 : 0;
}

/* The word in the table. Answers the stressed vowel, with 0x100 added when
   that vowel is an е to be read as ё, or -1. */
static int fromTable(const uint32_t *w, size_t n)
{
    unsigned char key[256];
    unsigned char word[256];
    size_t i, nword = 0;
    size_t blocks = sizeof RU_STRESS_BLOCKS / sizeof RU_STRESS_BLOCKS[0];
    size_t lo = 0, hi = blocks, at, end, read;

    if (n == 0 || n > sizeof key)
        return -1;
    for (i = 0; i < n; i++) {
        int c = letterCode(w[i]);

        if (c == 0)
            return -1;
        key[i] = (unsigned char)c;
    }

    /* The last block whose first word is not past the key. */
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        const unsigned char *e = RU_STRESS_DATA + RU_STRESS_BLOCKS[mid];

        if (compareCodes(e + 2, e[1], key, n) <= 0)
            lo = mid;
        else
            hi = mid;
    }

    at = RU_STRESS_BLOCKS[lo];
    end = lo + 1 < blocks ? RU_STRESS_BLOCKS[lo + 1] : sizeof RU_STRESS_DATA;
    for (read = 0; at < end && read < RU_STRESS_BLOCK; read++) {
        size_t shared = RU_STRESS_DATA[at];
        size_t rest = RU_STRESS_DATA[at + 1];
        int c;

        if (shared > nword || shared + rest > sizeof word)
            return -1;
        memcpy(word + shared, RU_STRESS_DATA + at + 2, rest);
        nword = shared + rest;
        c = compareCodes(word, nword, key, n);
        if (c == 0) {
            unsigned char v = RU_STRESS_DATA[at + 2 + rest];

            return (v & 0x7f) | ((v & 0x80) ? 0x100 : 0);
        }
        if (c > 0)
            return -1;
        at += 2 + rest + 1;
    }
    return -1;
}

/* ---- RuLex -------------------------------------------------------------- */

/* Igor Poretsky's pronunciation dictionary for Russian, read from a file of
   its own, rulex.dat, which tools/russian/rulex.py writes out of RuLex's
   sources. It is under the GNU GPL, which is why it is a file beside the
   engine and not a table in it: the engine uses it when it finds it and
   does as it did without it when it does not.

   What it gives a word is its pronunciation, not only its stress: RuLex
   respells, so что is што, солнце сонце and компьютер компьютэр, and leaves a
   function word unstressed. A word it does not have has its prefixes cut off
   by RuLex's own classifiers and is looked up again, and failing that
   RuLex's general rules say where the stress falls from the shape of the
   word; only then do the table and the rules of this file have their say.

   Where it is found: wherever ru_setLexicon was told, which is how the
   Android app says where it copied it; else EVV_RULEX in the environment;
   else, on Windows, rulex.dat beside the library this code is in, which is
   where the NVDA add-on puts it. */

/* -- a regular expression, the subset RuLex's rules are written in: groups,
   alternation, ?, * and +, bracket classes, the dot and both anchors -- */

enum { RX_LIT, RX_ANY, RX_SET, RX_GRP, RX_BOL, RX_EOL };

typedef struct RxItem RxItem;
typedef struct {
    RxItem *items;
    int n;
} RxSeq;
typedef struct {
    RxSeq *alts;
    int n;
} RxAlt;
struct RxItem {
    int kind;
    uint32_t lit;
    uint32_t *ranges;   /* pairs */
    int nranges;
    int negated;
    RxAlt grp;
    int gid;
    int min, max;       /* max -1 for no limit */
};

typedef struct {
    RxAlt top;
    int groups;
} Rx;

typedef struct {
    const uint32_t *p;
    size_t n, at;
    int groups;
    int bad;
} RxParse;

static int rxPeek(RxParse *ps)
{
    return ps->at < ps->n ? (int)ps->p[ps->at] : -1;
}

static void rxAlt(RxParse *ps, RxAlt *out);

static void rxItem(RxParse *ps, RxItem *it)
{
    int c = rxPeek(ps);

    memset(it, 0, sizeof *it);
    it->min = it->max = 1;
    ps->at++;
    if (c == '(') {
        it->kind = RX_GRP;
        it->gid = ++ps->groups;
        rxAlt(ps, &it->grp);
        if (rxPeek(ps) != ')')
            ps->bad = 1;
        else
            ps->at++;
    } else if (c == '[') {
        it->kind = RX_SET;
        if (rxPeek(ps) == '^') {
            it->negated = 1;
            ps->at++;
        }
        while (ps->at < ps->n && (rxPeek(ps) != ']' || it->nranges == 0)) {
            uint32_t lo = (uint32_t)rxPeek(ps), hi;

            ps->at++;
            hi = lo;
            if (rxPeek(ps) == '-' && ps->at + 1 < ps->n
                && ps->p[ps->at + 1] != ']') {
                hi = ps->p[ps->at + 1];
                ps->at += 2;
            }
            it->ranges = (uint32_t *)realloc(it->ranges,
                                             (it->nranges + 1) * 2 * sizeof *it->ranges);
            if (it->ranges == 0) {
                ps->bad = 1;
                return;
            }
            it->ranges[it->nranges * 2] = lo;
            it->ranges[it->nranges * 2 + 1] = hi;
            it->nranges++;
        }
        if (rxPeek(ps) != ']')
            ps->bad = 1;
        else
            ps->at++;
    } else if (c == '.') {
        it->kind = RX_ANY;
    } else if (c == '^') {
        it->kind = RX_BOL;
    } else if (c == '$') {
        it->kind = RX_EOL;
    } else if (c == '\\' && ps->at < ps->n) {
        it->kind = RX_LIT;
        it->lit = ps->p[ps->at++];
    } else {
        it->kind = RX_LIT;
        it->lit = (uint32_t)c;
    }
    c = rxPeek(ps);
    if (it->kind != RX_BOL && it->kind != RX_EOL
        && (c == '?' || c == '*' || c == '+')) {
        ps->at++;
        it->min = c == '+' ? 1 : 0;
        it->max = c == '?' ? 1 : -1;
    }
}

static void rxSeq(RxParse *ps, RxSeq *out)
{
    out->items = 0;
    out->n = 0;
    while (ps->at < ps->n && rxPeek(ps) != '|' && rxPeek(ps) != ')' && !ps->bad) {
        RxItem *grown = (RxItem *)realloc(out->items, (out->n + 1) * sizeof *grown);

        if (grown == 0) {
            ps->bad = 1;
            return;
        }
        out->items = grown;
        rxItem(ps, &out->items[out->n++]);
    }
}

static void rxAlt(RxParse *ps, RxAlt *out)
{
    out->alts = 0;
    out->n = 0;
    for (;;) {
        RxSeq *grown = (RxSeq *)realloc(out->alts, (out->n + 1) * sizeof *grown);

        if (grown == 0) {
            ps->bad = 1;
            return;
        }
        out->alts = grown;
        rxSeq(ps, &out->alts[out->n++]);
        if (rxPeek(ps) != '|' || ps->bad)
            return;
        ps->at++;
    }
}

/* Compile a pattern written as code points. Answers nought for one this
   subset cannot read, which is then never applied. */
static Rx *rxCompile(const uint32_t *p, size_t n)
{
    RxParse ps;
    Rx *rx = (Rx *)calloc(1, sizeof *rx);

    if (rx == 0)
        return 0;
    ps.p = p;
    ps.n = n;
    ps.at = 0;
    ps.groups = 0;
    ps.bad = 0;
    rxAlt(&ps, &rx->top);
    if (ps.bad || ps.at != n)
        return 0;   /* what was allocated is kept: rules are read once */
    rx->groups = ps.groups;
    return rx;
}

#define RX_GROUPS 16

typedef struct {
    const uint32_t *w;
    int n;
    int start[RX_GROUPS], end[RX_GROUPS];
    int depth;
} RxMatch;

enum { F_SEQ, F_GEND, F_REP };

/* What is still to match once the present item has: the rest of a sequence,
   the end of a group to record, or another time round a quantified item. */
typedef struct RxFrame {
    int kind;
    const RxSeq *s;
    int i, gid, count, from;
    const struct RxFrame *next;
} RxFrame;

static int rxSeqAt(RxMatch *m, const RxSeq *s, int i, int pos, const RxFrame *k);
static int rxRep(RxMatch *m, const RxSeq *s, int i, int count, int pos,
                 const RxFrame *k);

static int rxRun(RxMatch *m, const RxFrame *k, int pos)
{
    int was, r;

    if (k == 0)
        return 1;
    switch (k->kind) {
    case F_SEQ:
        return rxSeqAt(m, k->s, k->i, pos, k->next);
    case F_GEND:
        was = m->end[k->gid];
        m->end[k->gid] = pos;
        r = rxRun(m, k->next, pos);
        if (!r)
            m->end[k->gid] = was;
        return r;
    case F_REP:
        /* A round that matched nothing ends the repetition, or it would
           never end. */
        if (pos == k->from)
            return rxSeqAt(m, k->s, k->i + 1, pos, k->next);
        return rxRep(m, k->s, k->i, k->count, pos, k->next);
    }
    return 0;
}

static int rxClass(const RxItem *it, uint32_t c)
{
    int k, in = 0;

    for (k = 0; k < it->nranges; k++)
        if (c >= it->ranges[k * 2] && c <= it->ranges[k * 2 + 1])
            in = 1;
    return in != it->negated;
}

/* The item once at pos, then whatever `then' says. */
static int rxOnce(RxMatch *m, const RxItem *it, int pos, const RxFrame *then)
{
    int a, was0, was1;
    RxFrame end;

    switch (it->kind) {
    case RX_LIT:
        return pos < m->n && m->w[pos] == it->lit && rxRun(m, then, pos + 1);
    case RX_ANY:
        return pos < m->n && rxRun(m, then, pos + 1);
    case RX_SET:
        return pos < m->n && rxClass(it, m->w[pos]) && rxRun(m, then, pos + 1);
    case RX_GRP:
        if (it->gid >= RX_GROUPS)
            return 0;
        was0 = m->start[it->gid];
        was1 = m->end[it->gid];
        m->start[it->gid] = pos;
        end.kind = F_GEND;
        end.gid = it->gid;
        end.next = then;
        for (a = 0; a < it->grp.n; a++)
            if (rxSeqAt(m, &it->grp.alts[a], 0, pos, &end))
                return 1;
        m->start[it->gid] = was0;
        m->end[it->gid] = was1;
        return 0;
    }
    return 0;
}

/* The quantified item at s->items[i], already matched `count' times. */
static int rxRep(RxMatch *m, const RxSeq *s, int i, int count, int pos,
                 const RxFrame *k)
{
    const RxItem *it = &s->items[i];
    int r = 0;

    if (++m->depth > 4000) {
        m->depth--;
        return 0;
    }
    if (it->max < 0 || count < it->max) {
        RxFrame more;

        more.kind = F_REP;
        more.s = s;
        more.i = i;
        more.count = count + 1;
        more.from = pos;
        more.next = k;
        r = rxOnce(m, it, pos, &more);
    }
    if (!r && count >= it->min)
        r = rxSeqAt(m, s, i + 1, pos, k);
    m->depth--;
    return r;
}

static int rxSeqAt(RxMatch *m, const RxSeq *s, int i, int pos, const RxFrame *k)
{
    const RxItem *it;

    if (i == s->n)
        return rxRun(m, k, pos);
    it = &s->items[i];
    if (it->kind == RX_BOL)
        return pos == 0 && rxSeqAt(m, s, i + 1, pos, k);
    if (it->kind == RX_EOL)
        return pos == m->n && rxSeqAt(m, s, i + 1, pos, k);
    return rxRep(m, s, i, 0, pos, k);
}

/* Search w[0..n) for rx; on a match, start[0] and end[0] are the whole of
   it and start[g] and end[g] each group's. */
static int rxSearch(const Rx *rx, const uint32_t *w, int n, RxMatch *m,
                    int anchored)
{
    int from;

    m->w = w;
    m->n = n;
    for (from = 0; from <= n; from++) {
        int g, a;

        for (g = 0; g < RX_GROUPS; g++)
            m->start[g] = m->end[g] = -1;
        m->depth = 0;
        for (a = 0; a < rx->top.n; a++) {
            RxFrame end;

            end.kind = F_GEND;
            end.gid = 0;
            end.next = 0;
            m->start[0] = from;
            if (rxSeqAt(m, &rx->top.alts[a], 0, from, &end))
                return 1;
        }
        if (anchored)
            break;
    }
    return 0;
}

/* -- the file -- */

static int rlTried;
static int rlOk;
static char rlPath[1024];
static uint32_t rlWords, rlBlocks, rlBytes;
static const unsigned char *rlBlockAt, *rlEntries;
static Rx **rlPrefix;
static uint32_t **rlPrefixWith;
static size_t *rlPrefixWithN;
static int rlNPrefix;
static Rx **rlGeneral;
static int rlNGeneral;

void ru_setLexicon(const char *path)
{
    size_t n = path ? strlen(path) : 0;

    if (n >= sizeof rlPath)
        return;
    memcpy(rlPath, path ? path : "", n + 1);
    rlTried = 0;
}

static uint32_t rlU32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) | ((uint32_t)p[2] << 16)
        | ((uint32_t)p[3] << 24);
}

static unsigned char *rlReadAll(FILE *f, size_t *len)
{
    unsigned char *data = 0;
    size_t have = 0, room = 0, got;

    if (f == 0)
        return 0;
    for (;;) {
        if (have == room) {
            unsigned char *grown;

            room = room ? room * 2 : 1 << 20;
            grown = (unsigned char *)realloc(data, room);
            if (grown == 0) {
                free(data);
                fclose(f);
                return 0;
            }
            data = grown;
        }
        got = fread(data + have, 1, room - have, f);
        have += got;
        if (got == 0)
            break;
    }
    fclose(f);
    *len = have;
    return data;
}

#ifdef _WIN32
/* Beside the library: the NVDA add-on puts rulex.dat next to eci.dll. Wide
   characters all the way, since the add-on lives under the user's profile
   and a Russian user's name is in Cyrillic. */
static FILE *rlOpenBeside(void)
{
    HMODULE module = 0;
    wchar_t path[1024];
    DWORD n;
    wchar_t *slash;

    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS
                            | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                            (LPCWSTR)(void *)&ru_setLexicon, &module))
        return 0;
    n = GetModuleFileNameW(module, path, 1024 - 16);
    if (n == 0 || n >= 1024 - 16)
        return 0;
    slash = wcsrchr(path, L'\\');
    if (slash == 0)
        return 0;
    wcscpy(slash + 1, L"rulex.dat");
    return _wfopen(path, L"rb");
}

static FILE *rlOpenPath(const char *utf8)
{
    wchar_t wide[1024];

    if (!MultiByteToWideChar(CP_UTF8, 0, utf8, -1, wide, 1024))
        return 0;
    return _wfopen(wide, L"rb");
}
#else
static FILE *rlOpenPath(const char *utf8)
{
    return fopen(utf8, "rb");
}
#endif

/* One line of rules, UTF-8, into code points. */
static size_t rlCodes(const char *s, size_t len, uint32_t *out, size_t room)
{
    Buf b = { 0, 0, 0, 0 };
    char *copy = (char *)malloc(len + 1);
    size_t n = 0;

    if (copy == 0)
        return 0;
    memcpy(copy, s, len);
    copy[len] = 0;
    putUtf8(&b, copy);
    free(copy);
    if (!b.failed) {
        n = b.n < room ? b.n : room;
        memcpy(out, b.at, n * sizeof *out);
    }
    free(b.at);
    return n;
}

static void rlRules(const char *text, size_t len)
{
    size_t at = 0;
    int section = 0;   /* 1 prefix, 2 general */

    while (at < len) {
        size_t end = at, sp;
        uint32_t line[512];
        size_t n;

        while (end < len && text[end] != '\n')
            end++;
        if (end - at == 7 && memcmp(text + at, "#prefix", 7) == 0)
            section = 1;
        else if (end - at == 8 && memcmp(text + at, "#general", 8) == 0)
            section = 2;
        else if (end > at && section) {
            for (sp = at; sp < end && text[sp] != ' '; sp++)
                ;
            n = rlCodes(text + at, sp - at, line, 512);
            if (section == 1) {
                Rx *rx = rxCompile(line, n);
                Rx **p = (Rx **)realloc(rlPrefix, (rlNPrefix + 1) * sizeof *p);
                uint32_t **w = (uint32_t **)realloc(rlPrefixWith,
                                                    (rlNPrefix + 1) * sizeof *w);
                size_t *wn = (size_t *)realloc(rlPrefixWithN,
                                               (rlNPrefix + 1) * sizeof *wn);

                if (p) rlPrefix = p;
                if (w) rlPrefixWith = w;
                if (wn) rlPrefixWithN = wn;
                if (rx && p && w && wn) {
                    uint32_t with[64];
                    size_t m = sp < end ? rlCodes(text + sp + 1, end - sp - 1, with, 64) : 0;

                    rlPrefixWith[rlNPrefix] = (uint32_t *)malloc((m + 1) * sizeof *with);
                    if (rlPrefixWith[rlNPrefix]) {
                        memcpy(rlPrefixWith[rlNPrefix], with, m * sizeof *with);
                        rlPrefixWithN[rlNPrefix] = m;
                        rlPrefix[rlNPrefix++] = rx;
                    }
                }
            } else {
                Rx *rx = rxCompile(line, n);
                Rx **g = (Rx **)realloc(rlGeneral, (rlNGeneral + 1) * sizeof *g);

                if (g)
                    rlGeneral = g;
                if (rx && g)
                    rlGeneral[rlNGeneral++] = rx;
            }
        }
        at = end + 1;
    }
}

/* Read rulex.dat the first time it is wanted. Answers whether there is one. */
static int rlLoad(void)
{
    FILE *f = 0;
    unsigned char *data;
    size_t len = 0;
    uint32_t rulesLen;
    const char *env;

    if (rlTried)
        return rlOk;
    rlTried = 1;
    rlOk = 0;
    if (rlPath[0])
        f = rlOpenPath(rlPath);
    else if ((env = getenv("EVV_RULEX")) != 0 && env[0])
        f = rlOpenPath(env);
#ifdef _WIN32
    else
        f = rlOpenBeside();
#endif
    data = rlReadAll(f, &len);
    if (data == 0 || len < 24 || memcmp(data, "EVVRULEX", 8) != 0)
        return 0;
    rlWords = rlU32(data + 8);
    rlBlocks = rlU32(data + 12);
    rlBytes = rlU32(data + 16);
    rulesLen = rlU32(data + 20);
    if (24 + (size_t)rlBlocks * 4 + rlBytes + rulesLen > len || rlBlocks == 0)
        return 0;
    rlBlockAt = data + 24;
    rlEntries = rlBlockAt + (size_t)rlBlocks * 4;
    rlRules((const char *)rlEntries + rlBytes, rulesLen);
    rlOk = 1;
    return 1;
}

/* -- looking a word up -- */

static uint32_t letterOf(int code)
{
    if (code == 7)
        return YO;
    if (code >= 1 && code <= 6)
        return A + (uint32_t)code - 1;
    if (code >= 8 && code <= 33)
        return A + (uint32_t)code - 2;
    return 0;
}

/* The pronunciation of w[0..n) in RuLex, as letters into out and the
   stressed vowel into *stress, -1 for a word RuLex leaves unstressed.
   Answers whether the word is there. */
static int rlFind(const uint32_t *w, size_t n, uint32_t *out, size_t room,
                  size_t *outN, int *stress)
{
    unsigned char key[256], word[256];
    size_t i, nword = 0, lo = 0, hi = rlBlocks, at, end, read;

    if (n == 0 || n > sizeof key)
        return 0;
    for (i = 0; i < n; i++) {
        int c = letterCode(w[i]);

        if (c == 0)
            return 0;
        key[i] = (unsigned char)c;
    }
    while (lo + 1 < hi) {
        size_t mid = (lo + hi) / 2;
        const unsigned char *e = rlEntries + rlU32(rlBlockAt + mid * 4);

        if (compareCodes(e + 2, e[1], key, n) <= 0)
            lo = mid;
        else
            hi = mid;
    }
    at = rlU32(rlBlockAt + lo * 4);
    end = lo + 1 < rlBlocks ? rlU32(rlBlockAt + (lo + 1) * 4) : rlBytes;
    for (read = 0; at < end && read < 32; read++) {
        size_t shared = rlEntries[at], rest = rlEntries[at + 1], plen;
        const unsigned char *pron;
        int c;

        if (shared > nword || shared + rest > sizeof word)
            return 0;
        memcpy(word + shared, rlEntries + at + 2, rest);
        nword = shared + rest;
        pron = rlEntries + at + 2 + rest;
        plen = pron[0] == 255 ? 2 + (size_t)pron[1] : 1;
        c = compareCodes(word, nword, key, n);
        if (c > 0)
            return 0;
        if (c == 0) {
            if (pron[0] != 255) {
                int v = pron[0] & 0x7f;

                if (n > room)
                    return 0;
                memcpy(out, w, n * sizeof *w);
                *outN = n;
                *stress = v == 127 ? -1 : v;
                if (v != 127 && (pron[0] & 0x80)) {
                    int k = vowelAt(out, n, (size_t)v);

                    if (k >= 0 && out[k] == IE)
                        out[k] = YO;
                }
            } else {
                size_t k, o = 0;
                int vowels = 0;

                *stress = -1;
                for (k = 0; k < pron[1] && o < room; k++) {
                    int code = pron[2 + k];

                    if (code == 34)
                        *stress = vowels - 1;
                    else if (code != 36 && letterOf(code)) {
                        out[o++] = letterOf(code);
                        if (isVowel(out[o - 1]))
                            vowels++;
                    }
                }
                *outN = o;
                if (*stress < 0) {
                    for (k = 0; k < o; k++)
                        if (out[k] == YO)
                            *stress = vowelNumber(out, k);
                }
            }
            return 1;
        }
        at += 2 + rest + plen;
    }
    return 0;
}

/* The word, or failing that the word with a prefix RuLex knows cut off and
   looked up again -- twice over at most, as podo- and pere- can stack. */
static int rlLookup(const uint32_t *w, size_t n, uint32_t *out, size_t room,
                    size_t *outN, int *stress, int depth)
{
    int r;

    if (rlFind(w, n, out, room, outN, stress))
        return 1;
    if (depth >= 2)
        return 0;
    for (r = 0; r < rlNPrefix; r++) {
        RxMatch m;
        uint32_t rest[300], got[300];
        size_t cut, wn, rn = 0, gn = 0, k;
        int st;

        if (!rxSearch(rlPrefix[r], w, (int)n, &m, 1))
            continue;
        cut = (size_t)m.end[0];
        wn = rlPrefixWithN[r];
        if (cut == 0 || cut >= n || wn + (n - cut) > 300)
            continue;
        memcpy(rest, rlPrefixWith[r], wn * sizeof *rest);
        memcpy(rest + wn, w + cut, (n - cut) * sizeof *rest);
        rn = wn + (n - cut);
        if (vowelCount(rest, rn) == 0)
            continue;
        if (!rlLookup(rest, rn, got, 300, &gn, &st, depth + 1))
            continue;
        /* What replaced the prefix comes off again, the prefix as written
           goes back on, and the stress moves by the vowels that changed. */
        if (gn < wn || cut + (gn - wn) > room)
            continue;
        for (k = 0; k < wn; k++)
            if (got[k] != rlPrefixWith[r][k])
                break;
        if (k < wn)
            continue;
        memcpy(out, w, cut * sizeof *out);
        memcpy(out + cut, got + wn, (gn - wn) * sizeof *out);
        *outN = cut + (gn - wn);
        *stress = st < 0 ? -1
            : st + (int)vowelCount(w, cut) - (int)vowelCount(rlPrefixWith[r], wn);
        return 1;
    }
    return 0;
}

/* RuLex's general rules, which place the stress from the shape of a word
   it does not have: the stress falls on the vowel that ends what the first
   group matched. -1 when none applies. */
static int rlGuess(const uint32_t *w, size_t n)
{
    int r;

    if (!rlLoad())
        return -1;
    for (r = 0; r < rlNGeneral; r++) {
        RxMatch m;

        if (rxSearch(rlGeneral[r], w, (int)n, &m, 0) && m.end[1] > 0) {
            int k = vowelNumber(w, (size_t)m.end[1]) - 1;

            if (k >= 0)
                return k;
        }
    }
    return -1;
}

/* Endings whose word is stressed on the last vowel before them: the
   adjectives' and the nouns in -ие and -ия, which is where a reading by rule
   does best. Longest first. */
static const char *const STEM_STRESSED[] = {
    "ыми", "ими", "ого", "его", "ому", "ему", "ость", "есть",
    "ая", "яя", "ое", "ее", "ые", "ие", "ый", "ий", "ую", "юю", "ых", "их",
    "ия", "ию", "ии", "ье", "ья", "ью", "ьи", "ьё"
};

/* Endings that carry the stress themselves. */
static const char *const END_STRESSED[] = {
    "ация", "ист", "изм", "ант", "ент", "ок", "ец"
};

/* Where a word's stress falls when nothing written says. */
static int guessStress(const uint32_t *w, size_t n)
{
    size_t vowels = vowelCount(w, n);
    size_t k, len;
    int s;

    if (vowels <= 1)
        return vowels == 1 ? 0 : -1;
    for (k = 0; k < n; k++)
        if (w[k] == YO)
            return vowelNumber(w, k);
    s = fromTable(w, n);
    if (s >= 0)
        return s;

    /* A reflexive verb is stressed where the verb is: учиться as учить. */
    len = n;
    if (len > 4 && (endsWith(w, len, "ся") || endsWith(w, len, "сь"))) {
        len -= 2;
        s = fromTable(w, len);
        if (s >= 0)
            return s;
    }

    /* RuLex's general rules, which know the shape of a Russian word better
       than the endings below, when there is a RuLex. */
    s = rlGuess(w, n);
    if (s >= 0)
        return s;

    /* The infinitive and the past tense keep the stress on their vowel:
       читать, читал, говорила. */
    if (len >= 4 && endsWith(w, len, "ть") && isVowel(w[len - 3]))
        return vowelNumber(w, len - 3);
    {
        /* читал, читала, говорили: the vowel before the л. */
        size_t at = len;

        if (len >= 3 && w[len - 1] == EL)
            at = len - 2;
        else if (len >= 4 && w[len - 2] == EL
                 && (w[len - 1] == A || w[len - 1] == O || w[len - 1] == I))
            at = len - 3;
        if (at < len && (w[at] == A || w[at] == YA || w[at] == I
                         || w[at] == IE) && vowelCount(w, at + 1) >= 2)
            return vowelNumber(w, at);
    }

    for (k = 0; k < sizeof END_STRESSED / sizeof END_STRESSED[0]; k++)
        if (endsWith(w, len, END_STRESSED[k])) {
            Buf e = { 0, 0, 0, 0 };
            int last;

            putUtf8(&e, END_STRESSED[k]);
            last = vowelAt(w, len, vowelCount(w, len) - 1);
            free(e.at);
            if (last >= 0 && vowelCount(w, len) >= 2) {
                /* -ация is stressed on its а, not its last vowel. */
                if (k == 0)
                    return vowelNumber(w, len - 4);
                return vowelNumber(w, (size_t)last);
            }
        }

    for (k = 0; k < sizeof STEM_STRESSED / sizeof STEM_STRESSED[0]; k++) {
        Buf e = { 0, 0, 0, 0 };

        putUtf8(&e, STEM_STRESSED[k]);
        if (!e.failed && len > e.n && endsWith(w, len, STEM_STRESSED[k])) {
            size_t stem = len - e.n;
            size_t inStem = vowelCount(w, stem);

            free(e.at);
            if (inStem >= 1)
                return (int)inStem - 1;
            break;
        }
        free(e.at);
    }

    /* Otherwise the penultimate syllable, which is the commonest place. */
    return (int)vowels - 2;
}

/* ---- sounds ------------------------------------------------------------ */

/* The words that are said with their neighbour rather than on their own:
   prepositions, conjunctions and particles, none of them stressed. */
static const char *const CLITICS[] = {
    "а", "и", "но", "не", "ни", "же", "ли", "бы", "в", "во", "к", "ко", "с",
    "со", "у", "о", "об", "обо", "от", "ото", "до", "за", "на", "по", "из",
    "изо", "без", "для", "над", "под", "при", "про",
    /* and the particles written after a hyphen: кто-то, что-нибудь */
    "то", "нибудь", "либо", "ка", "таки"
};

static int isClitic(const uint32_t *w, size_t n)
{
    size_t k;

    for (k = 0; k < sizeof CLITICS / sizeof CLITICS[0]; k++)
        if (sameWord(w, n, CLITICS[k]))
            return 1;
    return 0;
}

static int isObstruent(uint32_t c)
{
    return c == BE || c == VE || c == GE || c == DE || c == ZHE || c == ZE
        || c == KA || c == PE || c == ES || c == TE || c == EF || c == HA
        || c == TSE || c == CHE || c == SHA || c == SHCHA;
}

static int isVoicedObstruent(uint32_t c)
{
    return c == BE || c == VE || c == GE || c == DE || c == ZHE || c == ZE;
}

/* A consonant's sound in the English module's alphabet, voiced or not. */
static const char *consonant(uint32_t c, int voiced)
{
    switch (c) {
    case BE: case PE:  return voiced ? "b" : "p";
    case VE: case EF:  return voiced ? "v" : "f";
    case GE: case KA:  return voiced ? "g" : "k";
    case DE: case TE:  return voiced ? "d" : "t";
    case ZHE: case SHA: return voiced ? "Z" : "S";
    case ZE: case ES:  return voiced ? "z" : "s";
    case HA:    return "h";
    case TSE:   return "ts";
    case CHE:   return voiced ? "J" : "C";
    case SHCHA: return "SS";
    case EL:    return "l";
    case EM:    return "m";
    case EN:    return "n";
    /* The module's flap, which in lang/ruru is placed at a trill's locus
       rather than a d's: one dip in the voicing at the formants of a trill,
       which is how Italian's r and so Polish's are made. sayWord doubles it
       where Russian р is strongest. */
    case ER:    return "F";
    case IK:    return "y";
    }
    return "";
}

static int isVoicedLetter(uint32_t c)
{
    return isVoicedObstruent(c);
}

/* One sound of a word, and whether it is the vowel of a syllable. */
typedef struct {
    const char *ph;
    int vowel;
    int stressed;
} Sound;

static void addSound(Sound *s, size_t *n, size_t room, const char *ph,
                     int vowel, int stressed)
{
    if (*n >= room || !*ph)
        return;
    /* A doubled consonant is said once: касса, грамм. */
    if (!vowel && *n > 0 && !s[*n - 1].vowel && strcmp(s[*n - 1].ph, ph) == 0)
        return;
    s[*n].ph = ph;
    s[*n].vowel = vowel;
    s[*n].stressed = stressed;
    (*n)++;
}

static void putAscii(Buf *b, const char *s)
{
    while (*s)
        put(b, (unsigned char)*s++);
}

/* One lowercase Cyrillic word as a pronunciation in the English module's
   alphabet: `[.0x.1bWt] for about. stress is which vowel is stressed, or -1
   for none at all, which is what a preposition is. */
static void sayWord(Buf *b, const uint32_t *w, size_t n, int stress)
{

    Sound s[512];
    size_t ns = 0, i;
    int voiced[256];
    int vowelNo = 0;
    if (n > sizeof voiced / sizeof voiced[0])
        n = sizeof voiced / sizeof voiced[0];

    /* Voicing, from the right: a consonant takes the voicing of the
       obstruent after it, and at the end of a word loses it. в does not
       pass voicing on, only takes it: твой keeps its т. */
    for (i = n; i-- > 0;) {
        size_t j = i + 1;

        voiced[i] = isVoicedLetter(w[i]);
        if (!isObstruent(w[i]))
            continue;
        while (j < n && (w[j] == SOFT || w[j] == HARD))
            j++;
        if (j >= n)
            voiced[i] = 0;
        else if (isObstruent(w[j]) && w[j] != VE)
            voiced[i] = voiced[j];
    }

    for (i = 0; i < n; i++) {
        uint32_t c = w[i];
        uint32_t prev = i > 0 ? w[i - 1] : 0;
        int hardPrev = prev == ZHE || prev == SHA || prev == TSE;
        int softPrev = prev == CHE || prev == SHCHA || prev == IK;

        if (isVowel(c)) {
            int stressed = vowelNo == stress;
            /* The syllable before the stress, or the first of the word, is
               reduced less than the rest: молоко is малако. */
            int pretonic = vowelNo == stress - 1 || i == 0
                || (i > 0 && isVowel(prev));
            int last = i + 1 == n;
            int glide = 0;
            const char *v = "x";

            switch (c) {
            case A:
                v = stressed ? "a" : softPrev ? "I" : pretonic ? "H" : "x";
                break;
            case O:
                /* The module's o, which in lang/ruru is a plain vowel rather
                   than the diphthong of boat: make-ruru.py says how. */
                v = stressed ? "o" : pretonic ? "H" : "x";
                break;
            case E:
                v = stressed ? "E" : "I";
                break;
            case YERY:
                v = "X";
                break;
            case I:
                if (hardPrev)
                    v = "X";
                else
                    v = stressed ? "i" : "I";
                if (prev == SOFT || prev == HARD)
                    glide = 1;
                break;
            case U:
                v = stressed ? "u" : "U";
                break;
            case IE:
                if (hardPrev)
                    v = stressed ? "E" : last ? "x" : "X";
                else
                    v = stressed ? "E" : "I";
                break;
            case YO:
                v = "o";
                break;
            case YU:
                v = stressed ? "u" : "U";
                break;
            case YA:
                v = stressed ? "a" : last ? "x" : "I";
                break;
            }
            if (isIotated(c)) {
                if (i == 0 || isVowel(prev) || prev == SOFT || prev == HARD)
                    glide = 1;
                else if (!hardPrev && !softPrev && isConsonant(prev)
                         && v[0] != 'i' && v[0] != 'I')
                    glide = 1;
            }
            if (glide)
                addSound(s, &ns, sizeof s / sizeof s[0], "y", 0, 0);
            addSound(s, &ns, sizeof s / sizeof s[0], v, 1, stressed);
            vowelNo++;
            continue;
        }
        if (c == SOFT || c == HARD)
            continue;
        /* щ is not ш. Before a vowel it is the module's sh softened by the
           glide, as every soft consonant here is, which is where Russian
           hears it; anywhere else the glide would be lost, so it is шч, the
           older reading of the letter and the one still heard in борщ. */
        if (c == SHCHA) {
            addSound(s, &ns, sizeof s / sizeof s[0], "S", 0, 0);
            if (i + 1 < n && isVowel(w[i + 1])) {
                if (w[i + 1] != I && w[i + 1] != IE)
                    addSound(s, &ns, sizeof s / sizeof s[0], "y", 0, 0);
            } else {
                addSound(s, &ns, sizeof s / sizeof s[0], "C", 0, 0);
            }
            continue;
        }
        /* р before a vowel is one tap. Anywhere else -- at the end of a
           word, before a consonant -- a tap has nothing to release into and
           was lost, so it is the tap and the module's r after it, which in
           lang/ruru is placed where the tap is. Doubling the tap at the
           start of a word and before the stress was heard as too much р. */
        if (c == ER) {
            size_t k = i + 1;

            while (k < n && (w[k] == SOFT || w[k] == HARD))
                k++;
            addSound(s, &ns, sizeof s / sizeof s[0], "F", 0, 0);
            if (!(k < n && isVowel(w[k])))
                addSound(s, &ns, sizeof s / sizeof s[0], "r", 0, 0);
            continue;
        }
        /* х before a vowel is the module's h. Anywhere else the module
           drops an h -- них sounded like ник -- and a fricative made of its
           other noises was heard as f, поездах as поездаф; what was heard
           as х there is k with the breath after it. */
        if (c == HA && !(i + 1 < n && isVowel(w[i + 1]))) {
            uint32_t next = i + 1 < n ? w[i + 1] : 0;

            /* The module's th, which in lang/ruru is a velar fricative made
               from its sh. Before л, р, м, н or в a k goes in front of it:
               хлеб with the fricative alone was not liked, and the module
               drops an h before a consonant as it does at the end of a
               syllable, so k and the fricative is what was chosen there. */
            if (next == EL || next == ER || next == EM || next == EN
                || next == VE)
                addSound(s, &ns, sizeof s / sizeof s[0], "k", 0, 0);
            addSound(s, &ns, sizeof s / sizeof s[0], "T", 0, 0);
            continue;
        }
        addSound(s, &ns, sizeof s / sizeof s[0], consonant(c, voiced[i]), 0, 0);
    }

    /* Into syllables: one consonant between two vowels starts the second,
       and of two or more the first closes the first. */
    putAscii(b, "`[");
    {
        size_t at = 0, v;
        size_t firstVowel = ns;

        for (i = 0; i < ns; i++)
            if (s[i].vowel) {
                firstVowel = i;
                break;
            }
        if (firstVowel == ns) {
            /* No vowel at all: said as it stands, one syllable. */
            putAscii(b, ".0");
            for (i = 0; i < ns; i++)
                putAscii(b, s[i].ph);
        }
        for (v = firstVowel; v < ns; ) {
            size_t next = v + 1, onset;

            while (next < ns && !s[next].vowel)
                next++;
            if (next < ns) {
                size_t between = next - v - 1;

                onset = between <= 1 ? v + 1 : v + 2;
                /* A consonant and the glide that softens it stay together:
                   привет is pri.vyet, not priv.yet. */
                if (between >= 2 && strcmp(s[next - 1].ph, "y") == 0)
                    onset = next - 2;
            } else {
                onset = ns;
            }
            /* The module flaps a t or a d that starts an unstressed
               syllable -- это came out as eda -- and Russian never does.
               A secondary stress is what it does not flap before. */
            if (s[v].stressed)
                putAscii(b, ".1");
            else if (at > 0 && at + 1 < ns && s[at + 1].vowel
                     && (strcmp(s[at].ph, "t") == 0
                         || strcmp(s[at].ph, "d") == 0))
                putAscii(b, ".2");
            else
                putAscii(b, ".0");
            for (i = at; i < onset; i++)
                putAscii(b, s[i].ph);
            at = onset;
            v = next;
        }
    }
    putAscii(b, "]");
}

/* ---- the text ---------------------------------------------------------- */

static int isSpace(uint32_t c)
{
    return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == 0xa0;
}

static int isHyphen(uint32_t c)
{
    return c == '-' || c == 0x2010 || c == 0x2011;
}

/* Is in[i] the first letter of a Cyrillic word, after any spaces or the
   hyphen of по-русски? */
static int wordFollows(const uint32_t *in, size_t n, size_t i)
{
    if (i < n && isHyphen(in[i]))
        return i + 1 < n && isCyrillic(in[i + 1]);
    while (i < n && (in[i] == ' ' || in[i] == 0xa0))
        i++;
    return i < n && isCyrillic(in[i]);
}

/* Was there a Cyrillic word before in[i], with only spaces between? */
static int wordPrecedes(const uint32_t *in, size_t i)
{
    if (i > 1 && isHyphen(in[i - 1]))
        return isCyrillic(in[i - 2]);
    while (i > 0 && (in[i - 1] == ' ' || in[i - 1] == 0xa0))
        i--;
    return i > 0 && isCyrillic(in[i - 1]);
}

/* One run of Cyrillic letters, read into w with the stress the text marks
   itself, if it marks one. Answers how far it read. */
/* A plus in front of a Cyrillic vowel is a stress mark, the way Silero and
   RUAccent write one and the way a speech dictionary entry can: з+амок. */
static int isStressPlus(const uint32_t *in, size_t n, size_t i)
{
    return in[i] == '+' && i + 1 < n && isVowel(lower(in[i + 1]));
}

static size_t readWord(const uint32_t *in, size_t n, size_t i, uint32_t *w,
                       size_t room, size_t *nw, int *marked, int *capitals)
{
    size_t j = i;

    *nw = 0;
    *marked = -1;
    *capitals = 0;
    while (j < n && (isCyrillic(in[j]) || in[j] == 0x301
                     || isStressPlus(in, n, j))) {
        if (in[j] == '+') {
            /* Marks the vowel after it. */
            *marked = vowelNumber(w, *nw);
        } else if (in[j] == 0x301) {
            /* The stress mark a careful text puts over a vowel. */
            if (*nw > 0 && isVowel(w[*nw - 1]))
                *marked = vowelNumber(w, *nw - 1);
        } else if (*nw + 2 < room) {
            if (isUpper(in[j]))
                (*capitals)++;
            /* ї is й and и together. */
            if (in[j] == 0x457 || in[j] == 0x407)
                w[(*nw)++] = IK;
            w[(*nw)++] = lower(in[j]);
        }
        j++;
    }
    return j - i;
}

/* A word, its stress decided and its spelling put right, said. prefix is a
   preposition of one consonant said as part of it. */
static void sayRussian(Buf *b, uint32_t prefix, const uint32_t *word,
                       size_t n, int marked, int unstressed, int inPhrase)
{
    uint32_t w[260], said[260];
    size_t nw = 0, nsaid = 0;
    int stress = -2, st;

    if (n > 250)
        n = 250;
    /* RuLex first, when there is one: it says how the word is said as well
       as where its stress is, and that a function word has none -- which
       is only worth acting on among other words. */
    if (!unstressed && marked < 0 && rlLoad()
        && rlLookup(word, n, said, 250, &nsaid, &st, 0)
        && nsaid > 0 && (st >= 0 || inPhrase)) {
        word = said;
        n = nsaid;
        /* A word of more than one syllable with no stress in RuLex is one it
           will not decide -- замок, дома, стоит -- rather than one without
           a stress, so its letters are RuLex's and its stress is ours. */
        stress = st >= 0 ? st
            : vowelCount(said, nsaid) >= 2 ? guessStress(said, nsaid) : -1;
    }
    if (stress == -2)
        stress = unstressed ? -1 : marked >= 0 ? marked : guessStress(word, n);
    if (prefix)
        w[nw++] = prefix;
    memcpy(w + nw, word, n * sizeof *w);
    nw += n;
    /* еще and идет, typed without their ё: the table says which е it was. */
    if (stress >= 0 && (stress & 0x100)) {
        int at;

        stress &= 0xff;
        at = vowelAt(word, n, (size_t)stress);
        if (at >= 0 && word[at] == IE)
            w[(prefix ? 1 : 0) + at] = YO;
    }
    /* The respellings keep the vowels where they were, so the stress found
       on the written word still points at the right one. */
    nw = (prefix ? 1 : 0) + russianSpelling(w + (prefix ? 1 : 0),
                                            nw - (prefix ? 1 : 0));
    sayWord(b, w, nw, stress);
}

/* A letter by its name: бэ, и краткое. */
static void sayName(Buf *b, uint32_t c)
{
    Buf name = { 0, 0, 0, 0 };
    size_t s, e;

    if (c == YO) {
        putAscii(b, "`[.1yo]");
        return;
    }
    if (c < A || c > YA)
        return;
    putUtf8(&name, NAMES[c - A]);
    for (s = 0; !name.failed && s < name.n; s = e + 1) {
        for (e = s; e < name.n && name.at[e] != ' '; e++)
            ;
        if (s > 0)
            put(b, ' ');
        sayRussian(b, 0, name.at + s, e - s, -1, 0, 0);
    }
    free(name.at);
}

static size_t cyrillicWord(Buf *b, const uint32_t *in, size_t n, size_t i)
{
    uint32_t w[256];
    size_t nw, j;
    int marked, capitals;

    j = i + readWord(in, n, i, w, sizeof w / sizeof w[0], &nw, &marked,
                     &capitals);
    if (nw == 0)
        return j - i;

    if (nw == 1 && !isVowel(w[0])) {
        /* в, к and с before a word are said as part of it: в доме is
           вдоме, с собой is ссобой. */
        if ((w[0] == VE || w[0] == KA || w[0] == ES) && wordFollows(in, n, j)) {
            uint32_t next[256];
            size_t nn, k = j;
            int nmarked, ncaps;

            while (k < n && (in[k] == ' ' || in[k] == 0xa0))
                k++;
            k += readWord(in, n, k, next, sizeof next / sizeof next[0], &nn,
                          &nmarked, &ncaps);
            sayRussian(b, w[0], next, nn, nmarked, 0, 1);
            return k - i;
        }
        /* Any other consonant on its own is said by its name, which is what
           a reader moving a letter at a time wants. */
        sayName(b, w[0]);
        return j - i;
    }

    /* A word with no vowel in it, СССР or ФСБ, is spelled. */
    if (vowelCount(w, nw) == 0) {
        size_t k;

        int onlySigns = 1;

        for (k = 0; k < nw; k++)
            if (w[k] != SOFT && w[k] != HARD)
                onlySigns = 0;
        for (k = 0; k < nw; k++) {
            /* A sign is silent inside a word and named when it is all
               there is. */
            if ((w[k] == SOFT || w[k] == HARD) && !onlySigns)
                continue;
            if (k > 0)
                put(b, ' ');
            sayName(b, w[k]);
        }
        return j - i;
    }

    /* A preposition, a conjunction or a particle among other words is said
       unstressed; on its own it is a word like any other. */
    {
        int inPhrase = wordFollows(in, n, j) || wordPrecedes(in, i);

        sayRussian(b, 0, w, nw, marked,
                   marked < 0 && isClitic(w, nw) && inPhrase, inPhrase);
    }
    return j - i;
}

/* ---- punctuation and symbols ----------------------------------------- */

/* What the English module would name in English, named in Russian: a
   symbol is a word wherever it stands. A bracket or a quotation mark is a
   pause when it is against a word and is named only when it stands on its
   own, which is how a screen reader leaves one when its own punctuation
   setting is the engine's. */
static const struct { uint32_t c; const char *name; } SYMBOLS[] = {
    { '%', "процент" }, { '+', "плюс" }, { '=', "равно" },
    { '@', "собака" }, { '#', "решётка" }, { '$', "доллар" },
    { 0x20ac, "евро" }, { 0x20bd, "рубль" }, { '&', "амперсанд" },
    { '*', "звёздочка" }, { '/', "слэш" }, { '\\', "обратный слэш" },
    { '|', "вертикальная черта" }, { '~', "тильда" }, { '^', "крышка" },
    { '_', "подчёркивание" }, { '<', "меньше" }, { '>', "больше" },
    { 0x2116, "номер" }, { 0xa7, "параграф" }, { 0xb0, "градус" },
    { 0xa9, "копирайт" }, { 0xae, "зарегистрированный знак" },
    { 0x2122, "торговая марка" }, { 0x2022, "точка списка" },
};

static const struct { uint32_t c; const char *name; } ALONE[] = {
    { '(', "левая скобка" }, { ')', "правая скобка" },
    { '[', "левая квадратная скобка" }, { ']', "правая квадратная скобка" },
    { '{', "левая фигурная скобка" }, { '}', "правая фигурная скобка" },
    { '"', "кавычка" }, { 0xab, "открывающая кавычка" },
    { 0xbb, "закрывающая кавычка" }, { 0x201e, "кавычка" },
    { 0x201c, "кавычка" }, { 0x201d, "кавычка" }, { '\'', "апостроф" },
    { 0x2019, "апостроф" }, { '.', "точка" }, { ',', "запятая" },
    { '!', "восклицательный знак" }, { '?', "вопросительный знак" },
    { ';', "точка с запятой" }, { ':', "двоеточие" }, { '-', "дефис" },
    { 0x2014, "тире" }, { 0x2013, "тире" }, { 0x2026, "многоточие" },
};

/* Abbreviations written with dots, said in full. */
static const struct { const char *written, *said; } ABBREVIATIONS[] = {
    { "т.е.", "то есть" }, { "т.д.", "так далее" }, { "т.п.", "тому подобное" },
    { "т.к.", "так как" }, { "т.н.", "так называемый" }, { "т.ч.", "том числе" },
    { "н.э.", "нашей эры" }, { "руб.", "рублей" }, { "коп.", "копеек" },
    { "тыс.", "тысяч" }, { "млн.", "миллионов" }, { "млрд.", "миллиардов" },
    { "млн", "миллионов" }, { "млрд", "миллиардов" }, { "ул.", "улица" },
    { "стр.", "страница" }, { "см.", "смотри" }, { "напр.", "например" },
    { "др.", "другие" }, { "пр.", "прочее" }, { "гг.", "годы" },
    { "вв.", "века" }, { "кв.", "квартира" }, { "обл.", "область" },
};

static int isLetterOrDigit(uint32_t c)
{
    return isCyrillic(c) || isDigit(c) || (c >= 'A' && c <= 'Z')
        || (c >= 'a' && c <= 'z') || (c >= 0xc0 && c <= 0x24f);
}

/* A character with nothing but space, or the edge of the text, either
   side. */
static int standsAlone(const uint32_t *in, size_t n, size_t i)
{
    return (i == 0 || isSpace(in[i - 1]))
        && (i + 1 >= n || isSpace(in[i + 1]));
}

static const char *symbolName(uint32_t c)
{
    size_t k;

    for (k = 0; k < sizeof SYMBOLS / sizeof SYMBOLS[0]; k++)
        if (SYMBOLS[k].c == c)
            return SYMBOLS[k].name;
    return 0;
}

static const char *aloneName(uint32_t c)
{
    size_t k;

    for (k = 0; k < sizeof ALONE / sizeof ALONE[0]; k++)
        if (ALONE[k].c == c)
            return ALONE[k].name;
    return 0;
}

/* An abbreviation starting at in[i], at the start of a word: how many
   characters it is, its expansion in *said. */
static size_t abbreviation(const uint32_t *in, size_t n, size_t i,
                           const char **said)
{
    size_t k;

    if (i > 0 && isLetterOrDigit(in[i - 1]))
        return 0;
    for (k = 0; k < sizeof ABBREVIATIONS / sizeof ABBREVIATIONS[0]; k++) {
        Buf w = { 0, 0, 0, 0 };
        size_t m;
        int same = 1;

        putUtf8(&w, ABBREVIATIONS[k].written);
        if (w.failed || i + w.n > n) {
            free(w.at);
            continue;
        }
        for (m = 0; m < w.n && same; m++)
            same = lower(in[i + m]) == w.at[m];
        /* млн on its own, not the start of a longer word. */
        if (same && i + w.n < n && isCyrillic(in[i + w.n]))
            same = 0;
        m = w.n;
        free(w.at);
        if (same) {
            *said = ABBREVIATIONS[k].said;
            return m;
        }
    }
    return 0;
}

/* What may stand straight after a pronunciation annotation without the
   module reading the annotation out letter by letter, which is what it does
   when one is followed by anything else -- 50% came out as backquote left
   bracket. */
static int safeAfterAnnotation(uint32_t c)
{
    return isSpace(c) || c == '.' || c == ',' || c == '!' || c == '?'
        || c == ';' || c == ':' || c == ')' || c == '"' || c == '\'';
}

static int isPauseMark(uint32_t c)
{
    return c == '.' || c == ',' || c == '!' || c == '?' || c == ';' || c == ':';
}

/* A mark that is a pause between words and is named only when it is all
   there is: a dash, a hyphen, a full stop. */
static int isPauseOrDash(uint32_t c)
{
    return isPauseMark(c) || c == '-' || c == 0x2014 || c == 0x2013
        || c == 0x2026;
}

static int nothingBefore(const Buf *b)
{
    size_t k;

    for (k = 0; k < b->n; k++)
        if (!isSpace(b->at[k]))
            return 0;
    return 1;
}

/* Drop the spaces at the end of what has been written, so that a mark is
   against what is before it, where the module pauses at it, rather than on
   its own, where it names it. Answers nought when nothing is before it. */
static int glueToAnnotation(Buf *b)
{
    size_t k = b->n;

    while (k > 0 && isSpace(b->at[k - 1]))
        k--;
    if (k == 0)
        return 0;
    b->n = k;
    return 1;
}

/* Is in[i..] the spelling annotation, `ts0 or `ts1? Answers the digit's
   value, or -1. */
static int spellingAnnotation(const uint32_t *in, size_t n, size_t i)
{
    if (i + 3 < n && in[i] == '`' && in[i + 1] == 't' && in[i + 2] == 's'
        && isDigit(in[i + 3]))
        return in[i + 3] != '0';
    return -1;
}

/* Copy an annotation through to the next space, noting what it does to
   spelling. */
static size_t annotation(Buf *b, const uint32_t *in, size_t n, size_t i,
                         int *spelling)
{
    size_t j = i;
    int sp = spellingAnnotation(in, n, i);

    if (sp >= 0)
        *spelling = sp;
    while (j < n && !isSpace(in[j]))
        put(b, in[j++]);
    return j - i;
}

/* The engine's spelling mode is on: a screen reader saying a character as
   the user types it, or spelling a word out. The module would spell a
   pronunciation annotation letter by letter -- т came out as point t e --
   so spelling is turned off around each Russian letter's name and on again
   after it. */
static size_t spellCyrillic(Buf *b, const uint32_t *in, size_t n, size_t i)
{
    size_t j = i;
    int first = 1;

    putAscii(b, "`ts0 ");
    while (j < n && (isCyrillic(in[j]) || in[j] == 0x301)) {
        uint32_t c = lower(in[j]);

        if (in[j] != 0x301) {
            if (!first)
                put(b, ' ');
            first = 0;
            if (c == YERY)
                putAscii(b, "`[.1X]");
            else
                sayName(b, c);
        }
        j++;
    }
    putAscii(b, " `ts1 ");
    return j - i;
}

uint32_t *ru_transcribe(const uint32_t *in, size_t n, size_t *out_n,
                        int *spelling)
{
    Buf b = { 0, 0, 0, 0 };
    Buf words = { 0, 0, 0, 0 };
    size_t i = 0;
    int start = spelling ? *spelling : 0;
    int sp = start;

    /* Numbers first, into Russian words, so that the words they become are
       spelled with the rest. A digit typed while spelling is said as its
       word, with spelling off around it, rather than spelled. */
    while (i < n) {
        uint32_t c = in[i];

        if (c == '`') {
            /* An annotation runs to the next space and is the engine's. */
            i += annotation(&words, in, n, i, &sp);
            continue;
        }
        if (!sp) {
            const char *said = 0;
            size_t ab = isCyrillic(c) ? abbreviation(in, n, i, &said) : 0;
            const char *name;

            if (ab) {
                put(&words, ' ');
                putUtf8(&words, said);
                put(&words, ' ');
                i += ab;
                continue;
            }
            /* A plus before a vowel is a stress mark, not a symbol. */
            if (c == '+' && isStressPlus(in, n, i)) {
                put(&words, c);
                i++;
                continue;
            }
            name = symbolName(c);
            if (name) {
                put(&words, ' ');
                putUtf8(&words, name);
                put(&words, ' ');
                i++;
                continue;
            }
            if (standsAlone(in, n, i) && (name = aloneName(c)) != 0
                && (!isPauseOrDash(c) || nothingBefore(&words))) {
                put(&words, ' ');
                putUtf8(&words, name);
                put(&words, ' ');
                i++;
                continue;
            }
            /* A dash between words is a pause, an ellipsis three dots. */
            if (c == 0x2014 || c == 0x2013) {
                put(&words, ',');
                put(&words, ' ');
                i++;
                continue;
            }
            if (c == 0x2026) {
                putAscii(&words, "...");
                i++;
                continue;
            }
            /* Quotation marks against a word are the plain one, which the
               module pauses at; brackets the round one, since a square one
               against an annotation would be read as the start of one. */
            if (c == 0xab || c == 0xbb || c == 0x201e || c == 0x201c
                || c == 0x201d) {
                put(&words, '"');
                i++;
                continue;
            }
            if (c == '[' || c == '{' || c == ']' || c == '}') {
                put(&words, (c == '[' || c == '{') ? '(' : ')');
                i++;
                continue;
            }
            /* A hyphen between letters or digits -- кто-то, arm64-v8a,
               e-mail -- joins words and is not to be named; and a dot
               between two letters where one is Cyrillic, т.е. not in the
               table, is not a full stop. */
            if (i > 0 && i + 1 < n && isLetterOrDigit(in[i - 1])
                && isLetterOrDigit(in[i + 1])
                && (isHyphen(c)
                    || (c == '.' && (isCyrillic(in[i - 1])
                                     || isCyrillic(in[i + 1]))))) {
                put(&words, ' ');
                i++;
                continue;
            }
        }
        if (isDigit(c) && sp) {
            putAscii(&words, "`ts0 ");
            word(&words, UNITS[c - '0']);
            putAscii(&words, " `ts1 ");
            i++;
            continue;
        }
        if (isDigit(c)) {
            /* A number glued to a word -- 5км, А4 -- stays apart from it. */
            if (words.n > 0 && !isSpace(words.at[words.n - 1]))
                put(&words, ' ');
            i += number(&words, in, n, i);
            /* The dots of a date and the slashes of a fraction between two
               numbers are a pause too long for them: 01.05.2026. */
            if (i + 1 < n && (in[i] == '.' || in[i] == '/' || in[i] == ':')
                && isDigit(in[i + 1])) {
                put(&words, ' ');
                i++;
                continue;
            }
            if (i < n && (isCyrillic(in[i]) || (in[i] >= 'A' && in[i] <= 'Z')
                          || (in[i] >= 'a' && in[i] <= 'z')))
                put(&words, ' ');
            /* arm64-v8a: the hyphen after a number joins it to what
               follows, and on its own the module would call it a dash. */
            if (i + 1 < n && isHyphen(in[i]) && isLetterOrDigit(in[i + 1])) {
                put(&words, ' ');
                i++;
            }
            continue;
        }
        put(&words, c);
        i++;
    }
    if (words.failed) {
        free(words.at);
        return 0;
    }

    i = 0;
    sp = start;
    while (i < words.n) {
        uint32_t c = words.at[i];

        if (c == '`') {
            i += annotation(&b, words.at, words.n, i, &sp);
            continue;
        }
        if (isCyrillic(c) && sp) {
            i += spellCyrillic(&b, words.at, words.n, i);
            continue;
        }
        if (isCyrillic(c) || (isStressPlus(words.at, words.n, i)
                              && (i == 0 || !isCyrillic(words.at[i - 1])))) {
            i += cyrillicWord(&b, words.at, words.n, i);
            if (i < words.n && !safeAfterAnnotation(words.at[i]))
                put(&b, ' ');
            continue;
        }
        /* A mark after a word, even with space between, is a pause and not
           a name; one with no word before it is named, in Russian. */
        if (isPauseMark(c) && (i == 0 || isSpace(words.at[i - 1]))) {
            if (!glueToAnnotation(&b) && standsAlone(words.at, words.n, i)) {
                const char *name = aloneName(c);
                Buf w = { 0, 0, 0, 0 };

                putUtf8(&w, name);
                {
                    size_t st, e;

                    for (st = 0; !w.failed && st < w.n; st = e + 1) {
                        for (e = st; e < w.n && w.at[e] != ' '; e++)
                            ;
                        if (st > 0)
                            put(&b, ' ');
                        sayRussian(&b, 0, w.at + st, e - st, -1, 0, 0);
                    }
                }
                free(w.at);
                i++;
                continue;
            }
        }
        if (c == 0x301) {
            i++;
            continue;
        }
        /* The hyphen of по-русски and кто-то joins two words the module
           would otherwise hear a dash between. */
        if (isHyphen(c) && i > 0 && i + 1 < words.n
            && isCyrillic(words.at[i - 1]) && isCyrillic(words.at[i + 1])) {
            put(&b, ' ');
            i++;
            continue;
        }
        put(&b, c);
        i++;
    }
    free(words.at);
    if (b.failed) {
        free(b.at);
        return 0;
    }
    if (spelling)
        *spelling = sp;
    *out_n = b.n;
    if (b.at == 0)
        b.at = (uint32_t *)malloc(sizeof *b.at);
    return b.at;
}

/* Whether each instance is spelling, which the text says in one call and
   acts on in the next: a screen reader sends `ts1 on its own and the
   character after it. Kept by the instance's address in a table of its own,
   since the instance's layout is IBM's and has no room for it. A slot is
   cleared when its instance stops or is made, so a spelling mode cut off by
   a stop cannot leak into what is said next. */
#define SPELLING_SLOTS 32

static struct {
    const void *key;
    int spelling;
} spellingSlots[SPELLING_SLOTS];

int *ru_spelling(const void *key)
{
    size_t k, empty = SPELLING_SLOTS;

    for (k = 0; k < SPELLING_SLOTS; k++) {
        if (spellingSlots[k].key == key)
            return &spellingSlots[k].spelling;
        if (spellingSlots[k].key == 0 && empty == SPELLING_SLOTS)
            empty = k;
    }
    if (empty == SPELLING_SLOTS)
        empty = ((size_t)key >> 4) % SPELLING_SLOTS;
    spellingSlots[empty].key = key;
    spellingSlots[empty].spelling = 0;
    return &spellingSlots[empty].spelling;
}

void ru_spellingForget(const void *key)
{
    size_t k;

    for (k = 0; k < SPELLING_SLOTS; k++)
        if (spellingSlots[k].key == key) {
            spellingSlots[k].key = 0;
            spellingSlots[k].spelling = 0;
        }
}

void ru_free(uint32_t *p)
{
    free(p);
}
