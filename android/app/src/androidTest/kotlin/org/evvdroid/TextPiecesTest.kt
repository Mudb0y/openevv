package org.evvdroid

import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertTrue
import org.junit.Test

/** The rule that decides where an utterance may be cut. Its numbers are
 *  openevv's, measured on the engine, and the cases that must not be cut are
 *  the ones that cost an audible pause when they were. */
class TextPiecesTest {

	@Test
	fun aFullStopEndsASentence() {
		assertTrue(TextPieces.endsSentence("dog."))
		assertTrue(TextPieces.endsSentence("dog?"))
		assertTrue(TextPieces.endsSentence("dog!"))
		assertTrue(TextPieces.endsSentence("\"dog.\""))
		assertTrue(TextPieces.endsSentence("(dog.)"))
	}

	@Test
	fun anAbbreviationOrAnInitialDoesNot() {
		assertFalse("a title", TextPieces.endsSentence("Mr."))
		assertFalse("a title", TextPieces.endsSentence("Mrs."))
		assertFalse("a title", TextPieces.endsSentence("Dr."))
		assertFalse("an initial", TextPieces.endsSentence("J."))
		assertFalse("a dotted abbreviation", TextPieces.endsSentence("e.g."))
		assertFalse("a dotted abbreviation", TextPieces.endsSentence("U.S."))
		assertFalse("a decimal", TextPieces.endsSentence("3."))
		assertFalse("no punctuation at all", TextPieces.endsSentence("dog"))
	}

	@Test
	fun theTextIsAllThereAndInOrder() {
		for (text in SAMPLES) {
			assertEquals("$text was not preserved", text, TextPieces.split(text).joinToString(""))
		}
	}

	@Test
	fun sentencesBecomePieces() {
		val pieces = TextPieces.split("One two three. Four five six. Seven eight nine.")
		assertEquals(3, pieces.size)
		assertTrue(pieces[0].trim().endsWith("three."))
		assertTrue(pieces[1].trim().endsWith("six."))
	}

	@Test
	fun aTitleDoesNotStartANewPiece() {
		val pieces = TextPieces.split("Mr. Jones asked whether the header is read first, and Mrs. Adams said it is.")
		assertEquals("a title was cut at", 1, pieces.size)
	}

	@Test
	fun initialsDoNotStartNewPieces() {
		val pieces = TextPieces.split("The book by J. R. R. Tolkien is on the shelf by the door.")
		assertEquals("an initial was cut at", 1, pieces.size)
	}

	@Test
	fun textWithNoSentenceEndIsStillBounded() {
		val runOn = "word ".repeat(400)
		val pieces = TextPieces.split(runOn)
		assertTrue("nothing was cut in ${runOn.length} characters", pieces.size > 1)
		for (piece in pieces.dropLast(1)) {
			assertTrue("a piece ran to ${piece.length}", piece.length < TextPieces.PIECE_CAP * 2)
		}
	}

	@Test
	fun shortTextIsOnePiece() {
		assertEquals(listOf("Settings."), TextPieces.split("Settings."))
		assertEquals(listOf("Navigate up"), TextPieces.split("Navigate up"))
	}

	private companion object {
		val SAMPLES = listOf(
			"",
			"One.",
			"Settings. Button.",
			"Mr. Jones and J. R. R. Tolkien met at 3.15 on the U.S. side.",
			"word ".repeat(300),
			"   leading and trailing   ",
			"no punctuation whatsoever here at all"
		)
	}
}
