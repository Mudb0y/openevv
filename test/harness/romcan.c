/* A romanizer with no language in it, replaying IBM's answers.
 *
 * What this is for. Japanese is spoken through a romanizer, and the romanizer
 * is a hundred and sixty thousand bytes of x86 that has to be transcribed
 * before our engine can say a word of it. Everything below the romanizer --
 * the rules, the dictionary tables, the machine, the synthesiser -- is
 * supposed to be right already, and nothing had ever tested that, because
 * there was no way to get Japanese text as far as it.
 *
 * So this stands where the romanizer will stand and answers from a recording.
 * `reference/romtap.c' writes down every call IBM's romanizer manager makes
 * and every answer it gets, for a given input; this reads that file back and
 * answers the same things in the same order. If our engine then produces the
 * same samples IBM's did, everything below the seam is proved, and what is
 * left of Japanese really is the romanizer and nothing else.
 *
 * It is also the check that our manager and IBM's hold the same conversation.
 * Every recorded call is compared against the call that actually arrives --
 * the text, the lengths, the flags, the parameter numbers -- and a difference
 * is a failure rather than a note, because a manager that asks different
 * questions would make the answers meaningless. The parameter reads are the
 * one thing the recording cannot show directly: the manager asks the romanizer
 * for a parameter before setting it and flushes if the two differ, so what
 * getParam has to answer is read off the recording's own flushes.
 *
 * usage: EVV_ROMCAN=<dump> romcan <text-file> <out.wav>
 *
 * The text file is the same one the reference was given, byte for byte, and
 * the dump is what it wrote. Both sides must have been asked for the same
 * language: EVV_LANGUAGE=0x80000.
 */

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#else
#include <unistd.h>
#endif

#include "evv_abi.h"
#include "eci_rom.h"
#include "jprom.h"

enum { FRAME = 1024 };

typedef struct OldInst OldInst;

enum ECIMessage { eciWaveformBuffer, eciPhonemeBuffer, eciIndexReply };
enum ECICallbackReturn { eciDataNotProcessed, eciDataProcessed, eciDataAbort };

OldInst *STDCALL eo_new(void);
OldInst *STDCALL eo_newEx(int32_t language);
int      STDCALL es_delete(OldInst *h);
int      STDCALL et_addText(OldInst *h, const char *text);
int      STDCALL et_insertIndex(OldInst *h, int32_t index);
int      STDCALL et_synthesize(OldInst *h);
int      STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf);
void     STDCALL eo_registerCallback(OldInst *h, void *cb, void *data);
void     STDCALL eo_synchronizeSynth(OldInst *h);
int      STDCALL eo_speaking(OldInst *h);
int      STDCALL eo_getAvailableLanguages(uint32_t *out, int *count);
void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

/* ---- the recording ---------------------------------------------------- */

enum Kind { K_ADDTEXT, K_ADDPARAM, K_INDEX, K_PROCESS, K_REMAIN, K_SETPARAM,
            K_STOP, K_RESUME, K_KINDS };

static const char *const KIND_NAME[] = {
    "ADDTEXT", "ADDPARAM", "INDEX", "PROCESS", "REMAIN", "SETPARAM",
    "STOP", "RESUME"
};

typedef struct Event {
    int      kind;
    int32_t  len, flag, rc, anno, which, value;
    char    *text;      /* the bytes, nought-terminated, or null */
    int32_t  text_len;
} Event;

static Event *events;
static int    nevents, cursor;
static int    faults;

static void fault(const char *what, const char *saw)
{
    fprintf(stderr, "romcan: %s (%s) at event %d of %d\n", what, saw, cursor,
            nevents);
    faults++;
}

/* One hex field. A single dot means an empty string and a dash means none at
   all, which is how romtap.c writes those two apart. */
static char *unhex(const char *s, int32_t *len)
{
    size_t n = strlen(s);
    char  *out;
    size_t i;

    if (n == 0 || s[0] == '-') {
        *len = 0;
        return NULL;
    }
    if (s[0] == '.') {
        *len = 0;
        out = malloc(1);
        out[0] = 0;
        return out;
    }
    out = malloc(n / 2 + 1);
    for (i = 0; i + 1 < n; i += 2) {
        unsigned v;

        if (sscanf(s + i, "%2x", &v) != 1)
            break;
        out[i / 2] = (char)v;
    }
    out[i / 2] = 0;
    *len = (int32_t)(i / 2);
    return out;
}

