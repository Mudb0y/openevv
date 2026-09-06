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

	private val shape = mutableStateMapOf<Int, Int>().apply { putAll(settings.shape()) }

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
			if (!settings.shapeIsCustom()) adoptVoice()
			shape.putAll(settings.shape())
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
		// The eight below belong to the voice, so they follow it.
		adoptVoice()
		shape.putAll(settings.shape())
	}

	fun setPercent(param: Int, percent: Int) = setShape(param, Eci.fromPercent(param, percent))

	fun setShape(param: Int, value: Int) {
		val settled = Eci.clampVoice(param, value)
		if (shape[param] == settled) return
		shape[param] = settled
		settings.setShapeValue(param, settled)
	}

	fun resetVoice() {
		settings.clearShape()
		adoptVoice()
		shape.clear()
		shape.putAll(settings.shape())
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

	fun chooseSampleRate(hz: Int) {
		sampleRateHz = hz
		settings.sampleRateHz = hz
	}

	// ---- hearing it ------------------------------------------------------

	/** Nothing here speaks by itself. Every setting is in force the moment it
	 *  is written down, and this is how it gets heard. */
	fun say() {
		preview?.say(app.getString(R.string.preview_text), voice, settings.shape(), sampleRateHz)
	}

	private fun adoptVoice() {
		val own = preview?.presetShape(settings.voice) ?: return
		if (own.isNotEmpty()) settings.writeShape(own)
	}

	companion object {
		val RATES: List<Int> = Eci.SAMPLE_RATES.sorted()
	}
}
