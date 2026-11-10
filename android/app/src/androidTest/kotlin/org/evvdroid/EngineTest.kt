package org.evvdroid

import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/** The engine on its own, with nothing of Android's TTS framework in the way.
 *  Everything here fails loudly rather than quietly speaking silence, which is
 *  the failure this whole port is most likely to produce. */
@RunWith(AndroidJUnit4::class)
class EngineTest {

	private var engine: EvvEngine? = null

	@Before
	fun setUp() {
		assertTrue("the native library did not load: ${EvvNative.loadError}", EvvNative.loaded)
	}

	@After
	fun tearDown() {
		engine?.close()
		engine = null
	}

	@Test
	fun theBuildHasALanguage() {
		val langs = EvvEngine.available
		assertTrue("no language module is linked in", langs.isNotEmpty())
		assertTrue(
			"US English is missing, found ${langs.joinToString { "0x%08x".format(it) }}",
			langs.contains(ENGLISH)
		)
	}

	@Test
	fun theEngineReportsAVersion() {
		assertTrue("no version string", EvvEngine.version.isNotEmpty())
	}

	@Test
	fun anInstanceOpens() {
		assertNotNull(open())
	}

	@Test
	fun itSpeaksAndTheSamplesAreNotSilence() {
		val e = open()
		e.setSampleRate(11025)
		val pcm = collect(e, "Hello from Eloquence.")
		assertTrue("only ${pcm.size} bytes came out", pcm.size > 11025)
		assertTrue("the samples are all zero", peak(pcm) > 1000)
	}

	/** The one check that catches a drive loop which ends an utterance early,
	 *  and a text path that hands the engine only its first character: eight
	 *  times the text has to give roughly eight times the audio. An absolute
	 *  length would only say what this engine happens to sound like. */
	@Test
	fun theAudioGrowsWithTheText() {
		val e = open()
		val one = collect(e, SENTENCE).size
		val eight = collect(e, SENTENCE.repeat(8)).size
		assertTrue("one sentence gave only $one bytes", one > 11025)
		val ratio = eight.toDouble() / one
		assertTrue("eight sentences gave $ratio times one, not about eight", ratio in 6.5..9.5)
	}

	@Test
	fun aLongerUtteranceRunsPastOneRingful() {
		val e = open()
		val pcm = collect(e, SENTENCE.repeat(12))
		// The ring holds 128 kilobytes, so this proves the engine's thread and
		// the reader hand over more than one ringful without deadlocking.
		assertTrue("only ${pcm.size} bytes came out", pcm.size > 128 * 1024)
	}

	@Test
	fun speakingTwiceInARowWorks() {
		val e = open()
		val first = collect(e, "One two three.")
		val second = collect(e, "One two three.")
		assertTrue("nothing came out the first time", first.isNotEmpty())
		assertEquals("the same sentence gave a different length", first.size, second.size)
	}

	@Test
	fun stoppingEndsTheUtterance() {
		val e = open()
		assertTrue(e.speak(SENTENCE.repeat(20)))
		val buffer = ByteArray(4096)
		assertTrue("nothing came out before the stop", e.read(buffer) > 0)
		val started = System.currentTimeMillis()
		e.stop()
		// A stop waits for the engine to finish the message it is on, which is
		// tens of milliseconds with the rules compiled, not seconds.
		val took = System.currentTimeMillis() - started
		assertTrue("the stop took ${took}ms", took < 2000)
		assertEquals("the reader kept going after a stop", -1, e.read(buffer))
	}

	@Test
	fun everyPresetVoiceSpeaks() {
		val e = open()
		for (preset in Eci.PRESET_NAMES.indices) {
			e.applyVoice(preset)
			val pcm = collect(e, "Testing one two three.")
			assertTrue("${Eci.PRESET_NAMES[preset]} gave only ${pcm.size} bytes", pcm.size > 8000)
		}
	}

