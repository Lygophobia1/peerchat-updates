plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}
// Integrated app always includes the shared native messaging engine.
val uiOnly = false
// Shared product version is authoritative for Android as well as Windows.
// An explicitly allocated development revision may be supplied by build tooling.
val sourceVersion = Regex("Display\\s*=\\s*\"([^\"]+)\"")
    .find(file("../../shared/VersionInfo.cs").readText())?.groupValues?.get(1)
    ?: error("Missing shared PeerChat version")
val peerChatVersion = providers.gradleProperty("peerchatVersion").orElse(sourceVersion).get()
require(Regex("[0-9]+\\.[0-9]+\\.[0-9]+-dev\\.[0-9]+").matches(peerChatVersion))
android {
    namespace = "com.peerchat.android"
    compileSdk = 35
    buildToolsVersion = "34.0.0"
    ndkVersion = "27.0.12077973"
    defaultConfig {
        applicationId = "com.peerchat.android"
        minSdk = 26
        targetSdk = 35
        versionCode = 2
        versionName = peerChatVersion
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        if (!uiOnly) {
            ndk { abiFilters += "arm64-v8a" }
            externalNativeBuild { cmake { arguments += "-DPEERCHAT_PORTABLE_TESTS=OFF" } }
        }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" } }
    buildTypes {
        getByName("debug") { externalNativeBuild { cmake { arguments += "-DPEERCHAT_STORAGE_TEST_HOOKS=ON" } } }
        getByName("release") { externalNativeBuild { cmake { arguments += "-DPEERCHAT_STORAGE_TEST_HOOKS=OFF" } } }
    }
    buildFeatures { compose = true; buildConfig = true }
    // Java-version/OSGi manifests are not Android runtime resources. Base classes
    // are handled by D8; no provider/global registration is installed.
    packaging {
        resources.excludes += "META-INF/versions/**/OSGI-INF/MANIFEST.MF"
        // Retain the identical upstream license instead of discarding it.
        resources.pickFirsts += "META-INF/LICENSE.md"
    }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
    testOptions { unitTests.isIncludeAndroidResources = true }
    sourceSets {
        getByName("test").java.srcDir("src/sharedUiTest/java")
        getByName("androidTest").java.srcDir("src/sharedUiTest/java")
    }
}
dependencies {
    // Pure Java TLS only. No JSSE provider registration/global provider changes.
    implementation("org.bouncycastle:bctls-jdk18on:1.86.1")
    implementation("org.bouncycastle:bcpkix-jdk18on:1.86")
    implementation("org.bouncycastle:bcprov-jdk18on:1.86")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.compose.material3:material3")
    implementation("androidx.compose.ui:ui-tooling-preview")
    implementation("androidx.compose.material:material-icons-core")
    implementation("androidx.lifecycle:lifecycle-viewmodel-compose:2.8.7")
    testImplementation("junit:junit:4.13.2")
    testImplementation("org.jetbrains.kotlinx:kotlinx-coroutines-test:1.7.3")
    testImplementation("org.robolectric:robolectric:4.15.1")
    testImplementation(platform("androidx.compose:compose-bom:2024.12.01"))
    testImplementation("androidx.compose.ui:ui-test-junit4")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test:runner:1.6.2")
    androidTestImplementation(platform("androidx.compose:compose-bom:2024.12.01"))
    androidTestImplementation("androidx.compose.ui:ui-test-junit4")
    debugImplementation("androidx.compose.ui:ui-tooling")
    debugImplementation("androidx.compose.ui:ui-test-manifest")
}
configurations.configureEach {
    resolutionStrategy.force("org.bouncycastle:bctls-jdk18on:1.86.1", "org.bouncycastle:bcpkix-jdk18on:1.86",
        "org.bouncycastle:bcutil-jdk18on:1.86", "org.bouncycastle:bcprov-jdk18on:1.86")
}
