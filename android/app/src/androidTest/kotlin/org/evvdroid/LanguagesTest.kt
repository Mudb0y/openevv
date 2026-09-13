package org.evvdroid

import androidx.test.ext.junit.runners.AndroidJUnit4
import java.nio.charset.Charset
import org.junit.After
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Before
import org.junit.Test
import org.junit.runner.RunWith

/**
 * Every language the build has, opened and spoken in.
 *
 * A language linked in but silent looks exactly like one that works from
 * outside: eciGetAvailableLanguages answers with it either way, and the
 * settings screen lists it either way. So each one says a sentence of its own
 * here and the samples are looked at.
 */
@RunWith(AndroidJUnit4::class)
class LanguagesTest {

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

	/** A build made with evvdroid.langs cut down fails this one, and is meant
	 *  to: it says what the default build has in it. */
	@Test
	fun theBuildHasEveryModule() {
		val have = EvvEngine.available.toSet()
		for (language in SENTENCES.keys) {
			assertTrue(
				"0x%08x is not in the build, which has %s".format(
					language, have.joinToString { "0x%08x".format(it) }
				),
				have.contains(language)
			)
		}
	}

	/** Android is told about a language by locale, so one the engine offers and
	 *  Eci has no locale for would be offered as nothing at all. */
	@Test
	fun everyLanguageOfferedIsOneAndroidCanBeTold() {
		for (language in EvvEngine.available) {
			assertNotNull("no locale for 0x%08x".format(language), Eci.localeOf(language))
		}
	}

	@Test
	fun everyLanguageSpeaksItsOwnSentence() {
		for (language in EvvEngine.available) {
			val sentence = SENTENCES[language] ?: continue
			val e = open(language)
			val pcm = collect(e, sentence)
			assertTrue(
				"0x%08x gave only %d bytes".format(language, pcm.size),
				pcm.size > 8000
			)
			assertTrue("0x%08x spoke silence".format(language), peak(pcm) > 1000)
			e.close()
			engine = null
		}
	}

	/** Polish declares eight letters of its own and the engine converts its
	 *  text from UTF-8 to reach them. Everything else in the line is still
	 *  flattened, which is what the quotation marks and the dash say. */
	@Test
	fun polishKeepsItsOwnLettersAndFlattensTheRest() {
		assertEquals(POLISH_SENTENCE, utf8(EngineText.encode(Eci.POLISH, POLISH_SENTENCE)))
		assertEquals(
			"\"tak\" - \u017caba",
			utf8(EngineText.encode(Eci.POLISH, "\u201etak\u201d \u2014 \u017caba"))
		)
		assertEquals("Dvor\u00e1k", utf8(EngineText.encode(Eci.POLISH, "Dvo\u0159\u00e1k")))
	}

	/** Japanese has a romanizer in front of the engine and it reads Shift-JIS.
	 *  A character it has no room for costs a space rather than a word. */
	@Test
	fun japaneseGoesOutAsShiftJis() {
		val bytes = EngineText.encode(Eci.JAPANESE, JAPANESE_SENTENCE)
		assertEquals(JAPANESE_SENTENCE, String(bytes, Charset.forName("Shift_JIS")))
		assertTrue("a nought byte would end the text early", bytes.none { it.toInt() == 0 })
		assertEquals(
			"a b",
			String(EngineText.encode(Eci.JAPANESE, "a\uD83D\uDE00b"), Charsets.US_ASCII)
		)
	}

	/** And every other language is still a byte a character in the Western
	 *  set, which is the path nothing above was allowed to disturb. */
	@Test
	fun theRestAreStillWesternBytes() {
		assertEquals(FRENCH_SENTENCE, latin1(EngineText.encode(FRENCH, FRENCH_SENTENCE)))
		assertEquals("caf\u00e9", latin1(EngineText.encode(FRENCH, "caf\u00e9")))
		assertEquals("Dvor\u00e1k", latin1(EngineText.encode(FRENCH, "Dvo\u0159\u00e1k")))
	}

	private fun open(language: Int): EvvEngine {
		val made = EvvEngine.open(language)
		assertNotNull("eciNewEx refused 0x%08x".format(language), made)
		engine = made
		made!!.setSampleRate(11025)
		made.applyVoice(0)
		return made
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

	private fun latin1(bytes: ByteArray) = String(bytes, Charsets.ISO_8859_1)

	private fun utf8(bytes: ByteArray) = String(bytes, Charsets.UTF_8)

	private companion object {
		const val FRENCH = 0x00030000
		const val FRENCH_SENTENCE = "Bonjour, ici Eloquence."
		const val POLISH_SENTENCE = "Dzie\u0144 dobry, m\u00f3wi Eloquence."
		const val JAPANESE_SENTENCE =
			"\u3053\u3093\u306b\u3061\u306f\u3002\u30a4\u30ed\u30af\u30a8\u30f3\u30b9\u3067\u3059\u3002"

		/** One sentence per module, written in the language. A sentence in
		 *  another language would still make samples, so it would say nothing
		 *  about whether this module is the one speaking. */
		val SENTENCES = mapOf(
			0x00010000 to "Hello from Eloquence.",
			0x00010001 to "Hello from Eloquence.",
			0x00020000 to "Hola, aqu\u00ed habla Eloquence.",
			0x00020001 to "Hola, aqu\u00ed habla Eloquence.",
			0x00030000 to FRENCH_SENTENCE,
			0x00030001 to FRENCH_SENTENCE,
			0x00040000 to "Hallo, hier spricht Eloquence.",
			0x00050000 to "Ciao, qui parla Eloquence.",
			0x00080000 to JAPANESE_SENTENCE,
			0x00110000 to POLISH_SENTENCE
		)
	}
}
