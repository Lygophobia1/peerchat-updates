#!/usr/bin/env bash
# Export notices for the pinned Android NetEq archive into an ignored assets
# directory. Run as peerchatbuild in the WSL checkout after the archive build.
set -euo pipefail

repo=$(cd "$(dirname "$0")/.." && pwd)
deps=${1:-/home/peerchatbuild/deps}
out=${2:-$deps/webrtc-6cf6bcb-android/src/out/peerchat-neteq-arm64-api26-ndk27}
destination=${3:-$repo/build/android-neteq/notices}
src=$deps/webrtc-6cf6bcb-android/src
sync_manifest=$deps/webrtc-6cf6bcb-android/sync-revisions.json
ndk=$deps/android-ndk27-target

python3 - "$src" "$sync_manifest" "$out" "$ndk" "$destination" <<'PY'
import ast
import hashlib
import json
import os
from pathlib import Path
import re
import sys
import tempfile

src, sync_manifest, out, ndk, destination = map(Path, sys.argv[1:])
pin = "6cf6bcb585b31414aac02377df2bc8fec770d0e8"
target_rule = out / "obj/peerchat_audio_receive/peerchat_audio_receive.ninja"
archive = out / "obj/peerchat_audio_receive/libpeerchat_audio_receive.a"
generator = src / "tools_webrtc/libs/generate_licenses.py"

def fail(message):
    raise SystemExit(f"ERROR: {message}")

if not sync_manifest.is_file():
    fail(f"missing gclient sync manifest: {sync_manifest}")
try:
    synced = json.loads(sync_manifest.read_text(encoding="utf-8"))["solutions"]["src/"]
except (OSError, ValueError, KeyError, TypeError) as error:
    fail(f"cannot read src revision from sync manifest: {error}")
if synced.get("revision") != pin or not synced.get("was_synced"):
    fail(f"sync manifest does not confirm pinned WebRTC revision {pin}")

if not target_rule.is_file() or not archive.is_file():
    fail("expected generated target rule and completed archive are required")
if not generator.is_file():
    fail(f"missing pinned upstream license mapping: {generator}")

rule_text = target_rule.read_text(encoding="utf-8")
rule_lines = [
    line for line in rule_text.splitlines()
    if line.startswith("build ")
    and "obj/peerchat_audio_receive/libpeerchat_audio_receive.a:" in line
]
if len(rule_lines) != 1:
    fail("could not identify a unique archive action in the generated GN rule")
archive_inputs = rule_lines[0].split("|", 1)[0]
object_paths = sorted(set(re.findall(r"obj/[^\s]+?\.o", archive_inputs)))
if len(object_paths) != 999:
    fail(f"expected 999 object inputs in the pinned archive rule; found {len(object_paths)}")
if "android-ndk27-target/libunwind.a" not in rule_text:
    fail("generated target no longer declares the NDK 27 libunwind input")

try:
    tree = ast.parse(generator.read_text(encoding="utf-8"), filename=str(generator))
except (OSError, SyntaxError) as error:
    fail(f"cannot parse upstream generator mapping: {error}")
mapping_node = next(
    (node.value for node in tree.body
     if isinstance(node, ast.Assign)
     and any(isinstance(name, ast.Name) and name.id == "LIB_TO_LICENSES_DICT"
             for name in node.targets)),
    None,
)
if mapping_node is None:
    fail("upstream generator has no LIB_TO_LICENSES_DICT assignment")
try:
    license_mapping = ast.literal_eval(mapping_node)
except (ValueError, TypeError) as error:
    fail(f"cannot evaluate upstream license mapping: {error}")

# Object paths below `obj/.../third_party/<name>/` map to the component names
# used by the upstream WebRTC generator. Components without object members are
# omitted, even if their sources are checked out or appear in GN host tools.
compiled_components = set()
for object_path in object_paths:
    parts = object_path.split("/")
    if "third_party" in parts:
        index = parts.index("third_party")
        if index + 1 < len(parts):
            compiled_components.add(parts[index + 1])

unmapped = sorted(name for name in compiled_components
                  if name not in license_mapping)
if unmapped:
    fail("compiled third-party component(s) lack upstream mapping: "
         + ", ".join(unmapped))

