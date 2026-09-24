package org.evvdroid

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateMapOf
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue

/**
 * What the settings screen is showing, and the engine it can hear itself on.
 *
 * The screen edits engine numbers and shows percentages, and this is where the
 * two meet. Everything written here goes straight into [Settings], which is
 * what the speech service reads, so a change is in force before the sample
 * that demonstrates it has finished.
 */
class SettingsModel(context: Context) {

	private val app = context.applicationContext
	private val settings = Settings(app)

	/** Every language the build has, in the order the engine names them. */
	val languages: List<Int> = EvvEngine.available.toList()

	/** Which of [languages] the preview speaks. What was chosen last time if
	 *  it is still in the build, and otherwise the first one there is. */
	var language by mutableStateOf(
		languages.indexOf(settings.previewLanguage).coerceAtLeast(0)
	)
		private set

	/** What the row for it says, in whatever language the phone is set to. */
	val languageNames: List<String> = languages.map { Eci.displayName(it) }

	private val preview: EvvPreview? =
		languages.getOrNull(language)?.let { EvvPreview(it) }

	var voice by mutableStateOf(settings.voice)
		private set

	var abbreviations by mutableStateOf(settings.abbreviations)
		private set

	var sampleRateHz by mutableStateOf(settings.sampleRateHz)
		private set

	var pauses by mutableStateOf(settings.pauses)
		private set

	var phrasePrediction by mutableStateOf(settings.phrasePrediction)
		private set

	var processNumbers by mutableStateOf(settings.processNumbers)
		private set

	var numberMode by mutableStateOf(settings.numberMode)
		private set

	var readTimeNaturally by mutableStateOf(settings.readTimeNaturally)
		private set

	var readRomanNumerals by mutableStateOf(settings.readRomanNumerals)
		private set

	var readPunctuation by mutableStateOf(settings.readPunctuation)
		private set

	var punctuationLevel by mutableStateOf(settings.punctuationLevel)
		private set

	var readEmoji by mutableStateOf(settings.readEmoji)
		private set

	var managingVolume: Int? by mutableStateOf(null)
	var dictSearchQuery by mutableStateOf("")
	var editingEntry: DictEntry? by mutableStateOf(null)
	var isAddEditDialogOpen by mutableStateOf(false)
	var activeDictWords by mutableStateOf<List<DictEntry>>(emptyList())

	private val shape = mutableStateMapOf<Int, Int>().apply { putAll(settings.shape(settings.voice)) }

	private var dictionaries: Map<Int, String> by mutableStateOf(
		Eci.DICT_VOLUMES.mapNotNull { volume ->
			settings.dictionaryName(volume)?.let { volume to it }
		}.toMap()
	)

	val gender: Int get() = shape[Eci.VOICE_GENDER] ?: 0

	init {
		preview?.open {
			// A voice's own settings are what the sliders start at, and only
			// the engine knows them.
			if (!settings.shapeIsCustom(settings.voice)) adoptVoice()
			shape.putAll(settings.shape(settings.voice))
		}
	}

	fun stopSpeaking() {
		preview?.stopSpeaking()
	}

	fun close() {
		preview?.close()
	}

	fun percentOf(param: Int): Int = Eci.toPercent(param, shape[param] ?: 0)

	// ---- what the screen changes -----------------------------------------

	fun chooseLanguage(which: Int) {
		val want = languages.getOrNull(which) ?: return
		language = which
		settings.previewLanguage = want
		preview?.useLanguage(want)
	}

	fun chooseVoice(which: Int) {
		voice = which
		settings.voice = which
		if (!settings.shapeIsCustom(which)) adoptVoice()
		shape.clear()
		shape.putAll(settings.shape(which))
	}

	fun setPercent(param: Int, percent: Int) = setShape(param, Eci.fromPercent(param, percent))

	fun stepPercent(param: Int, by: Int) {
		setPercent(param, (percentOf(param) + by).coerceIn(0, Eci.PERCENT_MAX))
	}

	fun setShape(param: Int, value: Int) {
		val settled = Eci.clampVoice(param, value)
		if (shape[param] == settled) return
		shape[param] = settled
		settings.setShapeValue(voice, param, settled)
	}

	fun resetVoice() {
		settings.clearShape(voice)
		adoptVoice()
		shape.clear()
		shape.putAll(settings.shape(voice))
	}

