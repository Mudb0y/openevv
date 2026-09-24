package org.evvdroid

/**
 * Punctuation pronunciation processing for Eloquence TTS.
 *
 * Supports 4 verbosity levels:
 * - LEVEL_NONE (0): Punctuation symbols are not spoken (they only provide standard pauses).
 * - LEVEL_SOME (1): Essential symbols are spoken (e.g. @, #, $, %, &, *, +, =, /, \, _, ^, <, >).
 * - LEVEL_MOST (2): Level SOME plus parentheses, brackets, braces, quotes, dashes, ellipsis, bullet.
 * - LEVEL_ALL  (3): Every punctuation mark is spoken, including periods, commas, colons, semicolons,
 *                   exclamation marks, and question marks.
 */
object Punctuation {

	const val LEVEL_NONE = 0
	const val LEVEL_SOME = 1
	const val LEVEL_MOST = 2
	const val LEVEL_ALL = 3

	fun apply(text: String, level: Int, language: Int? = null): String {
		if (text.isEmpty() || level <= LEVEL_NONE) return text
		val langCode = language?.let { Eci.localeOf(it)?.first } ?: "eng"
		val map = getSymbolMap(langCode)

		var out = text

		// 1. Process LEVEL_SOME symbols
		for (ch in SOME_SYMBOLS) {
			if (out.indexOf(ch) >= 0) {
				val name = map[ch] ?: continue
				out = out.replace(ch.toString(), " $name ")
			}
		}

		if (level < LEVEL_MOST) {
			return collapseSpaces(out)
		}

		// 2. Process LEVEL_MOST symbols
		// Ellipsis first
		if (out.contains("...")) {
			val ellipsisName = map['…'] ?: "ellipsis"
			out = out.replace("...", " $ellipsisName ")
		}
		for (ch in MOST_SYMBOLS) {
			if (out.indexOf(ch) >= 0) {
				val name = map[ch] ?: continue
				out = out.replace(ch.toString(), " $name ")
			}
		}

		if (level < LEVEL_ALL) {
			return collapseSpaces(out)
		}

		// 3. Process LEVEL_ALL symbols (sentence & clause punctuation)
		// We speak the punctuation name while keeping the punctuation mark for cadence/pausing
		for (ch in ALL_SYMBOLS) {
			if (out.indexOf(ch) >= 0) {
				val name = map[ch] ?: continue
				// Replace e.g. "." with " dot." so the engine speaks "dot" and observes sentence pause
				out = when (ch) {
					'.' -> out.replace(Regex("""(?<!\d)\.(?!\d)"""), " $name. ")
					',' -> out.replace(Regex("""(?<!\d),(?!\d)"""), " $name, ")
					':' -> out.replace(Regex("""(?<!\d):(?!\d)"""), " $name: ")
					';' -> out.replace(";", " $name; ")
					'?' -> out.replace("?", " $name? ")
					'!' -> out.replace("!", " $name! ")
					else -> out.replace(ch.toString(), " $name ")
				}
			}
		}

		return collapseSpaces(out)
	}

	private fun collapseSpaces(text: String): String =
		text.replace(Regex("""[ \t]+"""), " ")

	private val SOME_SYMBOLS = charArrayOf(
		'@', '#', '$', '%', '&', '*', '+', '=', '/', '\\', '_', '^', '<', '>', '~'
	)

	private val MOST_SYMBOLS = charArrayOf(
		'(', ')', '[', ']', '{', '}', '"', '“', '”', '‘', '’', '-', '—', '–', '•', '…'
	)

	private val ALL_SYMBOLS = charArrayOf(
		'.', ',', ':', ';', '?', '!', '|', '`'
	)

	private fun getSymbolMap(langCode: String): Map<Char, String> = when (langCode) {
		"spa" -> SPANISH_MAP
		"fra" -> FRENCH_MAP
		"deu" -> GERMAN_MAP
		"ita" -> ITALIAN_MAP
		"pol" -> POLISH_MAP
		else -> ENGLISH_MAP
	}

	private val ENGLISH_MAP: Map<Char, String> = mapOf(
		'@' to "at",
		'#' to "hash",
		'$' to "dollar",
		'%' to "percent",
		'&' to "and",
		'*' to "star",
		'+' to "plus",
		'=' to "equals",
		'/' to "slash",
		'\\' to "backslash",
		'_' to "underscore",
		'^' to "caret",
		'<' to "less than",
		'>' to "greater than",
		'~' to "tilde",
		'(' to "left paren",
		')' to "right paren",
		'[' to "left bracket",
		']' to "right bracket",
		'{' to "left brace",
		'}' to "right brace",
		'"' to "quote",
		'“' to "left quote",
		'”' to "right quote",
		'‘' to "left single quote",
		'’' to "right single quote",
		'-' to "dash",
		'—' to "dash",
		'–' to "dash",
		'•' to "bullet",
		'…' to "ellipsis",
		'.' to "dot",
		',' to "comma",
		':' to "colon",
		';' to "semicolon",
		'?' to "question mark",
		'!' to "exclamation mark",
		'|' to "vertical bar",
		'`' to "backtick"
	)

