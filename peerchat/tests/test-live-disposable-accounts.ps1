param(
 [Parameter(Mandatory)][PSCredential]$FirstCredential,
 [Parameter(Mandatory)][PSCredential]$SecondCredential,
 [Parameter(Mandatory)][string]$ServerHost,
 [int]$ServerPort=7443,
 [Parameter(Mandatory)][string]$ServerPin,
 [string]$GroupId,
 [switch]$InspectRetained,
 [switch]$Media,
 [string]$Serial='emulator-5580',
 [string]$Adb='C:\Users\Livel\AppData\Local\Android\Sdk\platform-tools\adb.exe'
)
$ErrorActionPreference='Stop'
if($Serial -notmatch '^emulator-[0-9]+$' -or $FirstCredential.UserName -cne 'codex' -or $SecondCredential.UserName -cne 'codex1'){throw 'Only the explicitly authorized disposable accounts and emulator are eligible'}
if($ServerPin -notmatch '^[0-9a-fA-F]{64}$'){throw 'Existing trusted server pin required'}
$root=Split-Path $PSScriptRoot -Parent
$evidence=Join-Path $root ('build\tests\live-disposable-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidence | Out-Null
$packages=(& $Adb -s $Serial shell pm list packages com.peerchat.android.voiceintegration | Out-String)
if($LASTEXITCODE -ne 0 -or $packages -notmatch 'package:com\.peerchat\.android\.voiceintegration'){throw 'Isolated emulator application required'}
$listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0)
$listener.Start();$port=$listener.LocalEndpoint.Port
$instrument=$null
try {
 & $Adb -s $Serial reverse "tcp:$port" "tcp:$port" | Out-Null
 if($LASTEXITCODE){throw 'ADB loopback mapping failed'}
 $testClass=if($GroupId){if($GroupId -notmatch '^[0-9a-f]{32}$'){throw 'Invalid test group identifier'};'com.peerchat.android.LiveWindowsRoomJoinTest'}else{'com.peerchat.android.LiveDisposableAccountsTest'}
 $instrument=Start-Process -FilePath $Adb -ArgumentList @('-s',$Serial,'shell','am','instrument','-w','-r','-e','disposableProbePort',$port,'-e','class',$testClass,'com.peerchat.android.voiceintegration.test/androidx.test.runner.AndroidJUnitRunner') -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $evidence 'instrumentation.log') -RedirectStandardError (Join-Path $evidence 'stderr.log')
 $accept=$listener.AcceptTcpClientAsync()
 if(-not $accept.Wait(30000)){throw 'Emulator secret-channel setup timed out'}
 $client=$accept.Result
 try {
  # In-memory credentials only; never instrumentation arguments, files or output.
  $payload=[Text.Encoding]::UTF8.GetBytes((@{host=$ServerHost;port=$ServerPort;pin=$ServerPin.ToLowerInvariant();groupId=$GroupId;inspect=[bool]$InspectRetained;media=[bool]$Media;first=$FirstCredential.UserName;firstPassword=$FirstCredential.GetNetworkCredential().Password;second=$SecondCredential.UserName;secondPassword=$SecondCredential.GetNetworkCredential().Password}|ConvertTo-Json -Compress))
  $stream=$client.GetStream();$size=[BitConverter]::GetBytes([Net.IPAddress]::HostToNetworkOrder([int]$payload.Length))
  $stream.Write($size,0,4);$stream.Write($payload,0,$payload.Length);$stream.Flush()
 } finally {if($payload){[Array]::Clear($payload,0,$payload.Length)};$client.Dispose()}
 if(-not $instrument.WaitForExit(720000)){
  # Stop only this isolated emulator app; preserve its profiles and history.
  & $Adb -s $Serial shell am force-stop com.peerchat.android.voiceintegration | Out-Null
  throw "Bounded live test expired; retained evidence $evidence"
 }
 $result=Get-Content -LiteralPath (Join-Path $evidence 'instrumentation.log') -Raw
 $result
 if($instrument.ExitCode -ne 0 -or $result -notmatch 'OK \(1 test\)' -or $result -match 'FAILURES|INSTRUMENTATION_FAILED'){throw "Live disposable-account test failed; evidence $evidence"}
 Write-Output "PASS live Android application-owner fixture; evidence $evidence. Not Windows interoperability or physical audio proof."
} finally {
 $listener.Stop()
 & $Adb -s $Serial reverse --remove "tcp:$port" | Out-Null
}
