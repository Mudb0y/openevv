/* Between the engine's scale and a person's.
 *
 * Three of a voice's settings mean something outside the engine: pitch in
 * hertz, speed in words a minute, volume on a scale of its own. The engine
 * counts all three from nought to a hundred, so somebody has to convert.
 *
 * Pitch and volume are tables, lifted from the original's data. Speed is a
 * curve: fourteen times the square, plus fourteen hundred and six times the
 * value, plus seventy thousand two hundred and fifty, over a thousand and
 * rounded. That runs from seventy words a minute at nought to three hundred
 * and fifty-one at a hundred.
 *
 * Going the other way there is no formula, so it is a binary search over the
 * forward one. When it narrows to two neighbours it picks whichever lands
 * closer, and a tie goes to the lower.
 *
 * Names are the ones the object uses, so no aliases are needed for the two
 * that carry plain C names.
 */

#include <stdint.h>
#include "eci_synththread.h"
#include "evv_abi.h"

extern const int32_t ev_voiceParamRange[8][2];

/* Which settings mean anything outside the engine. */
#define PARAM_PITCH   2
#define PARAM_SPEED   6
#define PARAM_VOLUME  7

/* And the flag that says the caller is speaking in those units. */
#define IN_REAL_WORLD 1

static const int32_t eciToRealPitch[101] = {
    40, 40, 40, 40, 40, 41, 41, 41, 42, 42,
    42, 43, 43, 43, 44, 44, 44, 45, 45, 46,
    46, 47, 47, 48, 49, 49, 50, 51, 51, 52,
    53, 54, 54, 55, 56, 57, 58, 59, 60, 62,
    63, 64, 65, 67, 68, 70, 71, 73, 74, 76,
    78, 80, 82, 84, 86, 89, 91, 94, 96, 99,
    102, 105, 108, 111, 115, 118, 122, 126, 130, 134,
    139, 144, 148, 154, 159, 164, 170, 176, 183, 189,
    196, 203, 211, 219, 227, 236, 244, 254, 264, 274,
    285, 296, 307, 320, 332, 346, 360, 374, 389, 405,
    422,
};

static const int32_t eciToRealVolume[101] = {
    1, 655, 1310, 1965, 2620, 3275, 3930, 4585, 5240, 5895,
    6550, 7205, 7860, 8515, 9170, 9825, 10480, 11135, 11790, 12445,
    13100, 13755, 14410, 15065, 15720, 16375, 17030, 17685, 18340, 18995,
    19650, 20305, 20960, 21615, 22270, 22925, 23580, 24235, 24890, 25545,
    26200, 26855, 27510, 28165, 28820, 29475, 30130, 30785, 31440, 32095,
    32750, 33405, 34060, 34715, 35370, 36025, 36680, 37335, 37990, 38645,
    39300, 39955, 40610, 41265, 41920, 42575, 43230, 43885, 44540, 45195,
    45850, 46505, 47160, 47815, 48470, 49125, 49780, 50435, 51090, 51745,
    52400, 53055, 53710, 54365, 55020, 55675, 56330, 56985, 57640, 58295,
    58950, 59605, 60260, 60915, 61570, 62225, 62880, 63535, 64190, 64845,
    65535,
};

/* The speed curve, in thousandths of a word a minute before rounding. */
#define SPEED_SQUARE  14
#define SPEED_LINEAR  1406
#define SPEED_BASE    70250
#define SPEED_SCALE   1000

int eci2RealWorld(int32_t realWorld, int32_t which, int32_t value)
{
    if (realWorld != IN_REAL_WORLD)
        return value;

    if (value > ev_voiceParamRange[which][1])
        value = ev_voiceParamRange[which][1];
    if (value < ev_voiceParamRange[which][0])
        value = ev_voiceParamRange[which][0];

    switch (which) {
    case PARAM_PITCH:
        return eciToRealPitch[value];
    case PARAM_SPEED: {
        int32_t t = value * SPEED_LINEAR
                  + SPEED_SQUARE * value * value
                  + SPEED_BASE;

        return (t + SPEED_SCALE / 2) / SPEED_SCALE;
    }
    case PARAM_VOLUME:
        return eciToRealVolume[value];
    default:
        return value;
    }
}

/* Find the engine value whose real-world value is nearest the one asked
   for, between lo and hi. */
int realWorld2eci(int32_t realWorld, int32_t which, int32_t target,
                  int32_t lo, int32_t hi)
{
    int32_t mid;
    int32_t here;

    if (hi > ev_voiceParamRange[which][1])
        hi = ev_voiceParamRange[which][1];
    if (lo < ev_voiceParamRange[which][0])
        lo = ev_voiceParamRange[which][0];

    if (realWorld != IN_REAL_WORLD)
        return target;
    if (which != PARAM_PITCH && which != PARAM_SPEED
        && which != PARAM_VOLUME)
        return target;

    if (lo == hi)
        return lo;

    /* Down to two: whichever is nearer, and a tie goes to the lower. */
    if (hi - lo == 1) {
        int32_t above = eci2RealWorld(realWorld, which, hi);
        int32_t below = eci2RealWorld(realWorld, which, lo);

        return (above - target >= target - below) ? lo : hi;
    }

    mid = (lo + hi) / 2;
    here = eci2RealWorld(realWorld, which, mid);
    if (target < here)
        return realWorld2eci(realWorld, which, target, lo, mid);
    return realWorld2eci(realWorld, which, target, mid, hi);
}

/* An error reporter that does nothing, which is what the engine installs
   when it does not want to hear. */
int errorIgnore(void)
{
    return 0;
}

ALIAS("?eci2RealWorld@@YAHHW4ECIVoiceParam@@H@Z", "eci2RealWorld");
ALIAS("?realWorld2eci@@YAHHW4ECIVoiceParam@@HHH@Z", "realWorld2eci");