	fun chooseAbbreviations(on: Boolean) {
		abbreviations = on
		settings.abbreviations = on
	}

	fun chooseProcessNumbers(on: Boolean) {
		processNumbers = on
		settings.processNumbers = on
	}

	fun chooseNumberMode(mode: Int) {
		numberMode = mode
		settings.numberMode = mode
	}

	fun chooseReadTimeNaturally(on: Boolean) {
		readTimeNaturally = on
		settings.readTimeNaturally = on
	}

	fun chooseReadRomanNumerals(on: Boolean) {
		readRomanNumerals = on
		settings.readRomanNumerals = on
	}

	fun chooseReadPunctuation(on: Boolean) {
		readPunctuation = on
		settings.readPunctuation = on
	}

	fun choosePunctuationLevel(level: Int) {
		punctuationLevel = level
		settings.punctuationLevel = level
	}

	fun chooseReadEmoji(on: Boolean) {
		readEmoji = on
		settings.readEmoji = on
	}

	/** What the row for [volume] says: the file picked for it, or nothing. */
	fun dictionaryName(volume: Int): String = dictionaries[volume] ?: app.getString(R.string.dictionary_none)

	fun dictionaryFile(volume: Int): java.io.File {
		val existing = settings.dictionaryPath(volume)
		if (existing != null) return java.io.File(existing)
		val dir = DirectBoot.dictionaries(app).apply { mkdirs() }
		val defaultName = when (volume) {
			Eci.DICT_MAIN -> "volume-0-main.dic"
			Eci.DICT_ROOT -> "volume-1-root.dic"
			Eci.DICT_ABBREVIATION -> "volume-2-abbr.dic"
			else -> "volume-3-emoji.dic"
		}
		return java.io.File(dir, defaultName)
	}

	fun openDictionaryManager(volume: Int) {
		managingVolume = volume
		dictSearchQuery = ""
		refreshDictWords(volume)
	}

	fun closeDictionaryManager() {
		managingVolume = null
		editingEntry = null
		isAddEditDialogOpen = false
		dictSearchQuery = ""
	}

	fun refreshDictWords(volume: Int) {
		val file = dictionaryFile(volume)
		activeDictWords = Dictionaries.readDictEntries(file)
	}

	fun openAddWordDialog(existing: DictEntry? = null) {
		editingEntry = existing
		isAddEditDialogOpen = true
	}

	fun closeAddWordDialog() {
		editingEntry = null
		isAddEditDialogOpen = false
	}

	fun addOrUpdateWord(
		volume: Int,
		key: String,
		say: String,
		caseSensitive: Boolean = false,
		suspended: Boolean = false
	): List<DictEntry> {
		val file = dictionaryFile(volume)
		val updated = Dictionaries.addOrUpdateDictEntry(file, key.trim(), say.trim(), caseSensitive, suspended)
		val name = when (volume) {
			Eci.DICT_MAIN -> app.getString(R.string.dictionary_main)
			Eci.DICT_ROOT -> app.getString(R.string.dictionary_root)
			Eci.DICT_ABBREVIATION -> app.getString(R.string.dictionary_abbreviation)
			else -> app.getString(R.string.dictionary_emoji)
		}
		val activeCount = updated.count { !it.suspended }
		val label = app.getString(R.string.dictionary_entries, name, activeCount)
		settings.setDictionary(volume, file.absolutePath, label)
		dictionaries = dictionaries + (volume to label)
		activeDictWords = updated
		if (volume == Eci.DICT_EMOJI) {
			Emoji.loadUserEmoji(file)
		}
		closeAddWordDialog()
		return updated
	}

	fun toggleSuspendWord(volume: Int, entry: DictEntry): List<DictEntry> {
		val file = dictionaryFile(volume)
		val updated = Dictionaries.toggleSuspendDictEntry(file, entry.key, entry.caseSensitive)
		val name = when (volume) {
			Eci.DICT_MAIN -> app.getString(R.string.dictionary_main)
			Eci.DICT_ROOT -> app.getString(R.string.dictionary_root)
			Eci.DICT_ABBREVIATION -> app.getString(R.string.dictionary_abbreviation)
			else -> app.getString(R.string.dictionary_emoji)
		}
		val activeCount = updated.count { !it.suspended }
		val label = if (updated.isEmpty()) {
			app.getString(R.string.dictionary_none)
		} else {
			app.getString(R.string.dictionary_entries, name, activeCount)
		}
		settings.setDictionary(volume, file.absolutePath, label)
		dictionaries = dictionaries + (volume to label)
		activeDictWords = updated
		if (volume == Eci.DICT_EMOJI) {
			Emoji.loadUserEmoji(file)
		}
		return updated
	}

