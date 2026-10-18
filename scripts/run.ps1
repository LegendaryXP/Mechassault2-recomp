# Run ma2.exe with the bring-up environment, capped, and summarise.
#   powershell -File scripts\run.ps1 -Name run22 -Secs 90 [-Skip "0x00420BDA,0x00310954"] [-Frames]
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$Secs = 90,
    [string]$Skip = "",
    [switch]$Frames,
    [switch]$NoKeyboard,
    [switch]$NoUsb
)
$root = 'C:\Mechassault2\ma2-recomp'
Set-Location $root
# A relaunch leaves a chain of ma2.exe processes (parent waits on child);
# killing only the top one strands the rest, which then hold the exe open.
Get-Process -Name ma2 -ErrorAction SilentlyContinue | Stop-Process -Force
$env:RECOMP_WATCHDOG_SECS = "$Secs"
$env:RECOMP_AC97_READY = '1'; $env:RECOMP_PB_EXEC = '1'; $env:RECOMP_PB_WALK = 'dma'; $env:RECOMP_ASYNC_IO = '1'; $env:RECOMP_VBLANK = '1'; $env:RECOMP_HEAP_RECLAIM = '1'
$env:RECOMP_USB = $(if ($NoUsb) { $null } else { '1' }); $env:RECOMP_USB_PADS = $(if ($NoUsb) { $null } else { '1' })
$env:RECOMP_KEYBOARD = $(if ($NoKeyboard) { $null } else { '1' })
# As play.bat: DirectSound's APU restarts off (audio), 4:3 until 16:9 boots.
$env:RECOMP_APU_SSL_IRQ = '0'; $env:RECOMP_APU_EP_MARK = '1'
if (-not $env:RECOMP_WIDESCREEN) { $env:RECOMP_WIDESCREEN = '0' }
$env:RECOMP_MUTE = '1'          # test runs are silent; play.bat is not
$env:MA2_SKIP_ICALL = $Skip
if ($Frames) {
    $fb = "$root\analysis\fb_$Name"
    New-Item -ItemType Directory -Force $fb | Out-Null
    $env:RECOMP_FB_DUMP = "$fb\frame"; $env:RECOMP_FB_DUMP_FLIPS = '1'
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
if (-not $p.WaitForExit(($Secs + 40) * 1000)) { Stop-Process -Id $p.Id -Force }
Get-Process -Name ma2 -ErrorAction SilentlyContinue | Stop-Process -Force
$flips = (Select-String -Path $err -Pattern '\((\d+) flips\)' | Select-Object -Last 1).Matches.Groups[1].Value
$crash = (Select-String -Path $err -Pattern '^\[CRASH\]' | Select-Object -First 1).Line
$exit = (Select-String -Path $err -Pattern 'HalReturnToFirmware: routine' | Select-Object -First 1)
"{0}: skip=[{1}] flips={2} exit={4} crash={3}" -f $Name, $Skip, $flips, $(if ($crash) { $crash.Substring(0, [Math]::Min(90, $crash.Length)) } else { 'none' }), $(if ($exit) { 'yes' } else { 'no' })
