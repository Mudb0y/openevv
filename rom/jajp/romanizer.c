/* The one piece of Romanizer anything else needs.
 *
 * Romanizer itself is not transcribed. `DictSearch::Do` calls this method
 * when it meets an annotation standing in front of the character it is about
 * to look up, so it is written here ahead of the rest of its class, and
 * rom/jajp/romanizer.h is as much of the record as has been read.
 *
 * What it does is read one of the engine's own voice annotations and change
 * the setting it names. The forms are `vs50 for an absolute value, `vs%+10
 * for a percentage of what is there, `vswpm+20 and `vbhz-5 for a step in
 * words per minute or in hertz, `vsmed for the middle setting, and `v1 or
 * `v2 for one of the two voices, which resets the other four.
 *
 * One thing here is IBM's and is kept: every one of the relative forms writes
 * the setting twice, once clamped at the top and then again from the
 * unclamped value, so the upper clamp has no effect at all. Only the clamp at
 * nought does anything.
 *
 * Held to IBM's answer by test/romprims.sh.
 */

#include <stdio.h>
#include <string.h>
#include "jprom.h"
#include "romanizer.h"

#define RZ_L(rz, off)   (*(int32_t *)((uint8_t *)(rz) + (off)))

/* Which voice the parameter block says is in force, which is what the middle
   settings and the voice reset are taken from. */
#define P_VOICE_ID      0x3ea

/* One setting stepped by a proportion of itself, and then by the two clamps
   IBM wrote -- the second of which undoes the first. */
static void stepBy(void *rz, int32_t off, int32_t by, int32_t cap)
{
    int32_t got = by + RZ_L(rz, off);

    RZ_L(rz, off) = got < cap ? got : cap;
    RZ_L(rz, off) = got > 0 ? got : 0;
}

int32_t rz_GetParameter(void *rz, char *p)
{
    RomInstParam *param = *(RomInstParam **)((uint8_t *)rz + RZ_PARAM_AT);
    int32_t       v;
    int32_t       dir;
    int32_t       was;
    char          which;
    char          sign;

    if (strlen(p) <= 3)
        return -1;

    if (p[1] == 'v') {
        if (sscanf(p + 2, "%c%d", &which, &v) == 2) {
            switch (which) {
            case 's': RZ_L(rz, RZ_SPEED) = v; break;
            case 'b': RZ_L(rz, RZ_BASELINE) = v; break;
            case 'f': RZ_L(rz, RZ_FLUENCY) = v; break;
            case 'v': RZ_L(rz, RZ_VOLUME) = v; break;
            default: break;
            }
            return 0;
        }
        if (sscanf(p + 2, "%c%%%c%d", &which, &sign, &v) == 3) {
            dir = sign == '+' ? 1 : -1;
            switch (which) {
            case 's':
                stepBy(rz, RZ_SPEED,
                       RZ_L(rz, RZ_SPEED) * dir * v / 100, 0xfa);
                break;
            case 'b':
                stepBy(rz, RZ_BASELINE,
                       RZ_L(rz, RZ_BASELINE) * dir * v / 100, 0x64);
                break;
            case 'f':
                stepBy(rz, RZ_FLUENCY,
                       RZ_L(rz, RZ_FLUENCY) * dir * v / 100, 0x64);
                break;
            case 'v':
                stepBy(rz, RZ_VOLUME,
                       RZ_L(rz, RZ_VOLUME) * dir * v / 100, 0x64);
                break;
            default: break;
            }
            return 0;
        }
        if (sscanf(p + 2, "swpm%c%d", &sign, &v) == 2) {
            dir = sign == '+' ? 1 : -1;
            stepBy(rz, RZ_SPEED, dir * v / 0xc8, 0xfa);
            return 0;
        }
        if (sscanf(p + 2, "bhz%c%d", &sign, &v) == 2) {
            dir = sign == '+' ? 1 : -1;
            stepBy(rz, RZ_BASELINE, dir * v / 0xa, 0x64);
            return 0;
        }
        if (sscanf(p + 2, "%cmed", &which) == 1) {
            was = rp_getParam(param, P_VOICE_ID);
            switch (which) {
            case 's': RZ_L(rz, RZ_SPEED) = 0x2e; break;
            case 'b': RZ_L(rz, RZ_BASELINE) = was == 1 ? 0x41 : 0x59; break;
            case 'f': RZ_L(rz, RZ_FLUENCY) = was == 1 ? 0x1e : 0x27; break;
            default: break;
            }
            return 0;
        }
        if (sscanf(p + 2, "%d", &v) == 1 && (v == 1 || v == 2)) {
            RZ_L(rz, RZ_VOICE) = v;
            if (v == 1) {
                RZ_L(rz, RZ_BASELINE) = 0x41;
                RZ_L(rz, RZ_FLUENCY) = 0x1e;
            } else {
                RZ_L(rz, RZ_BASELINE) = 0x59;
                RZ_L(rz, RZ_FLUENCY) = 0x27;
            }
            RZ_L(rz, RZ_SPEED) = 0x2e;
            return 0;
        }
        return -1;
    }

    if (p[1] == 't' && sscanf(p + 2, "%c%d", &which, &v) == 2) {
        if (which == 's')
            RZ_L(rz, RZ_SPELL_ENGLISH) = v;
        return 0;
    }
    return -1;
}
