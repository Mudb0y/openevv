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
 * A line is a key, a tab, and what to say instead. What to say is either a
 * respelling ("dee oe jeigh") or a pronunciation in the engine's own alphabet
 * ("`[Ekspoz1e]"), and the engine takes both. The bytes are the engine's own
 * code set already -- an e-acute is one byte -- so the file is read as Latin-1
 * rather than as UTF-8, which is what the files in the wild actually are.
 */
object Dictionaries {

	/** Teaches the engine every entry in [file]. Answers how many went in. */
	fun load(engine: EvvEngine, volume: Int, file: File): Int {
		var taught = 0
		var refused = 0
		try {
			file.forEachLine(Charsets.ISO_8859_1) { line ->
				val split = line.indexOf('\t')
				if (split > 0) {
					val key = line.substring(0, split).trim()
					val say = line.substring(split + 1).trim()
					if (key.isNotEmpty() && say.isNotEmpty()) {
						if (engine.teachWord(volume, key, say) == 0) taught++ else refused++
					}
				}
			}
		} catch (unreadable: Exception) {
			Log.e(TAG, "cannot read ${file.name}", unreadable)
			return taught
		}
		if (refused > 0) Log.e(TAG, "${file.name}: $refused of ${taught + refused} entries refused")
		return taught
	}

	/** How many entries a file offers, for saying so on the settings screen
	 *  without an engine to hand. */
	fun count(file: File): Int = try {
		file.useLines(Charsets.ISO_8859_1) { lines -> lines.count { it.indexOf('\t') > 0 } }
	} catch (unreadable: Exception) {
		0
	}

	private const val TAG = "evvdroid"
}
