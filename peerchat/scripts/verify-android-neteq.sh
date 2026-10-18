#!/usr/bin/env bash
# Independent relocation/link proof: no GN output libraries/search directories.
set -euo pipefail
repo=$(cd "$(dirname "$0")/.." && pwd)
deps=${1:-/home/peerchatbuild/deps}
src="$deps/webrtc-6cf6bcb-android/src"
out="$src/out/peerchat-neteq-arm64-api26-ndk27"
ndk="$deps/android-ndk27-target"
llvm="$src/third_party/llvm-build/Release+Asserts/bin"
stage=$(mktemp -d "$deps/neteq-relocation.XXXXXX")
cp "$out/obj/peerchat_audio_receive/libpeerchat_audio_receive.a" "$stage/"
cp "$repo/audio_receive/peerchat_audio_receive.h" "$repo/audio_receive/audio_receive_test.cpp" "$stage/"
cp -a "$out/gen/peerchat_audio_receive/include" "$stage/"
archive="$stage/libpeerchat_audio_receive.a"
test "$(head -c 7 "$archive")" = '!<arch>'
"$llvm/llvm-readelf" -h "$archive" > "$stage/archive-elf.txt"
if grep 'Machine:' "$stage/archive-elf.txt" | grep -v 'AArch64'; then exit 1; fi
if grep -q '\.rlib' "$out/obj/peerchat_audio_receive/peerchat_audio_receive.ninja"; then
  echo 'Unpackaged Rust dependencies remain' >&2; exit 1
fi
cd "$stage"
for mode in normal whole; do
  extra=()
  if test "$mode" = whole; then extra=(-Wl,--whole-archive); fi
  "$llvm/clang++" --target=aarch64-linux-android26 --sysroot="$ndk/sysroot" \
    -std=c++20 -O2 -fPIC -fexceptions -fno-rtti -static-libstdc++ --unwindlib=none \
    -fuse-ld=lld -I. -Iinclude audio_receive_test.cpp "${extra[@]}" "$archive" \
    -Wl,--no-whole-archive "$ndk/libunwind.a" -llog -ldl -lm \
    -Wl,-z,defs,-z,max-page-size=16384,-z,relro,-z,now \
    "-Wl,-Map=$mode.map" -o "receive-test-$mode"
  "$llvm/llvm-readelf" -h -l -d "receive-test-$mode" > "$mode-elf.txt"
  sha256sum "receive-test-$mode"
done
sha256sum "$archive"
echo "PASS relocation and normal/whole-archive static-runtime links: $stage"
echo 'Compile/link proof only. Android execution and physical calls remain unverified.'
