param([string]$Name, [string]$AF = '0', [string]$MSAA = '1', [string]$Scale = '1',
      [string]$WS = '', [int]$Secs = 135, [int]$FlagOn = 108, [int]$FlagOff = 122,
      [int]$PresentAt = 0, [switch]$NoDump)
# Boot to the first mission with the auto pad, then by script fire the laser
# (MOUSE1 = RT) for a few seconds; dump the frames while firing, and the
# presented (full-resolution) frame number PresentAt. Real input ignored.
# -NoDump: no frame dumps (RECOMP_FB_DUMP writes the presented target back
# every frame, ~1.3 ms, so frame rates are play.bat's only without it).
$r = 'C:\Mechassault2\ma2-recomp'
$flag = "$r\analysis\fb_$Name.on"
Remove-Item $flag -ErrorAction SilentlyContinue
Remove-Item "$r\analysis\fb_$Name" -Recurse -Force -ErrorAction SilentlyContinue
$env:RECOMP_D3D11 = '1'; $env:RECOMP_FB_WINDOW = '1'; $env:RECOMP_PAD_AUTO = '3000'
$env:RECOMP_RENDER_SCALE = $Scale; $env:RECOMP_D3D11_AF = $AF; $env:RECOMP_D3D11_MSAA = $MSAA
if ($WS) { $env:RECOMP_WIDESCREEN = $WS }
$env:RECOMP_MKB = '1'; $env:RECOMP_MKB_INI = "$r\mkb.ini"
$env:RECOMP_MKB_SCRIPT = $(if ($env:VIS_SCRIPT) { $env:VIS_SCRIPT } else { '112000:MOUSE1:2500,116000:MOUSE1:2500' })
if ($PresentAt) { $env:RECOMP_PRESENT_DUMP = "$r\analysis\present_$Name.bmp:$PresentAt" } else { $env:RECOMP_PRESENT_DUMP = $null }
$job = Start-Job -ScriptBlock {
    param($flag, $on, $off)
    Start-Sleep -Seconds $on; Set-Content -Path $flag -Value 'on'
    Start-Sleep -Seconds ($off - $on); Remove-Item $flag -ErrorAction SilentlyContinue
} -ArgumentList $flag, $FlagOn, $FlagOff
if ($NoDump) { & "$r\scripts\run.ps1" -Name $Name -Secs $Secs -NoKeyboard }
else { & "$r\scripts\run.ps1" -Name $Name -Secs $Secs -NoKeyboard -DumpWhile $flag }
Stop-Job $job -ErrorAction SilentlyContinue; Remove-Job $job -Force -ErrorAction SilentlyContinue
Remove-Item $flag -ErrorAction SilentlyContinue
Select-String -Path "$r\analysis\${Name}_stderr.txt" -Pattern '\[GPU\] [0-9.]+ fps|anisotropic|MSAA|present dump|PRESENT' | Select-Object -Last 6 | ForEach-Object { $_.Line.Trim() }
