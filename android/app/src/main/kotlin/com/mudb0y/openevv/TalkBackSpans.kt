package com.mudb0y.openevv

import android.text.Spanned
import android.text.style.TtsSpan

/**
 * What TalkBack has already put into words.
 *
 * TalkBack names punctuation itself when it is asked to, at its own "most" and
 * "all", by putting a text span on each mark that says what to read for it,
 * and it describes a run of dashes or a symbol the same way. Those words are
 * what the listener asked for, so they are read in the mark's place. Nothing
 * else in a text is named here: below those levels a mark reaches the engine
 * as it stands and is read as the engine reads it, as it is on Linux and in
 * NVDA.
 */
object TalkBackSpans {

	/** A stretch of the text that whoever sent it has already put into words. */
	data class SpokenSpan(val start: Int, val end: Int, val text: String)

	/**
	 * [charSequence] with its text spans replaced by what they say, or null if
	 * it has none. Only [TtsSpan.TYPE_TEXT] is a replacement; a verbatim span
	 * asks for its text to be spelled, and the others carry no text at all.
	 */
	fun expand(charSequence: CharSequence?): String? {
		if (charSequence !is Spanned) return null
		val spans = charSequence.getSpans(0, charSequence.length, TtsSpan::class.java)
		if (spans.isNullOrEmpty()) return null
		val spoken = spans.mapNotNull { span ->
			if (span.type != TtsSpan.TYPE_TEXT) return@mapNotNull null
			val text = span.args?.getString(TtsSpan.ARG_TEXT)
			if (text.isNullOrEmpty()) null
			else SpokenSpan(charSequence.getSpanStart(span), charSequence.getSpanEnd(span), text)
		}
		return expandSpans(charSequence, spoken)
	}

	/** The part of [expand] that needs nothing from Android. */
	fun expandSpans(text: CharSequence, spans: List<SpokenSpan>): String? {
		val usable = spans
			.filter { it.start >= 0 && it.start < it.end && it.end <= text.length && it.text.isNotEmpty() }
			.sortedBy { it.start }
		if (usable.isEmpty()) return null

		val sb = StringBuilder()
		var cursor = 0
		for (span in usable) {
			if (span.start < cursor) continue
			sb.append(text, cursor, span.start)
			sb.append(' ').append(span.text)
			// A phrase or sentence mark stays behind its name, so the engine
			// still pauses and inflects on it.
			val single = if (span.end - span.start == 1) text[span.start] else null
			if (single != null && single in KEPT) sb.append(single)
			sb.append(' ')
			cursor = span.end
		}
		sb.append(text, cursor, text.length)
		return SPACES.replace(sb.toString(), " ")
	}

	/** Marks that keep their place behind their names. */
	private const val KEPT = ".,:;?!"

	private val SPACES = Regex("""[ \t]+""")
}
