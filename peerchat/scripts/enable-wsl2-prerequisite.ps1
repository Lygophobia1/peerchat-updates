# Run elevated only after explicit WSL2 setup approval. Never reboots.
[CmdletBinding()]
param()
$ErrorActionPreference = 'Stop'
$root = Split-Path $PSScriptRoot -Parent
$evidence = Join-Path $root ('build/wsl-setup-' + (Get-Date -Format 'yyyyMMdd-HHmmss'))
New-Item -ItemType Directory -Path $evidence | Out-Null
Start-Transcript -Path (Join-Path $evidence 'prerequisite.log') | Out-Null
try {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = New-Object Security.Principal.WindowsPrincipal($identity)
    if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
        throw 'Administrator consent is required; no feature change attempted.'
    }
    Get-CimInstance Win32_OperatingSystem | Format-List Caption,Version,BuildNumber
    Get-CimInstance Win32_Processor | Format-List VirtualizationFirmwareEnabled,SecondLevelAddressTranslationExtensions
    # Modern WSL2 needs VirtualMachinePlatform; do not enable legacy WSL1 unnecessarily.
    $feature = Get-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform
    $feature | Format-List FeatureName,State
    if ($feature.State -eq 'EnablePending' -or $feature.State -eq 'DisablePending') {
        Write-Output 'RESTART_REQUIRED: pending feature transition. Stop; no reboot initiated.'
        exit 3010
    }
    if ($feature.State -ne 'Enabled') {
        $result = Enable-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform -All -NoRestart
        $result | Format-List Online,RestartNeeded
        if ($result.RestartNeeded) {
            Write-Output 'RESTART_REQUIRED: stop and request user restart. No reboot initiated.'
            exit 3010
        }
    }
    $verified = Get-WindowsOptionalFeature -Online -FeatureName VirtualMachinePlatform
    $verified | Format-List FeatureName,State
    if ($verified.State -eq 'EnablePending' -or $verified.State -eq 'DisablePending') {
        Write-Output 'RESTART_REQUIRED: pending feature transition. Stop; no reboot initiated.'
        exit 3010
    }
    if ($verified.State -ne 'Enabled') { throw 'VirtualMachinePlatform did not reach Enabled state.' }
    Write-Output 'PREREQUISITE_READY: validate WSL package and Ubuntu separately.'
} catch {
    Write-Output ('SETUP_FAILED: ' + $_.Exception.Message)
    exit 1
} finally {
    Stop-Transcript | Out-Null
}
