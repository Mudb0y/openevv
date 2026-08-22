/* The speak window: type something, hear it.
 *
 * This is the engine's front end on Windows, and the only front end anywhere
 * that plays what it makes rather than writing a file. Playing is forty lines
 * of waveOut on top of the buffer the engine already fills, and it asks
 * nothing of the engine at all.
 *
 * The engine is spoken to from one thread only. The window reads its own
 * controls and hands the worker the text and the numbers; the worker sets the
 * voice, gives the engine the text, and pumps it until it stops. That is why
 * nothing here locks anything: the window touches the engine before a worker
 * exists and never again while one does.
 */

#include <windows.h>
#include <commdlg.h>
#include <mmsystem.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "evv_abi.h"
#include "delta_lang.h"
#include "speak.h"

typedef struct OldInst OldInst;

enum ECIMessage { eciWaveformBuffer, eciPhonemeBuffer, eciIndexReply };
enum ECICallbackReturn { eciDataNotProcessed, eciDataProcessed, eciDataAbort };

/* The engine's parameters, and a voice's. Only the few this needs. */
enum { P_REAL_WORLD_UNITS = 8, P_LANGUAGE = 9 };

/* How many languages the window will offer. A build has one or two today;
   the API's own answer is a count and a list, so this is only the size of
   the array they are read into. */
#define LANGS_MAX 32
enum { V_GENDER, V_HEAD_SIZE, V_PITCH, V_FLUCTUATION, V_ROUGHNESS,
       V_BREATHINESS, V_SPEED, V_VOLUME, V_COUNT };

OldInst *STDCALL eo_new(void);
OldInst *STDCALL eo_newEx(int32_t language);
int      STDCALL es_delete(OldInst *h);
int      STDCALL et_addText(OldInst *h, const char *text);
int      STDCALL et_synthesize(OldInst *h);
int      STDCALL ev_setOutputBuffer(OldInst *h, int32_t n, void *buf);
int32_t  STDCALL ev_setParam(OldInst *h, int32_t which, int32_t value);
int32_t  STDCALL vc_getVoiceParam(OldInst *h, int32_t voice, int32_t which);
int      STDCALL vc_setVoiceParam(OldInst *h, int32_t voice, int32_t which,
                                  int32_t value);
int      STDCALL vc_copyVoice(OldInst *h, int32_t from, int32_t to);
void     STDCALL eo_registerCallback(OldInst *h, void *cb, void *data);
void     STDCALL eo_synchronizeSynth(OldInst *h);
int      STDCALL eo_speaking(OldInst *h);
int      STDCALL eo_stop(OldInst *h);
int      STDCALL eo_getAvailableLanguages(uint32_t *out, int *count);

void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

/* The formant voice runs at eleven thousand and twenty-five samples a second
   and nothing here changes that. */
#define RATE   11025
#define FRAME  2048
#define CHUNKS 8

static HWND     dlg;
static char    *opening;      /* what the command line put in the box */
static int      say_at_once;  /* and whether it asked for it to be spoken */
static OldInst *engine;
static uint32_t langs[LANGS_MAX];     /* what this build has in it */
static int      nlangs;
static int      lang_now;             /* which of them is being spoken */
static const char *wanted;            /* what the command line asked for */
static int      voices[9][V_COUNT];   /* the language's eight presets */

static short    frame[FRAME];         /* what the engine fills */
static HWAVEOUT wave;
static WAVEHDR  chunk_hdr[CHUNKS];
static short   *chunk_buf[CHUNKS];
static int      chunk_next;

static short   *kept;                 /* everything said, for Save WAV */
static size_t   kept_n, kept_room;

static HANDLE   worker;
static volatile int stopping;
static volatile int speaking;

#define WM_SPOKEN (WM_APP + 1)

static void status(const char *s)
{
    if (dlg != 0)
        SetDlgItemTextA(dlg, IDC_STATUS, s);
}

/* ---- the sound card -------------------------------------------------- */

static int wave_open(void)
{
    WAVEFORMATEX f;
    int i;

    if (wave != 0)
        return 1;

    memset(&f, 0, sizeof f);
    f.wFormatTag = WAVE_FORMAT_PCM;
    f.nChannels = 1;
    f.nSamplesPerSec = RATE;
    f.wBitsPerSample = 16;
    f.nBlockAlign = 2;
    f.nAvgBytesPerSec = RATE * 2;

    if (waveOutOpen(&wave, WAVE_MAPPER, &f, 0, 0, CALLBACK_NULL) != MMSYSERR_NOERROR) {
        wave = 0;
        return 0;
    }
    for (i = 0; i < CHUNKS; i++)
        if (chunk_buf[i] == 0)
            chunk_buf[i] = malloc(FRAME * sizeof *chunk_buf[i]);
    return 1;
}

