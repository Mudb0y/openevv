/* The accent layer. evv_accent.h says what it is for; this says how.
 *
 * ---- the markup ----------------------------------------------------------
 *
 * Markup is a group in braces, a letter first and then words with spaces
 * between them. Nothing is markup until a group beginning `{A v=1' has gone
 * by, so text that merely has a brace in it is left alone.
 *
 *   {A v=1 f0=own range=90 decl=10 ...}     the language: see profile_set
 *   {D id f2=85 f3=78 vot=15 ...}           a sound: see def_set
 *   {T id p=0:50,100:10 dur=100 ...}        a tone: see tone_set
 *   {X C t S}                               the module says C as t and then S
 *   {S d t}                                 the module may say d as t
 *   {W .1 t=rt a^55 .0 <h=hh a^- >q=gs}     a word, as it was meant
 *   {P s}                                   the phrase ends here, and how
 *
 * A word is its syllables: `.' and the stress digit the annotation gives the
 * syllable, then the phones in order under the names the annotation uses.
 * `=id' after a phone is the sound it was meant to be, a D group's id. `^id'
 * marks the phone as the syllable's nucleus and names its tone, a T group's
 * id, or `-' for none.
 *
 * A phone written with `<' or `>' in front of it is not in the annotation at
 * all. The module has nothing to say it with -- an h in Italian, a glottal
 * stop nearly anywhere -- so it is made here, out of the start of the phone
 * after it (`<') or the end of the phone before it (`>'), and its name is
 * only for whoever reads the markup.
 *
 * A phrase ends `s' as a statement, `q' as a question wanting yes or no, `w'
 * as a question with a question word, `e' as an exclamation, `c' where the
 * sentence goes on after a comma.
 *
 * ---- what happens to it --------------------------------------------------
 *
 * evv_accent_strip takes the groups out of the text and keeps what they
 * said. The words go on a queue, phone by phone, in the order they were
 * written.
 *
 * evv_accent_place is told each phone the rules are about to synthesise and
 * finds it in the queue. The module does not always say what it was given:
 * every module says an affricate as two phones, and the modules IBM wrote
 * for one language change a good deal besides. So the phone is looked for a
 * little way ahead rather than only next, a phone the module left out is
 * stepped over, and one it put in is let by with nothing said about it. X
 * and S groups say which differences a module is known to make.
 *
 * evv_accent_run is handed the frames. They do not arrive a phone at a time
 * -- the arrays the rules write run a little behind the phone they have just
 * named -- so the phones are kept in the order they were asked for and the
 * frames with the moment each belongs to, and a phone is worked on when all
 * of its frames have come.
 *
 * A phone's stretch begins where the mouth begins to move towards it and
 * ends where it begins to move towards the next, which is the engine's own
 * way of cutting speech up and is measured rather than assumed: the release
 * of a stop, and its aspiration, are at the start of the stretch after the
 * stop's own. So what a sound's definition says about formants is come to
 * over the first part of its stretch and held to the end of it, and what it
 * says about the release of a stop is done at the start of the next one.
 *
 * Formants are changed by ratio and never set. A module speaks with a voice
 * -- a head size, a sex -- that has already scaled every formant by the time
 * a frame arrives here, so the only thing that means the same in every voice
 * is how far a sound is from the module's own.
 *
 * Pitch is either the module's or ours. Where the language has tones, or a
 * melody of its own, the pitch the module worked out is dropped and a
 * contour is made here: a line of targets, one run of them to a syllable,
 * followed by a second order filter so that the voice moves between them
 * the way a larynx does, with a phrase's own fall and its ending added.
 */

#include <math.h>
#include <stddef.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "evv_accent.h"

/* ---- the frame ----------------------------------------------------------- */

enum {
    P_STEP = 0, P_F0, P_AV, P_OQ, P_TL, P_FL, P_DI, P_AH, P_AF,
    P_F1, P_B1, P_DF1, P_DB1, P_F2, P_B2, P_F3, P_B3, P_F4, P_B4,
    P_F5, P_B5, P_F6, P_B6, P_F7, P_B7, P_F8, P_B8,
    P_FNP, P_BNP, P_FNZ, P_BNZ, P_FTP, P_BTP, P_FTZ, P_BTZ,
    P_A1F, P_A2F, P_A3F, P_A4F, P_A5F, P_A6F, P_A7F, P_A8F, P_AB,
    P_B1F, P_B2F, P_B3F, P_B4F, P_B5F, P_B6F, P_B7F, P_B8F,
    P_ANV, P_A1V, P_A2V, P_A3V, P_A4V, P_A5V, P_A6V, P_A7V, P_A8V, P_ATV,
    P_COUNT
};

typedef char frame_is_62[P_COUNT == EVV_ACCENT_PARMS ? 1 : -1];

static const int FORMANT[4] = { P_F1, P_F2, P_F3, P_F4 };
static const int BANDWIDTH[4] = { P_B1, P_B2, P_B3, P_B4 };

/* The six amplitudes of the noise's own branch, in the order a definition
   names them: a2 a3 a4 a5 a6 ab. */
static const int NOISE[6] = { P_A2F, P_A3F, P_A4F, P_A5F, P_A6F, P_AB };

/* ---- what the markup declares ------------------------------------------- */

#define NAME_LEN   8
#define ID_LEN     12
#define MAX_DEFS   384
#define MAX_TONES  64
#define MAX_RULES  96
#define MAX_PARTS  4
#define MAX_POINTS 8

#define UNSET (-10000)

/* A sound, as far as it differs from the module's own phone. Every number
   is what the markup said or what def_defaults put there. */
typedef struct {
    char id[ID_LEN];
    int  f[4];          /* formant ratios in percent, at the start */
    int  g[4];          /* and where they have got to by the end */
    int  b[4];          /* bandwidth ratios in percent */
    int  dur;           /* how long a vowel's stretch lasts, in percent */
    int  hold;          /* and a consonant's */
    int  vot;           /* voice onset after the release, milliseconds */
    int  asp;           /* aspiration in that time, dB */
    int  voi;           /* 1 voiced throughout, 0 voiceless throughout */
    int  brth;          /* breathy voice after the release, milliseconds */
    int  ej;            /* silence after the burst, milliseconds */
    int  impl;          /* an implosive */
    int  burst;         /* the burst, dB either way */
    int  nas;           /* nasalised, percent */
    int  av, ah, af;    /* levels, dB either way */
    int  fric;          /* frication of its own, dB */
    int  amp[6];        /* a2f a3f a4f a5f a6f ab, as they are to be */
    int  tap;           /* closures: one for a tap, more for a trill */
    int  tapms;         /* how long each lasts */
    int  oq, tl;        /* voice quality, as it is to be */
    int  creak;         /* creaky voice, percent */
    int  reach;         /* how long the formants take to get there, ms */
    int  f0;            /* pitch at the onset of the vowel after, tenths of
                           a semitone either way */
    int  glide;         /* 1: the formants move from f to g across the
                           stretch, as a diphthong's do */
    int  pre;           /* preaspiration before the closure, milliseconds */
    int  whisper;       /* said without voice, breath at this level, dB */
    int  ms;            /* a sound made here: how long it lasts */
    int  hush;          /* a sound made here: silence, as a glottal stop */
    int  noburst;       /* the stop is not released into noise */
    int  lead;          /* a voiced stop after a silence: the voice begins
                           this long before the release, milliseconds */
    int  bar;           /* how loud the voice is behind a closure, dB */
} Def;

typedef struct {
    int pos;            /* percent of the nucleus */
    int level;          /* tenths of a Chao level: 10 lowest, 50 highest */
} TonePoint;

typedef struct {
    char id[ID_LEN];
    TonePoint p[MAX_POINTS];
    int  n;
    int  dur;           /* the nucleus, in percent */
    int  creak;         /* creaky voice, percent, over `cfrom' to `cto' */
    int  cfrom, cto;
    int  stop;          /* ends in a glottal closure this long, ms */
    int  weak;          /* a tone that gives way: percent of full strength */
    int  brth;          /* breathy voice, percent */
    int  av;            /* level, dB either way */
} Tone;

/* What the module says in place of what it was given. */
typedef struct {
    char given[NAME_LEN];
    char said[MAX_PARTS][NAME_LEN];
    int  n;
    int  several;       /* X: all of them in order. S: any one of them */
} Rule;

typedef struct {
    int own;            /* the pitch is made here */
    int range;          /* Chao 1 to 5 in tenths of a semitone, at the
                           module's usual pitch fluctuation */
    int decl;           /* fall across a phrase, tenths of a semitone a
                           second */
    int declmax;        /* and how far it may go, tenths of a semitone */
    int lag;            /* how quickly the voice follows: the filter's
                           natural frequency, radians a second */
    int lead;           /* targets brought forward by this, milliseconds */
    int final_s;        /* statement: the last syllable, tenths of a
                           semitone either way */
    int final_q;        /* question wanting yes or no */
    int final_w;        /* question with a question word */
    int final_c;        /* going on after a comma */
    int final_e;        /* exclamation */
    int span;           /* how many syllables the ending is come to over */
    int qreg;           /* the whole of a question, tenths of a semitone */
    int qfall;          /* a question that rises and falls: how far its last
                           syllable comes down again, tenths of a semitone */
    int coda;           /* how long what follows the vowel in a syllable is
                           taken to last, when a tone is laid over both,
                           milliseconds */
    int stop;           /* the number the module's table gives the manner
                           of a stop */
    int accent;         /* stress accent: the stressed syllable's peak in
                           tenths of a semitone, nought for none */
    int accent2;        /* the same for secondary stress */
    int shape;          /* 0 peak on the stressed syllable, 1 low on it
                           and rising after, 2 rising to a late peak */
    int unstressed;     /* where unstressed syllables sit, tenths of a
                           semitone */
    int stretch;        /* every steady part, percent */
    int vowel;          /* vowels' steady parts, percent */
    int stressed;       /* stressed vowels besides, percent */
    int weak;           /* unstressed vowels besides, percent */
    int last;           /* the last syllable of a phrase besides, percent */
    int reach;          /* formants, unless a sound says otherwise, ms */
    int mid;            /* the voice's middle pitch if the text never says,
                           tenths of a hertz */
    int level;          /* where a syllable with no tone sits, tenths of a
                           Chao level */
    int frange;         /* the module's own pitch movement, percent */
    int fshift;         /* and the whole of it moved, tenths of a semitone */
} Profile;

/* ---- what is waiting to be said ----------------------------------------- */

enum {
    EX_SYLLABLE = 1,    /* the first phone of a syllable */
    EX_WORD = 2,        /* and of a word */
    EX_NUCLEUS = 4,
    EX_LAST = 8,        /* the phrase ends with this phone's syllable */
    EX_ONSET = 16,      /* before the nucleus of its syllable */
    EX_BEFORE = 32,     /* made here, out of the phone after it */
    EX_AFTER = 64       /* made here, out of the phone before it */
};

#define EX_MADE (EX_BEFORE | EX_AFTER)

