#ifndef KLATT_RATES_H
#define KLATT_RATES_H

#include <stdint.h>

/* The two tables every resonator in KlattSynth is built from.
 *
 * IBM shipped four of them, two for each of the two rates its synthesiser
 * recognises by name, sliced out of clsyn.obj by tools/gen-tables.py. They
 * are not arbitrary data: the excitation table is the decay factor of a pole
 * at a given bandwidth and the cosine table is that pole's angle, both in
 * fifteen-bit fixed point, and both indexed by hertz from ten upwards.
 *
 * Which is what makes another rate possible at all. Generating IBM's four
 * from the formulae below and comparing entry by entry: every one of the
 * 3,991 excitation entries is exact at both rates, and every cosine entry is
 * exact except two at eight thousand where IBM stored the saturated value
 * where the formula rounds one short of it. test/rates.c is that comparison,
 * kept so the claim stays checked rather than remembered.
 *
 * So the tables for a rate IBM never shipped are built rather than stored,
 * and IBM's own four are never rebuilt -- KlattSetConstParms goes on handing
 * out the static arrays at eight thousand and eleven thousand and twenty
 * five, so the two rates that have to stay byte for byte identical do not
 * depend on anyone's rounding.
 */

/* KlattSynth clamps every frequency to ten through five thousand and every
   bandwidth to ten through four thousand before it indexes, so those bounds
   are the tables' bounds. IBM's arrays each carry one further slot that
   nothing can reach. */
#define KLATT_EX_FIRST   10
#define KLATT_EX_LAST    4000
#define KLATT_CO_FIRST   10
#define KLATT_CO_LAST    5000
#define KLATT_EX_COUNT   (KLATT_EX_LAST - KLATT_EX_FIRST + 1)
#define KLATT_CO_COUNT   (KLATT_CO_LAST - KLATT_CO_FIRST + 1)

/* Below eight thousand the cosine table would run more than half a turn and
   the arithmetic stops meaning what it says.

   The ceiling is measured rather than reasoned about, and it is lower than
   the arithmetic alone would suggest. Somewhere between 44,508 and 44,509
   hertz the synthesiser's accumulator runs away on ordinary speech: the peak
   sample goes from about 2,800 to several million in one hertz, which the
   caller sees as broadband noise once it has been cut down to sixteen bits.
   The boundary is the same for six of the seven varied sentences in
   test/cases, for three thousand of the crash corpus, and for pitches from
   forty to a hundred hertz, so it is a constant of the rate and not an
   interaction with the text.

   What it is not: the resonators are provably stable at every rate, since
   pole_filter's stability condition is exactly that the gain term the
   coefficients are built with is positive, and the worst case of that over
   every frequency and bandwidth the tables can be asked for is positive at
   every rate. It is not the coefficient ramp either, nor the pitch period,
   nor the frame length, all of which are identical either side of the
   boundary. It is not run down further than that, and docs/status.md says
   so.

   So the ceiling on what the synthesiser is asked to run at is 44,100 --
   four hundred hertz below the boundary and clean over every text measured.
   test/rates.c holds the slew of every synthesised rate to a bound, so this
   cannot be crossed again without something saying so.

   It is a ceiling on the engine and not on the caller. A rate reached by
   repeating samples never asks the synthesiser for anything above eleven
   thousand and twenty five, so the rate a caller may have goes higher --
   EV_RATE_MAX_HZ in src/eci/api/eci_env.c is that one, and 48,000 is above this
   line for exactly that reason. */
#define KLATT_RATE_MIN   8000
#define KLATT_RATE_MAX   44100

/* Fill ex with KLATT_EX_COUNT entries and co with KLATT_CO_COUNT for this
   rate. Answers zero and writes nothing when the rate is out of range. */
int klatt_buildRateTables(int32_t rate, int16_t *ex, int16_t *co);

#endif
