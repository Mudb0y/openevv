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

	private companion object {
		const val ENGLISH = 0x00010000
		const val WAIT_MS = 8000
	}
}
