package org.evvdroid

import android.util.Log
import java.io.File

/**
 * The pronunciation dictionaries people write for this engine.
 *
 * The engine has a loader of its own, and it does not take these: eciLoadDict
 * answers eciDictAccessError for every one of them, because what it reads is
 * IBM's own saved form and what people share is text. So the text is read here
 * and the engine is taught a word at a time, which is the same interface the
 * loader would have used and does not depend on agreeing about a file format.
 *
 * A line is a key, a tab, and what to say instead, with an optional second tab
 * for case sensitivity:
 * key \t say [\t 1]
 * What to say is either a respelling ("dee oe jeigh") or a pronunciation in the
 * engine's own alphabet ("`[Ekspoz1e]"), and the engine takes both. The bytes
 * are the engine's own code set already -- an e-acute is one byte -- so the file
 * is read as Latin-1 rather than as UTF-8, which is what the files in the wild
 * actually are.
 */
data class DictEntry(
	val key: String,
	val say: String,
	val caseSensitive: Boolean = false,
	val suspended: Boolean = false
)

object Dictionaries {

	/** Teaches the engine every entry in [file]. Answers how many went in. */
	fun load(engine: EvvEngine, volume: Int, file: File): Int {
		return loadWithCaseSensitive(engine, volume, file).first
	}

	/**
	 * Single-pass dictionary loader:
	 * Teaches case-insensitive words to the native [engine] and extracts
	 * cached case-sensitive regex entries for pre-processing.
	 */
	fun loadWithCaseSensitive(
		engine: EvvEngine,
		volume: Int,
		file: File
	): Pair<Int, List<CachedCaseSensitiveEntry>> {
		val csList = mutableListOf<CachedCaseSensitiveEntry>()
		if (!file.exists() || file.length() == 0L) {
			return Pair(0, csList)
		}
		if (volume == Eci.DICT_EMOJI || file.name.contains("emoji", ignoreCase = true)) {
			Emoji.loadUserEmoji(file)
			return Pair(count(file), csList)
		}
		var taught = 0
		var refused = 0
		try {
			file.forEachLine(charsetFor(file)) { rawLine ->
				val line = rawLine.trim()
				if (line.isEmpty() || line.startsWith("#") || line.startsWith("//")) return@forEachLine
				val split = line.indexOf('\t')
				if (split > 0) {
					val key = line.substring(0, split).trim()
					val rest = line.substring(split + 1).trim()
					val tab2 = rest.indexOf('\t')
					val say = (if (tab2 >= 0) rest.substring(0, tab2) else rest).trim()
					val flag = if (tab2 >= 0) rest.substring(tab2 + 1).trim() else ""
					val isCaseSensitive = flag == "1" || flag.equals("true", ignoreCase = true) || flag.equals("cs", ignoreCase = true)
					if (key.isNotEmpty() && say.isNotEmpty()) {
						if (!isCaseSensitive) {
							if (engine.teachWord(volume, key, say) == 0) taught++ else refused++
						} else {
							val cached = createCachedCaseSensitiveEntry(DictEntry(key, say, true, false))
							if (cached != null) {
								csList.add(cached)
							}
							taught++
						}
					}
				}
			}
		} catch (unreadable: Exception) {
			Log.e(TAG, "cannot read ${file.name}", unreadable)
			return Pair(taught, csList)
		}
		if (refused > 0) Log.e(TAG, "${file.name}: $refused of ${taught + refused} entries refused")
		return Pair(taught, csList)
	}

	data class CachedCaseSensitiveEntry(val key: String, val regex: Regex, val replacement: String)

	fun createCachedCaseSensitiveEntry(entry: DictEntry): CachedCaseSensitiveEntry? {
		if (entry.key.isEmpty() || entry.say.isEmpty() || !entry.caseSensitive || entry.suspended) return null
		val prefix = if (entry.key.first().isLetterOrDigit() || entry.key.first() == '_') "(?<!\\w)" else ""
		val suffix = if (entry.key.last().isLetterOrDigit() || entry.key.last() == '_') "(?!\\w)" else ""
		val regex = Regex("$prefix${Regex.escape(entry.key)}$suffix")
		return CachedCaseSensitiveEntry(entry.key, regex, entry.say)
	}

	fun applyCaseSensitiveDict(text: String, entries: List<CachedCaseSensitiveEntry>): String {
		if (text.isEmpty() || entries.isEmpty()) return text
		var result = text
		for (entry in entries) {
			if (result.contains(entry.key)) {
				result = entry.regex.replace(result) { entry.replacement }
			}
		}
		return result
	}

