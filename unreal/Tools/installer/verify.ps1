# Proves the setup program on this PC: installs it into a scratch folder with no shortcuts, checks
# what arrived against the build it was made from, uninstalls it, and waits until nothing is left.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\installer\verify.ps1
#
# Does nothing while a game or the editor is running.
$Root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
$setup = Get-ChildItem (Join-Path $Root 'Builds\Installer\Ephemera-Setup-*.exe') | Sort-Object LastWriteTime -Descending | Select-Object -First 1
if (-not $setup) { 'no setup program in Builds\Installer'; exit 2 }
if (Get-Process UnrealEditor, Ephemera* -ErrorAction SilentlyContinue) { 'a game or the editor is running: not started'; exit 9 }
$dir = Join-Path $Root 'Saved\InstallTest'
$run = Start-Process -Wait -PassThru $setup.FullName -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART', '/SP-', '/CURRENTUSER', "/DIR=$dir", '/MERGETASKS=!desktopicon', '/NOICONS'
"install exit $($run.ExitCode)"
$files = Get-ChildItem $dir -Recurse -File
'{0} files, {1:N0} MB installed' -f $files.Count, (($files | Measure-Object Length -Sum).Sum / 1MB)
$build = Join-Path $Root 'Builds\Windows'
$built = Get-ChildItem $build -Recurse -File | Where-Object { $_.FullName -notlike '*\Ephemera\Saved\*' -and $_.Name -notlike 'Manifest_*.txt' }
'{0} files, {1:N0} MB in the build' -f $built.Count, (($built | Measure-Object Length -Sum).Sum / 1MB)
$differ = 0
foreach ($file in $built) {
    $there = Join-Path $dir $file.FullName.Substring($build.Length + 1)
    if (-not (Test-Path $there) -or (Get-Item $there).Length -ne $file.Length) { $differ++ }
}
"files missing or of a different size: $differ"
$same = (Get-FileHash (Join-Path $dir 'Ephemera\Binaries\Win64\Ephemera.exe')).Hash -eq (Get-FileHash (Join-Path $build 'Ephemera\Binaries\Win64\Ephemera.exe')).Hash
"program identical: $same"
Start-Process -Wait (Join-Path $dir 'unins000.exe') -ArgumentList '/VERYSILENT', '/SUPPRESSMSGBOXES', '/NORESTART'
$waited = 0
while ((Test-Path $dir) -and $waited -lt 90) { Start-Sleep 1; $waited++ }
if (Test-Path $dir) { 'left behind: ' + ((Get-ChildItem $dir -Recurse | ForEach-Object { $_.FullName.Substring($dir.Length) }) -join ', ') } else { 'uninstall left nothing' }
