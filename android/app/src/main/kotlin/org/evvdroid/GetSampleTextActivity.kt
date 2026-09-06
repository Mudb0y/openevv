package org.evvdroid

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.speech.tts.TextToSpeech

/** The sentence the system settings screen speaks when someone taps Listen. */
class GetSampleTextActivity : Activity() {

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		val result = Intent().apply {
			putExtra(TextToSpeech.Engine.EXTRA_SAMPLE_TEXT, getString(R.string.sample_text))
		}
		setResult(TextToSpeech.LANG_AVAILABLE, result)
		finish()
	}
}
