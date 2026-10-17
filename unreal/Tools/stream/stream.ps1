# Ephemera in a web browser: starts Epic's signalling server and the game with no window, streaming
# what it draws (Pixel Streaming 2). Run Tools\stream\setup.ps1 once first.
#
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\stream\stream.ps1 -Serve     start, stay, stop on a key
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\stream\stream.ps1            start and return
#   powershell -NoProfile -ExecutionPolicy Bypass -File Tools\stream\stream.ps1 -Stop      stop what it started
#
# The game is drawn once, on this PC's graphics card, and the picture is sent to whoever opens the
# page: everyone who connects sees the same game and can play it (one world, one view).
param(
    [int]$Port = 80,              # where the page is served
    [string]$Size = '1920x1080',  # the picture that is sent
    [int]$Scale = 75,             # percent of those pixels the game really draws, the rest upscaled. Measured on the RTX 3060:
                                  # 100 gives 38 frames a second, 75 gives 47, and after video encoding the two pictures cannot be told apart
    [int]$MaxMbps = 50,           # the most the picture may use; the browser's connection decides between this and the least
    [int]$MinMbps = 22,           # the least it may use. High on purpose (the owner, 2026-10-14: "id rather it cut/pause than low
                                  # bitrate to look like shit"): on a link that cannot carry this the picture stalls, it does not blur
    [int]$Fps = 60,               # the most frames a second the game draws while streaming (the stream itself is told twice this)
    [string]$Weather = 'fronts',  # the sky visitors get (overcast costs a quarter of the frame rate)
    [string]$Codec = 'VP8',       # how the picture is packed: VP8 or VP9 (encoded on the processor) or H264 (on the graphics card).
                                  # H264 is the sharper and the cheaper, and in this engine version it cannot be used: with a viewer
                                  # connected, NVIDIA's encoder aborts the game (no error, no crash report; exit 0xC0000409 in
                                  # nvEncodeAPI64.dll) after one to three minutes, at any frame rate and any bit rate (measured
                                  # 14 October 2026: six deaths in four watched runs; VP8 ran its whole watched run without one).
                                  # With the engine's capture threads switched off instead (MediaIO.EnableExperimentalScheduling=0,
                                  # MediaIO.ScheduleOnAnyThread=0) the encoder hangs the graphics card within seconds.
    [int]$MinQuality = 55,        # 0 to 100: the worst a frame may look, whatever the bit rate says
    [string]$CVars = '',          # further console variables for the game, name=value,name=value
    [string]$Sky = 'grain=0',     # sky and camera settings for this stream (the game's -EphemeraSky), key=value,key=value. No film
                                  # grain: a video encoder spends its bits on grain and has none left for the grass
    [int]$Hold = 0,               # for tests: stop again after this many seconds
    [string]$Game = '',           # the game's exe (default: the packaged build in Builds\Windows)
    [string]$Extra = '',          # further arguments for the game
    [switch]$Loud,                # leave the game's sound playing on this PC as well (by default only visitors hear it)
    [switch]$Window,              # play it here too: the game in a window on this PC's screen, with its sound, while the same game
                                  # goes out to browsers (whoever opens the page sees what you see and can take the controls)
    [switch]$Serve,               # stay in the foreground, restart the game if it stops, end on a key press
    [switch]$Stop
)
$ErrorActionPreference = 'Stop'
$project = Split-Path (Split-Path $PSScriptRoot -Parent) -Parent
$root = Join-Path $project 'Stream'
$server = Join-Path $root 'infra\SignallingWebServer'
$node = Join-Path $server 'platform_scripts\cmd\node\node.exe'
$state = Join-Path $root 'running.json'
$logs = Join-Path $root 'logs'

function Stop-Stream {
    if (Test-Path $state) {
        # Only what this script started, by the process ids it wrote down.
        foreach ($id in (Get-Content $state | ConvertFrom-Json).ids) {
            $process = Get-Process -Id $id -ErrorAction SilentlyContinue
            if ($process -and ($process.ProcessName -like 'node*' -or $process.ProcessName -like 'Ephemera*')) { Stop-Process -Id $id -Force -ErrorAction SilentlyContinue }
        }
        Remove-Item $state -ErrorAction SilentlyContinue
        Write-Output 'stream stopped'
    } else { Write-Output 'no stream was running' }
}
if ($Stop) { Stop-Stream; exit 0 }

