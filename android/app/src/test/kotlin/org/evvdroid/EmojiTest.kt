package org.evvdroid

import org.junit.Assert.assertEquals
import org.junit.Assert.assertTrue
import org.junit.Test
import java.io.File

class EmojiTest {

	@Test
	fun testBuiltInEmojis() {
		// Grinning face
		val r1 = Emoji.apply("Hello 😀 world")
		assertTrue(r1.contains("grinning face"))

		// Red heart
		val r2 = Emoji.apply("Love ❤️ you")
		assertTrue(r2.contains("red heart"))

		// Thumbs up
		val r3 = Emoji.apply("Great job 👍!")
		assertTrue(r3.contains("thumbs up"))

		// Fire
		val r4 = Emoji.apply("That is 🔥")
		assertTrue(r4.contains("fire"))

		// Party popper
		val r5 = Emoji.apply("Congrats 🎉")
		assertTrue(r5.contains("party popper"))
	}

	@Test
	fun testMultipleEmojis() {
		val result = Emoji.apply("Check this out: 😀🔥👍")
		assertTrue(result.contains("grinning face"))
		assertTrue(result.contains("fire"))
		assertTrue(result.contains("thumbs up"))
	}

	@Test
	fun testUserCustomEmojiOverrides() {
		// Create a temporary emoji dictionary file
		val tempFile = File.createTempFile("test-emoji", ".dic")
		try {
			// Override grinning face and add a custom rocket
			tempFile.writeText("😀\tcustom beaming face\r\n🚀\tsuper rocket\r\n", Charsets.UTF_8)
			Emoji.loadUserEmoji(tempFile)

			val text1 = Emoji.apply("I am 😀")
			assertTrue("Expected custom beaming face but got: $text1", text1.contains("custom beaming face"))

			val text2 = Emoji.apply("Launch the 🚀")
			assertTrue("Expected super rocket but got: $text2", text2.contains("super rocket"))
		} finally {
			tempFile.delete()
			// Reset user overrides with an empty file
			val emptyFile = File.createTempFile("empty-emoji", ".dic")
			Emoji.loadUserEmoji(emptyFile)
			emptyFile.delete()
		}
	}

	@Test
	fun testTextWithoutEmojisRemainsUnchanged() {
		val input = "This is a normal English sentence without any emojis."
		val output = Emoji.apply(input)
		assertEquals(input, output)
	}

	@Test
	fun testNumbersAndPunctuationTogether() {
		var text = "Meeting at 14:30 😀! Room IV."
		// 1. Emoji apply
		text = Emoji.apply(text)
		assertTrue(text.contains("grinning face"))

		// 2. Numbers apply
		text = Numbers.apply(text, mode = Numbers.MODE_DEFAULT, readTimeNaturally = true, readRomanNumerals = true)
		assertTrue(text.contains("14 30"))
		assertTrue(text.contains("Room 4"))

		// 3. Punctuation apply (Some)
		text = Punctuation.apply(text, Punctuation.LEVEL_SOME)
		assertTrue(text.contains("grinning face"))
	}

	@Test
	fun testSuspendedDictionaryEntry() {
		val tempFile = File.createTempFile("test-dict", ".dic")
		try {
			// 1. Add active entry
			Dictionaries.addOrUpdateDictEntry(tempFile, "overview", "recents")
			var entries = Dictionaries.readDictEntries(tempFile)
			assertEquals(1, entries.size)
			assertEquals(false, entries[0].suspended)
			assertEquals(1, Dictionaries.count(tempFile))

			// 2. Suspend entry
			Dictionaries.toggleSuspendDictEntry(tempFile, "overview")
			entries = Dictionaries.readDictEntries(tempFile)
			assertEquals(1, entries.size)
			assertEquals(true, entries[0].suspended)
			assertEquals(0, Dictionaries.count(tempFile))

			// Verify cached entry creation skips suspended entry
			val cached = Dictionaries.createCachedCaseSensitiveEntry(entries[0].copy(caseSensitive = true))
			org.junit.Assert.assertNull(cached)
		} finally {
			tempFile.delete()
		}
	}

	@Test
	fun testSuspendedEmojiEntryRevertsToDefault() {
		val tempFile = File.createTempFile("test-emoji-suspend", ".dic")
		try {
			// Write suspended emoji override (starting with #)
			tempFile.writeText("#😀\tcustom smile\r\n", Charsets.UTF_8)
			Emoji.loadUserEmoji(tempFile)

			val text = Emoji.apply("Hello 😀")
			// Should speak the built-in original word "grinning face", NOT "custom smile"
			assertTrue("Expected grinning face, got: $text", text.contains("grinning face"))
			org.junit.Assert.assertFalse(text.contains("custom smile"))
		} finally {
			tempFile.delete()
			val emptyFile = File.createTempFile("empty-emoji", ".dic")
			Emoji.loadUserEmoji(emptyFile)
			emptyFile.delete()
		}
	}
}