source_files = [
    ("WebRTC project", "LICENSE"),
    ("WebRTC IPR", "PATENTS"),
    ("WebRTC contributors", "AUTHORS"),
]
for name in sorted(compiled_components):
    for relative_path in license_mapping[name]:
        if relative_path:
            source_files.append((name, relative_path))

# These upstream notices are nested dependencies, not separate generator keys.
# Include each only when its own object path is part of the archive rule.
nested_notices = [
    ("Opus patent/IPR notice", "third_party/opus/src/LICENSE_PLEASE_READ.txt",
     "obj/third_party/opus/"),
    ("utf8_range", "third_party/protobuf/third_party/utf8_range/LICENSE",
     "obj/third_party/protobuf/utf8_range/"),
]
for label, relative_path, object_prefix in nested_notices:
    if any(path.startswith(object_prefix) for path in object_paths):
        source_files.append((label, relative_path))

# GN names this NDK 27 runtime as a link input. Its packaged sysroot notice is
# the notice file available in this deliberately reduced NDK target directory.
source_files.append(("Android NDK 27 sysroot", "sysroot/NOTICE"))

deduplicated = []
seen = set()
for label, relative_path in source_files:
    if relative_path not in seen:
        seen.add(relative_path)
        deduplicated.append((label, relative_path))

for label, relative_path in deduplicated:
    base = ndk if relative_path == "sysroot/NOTICE" else src
    source = base / relative_path
    if not source.is_file():
        fail(f"missing mapped notice for {label}: {source}")

def sha256(data):
    return hashlib.sha256(data).hexdigest()

bundle_parts = [
    "PeerChat Android voice dependency notices\n"
    f"WebRTC source revision: {pin}\n"
    f"Archive object members: {len(object_paths)}\n"
    "The sections below reproduce the identified source files.\n"
]
manifest_entries = []
for label, relative_path in deduplicated:
    base = ndk if relative_path == "sysroot/NOTICE" else src
    source = base / relative_path
    contents = source.read_bytes()
    try:
        text = contents.decode("utf-8")
    except UnicodeDecodeError as error:
        fail(f"notice is not UTF-8 ({relative_path}): {error}")
    section = f"\n===== BEGIN {relative_path} ({label}) =====\n" + text
    if not text.endswith("\n"):
        section += "\n"
    bundle_parts.append(section + f"===== END {relative_path} =====\n")
    manifest_entries.append((relative_path, sha256(contents)))

bundle = "".join(bundle_parts).encode("utf-8")
bundle_name = "PeerChatVoiceThirdPartyNotices.txt"
manifest_name = "PeerChatVoiceThirdPartyNotices.manifest"
manifest_lines = [
    f"WebRTC revision: {pin}",
    f"Archive: {archive.relative_to(out)}",
    f"Archive SHA-256: {sha256(archive.read_bytes())}",
    f"Archive rule SHA-256: {sha256(target_rule.read_bytes())}",
    f"Archive object inputs: {len(object_paths)}",
    f"Bundle SHA-256: {sha256(bundle)}  {bundle_name}",
    "Source file SHA-256 values:",
]
manifest_lines.extend(
    f"{digest}  {relative_path}" for relative_path, digest in manifest_entries
)
manifest = ("\n".join(manifest_lines) + "\n").encode("utf-8")

destination.mkdir(parents=True, exist_ok=True)
outputs = {
    destination / bundle_name: bundle,
    destination / manifest_name: manifest,
}
temporary_paths = []
try:
    for output, contents in outputs.items():
        with tempfile.NamedTemporaryFile(
                mode="wb", dir=destination, prefix=".voice-notices-",
                delete=False) as temporary:
            temporary.write(contents)
            temporary_paths.append((Path(temporary.name), output))
    for temporary, output in temporary_paths:
        os.replace(temporary, output)
except Exception:
    for temporary, _ in temporary_paths:
        try:
            temporary.unlink()
        except FileNotFoundError:
            pass
    raise

print(f"Exported {len(deduplicated)} source notices from {len(object_paths)} archive objects")
print(f"Bundle: {destination / bundle_name}")
print(f"Manifest: {destination / manifest_name}")
print(f"Bundle SHA-256: {sha256(bundle)}")
PY
