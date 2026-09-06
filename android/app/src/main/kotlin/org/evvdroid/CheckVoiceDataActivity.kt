package org.evvdroid

import android.app.Activity
import android.content.Intent
import android.os.Bundle
import android.speech.tts.TextToSpeech

/**
 * What Android asks before it will use the engine. Everything is in the APK,
 * so every language the build has is already there and none is missing.
 */
class CheckVoiceDataActivity : Activity() {

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		val available = ArrayList<String>()
		for (language in EvvEngine.available) {
			val loc = Eci.localeOf(language) ?: continue
			available.add("${loc.first}-${loc.second}")
		}
		val result = Intent().apply {
			putStringArrayListExtra(TextToSpeech.Engine.EXTRA_AVAILABLE_VOICES, available)
			putStringArrayListExtra(TextToSpeech.Engine.EXTRA_UNAVAILABLE_VOICES, ArrayList())
		}
		val code = if (available.isEmpty()) {
			TextToSpeech.Engine.CHECK_VOICE_DATA_FAIL
		} else {
			TextToSpeech.Engine.CHECK_VOICE_DATA_PASS
		}
		setResult(code, result)
		finish()
	}
}
