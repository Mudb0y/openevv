package com.mudb0y.openevv

import java.nio.CharBuffer
import java.nio.charset.Charset
import java.nio.charset.CharsetEncoder
import java.nio.charset.CodingErrorAction
import java.text.Normalizer

/**
 * Android text turned into the bytes the language in force reads, the way the
 * NVDA add-on turns NVDA's: textFor in nvda/addon/synthDrivers/_openevv.py.
 * The same text reaches the engine as the same bytes from either, so it is
 * read the same way, and anything the engine reads wrongly is the engine's to
 * put right rather than this file's.
 *
 * Every language IBM shipped bar one reads the Windows Western set. Japanese
 * has a romanizer in front of the engine, and that reads Shift-JIS in
 * Microsoft's form of it, which is fixed when the instance is made. Polish
 * declares letters of its own and the engine converts its text from UTF-8
 * itself, so Polish is handed UTF-8.
 *
 * What a code set has no room for becomes the nearest thing it has where
 * there is one: a space for a space, a ligature's letters, a Latin letter with
 * as few of its accents taken off as will make one the set has, so Dvořák is
 * Dvorák. Anything else becomes a space. That is the one difference from the
 * add-on, which sends a question mark, as IBM's drivers always did: NVDA has
 * named every symbol and emoji itself before the add-on is handed the text,
 * so little is left there, but Android hands a voice the text as it is, and a
 * question mark in place of an emoji would make the sentence before it a
 * question.
 */
object EngineText {

	/**
	 * The bytes to hand the engine speaking [language], without a terminator:
	 * the native side adds one, and a nought anywhere inside would end the text
	 * early, so there is none in what comes back.
	 */
	fun encode(language: Int, text: String): ByteArray {
		val laid = laidOut(text)
		return when (Eci.baseLanguage(language)) {
			Eci.POLISH -> laid.toByteArray(Charsets.UTF_8)
			Eci.JAPANESE -> into(laid, JAPANESE_SET ?: WESTERN, latin = false)
			else -> into(laid, WESTERN, latin = true)
		}
	}

	private val WESTERN: Charset = Charset.forName("windows-1252")

	private val JAPANESE_SET: Charset? =
		runCatching { Charset.forName("windows-31j") }.getOrNull()
			?: runCatching { Charset.forName("Shift_JIS") }.getOrNull()

	/** Characters that lay text out and say nothing, and the few that lay it
	 *  out and stand for something the code sets have. */
	private val LAYOUT: Map<Char, String> =
		"­؜‎‏‪‫‬‭‮⁠⁦⁧⁨⁩﻿︎️"
			.associate { it to "" } + mapOf(
			'​' to " ", ' ' to "\n", ' ' to "\n\n",
			'‐' to "-", '‑' to "-", '\u0000' to " "
		)

	/** What to say instead of a character the code set has not got. Only these,
	 *  and not compatibility decomposition across the board, since a fraction or
	 *  an exponent means something its decomposition does not. */
	private val INSTEAD: Map<String, String> = mapOf(
		"‒" to "–", "―" to "—", "‛" to "'", "‟" to "\"",
		"․" to ".", "‥" to "..", "ẞ" to "SS",
		"ﬀ" to "ff", "ﬁ" to "fi", "ﬂ" to "fl", "ﬃ" to "ffi",
		"ﬄ" to "ffl", "ﬅ" to "st", "ﬆ" to "st"
	) + (0xff01..0xff5e).filter { it != 0xff40 }.associate {
		String(Character.toChars(it)) to String(Character.toChars(it - 0xfee0))
	}

	/** Composed, so that a u followed by a combining diaeresis says what the u
	 *  with one does, and with what only lays the text out taken away. */
	private fun laidOut(text: String): String {
		if (text.all { it.code in 1 until 0x80 }) return text
		val out = StringBuilder(text.length)
		for (c in text) out.append(LAYOUT[c] ?: c)
		return Normalizer.normalize(out, Normalizer.Form.NFC)
	}

	private fun into(text: String, set: Charset, latin: Boolean): ByteArray {
		val has = set.newEncoder()
		val out = StringBuilder(text.length)
		var i = 0
		while (i < text.length) {
			val point = text.codePointAt(i)
			val one = String(Character.toChars(point))
			i += one.length
			out.append(if (has.canEncode(one)) one else instead(one, point, has, latin))
		}
		val writer = set.newEncoder()
			.onMalformedInput(CodingErrorAction.REPLACE)
			.onUnmappableCharacter(CodingErrorAction.REPLACE)
			.replaceWith(byteArrayOf(' '.code.toByte()))
		val made = writer.encode(CharBuffer.wrap(out))
		val bytes = ByteArray(made.remaining())
		made.get(bytes)
		return bytes
	}

	private fun instead(one: String, point: Int, has: CharsetEncoder, latin: Boolean): String {
		if (Character.getType(point) == Character.SPACE_SEPARATOR.toInt()) return " "
		val listed = INSTEAD[one] ?: if (latin) nearestLatin(one, point, has) else null
		return if (listed != null && has.canEncode(listed)) listed else " "
	}

	/** A Latin letter the set has not got, with as few of its accents taken
	 *  off as will make it one it has: u with a diaeresis and a caron keeps the
	 *  diaeresis. */
	private fun nearestLatin(one: String, point: Int, has: CharsetEncoder): String? {
		if (!Character.isLetter(point)) return null
		if (Character.getName(point)?.startsWith("LATIN ") != true) return null
		val parts = Normalizer.normalize(one, Normalizer.Form.NFD)
		if (parts.length < 2 || !parts.substring(1).all(::isCombining)) return null
		for (end in parts.length - 1 downTo 1) {
			val candidate = Normalizer.normalize(parts.substring(0, end), Normalizer.Form.NFC)
			if (has.canEncode(candidate)) return candidate
		}
		return null
	}

	private fun isCombining(c: Char): Boolean = when (Character.getType(c).toByte()) {
		Character.NON_SPACING_MARK,
		Character.COMBINING_SPACING_MARK,
		Character.ENCLOSING_MARK -> true
		else -> false
	}
}
