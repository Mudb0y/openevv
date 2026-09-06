package org.evvdroid

import android.media.AudioAttributes
import android.media.AudioFormat
import android.media.AudioManager
import android.media.AudioTrack
import android.os.Handler
import android.os.HandlerThread
import android.util.Log

/**
 * An engine of its own for the settings screen, and somewhere to hear it.
 *
 * Tuning a voice by writing a setting down, leaving, and waiting for the
 * screen reader to say something else in it is not tuning. So the settings
 * screen keeps an instance of its own and speaks into an AudioTrack, which is
 * the one place in this program that plays audio itself: the engine that
 * TalkBack drives hands its samples to Android and never sees a device.
 *
 * Everything runs on one background thread, so a preview asked for while
 * another is speaking replaces it rather than overlapping.
 */
class EvvPreview(private val language: Int) {

	private val thread = HandlerThread("evv-preview").apply { start() }
	private val work = Handler(thread.looper)

	@Volatile
	private var engine: EvvEngine? = null

	@Volatile
	private var track: AudioTrack? = null

	@Volatile
	private var generation = 0

	/** How many bytes of audio have gone to the track. Nothing in the app reads
	 *  it; it is what lets a test say the preview really played, which is worth
	 *  having because a preview that quietly plays nothing looks exactly like
	 *  one that works. */
	@Volatile
	var bytesPlayed: Long = 0L
		private set

	/** Reads what a preset is set to. Answers an empty map until the engine has
	 *  opened, which is the first thing the thread does. */
	fun presetShape(index: Int): Map<Int, Int> = engine?.presetShape(index) ?: emptyMap()

	fun open(then: (() -> Unit)? = null) {
		work.post {
			if (engine == null) engine = EvvEngine.open(language)
			then?.let { Handler(android.os.Looper.getMainLooper()).post(it) }
		}
	}

	/** Says [text] in [voice] with [shape] on it, stopping whatever was being
	 *  said. */
	fun say(text: String, voice: Int, shape: Map<Int, Int>, rateHz: Int) {
		// Whatever is being said stops first, and the turn this utterance
		// belongs to is taken after that -- stopping is what moves the count
		// on, so reading it first would leave this utterance holding a number
		// that is already stale and it would never be spoken at all.
		stopSpeaking()
		val mine = generation
		work.post {
			if (mine != generation) return@post
			val e = engine ?: EvvEngine.open(language)?.also { engine = it } ?: return@post
			e.setSampleRate(rateHz)
			e.applyVoice(voice, shape)
			if (!e.speak(text)) {
				Log.e(TAG, "the engine refused the sample")
				return@post
			}
			play(e, mine)
		}
	}

	private fun play(e: EvvEngine, mine: Int) {
		val rate = e.sampleRateHz
		val minimum = AudioTrack.getMinBufferSize(
			rate, AudioFormat.CHANNEL_OUT_MONO, AudioFormat.ENCODING_PCM_16BIT
		)
		val size = if (minimum > 0) maxOf(minimum, CHUNK * 4) else CHUNK * 4
		val t = AudioTrack.Builder()
			.setAudioAttributes(
				AudioAttributes.Builder()
					.setUsage(AudioAttributes.USAGE_ASSISTANT)
					.setContentType(AudioAttributes.CONTENT_TYPE_SPEECH)
					.build()
			)
			.setAudioFormat(
				AudioFormat.Builder()
					.setEncoding(AudioFormat.ENCODING_PCM_16BIT)
					.setSampleRate(rate)
					.setChannelMask(AudioFormat.CHANNEL_OUT_MONO)
					.build()
			)
			.setBufferSizeInBytes(size)
			.setTransferMode(AudioTrack.MODE_STREAM)
			.build()
		track = t
		t.play()
		val buffer = ByteArray(CHUNK)
		while (mine == generation) {
			val n = e.read(buffer)
			if (n <= 0) break
			var at = 0
			while (at < n && mine == generation) {
				val wrote = t.write(buffer, at, n - at)
				if (wrote <= 0) break
				at += wrote
				bytesPlayed += wrote
			}
		}
		if (mine == generation) {
			// Let what is already in the track finish rather than cutting it.
			runCatching { t.stop() }
		} else {
			runCatching { t.pause(); t.flush(); t.stop() }
		}
		runCatching { t.release() }
		if (track === t) track = null
	}

	fun stopSpeaking() {
		generation++
		engine?.stop()
		runCatching { track?.pause(); track?.flush() }
	}

	fun close() {
		stopSpeaking()
		work.post {
			engine?.close()
			engine = null
			thread.quitSafely()
		}
	}

	private companion object {
		const val TAG = "evvdroid"
		const val CHUNK = 4096
	}
}
