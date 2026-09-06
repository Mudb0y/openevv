package org.evvdroid

/**
 * Shortening the gaps the engine leaves at punctuation and at the end of what
 * it was given.
 *
 * Eloquence pauses generously. That reads well in prose and badly in a screen
 * reader, where most utterances are a few words and the gap after each is time
 * spent waiting. The engine has no setting for it, but it does take an
 * annotation: `` `p `` and a number is a pause of that many milliseconds, and
 * putting one in front of a punctuation mark replaces the pause that mark would
 * have had.
 *
 * The rule is davidacm's, from the NVDA IBMTTS driver, which calls these
 * "JAWS-like" pauses. A mark counts when it follows a letter, a digit or a
 * space and is followed by whitespace, a slash or the end, which is what keeps
 * the decimal point in 3.14 and the colon in 2:30 from being touched.
 */
object Pauses {

	/** Leave the engine's own pauses alone. */
	const val KEEP = 0

	/** Shorten only the gap after the last thing said. */
	const val END_ONLY = 1

	/** Shorten the gaps at punctuation as well. */
	const val ALL = 2

	/** The marks the engine pauses at. The two dashes are here as the
	 *  characters Android hands over; WesternText turns them into hyphens
	 *  later, by which point this has already run. */
	private const val MARKS = "-,.:;?!–—"

	/** A mark that is really punctuation rather than part of a number: after a
	 *  letter, digit or space, and before whitespace, a slash or the end. The
	 *  third group takes a run of the same mark, so "wait..." is one pause. */
	private val AT_A_MARK = Regex("([A-Za-z0-9]|\\s)([$MARKS])(\\2*?)(\\s|[\\\\/]|$)")

	private const val BRIEF = "`p1"

	/** The gap after the last thing said, which is not made as short as the
	 *  rest. The engine leaves about four hundred milliseconds there, and
	 *  cutting all of it puts the final syllable hard against the end of the
	 *  buffer, where whatever plays it back clips a little off: "Google Gemini"
	 *  came out as "Google Gemin". This keeps a hundred of those milliseconds
	 *  as somewhere for that to land, and still saves the other three hundred.
	 *  It is silence either way, so nothing is heard except the missing wait. */
	private const val AT_THE_END = "`p100"

	/**
	 * [text] with the pauses [mode] asks for. [last] says whether this is the
	 * end of what was asked for, since the gap after that is the one
	 * [END_ONLY] is about.
	 */
	fun apply(text: String, mode: Int, last: Boolean): String {
		if (mode == KEEP || text.isEmpty()) return text
		var out = text
		if (mode == ALL) out = AT_A_MARK.replace(out, "$1 $BRIEF$2$3$4")
		// The engine pauses at the end of an utterance whether or not anything
		// there asked it to. A mark at the end has already been dealt with.
		if (last && out.trimEnd().lastOrNull() !in MARKS.toSet()) out = "$out $AT_THE_END"
		return out
	}
}
