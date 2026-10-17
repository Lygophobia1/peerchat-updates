param([string]$JavaHome='C:\Program Files\Java\jdk-22',[string]$FixtureRoot,[string]$RuntimeRoot,[switch]$SkipBuild,[switch]$ReuseJni)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if(-not $FixtureRoot){$FixtureRoot=Join-Path $root ('build/tests/android-jni-dm-'+[guid]::NewGuid().ToString('N'))}
$evidence=[IO.Path]::GetFullPath($FixtureRoot)
$jni=Join-Path $evidence 'jni'
New-Item -ItemType Directory -Force -Path $evidence|Out-Null
Push-Location $root
try {
 $stock=Join-Path $root 'build/deps/bouncycastle-1.86.1-jdk18on'
 $hook=& scripts/build-bc-hpke-hook.ps1 -JavaHome $JavaHome
 $classpath=(@($jni,$hook)+@(Get-ChildItem -LiteralPath $stock -Filter '*.jar'|Where-Object{$_.Name -notmatch '-sources|-javadoc'}|Sort-Object Name|ForEach-Object{$_.FullName})) -join ';'
 if(-not $SkipBuild){
  if(-not $ReuseJni){& tests/test-reliable-profile-jni.ps1 -JavaHome $JavaHome -TransferFiles -PrepareOnly -FixtureDirectory $jni
   if($LASTEXITCODE){throw 'Production JNI host preparation failed'}}
  & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O2 -Wall -Wextra -Werror -shared -static-libgcc -static-libstdc++ "-I$JavaHome/include" "-I$JavaHome/include/win32" tests/recipient_key_jni_host.cpp -o "$jni/recipient-key-proof.dll" -lncrypt
  if($LASTEXITCODE){throw 'Opaque test-only CNG recipient bridge build failed'}
  & "$JavaHome/bin/javac.exe" -encoding UTF-8 -cp $classpath -d $jni tests/jni-host/com/peerchat/android/MailboxBridge.java tests/tls-host/NativeCngHpkeFixture.java android/app/src/main/java/com/peerchat/android/platform/RecipientPublicKey.java android/app/src/main/java/com/peerchat/android/platform/OpaqueRecipientHpke.java tests/tls-host/NativeDmAttachmentSide.java tests/tls-host/DirectFileCompatibilityProof.java
  if($LASTEXITCODE){throw 'Production JNI cache helper build failed'}
  & dotnet build tests/portable/PeerChat.DirectAttachmentApplicationHost.csproj -c Release --nologo -v quiet -o "$evidence/managed"
  if($LASTEXITCODE){throw 'Managed authenticated TLS cache host build failed'}
  & scripts/build-go-hpke.ps1 -OutputDirectory $evidence
  & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O1 -Wall -Wextra -Werror -Wno-cast-function-type -Wno-misleading-indentation -static-libgcc -static-libstdc++ -I. tests/android_jni_dm_attachment_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp group_message_envelope.cpp -o "$evidence/jni-dm.exe" -lbcrypt -lcrypt32 -lncrypt -lws2_32
  if($LASTEXITCODE){throw 'Windows to production JNI cache fixture build failed'}
 }
 # Native Windows SQLite/cache ownership paths also include volume GUIDs and
 # immutable object names. Keep isolated runtime paths below legacy MAX_PATH.
 if(-not $RuntimeRoot){$RuntimeRoot=if(Test-Path -LiteralPath 'F:\PeerChat-isolated-cache-tests'){'F:\PeerChat-isolated-cache-tests'}else{[IO.Path]::GetTempPath()}}
 $profiles=Join-Path $RuntimeRoot ('pc-dm-jni-'+[guid]::NewGuid().ToString('N'))
 Write-Output "Retained isolated runtime profiles: $profiles"
 $resultLog=Join-Path $evidence ('results-'+[guid]::NewGuid().ToString('N')+'.log')
 & "$evidence/jni-dm.exe" $profiles (Resolve-Path "$evidence/managed/PeerChat.DirectAttachmentApplicationHost.dll").Path "$JavaHome/bin/java.exe" $classpath $jni 2>&1 | Tee-Object -FilePath $resultLog
 if($LASTEXITCODE){throw "Production JNI cache loopback failed; evidence $evidence"}
 & "$JavaHome/bin/java.exe" -Xcheck:jni "-Djava.library.path=$jni" -cp $classpath com.peerchat.android.platform.tls.DirectFileCompatibilityProof "$profiles/direct-compatibility" 2>&1 | Tee-Object -FilePath "$evidence/direct-compatibility.log"
 if($LASTEXITCODE){throw 'Explicit online direct compatibility JNI proof failed'}
 Write-Output "Evidence: $evidence. No phone access/install/deployment; Windows host platform adapters, not Android POSIX or Keystore."
}finally{Pop-Location}
