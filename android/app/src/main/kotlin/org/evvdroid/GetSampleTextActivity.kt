package org.evvdroid

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.speech.tts.TextToSpeech

/** The sentence the system settings screen speaks when someone taps Listen. */
class GetSampleTextActivity : Activity() {

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		// Which language it wants to hear, named the same way an utterance
		// names one. The settings screen reads the text back only where the
		// answer is LANG_AVAILABLE, so that is what goes back whichever
		// language was asked for.
		val found = Languages.match(
			intent.getStringExtra("language"), intent.getStringExtra("country")
		)
		val result = Intent().apply {
			putExtra(TextToSpeech.Engine.EXTRA_SAMPLE_TEXT, getString(sentenceFor(found?.first)))
		}
		setResult(TextToSpeech.LANG_AVAILABLE, result)
		finish()
	}

	/** A sentence written in the language rather than translated into it. The
	 *  point of a sample is to hear what the language sounds like, so one that
	 *  came out as English words would be no sample at all. */
	private fun sentenceFor(language: Int?): Int =
		when (language?.let { Eci.localeOf(it) }?.first) {
			"spa" -> R.string.sample_text_spa
			"fra" -> R.string.sample_text_fra
			"deu" -> R.string.sample_text_deu
			"ita" -> R.string.sample_text_ita
			"pol" -> R.string.sample_text_pol
			"jpn" -> R.string.sample_text_jpn
			else -> R.string.sample_text
		}
}
