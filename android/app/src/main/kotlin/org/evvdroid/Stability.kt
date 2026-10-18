package org.evvdroid

import android.annotation.SuppressLint
import android.app.ActivityManager
import android.app.ActivityManager.RunningAppProcessInfo
import android.app.ApplicationExitInfo
import android.content.ActivityNotFoundException
import android.content.ComponentName
import android.content.Context
import android.content.Intent
import android.net.Uri
import android.os.Build
import android.os.PowerManager
import android.provider.Settings as SystemSettings
import androidx.annotation.ChecksSdkIntAtLeast
import androidx.core.content.IntentCompat
import androidx.core.net.toUri

/**
 * What a phone may do to an engine it is not looking at, and the screens that
 * stop it.
 *
 * A screen reader binds the engine and keeps it, but a phone saving battery
 * does not ask what a process is for. It can freeze the engine between
 * utterances, kill it mid-sentence, or take its permissions away after a few
 * weeks of the settings screen going unopened, and every one of those sounds
 * like the engine crashing. None of it can be turned off from here: what an app
 * can do is open the right screen and say how things stand.
 */
object Stability {

	/** Whether Android has been told to leave the engine's process alone. */
	fun batteryUnrestricted(context: Context): Boolean {
		val power = context.getSystemService(Context.POWER_SERVICE) as PowerManager
		return power.isIgnoringBatteryOptimizations(context.packageName)
	}

	/** Whether the phone has app hibernation at all. It arrived in Android 11
	 *  as taking permissions away from unused apps, and grew into pausing them
	 *  outright in 12. */
	@get:ChecksSdkIntAtLeast(api = Build.VERSION_CODES.R)
	val hasHibernation: Boolean
		get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R

	/** Whether the engine has been excused from hibernation. Always true on a
	 *  phone without it, since there is nothing to be excused from. */
	fun hibernationExempt(context: Context): Boolean =
		!hasHibernation || context.packageManager.isAutoRevokeWhitelisted

	/** Asks for the engine to be let off battery optimisation, with Android's
	 *  own yes-or-no question. Already let off, there is nothing to ask, so it
	 *  opens the list where that can be undone. */
	@SuppressLint("BatteryLife")
	fun openBattery(context: Context) {
		val intent = if (batteryUnrestricted(context)) {
			Intent(SystemSettings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS)
		} else {
			Intent(SystemSettings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, packageUri(context))
		}
		if (!start(context, intent)) openAppInfo(context)
	}

	/** The switch that lets an unused app be paused, or have its permissions
	 *  taken, which is a different screen on each version that has one. */
	fun openHibernation(context: Context) {
		val intent = runCatching {
			IntentCompat.createManageUnusedAppRestrictionsIntent(context, context.packageName)
		}.getOrNull()
		if (intent == null || !start(context, intent)) openAppInfo(context)
	}

	/** Each maker's own screen for letting an app start by itself and run in
	 *  the background, which stock Android has no notion of. Answers whether
	 *  one opened; when none did, the app's own page is opened instead, which
	 *  is where most of these phones keep the same switch under another name. */
	fun openAutoStart(context: Context): Boolean {
		for (screen in AUTO_START) {
			if (start(context, Intent().setComponent(screen))) return true
		}
		openAppInfo(context)
		return false
	}

	/** How often each kind of ending has happened, over as much history as
	 *  Android keeps, which is a few dozen exits at most. [since] is when the
	 *  oldest of them was, or null when there are none. */
	data class Exits(
		val crashed: Int = 0,
		val unresponsive: Int = 0,
		val killedInUse: Int = 0,
		val idle: Int = 0,
		val byUser: Int = 0,
		val since: Long? = null
	)

	/** Whether Android keeps the record [exits] reads. It began in 11. */
	@get:ChecksSdkIntAtLeast(api = Build.VERSION_CODES.R)
	val hasExitHistory: Boolean
		get() = Build.VERSION.SDK_INT >= Build.VERSION_CODES.R

