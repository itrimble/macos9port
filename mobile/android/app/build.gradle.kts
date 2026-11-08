import org.gradle.api.tasks.Exec
import org.jetbrains.kotlin.gradle.dsl.JvmTarget

plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
}

// Repository root (two levels up from mobile/android/app) and the pinned SDL2 checkout.
val repoRoot = rootDir.resolve("../..").canonicalFile
val sdlDir = repoRoot.resolve("build/deps/SDL2")
val generatedAssets = layout.buildDirectory.dir("generated/os999-assets")

android {
    namespace = "org.remnantsecurity.os999"
    compileSdk = 36
    ndkVersion = "28.2.13676358"

    defaultConfig {
        applicationId = "org.remnantsecurity.os999"
        minSdk = 28   // posix_spawn (desktop installer) arrived in API 28; every foldable ships 10+
        targetSdk = 36
        versionCode = 1
        versionName = "0.1"
        ndk { abiFilters += listOf("arm64-v8a", "x86_64") }
        externalNativeBuild {
            cmake { arguments += listOf("-DANDROID_STL=c++_static") }
        }
    }
    externalNativeBuild {
        cmake { path = file("../../CMakeLists.txt") }
    }
    sourceSets["main"].apply {
        // SDLActivity and friends come from the SDL2 release we compile against.
        java.srcDir(sdlDir.resolve("android-project/app/src/main/java"))
        assets.srcDir(generatedAssets)
    }
    buildTypes {
        release {
            isMinifyEnabled = false
            signingConfig = signingConfigs.getByName("debug")
        }
    }
    compileOptions {
        sourceCompatibility = JavaVersion.VERSION_17
        targetCompatibility = JavaVersion.VERSION_17
    }
}

kotlin {
    compilerOptions { jvmTarget.set(JvmTarget.JVM_17) }
}

// Fonts, icons, sounds and wallpapers the desktop reads by relative path,
// plus os999-assets.txt, which the native side unpacks into internal storage.
val os999Assets by tasks.registering(Exec::class) {
    val out = generatedAssets.get().asFile
    outputs.dir(out)
    commandLine("cmake", "-DROOT=$repoRoot", "-DDEST=$out", "-DLIST=$out/os999-assets.txt",
        "-P", repoRoot.resolve("mobile/scripts/copy-resources.cmake").path)
}
tasks.named("preBuild") { dependsOn(os999Assets) }

dependencies {
    implementation("androidx.core:core-ktx:1.17.0")
    implementation("androidx.window:window:1.5.1")
    implementation("androidx.window:window-java:1.5.1")
    implementation("androidx.core:core-splashscreen:1.0.1")
}
