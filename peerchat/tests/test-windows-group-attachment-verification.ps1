param()
$ErrorActionPreference='Stop'
$root=Split-Path $PSScriptRoot -Parent
$executionPath=Join-Path $root 'desktop\GroupAttachmentExecution.cs'
$sessionPath=Join-Path $root 'desktop\AttachmentDeviceSession.cs'
$execution=Get-Content -LiteralPath $executionPath -Raw
$session=Get-Content -LiteralPath $sessionPath -Raw

function Get-MethodBody([string]$Source,[string]$Signature){
    $start=$Source.IndexOf($Signature,[StringComparison]::Ordinal)
    if($start -lt 0){throw "Missing method: $Signature"}
    $open=$Source.IndexOf('{',$start)
    if($open -lt 0){throw "Missing method body: $Signature"}
    $depth=0
    for($i=$open;$i -lt $Source.Length;$i++){
        if($Source[$i] -eq '{'){$depth++}
        elseif($Source[$i] -eq '}'){$depth--;if($depth -eq 0){return $Source.Substring($open+1,$i-$open-1)}}
    }
    throw "Unclosed method body: $Signature"
}

function Require([bool]$Condition,[string]$Reason){if(-not $Condition){throw $Reason}}

$record=Get-MethodBody $execution 'void RecordAttachmentCapability('
$verify=Get-MethodBody $execution 'async Task<bool> VerifyAttachmentDevice('
$hasSaved=Get-MethodBody $execution 'bool HasSavedAttachmentSession('
$load=Get-MethodBody $session 'public Credential Load('
$safeFailure=Get-MethodBody $session 'public static string SafeFailure('
$openSaved=Get-MethodBody $session 'internal static DiscoveryClient Open(DiscoveryClient main,Func<byte[],byte[]> nativeProof,long account,string device,string profileDirectory,bool allowFirstEnrollment,Func<bool> stillCurrent)'

Require ($record -notmatch '\bNotify\s*\(|MessageBox|ShowDialog') 'Capability refresh must not display a modal or notification.'
Require ($record -match 'value\.capabilityCode\s*==\s*"cache_device_session_required"') 'Automatic setup must require the exact device-session-required code.'
Require ($record -match 'value\.capabilityGeneration\s*==\s*groupAttachmentNetworkGeneration') 'Automatic setup must be limited to the current attachment generation.'
Require ($record -match 'groupAttachmentExecution\s*!=\s*null\s*&&\s*!groupAttachmentDeviceAttempted\s*&&\s*groupAttachmentDeviceClient\s*==\s*null\s*&&\s*groupAttachmentDeviceTask\s*==\s*null') 'Automatic setup must require an active execution and prevent a second attempt.'
Require ($record -match 'bool saved\s*=\s*HasSavedAttachmentSession\(owner\);\s*if\(String\.IsNullOrEmpty\(groupAttachmentDeviceError\)\)\s*\{\s*var ignored\s*=\s*VerifyAttachmentDevice\(!saved\);') 'Enrollment must be allowed only when no saved credential exists and no credential error occurred.'
Require ($verify -match 'groupAttachmentDeviceAttempted\s*=\s*true;') 'Verification must latch the single-attempt flag before starting work.'
Require ($hasSaved -match 'catch\(Exception error\)\s*\{\s*groupAttachmentDeviceError\s*=\s*AttachmentDeviceSession\.SafeFailure\(error\);\s*return false;') 'Unreadable saved credentials must become an error, not an enrollment opportunity.'
Require ($load -match 'Saved attachment device authorization is unavailable; it will not be replaced automatically\.') 'Corrupt saved authorization must be preserved as a hard failure.'
Require ($safeFailure -match 'remember_revoked' -and $safeFailure -match 'remember_invalid') 'Revoked or invalid saved authorization must not be replaced automatically.'
Require ($openSaved -match 'if\(saved==null\)\s*\{\s*if\(!allowFirstEnrollment\)') 'A saved-credential failure must not fall through to first enrollment.'

Write-Output 'Windows group attachment automatic-verification source contract passed.'
