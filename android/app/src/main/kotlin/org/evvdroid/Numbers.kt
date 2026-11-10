package org.evvdroid

/**
 * Intelligent number reading and processing for the Eloquence engine.
 *
 * Eloquence natively reads numbers as cardinal quantities. This processor allows
 * users to customize number reading behavior into:
 * - Default: Unmodified engine reading
 * - Digits: Reads numbers digit-by-digit (e.g. "404" -> "4 0 4")
 * - Pairs: Groups numbers into pairs (e.g. "1984" -> "19 84", "12345" -> "1 23 45")
 * - Triplets: Groups numbers into 3-digit chunks (e.g. "123456" -> "123 456")
 * - Words: Spells numbers out in words (e.g. "42" -> "forty-two")
 *
 * In addition, it supports:
 * - Natural time reading (e.g. "12:30" -> "12 30" to avoid colon pronunciation)
 * - Roman numeral conversion in contextual phrases (e.g. "Chapter IV" -> "Chapter 4")
 */
object Numbers {

	const val MODE_DEFAULT = 0
	const val MODE_DIGITS = 1
	const val MODE_PAIRS = 2
	const val MODE_TRIPLETS = 3
	const val MODE_WORDS = 4

	private val TIME_REGEX = Regex("""\b(\d{1,2}):(\d{2})(?::(\d{2}))?\b""")
	private val ROMAN_NUMERAL_CONTEXT = Regex(
		"""\b(Chapter|Part|Section|Volume|Book|Act|Scene|Room|Suite|Floor|Gate|King|Queen|Pope|Emperor|Henry|Louis|Edward|George|Charles|William|John|Paul|Richard|James|Alexander|Napoleon|Elizabeth|Tsar|Czar)\s+([IVXLCDM]+)\b""",
		RegexOption.IGNORE_CASE
	)
	private val NUMBER_TOKEN = Regex("""\b\d+\b""")

	/**
	 * Processes [text] according to [mode] and options.
	 */
	fun apply(
		text: String,
		mode: Int,
		readTimeNaturally: Boolean = true,
		readRomanNumerals: Boolean = true
	): String {
		if (text.isEmpty()) return text
		var out = text

		if (readTimeNaturally) {
			out = formatTime(out)
		}

		if (readRomanNumerals) {
			out = convertRomanNumerals(out)
		}

		if (mode == MODE_DEFAULT) {
			return out
		}

		out = NUMBER_TOKEN.replace(out) { match ->
			formatNumber(match.value, mode)
		}

		return out
	}

	private fun formatTime(text: String): String {
		return TIME_REGEX.replace(text) { match ->
			val h = match.groupValues[1]
			val m = match.groupValues[2]
			val s = match.groupValues[3]
			if (s.isNotEmpty()) "$h $m $s" else "$h $m"
		}
	}

	private fun convertRomanNumerals(text: String): String {
		return ROMAN_NUMERAL_CONTEXT.replace(text) { match ->
			val prefix = match.groupValues[1]
			val roman = match.groupValues[2].uppercase()
			val decimal = romanToDecimal(roman)
			if (decimal != null) "$prefix $decimal" else match.value
		}
	}

	private fun romanToDecimal(roman: String): Int? {
		if (roman.isEmpty() || !roman.matches(Regex("""^M{0,4}(CM|CD|D?C{0,3})(XC|XL|L?X{0,3})(IX|IV|V?I{0,3})$"""))) {
			return null
		}
		val values = mapOf('I' to 1, 'V' to 5, 'X' to 10, 'L' to 50, 'C' to 100, 'D' to 500, 'M' to 1000)
		var total = 0
		var prev = 0
		for (c in roman.reversed()) {
			val curr = values[c] ?: return null
			if (curr < prev) {
				total -= curr
			} else {
				total += curr
			}
			prev = curr
		}
		return if (total > 0) total else null
	}

	private fun formatNumber(numStr: String, mode: Int): String {
		if (numStr.length <= 1) return numStr
		return when (mode) {
			MODE_DIGITS -> numStr.toCharArray().joinToString(" ")
			MODE_PAIRS -> groupDigits(numStr, 2)
			MODE_TRIPLETS -> groupDigits(numStr, 3)
			MODE_WORDS -> numberToWords(numStr)
			else -> numStr
		}
	}

	/**
	 * Groups digits in chunks of [chunkSize] from right to left (e.g. "12345" with chunk 2 -> "1 23 45").
	 */
	private fun groupDigits(numStr: String, chunkSize: Int): String {
		if (numStr.length <= chunkSize) return numStr
		val chunks = mutableListOf<String>()
		var i = numStr.length
		while (i > 0) {
			val start = (i - chunkSize).coerceAtLeast(0)
			chunks.add(numStr.substring(start, i))
			i = start
		}
		return chunks.reversed().joinToString(" ")
	}

	private val ONES = arrayOf(
		"zero", "one", "two", "three", "four", "five", "six", "seven", "eight", "nine",
		"ten", "eleven", "twelve", "thirteen", "fourteen", "fifteen", "sixteen", "seventeen", "eighteen", "nineteen"
	)
	private val TENS = arrayOf(
		"", "", "twenty", "thirty", "forty", "fifty", "sixty", "seventy", "eighty", "ninety"
	)

	private fun numberToWords(numStr: String): String {
		val num = numStr.toLongOrNull() ?: return groupDigits(numStr, 3)
		if (num == 0L) return "zero"
		if (num < 0L) return "minus " + numberToWords((-num).toString())
		return convertChunk(num).trim()
	}

	private fun convertChunk(n: Long): String {
		return when {
			n < 20 -> ONES[n.toInt()]
			n < 100 -> {
				val ten = TENS[(n / 10).toInt()]
				val rem = n % 10
				if (rem > 0) "$ten-${ONES[rem.toInt()]}" else ten
			}
			n < 1000 -> {
				val hundred = ONES[(n / 100).toInt()] + " hundred"
				val rem = n % 100
				if (rem > 0) "$hundred " + convertChunk(rem) else hundred
			}
			n < 1000000 -> {
				val thousands = convertChunk(n / 1000) + " thousand"
				val rem = n % 1000
				if (rem > 0) "$thousands " + convertChunk(rem) else thousands
			}
			n < 1000000000 -> {
				val millions = convertChunk(n / 1000000) + " million"
				val rem = n % 1000000
				if (rem > 0) "$millions " + convertChunk(rem) else millions
			}
			n < 1000000000000L -> {
				val billions = convertChunk(n / 1000000000L) + " billion"
				val rem = n % 1000000000L
				if (rem > 0) "$billions " + convertChunk(rem) else billions
			}
			else -> numToStrDigits(n.toString())
		}
	}

	private fun numToStrDigits(str: String): String =
		str.map { ONES.getOrElse(it - '0') { it.toString() } }.joinToString(" ")
}
