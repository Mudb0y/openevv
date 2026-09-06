package org.evvdroid

/** The numbers the published ECI interface takes, and the few tables that turn
 *  them into something Android understands. */
object Eci {

	const val PARAM_SYNTH_MODE = 0
	const val PARAM_INPUT_TYPE = 1
	const val PARAM_TEXT_MODE = 2
	const val PARAM_DICTIONARY = 3
	const val PARAM_SAMPLE_RATE = 5
	const val PARAM_REAL_WORLD_UNITS = 8
	const val PARAM_LANGUAGE_DIALECT = 9
	const val PARAM_NUMBER_MODE = 10

	const val VOICE_GENDER = 0
	const val VOICE_HEAD_SIZE = 1
	const val VOICE_PITCH_BASELINE = 2
	const val VOICE_PITCH_FLUCTUATION = 3
	const val VOICE_ROUGHNESS = 4
	const val VOICE_BREATHINESS = 5
	const val VOICE_SPEED = 6
	const val VOICE_VOLUME = 7

	/** Voice 0 is the one being spoken in; 1 to 8 are the language's presets
	 *  and are read only; 9 to 16 are the caller's to edit. */
	const val VOICE_CURRENT = 0
	const val FIRST_PRESET = 1
	const val LAST_PRESET = 8
	const val SCRATCH_VOICE = 9

	/** The bit that says the text handed over is UTF-16 rather than bytes. It
	 *  travels in the language word, not in a setting of its own. */
	const val UNICODE_CODE_SET = 0x800

	const val NUM_VOICE_PARAMS = 8

	/** The volumes a dictionary set holds. The fourth, the extended main
	 *  dictionary, is for a language written in another script and every call
	 *  refuses it here. */
	const val DICT_MAIN = 0
	const val DICT_ROOT = 1
	const val DICT_ABBREVIATION = 2

	val DICT_VOLUMES = listOf(DICT_MAIN, DICT_ROOT, DICT_ABBREVIATION)

	/**
	 * The speed every voice is spoken at unless someone has chosen otherwise.
	 *
	 * Six of the eight presets ship at 50 and Glen and Sandy ship at 70, which
	 * is half again as fast. That is IBM's data rather than a fault, but it
	 * makes speed a property of the voice, so picking Glen sped everything up
	 * without anyone asking for it. Speed belongs to whoever is listening.
	 */
	const val DEFAULT_SPEED = 50

	const val SPEED_MAX = 250
	const val PERCENT_MAX = 100

	/** What each of the eight will take. Gender is a choice of two, speed runs
	 *  to 250, and the rest are hundredths. */
	fun voiceRange(param: Int): IntRange = when (param) {
		VOICE_GENDER -> 0..1
		VOICE_SPEED -> 0..SPEED_MAX
		else -> 0..PERCENT_MAX
	}

	fun clampVoice(param: Int, value: Int): Int {
		val range = voiceRange(param)
		return value.coerceIn(range.first, range.last)
	}

	/** The eight are on three different scales, and a screen reader saying a
	 *  bare number leaves it to the listener to remember which. So the settings
	 *  screen works in hundredths of whatever the setting's own range is, which
	 *  for six of the seven sliders is the number the engine already uses and
	 *  for speed is a fifth of it. */
	fun toPercent(param: Int, value: Int): Int {
		val top = voiceRange(param).last
		if (top <= 0) return 0
		return Math.round(value * 100.0 / top).toInt().coerceIn(0, PERCENT_MAX)
	}

	fun fromPercent(param: Int, percent: Int): Int {
		val top = voiceRange(param).last
		return Math.round(percent.coerceIn(0, PERCENT_MAX) * top / 100.0).toInt()
	}

	/** What eciSampleRate's index means in hertz, in the order the engine
	 *  numbers them. The first four are IBM's numbering and the rest openevv's. */
	val SAMPLE_RATES = intArrayOf(8000, 11025, 22050, 16000, 32000, 44100, 48000)

	fun sampleRateHz(index: Int): Int =
		SAMPLE_RATES.getOrElse(index) { SAMPLE_RATES[1] }

	fun sampleRateIndex(hz: Int): Int {
		val at = SAMPLE_RATES.indexOf(hz)
		return if (at >= 0) at else 1
	}

	/** The names the eight presets have gone by since Eloquence shipped. The
	 *  engine numbers them 1 to 8 in this order. */
	val PRESET_NAMES = arrayOf(
		"Reed", "Shelley", "Bobby", "Rocko", "Glen", "Sandy", "Grandma", "Grandpa"
	)

	/** A language word is the family in the top half, the code set in the third
	 *  byte and the dialect in the bottom one. */
	fun family(language: Int): Int = language and 0xFFFF0000.toInt()

	fun baseLanguage(language: Int): Int = language and UNICODE_CODE_SET.inv()

	private val LOCALES = mapOf(
		0x00010000 to Triple("eng", "USA", ""),
		0x00010001 to Triple("eng", "GBR", ""),
		0x00020000 to Triple("spa", "ESP", ""),
		0x00020001 to Triple("spa", "MEX", ""),
		0x00030000 to Triple("fra", "FRA", ""),
		0x00030001 to Triple("fra", "CAN", ""),
		0x00040000 to Triple("deu", "DEU", ""),
		0x00050000 to Triple("ita", "ITA", ""),
		0x00060000 to Triple("cmn", "CHN", ""),
		0x00060001 to Triple("cmn", "TWN", ""),
		0x00070000 to Triple("por", "BRA", ""),
		0x00080000 to Triple("jpn", "JPN", ""),
		0x00090000 to Triple("fin", "FIN", ""),
		0x000a0000 to Triple("kor", "KOR", ""),
		0x000b0000 to Triple("yue", "CHN", ""),
		0x000b0001 to Triple("yue", "HKG", ""),
		0x000c0000 to Triple("nld", "NLD", ""),
		0x000d0000 to Triple("nor", "NOR", ""),
		0x000e0000 to Triple("swe", "SWE", ""),
		0x000f0000 to Triple("dan", "DNK", ""),
		0x00110000 to Triple("pol", "POL", "")
	)

	/** The ISO-3 language and country an engine language answers to, which is
	 *  what Android asks about. */
	fun localeOf(language: Int): Triple<String, String, String>? =
		LOCALES[baseLanguage(language)]

	fun displayName(language: Int): String {
		val loc = localeOf(language) ?: return "0x%08x".format(language)
		return java.util.Locale(loc.first, loc.second).getDisplayName(java.util.Locale.getDefault())
	}
}
