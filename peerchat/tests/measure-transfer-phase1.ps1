param([Parameter(Mandatory=$true)][string]$BaselineExe,[Parameter(Mandatory=$true)][string]$OptimizedExe,[string]$OutputDirectory='build\tests\phase1-comparison',[switch]$ParseOnly,[switch]$DiagnosticsComparisonOnly)
$ErrorActionPreference='Stop'
$baseline=(Resolve-Path -LiteralPath $BaselineExe).Path
$optimized=(Resolve-Path -LiteralPath $OptimizedExe).Path
New-Item -ItemType Directory -Force -Path $OutputDirectory | Out-Null
$savedDelay=$env:PEERCHAT_TEST_RELAY_DELAY_MS;$savedDiagnostics=$env:PEERCHAT_TEST_TRANSFER_DIAGNOSTICS;$savedExtended=$env:PEERCHAT_TEST_EXTENDED_SIZE
$cases=@(
    @{Name='baseline-a';Exe=$baseline;Delay=0;Diagnostics=$false},
    @{Name='optimized-a';Exe=$optimized;Delay=0;Diagnostics=$false},
    @{Name='optimized-b';Exe=$optimized;Delay=0;Diagnostics=$false},
    @{Name='baseline-b';Exe=$baseline;Delay=0;Diagnostics=$false},
    @{Name='baseline-delay10';Exe=$baseline;Delay=10;Diagnostics=$false},
    @{Name='optimized-delay10';Exe=$optimized;Delay=10;Diagnostics=$false},
    @{Name='baseline-delay35';Exe=$baseline;Delay=35;Diagnostics=$false},
    @{Name='optimized-delay35';Exe=$optimized;Delay=35;Diagnostics=$false},
    @{Name='baseline-diagnostics';Exe=$baseline;Delay=0;Diagnostics=$true},
    @{Name='optimized-diagnostics';Exe=$optimized;Delay=0;Diagnostics=$true}
)
$summaries=[Collections.Generic.List[object]]::new()
if($DiagnosticsComparisonOnly){$cases=@(@{Name='closed-a';Exe=$optimized;Delay=0;Diagnostics=$false},@{Name='open-a';Exe=$optimized;Delay=0;Diagnostics=$true},@{Name='open-b';Exe=$optimized;Delay=0;Diagnostics=$true},@{Name='closed-b';Exe=$optimized;Delay=0;Diagnostics=$false})}
function Fields([string]$line){$result=@{};foreach($token in $line.Split(' ')){if($token -match '^([a-z_][a-z_0-9]*)=(\d+(?:\.\d+)?)$'){$result[$Matches[1]]=[double]::Parse($Matches[2],[Globalization.CultureInfo]::InvariantCulture)}};return $result}
try {
    foreach($case in $cases){
        $env:PEERCHAT_TEST_RELAY_DELAY_MS=[string]$case.Delay
        $env:PEERCHAT_TEST_TRANSFER_DIAGNOSTICS=if($case.Diagnostics){'1'}else{$null}
        $env:PEERCHAT_TEST_EXTENDED_SIZE=if($case.Delay -eq 0){'1'}else{$null}
        $log=Join-Path $OutputDirectory ($case.Name+'.log')
        if(-not $ParseOnly){& $case.Exe --benchmark 2>&1 | Tee-Object -FilePath $log | Out-Null
            if($LASTEXITCODE){throw "Authenticated transfer benchmark failed: $log"}}
        $current=$null
        foreach($line in Get-Content -LiteralPath $log){
            if($line.StartsWith('HOST_NATIVE_TLS_RELAY ')){
                $f=Fields $line
                $current=[pscustomobject]@{Case=$case.Name;OneWayFixtureDelayMs=$case.Delay;Diagnostics=$case.Diagnostics;Bytes=$f.bytes;TotalMs=$f.total_ms;AcceptedToCompleteMs=$f.accepted_to_complete_ms;Mbps=$f.bytes*0.008/$f.accepted_to_complete_ms;ProcessCpuMs=$f.process_cpu_us/1000;ProcessPeakWorkingSetMiB=$f.process_peak_working_set_bytes/1048576;AckSamples=0;MeanAckMs=$null;MeanSenderGapMs=$null;SenderJournalCommits=0;SenderJournalMs=0;ReceiverDispatchMs=0;ReceiverFlushMs=0;LastJournalWin32Error=0}
                $summaries.Add($current)
            }elseif($current -and $line.StartsWith('transfer=')){
                $f=Fields $line;if($f.size -ne $current.Bytes){continue}
                if($f.sending -eq 1){$current.AckSamples=$f.ack_samples;if($f.ack_samples){$current.MeanAckMs=$f.ack_turnaround_us/$f.ack_samples/1000};if($f.sender_idle_samples){$current.MeanSenderGapMs=$f.sender_ack_to_send_us/$f.sender_idle_samples/1000};$current.SenderJournalCommits=$f.journal_selected_commits;$current.SenderJournalMs=$f.journal_selected_us/1000;$current.LastJournalWin32Error=$f.journal_last_win32_error}
                else{$current.ReceiverDispatchMs=$f.receiver_dispatch_us/1000;$current.ReceiverFlushMs=$f.flush_us/1000}
            }
        }
        if(-not $ParseOnly){Write-Output "PASS: $($case.Name), exact bytes and SHA-256 completion, two simultaneous transfers; host fixture only."}
        $summaries | Export-Csv -LiteralPath (Join-Path $OutputDirectory 'summary.csv') -NoTypeInformation
    }
}finally{$env:PEERCHAT_TEST_RELAY_DELAY_MS=$savedDelay;$env:PEERCHAT_TEST_TRANSFER_DIAGNOSTICS=$savedDiagnostics;$env:PEERCHAT_TEST_EXTENDED_SIZE=$savedExtended}
