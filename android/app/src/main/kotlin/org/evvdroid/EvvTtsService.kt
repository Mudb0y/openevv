package org.evvdroid

import android.media.AudioFormat
import android.speech.tts.SynthesisCallback
import android.speech.tts.SynthesisRequest
import android.speech.tts.TextToSpeech
import android.speech.tts.TextToSpeechService
import android.speech.tts.Voice
import android.util.Log
import java.util.Locale

/**
 * The engine Android talks to.
 *
 * onSynthesizeText runs on the framework's own synthesis thread and is the
 * only place an utterance is driven; onStop arrives from another thread and
 * does nothing but set the engine's abort flag, which is what unblocks the
 * read below.
 */
class EvvTtsService : TextToSpeechService() {

	private var engine: EvvEngine? = null
	private var settings: Settings? = null

	@Volatile
	private var stopped = false

	private var appliedRevision = -1

	private var loadedDictionaries: Map<Int, String> = emptyMap()

	override fun onCreate() {
		settings = Settings(this)
		super.onCreate()
	}

	override fun onDestroy() {
		super.onDestroy()
		engine?.close()
		engine = null
	}

	// ---- languages -------------------------------------------------------

	private fun matchLanguage(lang: String?, country: String?): Pair<Int, Int>? {
		if (lang.isNullOrEmpty()) return null
		val wantLang = normalise(lang)
		val wantCountry = country?.takeIf { it.isNotEmpty() }?.let { normaliseCountry(it) }
		var byLanguage: Int? = null
		for (candidate in EvvEngine.available) {
			val loc = Eci.localeOf(candidate) ?: continue
			if (!loc.first.equals(wantLang, ignoreCase = true)) continue
			if (wantCountry != null && loc.second.equals(wantCountry, ignoreCase = true)) {
				return candidate to TextToSpeech.LANG_COUNTRY_AVAILABLE
			}
			if (byLanguage == null) byLanguage = candidate
		}
		return byLanguage?.let { it to TextToSpeech.LANG_AVAILABLE }
	}

	private fun normalise(lang: String): String =
		runCatching { Locale(lang).isO3Language }.getOrNull()?.takeIf { it.isNotEmpty() } ?: lang

	private fun normaliseCountry(country: String): String =
		runCatching { Locale("", country).isO3Country }.getOrNull()?.takeIf { it.isNotEmpty() } ?: country

	override fun onIsLanguageAvailable(lang: String?, country: String?, variant: String?): Int {
		val found = matchLanguage(lang, country) ?: return TextToSpeech.LANG_NOT_SUPPORTED
		return found.second
	}

	override fun onLoadLanguage(lang: String?, country: String?, variant: String?): Int {
		val found = matchLanguage(lang, country) ?: return TextToSpeech.LANG_NOT_SUPPORTED
		ensureEngine(found.first) ?: return TextToSpeech.LANG_NOT_SUPPORTED
		return found.second
	}

	override fun onGetLanguage(): Array<String> {
		val language = engine?.language ?: EvvEngine.available.firstOrNull() ?: return arrayOf("eng", "USA", "")
		val loc = Eci.localeOf(language) ?: return arrayOf("eng", "USA", "")
		return arrayOf(loc.first, loc.second, loc.third)
	}

	// ---- voices ----------------------------------------------------------

	private fun voiceName(language: Int, preset: Int): String {
		val loc = Eci.localeOf(language) ?: return "evv-$preset"
		return "${loc.first}-${loc.second}-${Eci.PRESET_NAMES[preset]}"
	}

	override fun onGetVoices(): MutableList<Voice> {
		val out = ArrayList<Voice>()
		for (language in EvvEngine.available) {
			val loc = Eci.localeOf(language) ?: continue
			val locale = Locale(loc.first, loc.second)
			for (preset in Eci.PRESET_NAMES.indices) {
				out.add(
					Voice(
						voiceName(language, preset),
						locale,
						Voice.QUALITY_NORMAL,
						Voice.LATENCY_VERY_LOW,
						false,
						emptySet()
					)
				)
			}
		}
		return out
	}

