package org.evvdroid

import android.text.Spanned
import android.text.style.TtsSpan

/**
 * Punctuation pronunciation processing for Eloquence TTS matching NVDA's symbol dictionary.
 *
 * Supports 4 verbosity levels matching NVDA:
 * - LEVEL_NONE (0): Punctuation symbols are not spoken (they only provide standard pauses).
 * - LEVEL_SOME (1): Essential symbols are spoken (@, #, $, %, &, *, +, =, /, \, _, ^, <, >, ~, •).
 * - LEVEL_MOST (2): Level SOME plus brackets, braces, quotes, dashes, phrase punctuation (: ;),
 *                   bar (|) and grave (`).
 * - LEVEL_ALL  (3): Every punctuation mark is spoken, including sentence punctuation (. , ? !),
 *                   ticks (') and ellipses (...).
 *
 * Every mark is named in one pass over the text, so a name is never itself
 * read for marks: French names "?" "point d'interrogation", and a later pass
 * for "'" would name the apostrophe inside it.
 */
object Punctuation {

	const val LEVEL_NONE = 0
	const val LEVEL_SOME = 1
	const val LEVEL_MOST = 2
	const val LEVEL_ALL = 3

	/** A stretch of the text that whoever sent it has already put into words. */
	data class SpokenSpan(val start: Int, val end: Int, val text: String)

	/**
	 * Stands in front of a mark kept behind a name someone else has already
	 * said, so that [apply] lets it be and does not name it a second time. It
	 * never reaches the engine: [apply] takes it out at every level.
	 */
	const val NAMED = '\uE000'