/* Hand one buffer to the card, waiting for a free one. Waiting here is what
   keeps the engine to about the speed of speech, which is also what makes
   Stop take effect promptly. */
static void wave_write(const short *p, size_t n)
{
    WAVEHDR *h = &chunk_hdr[chunk_next];

    if (wave == 0 || n == 0)
        return;

    while ((h->dwFlags & WHDR_INQUEUE) != 0) {
        if (stopping)
            return;
        Sleep(5);
    }
    memcpy(chunk_buf[chunk_next], p, n * sizeof *p);
    memset(h, 0, sizeof *h);
    h->lpData = (LPSTR)chunk_buf[chunk_next];
    h->dwBufferLength = (DWORD)(n * sizeof *p);
    if (waveOutPrepareHeader(wave, h, sizeof *h) == MMSYSERR_NOERROR)
        waveOutWrite(wave, h, sizeof *h);
    chunk_next = (chunk_next + 1) % CHUNKS;
}

static int wave_busy(void)
{
    int i;

    for (i = 0; i < CHUNKS; i++)
        if ((chunk_hdr[i].dwFlags & WHDR_INQUEUE) != 0)
            return 1;
    return 0;
}

/* ---- what the engine hands back -------------------------------------- */

static void keep(const short *p, size_t n)
{
    if (kept_n + n > kept_room) {
        size_t room = (kept_n + n) * 2 + FRAME;
        short *bigger = realloc(kept, room * sizeof *bigger);

        if (bigger == 0)
            return;
        kept = bigger;
        kept_room = room;
    }
    memcpy(kept + kept_n, p, n * sizeof *p);
    kept_n += n;
}

static enum ECICallbackReturn STDCALL on_message(OldInst *h,
                                                 enum ECIMessage msg,
                                                 long param, void *data)
{
    (void)h;
    (void)data;

    if (msg == eciWaveformBuffer) {
        keep(frame, (size_t)param);
        wave_write(frame, (size_t)param);
    }
    return stopping ? eciDataAbort : eciDataProcessed;
}

/* ---- writing what was said out --------------------------------------- */

static void put32(FILE *f, unsigned long v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
    fputc((int)((v >> 16) & 0xff), f);
    fputc((int)((v >> 24) & 0xff), f);
}

static void put16(FILE *f, unsigned v)
{
    fputc((int)(v & 0xff), f);
    fputc((int)((v >> 8) & 0xff), f);
}

static int write_wav(const char *path)
{
    FILE *f = fopen(path, "wb");
    unsigned long bytes = (unsigned long)kept_n * 2;

    if (f == 0)
        return 0;
    fwrite("RIFF", 1, 4, f);
    put32(f, 36 + bytes);
    fwrite("WAVE", 1, 4, f);
    fwrite("fmt ", 1, 4, f);
    put32(f, 16);
    put16(f, 1);
    put16(f, 1);
    put32(f, RATE);
    put32(f, RATE * 2);
    put16(f, 2);
    put16(f, 16);
    fwrite("data", 1, 4, f);
    put32(f, bytes);
    fwrite(kept, 2, kept_n, f);
    fclose(f);
    return 1;
}

/* ---- speaking -------------------------------------------------------- */

struct job {
    char *text;
    int   voice, rate, pitch, volume;
};

static DWORD WINAPI speak_job(LPVOID p)
{
    struct job *j = p;

    if (j->voice > 0)
        vc_copyVoice(engine, j->voice, 0);
    if (j->rate > 0)
        vc_setVoiceParam(engine, 0, V_SPEED, j->rate);
    if (j->pitch > 0)
        vc_setVoiceParam(engine, 0, V_PITCH, j->pitch);
    if (j->volume > 0)
        vc_setVoiceParam(engine, 0, V_VOLUME, j->volume);

    kept_n = 0;
    if (et_addText(engine, j->text) && et_synthesize(engine)) {
        int i;

        for (i = 0; i < 60000 && !stopping && eo_speaking(engine); i++)
            Sleep(5);
        while (!stopping && wave_busy())
            Sleep(10);
    }
    eo_synchronizeSynth(engine);

    free(j->text);
    free(j);

    /* Posted rather than said from here: setting a control's text from another
       thread sends a message and waits for the window to take it, and the
       window may be waiting for this thread to finish. */
    PostMessage(dlg, WM_SPOKEN, (WPARAM)kept_n, (LPARAM)stopping);
    return 0;
}

