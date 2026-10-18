package org.evvdroid

import android.text.SpannableString
import android.text.Spanned
import android.text.style.TtsSpan
import androidx.test.ext.junit.runners.AndroidJUnit4
import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test
import org.junit.runner.RunWith

/**
 * The spans TalkBack puts on punctuation, read through Android's own classes
 * rather than the stand-ins the unit tests use.
 */
@RunWith(AndroidJUnit4::class)
class TtsSpanTest {

	@Test
	fun plainTextHasNothingToExpand() {
		assertNull(Punctuation.expandTtsSpans("Hello, world."))
		assertNull(Punctuation.expandTtsSpans(SpannableString("Hello, world.")))
	}

	@Test
	fun textSpansAreSpokenAndNamedOnce() {
		val text = SpannableString("Hello (x), world.")
		text.span(6, "left paren")
		text.span(8, "right paren")
		val expanded = Punctuation.expandTtsSpans(text)!!
		assertEquals(
			"Hello left paren x right paren comma, world dot. ",
			Punctuation.apply(expanded, Punctuation.LEVEL_ALL, ENGLISH)
		)
		assertEquals(
			"Hello left paren x right paren , world.",
			Punctuation.apply(expanded, Punctuation.LEVEL_NONE, ENGLISH)
		)
	}

	@Test
	fun verbatimSpansAreLeftToTheEngine() {
		val text = SpannableString("Call ABC.")
		text.setSpan(TtsSpan.VerbatimBuilder("ABC").build(), 5, 8, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
		assertNull(Punctuation.expandTtsSpans(text))
	}

	private fun SpannableString.span(at: Int, said: String) =
		setSpan(TtsSpan.TextBuilder(said).build(), at, at + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)

	private companion object {
		const val ENGLISH = 0x00010000
	}
}