	private fun findVoice(name: String?): Pair<Int, Int>? {
		if (name.isNullOrEmpty()) return null
		for (language in EvvEngine.available) {
			for (preset in Eci.PRESET_NAMES.indices) {
				if (voiceName(language, preset) == name) return language to preset
			}
		}
		return null
	}

	override fun onIsValidVoiceName(name: String?): Int =
		if (findVoice(name) != null) TextToSpeech.SUCCESS else TextToSpeech.ERROR

	override fun onLoadVoice(name: String?): Int {
		val found = findVoice(name) ?: return TextToSpeech.ERROR
		ensureEngine(found.first) ?: return TextToSpeech.ERROR
		return TextToSpeech.SUCCESS
	}

	override fun onGetDefaultVoiceNameFor(lang: String?, country: String?, variant: String?): String? {
		val found = matchLanguage(lang, country) ?: return null
		return voiceName(found.first, settings?.voice ?: 0)
	}

	// ---- speaking --------------------------------------------------------

	private fun ensureEngine(language: Int): EvvEngine? {
		val held = engine
		if (held != null && held.language == language) return held
		held?.close()
		val made = EvvEngine.open(language)
		engine = made
		if (made != null) applySettings(made)
		return made
	}

	private fun applySettings(target: EvvEngine) {
		val s = settings ?: return
		target.setSampleRate(s.sampleRateHz)
		target.setAbbreviations(s.abbreviations)
		loadDictionaries(target, s)
		target.applyVoice(s.voice, s.shape(s.voice))
		appliedRevision = s.revision
	}

	/** Teaching three thousand words costs about half a second, so it happens
	 *  when the files change rather than whenever any setting does. */
	private fun loadDictionaries(target: EvvEngine, s: Settings) {
		val want = s.dictionaryPaths()
		if (want == loadedDictionaries) return
		target.forgetDictionaries()
		for ((volume, path) in want) {
			val file = java.io.File(path)
			if (!file.exists()) continue
			val taught = Dictionaries.load(target, volume, file)
			Log.i(TAG, "volume $volume: $taught entries from ${file.name}")
		}
		loadedDictionaries = want
	}

	override fun onStop() {
		stopped = true
		engine?.stop()
	}

	override fun onSynthesizeText(request: SynthesisRequest?, callback: SynthesisCallback?) {
		if (request == null || callback == null) return
		stopped = false
		val found = matchLanguage(request.language, request.country)
		if (found == null) {
			callback.error(TextToSpeech.ERROR_NOT_INSTALLED_YET)
			return
		}
		// The voice is whichever the settings screen says, not whichever the
		// caller named. A caller holds on to the Voice it was given, so a
		// screen reader that connected while Reed was chosen goes on asking
		// for Reed however many times the setting is changed underneath it.
		// The name still decides the language, since that is what it is for.
		val wanted = findVoice(request.voiceName)
		val language = wanted?.first ?: found.first
		val target = ensureEngine(language)
		if (target == null) {
			callback.error(TextToSpeech.ERROR_SERVICE)
			return
		}
		val s = settings
		if (s != null && s.revision != appliedRevision) applySettings(target)
		// Re-sent every time rather than once, because it is the one setting
		// with no way of telling whether something else has moved it.
		s?.let { target.setAbbreviations(it.abbreviations) }
		target.setRatePercent(request.speechRate)
		target.setPitchPercent(request.pitch)
		val text = request.charSequenceText?.toString().orEmpty()
		if (text.isEmpty()) {
			callback.start(target.sampleRateHz, AudioFormat.ENCODING_PCM_16BIT, 1)
			callback.done()
			return
		}
		if (callback.start(target.sampleRateHz, AudioFormat.ENCODING_PCM_16BIT, 1) != TextToSpeech.SUCCESS) {
			return
		}
		val pace = Pace(target.sampleRateHz * BYTES_PER_SAMPLE)
		// Piece by piece, so that asking for silence waits out a piece rather
		// than the whole message: the engine cannot abandon what it is saying.
		// Pauses are shortened after the split rather than before it: the
		// annotation sits in front of the full stop, and a piece boundary is
		// found by looking at the end of a word.
		val pauses = settings?.pauses ?: Pauses.ALL
		val prosody = Prosody.prefix(settings?.phrasePrediction ?: false)
		val pieces = TextPieces.split(text)
		for ((at, raw) in pieces.withIndex()) {
			if (stopped) break
			val piece = prosody + Pauses.apply(raw, pauses, at == pieces.lastIndex)
			if (!target.speak(piece)) {
				Log.e(TAG, "the engine refused ${piece.length} characters")
				callback.error(TextToSpeech.ERROR_SYNTHESIS)
				return
			}
			if (!pump(target, callback, pace)) {
						remember(pace)
				return
			}
		}
		remember(pace)
		callback.done()
	}

