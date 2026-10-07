> HISTORICAL ONLY — describes the dev.17 evaluation/proof, not the current provider or build instructions. The former provider, evaluation switch and dependency have been removed. See GO_HPKE_PRODUCTION_INTEGRATION.md for the current Go integration.

# dev.17 reproducible mailbox / FCM checkpoint

This checkpoint preserves the already-tested mailbox/FCM dependencies together
with the dev.17 known-sender/local-history correction. It is not an authoritative-main
merge or physical acceptance claim. The checkpoint hash is recorded by Git and the
physical validation report, not by a self-referential source constant.

## Exact included files

- `.gitignore`
- `android/app/build.gradle.kts`
- `android/app/src/androidTest/java/com/peerchat/android/NativeProfileTest.kt`
- `android/app/src/main/AndroidManifest.xml`
- `android/app/src/main/java/com/peerchat/android/ReliableBridge.kt`
- `android/app/src/main/java/com/peerchat/android/application/AndroidMessagingRuntime.kt`
- `android/app/src/main/java/com/peerchat/android/application/AndroidRelayExecution.kt`
- `android/app/src/main/java/com/peerchat/android/application/MessagingApplication.kt`
- `android/app/src/main/java/com/peerchat/android/application/MessagingBackend.kt`
- `android/app/src/main/java/com/peerchat/android/application/MessagingSnapshots.kt`
- `android/app/src/main/java/com/peerchat/android/application/ReliableRecords.kt`
- `android/app/src/main/java/com/peerchat/android/platform/AndroidQolStore.kt`
- `android/app/src/main/java/com/peerchat/android/platform/AndroidStoragePlatform.kt`
- `android/app/src/main/java/com/peerchat/android/platform/FriendIdentityIndex.kt`
- `android/app/src/main/java/com/peerchat/android/platform/OpaquePushWake.kt`
- `android/app/src/main/java/com/peerchat/android/platform/OpaqueRecipientHpke.java`
- `android/app/src/main/java/com/peerchat/android/platform/PeerChatWakeJob.kt`
- `android/app/src/main/java/com/peerchat/android/platform/ServerAccounts.kt`
- `android/app/src/main/java/com/peerchat/android/platform/tls/ServerChannel.java`
- `android/app/src/main/java/com/peerchat/android/ui/CallsAndYou.kt`
- `android/app/src/main/java/com/peerchat/android/ui/ChatsHome.kt`
- `android/app/src/main/java/com/peerchat/android/ui/ConversationScreen.kt`
- `android/app/src/main/java/com/peerchat/android/ui/NativePresenter.kt`
- `android/app/src/main/java/com/peerchat/android/ui/PeerChatApp.kt`
- `android/app/src/main/java/com/peerchat/android/ui/UiState.kt`
- `android/app/src/test/java/com/peerchat/android/application/NativeProjectionTest.kt`
- `android/app/src/test/java/com/peerchat/android/ui/NativePresenterTest.kt`
- `android/build.gradle.kts`
- `build.ps1`
- `delivery_manager.hpp`
- `desktop/App.cs`
- `desktop/ChatViewModel.cs`
- `desktop/CoreAdapter.cs`
- `desktop/DiscoveryClient.cs`
- `device_key_manager.hpp`
- `docs/ANDROID_QOL_NO_PHONE.md`
- `docs/ANDROID_REMEMBER_ME.md`
- `node.cpp`
- `node.hpp`
- `portable/peerchat_core.cpp`
- `reliable_peer_protocol.hpp`
- `scripts/publish-update.ps1`
- `server/PeerChat.Server.cs`
- `server/ServerConfiguration.cs`
- `storage.cpp`
- `storage.hpp`
- `tests/device_key_manager_test.cpp`
- `tests/reliable_peer_protocol_test.cpp`
- `tests/remembered_login_test.cs`
- `tests/test-android-profile-artifacts.ps1`
- `tests/test-android-tls-artifacts.ps1`
- `tests/test-group-ciphertext-fanout.ps1`
- `tests/test-group-media-rooms.ps1`
- `tests/test-group-registration.ps1`
- `tests/test-portable-storage.ps1`
- `tests/test-remembered-login.ps1`
- `tests/tls-host/ServerDeadlineProof.java`
- `tests/tls-host/ServerRelayProof.java`
- `tests/tls-host/WolfCryptCngEvaluation.java`
- `ui_bridge.cpp`
- `KEY_ARCHITECTURE_AUDIT.md`
- `android/app/src/androidTest/java/com/peerchat/android/FcmDeviceTest.kt`
- `android/app/src/androidTest/java/com/peerchat/android/HpkeInteropDeviceTest.kt`
- `android/app/src/androidTest/java/com/peerchat/android/NativeMailboxTest.kt`
- `android/app/src/androidTest/java/com/peerchat/android/WakeSystemDeviceTest.kt`
- `android/app/src/main/java/com/peerchat/android/MailboxBridge.kt`
- `android/app/src/main/java/com/peerchat/android/application/MailboxExecution.kt`
- `android/app/src/main/java/com/peerchat/android/application/RememberedAuthentication.kt`
- `android/app/src/main/java/com/peerchat/android/platform/LocalConversationContext.kt`
- `android/app/src/main/java/com/peerchat/android/platform/PeerChatFcmService.kt`
- `android/app/src/main/java/com/peerchat/android/platform/WakeSystemDiagnostics.kt`
- `android/app/src/test/java/com/peerchat/android/platform/LocalConversationContextTest.kt`
- `android/app/src/test/java/com/peerchat/android/platform/MessagingBootstrapTest.kt`
- `android/app/src/test/java/com/peerchat/android/platform/OpaqueMailboxHpkeTest.kt`
- `android/app/src/test/java/com/peerchat/android/platform/OpaqueWakeSchedulingTest.kt`
- `android/app/src/test/java/com/peerchat/android/ui/RememberedAuthenticationTest.kt`
- `android/app/src/test/java/com/peerchat/android/ui/WakeInterruptionTest.kt`
- `desktop/MailboxExecution.cs`
- `docs/ANDROID_HEADLESS_BOOTSTRAP.md`
- `docs/ANDROID_LOCAL_HISTORY_MAILBOX_ORDERING.md`
- `docs/ANDROID_NETWORK_STARTUP.md`
- `docs/ANDROID_THERMAL_WAKE_INVESTIGATION.md`
- `docs/ENCRYPTED_DM_MAILBOX.md`
- `docs/MAILBOX_EVALUATION_RUNTIME.md`
- `mailbox_key_coordinator.hpp`
- `mailbox_protocol.hpp`
- `platform/hpke_provider.hpp`
- `platform/sql_key_registry.hpp`
- `platform/windows_sql_database.hpp`
- `platform/wolfcrypt_evaluation_hpke.cpp`
- `portable/android_hpke_provider.hpp`
- `server/EncryptedMailbox.cs`
- `server/EncryptedMailboxChannel.cs`
- `server/MailboxProtocol.cs`
- `tests/android_hpke_adapter_compile_test.cpp`
- `tests/encrypted_mailbox_server_test.cs`
- `tests/fcm_provider_test.cs`
- `tests/hpke_device_interop_test.cpp`
- `tests/hpke_provider_test.cpp`
- `tests/mailbox_protocol_test.cpp`
- `tests/sql_key_registry_test.cpp`
- `tests/test-fcm-provider.ps1`
- `tests/test-hpke-package-containment.ps1`
- `tests/test-hpke-physical-interop.ps1`
- `tests/test-hpke-provider.ps1`
- `tests/test-mailbox-protocol.ps1`
- `windows_mailbox.hpp`

