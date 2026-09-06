package org.evvdroid

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/** Phrase prediction, which is an annotation rather than a parameter, so the
 *  only way to know the engine took it is to listen to the difference. */
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
	}
}
