package org.evvdroid

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNull
import org.junit.Assert.assertTrue
import org.junit.Test

class PunctuationAndFixesTest {

	private fun span(start: Int, end: Int, text: String) = Punctuation.SpokenSpan(start, end, text)

	@Test
	fun testSliderValuesUnbroken() {
		// Slider values and percentages must never be corrupted or broken
		assertEquals("50%", TextFixes.apply("50%"))
		assertEquals("0%", TextFixes.apply("0%"))
		assertEquals("100%", TextFixes.apply("100%"))
		assertEquals("50 percent", TextFixes.apply("50 percent"))
		assertEquals("50%, Volume slider", TextFixes.apply("50%, Volume slider"))
		assertEquals("50% seek bar", TextFixes.apply("50% seek bar"))
	}

	@Test
	fun testTextFixesLeaveMeaningAlone() {
		// A mark that belongs to what follows it is part of what is said
		assertEquals(".5 mg", TextFixes.apply(".5 mg"))
		assertEquals(".com", TextFixes.apply(".com"))
		assertEquals("doses of 1, .5 and 2", TextFixes.apply("doses of 1, .5 and 2"))
	}

	@Test
	fun testGroupedThousandsFixes() {
		assertEquals("1000000", TextFixes.apply("1,000,000"))
		assertEquals("1000000 items", TextFixes.apply("1,000,000 items"))
		assertEquals("2000000000", TextFixes.apply("2,000,000,000"))
		assertEquals("12,345", TextFixes.apply("12,345"))
		// A list of numbers is not one number
		assertEquals("1,000,5", TextFixes.apply("1,000,5"))
		assertEquals("2,000,10,12", TextFixes.apply("2,000,10,12"))
	}

	@Test
	fun testPunctuationSomeLevelDoesNotVocalizeCommaOrPeriod() {
		val input = "Hello, world. This is a test."
		val processed = Punctuation.apply(input, Punctuation.LEVEL_SOME, 0x00010000)
		assertFalse("Comma should not be pronounced at LEVEL_SOME", processed.contains("comma", ignoreCase = true))
		assertFalse("Period should not be pronounced at LEVEL_SOME", processed.contains("period", ignoreCase = true))
		assertFalse("Dot should not be pronounced at LEVEL_SOME", processed.contains("dot", ignoreCase = true))
	}

	@Test
	fun testPunctuationSomeLevelVocalizesBullet() {
		val input = "• First item"
		val processedEn = Punctuation.apply(input, Punctuation.LEVEL_SOME, 0x00010000)
		assertTrue("Bullet should be pronounced at LEVEL_SOME in English", processedEn.contains("bullet", ignoreCase = true))

		val processedEs = Punctuation.apply(input, Punctuation.LEVEL_SOME, 0x00020000)
		assertTrue("Bullet should be pronounced at LEVEL_SOME in Spanish", processedEs.contains("viñeta", ignoreCase = true))

		val processedFr = Punctuation.apply(input, Punctuation.LEVEL_SOME, 0x00030000)
		assertTrue("Bullet should be pronounced at LEVEL_SOME in French", processedFr.contains("puce", ignoreCase = true))

		val processedDe = Punctuation.apply(input, Punctuation.LEVEL_SOME, 0x00040000)
		assertTrue("Bullet should be pronounced at LEVEL_SOME in German", processedDe.contains("aufzählung", ignoreCase = true))
	}

	@Test
	fun testSomeLevelKeepsItsSymbols() {
		// What "some" has always spoken stays spoken for people already on it
		val someResult = Punctuation.apply("$5 a_b ^x ~y \\z #1", Punctuation.LEVEL_SOME, 0x00010000)
		assertEquals(" dollar 5 a underscore b caret x tilda y backslash z hash 1", someResult)
	}

	@Test
	fun testSomeVsMostPunctuationClassification() {
		val text = "test: item; a|b and `c"

		// At LEVEL_SOME, none of :, ;, |, ` should be vocalized
		val someResult = Punctuation.apply(text, Punctuation.LEVEL_SOME, 0x00010000)
		assertFalse(someResult.contains("colon", ignoreCase = true))
		assertFalse(someResult.contains("semi", ignoreCase = true))
		assertFalse(someResult.contains("bar", ignoreCase = true))
		assertFalse(someResult.contains("graav", ignoreCase = true))

		// At LEVEL_MOST, all of them should be vocalized
		val mostResult = Punctuation.apply(text, Punctuation.LEVEL_MOST, 0x00010000)
		assertTrue(mostResult.contains("colon", ignoreCase = true))
		assertTrue(mostResult.contains("semi", ignoreCase = true))
		assertTrue(mostResult.contains("bar", ignoreCase = true))
		assertTrue(mostResult.contains("graav", ignoreCase = true))
	}

	@Test
	fun testNvdaPunctuationAllLevel() {
		val text = "Really? Yes! It is $5, right? ... Wait."
		val processed = Punctuation.apply(text, Punctuation.LEVEL_ALL, 0x00010000)
		assertTrue("Question mark should be pronounced as question", processed.contains("question", ignoreCase = true))
		assertTrue("Exclamation mark should be pronounced as bang", processed.contains("bang", ignoreCase = true))
		assertTrue("Dollar should be pronounced as dollar", processed.contains("dollar", ignoreCase = true))
		assertTrue("Comma should be pronounced as comma", processed.contains("comma", ignoreCase = true))
		assertTrue("Ellipsis should be pronounced as dot dot dot", processed.contains("dot dot dot", ignoreCase = true))
		assertTrue("Period should be pronounced as dot", processed.contains("dot", ignoreCase = true))
	}

