Release checkpoint for **Windows 4.0.2-dev.251** and **Android 4.0.2-dev.250** from source commit `873f9500eb3a73dc88ebfc80e8e970c187ab0405`.

- Windows can start authenticated friend call preparation while the phone is offline/idle. Secure route and device verification remain required before starting the call.
- Android history opens from a bounded, encrypted recent-message cache. First-time history indexing runs independently of live messages and transfers.
- Includes the checkpointed attachment, photo preview, local deletion, group delivery and call interface work. Accepted RNNoise tuning is retained.

Windows: extract the ZIP for a manual install, or use the app's update checker after the feeds are published. Profiles and history are outside the update package.

Android: install the APK as an update to an installation signed with the same existing certificate. This checkpoint has **no Android in-app updater**, so publishing metadata cannot make older installations self-update. It is the existing full arm64 development-signed APK, not a newly re-signed package. Android may ask permission to install from the browser/download source. Do not uninstall to update; uninstalling removes local device identity/history.

Validation: Windows call-header checks and 12 alternating Windows/emulator calls passed, including bidirectional media counters, call controls and teardown. Android: 653 unit tests, lint and nine-large-chat history tests passed. Physical-phone cold/background wake remains an acceptance test; emulator media counters do not establish subjective listening quality.

SHA-256:

```text
731201a6dd11e5573cf1f138010ccc8dfadedfc8511fe3abb55526e28c1643c2  PeerChat-Windows-4.0.2-dev.251-full.zip
1eab18023777e12a66d0409d81722706193fdd0367bceeb12f5baeb61ec11705  PeerChat-Android-4.0.2-dev.250-full.apk
```

Version identifiers remain `dev` to identify the exact tested bytes. Signed Windows Release and Development feeds point to the same ZIP; Android metadata is published separately to avoid offering an APK to the Windows updater.
