package org.evvdroid

import android.speech.tts.TextToSpeech
import java.util.Locale

/**
 * Which of the engine's languages answers to the one Android asked about.
 *
 * Android names a language in ISO codes and the engine knows its own by
 * number, so the two have to be brought together somewhere. Two places ask:
 * the speech service, for every utterance, and the activity that hands back a
 * sentence to listen to.
 */
object Languages {

	/**
	 * The engine language for [lang] and [country], and how good the match is
	 * in Android's own terms.
	 *
	 * A language whose country matches too is the better answer and is taken
	 * first. One that matches on the language alone is still an answer, since
	 * text marked Australian should be read in English rather than refused;
	 * which English is then whichever the build names first.
	 */
	fun match(lang: String?, country: String?): Pair<Int, Int>? {
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
}
