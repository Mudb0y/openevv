/* The eight voices the caller may edit, on an instance that has just been made.
 *
 * The suite cannot see these. Nothing in cli/probe.c or in the reference asks
 * an instance about voice nine, so the loop in eo_newInstance that copies the
 * language's eight standard voices into the caller's eight editable ones can be
 * turned off entirely -- `for (i = 0; i < 0; i++)' -- and all 81 English cases
 * and all 80 German ones still match IBM's binary. That happened, by accident,
 * on 23 August 2026: a stale script in /tmp made exactly that edit, every check
 * passed, and only reading the commit caught it. This is what would have caught
 * it instead.
 *
 * What a caller can tell, and therefore what is checked here:
 *
 * A fresh editable voice is the standard voice of the same position. Voice 9 is
 * voice 1, voice 16 is voice 8, in every one of the eight parameters. That is
 * the copy, and it is the thing that was missing.
 *
 * A fresh editable voice is called "User-Defined", and the standard ones are
 * not. Both halves matter: the name alone would pass if the copy were replaced
 * by eight names and nothing else, and the parameters alone would pass if the
 * copy were made without the renaming.
 *
 * They are eight separate voices and they are copies. Writing a parameter into
 * voice 9 moves voice 9 and leaves voice 1 and voice 10 where they were.
 * Naming voice 9 does the same. If the eight shared one slot, or were the
 * standard table rather than a copy of it, that is where it shows.
 *
 * Which parameter to move is found rather than chosen. Each has a range of its
 * own -- the first is a sex and takes nought or one, the seventh runs to 250 --
 * and the ranges are not the same in the two sets of units, so this tries one
 * more than the value each holds and then one less, and uses the first
 * parameter that will take either.
 *
 * Writing a parameter the value it already holds is asked of all eight, and in
 * the engine's units all eight take it. In a person's units two of them will
 * not, and that is IBM's and not ours: its real-world range starts at one for
 * six of the eight, while the conversion answers nought for the roughness and
 * the breathiness of a voice that has none. So a caller reading those back and
 * writing them straight in is refused. The suite already holds those very
 * numbers against IBM's binary, over the twenty cases it reads the parameters
 * back in a person's units for, so the nought is theirs too. It is reported
 * here rather than called a fault.
 *
 * A second instance starts again. An edit made through one instance must not be
 * visible through another, which is what says the eight are per instance rather
 * than one table the language holds.
 *
 * And a voice number nobody may write to is refused: eciSetVoiceParam answers
 * -1 for the standard eight, because the caller does not own them.
 *
 * All of it twice: once in the engine's own units and once with the parameters
 * read back in a person's, since those go through a conversion on the way out
 * and a copy that is right in one could be wrong in the other.
 *
 * usage: voices
 */

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "evv_abi.h"

typedef struct OldInst OldInst;

/* The published interface, under the names this build gives it. */
OldInst *STDCALL eo_new(void);
int      STDCALL es_delete(OldInst *h);
int      STDCALL eo_getParam(OldInst *h, int32_t which);
int      STDCALL ev_setParam(OldInst *h, int32_t which, int32_t value);
int      STDCALL vc_getVoiceParam(OldInst *h, int32_t voiceno, int32_t which);
int      STDCALL vc_setVoiceParam(OldInst *h, int32_t voiceno, int32_t which,
                                  int32_t value);
int      STDCALL vc_getVoiceName(OldInst *h, int32_t voiceno, void *out);
int      STDCALL vc_setVoiceName(OldInst *h, int32_t voiceno,
                                 const char *name);
int      STDCALL vc_copyVoice(OldInst *h, int32_t from, int32_t to);
void evvRunStaticInitialisers(void);
void evv_port_start(void);
void evv_port_finish(void);

/* Eight standard voices, eight editable ones after them, and eight parameters
   in each. Parameter 8 is refused by the engine, so it is not one of them. */
enum { VOICES = 8, STANDARD_FIRST = 1, EDITABLE_FIRST = 9, PARAMS = 8 };
/* Which environment parameter says the caller wants a person's units. */
enum { ENV_REALWORLD = 8 };

static const char *const OWN = "User-Defined";

static int bad;

static void wrong(const char *what, long got, long want)
{
    printf("voices: %s: %ld where %ld was wanted\n", what, got, want);
    bad = 1;
}

/* Every parameter of one voice against every parameter of another. */
static void same(OldInst *h, int a, int b, const char *what)
{
    int w;

    for (w = 0; w < PARAMS; w++) {
        int va = vc_getVoiceParam(h, a, w);
        int vb = vc_getVoiceParam(h, b, w);

        if (va != vb) {
            printf("voices: %s: voice %d parameter %d is %d and voice %d's"
                   " is %d\n", what, a, w, va, b, vb);
            bad = 1;
        }
    }
}

static const char *name_of(OldInst *h, int voiceno, char *room)
{
    room[0] = 0;
    if (!vc_getVoiceName(h, voiceno, room))
        printf("voices: voice %d would not say its name\n", voiceno);
    return room;
}

