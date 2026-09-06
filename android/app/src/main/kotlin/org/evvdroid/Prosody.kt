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
 */
object Prosody {

	/** What to put in front of one utterance. */
	fun prefix(phrasePrediction: Boolean): String = if (phrasePrediction) ON else OFF

	private const val ON = "`pp1 "
	private const val OFF = "`pp0 "
}
