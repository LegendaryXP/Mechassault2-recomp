# Boot to the first mission on the D3D11 back end and measure it.
#   powershell -File scripts\gameplay.ps1 -Name run63 [-Secs 260] [-Frames] [-Profile] [-Cpu]
# RECOMP_PAD_AUTO presses START and A in turn until frames look like
# gameplay, then lets go (usb_gamepad.c), so boot speed does not matter.
param(
    [Parameter(Mandatory = $true)][string]$Name,
    [int]$Secs = 260,
    [switch]$Frames,
    [switch]$Profile,
    [switch]$Cpu
)
$root = 'C:\Mechassault2\ma2-recomp'
$env:RECOMP_D3D11 = $(if ($Cpu) { '0' } else { '1' })
$env:RECOMP_FB_WINDOW = '1'
$env:RECOMP_PAD_AUTO = '3000'
if ($Profile) {
    $env:MA2_PROFILE = '1'; $env:MA2_PROFILE_DELAY = '200'; $env:MA2_PROFILE_SECS = '25'
    $env:MA2_PROFILE_OUT = "$root\analysis\profile_$Name.txt"
}
$a = @{ Name = $Name; Secs = $Secs }
if ($Frames) { $a.Frames = $true }
& "$root\scripts\run.ps1" @a
$env:RECOMP_PAD_AUTO = $null; $env:RECOMP_D3D11 = $null; $env:RECOMP_FB_WINDOW = $null
$env:MA2_PROFILE = $null; $env:MA2_PROFILE_OUT = $null
Select-String -Path "$root\analysis\${Name}_stderr.txt" -Pattern '\[D3D11\] per frame|\[GPU\] [0-9.]+ fps' |
    Select-Object -Last 6 | ForEach-Object Line
