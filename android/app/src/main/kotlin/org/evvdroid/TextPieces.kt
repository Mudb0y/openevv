package org.evvdroid

/**
 * Long text, cut into pieces the engine can be stopped between.
 *
 * The engine cannot abandon an utterance. That is settled rather than open --
 * openevv's own documents say six ways of adding an abandonment point were
 * tried and all fault -- so asking for silence waits out whatever is in flight.
 * For one line of a list that is a thirtieth of a second and nobody hears it.
 * For a paragraph it is not, and a reader swiping every couple of hundred
 * milliseconds has everything that lands inside the wait dropped: speech goes
 * quiet for several items and then catches up on the last one.
 *
 * Handing the text over in pieces makes the wait one piece rather than one
 * message. Where the piece ends is the whole of the problem, because ending
 * one is not free: the engine pauses at the end of an utterance as it would at
 * a full stop, so a boundary it would not have made itself is an audible gap.
 *
 * The rule here is openevv's, ported from its NVDA driver, and the numbers in
 * it are measured rather than chosen. A sentence end costs nothing -- one
 * message came to 252,010 samples whole and the same 252,010 in six pieces --
 * and anywhere else costs about four tenths of a second. So a full stop has to
 * argue that it is one: a word carrying a dot inside it is an abbreviation
 * ("e.g.", "U.S.") and a short word starting with a capital is an initial or a
 * title ("J.", "Mr."), and cutting at those measured 1.48 s and 0.70 s longer
 * than the same sentences whole. Only past [PIECE_CAP] characters does a piece
 * end at plain whitespace, which bounds the case that offers no sentence end
 * at all.
 */
object TextPieces {

	/** How far a piece runs before it ends at whitespace for want of a
	 *  sentence end. */
	const val PIECE_CAP = 500

	/** Closing marks that may sit after the punctuation that ends a sentence. */
	private const val CLOSERS = "\"')]}»”’"

	private const val ENDERS = "?!…。！？"

	/** Whether a word ends a sentence rather than being a dotted abbreviation. */
	fun endsSentence(word: String): Boolean {
		val tail = word.trimEnd { it in CLOSERS }
		if (tail.isEmpty()) return false
		if (tail.last() in ENDERS) return true
		if (!tail.endsWith('.') || tail.length < 2 || tail[tail.length - 2].isDigit()) return false
		val without = tail.dropLast(1)
		if (without.contains('.')) return false
		return !(without.length <= 3 && without.first().isUpperCase())
	}

	/**
	 * [text] as the pieces to hand over one at a time. Every character of the
	 * original is in exactly one piece and in the same order, so the speech is
	 * the same speech.
	 */
	fun split(text: String): List<String> {
		if (text.length <= PIECE_CAP && !text.any { it.isWhitespace() }) return listOf(text)
		val pieces = ArrayList<String>()
		val piece = StringBuilder()
		var gathered = 0
		var sentence = false
		for (part in runs(text)) {
			piece.append(part)
			gathered += part.length
			if (part.isNotBlank()) {
				sentence = endsSentence(part)
				continue
			}
			// A piece ends after the space that follows the word, so no word is
			// split and nothing is left leaning on the next piece.
			if (sentence || gathered >= PIECE_CAP) {
				pieces.add(piece.toString())
				piece.setLength(0)
				gathered = 0
			}
			sentence = false
		}
		if (piece.isNotBlank()) {
			pieces.add(piece.toString())
		} else if (piece.isNotEmpty() && pieces.isNotEmpty()) {
			// Trailing space belongs to the piece in front of it rather than to
			// a piece of its own, which would be an utterance saying nothing.
			pieces[pieces.lastIndex] = pieces.last() + piece
		}
		return if (pieces.isEmpty()) listOf(text) else pieces
	}

	/** [text] as alternating runs of whitespace and everything else. */
	private fun runs(text: String): List<String> {
		val out = ArrayList<String>()
		var at = 0
		while (at < text.length) {
			val from = at
			val space = text[at].isWhitespace()
			while (at < text.length && text[at].isWhitespace() == space) at++
			out.add(text.substring(from, at))
		}
		return out
	}
}
