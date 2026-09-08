package org.evvdroid

import android.util.Log

/**
 * One engine instance and the settings in force on it.
 *
 * Everything but [stop] is called from the synthesis thread. [stop] comes from
 * whichever thread asked, which the native side is written for.
 */
class EvvEngine private constructor(private var handle: Long, val language: Int) {

	private val guard = Any()

	@Volatile
	private var closed = false

	var sampleRateHz: Int = Eci.sampleRateHz(1)
		private set

	/** What the voice was set to before Android's rate and pitch were scaled
	 *  onto it, so that a rate of 100% is the voice as it stands rather than
	 *  whatever the last utterance left behind. */
	private var baseSpeed = -1
	private var basePitch = -1

	/** What the rate and pitch last asked for come to in the engine's own
	 *  numbers, sent in front of every utterance. Below nought until a voice
	 *  has been applied, when there is no base to scale them onto. */
	private var wantSpeed = -1
	private var wantPitch = -1

	companion object {
		private const val TAG = "evvdroid"

		val available: IntArray by lazy {
			if (!EvvNative.loaded) IntArray(0) else runCatching { EvvNative.languages() }
				.onFailure { Log.e(TAG, "cannot ask for languages", it) }
				.getOrDefault(IntArray(0))
		}

		val version: String by lazy {
			if (!EvvNative.loaded) "" else runCatching { EvvNative.version() }.getOrDefault("")
		}

		/** Makes an instance speaking [language], which is one of the words
		 *  eciGetAvailableLanguages answered with and nothing else. */
		fun open(language: Int): EvvEngine? {
			if (!EvvNative.loaded) {
				Log.e(TAG, "the native library did not load", EvvNative.loadError)
				return null
			}
			val handle = EvvNative.create(language)
			if (handle == 0L) {
				Log.e(TAG, "no engine instance for language 0x%08x".format(language))
				return null
			}
			return EvvEngine(handle, language).apply { configure() }
		}
	}

	private fun configure() {
		EvvNative.setParam(handle, Eci.PARAM_LANGUAGE_DIALECT, language)
		EvvNative.setParam(handle, Eci.PARAM_SYNTH_MODE, 0)
		EvvNative.setParam(handle, Eci.PARAM_REAL_WORLD_UNITS, 0)
		// Always on. The engine is the only thing that can act on an annotation,
		// nothing in this app speaks without wanting pauses shortened, and one
		// that is malformed is spoken rather than obeyed, so ordinary text
		// carrying a backtick is read out as it stands.
		setAnnotations(true)
		setSampleRate(Eci.sampleRateHz(EvvNative.getParam(handle, Eci.PARAM_SAMPLE_RATE)))
	}

	fun setSampleRate(hz: Int) = synchronized(guard) {
		if (closed) return
		val index = Eci.sampleRateIndex(hz)
		EvvNative.setParam(handle, Eci.PARAM_SAMPLE_RATE, index)
		sampleRateHz = Eci.sampleRateHz(index)
	}

	fun setAnnotations(on: Boolean) = synchronized(guard) {
		if (!closed) EvvNative.setParam(handle, Eci.PARAM_INPUT_TYPE, if (on) 1 else 0)
	}

	/** The abbreviation dictionary, which is what eciDictionary switches.
	 *  Nought turns it on: that is the engine's own sense of the setting and
	 *  not a mistake here. */
	fun setAbbreviations(on: Boolean) = synchronized(guard) {
		if (!closed) EvvNative.setParam(handle, Eci.PARAM_DICTIONARY, if (on) 0 else 1)
	}

	/** What preset [index] (0 based, so 0 is Reed) is set to, without changing
	 *  the voice being spoken in. This is what the settings screen shows, so a
	 *  slider starts at the voice's own number rather than at one of ours. */
	fun presetShape(index: Int): Map<Int, Int> = synchronized(guard) {
		if (closed) return emptyMap()
		val preset = (index + Eci.FIRST_PRESET).coerceIn(Eci.FIRST_PRESET, Eci.LAST_PRESET)
		val out = LinkedHashMap<Int, Int>()
		for (param in 0 until Eci.NUM_VOICE_PARAMS) {
			val value = EvvNative.getVoiceParam(handle, preset, param)
			if (value >= 0) out[param] = value
		}
		return out
	}

