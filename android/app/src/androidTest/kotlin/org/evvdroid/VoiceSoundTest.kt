package org.evvdroid

import android.util.Log
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Test
import org.junit.runner.RunWith

/**
 * That the eight voices actually sound like eight voices.
 *
 * Nothing else checked this. One test says every preset speaks, another says
 * their settings differ, and a third says they all run at the same pace, and a
 * build where every voice came out as Reed would pass all three.
 *
 * Each voice gets an engine of its own. The engine's state moves on between
 * utterances by design, so the same sentence twice on one instance is not the
 * same samples, and a comparison across voices on one instance would find them
 * all different whether or not the voice ever changed.
 */
@RunWith(AndroidJUnit4::class)
class VoiceSoundTest {

	@Test
	fun eachVoiceSoundsLikeItself() {
		val heard = LinkedHashMap<String, Int>()
		for (preset in Eci.PRESET_NAMES.indices) {
			val e = EvvEngine.open(ENGLISH)
			assertNotNull(e)
			try {
				e!!.setSampleRate(11025)
				// The whole of what the settings screen would hand over, which
				// is what the service does too.
				e.applyVoice(preset, e.presetShape(preset))
				heard[Eci.PRESET_NAMES[preset]] = fingerprint(e)
			} finally {
				e?.close()
			}
		}
		Log.e(TAG, "$heard")
		assertEquals(
			"voices that sounded identical: $heard",
			Eci.PRESET_NAMES.size,
			heard.values.toSet().size
		)
	}

	/** And with nothing laid over them, which is the path a fresh install
	 *  takes. */
	@Test
	fun eachVoiceSoundsLikeItselfWithNoSettingsAtAll() {
		val heard = LinkedHashMap<String, Int>()
		for (preset in Eci.PRESET_NAMES.indices) {
			val e = EvvEngine.open(ENGLISH)
			assertNotNull(e)
			try {
				e!!.setSampleRate(11025)
				e.applyVoice(preset)
				heard[Eci.PRESET_NAMES[preset]] = fingerprint(e)
			} finally {
				e?.close()
			}
		}
		assertEquals(
			"voices that sounded identical: $heard",
			Eci.PRESET_NAMES.size,
			heard.values.toSet().size
		)
	}

	private fun fingerprint(e: EvvEngine): Int {
		if (!e.speak("Testing one two three.")) return 0
		val out = java.io.ByteArrayOutputStream()
		val buffer = ByteArray(4096)
		while (true) {
			val n = e.read(buffer)
			if (n <= 0) break
			out.write(buffer, 0, n)
		}
		return out.toByteArray().contentHashCode()
	}

	private companion object {
		const val TAG = "evvvoice"
		const val ENGLISH = 0x00010000
	}
}