typedef struct {
    char    name[NAME_LEN];
    int16_t def;        /* which Def, or -1 */
    int16_t tone;       /* which Tone, or -1 */
    uint8_t stress;
    uint8_t flags;
    uint8_t ending;     /* how the phrase it is in ends: s q w e c, or 0
                           while that has not been read */
    uint8_t part;       /* how many of an X rule's phones have gone by */
    int16_t left;       /* syllables still to come in the phrase after the
                           one this is in */
    int16_t after;      /* phones after the nucleus in this syllable */
} Expected;

typedef struct {
    char    name[NAME_LEN];
    Expected as;        /* what it was meant to be */
    int     known;      /* `as' is filled in */
    int     pause;
    int     on, off;    /* sounds made here at its start and its end: which
                           Def, or -1 */
    unsigned char record[8];
    int     from, to;   /* the arrays' milliseconds */
    int     whole;      /* the last of an X rule's phones, or not one */
    int     first;      /* the first of them, or not one */
} Phone;

#define TABLE 64
#define LINE_UP 16

typedef struct {
    int array_ms;
    int sounded_ms;
} Reached;

typedef struct Accent {
    void   *machine;
    int     on;

    Profile profile;
    Def     defs[MAX_DEFS];
    int     n_defs;
    Tone    tones[MAX_TONES];
    int     n_tones;
    Rule    rules[MAX_RULES];
    int     n_rules;

    Expected *queue;
    int     n_queue, room, next;
    int     phrase_from;        /* where the phrase being read began */
    int     cleared;

    Phone   now, before;
    int     have_now, have_before;
    int     misses;

    /* the voice, as the text last said */
    int     vb, vf;

    /* time */
    int     sounded;            /* milliseconds asked for in this run of
                                   arrays */
    int     last_to;
    Reached reached[TABLE];
    int     n_reached;

    /* the pitch */
    double  y, v;               /* the filter: semitones and their rate */
    int     started;
    double  line[MAX_POINTS + 2][2];   /* targets: ms sounded, semitones */
    int     n_line;
    double  command;
    double  phrase_ms;          /* since the phrase began */
    double  ending;             /* what the ending adds now, semitones */
    double  ending_to;          /* and what it is making for */
    double  register_to;
    double  reg;
    int     in_pause;
    double  mid_hz;
    int     creak_from, creak_to, creak;
    int     breath_from, breath_to, breath;
    int     stop_at, stop_for;

    int     release_at;         /* when the stop before let go, in what has
                                   sounded, or -1 */

    int32_t *out;
    int     out_room;
    double  vowel_av;

    /* the phones asked for and the frames that have come */
    Phone   line_up[LINE_UP];
    int     n_line_up;
    int32_t *held;
    int32_t *held_at;
    int     n_held, held_room;
} Accent;

/* ---- saying what was done ------------------------------------------------ */

/* EVV_ACCENT_TRACE names a file, and what is written there is which phone
   each stretch was taken to be and what was made of it. It is how a map is
   checked without anybody listening. */
static FILE *trace_file;
static int   trace_tried;

static FILE *trace(void)
{
    const char *name;

    if (trace_tried)
        return trace_file;
    trace_tried = 1;
    name = getenv("EVV_ACCENT_TRACE");
    if (name != 0 && *name != 0)
        trace_file = fopen(name, "a");
    return trace_file;
}

/* ---- one to a machine ---------------------------------------------------- */

#define MACHINES 16

static Accent *machines[MACHINES];

static Accent *find(void *machine, int make)
{
    int i;

    if (machine == 0)
        return 0;
    for (i = 0; i < MACHINES; i++)
        if (machines[i] != 0 && machines[i]->machine == machine)
            return machines[i];
    if (!make)
        return 0;
    for (i = 0; i < MACHINES; i++) {
        if (machines[i] == 0) {
            Accent *a = (Accent *)calloc(1, sizeof(Accent));

            if (a == 0)
                return 0;
            a->machine = machine;
            a->vb = -1;
            a->vf = -1;
            a->release_at = -1;
            machines[i] = a;
            return a;
        }
    }
    return 0;
}

void evv_accent_free(void *machine)
{
    int i;

    for (i = 0; i < MACHINES; i++) {
        if (machines[i] != 0 && machines[i]->machine == machine) {
            free(machines[i]->queue);
            free(machines[i]->out);
            free(machines[i]->held);
            free(machines[i]->held_at);
            free(machines[i]);
            machines[i] = 0;
        }
    }
}

int evv_accent_on(void *machine)
{
    Accent *a = find(machine, 0);

    return a != 0 && a->on;
}

/* ---- reading the markup -------------------------------------------------- */

static void profile_defaults(Profile *p)
{
    memset(p, 0, sizeof *p);
    p->range = 90;
    p->decl = 8;
    p->declmax = 30;
    p->lag = 45;
    p->lead = 20;
    p->final_s = -15;
    p->final_q = 25;
    p->final_w = -5;
    p->final_c = 10;
    p->final_e = -10;
    p->span = 2;
    p->qreg = 10;
    p->unstressed = -10;
    p->stretch = 100;
    p->vowel = 100;
    p->stressed = 100;
    p->weak = 100;
    p->last = 100;
    p->reach = 50;
    p->level = 30;
    p->frange = 100;
    p->coda = 70;
    p->stop = 1;
}

static void def_defaults(Def *d)
{
    int i;

    memset(d, 0, sizeof *d);
    for (i = 0; i < 4; i++) {
        d->f[i] = 100;
        d->g[i] = UNSET;
        d->b[i] = 100;
    }
    d->dur = 100;
    d->hold = 100;
    d->vot = UNSET;
    d->asp = UNSET;
    d->voi = UNSET;
    for (i = 0; i < 6; i++)
        d->amp[i] = UNSET;
    d->oq = UNSET;
    d->tl = UNSET;
    d->reach = UNSET;
    d->tapms = 20;
    d->whisper = UNSET;
    d->ms = 60;
    d->bar = 34;
}

static void tone_defaults(Tone *t)
{
    memset(t, 0, sizeof *t);
    t->dur = 100;
    t->weak = 100;
    t->cto = 100;
}

/* One word of a group: up to the next space or the brace. */
static const char *word(const char *p, const char *end, char *out, int room)
{
    int n = 0;

    while (p < end && (*p == ' ' || *p == '\t'))
        p++;
    while (p < end && *p != ' ' && *p != '\t') {
        if (n + 1 < room)
            out[n++] = *p;
        p++;
    }
    out[n] = 0;
    return p;
}

static int key_is(const char *w, const char *key, const char **value)
{
    size_t n = strlen(key);

    if (strncmp(w, key, n) != 0 || w[n] != '=')
        return 0;
    *value = w + n + 1;
    return 1;
}

static void name_copy(char *to, const char *from, int room)
{
    int n = 0;

    while (from[n] != 0 && n + 1 < room) {
        to[n] = from[n];
        n++;
    }
    to[n] = 0;
}

static int def_find(const Accent *a, const char *id)
{
    int i;

    for (i = 0; i < a->n_defs; i++)
        if (strcmp(a->defs[i].id, id) == 0)
            return i;
    return -1;
}

static int tone_find(const Accent *a, const char *id)
{
    int i;

    for (i = 0; i < a->n_tones; i++)
        if (strcmp(a->tones[i].id, id) == 0)
            return i;
    return -1;
}

typedef struct {
    const char *key;
    size_t      at;
} Key;

static void keys_set(void *record, const Key *keys, size_t n, const char *w)
{
    const char *v;
    size_t i;

    for (i = 0; i < n; i++) {
        if (key_is(w, keys[i].key, &v)) {
            *(int *)((char *)record + keys[i].at) = atoi(v);
            return;
        }
    }
}

static void profile_set(Accent *a, const char *p, const char *end)
{
    char w[64];
    const char *v;
    Profile *f = &a->profile;
    static const Key KEYS[] = {
        { "range", offsetof(Profile, range) },
        { "decl", offsetof(Profile, decl) },
        { "declmax", offsetof(Profile, declmax) },
        { "lag", offsetof(Profile, lag) },
        { "lead", offsetof(Profile, lead) },
        { "fs", offsetof(Profile, final_s) },
        { "fq", offsetof(Profile, final_q) },
        { "fw", offsetof(Profile, final_w) },
        { "fc", offsetof(Profile, final_c) },
        { "fe", offsetof(Profile, final_e) },
        { "span", offsetof(Profile, span) },
        { "qreg", offsetof(Profile, qreg) },
        { "qfall", offsetof(Profile, qfall) },
        { "coda", offsetof(Profile, coda) },
        { "stop", offsetof(Profile, stop) },
        { "accent", offsetof(Profile, accent) },
        { "accent2", offsetof(Profile, accent2) },
        { "shape", offsetof(Profile, shape) },
        { "unstressed", offsetof(Profile, unstressed) },
        { "stretch", offsetof(Profile, stretch) },
        { "vowel", offsetof(Profile, vowel) },
        { "stressed", offsetof(Profile, stressed) },
        { "weak", offsetof(Profile, weak) },
        { "last", offsetof(Profile, last) },
        { "reach", offsetof(Profile, reach) },
        { "mid", offsetof(Profile, mid) },
        { "level", offsetof(Profile, level) },
        { "frange", offsetof(Profile, frange) },
        { "fshift", offsetof(Profile, fshift) },
    };

    /* A new language, so nothing the last one declared is kept. */
    profile_defaults(f);
    a->n_defs = 0;
    a->n_tones = 0;
    a->n_rules = 0;

    for (;;) {
        p = word(p, end, w, sizeof w);
        if (w[0] == 0)
            break;
        if (key_is(w, "f0", &v)) {
            f->own = strcmp(v, "own") == 0;
            continue;
        }
        keys_set(f, KEYS, sizeof KEYS / sizeof KEYS[0], w);
    }
    a->on = 1;
}

