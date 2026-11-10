package org.evvdroid

import android.content.Context
import androidx.compose.runtime.getValue
import androidx.compose.runtime.mutableStateOf
import androidx.compose.runtime.setValue
import kotlinx.coroutines.CancellationException
import kotlinx.coroutines.CoroutineScope
import kotlinx.coroutines.Dispatchers
import kotlinx.coroutines.Job
import kotlinx.coroutines.SupervisorJob
import kotlinx.coroutines.launch

/**
 * The update check and download, kept for the life of the process rather than of the settings
 * screen, so that leaving the screen does not cancel a download and coming back finds it where
 * it was. There is at most one check or download at a time.
 */
object UpdateController {

	private val scope = CoroutineScope(Dispatchers.Main + SupervisorJob())
	private var job: Job? = null

	/** Set when the install was held back for the "install unknown apps" permission, so that
	 *  coming back from that screen can carry on instead of leaving the user to tap again. */
	private var installPending = false

	var status: UpdateStatus by mutableStateOf(UpdateStatus.Idle)
		private set

	var isDialogOpen by mutableStateOf(false)

	/** What was in flight belongs to the channel that has been left, and its answer must not land. */
	fun reset() {
		job?.cancel()
		job = null
		installPending = false
		status = UpdateStatus.Idle
	}

	fun check(channel: UpdateChannel, currentVersionName: String) {
		if (status is UpdateStatus.Checking || status is UpdateStatus.Downloading) return
		status = UpdateStatus.Checking
		job = scope.launch {
			Updater.fetchRelease(channel)
				.onSuccess { rel ->
					if (Updater.isUpdateAvailable(rel, channel, currentVersionName)) {
						status = UpdateStatus.UpdateAvailable(rel, isNewer = true)
						isDialogOpen = true
					} else {
						status = UpdateStatus.UpToDate(rel)
					}
				}
				.onFailure { err ->
					if (err is CancellationException) return@onFailure
					status = UpdateStatus.Error(err.message ?: "Check failed")
				}
		}
	}

	fun downloadAndInstall(context: Context, release: ReleaseInfo) {
		// Two downloads would write the same file, and each deletes the other's when it starts.
		if (status is UpdateStatus.Downloading) return
		val app = context.applicationContext
		status = UpdateStatus.Downloading(release, 0, 0, release.asset.size)
		isDialogOpen = true
		job = scope.launch {
			Updater.downloadApk(release, app) { percent, downloaded, total ->
				status = UpdateStatus.Downloading(release, percent, downloaded, total)
			}.onSuccess { file ->
				status = UpdateStatus.ReadyToInstall(release, file)
				install(app, file)
			}.onFailure { err ->
				if (err is CancellationException) return@onFailure
				status = UpdateStatus.Error(err.message ?: "Download failed")
			}
		}
	}

	fun install(context: Context, file: java.io.File) {
		val app = context.applicationContext
		if (Updater.canInstallPackages(app)) {
			installPending = false
			Updater.installApk(app, file)
		} else {
			installPending = true
			Updater.requestInstallPermission(app)
		}
	}

	/** Called when the settings screen returns, possibly from the permission screen. */
	fun resumeInstall(context: Context) {
		val ready = status as? UpdateStatus.ReadyToInstall ?: return
		if (installPending && Updater.canInstallPackages(context)) install(context, ready.apkFile)
	}
}
