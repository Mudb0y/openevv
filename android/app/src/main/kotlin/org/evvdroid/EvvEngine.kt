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

	private var currentRate = -1
	private var currentPitch = -1

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
	fun applyVoice(index: Int, shape: Map<Int, Int> = emptyMap()) = synchronized(guard) {
		if (closed) return
		val preset = (index + Eci.FIRST_PRESET).coerceIn(Eci.FIRST_PRESET, Eci.LAST_PRESET)
		EvvNative.copyVoice(handle, preset, Eci.SCRATCH_VOICE)
		// The preset's own speed is not kept. Two of the eight ship faster than
		// the rest, so leaving it would make changing voice change the pace.
		EvvNative.setVoiceParam(
			handle, Eci.SCRATCH_VOICE, Eci.VOICE_SPEED,
			shape[Eci.VOICE_SPEED] ?: Eci.DEFAULT_SPEED
		)
		for ((param, value) in shape) {
			EvvNative.setVoiceParam(handle, Eci.SCRATCH_VOICE, param, Eci.clampVoice(param, value))
		}
		EvvNative.copyVoice(handle, Eci.SCRATCH_VOICE, Eci.VOICE_CURRENT)
		baseSpeed = EvvNative.getVoiceParam(handle, Eci.VOICE_CURRENT, Eci.VOICE_SPEED)
		basePitch = EvvNative.getVoiceParam(handle, Eci.VOICE_CURRENT, Eci.VOICE_PITCH_BASELINE)
		currentRate = -1
		currentPitch = -1
	}

	/** Android hands rate and pitch over as a percentage of normal, where the
	 *  engine wants its own numbers. Normal is the voice as [applyVoice] left
	 *  it, so 100% changes nothing. */
	fun setRatePercent(percent: Int) = synchronized(guard) {
		if (closed || percent == currentRate || baseSpeed < 0) return
		// Not baseSpeed * percent. The engine's speed scale is nothing like
		// linear in how fast it speaks, so SpeechRate does the conversion.
		val want = SpeechRate.speedForPercent(baseSpeed, percent)
		EvvNative.setVoiceParam(handle, Eci.VOICE_CURRENT, Eci.VOICE_SPEED, want)
		currentRate = percent
	}

	fun setPitchPercent(percent: Int) = synchronized(guard) {
		if (closed || percent == currentPitch || basePitch < 0) return
		val want = (basePitch.toLong() * percent / 100).toInt().coerceIn(0, Eci.PERCENT_MAX)
		EvvNative.setVoiceParam(handle, Eci.VOICE_CURRENT, Eci.VOICE_PITCH_BASELINE, want)
		currentPitch = percent
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

	/** Queues [text] and starts the engine. The samples come out of [read]. */
	fun speak(text: String): Boolean = synchronized(guard) {
		if (closed) false else EvvNative.speak(handle, WesternText.encode(text))
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
