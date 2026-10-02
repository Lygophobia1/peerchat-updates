plugins {
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}
android {
    namespace = "com.peerchat.android"
    compileSdk = 35
    buildToolsVersion = "34.0.0"
    ndkVersion = "27.0.12077973"
    defaultConfig {
        applicationId = "com.peerchat.android"
        minSdk = 26
        targetSdk = 35
        versionCode = 1
        versionName = "Android dev.1"
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        ndk { abiFilters += "arm64-v8a" }
        externalNativeBuild { cmake { arguments += "-DPEERCHAT_PORTABLE_TESTS=OFF" } }
    }
    externalNativeBuild { cmake { path = file("src/main/cpp/CMakeLists.txt"); version = "3.22.1" } }
    buildTypes {
        getByName("debug") { externalNativeBuild { cmake { arguments += "-DPEERCHAT_STORAGE_TEST_HOOKS=ON" } } }
        getByName("release") { externalNativeBuild { cmake { arguments += "-DPEERCHAT_STORAGE_TEST_HOOKS=OFF" } } }
    }
    buildFeatures { compose = true }
    // Java-version/OSGi manifests are not Android runtime resources. Base classes
    // are handled by D8; no provider/global registration is installed.
    packaging {
        resources.excludes += "META-INF/versions/**/OSGI-INF/MANIFEST.MF"
        // Retain the identical upstream license instead of discarding it.
        resources.pickFirsts += "META-INF/LICENSE.md"
    }
    compileOptions { sourceCompatibility = JavaVersion.VERSION_17; targetCompatibility = JavaVersion.VERSION_17 }
    kotlinOptions { jvmTarget = "17" }
}
dependencies {
    // Pure Java TLS only. No JSSE provider registration/global provider changes.
    implementation("org.bouncycastle:bctls-jdk18on:1.86.1")
    implementation("org.bouncycastle:bcpkix-jdk18on:1.86")
    implementation("org.bouncycastle:bcprov-jdk18on:1.86")
    implementation("androidx.activity:activity-compose:1.9.3")
    implementation(platform("androidx.compose:compose-bom:2024.12.01"))
    implementation("androidx.compose.material3:material3")
    androidTestImplementation("androidx.test.ext:junit:1.2.1")
    androidTestImplementation("androidx.test:runner:1.6.2")
}
configurations.configureEach {
    resolutionStrategy.force("org.bouncycastle:bctls-jdk18on:1.86.1", "org.bouncycastle:bcpkix-jdk18on:1.86",
        "org.bouncycastle:bcutil-jdk18on:1.86", "org.bouncycastle:bcprov-jdk18on:1.86")
}
