package org.evvdroid

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Android's speech rate is a multiplier, so it has to behave like one.
 *
 * Multiplying the engine's speed number by the percentage does not: the scale
 * is nothing like linear, and a rate of 400 used to land on speed 200, which
 * is sixteen times the default rather than four.
 */
@RunWith(AndroidJUnit4::class)
class SpeechRateTest {

	@Test
	fun oneHundredPercentIsTheVoiceLeftAlone() {
		for (base in listOf(0, 25, 50, 70, 100, 250)) {
			assertEquals(base, SpeechRate.speedForPercent(base, 100))
		}
	}

	@Test
	fun theTableRoundTrips() {
		for (speed in 0..250 step 5) {
			val back = SpeechRate.speedFor(SpeechRate.timesFor(speed))
			assertTrue("speed $speed came back as $back", Math.abs(back - speed) <= 2)
		}
	}

	@Test
	fun fasterIsAlwaysFaster() {
		var last = -1.0
		for (speed in 0..250 step 5) {
			val times = SpeechRate.timesFor(speed)
			assertTrue("speed $speed went backwards", times >= last)
			last = times
		}
	}

	/** The one that matters, measured out of the engine rather than the table. */
	@Test
	fun theRateActuallyMultipliesTheSpeed() {
		val e = EvvEngine.open(ENGLISH)
		assertNotNull(e)
		try {
			e!!.setSampleRate(11025)
			e.applyVoice(0)
			e.setRatePercent(100)
			val normal = say(e)
			for ((percent, want) in listOf(200 to 2.0, 300 to 3.0, 400 to 4.0)) {
				e.setRatePercent(percent)
				val got = normal.toDouble() / say(e)
				assertTrue(
					"$percent% came out $got times faster, not about $want",
					got > want * 0.75 && got < want * 1.3
				)
			}
		} finally {
			e?.close()
		}
	}

	private fun say(e: EvvEngine): Int {
		if (!e.speak(SENTENCE)) return -1
		var total = 0
		val buffer = ByteArray(4096)
		while (true) {
			val n = e.read(buffer)
			if (n <= 0) break
			total += n
		}
		return total
	}

	private companion object {
		const val ENGLISH = 0x00010000
		const val SENTENCE = "The quick brown fox jumps over the lazy dog."
	}
}
