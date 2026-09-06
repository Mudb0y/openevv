package org.evvdroid

import android.os.Bundle
import androidx.activity.ComponentActivity
import androidx.activity.compose.setContent
import androidx.compose.material3.MaterialTheme
import androidx.compose.material3.Surface

/** The engine's own settings, reached from the system's text-to-speech screen
 *  and from the launcher. */
class SettingsActivity : ComponentActivity() {

	private var model: SettingsModel? = null

	override fun onCreate(savedInstanceState: Bundle?) {
		super.onCreate(savedInstanceState)
		val state = SettingsModel(this).also { model = it }
		setContent {
			MaterialTheme {
				Surface {
					SettingsScreen(state)
				}
			}
		}
	}

	override fun onStop() {
		super.onStop()
		// Leaving the screen should not leave a sample talking over whatever
		// comes next.
		model?.stopSpeaking()
	}

	override fun onDestroy() {
		super.onDestroy()
		model?.close()
		model = null
	}
}
