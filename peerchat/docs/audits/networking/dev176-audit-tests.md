# Audit characterization tests and evidence

These tests assert current defective behavior, not repaired behavior. Convert defect assertions to recovery expectations only with the corresponding approved fix; preserve stale-scope and rejection checks.

## Android tests

From `peerchat/android`, using the installed JDK 22 and Android SDK:

```powershell
$env:JAVA_HOME='C:/Program Files/Java/jdk-22'
$env:ANDROID_HOME='C:/Users/Livel/AppData/Local/Android/Sdk'
./gradlew.bat :app:testDebugUnitTest --tests com.peerchat.android.application.NetworkingReadinessAuditTest --tests com.peerchat.android.application.BackendLeasePolicyTest --tests com.peerchat.android.application.ScopedBackendWorkTest --offline --console=plain
```

Robolectric SDK 34; no real server or phone. The new tests use actual backend socket scope and actual mailbox executor (fake native registration boundary). Existing tests cover lease policy and exact scoped-work fencing. Result XML remains under `android/app/build/test-results/testDebugUnitTest`.

```text
NetworkingReadinessAuditTest: tests=2 failures=0 errors=0 skipped=0
BackendLeasePolicyTest: tests=3 failures=0 errors=0 skipped=0
ScopedBackendWorkTest: tests=5 failures=0 errors=0 skipped=0
BUILD SUCCESSFUL in 18s
```

## Native cache enrollment characterization

From `peerchat`, prerequisite: retained `build/tests/dev176-jni-cache/jni` fixture containing NativeDmAttachmentSide, ReliableProfileProof, supporting classes and host DLL from existing production JNI/cache integration. This is fixture-dependent, not a standalone native build script.

```powershell
$jni=(Resolve-Path build/tests/dev176-jni-cache/jni).Path
$out='build/tests/dev176-independent-audit'
New-Item -ItemType Directory -Force -Path $out | Out-Null
$deps=@(Get-ChildItem build/deps/bouncycastle-1.86.1-jdk18on -Filter '*.jar' | Where-Object Name -notmatch 'sources|javadoc' | ForEach-Object FullName)
$cp=(@($out,$jni)+$deps)-join ';'
javac -encoding UTF-8 -cp $cp -d $out tests/tls-host/CacheEnrollmentGenerationAudit.java
if($LASTEXITCODE -ne 0){throw 'Audit fixture compilation failed'}
$fixture=Join-Path ([IO.Path]::GetTempPath()) ('pc-cache-audit-'+[guid]::NewGuid().ToString('N'))
java -Xcheck:jni "-Djava.library.path=$jni" -cp $cp com.peerchat.android.platform.tls.CacheEnrollmentGenerationAudit $fixture
if($LASTEXITCODE -ne 0){throw 'Audit fixture failed'}
```

Use only a fresh private fixture path, never a real profile. Replies are synthetic; enrollment reset, controller validation and diagnostic generation use unchanged production JNI. The reused native fixture source was checked against current production source after its established host attachment-I/O include substitution. Arbitrary future DLLs do not establish the same provenance.

Sanitized output:

```text
REPRODUCED startup: capability before accepted mailbox binding rejected
PASS initial accepted local binding: 212-byte capability reply applied
REPRODUCED unchanged account/device + new authentication generation: native_transfer_request_failure operation=13 stage=cache_reply_validation_apply reason=native_operation_rejected input_bytes=212 elapsed_ms=1 dm_cache_storage=available
PASS control: existing binding reconciliation restores capability without new keys or relaxed validation
REPRODUCED second generation reset; HOST JNI/CNG/SQLite fixture, no phone/server access
```

Original logs: `build/tests/dev176-independent-audit/cache-enrollment.log` and `android-readiness.log`. No binaries, real diagnostic exports, keys or credentials are committed. First Java compilation needed test-only exception declarations; final compilation and execution passed. No production fix or physical improvement is claimed.