	/**
	 * [charSequence] with its text spans replaced by what they say, or null if
	 * it has none. Only [TtsSpan.TYPE_TEXT] is a replacement; a verbatim span
	 * asks for its text to be spelled, and the others carry no text at all.
	 */
	fun expandTtsSpans(charSequence: CharSequence?): String? {
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

	/**
	 * The part of [expandTtsSpans] that needs nothing from Android. TalkBack
	 * names punctuation at its own "most" and "all" by putting a text span on
	 * each mark; the user's level still holds for every mark it left alone.
	 */
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
			val single = if (span.end - span.start == 1) text[span.start] else null
			sb.append(' ').append(span.text)
			// The mark stays behind its name so the engine still pauses on it.
			if (single != null && single in KEPT) sb.append(NAMED).append(single)
			sb.append(' ')
			cursor = span.end
		}
		sb.append(text, cursor, text.length)
		return collapseSpaces(sb.toString())
	}

	fun apply(text: String, level: Int, language: Int? = null): String {
		if (text.isEmpty()) return text
		if (level <= LEVEL_NONE) return text.unmarked()
		val langCode = language?.let { Eci.localeOf(it)?.first } ?: "eng"
		val map = getSymbolMap(langCode)

		val out = MARKS.getValue(level.coerceAtMost(LEVEL_ALL)).replace(text) {
			val mark = it.value
			val ellipsis = mark.length > 1 || mark == "…"
			val name = map[if (ellipsis) '…' else mark[0]] ?: return@replace mark
			// Phrase and sentence marks stay behind their names, so Eloquence
			// still pauses and inflects on them.
			val kept = when {
				ellipsis -> "."
				mark[0] in KEPT -> mark
				else -> ""
			}
			" $name$kept "
		}
		return collapseSpaces(out).unmarked()
	}

	private fun String.unmarked(): String =
		if (indexOf(NAMED) < 0) this else replace(NAMED.toString(), "")

	private fun collapseSpaces(text: String): String =
		SPACES.replace(text, " ")

	private val SPACES = Regex("""[ \t]+""")

	/** Marks that keep their place behind their names. */
	private const val KEPT = ".,:;?!"

	private val SOME_SYMBOLS = charArrayOf(
		'@', '#', '$', '%', '&', '*', '+', '=', '/', '\\', '_', '^', '<', '>', '~', '•'
	)

	/** With the colon, which is not one inside a time. */
	private val MOST_SYMBOLS = charArrayOf(
		'"', '(', ')', '[', ']', '{', '}', '|', '`',
		'-', '—', '–', '“', '”', '‘', '’', '·', ';'
	)

	/** With the comma, which is not one grouping a number, and the dots. */
	private val ALL_SYMBOLS = charArrayOf(
		'?', '!', '\'', '¿', '¡'
	)

	/**
	 * What each level names, as one pattern so that a mark is named once and a
	 * name is never read for marks. A dot that is a decimal point and a comma
	 * grouping a number are not punctuation; neither is a colon inside a time.
	 * An ellipsis, or any longer run of dots, is one mark.
	 */
	private val MARKS: Map<Int, Regex> = (LEVEL_SOME..LEVEL_ALL).associateWith { level ->
		val singles = StringBuilder().append(SOME_SYMBOLS)
		val contextual = mutableListOf<String>()
		if (level >= LEVEL_MOST) {
			singles.append(MOST_SYMBOLS)
			contextual += """:(?!\d)"""
		}
		if (level >= LEVEL_ALL) {
			singles.append(ALL_SYMBOLS)
			contextual += listOf("""\.{3,}|…|(?<!\d)\.(?!\d)""", """,(?!\d)""")
		}
		val set = singles.map { "\\u%04x".format(it.code) }.joinToString("", "[", "]")
		Regex("""(?<!\uE000)(?:""" + (contextual + set).joinToString("|") + ")")
	}

	private fun getSymbolMap(langCode: String): Map<Char, String> = when (langCode) {
		"spa" -> SPANISH_MAP
		"fra" -> FRENCH_MAP
		"deu" -> GERMAN_MAP
		"ita" -> ITALIAN_MAP
		"pol" -> POLISH_MAP
		else -> ENGLISH_MAP
	}

	/** NVDA English symbol dictionary. */
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
		'<' to "less",
		'>' to "greater",
		'•' to "bullet",
		'(' to "left paren",
		')' to "right paren",
		'[' to "left bracket",
		']' to "right bracket",
		'{' to "left brace",
		'}' to "right brace",
		'"' to "quote",
		'“' to "left quote",
		'”' to "right quote",
		'‘' to "left tick",
		'’' to "right tick",
		'-' to "dash",
		'—' to "em dash",
		'–' to "en dash",
		':' to "colon",
		';' to "semi",
		'_' to "underscore",
		'^' to "caret",
		'~' to "tilda",
		'\\' to "backslash",
		'|' to "bar",
		'`' to "graav",
		'·' to "middle dot",
		'.' to "dot",
		',' to "comma",
		'?' to "question",
		'!' to "bang",
		'\'' to "tick",
		'…' to "dot dot dot"
	)

	/** NVDA Spanish symbol dictionary from locale/es/symbols.dic */
	private val SPANISH_MAP: Map<Char, String> = ENGLISH_MAP + mapOf(
		'@' to "arroba",
		'#' to "signo de número",
		'$' to "dólares",
		'%' to "porciento",
		'&' to "et",
		'*' to "asterisco",
		'+' to "más",
		'=' to "igual",
		'/' to "barra",
		'<' to "menor que",
		'>' to "mayor que",
		'•' to "viñeta",
		'(' to "abrir paréntesis",
		')' to "cerrar paréntesis",
		'[' to "abrir corchete",
		']' to "cerrar corchete",
		'{' to "abrir llave",
		'}' to "cerrar llave",
		'"' to "comillas",
		'“' to "abrir comillas",
		'”' to "cerrar comillas",
		'‘' to "abrir apóstrofo",
		'’' to "cerrar apóstrofo",
		'-' to "guion",
		'—' to "raya",
		'–' to "guion corto",
		':' to "dos puntos",
		';' to "punto y coma",
		'_' to "subrayado",
		'^' to "acento circunflejo",
		'~' to "tilde",
		'\\' to "barra inversa",
		'|' to "barra vertical",
		'`' to "acento grave",
		'·' to "punto centrado",
		'.' to "punto",
		',' to "coma",
		'?' to "cerrar interrogación",
		'!' to "cerrar exclamación",
		'¿' to "abrir interrogación",
		'¡' to "abrir exclamación",
		'\'' to "apóstrofo",
		'…' to "elipsis"
	)

	/** NVDA French symbol dictionary from locale/fr/symbols.dic */
	private val FRENCH_MAP: Map<Char, String> = ENGLISH_MAP + mapOf(
		'@' to "arobase",
		'#' to "dièse",
		'$' to "dollar",
		'%' to "pour cent",
		'&' to "et commercial",
		'*' to "astérisque",
		'+' to "pluss",
		'=' to "égal",
		'/' to "barre oblique",
		'<' to "inférieur",
		'>' to "supérieur",
		'•' to "puce",
		'(' to "parenthèse gauche",
		')' to "parenthèse droite",
		'[' to "crochet gauche",
		']' to "crochet droit",
		'{' to "accolade gauche",
		'}' to "accolade droite",
		'"' to "guillemet",
		'“' to "double guillemet ouvrant",
		'”' to "double guillemet fermant",
		'‘' to "apostrophe gauche",
		'’' to "apostrophe droite",
		'-' to "tiret",
		'—' to "tiret cadratin",
		'–' to "tiret demicadratin",
		':' to "deux points",
		';' to "point virgule",
		'_' to "souligné",
		'^' to "accent circonflexe",
		'~' to "tilde",
		'\\' to "barre oblique inversée",
		'|' to "barre verticale",
		'`' to "accent grave",
		'·' to "point médian",
		'.' to "point",
		',' to "virgule",
		'?' to "point d'interrogation",
		'!' to "point d'exclamation",
		'\'' to "apostrophe",
		'…' to "points de suspension"
	)

	/** NVDA German symbol dictionary from locale/de/symbols.dic */
	private val GERMAN_MAP: Map<Char, String> = ENGLISH_MAP + mapOf(
		'@' to "ät",
		'#' to "Nummernzeichen",
		'$' to "Dollar",
		'%' to "Prozent",
		'&' to "und",
		'*' to "Stern",
		'+' to "plus",
		'=' to "gleich",
		'/' to "Schrägstrich",
		'<' to "kleiner als",
		'>' to "größer als",
		'•' to "Aufzählungszeichen",
		'(' to "Runde Klammer auf",
		')' to "Runde Klammer zu",
		'[' to "Eckige Klammer auf",
		']' to "Eckige Klammer zu",
		'{' to "Geschweifte Klammer auf",
		'}' to "Geschweifte Klammer zu",
		'"' to "Anführungszeichen",
		'“' to "Anführungszeichen links",
		'”' to "Anführungszeichen rechts",
		'‘' to "Apostroph links",
		'’' to "Apostroph rechts",
		'-' to "Strich",
		'—' to "Gedankenstrich",
		'–' to "Bindestrich",
		':' to "Doppelpunkt",
		';' to "Semikolon",
		'_' to "Unterstrich",
		'^' to "Dach",
		'~' to "Tilde",
		'\\' to "Bäcksläsch",
		'|' to "Senkrechter Strich",
		'`' to "Gravis",
		'·' to "Mittelpunkt",
		'.' to "Punkt",
		',' to "Komma",
		'?' to "Fragezeichen",
		'!' to "Ausrufezeichen",
		'\'' to "Apostroph",
		'…' to "Punkt Punkt Punkt"
	)

	/** NVDA Italian symbol dictionary from locale/it/symbols.dic */
	private val ITALIAN_MAP: Map<Char, String> = ENGLISH_MAP + mapOf(
		'@' to "chiocciola",
		'#' to "cancelletto",
		'$' to "dollari",
		'%' to "percento",
		'&' to "and",
		'*' to "asterisco",
		'+' to "più",
		'=' to "uguale",
		'/' to "barra",
		'<' to "minore di",
		'>' to "maggiore di",
		'•' to "puntato",
		'(' to "aperta parentesi",
		')' to "chiusa parentesi",
		'[' to "aperta quadra",
		']' to "chiusa quadra",
		'{' to "aperta graffa",
		'}' to "chiusa graffa",
		'"' to "virgolette",
		'“' to "aperte virgolette",
		'”' to "chiuse virgolette",
		'‘' to "aperto apostrofo",
		'’' to "chiuso apostrofo",
		'-' to "trattino",
		'—' to "trattino lungo",
		'–' to "trattino corto",
		':' to "due punti",
		';' to "punto e virgola",
		'_' to "sottolineato",
		'^' to "accento circonflesso",
		'~' to "tilde",
		'\\' to "controbarra",
		'|' to "barra verticale",
		'`' to "accento grave",
		'·' to "punto medio",
		'.' to "punto",
		',' to "virgola",
		'?' to "punto di domanda",
		'!' to "punto esclamativo",
		'\'' to "apostrofo",
		'…' to "punto punto punto"
	)

	/** NVDA Polish symbol dictionary from locale/pl/symbols.dic */
	private val POLISH_MAP: Map<Char, String> = ENGLISH_MAP + mapOf(
		'@' to "małpa",
		'#' to "hasz",
		'$' to "dolar",
		'%' to "procent",
		'&' to "and",
		'*' to "gwiazdka",
		'+' to "plus",
		'=' to "równe",
		'/' to "slesz",
		'<' to "mniejsze",
		'>' to "większe",
		'•' to "punktor",
		'(' to "lewy nawias",
		')' to "prawy nawias",
		'[' to "lewy kwadratowy",
		']' to "prawy kwadratowy",
		'{' to "lewy klamrowy",
		'}' to "prawy klamrowy",
		'"' to "cudzysłów",
		'“' to "lewy cudzysłów",
		'”' to "prawy cudzysłów",
		'‘' to "lewy apostrof",
		'’' to "prawy apostrof",
		'-' to "minus",
		'—' to "myślnik",
		'–' to "półpauza",
		':' to "dwukropek",
		';' to "średnik",
		'_' to "podkreślacz",
		'^' to "daszek",
		'~' to "tylda",
		'\\' to "bekslesz",
		'|' to "kreska pionowa",
		'`' to "akcent",
		'·' to "kropka środkowa",
		'.' to "kropka",
		',' to "przecinek",
		'?' to "pytajnik",
		'!' to "wykrzyknik",
		'\'' to "apostrof",
		'…' to "wielokropek"
	)
}