static void def_set(Accent *a, const char *p, const char *end)
{
    char w[64];
    Def *d;
    int at;
    static const Key KEYS[] = {
        { "f1", offsetof(Def, f[0]) }, { "f2", offsetof(Def, f[1]) },
        { "f3", offsetof(Def, f[2]) }, { "f4", offsetof(Def, f[3]) },
        { "g1", offsetof(Def, g[0]) }, { "g2", offsetof(Def, g[1]) },
        { "g3", offsetof(Def, g[2]) }, { "g4", offsetof(Def, g[3]) },
        { "b1", offsetof(Def, b[0]) }, { "b2", offsetof(Def, b[1]) },
        { "b3", offsetof(Def, b[2]) }, { "b4", offsetof(Def, b[3]) },
        { "dur", offsetof(Def, dur) }, { "hold", offsetof(Def, hold) },
        { "vot", offsetof(Def, vot) }, { "asp", offsetof(Def, asp) },
        { "voi", offsetof(Def, voi) }, { "brth", offsetof(Def, brth) },
        { "ej", offsetof(Def, ej) }, { "impl", offsetof(Def, impl) },
        { "burst", offsetof(Def, burst) },
        { "nas", offsetof(Def, nas) },
        { "av", offsetof(Def, av) }, { "ah", offsetof(Def, ah) },
        { "af", offsetof(Def, af) }, { "fric", offsetof(Def, fric) },
        { "a2", offsetof(Def, amp[0]) }, { "a3", offsetof(Def, amp[1]) },
        { "a4", offsetof(Def, amp[2]) }, { "a5", offsetof(Def, amp[3]) },
        { "a6", offsetof(Def, amp[4]) }, { "ab", offsetof(Def, amp[5]) },
        { "tap", offsetof(Def, tap) }, { "tapms", offsetof(Def, tapms) },
        { "oq", offsetof(Def, oq) }, { "tl", offsetof(Def, tl) },
        { "creak", offsetof(Def, creak) },
        { "reach", offsetof(Def, reach) }, { "f0", offsetof(Def, f0) },
        { "glide", offsetof(Def, glide) }, { "pre", offsetof(Def, pre) },
        { "whisper", offsetof(Def, whisper) }, { "ms", offsetof(Def, ms) },
        { "hush", offsetof(Def, hush) },
        { "noburst", offsetof(Def, noburst) },
        { "lead", offsetof(Def, lead) }, { "bar", offsetof(Def, bar) },
    };

    p = word(p, end, w, sizeof w);
    if (w[0] == 0)
        return;
    at = def_find(a, w);
    if (at < 0) {
        if (a->n_defs >= MAX_DEFS)
            return;
        at = a->n_defs++;
    }
    d = &a->defs[at];
    def_defaults(d);
    name_copy(d->id, w, ID_LEN);

    for (;;) {
        p = word(p, end, w, sizeof w);
        if (w[0] == 0)
            break;
        keys_set(d, KEYS, sizeof KEYS / sizeof KEYS[0], w);
    }
}

static void tone_set(Accent *a, const char *p, const char *end)
{
    char w[128];
    const char *v;
    Tone *t;
    int at;
    static const Key KEYS[] = {
        { "dur", offsetof(Tone, dur) }, { "creak", offsetof(Tone, creak) },
        { "cfrom", offsetof(Tone, cfrom) }, { "cto", offsetof(Tone, cto) },
        { "stop", offsetof(Tone, stop) }, { "weak", offsetof(Tone, weak) },
        { "brth", offsetof(Tone, brth) }, { "av", offsetof(Tone, av) },
    };

    p = word(p, end, w, sizeof w);
    if (w[0] == 0)
        return;
    at = tone_find(a, w);
    if (at < 0) {
        if (a->n_tones >= MAX_TONES)
            return;
        at = a->n_tones++;
    }
    t = &a->tones[at];
    tone_defaults(t);
    name_copy(t->id, w, ID_LEN);

    for (;;) {
        p = word(p, end, w, sizeof w);
        if (w[0] == 0)
            break;
        if (key_is(w, "p", &v)) {
            t->n = 0;
            while (*v != 0 && t->n < MAX_POINTS) {
                char *e;
                long pos = strtol(v, &e, 10);
                long level;

                if (e == v || *e != ':')
                    break;
                v = e + 1;
                level = strtol(v, &e, 10);
                if (e == v)
                    break;
                t->p[t->n].pos = (int)pos;
                t->p[t->n].level = (int)level;
                t->n++;
                v = e;
                if (*v == ',')
                    v++;
            }
            continue;
        }
        keys_set(t, KEYS, sizeof KEYS / sizeof KEYS[0], w);
    }
}

static void rule_set(Accent *a, const char *p, const char *end, int several)
{
    char w[64];
    Rule *r;

    if (a->n_rules >= MAX_RULES)
        return;
    p = word(p, end, w, sizeof w);
    if (w[0] == 0)
        return;
    r = &a->rules[a->n_rules];
    memset(r, 0, sizeof *r);
    name_copy(r->given, w, NAME_LEN);
    r->several = several;
    for (;;) {
        p = word(p, end, w, sizeof w);
        if (w[0] == 0 || r->n >= MAX_PARTS)
            break;
        name_copy(r->said[r->n++], w, NAME_LEN);
    }
    if (r->n > 0)
        a->n_rules++;
}

static Expected *queue_more(Accent *a)
{
    if (a->n_queue == a->room) {
        int room = a->room ? a->room * 2 : 256;
        Expected *q;

        /* What has been said already is let go of first. */
        if (a->next > 128) {
            int drop = a->next - 8;

            memmove(a->queue, a->queue + drop,
                    (size_t)(a->n_queue - drop) * sizeof(Expected));
            a->n_queue -= drop;
            a->next -= drop;
            a->phrase_from -= drop;
            if (a->phrase_from < 0)
                a->phrase_from = 0;
            if (a->n_queue < a->room)
                return &a->queue[a->n_queue++];
        }
        q = (Expected *)realloc(a->queue, (size_t)room * sizeof(Expected));
        if (q == 0)
            return 0;
        a->queue = q;
        a->room = room;
    }
    return &a->queue[a->n_queue++];
}

static void word_set(Accent *a, const char *p, const char *end)
{
    char w[64];
    int stress = 0;
    int first_of_word = 1;
    int first_of_syllable = 0;

    for (;;) {
        Expected *e;
        char *mark;
        char *name = w;
        int made = 0;

        p = word(p, end, w, sizeof w);
        if (w[0] == 0)
            break;
        if (w[0] == '.' && (w[1] == 0 || (w[1] >= '0' && w[1] <= '9'))) {
            stress = w[1] ? w[1] - '0' : 0;
            first_of_syllable = 1;
            continue;
        }
        if (w[0] == '<' && w[1] != 0) {
            made = EX_BEFORE;
            name = w + 1;
        } else if (w[0] == '>' && w[1] != 0) {
            made = EX_AFTER;
            name = w + 1;
        }
        e = queue_more(a);
        if (e == 0)
            return;
        memset(e, 0, sizeof *e);
        e->def = -1;
        e->tone = -1;
        e->stress = (uint8_t)stress;
        e->flags = (uint8_t)made;
        if (!made) {
            if (first_of_syllable)
                e->flags |= EX_SYLLABLE;
            if (first_of_word)
                e->flags |= EX_WORD | EX_SYLLABLE;
            first_of_word = 0;
            first_of_syllable = 0;
        }

        mark = strchr(name, '^');
        if (mark != 0) {
            *mark = 0;
            if (!made) {
                e->flags |= EX_NUCLEUS;
                if (strcmp(mark + 1, "-") != 0)
                    e->tone = (int16_t)tone_find(a, mark + 1);
            }
        }
        mark = strchr(name, '=');
        if (mark != 0) {
            *mark = 0;
            e->def = (int16_t)def_find(a, mark + 1);
        }
        name_copy(e->name, name, NAME_LEN);
    }
}

/* The phrase ends here. Every syllable of it is told how it ends and how far
   off the end is, which is what lets an ending be come to over several
   syllables rather than dropped on the last. */
static void phrase_set(Accent *a, const char *p, const char *end)
{
    char w[16];
    int i, left = 0;
    int seen_last = 0;

    word(p, end, w, sizeof w);
    if (w[0] == 0)
        w[0] = 's';

    for (i = a->n_queue - 1; i >= a->phrase_from && i >= 0; i--) {
        Expected *e = &a->queue[i];

        e->ending = (uint8_t)w[0];
        e->left = (int16_t)left;
        if (!seen_last)
            e->flags |= EX_LAST;
        if (e->flags & EX_SYLLABLE) {
            left++;
            seen_last = 1;
        }
    }
    a->phrase_from = a->n_queue;
}

/* What a syllable's phones need to know of each other. A tone belongs to the
   syllable and is written on its nucleus, but the voice is already making
   for it through the consonants before, so every phone of the syllable is
   told the tone; those before the nucleus are marked as such; and the
   nucleus is told how many phones follow it, since a closed syllable
   carries its tone differently. */
static void syllables_set(Accent *a, int from)
{
    int i = from;

    while (i < a->n_queue) {
        int end = i + 1, nucleus = -1, k, after = 0;

        while (end < a->n_queue && !(a->queue[end].flags & EX_SYLLABLE))
            end++;
        for (k = i; k < end; k++)
            if (a->queue[k].flags & EX_NUCLEUS) {
                nucleus = k;
                break;
            }
        if (nucleus >= 0) {
            for (k = i; k < end; k++) {
                a->queue[k].tone = a->queue[nucleus].tone;
                if (k < nucleus)
                    a->queue[k].flags |= EX_ONSET;
                else if (k > nucleus && !(a->queue[k].flags & EX_MADE))
                    after++;
            }
            a->queue[nucleus].after = (int16_t)after;
        }
        i = end;
    }
}

static void queue_reset(Accent *a)
{
    a->n_queue = 0;
    a->next = 0;
    a->phrase_from = 0;
    a->have_now = 0;
    a->have_before = 0;
    a->misses = 0;
    a->release_at = -1;
    a->n_held = 0;
    a->n_line_up = 0;
}

char *evv_accent_strip(void *machine, const char *text, uint32_t len,
                       uint32_t *out_len)
{
    Accent *a;
    const char *end = text + len;
    const char *p;
    char *out, *o;
    int any = 0;

    if (text == 0 || len == 0 || memchr(text, '{', len) == 0)
        return 0;

    a = find(machine, 0);
    if (a == 0 || !a->on) {
        /* Not markup unless this is where it begins. */
        const char *at = text;
        int begins = 0;

        while (at + 6 < end) {
            at = (const char *)memchr(at, '{', (size_t)(end - at));
            if (at == 0 || at + 6 >= end)
                break;
            if (memcmp(at, "{A v=1", 6) == 0) {
                begins = 1;
                break;
            }
            at++;
        }
        if (!begins)
            return 0;
        a = find(machine, 1);
        if (a == 0)
            return 0;
    }

    out = (char *)malloc((size_t)len + 1);
    if (out == 0)
        return 0;
    o = out;

    if (a->cleared) {
        queue_reset(a);
        a->cleared = 0;
    }

    for (p = text; p < end;) {
        const char *close;

        if (*p != '{' || p + 2 >= end || strchr("ADTXSWP", p[1]) == 0
            || (p[2] != ' ' && p[2] != '}')) {
            *o++ = *p++;
            continue;
        }
        close = (const char *)memchr(p, '}', (size_t)(end - p));
        if (close == 0) {
            *o++ = *p++;
            continue;
        }
        if (!a->on && p[1] != 'A') {
            *o++ = *p++;
            continue;
        }
        switch (p[1]) {
        case 'A': profile_set(a, p + 2, close); break;
        case 'D': def_set(a, p + 2, close); break;
        case 'T': tone_set(a, p + 2, close); break;
        case 'X': rule_set(a, p + 2, close, 1); break;
        case 'S': rule_set(a, p + 2, close, 0); break;
        case 'W': {
            int from = a->n_queue;

            word_set(a, p + 2, close);
            syllables_set(a, from);
            break;
        }
        case 'P': phrase_set(a, p + 2, close); break;
        }
        any = 1;
        p = close + 1;
    }
    *o = 0;

    if (!any) {
        free(out);
        return 0;
    }
    if (out_len != 0)
        *out_len = (uint32_t)(o - out);
    return out;
}

