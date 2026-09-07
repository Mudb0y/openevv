package org.evvdroid

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Test
import org.junit.runner.RunWith

/**
 * That a step of one percent is a step.
 *
 * The sliders show percentages and the engine stores its own numbers, and a
 * step goes out through one conversion and comes back through the other. Speed
 * is the one where the two scales differ: a percent is two and a half of the
 * engine's units, so a percent that rounded to the same engine number as the
 * one before it would leave the slider stuck under an arrow key.
 */
@RunWith(AndroidJUnit4::class)
class SliderStepTest {

	@Test
	fun everyPercentOfEverySliderIsItsOwnNumber() {
		for (param in Settings.SLIDERS) {
			for (percent in 0..Eci.PERCENT_MAX) {
				assertEquals(
					"param $param lost $percent percent on the way through the engine's scale",
					percent,
					Eci.toPercent(param, Eci.fromPercent(param, percent))
				)
			}
		}
	}

	@Test
	fun aStepOfOneIsAudibleAtTheSlowEnd() {
		// Five percent of the speed scale is twelve and a half of the engine's,
		// which at the bottom is the difference between 0.40 and 0.48 times the
		// default rate. One percent divides that into five.
		val slowest = SpeechRate.timesFor(Eci.fromPercent(Eci.VOICE_SPEED, 0))
		val five = SpeechRate.timesFor(Eci.fromPercent(Eci.VOICE_SPEED, 5))
		val one = SpeechRate.timesFor(Eci.fromPercent(Eci.VOICE_SPEED, 1))
		assertEquals(0.400, slowest, 0.001)
		assertEquals(0.481, five, 0.005)
		assertEquals(true, one > slowest && one < five)
	}
}
