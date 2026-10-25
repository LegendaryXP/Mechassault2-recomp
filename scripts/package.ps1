# Make the release zip in dist\ (nothing is uploaded or published):
#   powershell -File scripts\package.ps1 [-RepoUrl https://github.com/LegendaryXP/Mechassault2-recomp] [-Build]
#
# The zip holds ma2.exe and what it needs to start on a clean PC, laid out
# for Quiver Launcher (one .exe and no .bat at the top, which is what it
# launches) and for unzipping by hand:
#   ma2.exe, vcruntime140.dll, mkb.default.ini, README.txt, LICENSE.txt,
#   THIRD_PARTY_NOTICES.txt, LGPL-2.1.txt
# No game data. It also refreshes patches\xboxrecomp-ma2-runtime.patch from
# ..\xboxrecomp (this port's toolkit changes, against upstream eb0aa1d), which
# the source release needs for the LGPL parts, and writes dist\icon.png and
# the zip's SHA-256 for the release page and Quiver's catalog entry.
param(
    [string]$RepoUrl = 'https://github.com/LegendaryXP/Mechassault2-recomp',
    [switch]$Build
)
$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$tk = Join-Path (Split-Path -Parent $root) 'xboxrecomp'
$upstream = 'eb0aa1d'

$m = Select-String -Path "$root\CMakeLists.txt" -Pattern 'project\(ma2 VERSION ([0-9.]+)'
if (-not $m) { throw 'no version in CMakeLists.txt' }
$version = $m.Matches[0].Groups[1].Value
$name = "MechAssault2-PC-v$version-win-x64"
"version $version"

if ($Build) {
    cmake --build "$root\build" --config Release
    if ($LASTEXITCODE) { throw 'build failed' }
}
$exe = "$root\build\Release\ma2.exe"
if ((Get-Item $exe).VersionInfo.ProductVersion -ne $version) {
    throw "ma2.exe is not version ${version}: rebuild (-Build)"
}

# The toolkit changes, as one patch against upstream.
Push-Location $tk
try {
    if (git status --porcelain --untracked-files=no) { Write-Warning 'xboxrecomp has uncommitted changes: they are not in the patch' }
    # git writes the file itself: piped through PowerShell, non-ASCII text
    # in the sources would be re-encoded and the patch would not apply.
    git diff --binary "--output=$root\patches\xboxrecomp-ma2-runtime.patch" "$upstream..HEAD"
    if ($LASTEXITCODE) { throw 'git diff failed' }
    "patch: $(git rev-list --count "$upstream..HEAD") toolkit commits since $upstream"
} finally { Pop-Location }

# The C runtime ma2.exe links (vcruntime140.dll), from Visual Studio's
# redistributable files.
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$crt = Get-ChildItem "$vs\VC\Redist\MSVC" -Recurse -Filter vcruntime140.dll |
    Where-Object { $_.FullName -match '\\x64\\Microsoft\.VC\d+\.CRT\\' } |
    Sort-Object { $_.VersionInfo.FileVersionRaw } | Select-Object -Last 1
if (-not $crt) { throw 'vcruntime140.dll not found in the Visual Studio redist folder' }

$dist = "$root\dist"
$stage = "$dist\$name"
if (Test-Path $stage) { Remove-Item $stage -Recurse -Force }
New-Item -ItemType Directory -Force $stage | Out-Null
function Fill([string]$from, [string]$to) {
    $t = [IO.File]::ReadAllText($from).Replace('@VERSION@', $version).Replace('@REPO_URL@', $RepoUrl)
    [IO.File]::WriteAllText($to, ($t -replace "`r?`n", "`r`n"))      # Notepad-friendly
}
Copy-Item $exe "$stage\ma2.exe"
Copy-Item $crt.FullName "$stage\vcruntime140.dll"
Copy-Item "$root\mkb.ini" "$stage\mkb.default.ini"
Fill "$root\release\README.txt" "$stage\README.txt"
Fill "$root\release\THIRD_PARTY_NOTICES.txt" "$stage\THIRD_PARTY_NOTICES.txt"
Fill "$root\LICENSE" "$stage\LICENSE.txt"
Copy-Item "$tk\LICENSES\LGPL-2.1.txt" "$stage\LGPL-2.1.txt"

# Nothing of the game's data, and nothing Quiver could launch but ma2.exe.
$bad = Get-ChildItem $stage -Recurse -File | Where-Object {
    $_.Extension -in '.xbe', '.mgf', '.bik', '.xwb', '.xbx', '.iso', '.bat', '.cmd' -or
    ($_.Extension -eq '.exe' -and $_.Name -ne 'ma2.exe') }
if ($bad) { throw "not for the zip: $($bad.Name -join ', ')" }

$zip = "$dist\$name.zip"
if (Test-Path $zip) { Remove-Item $zip -Force }
Compress-Archive -Path "$stage\*" -DestinationPath $zip -CompressionLevel Optimal
Copy-Item "$root\assets\icon.png" "$dist\icon.png"
$sha = (Get-FileHash $zip -Algorithm SHA256).Hash.ToLower()
"$sha  $name.zip" | Set-Content "$dist\$name.zip.sha256" -Encoding ascii
""
Get-ChildItem $stage | ForEach-Object { "  {0,-26} {1,12:N0}" -f $_.Name, $_.Length }
""
"zip    $zip ($([math]::Round((Get-Item $zip).Length / 1MB, 1)) MB)"
"sha256 $sha"
"icon   $dist\icon.png"
