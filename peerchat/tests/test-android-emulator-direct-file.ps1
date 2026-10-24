param(
 [string]$Serial='emulator-5582',
 [string]$Adb='C:\Users\Livel\AppData\Local\Android\Sdk\platform-tools\adb.exe',
 [string]$Package='com.peerchat.android.voiceintegration',
 [string]$FixtureRoot=(Join-Path ([IO.Path]::GetTempPath()) 'PeerChat-direct-tests'),
 [string]$PrebuiltBinary
)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
if($Serial -notmatch '^emulator-[0-9]+$' -or $Package -notmatch '^com\.peerchat\.android\.(voice|background)integration$'){throw 'Only isolated emulator packages are eligible'}
$evidence=Join-Path $FixtureRoot ([guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $evidence | Out-Null
Push-Location $root
try {
 $binary=if($PrebuiltBinary){(Resolve-Path -LiteralPath $PrebuiltBinary).Path}else{Join-Path $evidence 'direct-emulator.exe'}
 if(-not $PrebuiltBinary){
  & 'C:/TDM-GCC-64/bin/g++.exe' -std=c++17 -O1 -Wall -Wextra -Werror -static-libgcc -static-libstdc++ -I. tests/android_emulator_direct_file_test.cpp tls.cpp storage.cpp group.cpp -o $binary -lbcrypt -lcrypt32 -lncrypt -lsecur32 -lws2_32
  if($LASTEXITCODE){throw 'Direct emulator driver build failed'}
 }
 $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port;$listener.Stop()
 $stdout=Join-Path $evidence 'windows.stdout.log';$stderr=Join-Path $evidence 'windows.stderr.log'
 $driver=Start-Process -FilePath $binary -ArgumentList @(('"'+(Join-Path $evidence 'windows')+'"'),$port) -WindowStyle Hidden -PassThru -RedirectStandardOutput $stdout -RedirectStandardError $stderr
 try {
  for($n=0;$n -lt 100;$n++){
   if($driver.HasExited){throw "Windows driver stopped; see $stderr"}
   if((Get-Content -LiteralPath $stdout -Raw -ErrorAction SilentlyContinue) -match "LISTENING $port"){break}
   Start-Sleep -Milliseconds 100
  }
  if($n -eq 100){throw 'Windows listener timeout'}
  & $Adb -s $Serial reverse "tcp:$port" "tcp:$port"
  if($LASTEXITCODE){throw 'ADB reverse failed'}
  $instrument=(& $Adb -s $Serial shell am instrument -w -r -e bridgePort $port -e class com.peerchat.android.NativeWindowsDirectFileTest "$Package.test/androidx.test.runner.AndroidJUnitRunner" 2>&1 | Out-String)
  $instrument | Set-Content -LiteralPath (Join-Path $evidence 'instrumentation.log') -Encoding utf8
  $driver.WaitForExit(10000)|Out-Null
  if($instrument -notmatch 'OK \(1 test\)' -or $instrument -match 'FAILURES|INSTRUMENTATION_FAILED' -or -not $driver.HasExited -or $driver.ExitCode -ne 0){throw "Direct emulator interoperability failed; evidence $evidence"}
  Write-Output "PASS real Android emulator ↔ Windows direct file bytes/ACKs over opaque ADB bridge; evidence $evidence"
 }finally{& $Adb -s $Serial reverse --remove "tcp:$port" | Out-Null;if(-not $driver.HasExited){Stop-Process -Id $driver.Id -Force}}
}finally{Pop-Location}
