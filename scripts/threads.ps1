# Sample per-thread CPU time of the running ma2.exe over an interval.
#   powershell -File scripts\threads.ps1 -Secs 10
param([int]$Secs = 10)
$p = Get-Process -Name ma2 -ErrorAction SilentlyContinue | Sort-Object StartTime -Descending | Select-Object -First 1
if (-not $p) { "ma2.exe not running"; exit 1 }
$a = @{}; foreach ($t in $p.Threads) { try { $a[$t.Id] = $t.TotalProcessorTime.TotalMilliseconds } catch {} }
Start-Sleep -Seconds $Secs
$p.Refresh()
$rows = foreach ($t in $p.Threads) {
    try {
        $ms = $t.TotalProcessorTime.TotalMilliseconds - $(if ($a.ContainsKey($t.Id)) { $a[$t.Id] } else { 0 })
        [pscustomobject]@{ Tid = $t.Id; CpuPct = [math]::Round(100 * $ms / ($Secs * 1000), 1); Start = $t.StartAddress }
    } catch {}
}
$rows | Sort-Object CpuPct -Descending | Select-Object -First 16 | Format-Table -AutoSize | Out-String
"total: {0}% of one core" -f [math]::Round(($rows | Measure-Object CpuPct -Sum).Sum, 1)
