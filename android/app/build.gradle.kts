import org.gradle.internal.os.OperatingSystem
import java.util.Properties

plugins {
	alias(libs.plugins.android.application)
	alias(libs.plugins.kotlin.android)
	alias(libs.plugins.kotlin.compose)
}

val abis = (findProperty("evvdroid.abis") as String? ?: "arm64-v8a,armeabi-v7a,x86_64")
	.split(",").map { it.trim() }.filter { it.isNotEmpty() }
val rulesForm = findProperty("evvdroid.rules") as String? ?: "c"
val languages = findProperty("evvdroid.langs") as String? ?: "lang/enus"
val nativeOut = layout.buildDirectory.dir("native/jniLibs")

// Release signing, from a keystore.properties the repo does not carry. Without
// it a clone still builds and the release APK comes out unsigned.
val keystore = rootProject.file("keystore.properties").takeIf { it.exists() }?.let { where ->
	Properties().apply { where.inputStream().use { stream -> load(stream) } }
}

// The engine is built by its own Makefile rather than by CMake, because that
// Makefile writes the language rules with Python before it compiles them and
// nothing here has a reason to reimplement that.
val buildNative by tasks.registering(Exec::class) {
	group = "build"
	description = "Cross-compiles the openevv engine and the JNI bridge for each ABI."
	val engine = rootProject.layout.projectDirectory.dir("native/openevv")
	val script = rootProject.layout.projectDirectory.file("native/build-native.sh")
	// Deliberately not the engine's own sources. The script patches them, so a
	// task declaring them would be rewriting its own inputs every time it ran
	// and would never settle: the first build after any change fails and the
	// next one succeeds. What decides the output is the submodule's commit, the
	// patches laid on it, and the files named here. Editing native/openevv by
	// hand wants --rerun-tasks. The patches are a file tree rather than a
	// directory because there are none at the moment, and git does not carry an
	// empty one.
	inputs.files(rootProject.fileTree("native/patches"))
	inputs.file(rootProject.layout.projectDirectory.file("native/android.mk"))
	inputs.file(rootProject.layout.projectDirectory.file(".gitmodules"))
	inputs.file(script)
	inputs.file(layout.projectDirectory.file("src/main/cpp/evv_jni.c"))
	inputs.property("abis", abis)
	inputs.property("rules", rulesForm)
	inputs.property("langs", languages)
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
	doFirst {
		if (!engine.file("Makefile").asFile.exists()) {
			throw GradleException("native/openevv is empty. Run: git submodule update --init --recursive")
		}
	}
}

android {
	namespace = "org.evvdroid"
	compileSdk = 36
	ndkVersion = "26.1.10909125"

	defaultConfig {
		applicationId = "org.evvdroid"
		minSdk = 23
		targetSdk = 36
		versionCode = 1
		versionName = "0.1.0"
		testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
		ndk {
			abiFilters += abis
		}
	}

	sourceSets.getByName("androidTest") {
		kotlin.srcDirs("src/androidTest/kotlin")
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
			isEnable = (findProperty("evvdroid.abiSplits") as String?)?.toBoolean() ?: false
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

	androidTestImplementation(libs.junit)
	androidTestImplementation(libs.androidx.test.junit)
	androidTestImplementation(libs.androidx.test.runner)
	androidTestImplementation(libs.androidx.test.core)
}