	/** Reads all dictionary entries from [file], preserving case sensitivity and suspended status. */
	fun readDictEntries(file: File): List<DictEntry> {
		if (!file.exists()) return emptyList()
		val entries = mutableListOf<DictEntry>()
		try {
			file.forEachLine(charsetFor(file)) { rawLine ->
				val trimmed = rawLine.trim()
				if (trimmed.isEmpty() || trimmed.startsWith("//")) return@forEachLine
				val isSuspended = trimmed.startsWith("#")
				val content = if (isSuspended) trimmed.substring(1).trim() else trimmed
				val parts = content.split('\t')
				if (parts.size >= 2) {
					val key = parts[0].trim()
					val say = parts[1].trim()
					val cs = if (parts.size >= 3) {
						val flag = parts[2].trim()
						flag == "1" || flag.equals("true", ignoreCase = true) || flag.equals("cs", ignoreCase = true)
					} else {
						false
					}
					if (key.isNotEmpty()) {
						entries.add(DictEntry(key, say, cs, isSuspended))
					}
				}
			}
		} catch (unreadable: Exception) {
			Log.e(TAG, "cannot read entries from ${file.name}", unreadable)
		}
		return entries
	}

	/** Writes list of dictionary entries to [file] encoded as UTF-8 for emoji or ISO-8859-1 for traditional dictionaries. */
	fun writeDictEntries(file: File, entries: List<DictEntry>) {
		try {
			file.parentFile?.mkdirs()
			file.bufferedWriter(charsetFor(file)).use { writer ->
				for (entry in entries) {
					val prefix = if (entry.suspended) "#" else ""
					if (entry.caseSensitive) {
						writer.write("$prefix${entry.key}\t${entry.say}\t1\r\n")
					} else {
						writer.write("$prefix${entry.key}\t${entry.say}\r\n")
					}
				}
			}
		} catch (unwritable: Exception) {
			Log.e(TAG, "cannot write entries to ${file.name}", unwritable)
		}
	}

	/** Adds or updates an entry in [file], returning the updated list of DictEntry. */
	fun addOrUpdateDictEntry(
		file: File,
		key: String,
		say: String,
		caseSensitive: Boolean = false,
		suspended: Boolean = false
	): List<DictEntry> {
		val list = readDictEntries(file).toMutableList()
		val index = list.indexOfFirst {
			if (caseSensitive || it.caseSensitive) {
				it.key == key
			} else {
				it.key.equals(key, ignoreCase = true)
			}
		}
		val newEntry = DictEntry(key, say, caseSensitive, suspended)
		if (index >= 0) {
			list[index] = newEntry
		} else {
			list.add(0, newEntry) // Add at top for easy visibility
		}
		writeDictEntries(file, list)
		return list
	}

	/** Toggles the suspended state of an entry in [file]. */
	fun toggleSuspendDictEntry(file: File, key: String, caseSensitive: Boolean? = null): List<DictEntry> {
		val list = readDictEntries(file).toMutableList()
		val index = list.indexOfFirst {
			if (caseSensitive != null) {
				it.key == key && it.caseSensitive == caseSensitive
			} else if (it.caseSensitive) {
				it.key == key
			} else {
				it.key.equals(key, ignoreCase = true)
			}
		}
		if (index >= 0) {
			val current = list[index]
			list[index] = current.copy(suspended = !current.suspended)
			writeDictEntries(file, list)
		}
		return list
	}

	/** Deletes an entry matching [key] (and optionally [caseSensitive]) from [file]. */
	fun deleteDictEntry(file: File, key: String, caseSensitive: Boolean? = null): List<DictEntry> {
		val list = readDictEntries(file).filterNot {
			if (caseSensitive != null) {
				it.key == key && it.caseSensitive == caseSensitive
			} else if (it.caseSensitive) {
				it.key == key
			} else {
				it.key.equals(key, ignoreCase = true)
			}
		}
		writeDictEntries(file, list)
		return list
	}

	/** How many active entries a file offers, ignoring suspended/commented lines. */
	fun count(file: File): Int = try {
		if (!file.exists()) 0
		else file.useLines(charsetFor(file)) { lines ->
			lines.count {
				val t = it.trim()
				t.isNotEmpty() && !t.startsWith("#") && !t.startsWith("//") && t.contains('\t')
			}
		}
	} catch (unreadable: Exception) {
		0
	}

	private fun charsetFor(file: File): java.nio.charset.Charset =
		if (file.name.contains("emoji", ignoreCase = true)) Charsets.UTF_8 else Charsets.ISO_8859_1

	private const val TAG = "evvdroid"
}
