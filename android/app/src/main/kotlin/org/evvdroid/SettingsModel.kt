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
	private val preview: EvvPreview? =
		EvvEngine.available.firstOrNull()?.let { EvvPreview(it) }

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

	fun chooseVoice(which: Int) {
		voice = which
		settings.voice = which
		// A voice that has been changed keeps its changes. One that has not
		// takes what the engine says it is.
		if (!settings.shapeIsCustom(which)) adoptVoice()
		shape.clear()
		shape.putAll(settings.shape(which))
	}

	fun setPercent(param: Int, percent: Int) = setShape(param, Eci.fromPercent(param, percent))

	/** One percent up or down from wherever the setting is now.
	 *
	 *  It reads the value rather than taking one, because a held key sends
	 *  several of these before the screen has drawn any of them, and a step
	 *  measured from what the screen last showed would land on the same number
	 *  every time. */
	fun stepPercent(param: Int, by: Int) {
		setPercent(param, (percentOf(param) + by).coerceIn(0, Eci.PERCENT_MAX))
	}

	fun setShape(param: Int, value: Int) {
		val settled = Eci.clampVoice(param, value)
		if (shape[param] == settled) return
		shape[param] = settled
		settings.setShapeValue(voice, param, settled)
	}

	/** Forgets this voice's changes. Every other voice is left alone, and so is
	 *  the speed, which belongs to the listener rather than to any of them. */
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

	/** What the row for [volume] says: the file picked for it, or nothing. */
	fun dictionaryName(volume: Int): String = dictionaries[volume] ?: app.getString(R.string.dictionary_none)

	/**
	 * Takes a copy of the file the picker handed back.
	 *
	 * A picked document is a content URI belonging to whichever app supplied
	 * it, readable now and quite possibly not tomorrow, and the speech service
	 * is a different process that reads these when it starts. So what is stored
	 * is a copy of our own rather than a reference to somebody else's.
	 */
	fun chooseDictionary(volume: Int, uri: android.net.Uri) {
		val into = java.io.File(app.filesDir, "dictionaries").apply { mkdirs() }
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

	fun removeDictionaries() {
		for (volume in Eci.DICT_VOLUMES) {
			settings.dictionaryPath(volume)?.let { java.io.File(it).delete() }
			settings.setDictionary(volume, null, null)
		}
		dictionaries = emptyMap()
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

	/** Nothing here speaks by itself. Every setting is in force the moment it
	 *  is written down, and this is how it gets heard. */
	fun say() {
		preview?.say(app.getString(R.string.preview_text), voice, settings.shape(voice), sampleRateHz)
	}

	private fun adoptVoice() {
		val own = preview?.presetShape(settings.voice) ?: return
		if (own.isEmpty()) return
		// Everything but the speed, which stays where the listener put it.
		// Glen and Sandy ship at 70 where the rest are 50, so taking the
		// voice's own would move the slider and the pace on every change.
		settings.writeShape(settings.voice, own - Eci.VOICE_SPEED)
	}

	companion object {
		val RATES: List<Int> = Eci.SAMPLE_RATES.sorted()
	}
}