	/**
	 * Why the engine's process ended, each time Android remembers.
	 *
	 * What matters is how much the engine was wanted at the time. A process
	 * killed while it was perceptible or better was being used by the screen
	 * reader, which is where speech cuts out mid-sentence; that is the phone's
	 * own cleaner when it comes as a signal, since Android itself never kills
	 * something in use for memory before everything idle has gone. A process
	 * that was cached was doing nothing, and taking it back is ordinary: the
	 * screen reader starts it again the next time it speaks. Ends that say
	 * nothing about stability -- an update, a permission changing, another
	 * user on the phone being stopped, the catch-all "other" that Android says
	 * apps cannot act on -- are left out.
	 *
	 * Before 14, Android gave updating the app and changing its components the
	 * same reason as Force stop, so only from 14 on is that reason counted as
	 * the user's doing.
	 */
	fun exits(context: Context): Exits {
		if (!hasExitHistory) return Exits()
		val manager = context.getSystemService(Context.ACTIVITY_SERVICE) as ActivityManager
		val all = runCatching {
			manager.getHistoricalProcessExitReasons(context.packageName, 0, 0)
		}.getOrDefault(emptyList())
		val forceStopIsDistinct = Build.VERSION.SDK_INT >= Build.VERSION_CODES.UPSIDE_DOWN_CAKE

		var crashed = 0
		var unresponsive = 0
		var killedInUse = 0
		var idle = 0
		var byUser = 0
		var since: Long? = null
		for (exit in all) {
			val inUse = exit.importance <= RunningAppProcessInfo.IMPORTANCE_PERCEPTIBLE
			val counted = when (exit.reason) {
				ApplicationExitInfo.REASON_CRASH,
				ApplicationExitInfo.REASON_CRASH_NATIVE,
				ApplicationExitInfo.REASON_INITIALIZATION_FAILURE -> { crashed++; true }
				ApplicationExitInfo.REASON_ANR -> { unresponsive++; true }
				ApplicationExitInfo.REASON_USER_REQUESTED -> {
					if (forceStopIsDistinct) byUser++
					forceStopIsDistinct
				}
				ApplicationExitInfo.REASON_SIGNALED,
				ApplicationExitInfo.REASON_LOW_MEMORY,
				ApplicationExitInfo.REASON_EXCESSIVE_RESOURCE_USAGE,
				ApplicationExitInfo.REASON_FREEZER -> {
					if (inUse) killedInUse++ else idle++
					true
				}
				else -> false
			}
			if (counted) since = minOf(since ?: exit.timestamp, exit.timestamp)
		}
		return Exits(crashed, unresponsive, killedInUse, idle, byUser, since)
	}

	fun openAppInfo(context: Context) {
		start(context, Intent(SystemSettings.ACTION_APPLICATION_DETAILS_SETTINGS, packageUri(context)))
	}

	private fun packageUri(context: Context): Uri = "package:${context.packageName}".toUri()

	/** A screen that is not there is not found, and one that is there but not
	 *  for us refuses; either way the next one is tried. */
	private fun start(context: Context, intent: Intent): Boolean = try {
		context.startActivity(intent)
		true
	} catch (_: ActivityNotFoundException) {
		false
	} catch (_: SecurityException) {
		false
	}

	/** Most specific first, since a maker may carry an older screen beside the
	 *  one that does the work. */
	private val AUTO_START = listOf(
		// Xiaomi, Redmi, POCO
		ComponentName("com.miui.securitycenter", "com.miui.permcenter.autostart.AutoStartManagementActivity"),
		// Huawei
		ComponentName("com.huawei.systemmanager", "com.huawei.systemmanager.startupmgr.ui.StartupNormalAppListActivity"),
		ComponentName("com.huawei.systemmanager", "com.huawei.systemmanager.optimize.process.ProtectActivity"),
		// Honor
		ComponentName("com.hihonor.systemmanager", "com.hihonor.systemmanager.startupmgr.ui.StartupNormalAppListActivity"),
		// Oppo, Realme
		ComponentName("com.coloros.safecenter", "com.coloros.safecenter.permission.startup.StartupAppListActivity"),
		ComponentName("com.coloros.safecenter", "com.coloros.safecenter.startupapp.StartupAppListActivity"),
		ComponentName("com.oppo.safe", "com.oppo.safe.permission.startup.StartupAppListActivity"),
		// Vivo, iQOO
		ComponentName("com.vivo.permissionmanager", "com.vivo.permissionmanager.activity.BgStartUpManagerActivity"),
		ComponentName("com.iqoo.secure", "com.iqoo.secure.ui.phoneoptimize.AddWhiteListActivity"),
		// OnePlus
		ComponentName("com.oneplus.security", "com.oneplus.security.chainlaunch.view.ChainLaunchAppListActivity"),
		// Samsung: apps that are never put to sleep
		ComponentName("com.samsung.android.lool", "com.samsung.android.sm.battery.ui.BatteryActivity"),
		// Asus
		ComponentName("com.asus.mobilemanager", "com.asus.mobilemanager.autostart.AutoStartActivity"),
		// Letv
		ComponentName("com.letv.android.letvsafe", "com.letv.android.letvsafe.AutobootManageActivity")
	)
}
