package com.mudb0y.openevv

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Shortening the engine's pauses, which is eciPauseMode and the engine's own
 * work. What is checked here is that the setting reaches the engine from this
 * app and makes the speech shorter, which is the only thing anybody cares
 * about and the only check that would notice it quietly not arriving.
 */
@RunWith(AndroidJUnit4::class)
class PausesTest {

	@Test
	fun eachModeIsShorterThanTheOneBefore() {
		val e = EvvEngine.open(ENGLISH)
		assertNotNull(e)
		try {
			e!!.setSampleRate(11025)
			e.applyVoice(0)
			e.setPauseMode(Eci.PAUSES_KEPT)
			val kept = say(e, SENTENCE)
			e.setPauseMode(Eci.PAUSES_AT_END)
			val end = say(e, SENTENCE)
			e.setPauseMode(Eci.PAUSES_SHORT)
			val short = say(e, SENTENCE)
			android.util.Log.e("openevvpause", "kept=$kept end=$end short=$short (${100 * short / kept}%)")
			assertTrue("nothing was said", kept > 0)
			assertTrue("shortening the end saved nothing: $kept then $end", end < kept)
			assertTrue("shortening every mark saved nothing more: $end then $short", short < end)
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
		// No full stop at the end, so the pause there is one the engine makes
		// for the end of the text, which is what the second mode shortens.
		const val SENTENCE = "One, two, three. Four, five, six. Seven, eight"
	}
}