if (-not (Test-Path (Join-Path $server 'dist\index.js'))) { Write-Output 'The streaming servers are not built yet: run Tools\stream\setup.ps1 first.'; exit 1 }
if (-not $Game) { $Game = Join-Path $project 'Builds\Windows\Ephemera\Binaries\Win64\Ephemera.exe' }
if (-not (Test-Path $Game)) { Write-Output "No game at $Game"; exit 1 }
if (Test-Path $state) { Stop-Stream }
if (Get-Process 'Ephemera*' -ErrorAction SilentlyContinue) { Write-Output 'Ephemera is already running: close it first (one game at a time on this graphics card).'; exit 1 }
New-Item -ItemType Directory -Force $logs | Out-Null

# Where browsers are told to look for a way through to this PC: public STUN servers, which is all
# most home connections need. (The game and the browser then talk to each other directly.)
$peers = Join-Path $root 'peers.json'
'{"iceServers":[{"urls":["stun:stun.l.google.com:19302","stun:stun1.l.google.com:19302"]}]}' | Set-Content -Encoding ascii $peers
# The page a visitor gets: Epic's player with this game's name on it and this game's defaults (a
# first-person game wants the mouse locked to it, and the picture at the size the game draws), so
# the bare address is all anyone needs.
$www = Join-Path $server 'www'
$page = (Get-Content (Join-Path $www 'player.html') -Raw) -replace '<title>[^<]*</title>', '<title>Ephemera</title>'
$defaults = "<script>if (!location.search) { history.replaceState(null, '', location.pathname + '?AutoConnect=true&AutoPlayVideo=true&HoveringMouse=false&MatchViewportRes=false&StreamerId=Ephemera'); }</script>"
$page = $page -replace '<head>', "<head>`n    $defaults"
Set-Content -Encoding utf8 (Join-Path $www 'ephemera.html') $page