static void a_fresh_instance(int realworld)
{
    char room[64], other[64];
    OldInst *h = eo_new();
    OldInst *second;
    int i, was = 0, now, named, moved, refused;

    if (!h) {
        printf("voices: no instance\n");
        bad = 1;
        return;
    }
    if (realworld)
        ev_setParam(h, ENV_REALWORLD, 1);
    if (eo_getParam(h, ENV_REALWORLD) != (realworld ? 1 : 0)) {
        wrong("the units did not take", eo_getParam(h, ENV_REALWORLD),
              realworld);
        es_delete(h);
        return;
    }

    /* The copy: editable N is standard N. */
    for (i = 0; i < VOICES; i++)
        same(h, EDITABLE_FIRST + i, STANDARD_FIRST + i, "not copied");

    /* The naming, and the standard ones keeping their own names. */
    named = 0;
    for (i = 0; i < VOICES; i++) {
        if (strcmp(name_of(h, EDITABLE_FIRST + i, room), OWN) != 0) {
            printf("voices: voice %d is called %s and not %s\n",
                   EDITABLE_FIRST + i, room, OWN);
            bad = 1;
        }
        if (strcmp(name_of(h, STANDARD_FIRST + i, room), OWN) != 0)
            named++;
    }
    if (named != VOICES) {
        wrong("standard voices with a name of their own", named, VOICES);
    }

    /* Every parameter takes the value it already holds and says what that
       was, except where IBM's range in a person's units will not have it. */
    refused = 0;
    for (i = 0; i < PARAMS; i++) {
        was = vc_getVoiceParam(h, EDITABLE_FIRST, i);
        if (vc_setVoiceParam(h, EDITABLE_FIRST, i, was) == was)
            continue;
        if (realworld && was == 0) {
            printf("voices: parameter %d reads back as nought and a person's"
                   " units will not take a nought, which is IBM's range\n", i);
            refused++;
            continue;
        }
        printf("voices: parameter %d would not take the %d it holds\n",
               i, was);
        bad = 1;
    }

    /* Eight separate slots, and copies rather than the table itself. Which
       parameter to move is whichever one will move. */
    moved = -1;
    for (i = 0; i < PARAMS && moved < 0; i++) {
        int j;

        was = vc_getVoiceParam(h, EDITABLE_FIRST, i);
        for (j = 0; j < 2; j++) {
            now = was + (j == 0 ? 1 : -1);
            if (vc_setVoiceParam(h, EDITABLE_FIRST, i, now) != -1
                && vc_getVoiceParam(h, EDITABLE_FIRST, i) == now) {
                moved = i;
                break;
            }
        }
    }
    if (moved < 0) {
        printf("voices: not one of the eight parameters would move\n");
        bad = 1;
    } else {
        if (vc_getVoiceParam(h, STANDARD_FIRST, moved) != was)
            wrong("editing voice 9 moved voice 1",
                  vc_getVoiceParam(h, STANDARD_FIRST, moved), was);
        same(h, EDITABLE_FIRST + 1, STANDARD_FIRST + 1,
             "editing 9 moved 10");
    }

    if (!vc_setVoiceName(h, EDITABLE_FIRST, "Testing"))
        printf("voices: voice 9 would not be named\n");
    if (strcmp(name_of(h, EDITABLE_FIRST, room), "Testing") != 0)
        printf("voices: voice 9 is called %s after being named Testing\n",
               room);
    if (strcmp(name_of(h, EDITABLE_FIRST + 1, other), OWN) != 0) {
        printf("voices: naming voice 9 renamed voice 10 to %s\n", other);
        bad = 1;
    }

    /* A voice the caller does not own is refused. */
    for (i = 0; i < VOICES; i++) {
        if (vc_setVoiceParam(h, STANDARD_FIRST + i, 0, 50) != -1) {
            printf("voices: voice %d took a parameter it does not own\n",
                   STANDARD_FIRST + i);
            bad = 1;
        }
    }

    /* Copying one voice over another. */
    if (!vc_copyVoice(h, STANDARD_FIRST + 2, EDITABLE_FIRST))
        printf("voices: voice 3 would not copy onto voice 9\n");
    same(h, EDITABLE_FIRST, STANDARD_FIRST + 2, "copied but not equal");

    /* And a second instance starts again. */
    second = eo_new();
    if (!second) {
        printf("voices: no second instance\n");
        bad = 1;
    } else {
        if (realworld)
            ev_setParam(second, ENV_REALWORLD, 1);
        for (i = 0; i < VOICES; i++)
            same(second, EDITABLE_FIRST + i, STANDARD_FIRST + i,
                 "a second instance did not start again");
        if (strcmp(name_of(second, EDITABLE_FIRST, room), OWN) != 0) {
            printf("voices: a second instance's voice 9 is called %s\n", room);
            bad = 1;
        }
        es_delete(second);
    }

    es_delete(h);
    printf("voices: %s units, %d voices of %d parameters, parameter %d moved,"
           " %d refused their own value, %s\n",
           realworld ? "a person's" : "the engine's", VOICES, PARAMS, moved,
           refused, bad ? "something is wrong" : "all of it as it should be");
}

int main(void)
{
    evvRunStaticInitialisers();
    evv_port_start();

    a_fresh_instance(0);
    a_fresh_instance(1);

    evv_port_finish();
    if (bad)
        printf("voices: the eight the caller may edit are not what they"
               " should be\n");
    else
        printf("voices: the eight the caller may edit start as the language's"
               " own, named as the caller's, one instance apart from another\n");
    return bad;
}