	fun deleteWord(volume: Int, key: String, caseSensitive: Boolean? = null): List<DictEntry> {
		val file = dictionaryFile(volume)
		val updated = Dictionaries.deleteDictEntry(file, key, caseSensitive)
		val name = when (volume) {
			Eci.DICT_MAIN -> app.getString(R.string.dictionary_main)
			Eci.DICT_ROOT -> app.getString(R.string.dictionary_root)
			Eci.DICT_ABBREVIATION -> app.getString(R.string.dictionary_abbreviation)
			else -> app.getString(R.string.dictionary_emoji)
		}
		val activeCount = updated.count { !it.suspended }
		val label = if (updated.isEmpty()) {
			app.getString(R.string.dictionary_none)
		} else {
			app.getString(R.string.dictionary_entries, name, activeCount)
		}
		if (updated.isEmpty()) {
			settings.setDictionary(volume, null, null)
			dictionaries = dictionaries - volume
		} else {
			settings.setDictionary(volume, file.absolutePath, label)
			dictionaries = dictionaries + (volume to label)
		}
		activeDictWords = updated
		if (volume == Eci.DICT_EMOJI) {
			Emoji.loadUserEmoji(file)
		}
		return updated
	}

	fun chooseDictionary(volume: Int, uri: android.net.Uri) {
		val into = DirectBoot.dictionaries(app).apply { mkdirs() }
		val file = java.io.File(into, "volume-$volume.dic")
		val name = nameOf(uri) ?: file.name
		try {
			app.contentResolver.openInputStream(uri).use { source ->
				if (source == null) return
				file.outputStream().use { sink -> source.copyTo(sink) }
			}
		} catch (unreadable: Exception) {
			android.util.Log.e("evvdroid", "cannot read the dictionary picked", unreadable)
			return
		}
		val entries = Dictionaries.count(file)
		if (entries == 0) {
			file.delete()
			dictionaries = dictionaries - volume
			settings.setDictionary(volume, null, null)
			return
		}
		val said = app.getString(R.string.dictionary_entries, name, entries)
		settings.setDictionary(volume, file.absolutePath, said)
		dictionaries = dictionaries + (volume to said)
	}

	fun removeDictionary(volume: Int) {
		settings.dictionaryPath(volume)?.let { java.io.File(it).delete() }
		settings.setDictionary(volume, null, null)
		dictionaries = dictionaries - volume
		if (managingVolume == volume) {
			activeDictWords = emptyList()
		}
	}

	fun removeDictionaries() {
		for (volume in Eci.DICT_VOLUMES) {
			settings.dictionaryPath(volume)?.let { java.io.File(it).delete() }
			settings.setDictionary(volume, null, null)
		}
		dictionaries = emptyMap()
		activeDictWords = emptyList()
	}

	private fun nameOf(uri: android.net.Uri): String? = runCatching {
		app.contentResolver.query(uri, null, null, null, null)?.use { row ->
			val at = row.getColumnIndex(android.provider.OpenableColumns.DISPLAY_NAME)
			if (at >= 0 && row.moveToFirst()) row.getString(at) else null
		}
	}.getOrNull()

	fun choosePhrasePrediction(on: Boolean) {
		phrasePrediction = on
		settings.phrasePrediction = on
	}

	fun choosePauses(mode: Int) {
		pauses = mode
		settings.pauses = mode
	}

	fun chooseSampleRate(hz: Int) {
		sampleRateHz = hz
		settings.sampleRateHz = hz
	}

	// ---- hearing it ------------------------------------------------------

	fun say() {
		val sentence = app.getString(Sentences.preview(languages.getOrNull(language)))
		preview?.say(sentence, voice, settings.shape(voice), sampleRateHz)
	}

	private fun adoptVoice() {
		val own = preview?.presetShape(settings.voice) ?: return
		if (own.isEmpty()) return
		settings.writeShape(settings.voice, own - Eci.VOICE_SPEED)
	}

	companion object {
		val RATES: List<Int> = Eci.SAMPLE_RATES.sorted()
	}
}
