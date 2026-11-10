package org.evvdroid

import android.content.Context
import android.os.Build
import android.os.UserManager
import android.util.Log
import java.io.File

/**
 * Where the engine keeps what it needs before the phone has been unlocked.
 *
 * A screen reader is the first thing wanted on a locked phone and the last
 * thing that can wait for a PIN, so the speech service is marked direct-boot
 * aware and starts while the user's own storage is still encrypted. Ordinary
 * app storage is not there yet at that point: reading it throws, and settings
 * read through it would come back as the defaults, so the voice would be the
 * wrong one until somebody unlocked the phone and restarted the engine.
 *
 * Device-protected storage is readable from the moment the phone boots, and is
 * what everything the engine reads at startup lives in. It is a different
 * directory rather than a different kind of file, so what is stored there is
 * moved once, the first time a build with this in it runs after an unlock.
 */
object DirectBoot {

	/** The context to read settings and the files they name through. */
	fun storage(context: Context): Context {
		val app = context.applicationContext
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) return app
		val device = app.createDeviceProtectedStorageContext() ?: return app
		// Both of these are no-ops once there is nothing left behind to bring
		// over, which is every run after the first.
		device.moveSharedPreferencesFrom(app, prefsName(app))
		bringDictionariesForward(app, device)
		return device
	}

	/** The dictionaries directory, wherever it is being kept. */
	fun dictionaries(context: Context): File =
		File(storage(context).filesDir, DICTIONARIES)

	private fun prefsName(app: Context) = "${app.packageName}_preferences"

	/**
	 * Moves dictionaries picked by an earlier build into device-protected
	 * storage and points the settings at where they went.
	 *
	 * Nothing can be moved while the phone is locked -- the directory being
	 * moved from is the one that is not readable yet -- so a locked start
	 * leaves this for the next unlocked one. Until then the service finds the
	 * files missing and speaks without them, which is what it already does for
	 * a dictionary that has been deleted.
	 */
	private fun bringDictionariesForward(app: Context, device: Context) {
		if (!isUnlocked(app)) return
		val from = File(app.filesDir, DICTIONARIES)
		if (!from.isDirectory) return
		val into = File(device.filesDir, DICTIONARIES)
		val moved = mutableMapOf<String, String>()
		for (file in from.listFiles().orEmpty()) {
			if (!file.isFile) continue
			if (!into.isDirectory && !into.mkdirs()) return
			val there = File(into, file.name)
			try {
				// The two are on different mounts, so a rename can refuse and
				// the copy is not a fallback for a rare case but the usual way.
				if (!file.renameTo(there)) {
					file.copyTo(there, overwrite = true)
					file.delete()
				}
			} catch (stuck: Exception) {
				// Left where it is, and left in the settings as where it is,
				// so the next unlocked start tries again.
				Log.e(TAG, "cannot move ${file.name} to device-protected storage", stuck)
				continue
			}
			moved[file.absolutePath] = there.absolutePath
		}
		from.delete()
		if (moved.isEmpty()) return
		val prefs = device.getSharedPreferences(prefsName(app), Context.MODE_PRIVATE)
		val edit = prefs.edit()
		for (volume in Eci.DICT_VOLUMES) {
			val key = Settings.dictionaryPathKey(volume)
			val was = prefs.getString(key, null) ?: continue
			moved[was]?.let { edit.putString(key, it) }
		}
		edit.apply()
	}

	private fun isUnlocked(app: Context): Boolean {
		if (Build.VERSION.SDK_INT < Build.VERSION_CODES.N) return true
		val users = app.getSystemService(UserManager::class.java) ?: return true
		return users.isUserUnlocked
	}

	private const val DICTIONARIES = "dictionaries"

	private const val TAG = "evvdroid"
}
