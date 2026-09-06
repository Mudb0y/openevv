package org.evvdroid

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Exploring by touch for a long time, which used to take the engine down.
 *
 * Dragging a finger across a launcher cuts every utterance short, one after
 * another, and nothing is ever heard to its end. Done about fifty-five times
 * the engine jumped into its own data and the process went with it, and for a
 * while before that every round cost a little more than the last.
 *
 * The cause was a thread. The engine has none of its own, so whichever thread
 * calls eciSynchronize is the one it runs on, and this bridge used to hand it a
 * thread made fresh for every utterance. It now keeps one. Three hundred rounds
 * is comfortably past where it used to fall over.
 */
@RunWith(AndroidJUnit4::class)
class InterruptEnduranceTest {

	@Test
	fun threeHundredInterruptionsLeaveTheEngineWorking() {
		val e = EvvEngine.open(ENGLISH)
		assertNotNull(e)
		try {
			e!!.setSampleRate(11025)
			e.applyVoice(0)
			val whole = wholeUtterance(e)
			assertTrue("nothing was said to begin with", whole > 0)
			val buffer = ByteArray(8192)
			for (round in 1..ROUNDS) {
				assertTrue("round $round was refused", e.speak(WORDS[round % WORDS.size]))
				e.read(buffer)
				e.stop()
			}
			// The engine going silent is the other way this failed, so asking
			// whether it still speaks matters as much as getting here at all.
			val after = wholeUtterance(e)
			assertTrue(
				"after $ROUNDS interruptions a whole utterance was worth $after, not $whole",
				after == whole
			)
		} finally {
			e?.close()
		}
	}

	private fun wholeUtterance(e: EvvEngine): Int {
		if (!e.speak("The quick brown fox.")) return -1
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
		const val ROUNDS = 300
		val WORDS = listOf("Paperback", "YouTube", "Messages", "GitHub", "Settings", "Calendar")
	}
}
