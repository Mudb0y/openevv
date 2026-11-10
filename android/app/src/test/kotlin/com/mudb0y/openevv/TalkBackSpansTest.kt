package com.mudb0y.openevv

import org.junit.Assert.assertEquals
import org.junit.Assert.assertNull
import org.junit.Test

class TalkBackSpansTest {

	private fun span(start: Int, end: Int, text: String) = TalkBackSpans.SpokenSpan(start, end, text)

	@Test
	fun testNoSpansMeansNoExpansion() {
		assertNull(TalkBackSpans.expandSpans("Hello, world", emptyList()))
		// Spans outside the text or empty ones are ignored
		assertNull(TalkBackSpans.expandSpans("Hi", listOf(span(1, 9, "x"), span(1, 1, "x"), span(-1, 1, "x"))))
	}

	@Test
	fun testTalkBackPunctuationSpansAreSpoken() {
		// TalkBack at "all": one text span on each mark, and the phrase and
		// sentence marks stay behind their names for the pause
		val expanded = TalkBackSpans.expandSpans(
			"Hello, world (ok).",
			listOf(span(5, 6, "comma"), span(13, 14, "left paren"), span(16, 17, "right paren"), span(17, 18, "period"))
		)
		assertEquals("Hello comma, world left paren ok right paren period. ", expanded)
	}

	@Test
	fun testWhatTalkBackLeftIsLeftToTheEngine() {
		// TalkBack at "most" names the brackets and leaves the comma and the full stop
		val expanded = TalkBackSpans.expandSpans(
			"Hello (x), world.",
			listOf(span(6, 7, "left paren"), span(8, 9, "right paren"))
		)
		assertEquals("Hello left paren x right paren , world.", expanded)
	}

	@Test
	fun testOtherSpans() {
		// A run of dashes collapsed by TalkBack, which it does at every level
		assertEquals("a 4 dashes b, c", TalkBackSpans.expandSpans("a ---- b, c", listOf(span(2, 6, "4 dashes"))))
		// A span on a single letter is not punctuation either
		assertEquals(" ex = 1", TalkBackSpans.expandSpans("x = 1", listOf(span(0, 1, "ex"))))
		assertEquals(" star Rated 5, good.", TalkBackSpans.expandSpans("★ Rated 5, good.", listOf(span(0, 1, "star"))))
	}

	@Test
	fun testOverlappingSpansKeepTheFirst() {
		assertEquals(" early ef", TalkBackSpans.expandSpans("abcdef", listOf(span(3, 5, "late"), span(0, 4, "early"))))
	}
}
