param(
 [string]$Serial='emulator-5580',
 [string]$Adb='C:\Users\Livel\AppData\Local\Android\Sdk\platform-tools\adb.exe',
 [string]$FixtureRoot='F:\PeerChat-isolated-cache-tests',
 [string]$PrebuiltBinary,
 [string]$ManagedHost
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if($Serial -notmatch '^emulator-[0-9]+$'){throw 'Only an isolated emulator is eligible'}
$packages=(& $Adb -s $Serial shell pm list packages com.peerchat.android.voiceintegration | Out-String)
if($LASTEXITCODE -ne 0 -or $packages -notmatch 'package:com\.peerchat\.android\.voiceintegration'){throw 'Install the isolated x86_64 test app first; never target the production app'}
$evidence=Join-Path $FixtureRoot ('dm-emulator-'+[Guid]::NewGuid().ToString('N'))
$profiles=Join-Path $evidence 'profiles'
New-Item -ItemType Directory -Force -Path $evidence,$profiles | Out-Null
Push-Location $root
try {
 $managed=if($ManagedHost){(Resolve-Path -LiteralPath $ManagedHost).Path}else{(Resolve-Path 'tests/portable/bin/Release/net10.0/PeerChat.DirectAttachmentApplicationHost.dll').Path}
 $binary=if($PrebuiltBinary){(Resolve-Path -LiteralPath $PrebuiltBinary).Path}else{Join-Path $evidence 'dm-emulator.exe'}
 if(-not $PrebuiltBinary){
  & scripts/build-go-hpke.ps1 -OutputDirectory $evidence
  if($LASTEXITCODE){throw 'HPKE native prerequisite failed'}
  & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O1 -Wall -Wextra -Werror -Wno-cast-function-type -Wno-misleading-indentation -static-libgcc -static-libstdc++ -I. tests/android_emulator_dm_attachment_test.cpp platform/go_hpke_provider.cpp group.cpp storage.cpp group_message_envelope.cpp tls.cpp -o $binary -lbcrypt -lcrypt32 -lncrypt -lws2_32 -lsecur32
  if($LASTEXITCODE){throw 'Emulator/Windows native fixture build failed'}
 }
 if(-not(Test-Path -LiteralPath $binary)){throw 'Missing fixture executable'}
 $portOwner=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
 $portOwner.Start();$port=$portOwner.LocalEndpoint.Port;$portOwner.Stop()
 $output=Join-Path $evidence 'driver.stdout.log';$errors=Join-Path $evidence 'driver.stderr.log'
 $driver=Start-Process -FilePath $binary -ArgumentList @("`"$profiles`"","`"$managed`"",$port) -WindowStyle Hidden -PassThru -RedirectStandardOutput $output -RedirectStandardError $errors
 try {
  for($n=0;$n -lt 100;$n++){
   if($driver.HasExited){throw "Fixture driver failed before listening; see $errors"}
   if((Get-Content -LiteralPath $output -Raw -ErrorAction SilentlyContinue) -match "LISTENING $port"){break}
   Start-Sleep -Milliseconds 100
  }
  if($n -ge 100){throw "Fixture driver listener timeout; see $errors"}
  & $Adb -s $Serial reverse "tcp:$port" "tcp:$port"
  if($LASTEXITCODE){throw 'ADB reverse failed'}
  $instrument=(& $Adb -s $Serial shell am instrument -w -r -e bridgePort $port -e class com.peerchat.android.NativeWindowsDmInteropTest com.peerchat.android.voiceintegration.test/androidx.test.runner.AndroidJUnitRunner 2>&1 | Out-String)
  $instrument | Set-Content -LiteralPath (Join-Path $evidence 'instrumentation.log') -Encoding utf8
  $driver.WaitForExit(10000) | Out-Null
  if($instrument -notmatch 'OK \(1 test\)' -or $instrument -match 'FAILURES|INSTRUMENTATION_FAILED' -or -not $driver.HasExited -or $driver.ExitCode -ne 0){throw "Android/Windows interoperability fixture failed; retained evidence $evidence"}
  Write-Output "PASS isolated Android native ↔ Windows native bidirectional cached DM payload/ACK/receipt fixture (server RPC proxied by Windows test host); evidence $evidence"
 } finally {
  & $Adb -s $Serial reverse --remove "tcp:$port" | Out-Null
  if(-not $driver.HasExited){Stop-Process -Id $driver.Id -Force}
 }
} finally {Pop-Location}