void evv_accent_release(char *text)
{
    free(text);
}

void evv_accent_clear(void *machine)
{
    Accent *a = find(machine, 0);

    /* Asked from whichever thread wanted the input gone, so it is left as a
       note for the thread that owns the queue. */
    if (a != 0)
        a->cleared = 1;
}

/* The voice's pitch, as the text last set it. ECI says a voice parameter as
   an annotation ahead of the words, which is the only place the number ever
   appears. */
void evv_accent_sentence(void *machine, const char *text)
{
    Accent *a;
    const char *p;

    /* Only a text that sets the voice is worth keeping anything for: a
       machine that is never given markup is never given a state either,
       unless somebody sets its voice, and then the state is all it costs. */
    if (text == 0 || strstr(text, "`v") == 0)
        return;
    a = find(machine, 1);
    if (a == 0)
        return;
    if (a->on && trace() != 0) {
        fprintf(trace(), "sentence: %s\n", text);
        fflush(trace());
    }
    for (p = text; (p = strchr(p, '`')) != 0; p++) {
        if (p[1] != 'v')
            continue;
        if (p[2] == 'b' && p[3] >= '0' && p[3] <= '9')
            a->vb = atoi(p + 3);
        else if (p[2] == 'f' && p[3] >= '0' && p[3] <= '9')
            a->vf = atoi(p + 3);
        else if (p[2] >= '1' && p[2] <= '8'
                 && (p[3] == ' ' || p[3] == 0 || p[3] == '`')) {
            /* A preset: what it sets is the module's own business, and the
               pitch is read off the module's frames until the text says. */
            a->vb = -1;
            a->vf = -1;
        }
    }
}

/* ---- finding a phone in the queue --------------------------------------- */

#define AHEAD 4

static const Rule *rule_for(const Accent *a, const char *given, int several)
{
    int i;

    for (i = 0; i < a->n_rules; i++)
        if (a->rules[i].several == several
            && strcmp(a->rules[i].given, given) == 0)
            return &a->rules[i];
    return 0;
}

/* Whether the phone the module named can be what was written at `at', and
   whether that uses the entry up. */
static int fits(Accent *a, int at, const char *name, int *used, int *first)
{
    Expected *e = &a->queue[at];
    const Rule *r;
    int i;

    *used = 1;
    *first = 1;
    if (e->part > 0) {
        r = rule_for(a, e->name, 1);
        if (r != 0 && e->part < r->n && strcmp(r->said[e->part], name) == 0) {
            e->part++;
            *used = e->part >= r->n;
            *first = 0;
            return 1;
        }
        return 0;
    }
    if (strcmp(e->name, name) == 0)
        return 1;
    r = rule_for(a, e->name, 1);
    if (r != 0 && strcmp(r->said[0], name) == 0) {
        e->part = 1;
        *used = r->n <= 1;
        return 1;
    }
    for (i = 0; i < a->n_rules; i++) {
        int k;

        r = &a->rules[i];
        if (r->several || strcmp(r->given, e->name) != 0)
            continue;
        for (k = 0; k < r->n; k++)
            if (strcmp(r->said[k], name) == 0)
                return 1;
    }
    return 0;
}

void evv_accent_place(void *machine, const char *name,
                      const unsigned char *record, int length)
{
    Accent *a = find(machine, 0);
    Phone *p;
    int at, seen = 0, ahead, used = 1, first = 1;

    if (a == 0 || !a->on)
        return;
    if (a->cleared) {
        queue_reset(a);
        a->cleared = 0;
    }

    p = &a->now;
    memset(p, 0, sizeof *p);
    name_copy(p->name, name ? name : "", NAME_LEN);
    p->whole = 1;
    p->first = 1;
    p->on = -1;
    p->off = -1;
    if (record != 0 && length > 0)
        memcpy(p->record, record,
               (size_t)(length < (int)sizeof p->record
                            ? length : (int)sizeof p->record));
    a->have_now = 1;

    if (strcmp(p->name, "#") == 0) {
        p->pause = 1;
        return;
    }

    ahead = a->misses >= 3 ? AHEAD * 3 : AHEAD;
    for (at = a->next; at < a->n_queue && seen < ahead; at++) {
        if (a->queue[at].flags & EX_MADE)
            continue;
        seen++;
        if (fits(a, at, p->name, &used, &first)) {
            int k;

            p->as = a->queue[at];
            p->known = 1;
            p->whole = used;
            p->first = first;
            /* What is made here out of this phone: whatever was written
               straight before it to come before, and straight after it to
               come after. */
            if (first) {
                for (k = at - 1; k >= a->next && k >= 0; k--) {
                    if (!(a->queue[k].flags & EX_MADE))
                        break;
                    if (a->queue[k].flags & EX_BEFORE) {
                        p->on = a->queue[k].def;
                        break;
                    }
                }
            }
            k = at + 1;
            if (used) {
                while (k < a->n_queue && (a->queue[k].flags & EX_AFTER)) {
                    p->off = a->queue[k].def;
                    k++;
                }
            }
            a->next = used ? k : at;
            a->misses = 0;
            return;
        }
        /* An X rule that was begun and not finished is given up. */
        a->queue[at].part = 0;
    }
    a->misses++;
}

/* ---- time ---------------------------------------------------------------- */

int32_t evv_accent_sounded(void *machine, int32_t array_ms)
{
    Accent *a = find(machine, 0);
    int i;

    if (a == 0 || !a->on || a->n_reached == 0)
        return array_ms;
    for (i = a->n_reached - 1; i >= 0; i--)
        if (a->reached[i].array_ms == array_ms)
            return a->reached[i].sounded_ms;
    /* Between two stretches, or past the last: as far from the nearest one
       before it as the arrays say. */
    for (i = a->n_reached - 1; i >= 0; i--)
        if (a->reached[i].array_ms <= array_ms)
            return a->reached[i].sounded_ms
                 + (array_ms - a->reached[i].array_ms);
    return array_ms;
}

static void reach(Accent *a, int array_ms, int sounded_ms)
{
    if (a->n_reached == TABLE) {
        memmove(a->reached, a->reached + 1, (TABLE - 1) * sizeof(Reached));
        a->n_reached--;
    }
    a->reached[a->n_reached].array_ms = array_ms;
    a->reached[a->n_reached].sounded_ms = sounded_ms;
    a->n_reached++;
}

/* ---- the pitch ----------------------------------------------------------- */

/* The middle of the voice's pitch for each pitch baseline the interface can
   ask for, in tenths of a hertz: what the engine says at that baseline with
   the pitch fluctuation at nought, which is a line with nothing on it.
   Measured every ten and the same in every module, the arithmetic being in
   the rules they share. */
static const int MIDDLE[11] = {
    400, 427, 468, 533, 632, 786, 1025, 1394, 1966, 2851, 4221
};

static double middle_hz(const Accent *a, const int32_t *frame)
{
    if (a->profile.mid > 0)
        return a->profile.mid / 10.0;
    if (a->vb >= 0 && a->vb <= 100) {
        int i = a->vb / 10;
        double w = (a->vb % 10) / 10.0;
        double lo = MIDDLE[i];
        double hi = MIDDLE[i < 10 ? i + 1 : 10];

        return (lo * pow(hi / lo, w)) / 10.0;
    }
    if (a->vb > 100)
        return (double)a->vb;
    /* Nobody has said: the module begins a sentence close to the middle. */
    if (frame != 0 && frame[P_F0] > 0)
        return frame[P_F0] / 10.0;
    return 119.0;
}

static double range_scale(const Accent *a)
{
    double s;

    if (a->vf < 0)
        return 1.0;
    s = a->vf / 30.0;
    if (s > 2.5)
        s = 2.5;
    return s;
}

/* A Chao level in tenths, as semitones from the middle of the voice. */
static double level_st(const Accent *a, int level)
{
    return (level - 30) / 40.0 * (a->profile.range / 10.0) * range_scale(a);
}

static void line_clear(Accent *a)
{
    a->n_line = 0;
}

static void line_add(Accent *a, double ms, double st)
{
    if (a->n_line >= MAX_POINTS + 2)
        return;
    if (a->n_line > 0 && ms < a->line[a->n_line - 1][0])
        ms = a->line[a->n_line - 1][0];
    a->line[a->n_line][0] = ms;
    a->line[a->n_line][1] = st;
    a->n_line++;
}

static double line_at(Accent *a, double ms)
{
    int i;

    if (a->n_line == 0)
        return a->command;
    if (ms <= a->line[0][0])
        return a->line[0][1];
    for (i = 1; i < a->n_line; i++) {
        if (ms <= a->line[i][0]) {
            double span = a->line[i][0] - a->line[i - 1][0];
            double w = span > 0 ? (ms - a->line[i - 1][0]) / span : 1.0;

            return a->line[i - 1][1]
                 + w * (a->line[i][1] - a->line[i - 1][1]);
        }
    }
    return a->line[a->n_line - 1][1];
}

/* The targets a stretch brings with it. It begins `t0' into what has
   sounded and lasts `length'. */
