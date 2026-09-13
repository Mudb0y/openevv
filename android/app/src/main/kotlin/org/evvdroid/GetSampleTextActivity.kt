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
			putExtra(TextToSpeech.Engine.EXTRA_SAMPLE_TEXT, getString(Sentences.sample(found?.first)))
		}
		setResult(TextToSpeech.LANG_AVAILABLE, result)
		finish()
	}
}