	@Test
	fun testDotsAtAllLevelAreNamedOnce() {
		val all = Punctuation.LEVEL_ALL
		assertEquals("Wait dot dot dot. ok", Punctuation.apply("Wait... ok", all, 0x00010000))
		assertEquals("Wait dot dot dot. ok", Punctuation.apply("Wait… ok", all, 0x00010000))
		assertEquals("a dot dot dot. b", Punctuation.apply("a.... b", all, 0x00010000))
		// A longer run is an ellipsis in every language, not English words
		assertEquals("Ok points de suspension. fin", Punctuation.apply("Ok.... fin", all, 0x00030000))
		assertEquals("End dot. ", Punctuation.apply("End.", all, 0x00010000))
		// A dot on its own is still a dot
		assertEquals(" dot. ", Punctuation.apply(".", all, 0x00010000))
		// A dot inside a word is named, a decimal point is not
		assertEquals("a dot. b costs 1.5", Punctuation.apply("a.b costs 1.5", all, 0x00010000))
	}

	@Test
	fun testNvdaPunctuationMostParens() {
		val text = "(test) and [bracket]"
		val processed = Punctuation.apply(text, Punctuation.LEVEL_MOST, 0x00010000)
		assertTrue("Open paren should be left paren", processed.contains("left paren", ignoreCase = true))
		assertTrue("Close paren should be right paren", processed.contains("right paren", ignoreCase = true))
		assertTrue("Open bracket should be left bracket", processed.contains("left bracket", ignoreCase = true))
		assertTrue("Close bracket should be right bracket", processed.contains("right bracket", ignoreCase = true))
	}

	@Test
	fun testNoSpansMeansNoExpansion() {
		assertNull(Punctuation.expandSpans("Hello, world", emptyList()))
		// Spans that point outside the text or cover nothing are not spans
		assertNull(Punctuation.expandSpans("Hi", listOf(span(1, 9, "x"), span(1, 1, "x"), span(-1, 1, "x"))))
	}

	@Test
	fun testTalkBackPunctuationSpansAreSpoken() {
		// TalkBack at "all": one text span on each mark
		val expanded = Punctuation.expandSpans(
			"Hello, world (ok).",
			listOf(span(5, 6, "comma"), span(13, 14, "left paren"), span(16, 17, "right paren"), span(17, 18, "period"))
		)!!
		// Whatever the user's own level, each mark is named once
		for (level in Punctuation.LEVEL_NONE..Punctuation.LEVEL_ALL) {
			assertEquals(
				"Hello comma, world left paren ok right paren period. ",
				Punctuation.apply(expanded, level, 0x00010000)
			)
		}
	}

	@Test
	fun testUserLevelStillNamesWhatTalkBackLeft() {
		// TalkBack at "most" names the parens; evvdroid at "all" names the rest
		val expanded = Punctuation.expandSpans(
			"Hello (x), world.",
			listOf(span(6, 7, "left paren"), span(8, 9, "right paren"))
		)!!
		assertEquals(
			"Hello left paren x right paren comma, world dot. ",
			Punctuation.apply(expanded, Punctuation.LEVEL_ALL, 0x00010000)
		)
		// A described symbol does not turn the user's level off either
		val star = Punctuation.expandSpans("★ Rated 5, good.", listOf(span(0, 1, "star")))!!
		assertEquals(" star Rated 5 comma, good dot. ", Punctuation.apply(star, Punctuation.LEVEL_ALL, 0x00010000))
	}

	@Test
	fun testOtherSpansLeaveThePunctuationLevelAlone() {
		// A run of dashes collapsed by TalkBack, which it does at every level
		val dashes = Punctuation.expandSpans("a ---- b, c", listOf(span(2, 6, "4 dashes")))!!
		assertEquals("a 4 dashes b, c", dashes)

		// A span on a single letter is not punctuation either
		val letter = Punctuation.expandSpans("x = 1", listOf(span(0, 1, "ex")))!!
		assertEquals(" ex = 1", letter)
	}

	@Test
	fun testOverlappingSpansKeepTheFirst() {
		val expanded = Punctuation.expandSpans("abcdef", listOf(span(3, 5, "late"), span(0, 4, "early")))!!
		assertEquals(" early ef", expanded)
	}

	@Test
	fun testNamesAreNotReadForMarks() {
		// French names carry an apostrophe, which "all" also names
		assertEquals(
			"Vraiment point d'interrogation? Oui point d'exclamation! ",
			Punctuation.apply("Vraiment? Oui!", Punctuation.LEVEL_ALL, 0x00030000)
		)
		assertEquals("l apostrophe eau", Punctuation.apply("l'eau", Punctuation.LEVEL_ALL, 0x00030000))
	}

	@Test
	fun testSpanishOpeningMarks() {
		assertEquals(
			" abrir interrogación Qué cerrar interrogación? ",
			Punctuation.apply("¿Qué?", Punctuation.LEVEL_ALL, 0x00020000)
		)
		// Below "all" they are left for the engine, like their closing halves
		assertEquals("¡Hola!", Punctuation.apply("¡Hola!", Punctuation.LEVEL_MOST, 0x00020000))
	}
}