static int number_in(int id, int fallback)
{
    BOOL ok = FALSE;
    UINT v = GetDlgItemInt(dlg, id, &ok, FALSE);

    return ok ? (int)v : fallback;
}

static void start_speaking(void)
{
    struct job *j;
    int         n;

    if (speaking)
        return;

    n = GetWindowTextLengthA(GetDlgItem(dlg, IDC_TEXT));
    if (n <= 0) {
        status("nothing to say");
        return;
    }

    j = calloc(1, sizeof *j);
    if (j == 0)
        return;
    j->text = malloc((size_t)n + 1);
    if (j->text == 0) {
        free(j);
        return;
    }
    GetDlgItemTextA(dlg, IDC_TEXT, j->text, n + 1);
    j->voice = (int)SendDlgItemMessage(dlg, IDC_VOICE, CB_GETCURSEL, 0, 0) + 1;
    j->rate = number_in(IDC_RATE, 0);
    j->pitch = number_in(IDC_PITCH, 0);
    j->volume = number_in(IDC_VOLUME, 0);

    if (!wave_open())
        status("no sound card; Save WAV still works");

    stopping = 0;
    speaking = 1;
    EnableWindow(GetDlgItem(dlg, IDC_SPEAK), FALSE);
    status("speaking");

    worker = CreateThread(0, 0, speak_job, j, 0, 0);
    if (worker == 0) {
        speaking = 0;
        EnableWindow(GetDlgItem(dlg, IDC_SPEAK), TRUE);
        status("could not start speaking");
        free(j->text);
        free(j);
    }
}

static void stop_speaking(void)
{
    if (!speaking)
        return;
    stopping = 1;
    eo_stop(engine);
    if (wave != 0)
        waveOutReset(wave);
}

static void save_wav(void)
{
    OPENFILENAMEA o;
    char          path[MAX_PATH] = "spoken.wav";

    if (kept_n == 0) {
        status("nothing said yet");
        return;
    }

    memset(&o, 0, sizeof o);
    o.lStructSize = sizeof o;
    o.hwndOwner = dlg;
    o.lpstrFilter = "Wave files\0*.wav\0All files\0*.*\0";
    o.lpstrFile = path;
    o.nMaxFile = sizeof path;
    o.lpstrDefExt = "wav";
    o.Flags = OFN_OVERWRITEPROMPT | OFN_PATHMUSTEXIST;

    if (!GetSaveFileNameA(&o))
        return;
    status(write_wav(path) ? "saved" : "could not write that file");
}

/* ---- the window ------------------------------------------------------ */

static void show_voice(int v)
{
    if (v < 1 || v > 8)
        return;
    SetDlgItemInt(dlg, IDC_RATE, (UINT)voices[v][V_SPEED], FALSE);
    SetDlgItemInt(dlg, IDC_PITCH, (UINT)voices[v][V_PITCH], FALSE);
    SetDlgItemInt(dlg, IDC_VOLUME, (UINT)voices[v][V_VOLUME], FALSE);
}

/* What to call one in the list. The language module says: `enus' is what
   the tree calls it, 0x10000 is IBM's number for it, and the name is the
   third thing, which is there for exactly this. A language the engine has
   but this build did not link -- which cannot happen, but the lookup can
   answer nothing -- is offered under its number. */
static const char *language_name(uint32_t id, char *room, size_t n)
{
    const delta_language *l = delta_lang_by_id((int32_t)id);

    if (l != 0 && l->name != 0)
        return l->name;
    _snprintf(room, n, "0x%lx", (unsigned long)id);
    room[n - 1] = 0;
    return room;
}

/* Which one `/lang something' meant: its tag, its name or its number, so
   that `/lang dede', `/lang German' and `/lang 0x40000' all work. Answers
   -1 for a language this build does not have, and the window then opens in
   whichever the engine picked. */
