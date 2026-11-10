import org.gradle.internal.os.OperatingSystem
import java.util.Properties

// Every language module the engine has. US English is named first because the
// engine gives that one to a caller who asks for no language in particular,
// and the settings screen's preview is such a caller.
val ALL_LANGUAGES = listOf(
	"enus", "engb", "eses", "esus", "frfr", "frca", "dede", "itit", "fifi", "ptbr", "plpl", "jajp"
)

plugins {
	alias(libs.plugins.android.application)
	alias(libs.plugins.kotlin.android)
	alias(libs.plugins.kotlin.compose)
}

val abis = (findProperty("openevv.abis") as String? ?: "arm64-v8a,armeabi-v7a,x86_64")
	.split(",").map { it.trim() }.filter { it.isNotEmpty() }
val rulesForm = findProperty("openevv.rules") as String? ?: "c"
val languages = findProperty("openevv.langs") as String?
	?: ALL_LANGUAGES.joinToString(" ") { "lang/$it" }
// The commit count, which is the version code, so that every build installs over every one before
// it, and the number the development workflow publishes as "build: <n>" in its pre-release's notes,
// so that the updater can tell whether the build on GitHub is newer than this one. A checkout
// without history gives 0, which counts as older than any published build.
val buildNumber = providers.exec {
	commandLine("git", "rev-list", "--count", "HEAD")
	isIgnoreExitValue = true
}.standardOutput.asText.map { it.trim().toIntOrNull() ?: 0 }.orElse(0).get()

// The release this build is, or is after: v0.7 for a build of the tag itself, and 0.7-12-g1a2b3c4
// for one twelve commits after it. The updater compares it with a release's tag.
val versionFromTag = providers.exec {
	commandLine("git", "describe", "--tags", "--match", "v[0-9]*")
	isIgnoreExitValue = true
}.standardOutput.asText.map { it.trim().removePrefix("v").ifEmpty { "0" } }.orElse("0").get()
val nativeOut = layout.buildDirectory.dir("native/jniLibs")

// Release signing, from a keystore.properties the repo does not carry. Without
// it a clone still builds and the release APK comes out unsigned.
val keystore = rootProject.file("keystore.properties").takeIf { it.exists() }?.let { where ->
	Properties().apply { where.inputStream().use { stream -> load(stream) } }
}

// The engine is built by the tree's own Makefile rather than by CMake, because
// that Makefile writes the language rules with Python before it compiles them
// and nothing here has a reason to reimplement that.
val buildNative by tasks.registering(Exec::class) {
	group = "build"
	description = "Cross-compiles the OpenEVV engine and the JNI bridge for each ABI."
	val script = rootProject.layout.projectDirectory.file("native/build-native.sh")
	// Never up to date as far as Gradle knows. What the libraries are made from
	// is the whole engine, and the Makefile already knows which of it changed
	// and rebuilds only that, which a list of inputs here could only ever
	// approximate.
	outputs.upToDateWhen { false }
	outputs.dir(nativeOut)
	val shell = if (OperatingSystem.current().isWindows) {
		val git = System.getenv("PROGRAMFILES")?.let { file("$it/Git/bin/bash.exe") }
		if (git != null && git.exists()) git.absolutePath else "bash"
	} else {
		"bash"
	}
	commandLine(shell, script.asFile.absolutePath)
	environment("ABIS", abis.joinToString(" "))
	environment("RULES", rulesForm)
	environment("LANGS", languages)
	environment("OUT", nativeOut.get().asFile.absolutePath)
	val ndkEnv = System.getenv("ANDROID_NDK_HOME") ?: System.getenv("ANDROID_NDK_ROOT") ?: ""
	if (ndkEnv.isNotEmpty()) {
		environment("ANDROID_NDK_HOME", ndkEnv)
	}
}

android {
	namespace = "com.mudb0y.openevv"
	compileSdk = 36
	ndkVersion = "27.0.12077973"

	defaultConfig {
		applicationId = "com.mudb0y.openevv"
		minSdk = 23
		targetSdk = 36
		versionCode = buildNumber.coerceAtLeast(1)
		versionName = versionFromTag
		buildConfigField("int", "BUILD_NUMBER", buildNumber.toString())
		testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
		ndk {
			abiFilters += abis
		}
	}

	sourceSets.getByName("androidTest") {
		kotlin.srcDirs("src/androidTest/kotlin")
	}

	sourceSets.getByName("test") {
		kotlin.srcDirs("src/test/kotlin")
	}

	testOptions {
		unitTests.isReturnDefaultValues = true
	}

	sourceSets.getByName("main") {
		kotlin.srcDirs("src/main/kotlin")
		jniLibs.srcDirs(nativeOut)
	}

	signingConfigs {
		keystore?.let { props ->
			create("release") {
				storeFile = file(props.getProperty("storeFile"))
				storePassword = props.getProperty("storePassword")
				keyAlias = props.getProperty("keyAlias")
				keyPassword = props.getProperty("keyPassword")
				// Off by default in AGP. It is what lets the key be rotated
				// later without every install having to be thrown away.
				enableV3Signing = true
			}
		}
	}

	buildTypes {
		release {
			signingConfig = signingConfigs.findByName("release")
			isMinifyEnabled = true
			isShrinkResources = true
			proguardFiles(getDefaultProguardFile("proguard-android-optimize.txt"), "proguard-rules.pro")
		}
		debug {
			isJniDebuggable = true
		}
	}

	packaging {
		jniLibs {
			// A screen reader cannot wait for a library to be unpacked, and an
			// uncompressed one is mapped straight out of the APK.
			useLegacyPackaging = false
		}
	}

	splits {
		abi {
			isEnable = (findProperty("openevv.abiSplits") as String?)?.toBoolean() ?: true
			reset()
			include(*abis.toTypedArray())
			isUniversalApk = true
		}
	}

	compileOptions {
		sourceCompatibility = JavaVersion.VERSION_17
		targetCompatibility = JavaVersion.VERSION_17
	}

	kotlinOptions {
		jvmTarget = "17"
	}

	buildFeatures {
		buildConfig = true
		compose = true
	}
}

tasks.named("preBuild") {
	dependsOn(buildNative)
}

dependencies {
	implementation(libs.androidx.core.ktx)
	implementation(libs.material)
	implementation(libs.androidx.activity.compose)
	implementation(platform(libs.compose.bom))
	implementation(libs.compose.ui)
	implementation(libs.compose.material3)

	testImplementation(libs.junit)
	androidTestImplementation(libs.junit)
	androidTestImplementation(libs.androidx.test.junit)
	androidTestImplementation(libs.androidx.test.runner)
	androidTestImplementation(libs.androidx.test.core)
}
