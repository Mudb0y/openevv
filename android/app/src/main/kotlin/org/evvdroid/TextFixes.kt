package org.evvdroid

/**
 * Text the engine reads wrong, put right before it sees it.
 *
 * Eloquence gives up on a word it cannot parse and spells it out instead, one
 * letter at a time. That is why "teamtalk5" comes back as t e a m t a l k 5:
 * the trailing digit makes the whole token unparseable, so the whole token gets
 * spelled. A space in front of the digit is enough to fix it.
 *
 * Most of these are davidacm's, from the NVDA IBMTTS driver's ibm_global_fixes.
 * Every one was measured against the engine first, because a rule that reads
 * well can still cost more than it saves. Two of the driver's were left out:
 * the pair that collapses double spaces around brackets, which exist because
 * NVDA's own symbol processing puts them there and TalkBack's does not.
 *
 * This runs before Pauses and Prosody, and has to. Those add annotations like
 * `` `p1 ``, and a rule here that puts a space between a letter and a digit
 * would turn that into `` `p 1 ``, which the engine speaks rather than obeys.
 */
object TextFixes {

	/**
	 * [text] with the words the engine mishandles rewritten. Nothing here
	 * changes what is said, only how it is written down for the engine.
	 */
	fun apply(text: String): String {
		if (text.isEmpty()) return text
		var out = text
		out = BEFORE_A_DIGIT.replace(out, "$1 $2")
		out = BEFORE_AN_OPENER.replace(out, "$1 $2")
		out = LOOSE_S_SUFFIX.replace(out, "$1$2")
		out = SPACE_BEFORE_A_MARK.replace(out, "$1$2")
		out = GROUPED_THOUSANDS.replace(out) { it.value.replace(",", "") }
		return out
	}

	/** A letter against a digit, which is the whole of the teamtalk5 problem.
	 *  Where the engine already coped the space costs nothing: mp3, x86, v2,
	 *  NVDA5 and EURUSD5 all came back byte for byte the same with it and
	 *  without, so this needs no test for which words are worth splitting.
	 *  Digits against letters are left alone: 1st is right already and gets
	 *  worse when split. */
	private val BEFORE_A_DIGIT = Regex("""([A-Za-z])(\d)""")

	/** The marks the engine starts spelling in front of. Closing brackets are
	 *  deliberately absent: "abc)" already costs no more than "abc", while
	 *  "abc )" costs twice as much. */
	private val OPENERS = """~#${'$'}%^*({|\[<""" + "\u2022"

	private val BEFORE_AN_OPENER = Regex("([A-Za-z]+)([" + Regex.escape(OPENERS) + "])")

	/** "books (s)" back to "books(s)", which is a third shorter. */
	private val LOOSE_S_SUFFIX = Regex("""([A-Za-z]+)\s+(\(s\))""")

	/** The engine does not tolerate a space in front of a punctuation mark. The
	 *  lookahead keeps decimals and thousands separators out of it. */
	private val SPACE_BEFORE_A_MARK = Regex("""([A-Za-z]+|\d+|\W+)\s+([:.!;,?](?![A-Za-z]|\d))""")

	/** A number grouped with commas around a round thousand, which the engine
	 *  reads as "one comma hundred". The driver spells this out as four rules,
	 *  one per length; one repeat covers the same ground and keeps going past a
	 *  billion. Only a 000 group qualifies, so 12,345 is left alone, which
	 *  measured the same either way. */
	private val GROUPED_THOUSANDS = Regex("""\b\d{1,3},000(?:,\d{3})+\b""")
}
