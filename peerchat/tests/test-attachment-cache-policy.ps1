param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$out=Join-Path $root ('build/tests/cache-policy-'+[Guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Path $out | Out-Null
Push-Location $root
try {
 $fx=Join-Path $env:WINDIR 'Microsoft.NET/Framework64/v4.0.30319'
 foreach($arch in @('x64','x86')){
  & "$fx/csc.exe" /nologo /target:exe "/platform:$arch" /reference:System.Core.dll /reference:System.Web.Extensions.dll "/out:$out/cache-$arch.exe" .\server\ServerConfiguration.cs .\server\AttachmentCachePolicy.cs .\server\AttachmentCacheStorage.cs .\tests\cache_policy_test.cs
  if($LASTEXITCODE){throw "Cache $arch build failed"}
  & "$out/cache-$arch.exe" "$out/config-$arch"
  if($LASTEXITCODE){throw "Cache $arch tests failed"}
 }
 $refs=@('System.dll','System.Core.dll','System.Xaml.dll','System.Web.Extensions.dll','WPF/WindowsBase.dll','WPF/PresentationCore.dll','WPF/PresentationFramework.dll')|ForEach-Object{'/reference:'+(Join-Path $fx $_)}
 & "$fx/csc.exe" /nologo /target:exe /platform:x64 /main:CacheAdminSettingsTest $refs "/out:$out/cache-admin.exe" .\shared\VersionInfo.cs .\server\ServerConfiguration.cs .\server\PeerChat.Server.Admin.cs .\tests\cache_admin_settings_test.cs
 if($LASTEXITCODE){throw 'Cache admin fixture build failed'}
 & "$out/cache-admin.exe"
 if($LASTEXITCODE){throw 'Cache admin fixture failed'}
 Write-Output "Evidence: $out. No live server access; 32-bit Windows arithmetic is not physical ARM32 execution."
} finally {Pop-Location}