	/** Hands one piece's samples on as they arrive, and answers whether the
	 *  caller may go on to the next. */
	private fun pump(target: EvvEngine, callback: SynthesisCallback, pace: Pace): Boolean {
		val size = callback.maxBufferSize.coerceIn(MIN_CHUNK, MAX_CHUNK)
		val buffer = ByteArray(size)
		while (!stopped) {
			val n = target.read(buffer)
			if (n <= 0) break
			if (callback.audioAvailable(buffer, 0, n) != TextToSpeech.SUCCESS) {
				// The framework has torn the callback down already, so done()
				// is neither wanted nor listened to.
				target.stop()
				return false
			}
			pace.handed(n)
			hold(pace)
		}
		return true
	}

	private fun remember(pace: Pace) {
		if (pace.handedMs() > mostAudioHandedMs) mostAudioHandedMs = pace.handedMs()
	}

	/**
	 * Waits until the audio already handed over has nearly caught up.
	 *
	 * This engine is far faster than the speech it makes -- a second and a
	 * third of audio comes out in twelve milliseconds -- and Android takes
	 * every byte of it without complaint and queues it. So a reader swiping
	 * quickly has five or six items synthesised and queued while the first is
	 * still being spoken, and when it then asks for silence the whole queue
	 * goes, which is items that were never heard at all. Running no further
	 * ahead than [LEAD_MS] keeps at most one utterance in flight, so silence
	 * arrives where the reader actually is.
	 *
	 * The wait is in slices so that a stop is noticed at once rather than at
	 * the end of it.
	 */
	private fun hold(pace: Pace) {
		while (!stopped) {
			val ahead = pace.ahead() - LEAD_MS
			if (ahead <= 0) return
			try {
				Thread.sleep(minOf(ahead, SLICE_MS))
			} catch (stopping: InterruptedException) {
				Thread.currentThread().interrupt()
				return
			}
		}
	}

	/** How far the audio handed over has run ahead of the time it takes to
	 *  play it. */
	private class Pace(private val bytesPerSecond: Int) {
		private val from = System.nanoTime()
		private var bytes = 0L

		fun handed(more: Int) {
			bytes += more
		}

		fun handedMs(): Long = if (bytesPerSecond <= 0) 0 else bytes * 1000L / bytesPerSecond

		fun ahead(): Long = handedMs() - (System.nanoTime() - from) / 1_000_000L
	}

	internal companion object {
		/** The most audio, in milliseconds, that any one utterance has handed
		 *  over since this was last cleared. Nothing in the app reads it. It is
		 *  what lets a test say the engine is not running ahead of the speech,
		 *  which is invisible from outside: an engine that queues six items in
		 *  a tenth of a second and one that paces itself look the same to the
		 *  framework and sound completely different to a person. */
		@Volatile
		var mostAudioHandedMs: Long = 0

		const val TAG = "evvdroid"
		const val MIN_CHUNK = 1024
		const val MAX_CHUNK = 8192
		const val BYTES_PER_SAMPLE = 2
		/** How much audio may sit ahead of the speech, so that the track never
		 *  runs dry while the engine is held back. */
		const val LEAD_MS = 300L
		const val SLICE_MS = 50L
	}
}
