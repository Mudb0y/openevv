package org.evvdroid

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** What is asked of the engine by annotation rather than by parameter, so the
 *  only way to know it took is to listen to the difference. */
@RunWith(AndroidJUnit4::class)
class ProsodyTest {

	@Test
	fun theAnnotationIsTheOneTheEngineKnows() {
		assertEquals("`pp1 ", Prosody.prefix(true))
		assertEquals("`pp0 ", Prosody.prefix(false))
	}

	@Test
	fun theEngineActsOnIt() {
		val e = EvvEngine.open(ENGLISH)
		assertNotNull(e)
		try {
			e!!.setSampleRate(11025)
			e.applyVoice(0)
			val off = say(e, Prosody.prefix(false) + SENTENCE)
			val on = say(e, Prosody.prefix(true) + SENTENCE)
			assertTrue("nothing was said", off > 0 && on > 0)
			assertTrue("the annotation changed nothing", off != on)
			// And neither is the annotation being read out, which would make
			// one of them far longer than the words alone.
			val plain = say(e, SENTENCE)
			assertTrue("something was spoken aloud", on < plain * 2 && off < plain * 2)
		} finally {
			e?.close()
		}
	}

	/**
	 * A rate asked for while the engine is still speaking.
	 *
	 * This is where a screen reader asks: every item cuts off the one before,
	 * so the ask lands while the last utterance is still being made. The
	 * engine refuses a written parameter then -- vc_reentered answers -1 while
	 * the instance is busy -- and says so only in a return value, so an ask
	 * made at that moment used to be dropped, and the voice went on at
	 * whatever the last utterance left it until something happened to ask for
	 * a different number.
	 */
	@Test
	fun aRateAskedForWhileTheEngineIsSpeakingIsStillHeard() {
		val e = EvvEngine.open(ENGLISH)
		assertNotNull(e)
		try {
			e!!.setSampleRate(11025)
			e.applyVoice(0)
			e.setRatePercent(100)
			val quick = say(e, SENTENCE)
			// Half speed with nothing in the way, to know what it sounds like
			// when it does land.
			e.setRatePercent(50)
			val slow = say(e, SENTENCE)
			assertTrue("half speed was no slower: $slow against $quick", slow > quick)

			// And now the same ask, made in the middle of an utterance.
			e.setRatePercent(100)
			say(e, SENTENCE)
			assertTrue("nothing was queued", e.speak(PARAGRAPH))
			val buffer = ByteArray(4096)
			// One read, so the engine is certainly running by the time the ask
			// is made rather than merely holding the text.
			e.read(buffer)
			e.setRatePercent(50)
			e.stop()
			while (e.read(buffer) > 0) Unit
			val after = say(e, SENTENCE)
			assertTrue(
				"the rate asked for mid-utterance was never heard: $after against $quick",
				after > quick + quick / 5
			)
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
		const val SENTENCE = "The header is read first, and the list that follows it is long."
		val PARAGRAPH = ("The quick brown fox jumps over the lazy dog. " +
			"Every good boy deserves favour, and the rain in Spain stays mainly in the plain. ")
			.repeat(6)
	}
}
