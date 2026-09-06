package org.evvdroid

import org.junit.Assert.assertTrue
import org.junit.Test

/** That the settings screen's sample button really makes a sound.
 *
 *  It once did not, and nothing said so: the preview took its turn number
 *  before stopping whatever was speaking, stopping is what moves that number
 *  on, and so every sample was discarded as stale before it began. A preview
 *  that quietly plays nothing looks exactly like one that works. */
class PreviewTest {

	@Test
	fun theSampleReachesTheAudioTrack() {
		val preview = EvvPreview(ENGLISH)
		try {
			preview.say("Testing one two three.", 0, emptyMap(), 11025)
			val until = System.currentTimeMillis() + WAIT_MS
			while (preview.bytesPlayed == 0L && System.currentTimeMillis() < until) {
				Thread.sleep(50)
			}
			assertTrue("the preview played nothing", preview.bytesPlayed > 0)
		} finally {
			preview.close()
		}
	}

	/**
	 * And all of it is heard, not most of it.
	 *
	 * The track holds the best part of a second. Stopping it lets that play
	 * out, but releasing it does not, so releasing straight after stopping cut
	 * the last few words off every sample and nothing said so: every byte had
	 * been handed over, so the counts all looked right.
	 */
	@Test
	fun theEndOfTheSampleIsHeard() {
		val preview = EvvPreview(ENGLISH)
		try {
			preview.say(
				"This is a test of the current voice, and the end of it matters.",
				0, emptyMap(), 11025
			)
			val until = System.currentTimeMillis() + WAIT_MS
			while (!preview.playing && System.currentTimeMillis() < until) Thread.sleep(20)
			while (preview.playing && System.currentTimeMillis() < until) Thread.sleep(20)
			assertTrue("the sample never finished", !preview.playing)
			assertTrue("the tail was cut off", preview.drained)
		} finally {
			preview.close()
		}
	}

	private companion object {
		const val ENGLISH = 0x00010000
		const val WAIT_MS = 15000
	}
}