function Start-Server {
    $arguments = @('dist\index.js', '--no_config', '--serve', '--player_port', $Port, '--streamer_port', '8888', '--http_root', $www, '--homepage', 'ephemera.html',
        '--peer_options_file', $peers, '--log_folder', $logs, '--console_messages', 'basic')
    Start-Process -FilePath $node -ArgumentList $arguments -WorkingDirectory $server -WindowStyle Hidden -PassThru `
        -RedirectStandardOutput (Join-Path $logs 'server.out') -RedirectStandardError (Join-Path $logs 'server.err')
}

function Start-Game {
    $width, $height = $Size.Split('x')
    # The picture starts at a high bit rate and is allowed to climb, so a home network gets a sharp
    # image at once; the connection's own congestion control brings it down where it must.
    # r.RDG.AsyncSetupQueue=0: the one crash seen while streaming was in the engine setting up a
    # render pass on a worker thread; done in order instead, it costs nothing measurable.
    $shown = if ($Window) { @('-Windowed') } else { @('-RenderOffScreen', '-ForceRes', '-Windowed', '-Unattended', '-EphemeraNoRockGeometry') }
    $arguments = @('Ephemera') + $shown + @("-ResX=$width", "-ResY=$height",
        '-PixelStreamingConnectionURL=ws://127.0.0.1:8888', '-PixelStreamingID=Ephemera',
        "-PixelStreamingEncoderCodec=$Codec", '-PixelStreamingEncoderH264Profile=BASELINE',
        "-PixelStreamingWebRTCStartBitrate=$([Math]::Max($MinMbps, 30) * 1000000)", "-PixelStreamingWebRTCMinBitrate=$($MinMbps * 1000000)", "-PixelStreamingWebRTCMaxBitrate=$($MaxMbps * 1000000)",
        "-PixelStreamingWebRTCFps=$($Fps * 2)", "-EphemeraMaxFps=$Fps", '-PixelStreamingInputController=Any',
        "-EphemeraWeather=$Weather", "-EphemeraScale=$Scale", "-DPCVars=r.RDG.AsyncSetupQueue=0$(if ($CVars) { ',' + $CVars })",
        "-AbsLog=$(Join-Path $logs 'game.log')")
    if ($MinQuality -gt 0) { $arguments += "-PixelStreamingEncoderMinQuality=$MinQuality" }
    if ($Sky) { $arguments += "-EphemeraSky=$Sky" }
    if ($Extra) { $arguments += $Extra.Split(' ') }
    # What the game (and the streaming library inside it) writes to its error stream is kept, one
    # file per start: a library that aborts says why only there, and leaves no crash report.
    $stamp = (Get-Date).ToString('HHmmss')
    $style = if ($Window) { 'Normal' } else { 'Hidden' }
    $process = Start-Process -FilePath $Game -ArgumentList $arguments -PassThru -WindowStyle $style `
        -RedirectStandardError (Join-Path $logs "game-$stamp.err") -RedirectStandardOutput (Join-Path $logs "game-$stamp.out")
    if (-not ($Loud -or $Window)) {
        # Visitors hear the game; this PC's speakers need not. The game is muted in Windows' own
        # volume mixer (after its sound has been taken for the stream), as soon as it opens its sound.
        Start-Process -FilePath 'powershell' -WindowStyle Hidden -ArgumentList @('-NoProfile', '-ExecutionPolicy', 'Bypass', '-File', (Join-Path $PSScriptRoot 'mute.ps1'), '-ProcessId', $process.Id) `
            -RedirectStandardOutput (Join-Path $logs 'mute.out') -RedirectStandardError (Join-Path $logs 'mute.err')
    }
    $process
}

function Save-State($web, $play) { @{ ids = @($web.Id, $play.Id); started = (Get-Date).ToString('s') } | ConvertTo-Json | Set-Content $state }

$web = Start-Server
$play = Start-Game
Save-State $web $play
$address = (Get-NetIPAddress -AddressFamily IPv4 | Where-Object { $_.IPAddress -like '192.168.*' -or $_.IPAddress -like '10.*' -or $_.IPAddress -like '172.*' } | Select-Object -First 1).IPAddress
$suffix = if ($Port -eq 80) { '' } else { ":$Port" }
Write-Output 'Ephemera is starting (about 20 seconds to load).'
Write-Output "On this PC:        http://localhost$suffix/"
Write-Output "On your network:   http://$address$suffix/"
# Tailscale, if this PC uses it: the same page for every device in the tailnet, wherever it is.
# (`tailscale serve --bg 80`, done once, gives the https address; without it the plain one still works.)
$tailscale = Join-Path $env:ProgramFiles 'Tailscale\tailscale.exe'
if (Test-Path $tailscale) {
    try {
        $self = (& $tailscale status --json | ConvertFrom-Json).Self
        if ($self.Online) { Write-Output "Over Tailscale:    https://$($self.DNSName.TrimEnd('.'))/   or   http://$($self.TailscaleIPs[0])$suffix/" }
    } catch { }
}
if (-not ($Serve -or $Hold -gt 0)) { Write-Output 'Stop it with:      Tools\stream\stream.ps1 -Stop'; exit 0 }

# Stay with it: a game that has stopped (it should not, but a visitor must never find a dead page)
# is started again, and so is the server.
if ($Serve) { Write-Output ''; Write-Output 'Press any key in this window to stop the stream.' }
$until = if ($Hold -gt 0) { (Get-Date).AddSeconds($Hold) } else { [DateTime]::MaxValue }
while ((Get-Date) -lt $until) {
    if ($Serve) {
        $pressed = $false
        try { $pressed = [Console]::KeyAvailable } catch { }
        if ($pressed) { [Console]::ReadKey($true) | Out-Null; break }
    }
    if ($play.HasExited) {
        Write-Output "$(Get-Date -Format T)  the game stopped (code $($play.ExitCode)); starting it again"
        $play = Start-Game
        Save-State $web $play
    }
    if ($web.HasExited) {
        Write-Output "$(Get-Date -Format T)  the server stopped; starting it again"
        $web = Start-Server
        Save-State $web $play
    }
    Start-Sleep -Milliseconds 500
}
Stop-Stream
