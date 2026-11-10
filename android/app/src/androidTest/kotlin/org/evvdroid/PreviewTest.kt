package org.evvdroid

import org.junit.Assert.assertTrue
import org.junit.Test

/** That the settings screen's sample button really makes a sound.
 *
 *  A preview that quietly plays nothing looks exactly like one that works, so
 *  nothing else here would catch it. The trap is the order: stopping whatever
 *  is speaking is what moves the turn number on, so a preview that takes its
 *  turn number first is stale before it begins. */
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

	/**
	 * And it speaks the language the screen last chose.
	 *
	 * An instance is made for one language and cannot be moved, so changing
	 * the row puts one engine down and opens another. What that can get wrong
	 * is silent: the old instance goes on speaking, or the new one is never
	 * opened and the button does nothing at all.
	 */
	@Test
	fun itSpeaksWhicheverLanguageWasChosen() {
		val preview = EvvPreview(ENGLISH)
		try {
			preview.say("Testing one two three.", 0, emptyMap(), 11025)
			waitForSomething(preview)
			val english = preview.bytesPlayed
			assertTrue("the preview played nothing in English", english > 0)
			preview.useLanguage(GERMAN)
			preview.say("Dies ist eine Probe.", 0, emptyMap(), 11025)
			waitForMoreThan(preview, english)
			assertTrue("nothing was played after the language changed",
				preview.bytesPlayed > english)
		} finally {
			preview.close()
		}
	}

	private fun waitForSomething(preview: EvvPreview) = waitForMoreThan(preview, 0)

	private fun waitForMoreThan(preview: EvvPreview, than: Long) {
		val until = System.currentTimeMillis() + WAIT_MS
		while (preview.bytesPlayed <= than && System.currentTimeMillis() < until) {
			Thread.sleep(50)
		}
	}

	private companion object {
		const val ENGLISH = 0x00010000
		const val GERMAN = 0x00040000
		const val WAIT_MS = 15000
	}
}
