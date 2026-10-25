# Run ma2.exe with the bring-up environment, capped, and summarise.
#   powershell -File scripts\run.ps1 -Name run22 -Secs 90 [-Skip "0x00420BDA,0x00310954"] [-Frames]
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$Secs = 90,
    [string]$Skip = "",
    [switch]$Frames,
    [string]$DumpWhile = "",
    [switch]$NoKeyboard,
    [switch]$NoUsb,
    [switch]$SeparateSave,
    [string]$SaveSeed = 'testsave_seed'
)
$root = 'C:\Mechassault2\ma2-recomp'
Set-Location $root
# A relaunch leaves a chain of ma2.exe processes (parent waits on child);
# killing only the top one strands the rest, which then hold the exe open.
# So a run ends by killing every process descended from the one it started --
# and only those: an ma2.exe already running is the player's (play.bat) or
# another session's, and is never touched. Wait for it instead.
function Get-Descendants([int]$Id) {
    $all = Get-CimInstance Win32_Process -ErrorAction SilentlyContinue
    $out = @(); $todo = @($Id)
    while ($todo.Count) {
        $next = @()
        foreach ($q in $todo) {
            foreach ($c in ($all | Where-Object { $_.ParentProcessId -eq $q })) { $out += $c.ProcessId; $next += $c.ProcessId }
        }
        $todo = $next
    }
    $out
}
while (Get-Process -Name ma2 -ErrorAction SilentlyContinue) {
    Write-Host "run.ps1: ma2.exe is already running (the player's or another run's); waiting"
    Start-Sleep -Seconds 15
}
$env:RECOMP_WATCHDOG_SECS = "$Secs"
$env:RECOMP_AC97_READY = '1'; $env:RECOMP_PB_EXEC = '1'; $env:RECOMP_PB_WALK = 'dma'; $env:RECOMP_ASYNC_IO = '1'; $env:RECOMP_VBLANK = '1'; $env:RECOMP_HEAP_RECLAIM = '1'
$env:RECOMP_USB = $(if ($NoUsb) { $null } else { '1' }); $env:RECOMP_USB_PADS = $(if ($NoUsb) { $null } else { '1' })
$env:RECOMP_KEYBOARD = $(if ($NoKeyboard) { $null } else { '1' })
# As play.bat: DirectSound's APU restarts off (audio).
$env:RECOMP_APU_SSL_IRQ = '0'; $env:RECOMP_APU_EP_MARK = '1'
$env:RECOMP_MUTE = '1'          # test runs are silent; play.bat is not
# Saves: test runs use %LOCALAPPDATA%\xboxrecomp UDATA and TDATA, where the
# player's saves were before they moved to Documents\MechAssault2 (which the
# test runs must never touch: ma2.exe would default to it). Each run first
# copies them to %LOCALAPPDATA%\xboxrecomp\backup-before-test (replaced every
# run).
# -SeparateSave runs from a fresh copy of analysis\testsave_seed instead (a
# profile, no progress: what the scripted boot timings were tuned against).
$xr = "$env:LOCALAPPDATA\xboxrecomp"
if ($SeparateSave) {
    $env:RECOMP_SAVE_DIR = "$root\analysis\testsave"
    # -SaveSeed usersave_seed: the player's save, copied by "Copy save for
    # testing.bat" (test runs cannot read the player's own save folder).
    if (Test-Path "$root\analysis\$SaveSeed") {
        Remove-Item "$root\analysis\testsave" -Recurse -Force -ErrorAction SilentlyContinue
        Copy-Item "$root\analysis\$SaveSeed" "$root\analysis\testsave" -Recurse
    }
} else {
    $env:RECOMP_SAVE_DIR = $xr
    if (Test-Path "$xr\UDATA") {
        Remove-Item "$xr\backup-before-test" -Recurse -Force -ErrorAction SilentlyContinue
        New-Item -ItemType Directory -Force "$xr\backup-before-test" | Out-Null
        Copy-Item "$xr\UDATA", "$xr\TDATA" "$xr\backup-before-test" -Recurse -ErrorAction SilentlyContinue
    }
}
$env:MA2_SKIP_ICALL = $Skip
if ($Frames) {
    $fb = "$root\analysis\fb_$Name"
    New-Item -ItemType Directory -Force $fb | Out-Null
    $env:RECOMP_FB_DUMP = "$fb\frame"; $env:RECOMP_FB_DUMP_FLIPS = '1'
} elseif ($DumpWhile) {
    # Every frame, but only while the file $DumpWhile exists.
    $fb = "$root\analysis\fb_$Name"
    New-Item -ItemType Directory -Force $fb | Out-Null
    $env:RECOMP_FB_DUMP = "$fb\frame"; $env:RECOMP_FB_DUMP_FLIPS = $DumpWhile
} else {
    $env:RECOMP_FB_DUMP = $null; $env:RECOMP_FB_DUMP_FLIPS = $null
}
$err = "$root\analysis\${Name}_stderr.txt"
# Redirected by cmd straight to the files, as play.bat does. Start-Process's
# own -RedirectStandard* hands the game a pipe that PowerShell drains slowly:
# a log write then blocked for ~7 ms, and a loading screen that logs every
# file read ran at 2 MB/s.
$p = Start-Process -FilePath "$env:ComSpec" -WorkingDirectory $root -WindowStyle Hidden -PassThru `
    -ArgumentList '/c', "`"`"$root\build\Release\ma2.exe`" 1>`"$root\analysis\${Name}_stdout.txt`" 2>`"$err`"`""
$kids = @()
if (-not $p.WaitForExit(($Secs + 40) * 1000)) {
    $kids = Get-Descendants $p.Id
    Stop-Process -Id $p.Id -Force -ErrorAction SilentlyContinue
}
$kids += Get-Descendants $p.Id
foreach ($k in $kids) { Stop-Process -Id $k -Force -ErrorAction SilentlyContinue }
$flips = (Select-String -Path $err -Pattern '\((\d+) flips\)' | Select-Object -Last 1).Matches.Groups[1].Value
$crash = (Select-String -Path $err -Pattern '^\[CRASH\]' | Select-Object -First 1).Line
$exit = (Select-String -Path $err -Pattern 'HalReturnToFirmware: routine' | Select-Object -First 1)
"{0}: skip=[{1}] flips={2} exit={4} crash={3}" -f $Name, $Skip, $flips, $(if ($crash) { $crash.Substring(0, [Math]::Min(90, $crash.Length)) } else { 'none' }), $(if ($exit) { 'yes' } else { 'no' })