	/**
	 * Speaks in preset [index] with [shape] laid over it.
	 *
	 * The presets are read only, so the way to a voice of your own is to copy
	 * one into an editable number, change it there, and copy that onto the
	 * voice being spoken in. Anything [shape] does not name keeps what the
	 * preset had, which is why an empty map is the right thing to pass when
	 * nobody has chosen otherwise.
	 */
	fun applyVoice(index: Int, shape: Map<Int, Int> = emptyMap()): Boolean = synchronized(guard) {
		if (closed) return false
		val preset = (index + Eci.FIRST_PRESET).coerceIn(Eci.FIRST_PRESET, Eci.LAST_PRESET)
		// A voice is written rather than annotated, so it can be refused: the
		// engine takes nothing while it is speaking, and answers nought for a
		// copy and -1 for a parameter when it does. Saying so is what lets the
		// caller ask again rather than believe a voice is in force that never
		// arrived.
		if (EvvNative.copyVoice(handle, preset, Eci.SCRATCH_VOICE) == 0) return false
		// The preset's own speed is not kept. Two of the eight ship faster than
		// the rest, so leaving it would make changing voice change the pace.
		if (EvvNative.setVoiceParam(
				handle, Eci.SCRATCH_VOICE, Eci.VOICE_SPEED,
				shape[Eci.VOICE_SPEED] ?: Eci.DEFAULT_SPEED
			) < 0
		) return false
		for ((param, value) in shape) {
			if (EvvNative.setVoiceParam(
					handle, Eci.SCRATCH_VOICE, param, Eci.clampVoice(param, value)
				) < 0
			) return false
		}
		if (EvvNative.copyVoice(handle, Eci.SCRATCH_VOICE, Eci.VOICE_CURRENT) == 0) return false
		// Reading is never refused, only writing, so these are what the engine
		// really has.
		baseSpeed = EvvNative.getVoiceParam(handle, Eci.VOICE_CURRENT, Eci.VOICE_SPEED)
		basePitch = EvvNative.getVoiceParam(handle, Eci.VOICE_CURRENT, Eci.VOICE_PITCH_BASELINE)
		wantSpeed = baseSpeed
		wantPitch = basePitch
		return true
	}

	/** Android hands rate and pitch over as a percentage of normal, where the
	 *  engine wants its own numbers. Normal is the voice as [applyVoice] left
	 *  it, so 100% changes nothing. Neither is written: [speak] sends both in
	 *  front of the words, for the reason [Prosody] gives. */
	fun setRatePercent(percent: Int) = synchronized(guard) {
		if (closed || baseSpeed < 0) return
		// Not baseSpeed * percent. The engine's speed scale is nothing like
		// linear in how fast it speaks, so SpeechRate does the conversion.
		wantSpeed = SpeechRate.speedForPercent(baseSpeed, percent)
	}

	fun setPitchPercent(percent: Int) = synchronized(guard) {
		if (closed || basePitch < 0) return
		wantPitch = (basePitch.toLong() * percent / 100).toInt().coerceIn(0, Eci.PERCENT_MAX)
	}

	fun getVoiceParam(param: Int): Int = synchronized(guard) {
		if (closed) -1 else EvvNative.getVoiceParam(handle, Eci.VOICE_CURRENT, param)
	}

	/** Hands a file to the engine's own dictionary loader. Nought is success. */
	fun loadDictionary(volume: Int, path: String): Int = synchronized(guard) {
		if (closed) -1 else EvvNative.loadDictionary(handle, volume, path)
	}

	fun teachWord(volume: Int, key: String, say: String): Int = synchronized(guard) {
		if (closed) -1 else EvvNative.teachWord(
			handle, volume, WesternText.encode(key), WesternText.encode(say)
		)
	}

	fun lookUpWord(volume: Int, key: String): String? = synchronized(guard) {
		if (closed) null else EvvNative.lookUpWord(handle, volume, WesternText.encode(key))
	}

	fun forgetDictionaries() = synchronized(guard) {
		if (!closed) EvvNative.forgetDictionaries(handle)
	}

	/** Queues [text] and starts the engine. The samples come out of [read].
	 *  The rate and pitch asked for go in front of the words rather than into
	 *  the voice, so that an utterance carries its own and nothing is left to
	 *  be refused. */
	fun speak(text: String): Boolean = synchronized(guard) {
		if (closed) return false
		val ask = if (wantSpeed >= 0 && wantPitch >= 0) Prosody.voice(wantSpeed, wantPitch) else ""
		return EvvNative.speak(handle, WesternText.encode(ask + text))
	}

	/** Bytes of PCM, blocking until there are some. 0 ends the utterance and
	 *  -1 says it was stopped. */
	fun read(into: ByteArray): Int {
		if (closed) return -1
		return EvvNative.read(handle, into)
	}

	fun stop() = synchronized(guard) {
		if (!closed) EvvNative.stop(handle)
	}

	fun close() = synchronized(guard) {
		if (closed) return
		closed = true
		EvvNative.destroy(handle)
		handle = 0
	}
}