static int32_t field(const char *line, const char *name, int32_t missing)
{
    const char *at = strstr(line, name);

    if (at == NULL)
        return missing;
    return (int32_t)strtol(at + strlen(name), NULL, 10);
}

static char *textfield(const char *line, int32_t *len)
{
    const char *at = strstr(line, "text=");

    if (at == NULL)
        at = strstr(line, "out=");
    *len = 0;
    if (at == NULL)
        return NULL;
    at = strchr(at, '=') + 1;
    return unhex(at, len);
}

static void load(const char *path)
{
    FILE *f = fopen(path, "r");
    char  line[65536];
    int   room = 0;

    if (f == NULL) {
        fprintf(stderr, "romcan: cannot read %s\n", path);
        exit(1);
    }
    while (fgets(line, sizeof line, f)) {
        Event e;
        int   k;

        memset(&e, 0, sizeof e);
        for (k = 0; k < K_KINDS; k++)
            if (strncmp(line, KIND_NAME[k], strlen(KIND_NAME[k])) == 0)
                break;
        if (k == K_KINDS)
            continue;
        e.kind = k;
        e.len = field(line, "len=", 0);
        e.flag = field(line, "flag=", 0);
        e.rc = field(line, "rc=", 0);
        e.anno = field(line, "anno=", 0);
        e.which = field(line, "which=", 0);
        e.value = field(line, "value=", 0);
        e.text = textfield(line, &e.text_len);

        if (nevents == room) {
            room = room ? room * 2 : 256;
            events = realloc(events, (size_t)room * sizeof *events);
        }
        events[nevents++] = e;
    }
    fclose(f);
}

/* Where the replay begins.
 *
 * The recording is taken at the manager's own methods, which are called
 * whether or not there is a romanizer to pass anything to; this stands one
 * step further in, where only the calls that reach a romanizer arrive. Those
 * are the same calls from the moment the romanizer exists, and it comes into
 * existence when the language is set -- parameter 2, with a language in it.
 * Everything the manager was asked before that never reached a romanizer on
 * IBM's side either, so the replay starts there. */
static void start(void)
{
    int i;

    for (i = 0; i < nevents; i++)
        if (events[i].kind == K_SETPARAM && events[i].which == 2
            && events[i].value != 0) {
            cursor = i;
            return;
        }
    fprintf(stderr, "romcan: the recording never sets a language\n");
    faults++;
}

/* The next event, or null at the end. */
static Event *next(int kind)
{
    Event *e;

    if (cursor >= nevents) {
        fault("recording ran out", KIND_NAME[kind]);
        return NULL;
    }
    e = &events[cursor++];
    if (e->kind != kind) {
        fprintf(stderr, "romcan: expected %s, recording says %s, at event %d\n",
                KIND_NAME[kind], KIND_NAME[e->kind], cursor - 1);
        faults++;
    }
    return e;
}

/* Our own side of the recording, in the same format, so that the two can be
   put side by side when they differ. EVV_ROMCAN_TRACE names the file. */
static FILE *trace;
static int   trace_looked;

static FILE *trace_open(void)
{
    if (!trace_looked) {
        const char *path = getenv("EVV_ROMCAN_TRACE");

        trace_looked = 1;
        trace = path ? fopen(path, "w") : NULL;
    }
    return trace;
}

static void trace_hex(FILE *f, const char *s, int32_t n)
{
    int32_t i;

    if (s == NULL) {
        fputs("-", f);
        return;
    }
    if (n < 0)
        for (n = 0; s[n]; n++)
            ;
    if (n == 0) {
        fputs(".", f);
        return;
    }
    for (i = 0; i < n; i++)
        fprintf(f, "%02x", (unsigned char)s[i]);
}

/* ---- the romanizer --------------------------------------------------- */

typedef struct Can {
    EvvRom       base;
    RomInstParam param;
    char         held[65536];
} Can;

static Can can;

/* Whether the parameter calls are answered from the recording or by the
   romanizer's own RomInstParam. Canned is the default and is what proves the
   engine below the seam without a line of romanizer code; real is what proves
   rom/jajp/rominstparam.c, because the manager reads a parameter before it
   writes one and flushes when the two differ, so a wrong answer moves every
   flush after it and the recording stops lining up. */
