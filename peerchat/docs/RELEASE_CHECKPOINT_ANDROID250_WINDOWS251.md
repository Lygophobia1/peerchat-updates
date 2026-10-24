# Release checkpoint: Android dev.250 and Windows dev.251

This is the user-requested release checkpoint, retaining development version identifiers. It freezes the current attachment, history, Android background operation and call interface work. It is not a claim that all physical-device acceptance tests have passed.

## Artifacts

| Platform | Artifact | SHA-256 |
| --- | --- | --- |
| Android | PeerChat-Android-4.0.2-dev.250-full.apk | 1EAB18023777E12A66D0409D81722706193FDD0367BCEEB12F5BAEB61EC11705 |
| Windows | PeerChat-Windows-4.0.2-dev.251-full.zip | 731201A6DD11E5573CF1F138010CCC8DFADEDFC8511FE3ABB55526E28C1643C2 |

Android is the full arm64 package. Windows packages the newly compiled managed desktop with the existing dev.245 native/audio/runtime binaries. No RNNoise tuning was changed for this checkpoint. The Windows RNNoise DLL remains byte-identical to dev.245 (SHA-256 D43B43B0C712A73B387CBC55B81FC378C19583244C4C2B2D1BB54ED4E9B1FC6E).

## Windows call availability correction

The observed dev.245 Call button required the selected device to already be verified, before the user could start route preparation. The observed friend was offline/idle and that selected device was unverified. This blocked the authenticated friend route and existing server wake hint from being attempted.

An authenticated, established friend can now begin call preparation without a live presence/event connection or a pre-existing peer session. Secure route establishment and device verification still precede starting the actual call. Signed-out, removed-friend, busy and active-call cases remain blocked. Legacy direct contacts still require an online, verified device. The change does not automatically trust changed fingerprints or alter server infrastructure.

## Validation

- Call-header UI tests passed, including the actual disabled/enabled button behavior, preparation, cancellation, incoming/outgoing/connected/failure, group states and diagnostics controls.
- Twelve alternating Windows/emulator calls using codex/codex1 completed. Each checked received and sent packet counters, call-control availability, mute/unmute and teardown. Six warmups precede six measured rounds.
- Measured median start-to-ring: Windows to Android 822 ms; Android to Windows 171 ms. Median accept-to-connected: 2116 ms and 998 ms respectively. Maximum measured accept-to-connected was 4671 ms. These are emulator/network measurements, not physical listening results.
- The benchmark also emitted redacted native diagnostic events; their text was not retained by its privacy filter. Successful call assertions do not establish an error-free diagnostic stream.
- Android history validation: 653 unit tests and lint passed. Nine chats with 1000 messages each reopened in 854 ms after the derived cache was built. The first migration still took about 77 seconds in the background; durable message queueing completed in 96 ms during that scan.
- Native nine-chat, 4000-message-per-chat checks covered bounded recent-page reads, invalid derived-cache fallback, older paging and deletion. Independent history reads avoid holding the live message/transfer owner lock.
- Retained-history cached-image regression confirmed four 2,368,998-byte uploads in 54.7 seconds total. Server upload confirmation is not recipient delivery confirmation.

## Remaining acceptance / release boundaries

Physical Android cold/background wake, notification delivery and listening quality must still be checked on the phone; no phone was attached for this validation. Emulator calls and the Windows button test cover separate portions of this path. No Pi update is required for the Windows preparation correction. Ordinary messaging and RNNoise were not changed by that correction.

The source checkpoint includes the preceding accepted development work since the previous local checkpoint. Release binaries are identified above rather than committed into Git. The existing remote release manifests are not overwritten by this source checkpoint.
