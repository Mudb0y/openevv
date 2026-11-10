package com.mudb0y.openevv

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
		assertNull(TalkBackSpans.expand("Hello, world."))
		assertNull(TalkBackSpans.expand(SpannableString("Hello, world.")))
	}

	@Test
	fun textSpansAreSpokenInPlaceOfTheirMarks() {
		val text = SpannableString("Hello (x), world.")
		text.span(6, "left paren")
		text.span(8, "right paren")
		assertEquals("Hello left paren x right paren , world.", TalkBackSpans.expand(text))
	}

	@Test
	fun verbatimSpansAreLeftToTheEngine() {
		val text = SpannableString("Call ABC.")
		text.setSpan(TtsSpan.VerbatimBuilder("ABC").build(), 5, 8, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
		assertNull(TalkBackSpans.expand(text))
	}

	private fun SpannableString.span(at: Int, said: String) =
		setSpan(TtsSpan.TextBuilder(said).build(), at, at + 1, Spanned.SPAN_EXCLUSIVE_EXCLUSIVE)
}
