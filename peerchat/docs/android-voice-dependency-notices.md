# Android voice dependency notices

## Scope and evidence

The Android receive archive is built from the pinned WebRTC source revision
`6cf6bcb585b31414aac02377df2bc8fec770d0e8` and the `peerchat_audio_receive`
target in `audio_receive/BUILD.android.gn`. That target includes WebRTC's
`DefaultNetEqFactory`, WebRTC Opus decoder, environment factory, audio mixer,
and `system_wrappers`; the complete static archive incorporates the linked
transitive object files. The generated target's exact transitive source/license
closure must be taken from the pinned build output, not inferred from the
declared top-level dependencies.

Read-only inspection of the pinned WSL output directory
`out/peerchat-neteq-arm64-api26-ndk27` found the saved generated target file
`obj/peerchat_audio_receive/peerchat_audio_receive.ninja` and archive
`obj/peerchat_audio_receive/libpeerchat_audio_receive.a`. The archive has 999
object members. The adjacent `sync-revisions.json` records the exact WebRTC
revision above as successfully synced. The generated archive rule includes
object paths under Abseil, Opus, cpu_features, Perfetto, protobuf/utf8_range,
Ooura, and spl_sqrt_floor; these are compiled into the complete archive, not
merely present in the source checkout. The same checkout has source-only
components and host code that are not proof of inclusion. The broad
`receive-dependencies.txt` report includes GN graph and host-tool entries, so
it is not by itself an archive membership list.

The local Android app's original notice-related packaging rule is in
`android/app/build.gradle.kts`: it retains one duplicate `META-INF/LICENSE.md`
resource from Java dependencies with `resources.pickFirsts`. It does not copy
WebRTC source-tree license files or create a native third-party notices bundle.
This alone was not evidence that WebRTC notices appeared in the APK. The
dev.181 voice build now additionally verifies the generated notice hash and
includes the bundle and its manifest as APK assets; production archive inspection
is recorded in the dev.181 validation report.

`scripts/export-android-voice-notices.sh` now exports the app-ready native
notice bundle under the ignored `build/android-neteq/notices` directory. It
checks the successful sync manifest against the WebRTC pin, reads the generated
archive action's object inputs, matches compiled third-party directory names
against the pinned `generate_licenses.py` mapping, and stops if an included
component has no mapping or a mapped file is missing. It adds the WebRTC root
`LICENSE`, `PATENTS`, and `AUTHORS`; the nested Opus patent/IPR file and
protobuf `utf8_range` license are included only when their object paths appear.
The NDK `sysroot/NOTICE` is added because the generated target names its NDK 27
`libunwind.a` input. The reduced NDK target directory contains no separate
libc++, unwind, or compiler-rt license files; its bundled sysroot `NOTICE`
contains the relevant aggregate notices, including LLVM-exception SPDX text.

The exporter writes `PeerChatVoiceThirdPartyNotices.txt` and
`PeerChatVoiceThirdPartyNotices.manifest`. The manifest records source hashes,
archive and generated-rule hashes, the WebRTC revision, and object count. The
current bundle contains 13 source notices for 999 archive object inputs; its
SHA-256 is `d02f083a5ce0e2375cee4f6786a3035f70ef5a10d000d46c37a97859b422b131`.

## Source files to retain

The following exact files were found in the pinned checkout and correspond to
components confirmed in the generated archive rule:

- `src/LICENSE` — WebRTC project license.
- `src/third_party/abseil-cpp/LICENSE` — Abseil (Apache-2.0).
- `src/third_party/opus/src/COPYING` and
  `src/third_party/opus/src/LICENSE_PLEASE_READ.txt` — vendored Opus license
  and accompanying patent/IPR notice text.
- `src/common_audio/third_party/ooura/LICENSE` — Ooura FFT permissive license
  text.
- `src/common_audio/third_party/spl_sqrt_floor/LICENSE` — spl_sqrt_floor
  public-domain license text.
- `src/third_party/cpu_features/src/LICENSE` — cpu_features BSD-2-Clause and
  Apache-2.0 license texts.
- `src/third_party/perfetto/LICENSE` — Perfetto Apache-2.0 license.
- `src/third_party/protobuf/LICENSE` — protobuf BSD license.
- `src/third_party/protobuf/third_party/utf8_range/LICENSE` — utf8_range
  MIT license.
- `deps/android-ndk27-target/sysroot/NOTICE` — NDK 27 sysroot notice file;
  it includes LLVM-exception SPDX material. The generated adapter target names
  the matching NDK `libunwind.a` as a system link input. Preserve this NDK
  notice with the eventual application distribution that links that runtime.

The source revision has no top-level `LICENSE_THIRD_PARTY` file. The first
inventory draft incorrectly listed one; this was corrected after direct source
inspection. `tools_webrtc/libs/generate_licenses.py` is a source-side notice
collection script, not itself a notice to bundle.

The listed component names and license categories are inventory labels only;
this document does not assess legal sufficiency. Because the complete archive
contains 999 objects, the final package review should compare all source object
paths in the generated archive rule with the pinned upstream license mapping
and include any additional mapped component whose code is present. Do not add
licenses merely because unrelated sources or GN host tools exist in the
checkout.

## Remaining audit gaps

- The exporter records the exact source file set and archive-rule hash, but it
  does not persist a separate list of all 999 object paths or a per-object
  mapping. The mapping is recomputed from the pinned generated archive rule
  each time it runs.
- Existing Java `pickFirsts` behavior is separate from the native notice bundle;
  future archive changes require regenerating/reviewing the closure and updating
  its pinned hash, not merely reusing this bundle.

This document records evidence and gaps; it makes no legal sufficiency claim.
