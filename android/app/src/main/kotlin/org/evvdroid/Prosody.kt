package org.evvdroid

/**
 * What goes in front of an utterance to put the engine in the state it should
 * speak it in.
 *
 * Phrase prediction is the engine guessing where the phrase boundaries in a
 * sentence are and shaping the intonation to match. It reads well in prose and
 * gets in the way in a screen reader, where a line is usually a fragment and
 * the guess is wrong, so it is off unless asked for. There is no parameter for
 * it, only the annotation, which is why it is sent with every utterance rather
 * than set once.
 *
 * Rate and pitch have a parameter and are sent here anyway. The engine refuses
 * a written parameter while it is speaking -- vc_reentered answers -1 for the
 * whole time the instance is busy, which for this app is whenever the thread
 * that runs the engine is inside eciSynchronize -- and a screen reader asks
 * for exactly that moment, because every item it speaks cuts off the one
 * before. An annotation travels in the text instead, through the same queue
 * the synthesizer already reads in order, so it lands for the utterance that
 * asked for it and nothing has to survive in between.
 */
object Prosody {

	/** What to put in front of one utterance. */
	fun prefix(phrasePrediction: Boolean): String = if (phrasePrediction) ON else OFF

	/** The rate and pitch to speak one utterance at, in the engine's own
	 *  numbers. A malformed annotation is spoken aloud rather than obeyed, so
	 *  both are clamped to what the engine will take. */
	fun voice(speed: Int, pitch: Int): String =
		"`vs${Eci.clampVoice(Eci.VOICE_SPEED, speed)} " +
			"`vb${Eci.clampVoice(Eci.VOICE_PITCH_BASELINE, pitch)} "

	private const val ON = "`pp1 "
	private const val OFF = "`pp0 "
}
