package org.evvdroid

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Shortening the engine's pauses.
 *
 * The text checks say the annotation lands where it should and nowhere else.
 * The last one says it actually makes the speech shorter, which is the only
 * thing anybody cares about and the only one that would notice the engine
 * quietly ignoring the annotation.
 */
@RunWith(AndroidJUnit4::class)
class PausesTest {

	@Test
	fun keepingChangesNothing() {
		assertEquals(SENTENCE, Pauses.apply(SENTENCE, Pauses.KEEP, true))
	}

	@Test
	fun everyMarkGetsOne() {
		assertEquals(
			"One `p1, two `p1, three `p1.",
			Pauses.apply("One, two, three.", Pauses.ALL, false)
		)
	}

	@Test
	fun aRunOfOneMarkIsOnePause() {
		assertEquals("Wait `p1... Now", Pauses.apply("Wait... Now", Pauses.ALL, false))
	}

	/** The whole reason the rule looks at what is on either side. */
	@Test
	fun numbersAreLeftAlone() {
		assertEquals("It is 3.14 exactly", Pauses.apply("It is 3.14 exactly", Pauses.ALL, false))
		assertEquals("At 2:30 today", Pauses.apply("At 2:30 today", Pauses.ALL, false))
		assertEquals("1,024 bytes", Pauses.apply("1,024 bytes", Pauses.ALL, false))
	}

	@Test
	fun theEndOnlyGetsOneWhenNothingElseWould() {
		assertEquals("Navigate up `p1", Pauses.apply("Navigate up", Pauses.END_ONLY, true))
		assertEquals("Navigate up.", Pauses.apply("Navigate up.", Pauses.END_ONLY, true))
		assertEquals("Navigate up", Pauses.apply("Navigate up", Pauses.END_ONLY, false))
	}

	@Test
	fun shorteningAllStillShortensTheEnd() {
		assertEquals("Settings `p1, button `p1", Pauses.apply("Settings, button", Pauses.ALL, true))
	}

	/** Out of the engine rather than out of the rule. */
	@Test
	fun theSpeechIsActuallyShorter() {
		val e = EvvEngine.open(ENGLISH)
		assertNotNull(e)
		try {
			e!!.setSampleRate(11025)
			e.applyVoice(0)
			val kept = say(e, Pauses.apply(SENTENCE, Pauses.KEEP, true))
			val short = say(e, Pauses.apply(SENTENCE, Pauses.ALL, true))
			android.util.Log.e("evvpause", "kept=$kept shortened=$short (${100 * short / kept}%)")
			assertTrue("nothing was said", kept > 0)
			assertTrue(
				"shortening saved nothing: $kept then $short",
				short < kept
			)
			// Every word is still there, so this is pauses going rather than
			// speech being cut.
			assertTrue("far too much went: $kept then $short", short > kept * 0.5)
		} finally {
			e?.close()
		}
	}

	private fun say(e: EvvEngine, text: String): Int {
		if (!e.speak(text)) return -1
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
		const val SENTENCE = "One, two, three. Four, five, six. Seven, eight."
	}
}