static int language_asked_for(const char *want)
{
    int i;

    if (want == 0 || *want == 0)
        return -1;

    for (i = 0; i < nlangs; i++) {
        const delta_language *l = delta_lang_by_id((int32_t)langs[i]);
        char                  num[16];

        _snprintf(num, sizeof num, "0x%lx", (unsigned long)langs[i]);
        if (_stricmp(want, num) == 0
            || (l != 0 && l->tag != 0 && _stricmp(want, l->tag) == 0)
            || (l != 0 && l->name != 0 && _stricmp(want, l->name) == 0))
            return i;
    }
    return -1;
}

/* The eight voices belong to the language, so they are read again whenever
   it changes: each language has its own presets and they are not the same
   numbers. */
static void read_voices(void)
{
    int v, i;

    for (v = 1; v <= 8; v++)
        for (i = 0; i < V_COUNT; i++)
            voices[v][i] = vc_getVoiceParam(engine, v, i);
}

/* Speak in another of the languages this build has. The engine takes a
   language change on the instance it already has -- it is an engine change
   underneath, which is the original's own arrangement -- so nothing here has
   to be taken down and put back up. */
static void set_language(int which)
{
    if (which < 0 || which >= nlangs || engine == 0)
        return;

    ev_setParam(engine, P_LANGUAGE, (int32_t)langs[which]);
    lang_now = which;
    read_voices();
    show_voice((int)SendDlgItemMessage(dlg, IDC_VOICE,
                                       CB_GETCURSEL, 0, 0) + 1);
}

static INT_PTR CALLBACK on_dialog(HWND w, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {
    case WM_INITDIALOG:
        dlg = w;
        {
            int v;

            for (v = 1; v <= 8; v++) {
                char name[32];

                sprintf(name, "Voice %d", v);
                SendDlgItemMessageA(w, IDC_VOICE, CB_ADDSTRING, 0,
                                    (LPARAM)name);
            }
        }
        SendDlgItemMessage(w, IDC_VOICE, CB_SETCURSEL, 0, 0);
        {
            int i;

            for (i = 0; i < nlangs; i++) {
                char room[32];

                SendDlgItemMessageA(w, IDC_LANG, CB_ADDSTRING, 0,
                                    (LPARAM)language_name(langs[i], room,
                                                          sizeof room));
            }
            SendDlgItemMessage(w, IDC_LANG, CB_SETCURSEL, lang_now, 0);
            /* Left there but not to be argued with when there is only the
               one: a list of one is a choice nobody has. */
            if (nlangs < 2)
                EnableWindow(GetDlgItem(w, IDC_LANG), FALSE);
        }
        show_voice(1);
        SetDlgItemTextA(w, IDC_TEXT, (opening != 0 && *opening != 0)
                        ? opening
                        : "Hello. This is the Eloquence synthesizer speaking.");
        status("ready");
        return TRUE;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case IDC_SPEAK:
            start_speaking();
            return TRUE;
        case IDC_STOP:
            /* Escape comes here too, and closes the window when there is
               nothing to stop. */
            if (speaking)
                stop_speaking();
            else
                PostMessage(w, WM_CLOSE, 0, 0);
            return TRUE;
        case IDC_SAVE:
            save_wav();
            return TRUE;
        case IDC_VOICE:
            if (HIWORD(wp) == CBN_SELCHANGE)
                show_voice((int)SendDlgItemMessage(w, IDC_VOICE,
                                                   CB_GETCURSEL, 0, 0) + 1);
            return TRUE;
        case IDC_LANG:
            /* Not while something is being said: the engine is spoken to
               from one thread, and the worker is the one holding it. */
            if (HIWORD(wp) == CBN_SELCHANGE) {
                int which = (int)SendDlgItemMessage(w, IDC_LANG,
                                                    CB_GETCURSEL, 0, 0);

                if (speaking) {
                    SendDlgItemMessage(w, IDC_LANG, CB_SETCURSEL, lang_now, 0);
                    status("not while it is speaking");
                } else if (which != lang_now) {
                    char room[32];

                    set_language(which);
                    status(language_name(langs[lang_now], room, sizeof room));
                }
            }
            return TRUE;
        case IDCANCEL:
            PostMessage(w, WM_CLOSE, 0, 0);
            return TRUE;
        }
        return FALSE;

    case WM_SPOKEN:
        {
            char line[64];
            unsigned long n = (unsigned long)wp;

            sprintf(line, "%lu samples, %lu seconds", n, n / RATE);
            status(lp ? "stopped" : line);
        }
        speaking = 0;
        if (worker != 0) {
            CloseHandle(worker);
            worker = 0;
        }
        EnableWindow(GetDlgItem(w, IDC_SPEAK), TRUE);
        SetFocus(GetDlgItem(w, IDC_TEXT));
        return TRUE;

    case WM_CLOSE:
        stop_speaking();
        DestroyWindow(w);
        return TRUE;

    case WM_DESTROY:
        PostQuitMessage(0);
        return TRUE;
    }
    return FALSE;
}