	/**
	 * A voice keeps its own settings when nobody has chosen otherwise.
	 *
	 * This is the one that matters: every preset carries its own head size,
	 * inflection and volume -- Reed speaks at inflection 30 and volume 92 --
	 * and a default of ours written over the top made every voice wrong in the
	 * same way. It was audible as Eloquence at the wrong inflection.
	 */
	@Test
	fun aVoiceKeepsItsOwnSettingsWhenNothingIsChosen() {
		val e = open()
		for (preset in Eci.PRESET_NAMES.indices) {
			val own = e.presetShape(preset)
			assertTrue("preset $preset reported nothing", own.isNotEmpty())
			e.applyVoice(preset)
			for ((param, value) in own) {
				// Speed is the one a preset does not bring with it. See
				// everyVoiceSpeaksAtTheSamePace.
				if (param == Eci.VOICE_SPEED) continue
				assertEquals(
					"${Eci.PRESET_NAMES[preset]} parameter $param",
					value,
					e.getVoiceParam(param)
				)
			}
		}
	}

	/**
	 * Changing voice must not change the pace.
	 *
	 * Six of the eight presets ship at speed 50 and Glen and Sandy ship at 70,
	 * which is half again as fast, so a speed that followed the voice would
	 * speed everything up by itself on those two. Speed is the listener's
	 * setting, and the only one of the eight a preset does not bring with it.
	 */
	@Test
	fun everyVoiceSpeaksAtTheSamePace() {
		val e = open()
		e.setSampleRate(11025)
		val lengths = LinkedHashMap<String, Int>()
		for (preset in Eci.PRESET_NAMES.indices) {
			e.applyVoice(preset)
			e.setRatePercent(100)
			lengths[Eci.PRESET_NAMES[preset]] = collect(e, SENTENCE).size
		}
		assertEquals("the voices ran at different paces: $lengths", 1, lengths.values.toSet().size)
	}

	/** And a speed that was chosen is still obeyed. */
	@Test
	fun aChosenSpeedSurvivesTheVoice() {
		val e = open()
		e.setSampleRate(11025)
		e.applyVoice(0)
		e.setRatePercent(100)
		val ordinary = collect(e, SENTENCE).size
		e.applyVoice(0, mapOf(Eci.VOICE_SPEED to 100))
		e.setRatePercent(100)
		val quicker = collect(e, SENTENCE).size
		assertTrue("a chosen speed of 100 was ignored", quicker < ordinary / 2)
	}

	/** And the presets really are different, so the check above is not passing
	 *  on eight copies of one voice. */
	@Test
	fun thePresetsDifferFromEachOther() {
		val e = open()
		val shapes = Eci.PRESET_NAMES.indices.map { e.presetShape(it) }
		assertEquals(Eci.PRESET_NAMES.size, shapes.toSet().size)
	}

	@Test
	fun aChosenSettingIsLaidOverThePresetAndTheRestIsLeftAlone() {
		val e = open()
		val own = e.presetShape(0)
		e.applyVoice(0, mapOf(Eci.VOICE_VOLUME to 40))
		assertEquals("the chosen setting did not take", 40, e.getVoiceParam(Eci.VOICE_VOLUME))
		assertEquals(
			"a setting nobody chose moved",
			own[Eci.VOICE_PITCH_FLUCTUATION],
			e.getVoiceParam(Eci.VOICE_PITCH_FLUCTUATION)
		)
	}