static int real_params;

static void can_release(EvvRom *r)
{
    (void)r;
}

static int32_t can_addText(EvvRom *r, const char *text, int32_t len,
                           int32_t flag)
{
    Event *e = next(K_ADDTEXT);
    FILE  *f = trace_open();

    (void)r;
    if (f) {
        fprintf(f, "ADDTEXT len=%d flag=%d rc=%d text=", (int)len, (int)flag,
                e ? (int)e->rc : 0);
        trace_hex(f, text, len);
        fputc('\n', f);
        fflush(f);
    }
    if (e == NULL)
        return 0;
    if (e->len != len || e->flag != flag)
        fault("addText length or flag differs", "");
    if (e->text == NULL || len != e->text_len
        || memcmp(e->text, text, (size_t)len) != 0)
        fault("addText text differs", "");
    return e->rc;
}

static int32_t can_addParam(EvvRom *r, const char *text, int32_t len)
{
    Event *e = next(K_ADDPARAM);
    FILE  *f = trace_open();

    (void)r;
    if (f) {
        fprintf(f, "ADDPARAM len=%d rc=%d text=", (int)len,
                e ? (int)e->rc : 0);
        trace_hex(f, text, len);
        fputc('\n', f);
        fflush(f);
    }
    if (e == NULL)
        return 0;
    if (e->len != len)
        fault("addParam length differs", "");
    if (e->text == NULL || len != e->text_len
        || memcmp(e->text, text, (size_t)len) != 0)
        fault("addParam text differs", "");
    return e->rc;
}

static int32_t can_insertIndex(EvvRom *r)
{
    Event *e = next(K_INDEX);
    FILE  *f = trace_open();

    (void)r;
    if (f) {
        fprintf(f, "INDEX rc=%d\n", e ? (int)e->rc : 0);
        fflush(f);
    }
    return e ? e->rc : 0;
}

/* The manager asks for a sentence with the annotation flag it was given.
   processRemaining is the same call with that flag set, which is why the
   recording has two names for it. */
static int32_t can_processSentence(EvvRom *r, char **out, int32_t annotated)
{
    Can   *c = (Can *)r;
    Event *e;
    FILE  *f = trace_open();

    if (cursor >= nevents) {
        fault("recording ran out", "PROCESS");
        return 0;
    }
    e = &events[cursor];
    if (e->kind != K_PROCESS && e->kind != K_REMAIN) {
        fprintf(stderr, "romcan: expected PROCESS or REMAIN, recording says "
                "%s, at event %d\n", KIND_NAME[e->kind], cursor);
        faults++;
        return 0;
    }
    cursor++;
    if (e->kind == K_REMAIN && annotated != 1)
        fault("processRemaining arrived without the annotation flag", "");
    if (e->kind == K_PROCESS && e->anno != annotated)
        fault("processSentence annotation flag differs", "");

    if (e->rc > 0 && e->text != NULL
        && (size_t)e->text_len < sizeof c->held) {
        memcpy(c->held, e->text, (size_t)e->text_len);
        c->held[e->text_len] = 0;
        *out = c->held;
    }
    if (f) {
        fprintf(f, "%s ", annotated == 1 ? "REMAIN" : "PROCESS");
        if (annotated != 1)
            fprintf(f, "anno=%d ", (int)annotated);
        fprintf(f, "rc=%d out=", (int)(e->rc > 0 ? e->rc : 0));
        trace_hex(f, e->rc > 0 ? *out : NULL, e->rc > 0 ? e->rc : 0);
        fputc('\n', f);
        fflush(f);
    }
    if (e->rc <= 0 || e->text == NULL)
        return 0;
    if ((size_t)e->text_len >= sizeof c->held) {
        fault("recorded sentence too long to hold", "");
        return 0;
    }
    return 2;
}

static int32_t can_stop(EvvRom *r)
{
    Event *e = next(K_STOP);
    FILE  *f = trace_open();

    (void)r;
    if (f) {
        fprintf(f, "STOP rc=%d\n", e ? (int)e->rc : 1);
        fflush(f);
    }
    return e ? e->rc : 1;
}

static int32_t can_resume(EvvRom *r)
{
    Event *e = next(K_RESUME);
    FILE  *f = trace_open();

    (void)r;
    if (f) {
        fprintf(f, "RESUME rc=%d\n", e ? (int)e->rc : 1);
        fflush(f);
    }
    return e ? e->rc : 1;
}

