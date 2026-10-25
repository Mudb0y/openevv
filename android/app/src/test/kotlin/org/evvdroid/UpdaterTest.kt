package org.evvdroid

import org.json.JSONArray
import org.json.JSONObject
import org.junit.Assert.assertEquals
import org.junit.Assert.assertFalse
import org.junit.Assert.assertNotNull
import org.junit.Assert.assertTrue
import org.junit.Test

class UpdaterTest {

	@Test
	fun testChannelFromId() {
		assertEquals(UpdateChannel.STABLE, UpdateChannel.fromId(0))
		assertEquals(UpdateChannel.DEVELOPMENT, UpdateChannel.fromId(1))
		assertEquals(UpdateChannel.STABLE, UpdateChannel.fromId(99))
	}

	@Test
	fun testReleaseEndpoint() {
		assertEquals("https://api.github.com/repos/trypsynth/evvdroid/releases/latest", Updater.releaseEndpoint(UpdateChannel.STABLE))
		assertEquals("https://api.github.com/repos/trypsynth/evvdroid/releases/tags/dev", Updater.releaseEndpoint(UpdateChannel.DEVELOPMENT))
	}

	@Test
	fun testVersionComparison() {
		assertTrue(Updater.isNewerVersion("0.1.0", "v0.2.0"))
		assertTrue(Updater.isNewerVersion("0.2.0", "0.2.1"))
		assertTrue(Updater.isNewerVersion("0.2.0", "v1.0.0"))
		assertFalse(Updater.isNewerVersion("0.2.0", "0.2.0"))
		assertFalse(Updater.isNewerVersion("0.2.0", "v0.1.0"))
		assertFalse(Updater.isNewerVersion("1.0.0", "0.9.9"))
	}

	@Test
	fun testFindCompatibleAsset() {
		val assets = listOf(
			ReleaseAsset("evvdroid-0.2.0-x86_64.apk", "https://example.com/x86_64.apk", 1000L),
			ReleaseAsset("evvdroid-0.2.0-universal.apk", "https://example.com/universal.apk", 2000L),
			ReleaseAsset("evvdroid-0.2.0-arm64-v8a.apk", "https://example.com/arm64.apk", 3000L)
		)

		// Test ARM64 ABI
		val arm64Asset = Updater.selectAsset(assets, arrayOf("arm64-v8a", "armeabi-v7a"))
		assertNotNull(arm64Asset)
		assertEquals("evvdroid-0.2.0-arm64-v8a.apk", arm64Asset!!.name)

		// Test x86_64 ABI
		val x86Asset = Updater.selectAsset(assets, arrayOf("x86_64"))
		assertNotNull(x86Asset)
		assertEquals("evvdroid-0.2.0-x86_64.apk", x86Asset!!.name)

		// 32-bit x86 must not take the x86_64 build
		val x86Only = listOf(
			ReleaseAsset("evvdroid-x86_64.apk", "https://example.com/x86_64.apk", 1L),
			ReleaseAsset("evvdroid-universal.apk", "https://example.com/universal.apk", 2L)
		)
		assertEquals("evvdroid-universal.apk", Updater.selectAsset(x86Only, arrayOf("x86"))!!.name)

		// Test unknown ABI fallback to universal
		val fallbackAsset = Updater.selectAsset(assets, arrayOf("mips"))
		assertNotNull(fallbackAsset)
		assertEquals("evvdroid-0.2.0-universal.apk", fallbackAsset!!.name)
	}

	@Test
	fun testIsUpdateAvailable() {
		val stableRelease = ReleaseInfo(
			tagName = "v0.3.0",
			name = "evvdroid 0.3.0",
			body = "Release notes",
			isPrerelease = false,
			publishedAt = "2026-10-25T00:00:00Z",
			asset = ReleaseAsset("evvdroid-0.3.0-arm64-v8a.apk", "http://example.com", 1234L)
		)

		assertTrue(Updater.isUpdateAvailable(stableRelease, UpdateChannel.STABLE, "0.2.0"))
		assertFalse(Updater.isUpdateAvailable(stableRelease, UpdateChannel.STABLE, "0.3.0"))

		val devRelease = ReleaseInfo(
			tagName = "dev",
			name = "Development build 28 (57f464f)",
			body = "build: 28\n\n- 57f464f Make gradlew executable",
			isPrerelease = true,
			publishedAt = "2026-10-25T00:00:00Z",
			asset = ReleaseAsset("evvdroid-arm64-v8a.apk", "http://example.com", 1234L)
		)

		assertTrue(Updater.isUpdateAvailable(devRelease, UpdateChannel.DEVELOPMENT, "0.2.0", 27))
		assertFalse(Updater.isUpdateAvailable(devRelease, UpdateChannel.DEVELOPMENT, "0.2.0", 28))
		assertFalse(Updater.isUpdateAvailable(devRelease, UpdateChannel.DEVELOPMENT, "0.2.0", 30))
		// A build with no history to count has no number, and is older than any release.
		assertTrue(Updater.isUpdateAvailable(devRelease, UpdateChannel.DEVELOPMENT, "0.2.0", 0))
	}

	@Test
	fun testParseReleaseNotes() {
		val markdown = """
			# New in this release
			- 57f464f Make gradlew executable
			* Improve punctuation handling (#9)
			Here is a general comment.
			1. First numbered feature
			2. Second numbered feature
		""".trimIndent()

		val items = Updater.parseReleaseNotes(markdown)
		assertEquals(6, items.size)
		assertTrue(items[0].isHeading)
		assertEquals("New in this release", items[0].text)

		assertTrue(items[1].isBullet)
		// Should strip commit hash
		assertEquals("Make gradlew executable", items[1].text)

		assertTrue(items[2].isBullet)
		assertEquals("Improve punctuation handling (#9)", items[2].text)

		assertFalse(items[3].isHeading)
		assertFalse(items[3].isBullet)
		assertEquals("Here is a general comment.", items[3].text)

		assertTrue(items[4].isBullet)
		assertEquals("First numbered feature", items[4].text)
	}
}
