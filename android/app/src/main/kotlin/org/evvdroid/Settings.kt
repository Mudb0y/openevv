package org.evvdroid

import android.content.Context
import android.content.SharedPreferences

/**
 * What the settings screen wrote, read back for the engine.
 *
 * The eight voice settings are stored only once someone has chosen them. That
 * is the whole point rather than a detail: every preset carries its own head
 * size, inflection and volume -- Reed speaks at inflection 30 and volume 92,
 * Shelley at 30 and 100, Bobby at 35 and 90 -- so a default of our own written
 * over the top would make every voice wrong in the same way, which is what it
 * did. An absent setting means the voice keeps what it came with.
 *
 * A revision goes up whenever anything changes, so the service can tell
 * whether the instance it is holding is still configured the way the user left
 * it without comparing every value.
 */
class Settings(context: Context) {

	// The file androidx.preference used, kept under that name so that a build
	// with the old screen in it and a build with this one read the same
	// settings.
	private val prefs: SharedPreferences = context.applicationContext
		.getSharedPreferences("${context.packageName}_preferences", Context.MODE_PRIVATE)

	@Volatile
	var revision: Int = 0
		private set

	private val watcher = SharedPreferences.OnSharedPreferenceChangeListener { _, _ -> revision++ }

	init {
		prefs.registerOnSharedPreferenceChangeListener(watcher)
	}

	var voice: Int
		get() = prefs.getInt(KEY_VOICE, 0).coerceIn(0, Eci.PRESET_NAMES.lastIndex)
		set(value) = prefs.edit().putInt(KEY_VOICE, value).apply()

	var sampleRateHz: Int
		get() = prefs.getInt(KEY_SAMPLE_RATE, DEFAULT_SAMPLE_RATE)
		set(value) = prefs.edit().putInt(KEY_SAMPLE_RATE, value).apply()

	/** The abbreviation dictionary. Off by default: it expands what it takes
	 *  for an abbreviation whether that was wanted or not. */
	var abbreviations: Boolean
		get() = prefs.getBoolean(KEY_ABBREVIATIONS, false)
		set(value) = prefs.edit().putBoolean(KEY_ABBREVIATIONS, value).apply()

	/** Where the dictionary loaded into [volume] came from, and what it was
	 *  called when it was picked. */
	fun dictionaryPath(volume: Int): String? = prefs.getString("dict_path_$volume", null)

	fun dictionaryName(volume: Int): String? = prefs.getString("dict_name_$volume", null)

	fun setDictionary(volume: Int, path: String?, name: String?) {
		val edit = prefs.edit()
		if (path == null) {
			edit.remove("dict_path_$volume").remove("dict_name_$volume")
		} else {
			edit.putString("dict_path_$volume", path).putString("dict_name_$volume", name)
		}
		edit.apply()
	}

	fun dictionaryPaths(): Map<Int, String> {
		val out = LinkedHashMap<Int, String>()
		for (volume in Eci.DICT_VOLUMES) dictionaryPath(volume)?.let { out[volume] = it }
		return out
	}

	/** The voice settings someone has actually chosen, by the number
	 *  eciSetVoiceParam takes. What is not here is left as the preset has it. */
	fun shape(): Map<Int, Int> {
		val out = LinkedHashMap<Int, Int>()
		for (param in SHAPE) {
			val key = keyOf(param)
			if (!prefs.contains(key)) continue
			out[param] = Eci.clampVoice(param, prefs.getInt(key, 0))
		}
		return out
	}

	fun shapeValue(param: Int): Int? {
		val key = keyOf(param)
		return if (prefs.contains(key)) Eci.clampVoice(param, prefs.getInt(key, 0)) else null
	}

	fun setShapeValue(param: Int, value: Int) {
		prefs.edit().putInt(keyOf(param), Eci.clampVoice(param, value)).apply()
	}

	/** Whether a voice's own settings have been overridden at all, which is
	 *  what the reset in the settings screen offers to undo. */
	fun shapeIsCustom(): Boolean = SHAPE.any { prefs.contains(keyOf(it)) }

	/** Writes the settings a preset carries, so the sliders show what the voice
	 *  actually is rather than a number of ours. */
	fun writeShape(values: Map<Int, Int>) {
		val edit = prefs.edit()
		for (param in SHAPE) values[param]?.let { edit.putInt(keyOf(param), it) }
		edit.apply()
	}

	fun clearShape() {
		val edit = prefs.edit()
		for (param in SHAPE) edit.remove(keyOf(param))
		edit.apply()
	}

	private fun keyOf(param: Int) = "voice_param_$param"

	companion object {
		const val KEY_VOICE = "voice_preset"
		const val KEY_SAMPLE_RATE = "sample_rate_hz"
		const val KEY_ABBREVIATIONS = "abbreviations"
		const val DEFAULT_SAMPLE_RATE = 11025

		/** The eight, in the order the settings screen shows them. */
		val SHAPE = listOf(
			Eci.VOICE_GENDER,
			Eci.VOICE_SPEED,
			Eci.VOICE_PITCH_BASELINE,
			Eci.VOICE_PITCH_FLUCTUATION,
			Eci.VOICE_HEAD_SIZE,
			Eci.VOICE_ROUGHNESS,
			Eci.VOICE_BREATHINESS,
			Eci.VOICE_VOLUME
		)

		/** The seven that are a slider. Gender is a choice of two. */
		val SLIDERS = SHAPE.filter { it != Eci.VOICE_GENDER }
	}
}
