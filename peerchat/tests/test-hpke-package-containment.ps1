param([string]$EvaluationPackage)
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$tokens=$null;$errors=$null
$ast=[Management.Automation.Language.Parser]::ParseFile((Join-Path $root 'scripts\publish-update.ps1'),[ref]$tokens,[ref]$errors)
if($errors.Count){throw 'Publisher syntax errors'}
# Load only pure packaging functions; never execute publish/sign/network logic.
foreach($name in @('Stop-Publish','New-ClientZip','New-Manifest')){
    $definition=$ast.FindAll({param($node)$node -is [Management.Automation.Language.FunctionDefinitionAst]},$false) | Where-Object Name -eq $name
    if(@($definition).Count -ne 1){throw "Packaging function missing: $name"}
    Invoke-Expression $definition.Extent.Text
}
function Reject([scriptblock]$Operation){
    $rejected=$false
    try{& $Operation}catch{if($_.Exception.Message -notmatch 'Evaluation|license'){throw};$rejected=$true}
    if(!$rejected){throw 'Evaluation packaging was incorrectly accepted'}
}
$out=Join-Path $root ('build\tests\hpke-packaging-'+[Guid]::NewGuid().ToString('N'))
$fixture=Join-Path $out 'marked';$bare=Join-Path $out 'binary-only';$normal=Join-Path $out 'normal'
New-Item -ItemType Directory -Path $fixture,$bare,$normal | Out-Null
# Synthetic binary marker and sidecar fixtures; no production package modified.
[IO.File]::WriteAllText((Join-Path $fixture 'HPKE-EVALUATION.json'),'{}')
[IO.File]::WriteAllText((Join-Path $bare 'PeerChat.Core.dll'),'PEERCHAT_HPKE_EVALUATION_UNLICENSED')
[IO.File]::WriteAllText((Join-Path $normal 'PeerChat.Core.dll'),'synthetic normal package fixture')
Reject {New-ClientZip $fixture (Join-Path $out 'marked.zip')}
Reject {New-ClientZip $bare (Join-Path $out 'binary.zip')}
Add-Type -AssemblyName System.IO.Compression.FileSystem
[IO.Compression.ZipFile]::CreateFromDirectory($fixture,(Join-Path $out 'external-marked.zip'))
[IO.Compression.ZipFile]::CreateFromDirectory($bare,(Join-Path $out 'external-binary.zip'))
Reject {New-Manifest (Join-Path $out 'external-marked.zip') (Join-Path $out 'manifest.json') 'test'}
Reject {New-Manifest (Join-Path $out 'external-binary.zip') (Join-Path $out 'manifest.json') 'test'}
New-ClientZip $normal (Join-Path $out 'normal.zip')
if(!(Test-Path -LiteralPath (Join-Path $out 'normal.zip'))){throw 'Normal packaging path did not work'}
if($EvaluationPackage){
    if(!(Test-Path -LiteralPath (Join-Path $EvaluationPackage 'HPKE-EVALUATION.json'))){throw 'Actual evaluation package sidecar missing'}
    if(![Text.Encoding]::ASCII.GetString([IO.File]::ReadAllBytes((Join-Path $EvaluationPackage 'PeerChat.Core.dll'))).Contains('PEERCHAT_HPKE_EVALUATION_UNLICENSED')){throw 'Actual evaluation binary marker missing'}
    Reject {New-ClientZip $EvaluationPackage (Join-Path $out 'forbidden-updater.zip')}
}
Write-Output 'PASS: sidecar/binary-marker rejection, promoted ZIP rejection, normal fixture packaging, no publication performed'