static void pitch_plan(Accent *a, const Phone *ph, double t0, int length)
{
    const Profile *f = &a->profile;
    double lead = f->lead;

    /* What follows the vowel in its syllable goes on with the vowel's
       melody: a tone is laid over the whole rhyme, and what was planned
       when the vowel began is still being carried out. */
    if (ph->known && !ph->pause && a->n_line > 0 && a->started
        && !(ph->as.flags & (EX_NUCLEUS | EX_ONSET | EX_SYLLABLE | EX_MADE)))
        return;

    /* Wherever the line had got to is where the next one starts. */
    a->command = line_at(a, t0);
    line_clear(a);
    a->creak = 0;
    a->breath = 0;
    a->stop_for = 0;

    if (ph->pause) {
        a->in_pause = 1;
        return;
    }
    if (!ph->known)
        return;

    if (a->in_pause || !a->started) {
        /* A phrase begins. */
        a->phrase_ms = 0;
        a->ending = 0;
        a->ending_to = 0;
        a->in_pause = 0;
        a->reg = 0;
        a->register_to = 0;
        /* The voice starts where the first tone starts. There is nothing
           before it for the pitch to have come from. */
        if (ph->as.tone >= 0 && ph->as.tone < a->n_tones
            && a->tones[ph->as.tone].n > 0) {
            const Tone *t = &a->tones[ph->as.tone];
            double mid = level_st(a, f->level);
            double st = level_st(a, t->p[0].level);

            st = mid + (st - mid) * t->weak / 100.0;
            a->command = st;
            a->y = st;
            a->v = 0;
            a->started = 1;
        }
    }

    /* How the phrase ends is come to over its last syllables. */
    if (ph->as.flags & EX_SYLLABLE) {
        int span = f->span > 0 ? f->span : 1;
        double whole = 0;

        switch (ph->as.ending) {
        case 's': whole = f->final_s / 10.0; break;
        case 'q': whole = f->final_q / 10.0; break;
        case 'w': whole = f->final_w / 10.0; break;
        case 'c': whole = f->final_c / 10.0; break;
        case 'e': whole = f->final_e / 10.0; break;
        }
        if (ph->as.ending != 0 && ph->as.left < span)
            a->ending_to = whole * (span - ph->as.left) / span;
        else
            a->ending_to = 0;
        if (ph->as.ending == 'q' && f->qfall != 0) {
            /* Up to the syllable before the last and down on the last:
               the question of Hungarian, Russian, Romanian, Greek. */
            if (ph->as.left == 1)
                a->ending_to = whole;
            else if (ph->as.left == 0)
                a->ending_to = whole - f->qfall / 10.0;
            else if (ph->as.left == 2)
                a->ending_to = whole * 0.4;
        }
        a->register_to = ph->as.ending == 'q' ? f->qreg / 10.0 : 0;
    }

    if (ph->as.flags & EX_NUCLEUS) {
        if (ph->as.tone >= 0 && ph->as.tone < a->n_tones) {
            const Tone *t = &a->tones[ph->as.tone];
            double mid = level_st(a, f->level);
            double weak = t->weak / 100.0;
            /* A tone belongs to the rhyme: the vowel and what is voiced
               after it in the syllable, which has not been seen yet and is
               taken to last what a nasal lasts. */
            double whole = length + (ph->as.after > 0 ? f->coda : 0);
            int i;

            line_add(a, t0, a->command);
            for (i = 0; i < t->n; i++) {
                double at = t0 + t->p[i].pos / 100.0 * whole - lead;
                double st = level_st(a, t->p[i].level);

                if (at < t0)
                    at = t0;
                /* A weak tone gives way towards the middle. */
                st = mid + (st - mid) * weak;
                line_add(a, at, st);
            }
            if (t->creak > 0) {
                a->creak = t->creak;
                a->creak_from = (int)(t0 + t->cfrom / 100.0 * length);
                a->creak_to = (int)(t0 + t->cto / 100.0 * length);
            }
            if (t->brth > 0) {
                a->breath = t->brth;
                a->breath_from = (int)t0;
                a->breath_to = (int)(t0 + length);
            }
            if (t->stop > 0) {
                a->stop_for = t->stop;
                a->stop_at = (int)(t0 + length - t->stop);
            }
        } else if (f->accent != 0 || f->unstressed != 0) {
            /* No tone: the melody is the stress. */
            double peak = ph->as.stress == 1 ? f->accent / 10.0
                        : ph->as.stress == 2 ? f->accent2 / 10.0
                        : f->unstressed / 10.0;

            peak *= range_scale(a);
            line_add(a, t0, a->command);
            if (ph->as.stress == 0 || f->shape == 0) {
                line_add(a, t0 + 0.4 * length, peak);
                line_add(a, t0 + length, peak * 0.6);
            } else if (f->shape == 1) {
                /* Low on the syllable and up out of it. */
                line_add(a, t0 + 0.3 * length, -peak * 0.4);
                line_add(a, t0 + length, peak);
            } else {
                /* Rising through the syllable to a peak at its end. */
                line_add(a, t0 + 0.2 * length, peak * 0.2);
                line_add(a, t0 + 0.9 * length, peak);
            }
        } else {
            line_add(a, t0, a->command);
            line_add(a, t0 + 0.5 * length, level_st(a, f->level));
        }
    } else if ((ph->as.flags & EX_ONSET) && ph->as.tone >= 0
               && ph->as.tone < a->n_tones && a->tones[ph->as.tone].n > 0) {
        /* Before the nucleus the voice is already making for where the
           tone begins. */
        const Tone *t = &a->tones[ph->as.tone];
        double mid = level_st(a, f->level);
        double st = level_st(a, t->p[0].level);
        double by = t0 + length - lead;

        st = mid + (st - mid) * t->weak / 100.0;
        if (by < t0 + 10)
            by = t0 + 10;
        line_add(a, t0, a->command);
        line_add(a, by, st);
    }
    /* After the nucleus the voice stays where the tone left it. */
}

/* One frame's pitch, `ms' into what has sounded. */
static int pitch_at(Accent *a, double ms, double dt_ms, const int32_t *frame)
{
    const Profile *f = &a->profile;
    double w = f->lag > 0 ? (double)f->lag : 45.0;
    double dt = dt_ms / 1000.0;
    double c = line_at(a, ms);
    double acc, fall, st, hz;
    int steps, i;

    if (a->mid_hz <= 0)
        a->mid_hz = middle_hz(a, frame);

    if (!a->started) {
        a->y = c;
        a->v = 0;
        a->started = 1;
    }

    /* Short steps, so the filter is the same filter whatever a frame lasts. */
    steps = (int)(dt_ms + 0.5);
    if (steps < 1)
        steps = 1;
    for (i = 0; i < steps; i++) {
        double h = dt / steps;

        acc = w * w * (c - a->y) - 2.0 * w * a->v;
        a->v += acc * h;
        a->y += a->v * h;
    }

    /* The ending and the register are come to gently as well. */
    a->ending += (a->ending_to - a->ending) * (1.0 - exp(-dt * 12.0));
    a->reg += (a->register_to - a->reg) * (1.0 - exp(-dt * 6.0));

    a->phrase_ms += dt_ms;
    fall = f->decl / 10.0 * a->phrase_ms / 1000.0;
    if (fall > f->declmax / 10.0)
        fall = f->declmax / 10.0;
    fall *= range_scale(a);

    st = a->y - fall + (a->ending + a->reg) * range_scale(a);
    hz = a->mid_hz * pow(2.0, st / 12.0);
    if (hz < 40)
        hz = 40;
    if (hz > 600)
        hz = 600;
    return (int)(hz * 10.0 + 0.5);
}

/* The module's own pitch, made wider or narrower about the middle of the
   voice and moved as a whole. */
static int pitch_of_module(Accent *a, const int32_t *frame)
{
    const Profile *f = &a->profile;
    double mid, st;

    if (f->frange == 100 && f->fshift == 0)
        return frame[P_F0];
    if (frame[P_F0] <= 0)
        return frame[P_F0];
    if (a->mid_hz <= 0)
        a->mid_hz = middle_hz(a, frame);
    mid = a->mid_hz;
    st = 12.0 * log(frame[P_F0] / 10.0 / mid) / log(2.0);
    st = st * f->frange / 100.0 + f->fshift / 10.0;
    return (int)(mid * pow(2.0, st / 12.0) * 10.0 + 0.5);
}

/* ---- a stretch of frames ------------------------------------------------- */

static int is_silent(const int32_t *f)
{
    return f[P_AV] == 0 && f[P_AF] == 0 && f[P_AH] == 0;
}

static int is_stop(const Accent *a, const Phone *p)
{
    /* Class 2 is a consonant in every module. Which manner is the stop is
       not the same in all of them -- the German module numbers its manners
       from one and the rest from nought -- so the language's profile says. */
    return p->record[0] == 2 && p->record[3] == a->profile.stop;
}

static int is_vowel(const Phone *p)
{
    return p->record[0] == 1;
}

static double smooth(double x)
{
    if (x <= 0)
        return 0;
    if (x >= 1)
        return 1;
    return x * x * (3.0 - 2.0 * x);
}

static double ratio_log(int percent)
{
    if (percent <= 0 || percent == 100)
        return 0;
    return log(percent / 100.0);
}

static const Def *def_at(const Accent *a, int index)
{
    if (index < 0 || index >= a->n_defs)
        return 0;
    return &a->defs[index];
}

static const Def *def_of(const Accent *a, const Phone *p)
{
    if (!p->known)
        return 0;
    return def_at(a, p->as.def);
}

static int clamp(int v, int lo, int hi)
{
    return v < lo ? lo : v > hi ? hi : v;
}

static int32_t *room_for(Accent *a, int frames)
{
    if (frames > a->out_room) {
        int room = frames + 64;
        int32_t *o = (int32_t *)realloc(a->out,
                                        (size_t)room * P_COUNT
                                            * sizeof(int32_t));

        if (o == 0)
            return 0;
        a->out = o;
        a->out_room = room;
    }
    return a->out;
}

/* How steady a frame is: whether lengthening the stretch may be done here.
   A steady frame is one whose neighbours say the same thing. */
static int steady(const int32_t *frames, int count, int i)
{
    const int32_t *f = frames + (size_t)i * P_COUNT;
    const int32_t *p = frames + (size_t)(i > 0 ? i - 1 : i) * P_COUNT;
    const int32_t *n = frames + (size_t)(i + 1 < count ? i + 1 : i) * P_COUNT;
    int k;
    static const int WATCH[] = { P_AV, P_AF, P_AH, P_F1, P_F2, P_F3 };

    for (k = 0; k < (int)(sizeof WATCH / sizeof WATCH[0]); k++) {
        int x = p[WATCH[k]], y = n[WATCH[k]], c = f[WATCH[k]];
        int tol = WATCH[k] >= P_F1 ? c / 50 + 4 : 1;

        if (abs(x - y) > tol)
            return 0;
    }
    return 1;
}

static void lerp(int32_t *out, const int32_t *x, const int32_t *y, double w)
{
    int k;

    for (k = 0; k < P_COUNT; k++)
        out[k] = (int32_t)floor(x[k] + (y[k] - x[k]) * w + 0.5);
}

/* A sound made here: breath, or noise, or nothing, with the formants of the
   frame it was made out of. */
static void made(int32_t *f, const Def *d, double w)
{
    int i;

    f[P_AV] = 0;
    f[P_TL] = 0;
    if (d->hush) {
        f[P_AH] = 0;
        f[P_AF] = 0;
        return;
    }
    f[P_AH] = (int32_t)((d->whisper != UNSET ? d->whisper : 45) * w);
    f[P_AF] = (int32_t)(d->fric * w);
    if (d->fric > 0)
        for (i = 0; i < 6; i++)
            f[NOISE[i]] = d->amp[i] != UNSET ? d->amp[i] : 0;
    if (d->voi == 1) {
        /* Said with the voice going: a breathy h, a voiced pharyngeal. */
        f[P_AV] = (int32_t)(44 * w);
        f[P_OQ] = clamp(f[P_OQ] + 25, 10, 99);
        f[P_TL] = 12;
    }
    for (i = 0; i < 4; i++) {
        double l = ratio_log(d->f[i]);

        if (l != 0)
            f[FORMANT[i]] = (int32_t)(f[FORMANT[i]] * exp(l) + 0.5);
    }
}

/* One phone's stretch, whole: every frame the arrays gave between where the
   mouth began to move towards it and where it began to move towards the
   next. */