	private val SPANISH_MAP: Map<Char, String> = ENGLISH_MAP + mapOf(
		'@' to "arroba",
		'#' to "almohadilla",
		'$' to "dólar",
		'%' to "por ciento",
		'&' to "y",
		'*' to "asterisco",
		'+' to "más",
		'=' to "igual",
		'/' to "barra",
		'\\' to "barra invertida",
		'_' to "guión bajo",
		'<' to "menor que",
		'>' to "mayor que",
		'(' to "abrir paréntesis",
		')' to "cerrar paréntesis",
		'[' to "abrir corchete",
		']' to "cerrar corchete",
		'"' to "comillas",
		'-' to "guión",
		'.' to "punto",
		',' to "coma",
		':' to "dos puntos",
		';' to "punto y coma",
		'?' to "interrogación",
		'!' to "exclamación"
	)

	private val FRENCH_MAP: Map<Char, String> = ENGLISH_MAP + mapOf(
		'@' to "arobase",
		'#' to "dièse",
		'$' to "dollar",
		'%' to "pour cent",
		'&' to "et",
		'*' to "étoile",
		'+' to "plus",
		'=' to "égale",
		'/' to "barre oblique",
		'\\' to "barre oblique inversée",
		'_' to "tiret bas",
		'<' to "inférieur à",
		'>' to "supérieur à",
		'(' to "parenthèse ouvrante",
		')' to "parenthèse fermante",
		'"' to "guillemets",
		'-' to "tiret",
		'.' to "point",
		',' to "virgule",
		':' to "deux points",
		';' to "point virgule",
		'?' to "point d'interrogation",
		'!' to "point d'exclamation"
	)

	private val GERMAN_MAP: Map<Char, String> = ENGLISH_MAP + mapOf(
		'@' to "at",
		'#' to "raute",
		'$' to "dollar",
		'%' to "prozent",
		'&' to "und",
		'*' to "stern",
		'+' to "plus",
		'=' to "gleich",
		'/' to "schrägstrich",
		'\\' to "umgekehrter schrägstrich",
		'_' to "unterstrich",
		'<' to "kleiner als",
		'>' to "größer als",
		'(' to "klammer auf",
		')' to "klammer zu",
		'"' to "anführungszeichen",
		'-' to "bindestrich",
		'.' to "punkt",
		',' to "komma",
		':' to "doppelpunkt",
		';' to "semikolon",
		'?' to "fragezeichen",
		'!' to "ausrufezeichen"
	)

	private val ITALIAN_MAP: Map<Char, String> = ENGLISH_MAP + mapOf(
		'@' to "chiocciola",
		'#' to "cancelletto",
		'$' to "dollaro",
		'%' to "percento",
		'&' to "e commerciale",
		'*' to "asterisco",
		'+' to "più",
		'=' to "uguale",
		'/' to "barra",
		'\\' to "barra inversa",
		'_' to "trattino basso",
		'<' to "minore di",
		'>' to "maggiore di",
		'(' to "parentesi aperta",
		')' to "parentesi chiusa",
		'"' to "virgolette",
		'-' to "trattino",
		'.' to "punto",
		',' to "virgola",
		':' to "due punti",
		';' to "punto e virgola",
		'?' to "punto interrogativo",
		'!' to "punto esclamativo"
	)

	private val POLISH_MAP: Map<Char, String> = ENGLISH_MAP + mapOf(
		'@' to "małpa",
		'#' to "krzyżyk",
		'$' to "dolar",
		'%' to "procent",
		'&' to "i",
		'*' to "gwiazdka",
		'+' to "plus",
		'=' to "równa się",
		'/' to "ukośnik",
		'\\' to "ukośnik odwrotny",
		'_' to "podkreślenie",
		'<' to "mniejsze niż",
		'>' to "większe niż",
		'(' to "otwórz nawias",
		')' to "zamknij nawias",
		'"' to "cudzysłów",
		'-' to "myślnik",
		'.' to "kropka",
		',' to "przecinek",
		':' to "dwukropek",
		';' to "średnik",
		'?' to "znak zapytania",
		'!' to "wykrzyknik"
	)
}