static int engine_start(void)
{
    int n = LANGS_MAX;

    evv_port_start();
    evvRunStaticInitialisers();

    if (eo_getAvailableLanguages(langs, &n) || n < 1)
        return 0;
    nlangs = n;

    /* The one asked for on the command line, if this build has it, and
       otherwise whichever the engine picks, which is the first one linked
       in. */
    lang_now = language_asked_for(wanted);
    if (lang_now >= 0)
        engine = eo_newEx((int32_t)langs[lang_now]);
    else
        engine = eo_new();
    if (engine == 0)
        engine = eo_newEx((int32_t)langs[0]);
    if (engine == 0)
        return 0;
    if (lang_now < 0)
        lang_now = 0;

    /* A person's units, so the numbers in the window are words a minute and
       hertz rather than the engine's own scale. */
    ev_setParam(engine, P_REAL_WORLD_UNITS, 1);

    read_voices();

    eo_registerCallback(engine, (void *)on_message, 0);
    return ev_setOutputBuffer(engine, FRAME, frame) != 0;
}

int WINAPI WinMain(HINSTANCE inst, HINSTANCE prev, LPSTR cmd, int show)
{
    HACCEL acc;
    MSG    m;

    (void)prev;
    (void)show;

    /* Anything on the command line starts in the text box, and /say asks for
       it to be spoken as soon as the window is up. That is how the sound is
       tested without a mouse, and `/lang dede /say ...' is how that is done
       in a build's other language. */
    opening = cmd;
    /* The whole line may have been handed over as one quoted argument, so
       what is in front of the first word comes off before it is read. */
    while (opening != 0 && (*opening == ' ' || *opening == '"'))
        opening++;
    if (opening != 0 && strncmp(opening, "/lang", 5) == 0) {
        char *at = opening + 5;
        char *end;

        while (*at == ' ' || *at == '"')
            at++;
        end = at;
        while (*end != 0 && *end != ' ' && *end != '"')
            end++;
        if (*end != 0)
            *end++ = 0;
        wanted = at;
        opening = end;
        while (*opening == ' ')
            opening++;
    }
    if (opening != 0 && strncmp(opening, "/say", 4) == 0) {
        say_at_once = 1;
        opening += 4;
        while (*opening == ' ' || *opening == '"')
            opening++;
        {
            size_t n = strlen(opening);

            while (n > 0 && (opening[n - 1] == ' ' || opening[n - 1] == '"'))
                opening[--n] = 0;
        }
    }

    if (!engine_start()) {
        MessageBoxA(0, "The engine would not start.", "openevv",
                    MB_OK | MB_ICONERROR);
        return 1;
    }

    dlg = CreateDialogParamA(inst, (LPCSTR)IDD_SPEAK, 0, on_dialog, 0);
    if (dlg == 0) {
        MessageBoxA(0, "The window would not open.", "openevv",
                    MB_OK | MB_ICONERROR);
        return 1;
    }
    acc = LoadAcceleratorsA(inst, (LPCSTR)IDA_SPEAK);
    ShowWindow(dlg, SW_SHOW);
    if (say_at_once)
        PostMessage(dlg, WM_COMMAND, IDC_SPEAK, 0);

    while (GetMessage(&m, 0, 0, 0) > 0) {
        if (acc != 0 && TranslateAcceleratorA(dlg, acc, &m))
            continue;
        if (IsDialogMessage(dlg, &m))
            continue;
        TranslateMessage(&m);
        DispatchMessage(&m);
    }

    /* The worker may still be inside the engine, so it is asked to stop and
       then waited for: deleting the instance under it would be a use of freed
       memory that only showed up as noise. */
    stop_speaking();
    if (worker != 0)
        WaitForSingleObject(worker, 5000);
    if (wave != 0) {
        waveOutReset(wave);
        waveOutClose(wave);
    }
    es_delete(engine);
    evv_port_finish();
    return 0;
}