static int stretch(Accent *a, Phone *ph, int32_t step,
                   const int32_t *frames, int count,
                   evv_accent_emit emit, void *context)
{
    const Phone *before = a->have_before ? &a->before : 0;
    const Def *def = def_of(a, ph);
    const Def *was = before != 0 ? def_of(a, before) : 0;
    const Def *on = def_at(a, ph->on);
    const Def *off = def_at(a, ph->off);
    const Profile *prof = &a->profile;
    double *rate = 0;
    int32_t *out;
    int i, k, n_own, n_on = 0, n_off = 0, n_lead = 0, n_out, ok = 1;
    int release_from = -1, release_voiced = -1;
    int released = -1;
    int length_out, own_from, own_to;
    int changed = 0;
    int32_t to = ph->to;

    if (count <= 0) {
        /* Too short to have a frame of its own, but it was said. */
        reach(a, to, a->sounded);
        a->before = *ph;
        a->have_before = 1;
        return 1;
    }

    /* A release belongs to the stop before only if that stop was whole and
       this is not the second half of an affricate, which has its own. */
    if (before == 0 || !is_stop(a, before) || !before->whole)
        was = before != 0 && !is_stop(a, before) ? was : 0;

    /* ---- how long each frame is to last ---- */

    rate = (double *)malloc((size_t)count * sizeof(double));
    if (rate == 0)
        return 0;
    for (i = 0; i < count; i++)
        rate[i] = 1.0;

    /* Where the stop before was released, and where the voice began. */
    if (before != 0 && is_stop(a, before) && before->whole) {
        int limit = count < 40 ? count : 40;

        for (i = 0; i < limit; i++) {
            const int32_t *f = frames + (size_t)i * P_COUNT;

            if (release_from < 0 && (f[P_AF] > 0 || f[P_AH] > 0))
                release_from = i;
            if (f[P_AV] > 0) {
                release_voiced = i;
                break;
            }
        }
        if (release_from < 0)
            release_from = release_voiced >= 0 ? release_voiced : 0;
    }

    {
        double s = 1.0;

        if (!ph->pause) {
            s = prof->stretch / 100.0;
            if (is_vowel(ph)) {
                s *= prof->vowel / 100.0;
                if (ph->known && ph->as.stress == 1)
                    s *= prof->stressed / 100.0;
                if (ph->known && ph->as.stress == 0)
                    s *= prof->weak / 100.0;
            }
            if (ph->known && (ph->as.flags & EX_LAST) && ph->as.ending != 0
                && ph->as.ending != 'c')
                s *= prof->last / 100.0;
            if (def != 0)
                s *= (is_vowel(ph) ? def->dur : def->hold) / 100.0;
            if (ph->known && (ph->as.flags & EX_NUCLEUS) && ph->as.tone >= 0
                && ph->as.tone < a->n_tones)
                s *= a->tones[ph->as.tone].dur / 100.0;
        }
        if (s < 0.2)
            s = 0.2;
        if (s > 4.0)
            s = 4.0;
        if (s < 0.999 || s > 1.001) {
            /* The stretch is to last s times as long, all but the release
               of the stop before it. What is steady in it gives or takes
               the difference as far as it can, since a vowel is made longer
               in its middle and not in the movements at its ends; what the
               steady part cannot give, the rest shares. */
            int from = release_voiced >= 0 ? release_voiced : 0;
            double all = 0, still = 0, want, r = 1.0, rest = s;

            for (i = from; i < count; i++) {
                all += 1;
                if (steady(frames, count, i))
                    still += 1;
            }
            want = s * all;
            if (still > 0) {
                double moving = all - still;

                r = (want - moving) / still;
                if (r < 0.34)
                    r = 0.34;
                if (r > 3.0)
                    r = 3.0;
                rest = moving > 0 ? (want - still * r) / moving : 1.0;
                if (rest < 0.5)
                    rest = 0.5;
                if (rest > 2.0)
                    rest = 2.0;
            }
            for (i = from; i < count; i++)
                rate[i] = steady(frames, count, i) ? r : rest;
            changed = 1;
        }
    }

    /* A voice onset later than the module makes it has to be given the
       time. One that is earlier needs none: the voice is begun sooner. */
    if (was != 0 && is_stop(a, before) && release_voiced >= 0
        && was->vot != UNSET && was->vot > 0) {
        int native = 0;

        for (i = release_from; i < release_voiced; i++)
            native += frames[(size_t)i * P_COUNT + P_STEP];
        if (a->release_at >= 0 && a->sounded - a->release_at <= 30)
            native += a->sounded - a->release_at;
        if (native > 0 && release_voiced > release_from
            && was->vot > native + step) {
            double s = (double)(was->vot - (native
                - (release_voiced - release_from) * step))
                / ((release_voiced - release_from) * step);

            for (i = release_from; i < release_voiced; i++)
                rate[i] = s;
            changed = 1;
        } else if (release_voiced <= release_from && was->vot > native + step) {
            /* No time at all between the release and the voice, so the
               first frames of the vowel are what is lengthened. */
            int want = (was->vot - native + step - 1) / step;
            int have = 2;

            if (release_voiced + have > count)
                have = count - release_voiced;
            for (i = 0; i < have; i++)
                rate[release_voiced + i] = (double)(want + have) / have;
            changed = 1;
        }
    }

    /* ---- the frames as they are to sound ---- */

    {
        double total = 0;

        for (i = 0; i < count; i++)
            total += rate[i];
        n_own = changed ? (int)(total + 0.5) : count;
        if (n_own < 1)
            n_own = 1;
    }
    if (on != 0 && !ph->pause)
        n_on = (on->ms + step - 1) / step;
    if (off != 0 && !ph->pause)
        n_off = (off->ms + step - 1) / step;

    /* A voiced stop that follows a silence. The module begins such a stop
       where it lets go or just before, as German and English do, and a
       language whose b, d and g are voiced has the voice going well before
       that: the closure is made here, as long as the definition says. */
    if (def != 0 && def->lead > 0 && is_stop(a, ph) && ph->whole && !ph->pause
        && (before == 0 || before->pause)) {
        double shut = 0;

        for (i = 0; i < count; i++) {
            const int32_t *f = frames + (size_t)i * P_COUNT;

            /* the closure lasts until the burst, or until the mouth opens:
               some modules voice their own closures, as loud as a vowel,
               and those count towards the lead as well */
            if (f[P_AF] != 0 || f[P_AH] != 0
                || (f[P_AV] >= 40 && f[P_F1] > 280))
                break;
            shut += f[P_STEP] * rate[i];
        }
        if (shut < def->lead)
            n_lead = (int)((def->lead - shut + step - 1) / step);
        if (n_lead > 40)
            n_lead = 40;
    }
    n_out = n_on + n_lead + n_own + n_off;

    out = room_for(a, n_out + 8);
    if (out == 0) {
        free(rate);
        return 0;
    }

    {
        int32_t *own = out + (size_t)(n_on + n_lead) * P_COUNT;

        if (!changed) {
            memcpy(own, frames, (size_t)count * P_COUNT * sizeof(int32_t));
        } else {
            /* Frame k of the output is wherever in the input the rates have
               got to by then. */
            int src = 0;
            double into = 0;        /* how much of frame src has been used */

            for (k = 0; k < n_own; k++) {
                const int32_t *f0, *f1;
                double w;

                while (src < count - 1 && into >= rate[src]) {
                    into -= rate[src];
                    src++;
                }
                w = rate[src] > 0 ? into / rate[src] : 0;
                if (w > 1)
                    w = 1;
                f0 = frames + (size_t)src * P_COUNT;
                f1 = frames + (size_t)(src + 1 < count ? src + 1 : src)
                                  * P_COUNT;
                /* A frame that is being held is said again as it is; only
                   a frame being hurried is met half way to the next. */
                if (rate[src] > 1.0)
                    w = 0;
                lerp(own + (size_t)k * P_COUNT, f0, f1, w);
                own[(size_t)k * P_COUNT + P_STEP] = step;
                into += 1.0;
            }
        }

        /* The closure put in front of a voiced stop: the mouth as it is
           when the stop begins, shut, and nothing heard but what the
           definition's voice gives it further on. */
        if (n_lead > 0) {
            for (k = 0; k < n_lead; k++) {
                int32_t *f = out + (size_t)(n_on + k) * P_COUNT;

                memcpy(f, own, P_COUNT * sizeof(int32_t));
                f[P_STEP] = step;
                f[P_AV] = 0;
                f[P_AF] = 0;
                f[P_AH] = 0;
            }
            own = out + (size_t)n_on * P_COUNT;
            n_own += n_lead;
        }

        /* What is made here out of the phone's start and its end. */
        if (n_on > 0) {
            const int32_t *like = own;

            /* The first frame with the voice in it is what the sound is
               made like: its formants are where the mouth already is. */
            for (k = 0; k < n_own; k++)
                if (own[(size_t)k * P_COUNT + P_AV] > 0) {
                    like = own + (size_t)k * P_COUNT;
                    break;
                }
            for (k = 0; k < n_on; k++) {
                int32_t *f = out + (size_t)k * P_COUNT;
                double w = 1.0;

                memcpy(f, like, P_COUNT * sizeof(int32_t));
                f[P_STEP] = step;
                if (k == 0)
                    w = 0.6;
                made(f, on, w);
            }
        }
        if (n_off > 0) {
            const int32_t *like = own + (size_t)(n_own - 1) * P_COUNT;

            for (k = n_own - 1; k >= 0; k--)
                if (own[(size_t)k * P_COUNT + P_AV] > 0) {
                    like = own + (size_t)k * P_COUNT;
                    break;
                }
            for (k = 0; k < n_off; k++) {
                int32_t *f = out + (size_t)(n_on + n_own + k) * P_COUNT;
                double w = k == n_off - 1 ? 0.6 : 1.0;

                memcpy(f, like, P_COUNT * sizeof(int32_t));
                f[P_STEP] = step;
                made(f, off, w);
            }
        }
    }

    length_out = 0;
    for (k = 0; k < n_out; k++)
        length_out += out[(size_t)k * P_COUNT + P_STEP];
    own_from = n_on * step;
    own_to = own_from;
    for (k = n_on; k < n_on + n_own; k++)
        own_to += out[(size_t)k * P_COUNT + P_STEP];

    /* Where the release and the voice are in what is to sound. The burst
       may have begun at the end of the stop's own stretch, and then that
       is where the time is counted from. */
    if (release_from >= 0 || release_voiced >= 0) {
        int rf = -1, rv = -1;

        for (k = n_on; k < n_on + n_own && k < n_on + 80; k++) {
            const int32_t *f = out + (size_t)k * P_COUNT;

            if (rf < 0 && (f[P_AF] > 0 || f[P_AH] > 0))
                rf = k;
            if (f[P_AV] > 0) {
                rv = k;
                break;
            }
        }
        if (rf < 0)
            rf = rv >= 0 ? rv : n_on;
        release_from = rf;
        release_voiced = rv;
        released = a->sounded + release_from * step;
        if (a->release_at >= 0 && a->release_at <= a->sounded
            && a->sounded - a->release_at <= 30)
            released = a->release_at;
    }

    pitch_plan(a, ph, a->sounded + own_from, own_to - own_from);

    /* ---- each frame ---- */

    {
        double reach_ms = def != 0 && def->reach != UNSET ? def->reach
                                                          : prof->reach;
        double own_ms = own_to - own_from;
        double t = 0;
        double lf_from[4], lf_to[4], lf_end[4], lb_from[4], lb_to[4];
        int any_formant = 0;
        double vowel_av = a->vowel_av > 0 ? a->vowel_av : 50;
        double tap_every = 0;
        const Def *prev = before != 0 ? def_of(a, before) : 0;
        int shut_at = -1;

        if (reach_ms > own_ms * 0.6)
            reach_ms = own_ms * 0.6;
        if (reach_ms < 1)
            reach_ms = 1;

        for (i = 0; i < 4; i++) {
            lf_from[i] = prev != 0
                ? ratio_log(prev->g[i] != UNSET ? prev->g[i] : prev->f[i])
                : 0;
            lf_to[i] = def != 0 ? ratio_log(def->f[i]) : 0;
            lf_end[i] = def != 0 && def->g[i] != UNSET ? ratio_log(def->g[i])
                                                       : lf_to[i];
            lb_from[i] = prev != 0 ? ratio_log(prev->b[i]) : 0;
            lb_to[i] = def != 0 ? ratio_log(def->b[i]) : 0;
            if (lf_from[i] != 0 || lf_to[i] != 0 || lf_end[i] != 0
                || lb_from[i] != 0 || lb_to[i] != 0)
                any_formant = 1;
        }
        /* A pause has no sound to be anything: what came before lets go. */
        if (ph->pause)
            for (i = 0; i < 4; i++) {
                lf_to[i] = lf_end[i] = 0;
                lb_to[i] = 0;
            }

        if (def != 0 && def->tap > 0 && own_ms > 0)
            tap_every = own_ms / (def->tap + 1);

        if (def != 0 && def->pre > 0 && is_stop(a, ph)) {
            for (k = n_on; k < n_on + n_own; k++) {
                const int32_t *f = out + (size_t)k * P_COUNT;

                if (is_silent(f) || f[P_AV] < 30) {
                    shut_at = k;
                    break;
                }
            }
        }

        for (k = 0; k < n_out && ok; k++) {
            int32_t *f = out + (size_t)k * P_COUNT;
            int dt = f[P_STEP];
            double at = a->sounded + t;     /* ms into what has sounded */
            double into = t - own_from;     /* ms into the phone's own */
            int own = k >= n_on && k < n_on + n_own;

            /* formants and bandwidths */
            if (any_formant && own) {
                double w = smooth(into / reach_ms);
                double along = own_ms > reach_ms
                    ? (into - reach_ms) / (own_ms - reach_ms) : 1.0;

                if (def != 0 && def->glide)
                    /* A diphthong holds where it starts for a while and
                       arrives before it ends. */
                    along = own_ms > 0
                        ? (into / own_ms - 0.25) / 0.6 : 1.0;
                if (along < 0)
                    along = 0;
                if (along > 1)
                    along = 1;
                if (def != 0 && def->glide)
                    along = smooth(along);
                for (i = 0; i < 4; i++) {
                    double here = lf_to[i] + (lf_end[i] - lf_to[i]) * along;
                    double l = lf_from[i] + (here - lf_from[i]) * w;
                    double lb = lb_from[i] + (lb_to[i] - lb_from[i]) * w;

                    if (l != 0)
                        f[FORMANT[i]] =
                            (int32_t)(f[FORMANT[i]] * exp(l) + 0.5);
                    if (lb != 0)
                        f[BANDWIDTH[i]] =
                            (int32_t)(f[BANDWIDTH[i]] * exp(lb) + 0.5);
                }
                /* Formants keep their order and their distance. */
                if (f[P_F2] < f[P_F1] + 200)
                    f[P_F2] = f[P_F1] + 200;
                if (f[P_F3] < f[P_F2] + 200)
                    f[P_F3] = f[P_F2] + 200;
                if (f[P_F4] < f[P_F3] + 200)
                    f[P_F4] = f[P_F3] + 200;
            }

            /* ---- the release of the stop before ---- */
            if (was != 0 && released >= 0 && own && before != 0
                && is_stop(a, before)) {
                int since = (int)at - released;
                int voiced_at = release_voiced >= 0
                    ? a->sounded + release_voiced * step - released : 9999;

                if (since >= 0 && since < 25 && f[P_AF] > 0) {
                    if (was->burst != 0)
                        f[P_AF] = clamp(f[P_AF] + was->burst, 0, 80);
                    if (was->noburst)
                        f[P_AF] = 0;
                }
                if (since >= 0 && since < 30 && f[P_AF] > 0) {
                    for (i = 0; i < 6; i++)
                        if (was->amp[i] != UNSET)
                            f[NOISE[i]] = clamp(was->amp[i], 0, 80);
                }

                if (was->vot != UNSET && since >= 0) {
                    if (since >= was->vot - step && since < voiced_at) {
                        /* The voice begins here rather than where the
                           module had it: a frame before the moment, so
                           that it is up by then. */
                        double up = smooth((since - was->vot + 2 * step)
                                           / 15.0);

                        f[P_AV] = (int32_t)(vowel_av * up + 0.5);
                        f[P_AH] = (int32_t)(f[P_AH] * (1.0 - up));
                        f[P_AF] = (int32_t)(f[P_AF] * (1.0 - up));
                    } else if (since < was->vot - step) {
                        /* And not before. */
                        if (since >= 10 || f[P_AV] > 0) {
                            int asp = was->asp != UNSET ? was->asp : 45;

                            if (since >= 10 && f[P_AH] < asp
                                && was->vot > 25 && was->ej == 0)
                                f[P_AH] = asp;
                            f[P_AV] = 0;
                        }
                    }
                }

                if (was->ej > 0 && since >= 10 && since < 10 + was->ej) {
                    /* An ejective: the burst, then nothing while the
                       glottis is shut, then the vowel at once. */
                    f[P_AV] = 0;
                    f[P_AH] = 0;
                    f[P_AF] = 0;
                }

                if (was->brth > 0 && since >= 0 && since < was->brth) {
                    /* Breathy voice: the voice and the breath together. */
                    double w = 1.0 - smooth((double)since / was->brth);

                    if (f[P_AV] == 0 && since >= 5)
                        f[P_AV] = (int32_t)(vowel_av - 4);
                    if (f[P_AV] > 0) {
                        f[P_AH] = clamp((int)(f[P_AH]
                            + (48 - f[P_AH]) * w), 0, 70);
                        f[P_OQ] = clamp((int)(f[P_OQ] + 30 * w), 10, 99);
                        f[P_TL] = clamp((int)(f[P_TL] + 12 * w), 0, 41);
                        f[P_AF] = 0;
                    }
                }

                if (was->f0 != 0 && release_voiced >= 0
                    && k >= release_voiced && !prof->own) {
                    int since_voice = (k - release_voiced) * step;

                    if (since_voice < 80) {
                        double st = was->f0 / 10.0
                            * (1.0 - smooth(since_voice / 80.0));

                        f[P_F0] = (int32_t)(f[P_F0] * pow(2.0, st / 12.0)
                                            + 0.5);
                    }
                }
            }

            /* ---- the sound itself ---- */
            if (def != 0 && own) {
                double w = smooth(into / reach_ms);

                if (def->voi == 1) {
                    if (is_stop(a, ph) && f[P_AF] == 0 && f[P_AH] == 0
                        && f[P_AV] < def->bar) {
                        /* a closure: the voice goes on behind it, and is
                           heard through the walls of the mouth, which let
                           through little but what is low */
                        f[P_AV] = def->bar;
                        f[P_TL] = 24;
                        if (f[P_F1] > 250)
                            f[P_F1] = 250;
                    } else if (f[P_AV] == 0 && f[P_AH] == 0) {
                        if (f[P_AF] == 0) {
                            f[P_AV] = 22;
                            f[P_TL] = 35;
                        } else {
                            f[P_AV] = 38;
                            f[P_TL] = 35;
                            f[P_AF] = clamp(f[P_AF] - 4, 0, 80);
                        }
                    }
                } else if (def->voi == 0) {
                    if (f[P_AV] > 0 && (f[P_AF] > 0 || f[P_AV] < 40)) {
                        f[P_AV] = 0;
                        f[P_TL] = 0;
                    }
                }
                if (def->whisper != UNSET && f[P_AV] > 0) {
                    /* The mouth as it was, the voice taken out: at once
                       after a silence or a sound without voice, and
                       within a few milliseconds after one with it. */
                    double q = smooth(into / 15.0);

                    if (before == 0 || before->pause
                        || (prev != 0 && (prev->whisper != UNSET
                                          || prev->voi == 0)))
                        q = 1.0;
                    f[P_AH] = clamp((int)(f[P_AH]
                        + (def->whisper - f[P_AH]) * q), 0, 70);
                    f[P_AV] = (int32_t)(f[P_AV] * (1.0 - q));
                }
                if (def->impl && f[P_AV] > 0 && f[P_AV] < 40) {
                    /* An implosive swells towards its release. */
                    double x = own_ms > 0 ? into / own_ms : 0;

                    f[P_AV] = (int32_t)(f[P_AV] + 14 * x);
                    f[P_TL] = clamp((int)(f[P_TL] - 15 * x), 0, 41);
                }
                if (def->av != 0 && f[P_AV] > 0)
                    f[P_AV] = clamp((int)(f[P_AV] + def->av * w), 0, 80);
                if (def->ah != 0 && f[P_AH] > 0)
                    f[P_AH] = clamp(f[P_AH] + def->ah, 0, 80);
                if (def->af != 0 && f[P_AF] > 0)
                    f[P_AF] = clamp(f[P_AF] + def->af, 0, 80);
                if (def->fric > 0) {
                    double x = w;

                    if (own_ms - into < 15)
                        x *= (own_ms - into) / 15.0;
                    if (f[P_AF] < def->fric * x) {
                        f[P_AF] = (int32_t)(def->fric * x);
                        for (i = 0; i < 6; i++)
                            if (def->amp[i] != UNSET)
                                f[NOISE[i]] = def->amp[i];
                    }
                }
                if (f[P_AF] > 0 && !is_stop(a, ph)) {
                    for (i = 0; i < 6; i++)
                        if (def->amp[i] != UNSET) {
                            int has = f[NOISE[i]];

                            f[NOISE[i]] = clamp((int)(has
                                + (def->amp[i] - has) * w + 0.5), 0, 80);
                        }
                }
                if (def->nas > 0 && f[P_AV] > 0) {
                    /* The nose opened: its zero moves up and away from its
                       pole, as far as the modules move it for the nasal
                       vowels they have of their own (100 to 200 Hz), and
                       the first formant is damped. */
                    double x = w * def->nas / 100.0;

                    f[P_FNZ] = (int32_t)(f[P_FNZ]
                        + (f[P_FNP] + 170 - f[P_FNZ]) * x);
                    f[P_B1] = (int32_t)(f[P_B1] * (1.0 + 0.5 * x));
                }
                if (def->oq != UNSET && f[P_AV] > 0)
                    f[P_OQ] = clamp((int)(f[P_OQ]
                        + (def->oq - f[P_OQ]) * w), 10, 99);
                if (def->tl != UNSET && f[P_AV] > 0)
                    f[P_TL] = clamp((int)(f[P_TL]
                        + (def->tl - f[P_TL]) * w), 0, 41);
                if (def->creak > 0 && f[P_AV] > 0) {
                    f[P_OQ] = clamp(f[P_OQ] - def->creak * 30 / 100, 10, 99);
                    f[P_DI] = clamp(def->creak / 4, 0, 40);
                }
                if (tap_every > 0) {
                    /* A tap is the tongue shutting the mouth for a moment,
                       and a trill is that several times. */
                    int n;

                    for (n = 1; n <= def->tap; n++) {
                        double centre = n * tap_every;
                        double away = fabs(into + dt / 2.0 - centre);

                        if (away < def->tapms / 2.0 + 5) {
                            double x = away < def->tapms / 2.0 ? 1.0
                                : 1.0 - (away - def->tapms / 2.0) / 5.0;

                            f[P_AV] = (int32_t)(f[P_AV] * (1.0 - 0.45 * x));
                            f[P_AH] = (int32_t)(f[P_AH] * (1.0 - 0.8 * x));
                            f[P_F1] = (int32_t)(f[P_F1]
                                + (300 - f[P_F1]) * 0.6 * x);
                            f[P_TL] = clamp((int)(f[P_TL] + 20 * x), 0, 41);
                        }
                    }
                }
                if (shut_at > n_on && k < shut_at
                    && (shut_at - k) * step <= def->pre && f[P_AV] > 0) {
                    /* Preaspiration: the voice of the vowel before gives
                       out early, and breath is heard until the closure. */
                    f[P_AH] = clamp(f[P_AV] - 2, 0, 60);
                    f[P_AV] = 0;
                }
            }

            /* The vowel after a sound made here in silence begins hard. */
            if (on != 0 && on->hush && own && into < 30 && f[P_AV] > 0) {
                f[P_OQ] = clamp(f[P_OQ] - 14, 10, 99);
                if (into < 10)
                    f[P_DI] = clamp(f[P_DI] + 10, 0, 40);
            }
            if (off != 0 && off->hush && own && own_ms - into < 30
                && f[P_AV] > 0) {
                f[P_OQ] = clamp(f[P_OQ] - 14, 10, 99);
                f[P_DI] = clamp(f[P_DI] + 10, 0, 40);
            }

            /* a tone's own voice */
            if (a->creak > 0 && f[P_AV] > 0 && at >= a->creak_from
                && at <= a->creak_to) {
                f[P_OQ] = clamp(f[P_OQ] - a->creak * 30 / 100, 10, 99);
                f[P_DI] = clamp(a->creak / 4, 0, 40);
                f[P_TL] = clamp(f[P_TL] + a->creak / 20, 0, 41);
            }
            if (a->breath > 0 && f[P_AV] > 0 && at >= a->breath_from
                && at <= a->breath_to) {
                f[P_OQ] = clamp(f[P_OQ] + a->breath * 30 / 100, 10, 99);
                f[P_AH] = clamp(f[P_AH] + a->breath * 12 / 100, 0, 70);
                f[P_TL] = clamp(f[P_TL] + a->breath * 10 / 100, 0, 41);
            }
            if (a->stop_for > 0 && at >= a->stop_at) {
                double w = smooth((at - a->stop_at) / 15.0);

                f[P_AV] = (int32_t)(f[P_AV] * (1.0 - w));
                f[P_AH] = (int32_t)(f[P_AH] * (1.0 - w));
            }

            if (f[P_AV] >= 40 && is_vowel(ph) && own)
                a->vowel_av = f[P_AV];

            if (prof->own)
                f[P_F0] = pitch_at(a, at, dt, f);
            else
                f[P_F0] = pitch_of_module(a, f);

            t += dt;
            if (!emit(context, f))
                ok = 0;
        }
    }

    /* Where a stop lets go, if it does so before its stretch is over. */
    a->release_at = -1;
    if (is_stop(a, ph) && ph->whole) {
        int shut = 0;

        for (k = n_on; k < n_on + n_own; k++) {
            const int32_t *f = out + (size_t)k * P_COUNT;

            if (f[P_AF] == 0 && f[P_AH] == 0 && f[P_AV] < 30)
                shut = 1;
            else if (shut && f[P_AF] > 0) {
                a->release_at = a->sounded + k * step;
                break;
            }
        }
    }

    a->sounded += length_out;
    reach(a, to, a->sounded);

    if (trace() != 0) {
        fprintf(trace(),
                "%s\t%d\t%d\t%d\t%d\t%s\t%s\t%s\t%d\t%d\t%s\t%s\t"
                "%d.%d.%d.%d.%d\n",
                ph->name[0] ? ph->name : "-", (int)ph->from, (int)to,
                a->sounded - length_out, a->sounded,
                ph->known ? ph->as.name : "-",
                def != 0 ? def->id : "-",
                ph->known && ph->as.tone >= 0 && ph->as.tone < a->n_tones
                    ? a->tones[ph->as.tone].id : "-",
                ph->known ? ph->as.stress : -1,
                ph->known ? ph->as.flags : -1,
                on != 0 ? on->id : "-", off != 0 ? off->id : "-",
                /* what the module's own table says the phone is: its
                   class, voicing, sonority, manner and place */
                ph->record[0], ph->record[1], ph->record[2], ph->record[3],
                ph->record[4]);
        fflush(trace());
    }

    a->before = *ph;
    a->have_before = 1;

    free(rate);
    return ok;
}

