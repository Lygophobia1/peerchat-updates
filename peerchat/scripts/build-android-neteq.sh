#!/usr/bin/env bash
# Run as the non-root Linux build user after the pinned gclient sync/hooks.
# Produces dependency proof only; Android application/runtime gates still apply.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
deps=${1:-/home/peerchatbuild/deps}
ndk=${2:-$deps/android-ndk27-target}
src="$deps/webrtc-6cf6bcb-android/src"
pin=6cf6bcb585b31414aac02377df2bc8fec770d0e8
export DEPOT_TOOLS_UPDATE=0
export PATH="$deps/depot_tools:$PATH"
test "$(uname -s)" = Linux
test "$(id -u)" != 0
test "$(git -C "$src" rev-parse HEAD)" = "$pin"
ndk=$(realpath "$ndk")
# Generated GN string literals must not accept injected quoting or newlines.
[[ "$ndk" =~ ^/[a-zA-Z0-9_./-]+$ ]]
grep -Fxq 'Pkg.Revision = 27.0.12077973' "$ndk/source.properties"
test -f "$ndk/sysroot/usr/include/android/ndk-version.h"
test -f "$ndk/libunwind.a"
patch="$repo/audio_receive/webrtc-ndk-iterators.patch"
if git -C "$src" apply --check "$patch" 2>/dev/null; then
  git -C "$src" apply "$patch"
else
  # Already applied is allowed; a mismatching dependency is not overwritten.
  git -C "$src" apply --reverse --check "$patch"
fi
if ! test -f "$deps/depot_tools/python3_bin_reldir.txt"; then
  # gclient's vpython initialization alone does not initialize GN/autoninja.
  "$deps/depot_tools/ensure_bootstrap"
fi
target="$src/peerchat_audio_receive"
out="$src/out/peerchat-neteq-arm64-api26-ndk27"
mkdir -p "$target" "$out"
cp "$repo/audio_receive/peerchat_audio_receive.cpp" "$repo/audio_receive/peerchat_audio_receive.h" "$repo/audio_receive/audio_receive_test.cpp" "$target/"
cp "$repo/audio_receive/BUILD.android.gn" "$target/BUILD.gn"
args=$(mktemp)
trap 'rm -f -- "$args"' EXIT
cp "$repo/scripts/android-neteq.args.gn" "$args"
printf '\ntarget_sysroot = "%s/sysroot"\npeerchat_ndk_unwind_library = "%s/libunwind.a"\n' "$ndk" "$ndk" >> "$args"
if test -f "$out/args.gn"; then cmp "$args" "$out/args.gn"; else cp "$args" "$out/args.gn"; fi
cd "$src"
# GN supports an alternate root target; no edit to WebRTC's root BUILD.gn.
gn gen "$out" --root-target=//peerchat_audio_receive
gn desc "$out" //peerchat_audio_receive:peerchat_audio_receive deps --all --root-target=//peerchat_audio_receive > "$out/receive-dependencies.txt"
gn desc "$out" //peerchat_audio_receive:peerchat_audio_receive libs --blame --root-target=//peerchat_audio_receive > "$out/receive-system-libs.txt"
gn desc "$out" //peerchat_audio_receive:peerchat_audio_receive_test libs --blame --root-target=//peerchat_audio_receive > "$out/test-system-libs.txt"
gn desc "$out" //peerchat_audio_receive:peerchat_audio_receive_test ldflags --blame --root-target=//peerchat_audio_receive > "$out/test-link-flags.txt"
autoninja -C "$out" -j 6 peerchat_audio_receive peerchat_audio_receive_test
gn desc "$out" //peerchat_audio_receive:peerchat_audio_receive outputs --root-target=//peerchat_audio_receive
git rev-parse HEAD
sha256sum "$out/args.gn" "$target/BUILD.gn" "$target/peerchat_audio_receive.cpp" "$target/peerchat_audio_receive.h" "$target/audio_receive_test.cpp"
find "$out/obj/peerchat_audio_receive" -maxdepth 1 -name '*.a' -exec sha256sum {} +
sha256sum "$out/peerchat_audio_receive_test"
"$src/third_party/llvm-build/Release+Asserts/bin/clang" --version
cat "$ndk/source.properties"
cat "$ndk/sysroot/usr/include/android/ndk-version.h"
sha256sum "$ndk/libunwind.a" "$patch"
echo 'Archive built. ABI, relocation, final application linkage and real Android execution remain mandatory.'
