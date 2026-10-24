import java.security.MessageDigest

plugins {
    id("com.google.gms.google-services") apply false
    id("com.android.application")
    id("org.jetbrains.kotlin.android")
    id("org.jetbrains.kotlin.plugin.compose")
}
// Integrated app always includes the shared native messaging engine.
apply(from = "../hpke-dependency.gradle.kts")
val uiOnly = false
// Isolated emulator package cannot replace the user's signed application/data.
val voiceIntegrationTest = providers.gradleProperty("peerchatVoiceIntegrationTest").orNull == "true"
// Separate fresh default profiles for cold/background runtime tests. Never
// replace the production app or the retained voice/attachment fixture profiles.
val backgroundIntegrationTest = providers.gradleProperty("peerchatBackgroundIntegrationTest").orNull == "true"
require(!backgroundIntegrationTest || voiceIntegrationTest)
val voiceEnabled = providers.gradleProperty("peerchatVoiceEnabled").orNull == "true"
val nativeAbi = providers.gradleProperty("peerchatTestAbi").orElse("arm64-v8a").get()
require(nativeAbi == "arm64-v8a" || (voiceIntegrationTest && nativeAbi == "x86_64"))
val receiveDirectory = providers.gradleProperty("peerchatVoiceReceiveDirectory").orNull
val voiceNotices = file("../../build/android-neteq/notices")
if(voiceEnabled) {
    require(receiveDirectory != null) { "Voice requires the validated genuine receive archive" }
    val archive = file("$receiveDirectory/libpeerchat_audio_receive.a")
    require(archive.isFile) { "Missing genuine receive archive" }
    val expected = if(nativeAbi == "arm64-v8a") "fcef3218613460ca02eae2bd83f997a5a7e6a3097440a7c9e2b19f6472bd47e4"
        else "671d90a6a95d4bfc4a8561dbd28196138662449a98ccefd1a9334d9da190a14e"
    val actual = MessageDigest.getInstance("SHA-256").digest(archive.readBytes()).joinToString("") { "%02x".format(it.toInt() and 255) }
    require(actual == expected) { "Receive archive pin mismatch" }
    val noticeBundle = file("$voiceNotices/PeerChatVoiceThirdPartyNotices.txt")
    require(noticeBundle.isFile && file("$voiceNotices/PeerChatVoiceThirdPartyNotices.manifest").isFile) {
        "Generate the pinned Android voice license bundle before packaging"
    }
    val noticeHash = MessageDigest.getInstance("SHA-256").digest(noticeBundle.readBytes()).joinToString("") { "%02x".format(it.toInt() and 255) }
    require(noticeHash == "d02f083a5ce0e2375cee4f6786a3035f70ef5a10d000d46c37a97859b422b131") { "Android voice notice pin mismatch" }
}
// Shared product version is authoritative for Android as well as Windows.
// An explicitly allocated development revision may be supplied by build tooling.
val sourceVersion = Regex("Display\\s*=\\s*\"([^\"]+)\"")
    .find(file("../../shared/VersionInfo.cs").readText())?.groupValues?.get(1)
    ?: error("Missing shared PeerChat version")
val peerChatVersion = providers.gradleProperty("peerchatVersion").orElse(sourceVersion).get()
require(Regex("[0-9]+\\.[0-9]+\\.[0-9]+-dev\\.[0-9]+").matches(peerChatVersion))
// Standard Google-services input, ignored by Git. No service-account keys belong
// in this Android file; server credentials remain external to the app/package.
val firebaseConfigPath = file("google-services.json").takeIf { it.isFile && !voiceIntegrationTest }?.absolutePath
val firebaseConfig = firebaseConfigPath?.let {
    @Suppress("UNCHECKED_CAST")
    groovy.json.JsonSlurper().parse(file(it)) as Map<String, Any>
}
@Suppress("UNCHECKED_CAST")
val firebaseProject = firebaseConfig?.get("project_info") as? Map<String, Any>
@Suppress("UNCHECKED_CAST")
val firebaseClient = (firebaseConfig?.get("client") as? List<Map<String, Any>>)?.singleOrNull { client ->
    val info = client["client_info"] as Map<*, *>
    (info["android_client_info"] as Map<*, *>)["package_name"] == "com.peerchat.android"
}
if(firebaseConfig != null) require(firebaseProject != null && firebaseClient != null) { "Firebase package configuration mismatch" }
if(firebaseConfigPath != null) {
    pluginManager.apply("com.google.gms.google-services")
}
android {
    namespace = "com.peerchat.android"
    compileSdk = 35
    buildToolsVersion = "34.0.0"
    ndkVersion = "27.0.12077973"
    defaultConfig {
        applicationId = if(backgroundIntegrationTest) "com.peerchat.android.backgroundintegration" else if(voiceIntegrationTest) "com.peerchat.android.voiceintegration" else "com.peerchat.android"
        minSdk = 26
        targetSdk = 35
        versionCode = 2
        versionName = peerChatVersion
        buildConfigField("boolean", "FCM_CONFIGURED", (firebaseClient != null).toString())
        testInstrumentationRunner = "androidx.test.runner.AndroidJUnitRunner"
        if (!uiOnly) {
            ndk { abiFilters += nativeAbi }
            externalNativeBuild { cmake {
                arguments += "-DPEERCHAT_PORTABLE_TESTS=OFF"
                arguments += "-DPEERCHAT_ANDROID_VOICE_ENABLED=${if(voiceEnabled)"ON" else "OFF"}"
                arguments += "-DPEERCHAT_ANDROID_TEST_ABI=${if(voiceIntegrationTest)"ON" else "OFF"}"
                arguments += "-DPEERCHAT_ANDROID_RECEIVE_LIBRARY=${receiveDirectory?.let{file("$it/libpeerchat_audio_receive.a").absolutePath} ?: ""}"
                arguments += "-DPEERCHAT_ANDROID_RECEIVE_INCLUDE_DIR=${receiveDirectory?.let{file("$it/include").absolutePath} ?: ""}"
            } }
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
        if(voiceEnabled) getByName("main").assets.srcDir(voiceNotices)
        getByName("test").java.srcDir("src/sharedUiTest/java")
        getByName("androidTest").java.srcDir("src/sharedUiTest/java")
    }
}
dependencies {
    implementation(platform("com.google.firebase:firebase-bom:34.19.0"))
    implementation("com.google.firebase:firebase-messaging")
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