/* ---- the frames as they arrive ------------------------------------------ */

/* The rules name a phone and ask for its stretch, from one moment of the
   arrays to another, and the frames that come of asking are not that
   stretch. They are as much of the arrays as has been written by then, which
   runs a little behind: a stretch's last frames arrive with the next one's
   first, and sometimes a whole stretch arrives with the one after it. That
   is measured, and it is why nothing here is done to a frame as it arrives.

   So the phones are kept in the order they were asked for, each with the
   moments it was asked from and to, and the frames are kept with the moment
   each belongs to; and a phone's stretch is worked on when every frame of it
   has come. What that costs is that the sound is a phone behind the engine,
   which evv_accent_held answers for whoever is timing a mark. */

static void held_drop(Accent *a, int n)
{
    if (n >= a->n_held) {
        a->n_held = 0;
        return;
    }
    memmove(a->held, a->held + (size_t)n * P_COUNT,
            (size_t)(a->n_held - n) * P_COUNT * sizeof(int32_t));
    memmove(a->held_at, a->held_at + n,
            (size_t)(a->n_held - n) * sizeof(int32_t));
    a->n_held -= n;
}

static int held_add(Accent *a, const int32_t *frame, int32_t at)
{
    if (a->n_held == a->held_room) {
        int room = a->held_room ? a->held_room * 2 : 128;
        int32_t *h = (int32_t *)realloc(a->held,
                         (size_t)room * P_COUNT * sizeof(int32_t));
        int32_t *t;

        if (h == 0)
            return 0;
        a->held = h;
        t = (int32_t *)realloc(a->held_at, (size_t)room * sizeof(int32_t));
        if (t == 0)
            return 0;
        a->held_at = t;
        a->held_room = room;
    }
    memcpy(a->held + (size_t)a->n_held * P_COUNT, frame,
           P_COUNT * sizeof(int32_t));
    a->held_at[a->n_held] = at;
    a->n_held++;
    return 1;
}