static int32_t can_UCS2ToMBCS(EvvRom *r, const uint16_t *in, char **out,
                              int32_t n)
{
    (void)r; (void)in; (void)out; (void)n;
    fault("UCS2ToMBCS was not in the recording", "");
    return 0;
}

static int32_t can_setParam(EvvRom *r, int32_t which, int32_t value)
{
    Event *e = next(K_SETPARAM);
    FILE  *f = trace_open();

    (void)r;
    if (f) {
        fprintf(f, "SETPARAM which=%d value=%d rc=%d\n", (int)which,
                (int)value, e ? (int)e->rc : 0);
        fflush(f);
    }
    if (e == NULL)
        return 0;
    if (e->which != which || e->value != value) {
        fprintf(stderr, "romcan: setParam(%d, %d), recording says (%d, %d), "
                "at event %d\n", (int)which, (int)value, (int)e->which,
                (int)e->value, cursor - 1);
        faults++;
    }
    if (real_params) {
        int32_t got = rp_setParam(&can.param, which, value);

        if (got != e->rc) {
            fprintf(stderr, "romcan: setParam(%d, %d) answered %d, recording "
                    "says %d, at event %d\n", (int)which, (int)value,
                    (int)got, (int)e->rc, cursor - 1);
            faults++;
        }
        return got;
    }
    return e->rc;
}

/* What the manager reads before it writes, which decides whether it flushes
   what the romanizer is holding first. The recording does not carry it, so it
   is read off the flushes: look ahead to the setParam this read belongs to,
   and answer its value when nothing stands between, or something else when a
   flush does. */
static int32_t can_getParam(EvvRom *r, int32_t which)
{
    int i;

    (void)r;
    if (real_params)
        return rp_getParam(&can.param, which);

    for (i = cursor; i < nevents; i++) {
        if (events[i].kind == K_REMAIN || events[i].kind == K_PROCESS)
            continue;
        if (events[i].kind != K_SETPARAM) {
            fault("getParam does not sit before a setParam", "");
            return ~which;
        }
        if (events[i].which != which)
            fault("getParam is for a different parameter than the next set",
                  "");
        if (i == cursor)
            return events[i].value;
        return events[i].value + 1;
    }
    fault("getParam past the end of the recording", "");
    return ~which;
}

static void can_clearErrors(EvvRom *r)
{
    (void)r;
}

static uint32_t can_progStatus(EvvRom *r)
{
    (void)r;
    return 0;
}

static void can_errorMessage(EvvRom *r, char *out)
{
    (void)r;
    strcpy(out, "No Romanizer Error");
}

static const EvvRomOps CAN_OPS = {
    can_release,
    can_addText,
    can_insertIndex,
    can_processSentence,
    can_stop,
    can_resume,
    can_UCS2ToMBCS,
    can_setParam,
    can_getParam,
    can_clearErrors,
    can_progStatus,
    can_errorMessage,
    can_addParam,
};

static EvvRom *can_make(const char *dir)
{
    can.base.ops = &CAN_OPS;
    rp_ctor(&can.param, dir);
    return &can.base;
}

/* ---- speaking -------------------------------------------------------- */

static short frame[FRAME];
static long  said;

static enum ECICallbackReturn STDCALL on_message(OldInst *h,
                                                 enum ECIMessage msg,
                                                 long param, void *data)
{
    (void)h;
    (void)data;
    if (msg == eciWaveformBuffer)
        said += param;
    return eciDataProcessed;
}

static void nap(long ms)
{
#if defined(_WIN32)
    Sleep((DWORD)ms);
#else
    struct timespec t;

    t.tv_sec = ms / 1000;
    t.tv_nsec = (ms % 1000) * 1000000L;
    nanosleep(&t, NULL);
#endif
}

static char *slurp(const char *path)
{
    FILE *f = fopen(path, "rb");
    char *buf;
    long  n;

    if (f == NULL) {
        fprintf(stderr, "romcan: cannot read %s\n", path);
        exit(1);
    }
    fseek(f, 0, SEEK_END);
    n = ftell(f);
    fseek(f, 0, SEEK_SET);
    buf = malloc((size_t)n + 1);
    if (buf == NULL || fread(buf, 1, (size_t)n, f) != (size_t)n) {
        fprintf(stderr, "romcan: cannot read %s\n", path);
        exit(1);
    }
    fclose(f);
    buf[n] = 0;
    while (n > 0 && (buf[n - 1] == '\n' || buf[n - 1] == '\r'))
        buf[--n] = 0;
    return buf;
}