	/**
	 * Android's speech rate and pitch are percentages of normal, and normal is
	 * the voice as it stands. A hundred per cent has to change nothing.
	 *
	 * The doubled rate is heard rather than read back out of the voice, because
	 * neither goes into the voice any more: both are sent in front of the words
	 * for the reason Prosody gives. So the voice standing still under an ask is
	 * part of what this says now rather than something it works around.
	 */
	@Test
	fun aRateOfOneHundredPerCentChangesNothing() {
		val e = open()
		e.setSampleRate(11025)
		e.applyVoice(0)
		val speed = e.getVoiceParam(Eci.VOICE_SPEED)
		val pitch = e.getVoiceParam(Eci.VOICE_PITCH_BASELINE)
		e.setRatePercent(100)
		e.setPitchPercent(100)
		assertEquals("the speed moved", speed, e.getVoiceParam(Eci.VOICE_SPEED))
		assertEquals("the pitch moved", pitch, e.getVoiceParam(Eci.VOICE_PITCH_BASELINE))
		val ordinary = collect(e, SENTENCE).size
		e.setRatePercent(200)
		val quicker = collect(e, SENTENCE).size
		assertTrue("a doubled rate did nothing: $quicker against $ordinary", quicker < ordinary)
	}

	/** A Latin-1 character is in the engine's own code set and goes straight
	 *  through; anything above it has been turned into something that is. In
	 *  both cases the rest of the sentence still has to be spoken. */
	@Test
	fun textOutsideAsciiStillSpeaksTheWholeSentence() {
		val e = open()
		val plain = collect(e, "The cafe in Prague is open today.").size
		assertTrue("the plain sentence gave only $plain bytes", plain > 11025)
		for (awkward in AWKWARD) {
			val n = collect(e, awkward).size
			val ratio = n.toDouble() / plain
			assertTrue("\"$awkward\" gave $ratio of the plain length", ratio in 0.5..2.0)
		}
	}

	/** The words a misresolved language store silenced. Each of these gave a
	 *  wave file with no samples in it until native/patches/0001 went in, and
	 *  every one of them is ordinary enough that a screen reader says it in the
	 *  first minute. */
	@Test
	fun theWordsTheStoreLookupSilencedSpeak() {
		val e = open()
		for (text in SILENCED) {
			val pcm = collect(e, text)
			assertTrue("\"$text\" gave only ${pcm.size} bytes", pcm.size > 8000)
		}
	}

	@Test
	fun theEncoderKeepsTheWordsAndLosesNothingThatMatters() {
		assertEquals(
			"It's a \"test\" - really...",
			latin1(WesternText.encode("It’s a “test” — really…"))
		)
		assertEquals("Dvorák", latin1(WesternText.encode("Dvořák")))
		assertEquals("café", latin1(WesternText.encode("café")))
		assertTrue(
			"a nought byte would end the text early",
			WesternText.encode("a b").none { it.toInt() == 0 }
		)
	}

	private fun latin1(bytes: ByteArray) = String(bytes, Charsets.ISO_8859_1)

	private fun open(): EvvEngine {
		val made = EvvEngine.open(ENGLISH)
		assertNotNull("eciNewEx refused US English", made)
		engine = made
		return made!!
	}

	private fun collect(e: EvvEngine, text: String): ByteArray {
		assertTrue("the engine refused the text", e.speak(text))
		val out = java.io.ByteArrayOutputStream()
		val buffer = ByteArray(4096)
		while (true) {
			val n = e.read(buffer)
			if (n <= 0) break
			out.write(buffer, 0, n)
		}
		return out.toByteArray()
	}

	private fun peak(pcm: ByteArray): Int {
		var top = 0
		var i = 0
		while (i + 1 < pcm.size) {
			val sample = ((pcm[i + 1].toInt() shl 8) or (pcm[i].toInt() and 0xFF)).toShort().toInt()
			val size = if (sample < 0) -sample else sample
			if (size > top) top = size
			i += 2
		}
		return top
	}

	private companion object {
		const val ENGLISH = 0x00010000
		const val SENTENCE = "The quick brown fox jumps over the lazy dog. "
		val SILENCED = listOf(
			"seven.",
			"eight.",
			"One two three four five six seven eight.",
			"The cafe in Prague is open today."
		)
		val AWKWARD = listOf(
			"The café in Prague is open today.",
			"The café in Prague is open — today.",
			"The cafe in Dvořák street is open today.",
			"The cafe in Prague is open today 😀"
		)
	}
}
