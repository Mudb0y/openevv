package org.evvdroid

/**
 * The sentences the engine says about itself, one per language.
 *
 * Each is written in its language rather than translated into it. The point of
 * a sample is to hear what the language sounds like, so one that came out as
 * English words would be no sample at all.
 *
 * A language with no sentence of its own falls back to English, which is still
 * a voice a person can judge. That is what a build with a module nobody has
 * written a line for gets.
 */
object Sentences {

	/** The short one, for the Listen button in the system's own settings. */
	fun sample(language: Int?): Int = when (isoOf(language)) {
		"spa" -> R.string.sample_text_spa
		"fra" -> R.string.sample_text_fra
		"deu" -> R.string.sample_text_deu
		"ita" -> R.string.sample_text_ita
		"pol" -> R.string.sample_text_pol
		"jpn" -> R.string.sample_text_jpn
		else -> R.string.sample_text
	}

	/** The longer one, for the preview on our own settings screen. It runs to
	 *  two clauses because a voice is being judged rather than merely heard. */
	fun preview(language: Int?): Int = when (isoOf(language)) {
		"spa" -> R.string.preview_text_spa
		"fra" -> R.string.preview_text_fra
		"deu" -> R.string.preview_text_deu
		"ita" -> R.string.preview_text_ita
		"pol" -> R.string.preview_text_pol
		"jpn" -> R.string.preview_text_jpn
		else -> R.string.preview_text
	}

	private fun isoOf(language: Int?): String? =
		language?.let { Eci.localeOf(it) }?.first
}