static void put32(FILE *f, uint32_t v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
    fputc((int)((v >> 16) & 0xff), f);
    fputc((int)((v >> 24) & 0xff), f);
}

static void put16(FILE *f, uint32_t v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
}

static short *kept;
static long   nkept, roomkept;

static enum ECICallbackReturn STDCALL keep(OldInst *h, enum ECIMessage msg,
                                          long param, void *data)
{
    (void)h;
    (void)data;
    if (msg == eciWaveformBuffer) {
        if (nkept + param > roomkept) {
            roomkept = (nkept + param) * 2 + FRAME;
            kept = realloc(kept, (size_t)roomkept * sizeof *kept);
        }
        memcpy(kept + nkept, frame, (size_t)param * sizeof *frame);
        nkept += param;
        said += param;
    }
    return eciDataProcessed;
}

static void write_wav(const char *path, int32_t rate)
{
    FILE    *f = fopen(path, "wb");
    uint32_t bytes = (uint32_t)nkept * 2;

    if (f == NULL) {
        fprintf(stderr, "romcan: cannot write %s\n", path);
        exit(1);
    }
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, (uint32_t)rate);
    put32(f, (uint32_t)rate * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
    fwrite(kept, 2, (size_t)nkept, f);
    fclose(f);
}

int main(int argc, char **argv)
{
    const char *dump = getenv("EVV_ROMCAN");
    const char *want = getenv("EVV_LANGUAGE");
    const char *text = argc > 1 ? argv[1] : "";
    const char *out = argc > 2 ? argv[2] : "romcan.wav";
    OldInst    *h;
    int         i;

    (void)on_message;

    if (dump == NULL || *dump == 0) {
        fprintf(stderr, "romcan: EVV_ROMCAN must name a dump from "
                "reference/romtap.c\n");
        return 1;
    }
    if (text[0] == '@')
        text = slurp(text + 1);
    else if (text[0] == 0) {
        fprintf(stderr, "usage: romcan <@text-file> <out.wav>\n");
        return 1;
    }

    {
        const char *how = getenv("EVV_ROMCAN_PARAMS");

        real_params = how != NULL && strcmp(how, "real") == 0;
    }

    setvbuf(stdout, NULL, _IONBF, 0);
    load(dump);
    start();
    printf("romcan: %d events, replaying from %d, %s parameters\n", nevents,
           cursor, real_params ? "real" : "canned");

    evv_port_start();
    evvRunStaticInitialisers();

    /* Before any instance is made: this is what a language module's own bind
       function will do once there is a romanizer to register. Japanese is
       family 8, dialect 0. */
    evv_rom_provide(8, 0, can_make);

    h = want && *want ? eo_newEx((int32_t)strtoul(want, NULL, 0)) : eo_new();
    if (h == NULL) {
        printf("romcan: the engine would not build an instance\n");
        return 1;
    }

    eo_registerCallback(h, (void *)keep, NULL);
    if (!ev_setOutputBuffer(h, FRAME, frame)) {
        printf("romcan: setOutputBuffer refused\n");
        return 1;
    }

    /* An index mark before the text, which is what reference/speak.c does:
       without it the romanizer is asked one thing fewer than IBM's was and
       the recordings do not line up. */
    if (!et_insertIndex(h, 4242))
        printf("romcan: insertIndex refused\n");

    if (!et_addText(h, text)) {
        printf("romcan: addText refused\n");
        return 1;
    }
    if (!et_synthesize(h)) {
        printf("romcan: synthesize refused\n");
        return 1;
    }
    for (i = 0; i < 6000 && eo_speaking(h); i++)
        nap(10);
    eo_synchronizeSynth(h);
    es_delete(h);

    write_wav(out, 11025);
    printf("romcan: %ld samples, %d of %d events used\n", said, cursor,
           nevents);
    if (cursor != nevents)
        printf("romcan: %d events left over\n", nevents - cursor);
    if (faults)
        printf("romcan: %d faults\n", faults);
    evv_port_finish();
    return faults != 0 || cursor != nevents;
}
