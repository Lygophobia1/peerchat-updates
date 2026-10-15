# Android dev.162 — existing FCM adapter configured

Date: 2026-10-15. Background-wake physical acceptance pending. No phone install, Pi mutation, routing change, registration edit, key rotation, trust reset or history/data migration performed.

## Scope and provenance

Worktree: `C:/Users/Livel/Documents/Codex/peerchat-android-groups-voice`; branch `codex/android-groups-voice`.
Build input HEAD: `cd0707cc5a48616ad314d0fbe1017404fb9f5916`; application code remains the dev.161 implementation checkpoint `dba61c093beaffb217ef79bafe29eb4384955289`.
This candidate changes only local build configuration and the allocated Android version. The final documentation checkpoint is recorded in the deliverable checkpoint.json. Tracked application source is unchanged.

The standard ignored `android/app/google-services.json` was missing in this worktree, so dev.161 compiled FCM_CONFIGURED=false. Existing identical configuration copies were found read-only in the older go-hpke-production, windows-android-unified and network-cleanup-phase1 worktrees. Copied the go-hpke-production configuration into this Android worktree; none of those source worktrees was modified.

Configuration SHA256: `8257a1a223e5862e2422d8eed7d0258941e2a0559e1b305294b53f1df9ebd6b8`.
Firebase project: `peerchat-development`; Android package: `com.peerchat.android`.
Existing Firebase application ID: `1:732729608126:android:b033fc0a0136f978d89bef`.
A read-only check confirmed the Pi is already configured with this exact project and FcmEnabled=true. No Pi credential file was read; no service-account secret was copied into the app. The Android config contains public client configuration, remains ignored by Git and is retained locally for subsequent builds. No new Firebase project, token substitution or trust system was created. The config has no OAuth certificate entries; the actual APK signing certificate was independently checked against dev.161.

## Enabled existing path

The unchanged Gradle configuration recognizes the matching Firebase Android client, applies the existing Google-services plugin, generates the project/app/sender resources and sets FCM_CONFIGURED=true. Existing PeerChatAndroidApplication can now initialize Firebase and obtain a token. Existing PeerChatFcmService can process opaque push hints and schedule the existing bounded wake job. The app registers its current token through the existing authenticated account/device path. This normal runtime behavior occurs on the user's phone after installation/opening; no registration was remotely edited during this task.

The existing remembered-login/native mailbox/Keystore profile is reused by the bounded worker for authentication, accepted recipient registration, fetch, decrypt, durable import and signed ACK. Existing notification permissions and preferences still apply. No permanent socket, extra polling service, messaging protocol or routing policy was added.

The successful dev.161 scoped account-4 enrollment, original account-19 binding, permanent identity, verified Sammy fingerprint, history and outbox are preserved by using the same package and signing certificate. No new reenrollment or reset is required by this configuration change.

## Package and validation

APK: `PeerChat-4.0.2-dev.162-fcm-background-wake-debug.apk`.
App ID: `com.peerchat.android`; versionName `4.0.2-dev.162`; versionCode 2; ARM64; min SDK 26; target SDK 35.
APK SHA256: `a323215e67cb93488c0869fd9825215188f1f1381757d6b93fc10afd4ce9ebb0`.
Signer certificate SHA256: `db65bb23a9dc96e7504ff1156bbee6108af9131dbe43164dc39d11bf802d4141`, same certificate as dev.161.

- Gradle testDebugUnitTest, lintDebug and assembleDebug: BUILD SUCCESSFUL in 1m36s.
- 219 Android host tests passed, zero failures/errors/skips, with the configured Firebase variant.
- Lint: zero errors/fatal issues, 25 warnings. Existing Firebase SDK token/override deprecation compiler warnings remain; no warning suppression or SDK/toolchain replacement was added.
- Generated BuildConfig confirms dev.162 and FCM_CONFIGURED=true.
- Generated Google-services resources match the original client app ID, project and sender ID. Final APK resource inspection independently confirms the expected app ID/project are packaged; API-key values were not printed.
- APK signature verified with the existing SDK; package/version/ABI checked. No release APK or new instrumentation run was produced. Native binaries/protocol source did not change, so the previously passed scoped offline/security/TLS suites were not rerun unnecessarily.

Build log and JUnit/lint evidence accompany the delivered APK. This proves the configuration/build boundary, not vendor delivery, Android job execution or phone notification acceptance.

## Physical test instructions

1. Install over dev.161; do not uninstall, clear storage or reset identities. Open PeerChat once, retain the existing remembered login, and wait for authenticated account 4/presence. Confirm the same Motorola identity `61f70fb01a6055499eed089025e8e8e2850d640b4c4a6ba3ad772168f092b51b`, Sammy trust and history. Allow notifications using the existing Android prompt/settings if desired. No manual reenrollment is needed for this config update.
2. Confirm live bidirectional messaging still works. Close the Windows PHONE-profile session. Press Home on Android and lock the screen while leaving its network available; use this ordinary background state for the first test, rather than Android Settings Force stop.
3. Sammy sends one uniquely labeled message and notes Stored for delivery plus send time. Fully close Sammy's client. Leave Android unopened and record whether/when a notification arrives. Do not reopen merely to trigger a successful foreground fetch before measuring the wake result.
4. Then open PeerChat and confirm exactly one copy is present, persists after restart, and has a Motorola-specific ACK when inspected. Windows' separate encrypted copy may remain pending; it must not be retargeted or deleted as part of this test.
5. If no notification arrives, report the send time, whether the message was already present when opening, app/background/lock state, notification permission and current diagnostic text. With optional local Android logging, capture only PeerChatWake stages: fcm_received, wake_schedule_accepted/unavailable, wake_job_started, remembered authentication, mailbox_receive_batch and wake_delivery_complete. These distinguish provider arrival, scheduling, authenticated import and notification display. Do not paste tokens or private credentials.

After the first successful ordinary-background test, separately repeat restart, longer idle, swipe-away and transient-network cases. Treat force-stop separately. Server-side FCM acceptance, token freshness, OS scheduling and notification suppression may still require focused diagnosis; no unattended acceptance is claimed here.

## Integration and remaining scope

No shared native files, JNI/C ABI, group crypto, attachment protocol, media protocol or server interface changed. Main Work's committed attachment manifest was reviewed in the preceding routing audit and no dependent interface changed here. Main Work, Windows/Pi attachment work and live server configuration remain untouched.

This candidate does not resolve the independent outgoing mailbox_keys rejection or History Sync relay failure shown in the dev.161 report. It retains the scoped-enrollment storage-OFF limitation documented in dev.161. Stop for physical background-wake testing before any further repair.

## Reproduce without losing configuration

Retain the ignored Android client config with the SHA256 above in android/app/google-services.json. Use the existing SDK/JDK and signing store, then run the same Gradle tasks with `-PpeerchatVersion=4.0.2-dev.162`; ANDROID_HOME points to the existing SDK, and the existing user.home is C:/Users/Livel. Verify FCM_CONFIGURED=true, generated project/app IDs and signing digest before distributing another APK. A fresh checkout without this ignored input would again build an unconfigured adapter; do not mistake that APK for this candidate.