## Deliberately excluded and preserved

- `peerchat_screen_media.obj`, `screen_decoder.obj`, `screen_encoder.obj`: generated objects.
- `tests/fcm_live_probe.cs`: one-off manual FCM probe; not referenced by application, build, or automated regression inputs.
- All ignored build outputs, APKs, binaries, caches and external secrets/configuration.

No screen/voice implementation is changed. Existing session/delivery owners remain authoritative.
Android cached conversation data is display-only; mailbox authentication remains fail-closed.

## Reproduction

Reuse the documented pinned shared dependency caches and HPKE evaluation artifacts.
Supply the existing external Firebase Android configuration privately; it is ignored
and contains no server service-account private key. Do not commit server credentials.

Android: JDK 22, configured SDK/NDK, `android/gradlew.bat --offline
-PpeerchatVersion=4.0.2-dev.17 :app:testDebugUnitTest :app:assembleDebug
:app:assembleDebugAndroidTest :app:assembleRelease :app:lintDebug`.

Windows: `build.ps1 -All -HpkeEvaluation` builds the same committed sources with
the next immutable revision. The preserved dev.17 Windows package was built from
these identical Windows inputs; no old output is overwritten to recreate a revision.

See `ANDROID_LOCAL_HISTORY_MAILBOX_ORDERING.md`, `ENCRYPTED_DM_MAILBOX.md` and
`MAILBOX_EVALUATION_RUNTIME.md` for regression evidence, protocol boundaries and
external input hashes. Source reproduction does not imply byte-reproducible ZIP/APK
timestamps or equivalence to physical locked-screen tests.

Evaluation only / non-publishable: wolfCrypt shipping license unresolved.
