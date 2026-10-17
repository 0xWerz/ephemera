# Fetches NVIDIA's DLSS plugin for Unreal Engine 5.8 into Plugins/ (Windows only; optional).
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\fetch_dlss.ps1
#
# NVIDIA's licence lets the plugin ship inside a packaged game but not be redistributed on its own, so it
# is not part of the public source: this downloads it from NVIDIA (https://developer.nvidia.com/rtx/dlss/
# get-started) and unpacks the two plugins the game uses, DLSS and StreamlineNGXCommon (about 1.9 GB).
# Without it the game builds and runs the same, with Unreal's own TSR.
$ErrorActionPreference = 'Stop'
$ProgressPreference = 'SilentlyContinue'
$Root = Split-Path -Parent $PSScriptRoot
$Url = 'https://developer.nvidia.com/downloads/assets/gameworks/downloads/secure/dlss/ue-dlss-5.8/8.9.0/2026.10.16_ue5.8_dlss4.5plugin_v8.9.0.zip'
$Zip = Join-Path $env:TEMP 'dlss_ue58_8.9.0.zip'
if (Test-Path (Join-Path $Root 'Plugins\DLSS\DLSS.uplugin')) { 'DLSS is already in Plugins\'; exit 0 }
"Downloading DLSS 8.9.0 for UE 5.8 (about 1.2 GB)..."
Invoke-WebRequest -Uri $Url -OutFile $Zip -MaximumRedirection 10
Push-Location $Root
try { tar -xf $Zip Plugins/DLSS Plugins/StreamlineNGXCommon } finally { Pop-Location }
Remove-Item $Zip
if (-not (Test-Path (Join-Path $Root 'Plugins\DLSS\DLSS.uplugin'))) { throw 'The plugin did not unpack into Plugins\DLSS' }
'DLSS installed in Plugins\. Rebuild the game.'