static int work_one(Accent *a, int32_t step, int all, evv_accent_emit emit,
                    void *context)
{
    Phone *p = &a->line_up[0];
    int n = 0;

    /* The last phone there is takes every frame there is, if no more are
       coming; otherwise a phone takes what is its own. */
    while (n < a->n_held
           && (a->held_at[n] < p->to || (all && a->n_line_up == 1)))
        n++;
    if (!stretch(a, p, step, a->held, n, emit, context))
        return 0;
    held_drop(a, n);
    memmove(a->line_up, a->line_up + 1,
            (size_t)(a->n_line_up - 1) * sizeof(Phone));
    a->n_line_up--;
    return 1;
}

/* Every phone whose frames have all come, or every phone there is. */
static int work(Accent *a, int32_t step, int all, evv_accent_emit emit,
                void *context)
{
    while (a->n_line_up > 0) {
        if (!all) {
            int32_t end = a->n_held > 0
                ? a->held_at[a->n_held - 1] + step : -1;

            if (a->n_held == 0 || end < a->line_up[0].to)
                break;
        }
        if (!work_one(a, step, all, emit, context))
            return 0;
    }
    if (all && a->n_held > 0) {
        /* Frames nobody asked for by name: they are said as they are. */
        int i;

        for (i = 0; i < a->n_held; i++)
            if (!emit(context, a->held + (size_t)i * P_COUNT))
                return 0;
        a->sounded += a->n_held * step;
        a->n_held = 0;
    }
    return 1;
}

int32_t evv_accent_held(void *machine)
{
    Accent *a = find(machine, 0);
    int i, ms = 0;

    if (a == 0 || !a->on)
        return 0;
    for (i = 0; i < a->n_held; i++)
        ms += a->held[(size_t)i * P_COUNT + P_STEP];
    return ms;
}

int evv_accent_run(void *machine, int32_t from, int32_t to, int32_t first,
                   int32_t step, const int32_t *frames, int count,
                   int last, evv_accent_emit emit, void *context)
{
    Accent *a = find(machine, 0);
    int i;

    if (a == 0 || !a->on) {
        for (i = 0; i < count; i++)
            if (!emit(context, frames + (size_t)i * P_COUNT))
                return 0;
        return 1;
    }

    if (a->cleared) {
        /* What was waiting to be said is not going to be. */
        queue_reset(a);
        a->cleared = 0;
    }

    if (from == 0 && to > 0 && (a->last_to > 0 || a->n_line_up > 0)) {
        /* The arrays start again. Whatever the last run left is said first,
           and what has sounded is counted from here. */
        if (!work(a, step, 1, emit, context))
            return 0;
        a->last_to = 0;
    }
    if (a->last_to == 0 && to > from) {
        a->sounded = 0;
        a->n_reached = 0;
        a->started = 0;
        a->in_pause = 1;
        a->n_line = 0;
        a->command = level_st(a, a->profile.level);
        a->mid_hz = 0;
        a->have_before = 0;
        a->release_at = -1;
        reach(a, 0, 0);
    }

    if (to > from) {
        Phone *p;

        if (a->n_line_up == LINE_UP) {
            /* More phones asked for than frames have come for: the oldest
               is worked on with what there is. */
            if (!work_one(a, step, 0, emit, context))
                return 0;
        }
        p = &a->line_up[a->n_line_up++];
        if (a->have_now) {
            *p = a->now;
        } else {
            memset(p, 0, sizeof *p);
            p->whole = 1;
            p->first = 1;
            p->on = -1;
            p->off = -1;
            p->pause = count > 0 ? is_silent(frames) : 0;
        }
        p->from = from;
        p->to = to;
        a->have_now = 0;
        a->last_to = to;
    }

    for (i = 0; i < count; i++)
        if (!held_add(a, frames + (size_t)i * P_COUNT, first + i * step))
            return 0;

    return work(a, step, last, emit, context);
}
