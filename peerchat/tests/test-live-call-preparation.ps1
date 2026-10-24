param(
 [Parameter(Mandatory)][PSCredential]$FirstCredential,
 [Parameter(Mandatory)][PSCredential]$SecondCredential,
 [Parameter(Mandatory)][string]$ServerPin,
 [string]$ServerHost='173.225.14.37',
 [int]$ServerPort=7443,
 [switch]$BackgroundWake,
 [string]$FirstSerial='emulator-5580',
 [string]$SecondSerial='emulator-5582',
 [string]$Adb='C:\Users\Livel\AppData\Local\Android\Sdk\platform-tools\adb.exe'
)
$ErrorActionPreference='Stop'
if($FirstCredential.UserName -cne 'codex' -or $SecondCredential.UserName -cne 'codex1' -or $FirstSerial -notmatch '^emulator-\d+$' -or $SecondSerial -notmatch '^emulator-\d+$' -or $FirstSerial -eq $SecondSerial){throw 'Two distinct emulators and authorized test accounts required'}
if($ServerPin -notmatch '^[0-9a-f]{64}$'){throw 'Existing trusted server pin required'}
$package=if($BackgroundWake){'com.peerchat.android.backgroundintegration'}else{'com.peerchat.android.voiceintegration'}
$root=Split-Path $PSScriptRoot -Parent
$evidence=Join-Path $root ('build/tests/live-calls-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $evidence | Out-Null
$runs=@()
try {
 foreach($item in @(@($FirstSerial,$FirstCredential),@($SecondSerial,$SecondCredential))){
  $serial=$item[0];$credential=$item[1]
  $packages=(& $Adb -s $serial shell pm list packages $package | Out-String)
  if($packages -notmatch ('package:'+ [regex]::Escape($package))){throw 'Isolated package required'}
  $listener=[Net.Sockets.TcpListener]::new([Net.IPAddress]::Loopback,0);$listener.Start();$port=$listener.LocalEndpoint.Port
  & $Adb -s $serial reverse "tcp:$port" "tcp:$port" | Out-Null
  $process=Start-Process -FilePath $Adb -ArgumentList @('-s',$serial,'shell','am','instrument','-w','-r','-e','disposableProbePort',$port,'-e','class',$(if($BackgroundWake){'com.peerchat.android.LiveBackgroundCallTest'}else{'com.peerchat.android.LiveCallPreparationTest'}),"$package.test/androidx.test.runner.AndroidJUnitRunner") -WindowStyle Hidden -PassThru -RedirectStandardOutput (Join-Path $evidence "$serial.log") -RedirectStandardError (Join-Path $evidence "$serial.err")
  $runs+=@{serial=$serial;credential=$credential;listener=$listener;port=$port;process=$process;accept=$listener.AcceptTcpClientAsync()}
 }
 foreach($run in $runs){
  if(-not $run.accept.Wait(60000)){throw 'Test configuration channel timeout'}
  $run.client=$run.accept.Result;$stream=$run.client.GetStream();$stream.ReadTimeout=180000
  $payload=[Text.Encoding]::UTF8.GetBytes((@{host=$ServerHost;port=$ServerPort;pin=$ServerPin;account=$run.credential.UserName;password=$run.credential.GetNetworkCredential().Password}|ConvertTo-Json -Compress))
  try{$size=[BitConverter]::GetBytes([Net.IPAddress]::HostToNetworkOrder([int]$payload.Length));$stream.Write($size,0,4);$stream.Write($payload,0,$payload.Length);$stream.Flush()}
  finally{[Array]::Clear($payload,0,$payload.Length)}
 }
 # Exchange only public device identities from each independent logged-in owner.
 foreach($run in $runs){
  $peer=New-Object byte[] 64;$offset=0
  while($offset -lt 64){$read=$run.client.GetStream().Read($peer,$offset,64-$offset);if($read -eq 0){throw 'Peer identity channel closed'};$offset+=$read}
  if([Text.Encoding]::ASCII.GetString($peer) -notmatch '^[0-9a-f]{64}$'){throw 'Invalid public identity'}
  $run.peer=$peer
 }
 for($i=0;$i -lt 2;$i++){$stream=$runs[$i].client.GetStream();$stream.Write($runs[1-$i].peer,0,64);$stream.Flush()}
 foreach($run in $runs){
  if(-not $run.process.WaitForExit(720000)){throw 'Bounded call test timeout'}
  $result=Get-Content -LiteralPath (Join-Path $evidence "$($run.serial).log") -Raw
  $result
  if($result -notmatch 'OK \(1 test\)' -or $result -match 'FAILURES|INSTRUMENTATION_FAILED'){throw "Call test failed; evidence $evidence"}
 }
 Write-Output "PASS live test-account calls (background wake scheduling injected when selected); evidence $evidence. Emulator packet evidence only, not physical audio quality or Windows interoperability."
}finally{
 foreach($run in $runs){
  if(-not $run.process.HasExited){& $Adb -s $run.serial shell am force-stop $package | Out-Null}
  if($run.client){$run.client.Dispose()};$run.listener.Stop()
  & $Adb -s $run.serial reverse --remove "tcp:$($run.port)" | Out-Null
 }
}
