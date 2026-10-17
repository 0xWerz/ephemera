# Fetches Epic's Pixel Streaming servers for this engine version and builds them, once.
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\stream\setup.ps1
# Everything goes into <project>\Stream (nothing is added to the engine's folder): the servers'
# source (Epic's PixelStreamingInfrastructure, branch UE5.8), the Node.js they ask for, and what
# they are built into. Safe to run again: what is already there is kept.
$ErrorActionPreference = 'Stop'
$project = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$root = Join-Path $project 'Stream'
$infra = Join-Path $root 'infra'
New-Item -ItemType Directory -Force $root | Out-Null
if (-not (Test-Path (Join-Path $infra 'SignallingWebServer'))) {
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $zip = Join-Path $root 'infra.zip'
    Write-Output 'fetching the servers (branch UE5.8)'
    Invoke-WebRequest 'https://github.com/EpicGamesExt/PixelStreamingInfrastructure/archive/refs/heads/UE5.8.zip' -OutFile $zip -UseBasicParsing
    & "$env:SystemRoot\System32\tar.exe" -xf $zip -C $root
    Rename-Item (Join-Path $root 'PixelStreamingInfrastructure-UE5.8') 'infra'
    Remove-Item $zip
}
# Epic's own script: fetches Node.js into the servers' folder, installs their packages, builds the
# signalling server and the page the browser loads.
$cmd = Join-Path $infra 'SignallingWebServer\platform_scripts\cmd'
Push-Location $cmd
& cmd /c 'call setup.bat < nul'
Pop-Location
# The signalling server itself (Epic's start script builds it on first start; here it is built now,
# so that starting a stream never waits on a compiler).
$server = Join-Path $infra 'SignallingWebServer'
if (-not (Test-Path (Join-Path $server 'dist\index.js'))) {
    Push-Location $server
    & (Join-Path $cmd 'node\npm.cmd') run build
    Pop-Location
}
# Windows' firewall: other devices may fetch the page from this PC, but only devices on the same
# network (and the owner's own Tailscale devices, if that is in use). Nothing is opened to the
# internet. Needs an administrator; without one it is skipped and said.
$node = Join-Path $cmd 'node\node.exe'
$rule = 'Ephemera stream page'
try {
    if (-not (Get-NetFirewallRule -DisplayName $rule -ErrorAction SilentlyContinue)) {
        New-NetFirewallRule -DisplayName $rule -Description 'Lets devices on this network open the page that plays Ephemera in a browser (Tools\stream).' `
            -Direction Inbound -Action Allow -Program $node -Protocol TCP -LocalPort 80 -RemoteAddress LocalSubnet, '100.64.0.0/10' -Profile Any | Out-Null
        Write-Output "firewall: added the rule '$rule' (this network only, TCP 80, for $node)"
    } else { Write-Output "firewall: the rule '$rule' is already there" }
} catch { Write-Output "firewall: not changed ($($_.Exception.Message)); other devices may be unable to open the page" }
$built = (Test-Path (Join-Path $infra 'SignallingWebServer\dist\index.js')) -and (Test-Path (Join-Path $infra 'SignallingWebServer\www\player.html'))
Write-Output ("servers built: {0}" -f $built)
Get-ChildItem (Join-Path $infra 'SignallingWebServer\www') -ErrorAction SilentlyContinue | Select-Object -First 12 Name, Length | Format-Table -AutoSize | Out-String
if (-not $built) { exit 1 }
