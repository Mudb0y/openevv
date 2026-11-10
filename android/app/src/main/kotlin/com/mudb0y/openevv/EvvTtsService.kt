package com.mudb0y.openevv

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

	@Volatile
	private var engine: EvvEngine? = null
	private var settings: Settings? = null

	@Volatile
	private var stopped = false

	@Volatile
	private var utterance = 0

	private var appliedRevision = -1

	private data class LoadedDictState(val path: String, val lastModified: Long, val length: Long)
	private var loadedDictStates: Map<Int, LoadedDictState> = emptyMap()
	@Volatile
	private var activeCaseSensitiveEntries: List<Dictionaries.CachedCaseSensitiveEntry> = emptyList()

	override fun onCreate() {
		settings = Settings(this)
		super.onCreate()
	}

	override fun onDestroy() {
		Log.w(TAG, "service destroyed after $utterance utterances")
		super.onDestroy()
		engine?.close()
		engine = null
	}

	// ---- languages -------------------------------------------------------

	override fun onIsLanguageAvailable(lang: String?, country: String?, variant: String?): Int {
		val found = Languages.match(lang, country) ?: return TextToSpeech.LANG_NOT_SUPPORTED
		return found.second
	}

	override fun onLoadLanguage(lang: String?, country: String?, variant: String?): Int {
		val found = Languages.match(lang, country) ?: return TextToSpeech.LANG_NOT_SUPPORTED
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
		val found = Languages.match(lang, country) ?: return null
		return voiceName(found.first, settings?.voice ?: 0)
	}

	// ---- speaking --------------------------------------------------------

	private fun ensureEngine(language: Int): EvvEngine? {
		val held = engine
		if (held != null && held.language == language) return held
		held?.close()
		loadedDictStates = emptyMap()
		val made = EvvEngine.open(language)
		engine = made
		if (made != null) applySettings(made)
		return made
	}

	private fun applySettings(target: EvvEngine) {
		val s = settings ?: return
		target.setSampleRate(s.sampleRateHz)
		target.setWideband(s.wideband)
		target.setPauseMode(s.pauses)
		target.setAbbreviations(s.abbreviations)
		loadDictionaries(target, s)
		// A voice is written, so the engine can refuse it while it is speaking.
		// One that did not land is left unrecorded and asked for again at the
		// next utterance rather than believed.
		if (target.applyVoice(s.voice, s.shape(s.voice))) appliedRevision = s.revision
	}

	/** Teaching words happens when the files change rather than whenever any setting does. */
	private fun loadDictionaries(target: EvvEngine, s: Settings) {
		val wantPaths = s.dictionaryPaths().filterValues { java.io.File(it).exists() }
		val currentStates = wantPaths.mapValues { (_, path) ->
			val f = java.io.File(path)
			LoadedDictState(path, f.lastModified(), f.length())
		}
		if (currentStates == loadedDictStates) return

		Log.i(TAG, "Reloading dictionaries because files or settings changed...")
		target.forgetDictionaries()
		val csList = mutableListOf<Dictionaries.CachedCaseSensitiveEntry>()
		for ((volume, path) in wantPaths) {
			val file = java.io.File(path)
			val (taught, entries) = Dictionaries.loadWithCaseSensitive(target, volume, file)
			csList.addAll(entries)
			Log.i(TAG, "volume $volume: $taught active entries from ${file.name}")
		}
		activeCaseSensitiveEntries = csList
		loadedDictStates = currentStates
	}

	override fun onStop() {
		Log.i(TAG, "#$utterance onStop")
		stopped = true
		engine?.stop()
	}

	override fun onSynthesizeText(request: SynthesisRequest?, callback: SynthesisCallback?) {
		if (request == null || callback == null) return
		val id = ++utterance
		val began = System.nanoTime()
		Log.i(TAG, "#$id begin voice=${request.voiceName} lang=${request.language}-${request.country} rate=${request.speechRate} pitch=${request.pitch} chars=${request.charSequenceText?.length}")
		try {
			synthesize(id, request, callback)
		} catch (t: Throwable) {
			Log.e(TAG, "#$id threw after ${(System.nanoTime() - began) / 1_000_000}ms", t)
			throw t
		}
		Log.i(TAG, "#$id end after ${(System.nanoTime() - began) / 1_000_000}ms stopped=$stopped")
	}

	private fun synthesize(id: Int, request: SynthesisRequest, callback: SynthesisCallback) {
		stopped = false
		val found = Languages.match(request.language, request.country)
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
		if (s != null) {
			if (s.revision != appliedRevision) {
				applySettings(target)
			} else {
				loadDictionaries(target, s)
			}
		}
		// Re-sent every time rather than once. A setting is written rather than
		// annotated, so the engine refuses one while it is still finishing the
		// utterance before, and nothing says whether it landed.
		s?.let {
			target.setAbbreviations(it.abbreviations)
			target.setPauseMode(it.pauses)
			target.setWideband(it.wideband)
		}
		target.setRatePercent(request.speechRate)
		target.setPitchPercent(request.pitch)
		// What TalkBack has put into words is read in place of the marks it
		// names, and a word the listener's own dictionary holds by its case is
		// put the way they wrote it. Everything else is the engine's to read.
		val charSeq = request.charSequenceText
		var text = TalkBackSpans.expand(charSeq) ?: charSeq?.toString().orEmpty()
		val cs = activeCaseSensitiveEntries
		if (cs.isNotEmpty()) text = Dictionaries.applyCaseSensitiveDict(text, cs)
		if (text.isEmpty()) {
			callback.start(target.sampleRateHz, AudioFormat.ENCODING_PCM_16BIT, 1)
			callback.done()
			return
		}
		// The framework is not told an utterance has begun until there are
		// samples for it. Saying so up front and then being silenced before the
		// engine has made anything hands the reader a whole start and finish
		// with no speech in it, and TalkBack reads that as its queue running
		// dry: it answers "Completed speech while already completed", stops the
		// queue, and the item the finger is actually on dies with it. That is
		// what made dragging across a launcher go quiet. RHVoice opens the same
		// way, from inside its player rather than ahead of it.
		val opening = Opening(callback, target.sampleRateHz)
		val pace = Pace(target.sampleRateHz * BYTES_PER_SAMPLE)
		// Piece by piece, so that asking for silence waits out a piece rather
		// than the whole message: the engine cannot abandon what it is saying.
		val prosody = Prosody.prefix(settings?.phrasePrediction ?: false)
		val pieces = TextPieces.split(text)
		Log.i(TAG, "#$id ${text.length} chars in ${pieces.size} pieces")
		for ((at, raw) in pieces.withIndex()) {
			if (stopped) {
				Log.i(TAG, "#$id stopped before piece $at")
				break
			}
			val piece = prosody + raw
			if (!target.speak(piece)) {
				Log.e(TAG, "#$id the engine refused ${piece.length} characters")
				Log.e(TAG, "#$id refused piece $at: ${piece.take(200)}")
				callback.error(TextToSpeech.ERROR_SYNTHESIS)
				return
			}
			// stop may have arrived while speak waited for the previous worker.
			// speak resets native cancellation when starting the new piece;
			// abort it again rather than leave an undrained worker behind.
			if (stopped) {
				target.stop()
				break
			}
			if (!pump(id, target, callback, pace, opening)) {
				Log.w(TAG, "#$id pump gave up at piece $at of ${pieces.size}")
				remember(pace)
				return
			}
		}
		remember(pace)
		Log.i(TAG, "#$id finished, handed ${pace.handedMs()}ms began=${opening.began}")
		if (!opening.began && !stopped) {
			Log.w(TAG, "#$id the engine said nothing for: ${text.take(200)}")
			opening.open()
		}
		if (opening.began) callback.done()
	}

	/** Hands one piece's samples on as they arrive, and answers whether the
	 *  caller may go on to the next. */
	private fun pump(id: Int, target: EvvEngine, callback: SynthesisCallback, pace: Pace, opening: Opening): Boolean {
		val size = callback.maxBufferSize.coerceIn(MIN_CHUNK, MAX_CHUNK)
		val buffer = ByteArray(size)
		var chunks = 0
		while (!stopped) {
			val n = target.read(buffer)
			if (n <= 0) {
				Log.i(TAG, "#$id read answered $n after $chunks chunks")
				break
			}
			chunks++
			if (!opening.open()) {
				Log.w(TAG, "#$id the framework refused start()")
				target.stop()
				return false
			}
			val delivered = callback.audioAvailable(buffer, 0, n)
			if (delivered != TextToSpeech.SUCCESS) {
				Log.w(TAG, "#$id audioAvailable answered $delivered after $chunks chunks")
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

	/** Tells the framework an utterance has begun, once, and not before there
	 *  is something to hear. [began] answers whether it was ever told, which is
	 *  what decides whether it is owed a finish. */
	private class Opening(private val callback: SynthesisCallback, private val rateHz: Int) {
		var began = false
			private set

		fun open(): Boolean {
			if (began) return true
			began = callback.start(rateHz, AudioFormat.ENCODING_PCM_16BIT, 1) == TextToSpeech.SUCCESS
			return began
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

		const val TAG = "openevv"
		const val MIN_CHUNK = 1024
		const val MAX_CHUNK = 8192
		const val BYTES_PER_SAMPLE = 2
		/** How much audio may sit ahead of the speech, so that the track never
		 *  runs dry while the engine is held back. */
		const val LEAD_MS = 300L
		const val SLICE_MS = 50L
	}
}
