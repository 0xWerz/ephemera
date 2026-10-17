# Pixel Streaming for Ephemera on the home PC (UE 5.8, RTX 3060)

Research date: 2026-10-13. Research only. No game code, config, server or program was changed or started. Only web fetches/searches and read-only file inspection were used.

Scope note: the local engine copy read is the macOS build of UE 5.8 at `/Volumes/External/code/tests/outerwilds/.unreal/UE_5.8`. The Windows folder `/Volumes/External/code/tests/outerwilds/.unreal/UE_5.8_Win` contains the same plugin folders (checked by listing only). The PC's own install at `F:\UE_5.8` was not inspected. Platform gating is in the .uplugin files, so the plugin source is the same for Win64.

Tags used below:
- [LOCAL] file read under `.unreal/UE_5.8`.
- [REPO] file read from the `UE5.8` branch of `EpicGamesExt/PixelStreamingInfrastructure`.
- [EPIC] Epic documentation at dev.epicgames.com (URLs in the source index).
- [WEB] other official pages (NVIDIA, MDN, Microsoft, Cloudflare, Tailscale).
- [3P] third-party blog or forum. Secondary only.
- UNVERIFIED: not confirmed by a source or a test run.

---

## What to do, in order (for this PC: Win11, RTX 3060 12 GB, F:\UE_5.8, F:\Ephemera)

1. Check that `F:\UE_5.8\Engine\Plugins\Media\PixelStreaming2` exists on the PC. It does on the Mac copy; the PC copy was not inspected (UNVERIFIED).
2. Back up `F:\Ephemera\Ephemera.uproject` and the project `Config\DefaultEngine.ini` before editing (the project is not a git repo).
3. Add `{ "Name": "PixelStreaming2", "Enabled": true }` to the `Plugins` array of `Ephemera.uproject` (UE 5.8 `EngineAssociation` is already 5.8). Use one plugin only (PixelStreaming2).
4. Install Node.js v22.14.0 (value of the UE5.8 branch `NODE_VERSION` file), or let `start.bat` download it into the signalling folder.
5. From `F:\UE_5.8\Engine\Plugins\Media\PixelStreaming2\Resources\WebServers`, run `get_ps_servers.bat /b UE5.8`. Do not use `/v 5.8`: the script has no 5.8 case and silently falls back to the UE5.7 branch.
6. Run `SignallingWebServer\platform_scripts\cmd\start.bat --dev` once (installs and builds), then start without `--dev`. Confirm the console shows the streamer listener on 8888 and the player listener on 80.
7. Keep the signalling ports on LAN only at first. Do not forward or open 8888 (streamer) or 8889 (SFU) to the internet at any stage.
8. Start the signalling server with `--max_players 1` for the single-user phase.
9. Package Ephemera for Win64 with the plugin enabled (standard `RunUAT BuildCookRun`; command not run or verified here).
10. Put the encoder and WebRTC settings on the game's command line first. Command-line values override `.ini` values (Epic PS2 overview).
11. Launch the packaged game with `-PixelStreamingConnectionURL=ws://127.0.0.1:8888 -RenderOffScreen -ForceRes -ResX=1920 -ResY=1080 -Unattended` plus the encoder flags in section 3.
12. Start the game inside the logged-in desktop session, not directly over SSH. Use a scheduled task created with `/IT` (section 8). Confirm with `query session` and `tasklist /v`.
13. Test locally at `http://127.0.0.1/?AutoConnect=true&AutoPlayVideo=true&StartVideoMuted=true`. Check video, keyboard, mouse lock and gamepad.
14. Measure: game fps (`stat fps` in the game), stream stats (`-PixelStreamingLogStats`), NVENC session count (`nvidia-smi`, standard tool, UNVERIFIED here), and the feel of input latency.
15. Test LAN access from a second device at `http://<PC LAN IP>/`. Expect video and input. Gamepad and clipboard need a secure context (section 6).
16. For internet access, first try plain port forwarding: TCP 80 (or 443 with TLS), plus a narrow UDP range set with `-PixelStreamingWebRTCMinPort/MaxPort`. Test from a phone on mobile data.
17. If the phone test fails (strict NAT, carrier NAT), add a TURN relay on a cheap cloud VM (coturn). Epic recommends CoTURN; the infrastructure repo has a `v4.7.0-coturn-windows` tag.
18. Put HTTPS and WSS in front of the player (`--https` with certificates, or a tunnel for HTTP only) before showing it to others. This gives a secure context and avoids mixed content. Media still needs a UDP path (step 16 or 17).
19. For several visitors, plan one UE process per visitor with a unique `-PixelStreamingID`, plus your own orchestrator (the Matchmaker was removed in 5.5). On one RTX 3060, realistically one 40 fps instance (estimate, not measured).
20. Leave `PixelStreaming2.AllowPixelStreamingCommands` off, keep the default `InputController`, and log each test result under `.scratch` before anyone else uses the link.

---

## 1. Which plugin in 5.8

**Both plugins ship with 5.8.** Both `.uplugin` files have `EnabledByDefault: false` and `Category: Graphics`.

| Plugin name in `.uproject` | Friendly name | Descriptor | Status |
|---|---|---|---|
| `PixelStreaming` | Pixel Streaming | `Engine/Plugins/Media/PixelStreaming/PixelStreaming.uplugin` [LOCAL] | Original plugin. Epic's reference page says many settings "have changed or been removed in the new Pixel Streaming 2 plugin" [EPIC] |
| `PixelStreaming2` | Pixel Streaming 2 | `Engine/Plugins/Media/PixelStreaming2/PixelStreaming2.uplugin` [LOCAL] | Newer plugin, built on Epic's internal WebRTC layer (EpicRTC). Described as "a layer that makes it easier for Epic to maintain WebRTC internally" [EPIC] |

- The 5.8 "Getting Started" page says: "enable the **Pixel Streaming** or **Pixel Streaming 2** plugin" under Edit > Plugins > Graphics [EPIC].
- Epic's PS2 overview says both ship "to give users time to migrate", and does not explicitly rank them [EPIC].
- **Recommendation: `PixelStreaming2`.** It is the successor, its CVars are the ones in the 5.8 source, and the 5.8 infrastructure branch and frontend are built for it. Do not mix the original plugin's flags (`-PixelStreamingEncoderRateControl`, `-PixelStreamingEncoderMultipass`, `PixelStreaming.Encoder.*`) with PS2; several do not exist in PS2 (checked, section 4).

**Other plugins.** You enable only `PixelStreaming2`. Its descriptor lists its dependencies with `Enabled: true`, and the engine pulls them in [LOCAL, `PixelStreaming2.uplugin`]:
`PixelCapture`, `WmfMedia` (Win64), `AVCodecsCore`, `NVCodecs` (Win64/Linux), `AMFCodecs` (Win64), `VTCodecs` (Mac/iOS), `LibVpxCodecs` (software VP8/VP9), `WebSocketNetworking`, `XRBase`, `MediaIOFramework`.

PS2 modules [LOCAL]: `PixelStreaming2Core`, `PixelStreaming2`, `PixelStreaming2Settings`, `PixelStreaming2RTC`, `PixelStreaming2Servers`, `PixelStreaming2Input`, `PixelStreaming2HMD` (runtime); `PixelStreaming2Editor` (editor only).

Not needed for streaming out: `Experimental/PixelStreamingPlayer` (receives streams inside a UE app) [LOCAL].

---

## 2. Signalling server

### 2.1 Where it lives and which branch

- Repo: `https://github.com/EpicGamesExt/PixelStreamingInfrastructure` [REPO].
- Branch `UE5.8` exists. The UE5.8 README calls it "Current (latest released)"; `UE5.7` is "Supported"; `UE5.6` "End of life"; `UE5.5` "Unsupported"; `master` is dev [REPO].
- Tags for UE 5.8 (from the GitHub tags API) [REPO]:
  - `signalling-webserver-UE5.8-3.1.0` (newest UE5.8 signalling tag) and `signalling-webserver-UE5.8-3.0.0`.
  - `sfu-UE5.8-1.1.2` and `sfu-UE5.8-1.1.1`.
  - `lib-pixelstreamingfrontend-ue5.8-0.1.2`, `lib-pixelstreamingfrontend-ui-ue5.8-0.1.1`, `lib-pixelstreamingsignalling-ue5.8-0.2.0`.
  - `v4.7.0-coturn-windows` (coturn build for Windows; listed first in the tags API, which is not sorted by date) [REPO].
- `RELEASE_VERSION` on the UE5.8 branch = `0.1.0` (so the script's release name would be `UE5.8-0.1.0`). The GitHub release page `UE5.8-0.1.0` exists, but its asset list would not load, so the asset filenames are UNVERIFIED [WEB].

### 2.2 How the engine fetches it: `get_ps_servers`

Files (same in both plugin folders) [LOCAL]:
- `Engine/Plugins/Media/PixelStreaming2/Resources/WebServers/get_ps_servers.bat`
- `Engine/Plugins/Media/PixelStreaming2/Resources/WebServers/get_ps_servers.sh`
- (same two files under `Engine/Plugins/Media/PixelStreaming/Resources/WebServers/`)

Switches in `get_ps_servers.bat` [LOCAL]:
- `/v <UE version>`: picks a branch only for 4.26 to 5.7. **5.8 is not in the list**, so it falls back to `UE5.7` (`SET PSInfraTagOrBranch=UE5.7`).
- `/b <branch>`: branch. Sets `FlagPassed`, so the RELEASE_VERSION lookup is skipped. Downloads `https://github.com/EpicGamesExt/PixelStreamingInfrastructure/archive/refs/heads/<branch>.zip`.
- `/t <tag>`: tag. Downloads `.../archive/refs/tags/<tag>.zip`.
- `/r <release>`: release URL.
- `/h`: help.

Working command on the PC (from the WebServers folder):
```
get_ps_servers.bat /b UE5.8
```
The script writes a `DOWNLOAD_VERSION` file and copies `SignallingWebServer`, `Frontend`, `Signalling`, `Common`, `SFU` and others into the folder. The `.sh` equivalent uses `-b UE5.8` [LOCAL].

### 2.3 Node.js

- `NODE_VERSION` on the UE5.8 branch = `v22.14.0` [REPO].
- `common.bat` downloads `https://nodejs.org/dist/%NODE_VERSION%/<name>.zip` into a `node\` folder if none exists, and prepends it to PATH [REPO].
- Frontend README: "Only this NodeJS version is officially supported, other versions may BREAK YOUR BUILD" [REPO].

### 2.4 Start from the command line (Windows)

From the repo root (the WebServers folder on the PC) [REPO README]:
```
.\SignallingWebServer\platform_scripts\cmd\start.bat --dev
```
- `start.bat --dev` builds and installs everything (README). Then `npm start -- [arguments]` or `node dist/index.js [arguments]` runs it. `npm run develop` gives watchers.
- `platform_scripts\cmd\` contains `README.txt`, `common.bat`, `setup.bat`, `start.bat`, `start_turn.bat`, `start_with_stun.bat`, `start_with_turn.bat` [REPO listing].
- `start_with_stun.bat` only forwards its arguments to `start.bat` [REPO]. `start.bat` sets the server flags (`--serve`, `--console_messages verbose`, `--https_redirect`, `--log_config`, `--public_ip`) and an ICE `--peer_options` JSON, and adds `--http_root` (per the start.bat summary; exact text not checked). `common.bat` looks up the public IP with `curl http://api.ipify.org` [REPO summary].
- Epic's 5.8 Getting Started page gives a different sequence: run `setup.bat` (elevated PowerShell), then `start_with_stun.bat`. Expected console lines: `WebSocket listening to Streamer connections on :8888` and `Http listening on *: 80`, then `Streamer connected` [EPIC].

### 2.5 Ports

| Role | Default port | Flag (UE5.8 README) | Notes |
|---|---|---|---|
| Player (browser page + player WebSocket) | 80 TCP | `--player_port` | Only one to expose, if needed |
| Streamer (UE app connects here) | 8888 TCP/WS | `--streamer_port` | Keep local. Anyone who reaches it can register as a streamer |
| SFU | 8889 | `--sfu_port` | Only if using the SFU |
| HTTPS / WSS for player | 443 | `--https_port` (with `--https`) | Needs `--ssl_key_path`, `--ssl_cert_path` |
| WebRTC media (UE side) | UDP/TCP, default range 49152-65535 | none; UE CVars `PixelStreaming2.WebRTC.MinPort/MaxPort` | Narrow this range for port forwarding (section 6) [LOCAL] |
| SFU RTC media | 40000-49999 | `config.js` `rtcMinPort`/`rtcMaxPort` | [REPO SFU README] |
| TURN (coturn) | 3478 (start scripts' fallback) | from start_turn scripts | [REPO] |

### 2.6 Flags that matter (UE5.8 `SignallingWebServer/README.md`) [REPO]

| Flag | Default | Meaning |
|---|---|---|
| `--player_port` | 80 | Player connection port |
| `--streamer_port` | 8888 | Streamer connection port |
| `--sfu_port` | 8889 | SFU connection port |
| `--max_players` | 0 | Max viewers per streamer; 0 = unlimited |
| `--player_keepalive_timeout` | 30000 | ms before dropping an unresponsive viewer; 0 disables |
| `--serve` | true | Serve the web app on the player port |
| `--http_root` | `...\SignallingWebServer\www` | Web root |
| `--homepage` | `player.html` | Default page |
| `--https` | false | Serve HTTPS on `--https_port` |
| `--https_port` | 443 | HTTPS port |
| `--ssl_key_path` | `certificates/client-key.pem` | TLS key |
| `--ssl_cert_path` | `certificates/client-cert.pem` | TLS certificate |
| `--https_redirect` | true | Redirect HTTP to HTTPS |
| `--peer_options <json>` | "" | Extra JSON merged into `peerConnectionOptions` (STUN/TURN goes here) |
| `--turn_secret <secret>` | "" | Shared secret for time-limited TURN credentials (matches coturn `static-auth-secret`) |
| `--turn_secret_file <file>` | "" | Same, read from a file |
| `--turn_ttl <s>` | 86400 | Lifetime of player TURN credentials |
| `--log_level_console`, `--log_level_file` | info | debug/info/warning/error |
| `--log_folder` | `logs` | Log folder |
| `--console_messages [detail]` | not shown | Print signalling messages (`basic`, `verbose`, `formatted`) |
| `--rest_api` | false | REST API at `/api/api-definition` |
| `--config_file <file>` | not shown | Overrides `config.json`. `--save` writes arguments to `config.json` |
| `--stdin` | false | Accept console input |

- Config file: a `config.json` whose keys match the flag names without dashes, e.g. `"streamer_port": "8888"`. Its location is not stated (UNVERIFIED).
- STUN/TURN: the README gives no STUN keys. `common.bat` uses `stun.l.google.com:19302` as STUN and sets TURN defaults `PixelStreamingUser` / `AnotherTURNintheroad` on port 3478 [REPO]. Those TURN credentials are demo values. Do not use them.
- The Epic hosting guide says: "the IP address and port you specify for it in the peerConnectionOptions parameter are visible on the open Internet", and recommends CoTURN [EPIC].
- Matchmaker: the UE5.8 branch README says "The matchmaker has been removed" (since UE 5.5). The UE5.8 root listing still shows a `Matchmaker` folder. Its contents were not read (UNVERIFIED).

### 2.7 SFU (experimental)

- `SFU/README.md` in the UE5.8 branch [REPO]: `signallingURL` default `http://localhost:8889`; `SFUId` default `SFU`; `subscribeStreamerId` default `DefaultStreamer`; `retrySubscribeDelaySecs` default 10; mediasoup `rtcMinPort` 40000, `rtcMaxPort` 49999. The README gives no viewer limit and no HTTP flags.
- Epic says the SFU is experimental [EPIC].

---

## 3. Launching the packaged game as a streamer

### 3.1 Launch command (PS2)

Verified CLI name rule [LOCAL, `PixelStreaming2Settings/Private/PixelStreaming2PluginSettings.cpp` lines 96-104]: take the CVar name, remove dots, replace `PixelStreaming2` with `PixelStreaming`. A value adds `=`; a bool is a bare flag.

Connection:
- `-PixelStreamingConnectionURL=ws://127.0.0.1:8888` sets `PixelStreaming2.ConnectionURL` [LOCAL, line 367 and 1423].
- `-PixelStreamingURL=ws://127.0.0.1:8888` is a legacy alias that also maps to `ConnectionURL` (`GetLegacyCmdArg`, line 243) [LOCAL]. Epic's 5.8 Getting Started page uses `-PixelStreamingURL=` [EPIC].
- Legacy `-PixelStreamingIP=` and `-PixelStreamingPort=` are combined into `ws://IP:PORT` [LOCAL, line 1408-1423].
- If `ConnectionURL` is empty, the streamer logs "no signalling server URL has been set. Use ... -PixelStreamingConnectionURL=" and does not start [LOCAL, `EpicRtcStreamer.cpp` line 169].

Example (LAN baseline):
```
"F:\Ephemera\Builds\Windows\Ephemera.exe" -PixelStreamingConnectionURL=ws://127.0.0.1:8888 -PixelStreamingID=DefaultStreamer -PixelStreamingEncoderCodec=H264 -PixelStreamingEncoderH264Profile=BASELINE -PixelStreamingEncoderQualityPreset=HIGH_QUALITY -PixelStreamingWebRTCStartBitrate=15000000 -PixelStreamingWebRTCMinBitrate=2000000 -PixelStreamingWebRTCMaxBitrate=30000000 -RenderOffScreen -ForceRes -ResX=1920 -ResY=1080 -Unattended
```
- `-PixelStreamingID=` sets `PixelStreaming2.ID` (default `DefaultStreamer`) [LOCAL, line 348 and GetCmdArg line 177].
- `-RenderOffScreen`: Epic spells it `-RenderOffscreen` (the flag is case-insensitive in UE's parser: UNVERIFIED). Epic says it "runs headless with no local rendering. Recommended unless you need local output" [EPIC reference, original plugin]; the PS2 overview also suggests it "to avoid stream loss if the window is minimized" [EPIC].
- `-ForceRes`, `-ResX=`, `-ResY=`: standard UE flags. Epic lists them for the original plugin [EPIC reference].
- `-Unattended`: suppresses error dialogs [EPIC reference].
- `-AudioMixer`: "software audio mixing, for when no audio device is present" [EPIC reference]. Add only if the game cannot open the audio device in the session it runs in (UNVERIFIED need).
- `-StdOut -FullStdOutLogOutput`: maximum logs [EPIC reference].
- Stream logs: `-PixelStreamingLogStats` (CVar `PixelStreaming2.LogStats`, default false) [LOCAL].

### 3.2 Enable in the project before packaging

Yes. Add the plugin to `Ephemera.uproject` (section 1) and package again. The packaged modules come from the project's plugin list (standard UE behaviour; the Epic 5.8 page says to enable the plugin and "Restart Now") [EPIC]. The Epic packaging steps used by the 5.8 page: File > Package Project > Windows (64-bit) [EPIC].

### 3.3 Does the game need code or `DefaultEngine.ini`

- Not for basic streaming. `PixelStreaming2.AutoStartStream` (default true) and `PixelStreaming2.InitializeDefaultStreamer` (default true) mean the default streamer starts when the URL is set [LOCAL, lines 342-366].
- The Epic 5.8 page lists no `DefaultEngine.ini` change. Its only optional project setting is `Engine > Input > Always Show Touch Interface` [EPIC]. The launch parameter is set in the editor under Level Editor > Play > Additional Launch Parameters.
- Settings can come from `.ini`, command line or console. Command line overrides `.ini`; console overrides command line [EPIC PS2 overview].
- The `.ini` section is `[/Script/PixelStreaming2Settings.PixelStreaming2PluginSettings]` (seen in `DefaultPixelStreaming2.ini`, `[LOCAL]`). Property names are the second column of `GetCmdArg`, e.g. `Codec=H264`, `WebRTCFps=60`, `EncoderMinQuality=0`, `WebRTCMaxBitrate=30000000` (inferred from the code; UNVERIFIED in a packaged build).
- Input: the PS2 input module handles browser events. No game-side input component is documented for 5.8 (UNVERIFIED that none is needed for keyboard/mouse with Enhanced Input).

### 3.4 Encoder and WebRTC CVars (PS2 5.8) with CLI names and defaults

All from `PixelStreaming2Settings/Private/PixelStreaming2PluginSettings.cpp` [LOCAL] unless noted.

| CVar | CLI | Default | Note |
|---|---|---|---|
| `PixelStreaming2.ConnectionURL` | `-PixelStreamingConnectionURL=` | "" | line 367 |
| `PixelStreaming2.ID` | `-PixelStreamingID=` | DefaultStreamer | line 348 |
| `PixelStreaming2.Encoder.Codec` | `-PixelStreamingEncoderCodec=` | H264 | H264, VP8, VP9, AV1 (line 462) |
| `PixelStreaming2.Encoder.H264Profile` | `-PixelStreamingEncoderH264Profile=` | Baseline | AUTO, BASELINE, MAIN, HIGH, PROGRESSIVE_HIGH, CONSTRAINED_HIGH, HIGH444 (line 483) |
| `PixelStreaming2.Encoder.QualityPreset` | `-PixelStreamingEncoderQualityPreset=` | Default | ULTRA_LOW_QUALITY, LOW_QUALITY, DEFAULT, HIGH_QUALITY, LOSSLESS (line 421) |
| `PixelStreaming2.Encoder.LatencyMode` | `-PixelStreamingEncoderLatencyMode=` | UltraLowLatency | ULTRA_LOW_LATENCY, LOW_LATENCY, DEFAULT (line 428). No effect found on NVENC (section 4.3) |
| `PixelStreaming2.Encoder.TargetBitrate` | `-PixelStreamingEncoderTargetBitrate=` | -1 | Bps; -1 = off (line 403) |
| `PixelStreaming2.Encoder.MinQuality` | `-PixelStreamingEncoderMinQuality=` | 0 | 0-100 (line 409) |
| `PixelStreaming2.Encoder.MaxQuality` | `-PixelStreamingEncoderMaxQuality=` | 100 | 0-100 (line 415) |
| `PixelStreaming2.Encoder.KeyframeInterval` | `-PixelStreamingEncoderKeyframeInterval=` | -1 | -1 = no periodic keyframes; NVENC re-inits on change (line 434) |
| `PixelStreaming2.Encoder.MaxSessions` | `-PixelStreamingEncoderMaxSessions=` | -1 | Cap on hardware sessions (line 440) |
| `PixelStreaming2.Encoder.EnableSimulcast` | `-PixelStreamingEncoderEnableSimulcast` (flag) | false | H264 or VP8 only; the CVar text says full use needs the SFU (line 446) |
| `PixelStreaming2.WebRTC.Fps` | `-PixelStreamingWebRTCFps=` | 60 | line 504 |
| `PixelStreaming2.WebRTC.StartBitrate` | `-PixelStreamingWebRTCStartBitrate=` | 1000000 | bps; line 518 |
| `PixelStreaming2.WebRTC.MinBitrate` | `-PixelStreamingWebRTCMinBitrate=` | 100000 | bps; line 524 |
| `PixelStreaming2.WebRTC.MaxBitrate` | `-PixelStreamingWebRTCMaxBitrate=` | 40000000 | bps; line 537 |
| `PixelStreaming2.WebRTC.DisableReceiveAudio` | flag | false | line 550 |
| `PixelStreaming2.WebRTC.DisableReceiveVideo` | flag | true | line 556 |
| `PixelStreaming2.WebRTC.DisableTransmitAudio` | flag | false | line 562 |
| `PixelStreaming2.WebRTC.DisableAudioSync` | flag | true | "useful in low latency usecases" (line 574) |
| `PixelStreaming2.WebRTC.EnableFlexFec` | flag | false | "can cause a reduction in quality if total bitrate is low" (line 580) |
| `PixelStreaming2.WebRTC.NegotiateCodecs` | flag | false | false = send only the selected codec (line 605) |
| `PixelStreaming2.WebRTC.CodecPreferences` | `-PixelStreamingWebRTCCodecPreferences=` | AV1,H264,VP9,VP8 | Used only when NegotiateCodecs is on (line 611) |
| `PixelStreaming2.WebRTC.MinPort` / `MaxPort` | `-PixelStreamingWebRTCMinPort=` / `...MaxPort=` | 49152 / 65535 | ICE port range (lines 937-943) |
| `PixelStreaming2.WebRTC.PortAllocatorFlags` | `-PixelStreamingWebRTCPortAllocatorFlags=` | "" | "DISABLE_UDP,DISABLE_STUN,..." (line 931) |
| `PixelStreaming2.WebRTC.DisableFrameDropper` | flag | false | line 955 |
| `PixelStreaming2.WebRTC.VideoPacing.MaxDelay` / `Factor` | `-PixelStreamingWebRTCVideoPacingMaxDelay=` / `...Factor=` | -1 / -1 | off below zero (lines 961-967) |
| `PixelStreaming2.DecoupleFramerate` | `-PixelStreamingDecoupleFramerate` (flag) | false | key spelled `DecoupleFrameRate` in the CLI table (line 170), CVar spelled `Framerate` (line 292). Case is probably harmless (UNVERIFIED) |
| `PixelStreaming2.DecoupleWaitFactor` | `-PixelStreamingDecoupleWaitFactor=` | 1.25 | line 305 |
| `PixelStreaming2.InputController` | `-PixelStreamingInputController=` | Any | Any or Host (line 286) |
| `PixelStreaming2.DisableLatencyTester` | flag | false | line 280 |
| `PixelStreaming2.LogStats` | flag | false | line 254 |
| `PixelStreaming2.AllowPixelStreamingCommands` | legacy name `-AllowPixelStreamingCommands`; keep off | false | browser console commands (line 709) |
| `PixelStreaming2.UseMediaCapture` | flag | true | line 329 |
| `PixelStreaming2.CaptureUseFence` | flag | true | line 375 |

Frontend or SFU pieces are in sections 5 to 7.

---

## 4. Quality and performance on an RTX 3060

### 4.1 Codec: what the 3060 can encode and what browsers decode

- RTX 3060 (NVENC 7th gen, Ampere). NVIDIA's support matrix row [WEB, as parsed by fetch]: H.264 4:2:0 YES, H.264 4:4:4 YES, HEVC YES, HEVC B-frames YES, **AV1 encode NO**. "Max # of concurrent sessions" listed as **12** for the RTX 3060 row. The page has no GA106-specific row. AV1 decode is listed (8-bit and 10-bit; the parse was not clean) [WEB].
- The UE PS2 source comment and the Epic hosting guide both say GeForce cards support **8** concurrent encode sessions [LOCAL, `PixelStreaming2Settings/.../PixelStreaming2PluginSettings.cpp` line 443; EPIC hosting guide]. This conflicts with the 12 from the NVIDIA page. Use the runtime count (NVML) to settle it (UNVERIFIED).
- PS2 has no HEVC encoder path. The encoder switch in `EpicRtcVideoEncoderInitializer.cpp` handles H264, AV1, VP8 and VP9 only [LOCAL].
- The NVENC session check in `EpicRtcVideoEncoderInitializer.cpp` (lines 111-135) applies to H264 and AV1 selections, and to every codec when `NegotiateCodecs` is on [LOCAL].
- **So on this card, H264 is the only real hardware option.** AV1 would go to the `FVideoResourceCPU` software path if the AV1 hardware check fails (`CreateEncoder` picks hardware first, then software) [LOCAL, lines 18-35]. CPU AV1 at 1080p60 on a 5700X is probably too slow (UNVERIFIED).
- VP8/VP9 use the software libvpx plugin (`LibVpxCodecs`) [LOCAL]. Same CPU concern.
- **Session overflow falls back to VP8 software.** If the NVENC session count reaches the cap (`MaxSessions`, or the driver count when `MaxSessions` is -1), the code sets the codec CVar to VP8 and broadcasts `OnFallbackToSoftwareEncoding` [LOCAL, `EpicRtcVideoEncoderInitializer.cpp` lines 111-134]. Epic: "In Pixel Streaming 2, any streams beyond the 8th will automatically switch to software encoders" [EPIC hosting guide].

Browser decode (MDN WebRTC codec page) [WEB]:
- H.264 Constrained Baseline: required in all fully-compliant WebRTC implementations. Chrome 52+, Edge, Firefox, Safari. Firefox on Android 73+ hardware. MDN says: "Support for AVC's Constrained Baseline (CB) profile is required in all fully-compliant WebRTC implementations."
- VP8: Chrome, Edge, Firefox, Safari 12.1+. Firefox 134+ for simulcast.
- VP9: Chrome 48+, Firefox. Firefox does not support VP9 simulcast by default.
- AV1: Chrome 113+, Firefox 136+. Safari support is not listed on the page (UNVERIFIED; third-party says hardware-gated).
- HEVC: Chrome 136+ only.
- Recommended codec: **H264, profile BASELINE.** Epic's original-plugin reference says BASELINE is "the only one guaranteed on all WebRTC receivers" [EPIC reference].

### 4.2 Rate control, quality and bitrate (PS2)

Forced by the code [LOCAL, `EpicRtcVideoEncoder.cpp` lines 170-183]:
- `RateControlMode = CBR` (no CVar in PS2 to change it).
- `MultipassMode = Quarter` (no CVar).
- `bFillData = false`.
- Bitrate target and max come from WebRTC congestion control: `TargetBitrate = CodecSettings._startBitrate`, `MaxBitrate = CodecSettings._maxBitrate`. So the `StartBitrate`, `MinBitrate` and `MaxBitrate` CVars control the encoder's real rate.
- `PixelStreaming2.Encoder.TargetBitrate` (default -1) is not used in `SetInitialSettings`.
- NVENC constants when no target is set: target 1 Mbps, max 10 Mbps [LOCAL, `VideoEncoderConfigNVENC.cpp` lines 90-91].
- VBV buffer = one frame at the target bitrate (`vbvBufferSize = averageBitRate / fps`) [LOCAL, line 152]. Low latency, but it can cost sharpness on busy frames (estimate).
- Adaptive quantisation on (`enableAQ = 1`) [LOCAL, line 149].

QP limits from quality (H.264, 51 max; AV1 uses 255) [LOCAL, `VideoEncoderConfigNVENC.cpp` lines 122-148]:
- `MinQP = (1 - MaxQuality/100) * 51`
- `MaxQP = (1 - MinQuality/100) * 51`
- `enableMinQP = MaxQuality < 100`; `enableMaxQP = MinQuality > 0`.
- Defaults (MinQuality 0, MaxQuality 100) give the full QP range 0-51 and no clamp.
- Lower `MaxQuality` raises the floor on QP, which lowers the best quality you can get and lowers bitrate. Higher `MinQuality` lowers the ceiling on QP, which raises the worst quality and bitrate. The CVar text says the same.

### 4.3 Presets and latency

- `QualityPreset` controls the NVENC tuning [LOCAL, `VideoEncoderConfigNVENC.cpp` lines 105-120]:
  - `Lossless` maps to `NV_ENC_TUNING_INFO_LOSSLESS` and forces ConstQP.
  - `HighQuality` maps to `NV_ENC_TUNING_INFO_HIGH_QUALITY`.
  - `LowQuality` maps to `NV_ENC_TUNING_INFO_LOW_LATENCY`.
  - `UltraLowQuality` **and** `Default` (the `default:` case) map to `NV_ENC_TUNING_INFO_ULTRA_LOW_LATENCY`.
- `LatencyMode` is copied into the encoder config but is not read by the NVENC encode setup [LOCAL, grep of `NVCodecs` and `PixelStreaming2RTC`]. Its runtime effect is UNVERIFIED; treat it as inert.
- Keyframes: default -1 (none). For late viewers, a fixed interval helps. Changing it re-inits NVENC [LOCAL text].
- Frame rate: `WebRTC.Fps` default 60. `DecoupleFramerate` default false = "Coupled means only stream what we render" [LOCAL]. **The stream is only as fast as the game.** At 40 fps the stream is about 40 fps. Raising game fps is the real latency and smoothness lever; decoupled mode repeats frames (per the original plugin's reference, "duplicate frames are sent if the app can't keep up" [EPIC reference]).
- Audio: `DisableAudioSync` true (default; good for latency). `AudioGain` 1.0.
- Frame dropper: on by default (`DisableFrameDropper` false). Keep on for stable pacing (estimate).
- `NegotiateCodecs` false is important. If true, `CodecPreferences` (AV1 first by default) is offered, and a browser may pick AV1 (section 4.1).
- Simulcast: `EnableSimulcast` false. PS2 derives its layers from `WebRTC.MinBitrate`/`MaxBitrate` [LOCAL, `UtilsVideo.h` lines 20-50]. The `-SimulcastParameters` switch from the SFU README is not read by PS2 (it exists only in the original plugin) [LOCAL].
- 4:4:4: the 3060 can encode H.264 4:4:4 (NVIDIA page), and PS2 has a `HIGH444` profile option. Browser decode of 4:4:4 H.264 is not covered by MDN and is almost certainly not usable by a browser. Do not use (estimate).
- Hardware vs software: H.264 uses NVENC on NVIDIA. Software is only the fallback (above).

### 4.4 Frontend-side quality settings

From `Frontend/library/src/Config/Config.ts` [REPO] (defaults):
- `MinQP` 0 (0-51), `MaxQP` 51 (0-51), `MinQuality` 0, `MaxQuality` 100.
- `WebRTCFPS` 60 (1-999), `WebRTCMinBitrate` 0 and `WebRTCMaxBitrate` 0, in **kbps** (0-500000).
- `PreferredCodec`: first supported H264 by default.
- `ViewportResScale` 1.0 (with `MatchViewportRes`).
- How the browser values reach the streamer is UNVERIFIED (the Config file defines them; the sending code was not read).

### 4.5 Recommended starting values (my estimates unless sourced)

No Epic page gives recommended PS2 values for 1080p. The third-party performance guide has no numbers. The values below are starting points to test, not published values.

| Setting | LAN, sharp 1080p | Internet, 1080p | Basis |
|---|---|---|---|
| Codec / profile | H264 / BASELINE | H264 / BASELINE | Sections 4.1 (verified) |
| QualityPreset | HIGH_QUALITY | DEFAULT | Tuning mapping verified; latency trade-off estimate |
| StartBitrate | 15 Mbps | 5 Mbps | Estimate |
| MinBitrate | 2 Mbps | 1 Mbps | Estimate |
| MaxBitrate | 30 Mbps (default 40) | 10-12 Mbps | Estimate |
| MinQuality / MaxQuality | 0 / 100 | 0 / 90 (to trim peaks) | Formulas verified; levels estimate |
| KeyframeInterval | -1 (test 120 for late joiners) | 120 | Estimate |
| WebRTC.Fps | 60 | 60 | Verified default |
| DecoupleFramerate | false | false | Verified default |
| EnableFlexFec | false | test true | Verified default; internet use is estimate |
| NegotiateCodecs | false | false | Verified default |
| EnableSimulcast | false | false (true only with SFU) | Verified |
| DisableAudioSync | true | true | Verified default |
| MinPort / MaxPort | default | narrow range, e.g. 50000-50100 | Verified CVar; forward these UDP ports |

Per-viewer cost (estimate): one encoder session per stream on the PC (Epic's "beyond the 8th" statement), and bandwidth per viewer roughly the negotiated bitrate, 5-15 Mbps at these settings. CPU decode cost is on the viewer.

---

## 5. Several people at once

### 5.1 Two browsers, one game instance (direct connections)

- All viewers of one streamer get the same game session. There is one player character and one view.
- Input control: `PixelStreaming2.InputController`.
  - `Any` (default): every viewer can send input [LOCAL, `EpicRtcStreamer.cpp` line 467].
  - `Host`: only the first non-SFU viewer gets input; later viewers are view-only [LOCAL, lines 805, 2156].
- Quality control: the PS2 source says "Even though the QualityController feature is removed we send it for backwards compatibility" and always sends `QualityControlOwnership = 1` [LOCAL, lines 1058-1060]. So quality control is no longer a per-viewer role in PS2. The effect in the frontend (`ControlsQuality`, default true) is UNVERIFIED.
- Each viewer gets its own WebRTC peer. Epic's text says streams beyond the 8th go to software (section 4.1). Treating each viewer as its own encoder stream is an inference from that text and the per-stream session check (UNVERIFIED at runtime).

### 5.2 SFU for many viewers of one game

- Epic: the SFU "takes the stream from the Unreal app and forwards it to viewers' browsers, adjusting quality to each viewer's network". Simulcast tiers default to full, half and quarter. "The SFU feature of Pixel Streaming is experimental" [EPIC hosting guide].
- The PC then sends one stream (or a few simulcast layers) to the SFU, not one encode per viewer. This is the expected way to keep NVENC session count flat (inference from the design; UNVERIFIED).
- Needs: the SFU process from `sfu-UE5.8-1.1.2`, a streamer with `EnableSimulcast` on (H264 or VP8), and SFU `subscribeStreamerId` matching the streamer ID [REPO SFU README].
- The SFU README's Docker example is `docker run -e SIGNALLING_URL=ws://192.168.1.10:8889 --network="host" ghcr.io/epicgames/pixel-streaming-sfu:5.4` [REPO]. Only the 5.4 image name appears in the README. Use the 5.8 tag (UNVERIFIED).

### 5.3 One game per visitor

- The Matchmaker was removed in 5.5 because it "was not a good refernce for how to handle Pixel Streaming at scale" [REPO].
- Epic's guidance: "Ideally, your scaling solution should provision a new Unreal Engine instance instantly whenever a user connects" [EPIC hosting guide].
- So the design is: an orchestrator (yours) launches one UE process per visitor with a unique `-PixelStreamingID=<id>`, and the browser is pointed at that streamer with the frontend `StreamerId` parameter [REPO Config.ts]. The orchestrator-to-signalling wiring is UNVERIFIED because Wilbur's routing was not read.
- **GPU limit for this PC:** one RTX 3060 is realistically one playable instance. The game already uses the card at 40 fps at 1080p, so a second instance will take GPU time from the first (estimate; not measured). NVENC is 12 or 8 sessions per GPU (section 4.1), and the 8th-plus stream falls to CPU VP8. Plan several visitors on several GPUs or cloud GPUs.

---

## 6. Reaching it from the internet

### 6.1 Secure-context facts (MDN) [WEB]

- `localhost` and `127.0.0.0/8` are potentially trustworthy (secure). `http://127.0.0.1` works for these features.
- `http://` on a LAN or public IP is not a secure context. Per MDN's rules only `https`, `wss`, `file`, loopback and `*.localhost` count (the LAN case is an inference from the listed rules).
- Restricted to secure contexts per the MDN page: `getUserMedia` (camera and microphone), **Gamepad API**, **Async Clipboard API**.
- Not listed on the MDN page: Pointer Lock API, Fullscreen API, WebRTC, WebSocket, Keyboard API by name (`navigator.keyboard.lock` not stated).
- Pointer lock does not need a secure context per MDN's list, but it needs a user gesture (click). Epic's `MouseControllerLocked.ts` requests lock from a click on the video parent [REPO].
- Epic's `KeyboardController.ts` does not call `navigator.keyboard.lock` [REPO].
- So over plain HTTP on a LAN or public IP: video and mouse work (UNVERIFIED in Chrome); gamepad and clipboard do not work.
- Mixed content (estimate; general browser behaviour, not fetched): an HTTPS player page can only open `wss://` signalling. The frontend builds `ss` from the page protocol, so an HTTPS page automatically uses `wss` [REPO Config.ts, `ss` default].

### 6.2 Options

**(a) LAN only.** No port forwarding, no TURN, no TLS needed for video. Quality: best. Security: nothing exposed. Limit: only your network.

**(b) Port forwarding to the PC.** Forward TCP 80 (or 443 with TLS) to the PC's player port. Forward a small UDP range that matches `-PixelStreamingWebRTCMinPort`/`MaxPort` (for example 50000-50100). Use the PC's public IP (dynamic DNS if the ISP changes it). STUN is needed for the public address (default `stun.l.google.com:19302` in the start scripts, or your own; `--peer_options` carries it) [REPO].
- Quality: direct UDP, lowest latency.
- Risk: the PC's ports are reachable from the internet. Never forward 8888 (streamer, which would let anyone register a fake streamer) or 8889 (SFU).
- Works when the viewer's NAT is not strict. Fails for carrier-grade NAT and some corporate networks (-> option c) (estimate).

**(c) TURN relay (coturn) on a server.** For viewers behind strict NAT. Media is relayed through the TURN server.
- Epic: "The page recommends CoTURN" and provides scripts [EPIC hosting guide]. The infrastructure repo has a `v4.7.0-coturn-windows` tag and `start_turn.bat` / `start_with_turn.bat` [REPO].
- Credentials: `--turn_secret` (or `--turn_secret_file`) must match coturn's `static-auth-secret`; `--turn_ttl` sets lifetime [REPO README].
- Quality: adds a hop, so more latency, and bandwidth cost on the relay. A third-party source says TCP fallback "degrades voice quality severely" (Mumble, not Pixel Streaming) [3P].
- Managed alternative (Cloudflare Realtime TURN) [WEB]: `turn.cloudflare.com`; TURN UDP 3478 and 443 (alternate), TCP 3478 and 80, TLS 5349 and 443. Free with the Realtime SFU; otherwise $0.05 per real-time GB outbound. Limits: roughly 50-100 Mbps per allocation. Wiring it into Wilbur's `--peer_options` needs Cloudflare's credential API (UNVERIFIED).

**(d) Tunnels.**
- **Cloudflare Tunnel** (`cloudflared`): the page fetched says a tunnel "can connect HTTP web servers, SSH servers, remote desktops, and other protocols safely to Cloudflare". It does not state UDP support for public hostnames [WEB]. A third-party source says public hostnames only proxy HTTP/HTTPS and that UDP is an open feature request (UNVERIFIED). Treat as: good for the HTTPS player and WSS signalling, **not** for WebRTC media. Media then needs option (b) or (c). Benefit: no inbound ports, no public IP needed for the page.
- **Tailscale Funnel** [WEB]: "Funnel can only listen on ports `443`, `8443`, and `10000`"; "Funnel only works over TLS-encrypted connections"; "Tailscale does this using a TCP proxy and Funnel relay servers"; "Traffic sent over a Funnel is subject to non-configurable bandwidth limits"; "Tailscale Funnel is currently in beta". No UDP. Use for the player page only, not media.
- **Tailscale or ZeroTier (overlay, invited people)**: point-to-point UDP over an overlay, which suits media. A search snippet says Tailscale's comparison page lists it as supporting any IP protocol (UNVERIFIED, not fetched). Usable only for invited people. Relay fallback path and quality UNVERIFIED here.
- **ngrok**: TCP endpoints exist. Sources disagree on UDP: a dev.to guide and LocalXpose say no native UDP, one dev.to post says yes (3P). Not verified against ngrok's docs (UNVERIFIED). Even TCP would carry only the page and signalling, not WebRTC UDP media.

**(e) Cloud VM for signalling and TURN; game stays at home.**
- The streamer connects outbound: `-PixelStreamingConnectionURL=wss://<your-vm>/...`. No inbound port on the home PC is needed for signalling.
- Media: the streamer uses TURN on the VM as its relay (option c). Home PC needs only outbound UDP/TCP.
- Quality: depends on the VM's region. Cost: relay bandwidth. Risk: the VM is public (small attack surface); the home PC is not.
- Caveat: end-to-end behaviour of an outbound streamer to a remote Wilbur is UNVERIFIED.

**(f) Cloud GPU instead of the home PC.** One instance per visitor on cloud GPUs. No home exposure. Costs scale with usage. Latency depends on region. Epic's hosting guide says a scaling solution should spin up instances on demand, and lists cloud providers generically [EPIC]. Specific providers and prices were not checked (UNVERIFIED).

### 6.3 Security risks of exposing the home PC

- Anyone who reaches port 80 can load the player page and try to connect. Use `--max_players` and the keepalive timeout to limit it.
- Port 8888 (streamer) and 8889 (SFU) must stay on LAN. Otherwise anyone can register a streamer.
- `PixelStreaming2.AllowPixelStreamingCommands` default false. Keep it false (browser cannot run UE console commands) [LOCAL].
- Public IP is revealed to viewers through STUN and the ICE candidates. `start.bat` also sends a request to `api.ipify.org`. Set your own STUN or peer options if that matters (estimate).
- TURN demo credentials in `common.bat` (`PixelStreamingUser`/`AnotherTURNintheroad`) must be replaced [REPO].
- Use a dedicated low-privilege Windows account for the game and signalling (estimate).

---

## 7. The browser side

### 7.1 Default frontend

- Reference player source: `Frontend/implementations/typescript/` with `player.html`/`player.ts`, `uiless.html`/`uiless.ts`, `showcase.html`/`showcase.ts`, `stresstest.html`/`stresstest.ts` [REPO listing].
- Libraries: `Frontend/library` (`lib-pixelstreamingfrontend`) and `Frontend/ui-library` (`lib-pixelstreamingfrontend-ui`). Published as npm packages, e.g. `@epicgames-ps/lib-pixelstreamingfrontend-ue5.8` [REPO README].
- Build (typescript README): `cd Frontend/implementations/typescript`, `npm install`, `npm run build`, after building `Frontend/library` and `Frontend/ui-library`. Output goes to `SignallingWebServer/www`. Run: `cd SignallingWebServer/platform_scripts/cmd` then `start.bat`, then open `http://localhost` [REPO].
- Customise: study `/ui-library`, `player.ts`/`player.html` or `implementations/react` [REPO]. The play button and title live in the page HTML (estimate, the file was not read for the exact element).

### 7.2 URL parameters (`Frontend/library/src/Config/Config.ts`, UE5.8) [REPO]

Flags (default):
- `AutoConnect` (false): connect without a click-to-start prompt.
- `AutoPlayVideo` (true).
- `StartVideoMuted` (false).
- `HoveringMouse` (false): **false = locked mouse** (FPS mode); true = hovering.
- `MatchViewportRes` (false): resize the stream to the video element. `ViewportResScale` 1.0.
- `KeyboardInput` (true), `MouseInput` (true), `GamepadInput` (true), `TouchInput` (true), `XRControllerInput` (true).
- `FakeMouseWithTouches` (false), `MouseDoubleClickAutoRelease` (true).
- `SuppressBrowserKeys` (true): suppresses F1-F12 and Tab. `F5` is suppressed; `Esc` and `Ctrl+W` are not (per `KeyboardController.ts`).
- `WaitForStreamer` (true), `TimeoutIfIdle` (false) with `AFKTimeout` 120 s and `AFKCountdown` 10 s.
- `ForceTURN` (false): only relayed candidates. Useful to test TURN.
- `ForceMonoAudio` (false), `UseMic` (false), `UseCamera` (false).
- `ControlsQuality` (true), `LatencyCSV` (false), `HideUI` (false).

Numbers: `WebRTCFPS` 60; `WebRTCMinBitrate`/`WebRTCMaxBitrate` 0 (kbps); `MinQP` 0 / `MaxQP` 51; `MinQuality` 0 / `MaxQuality` 100; `MaxReconnectAttempts` 3; `KeepaliveDelay` 30000 ms.

Options: `PreferredCodec` (first H264 the browser supports), `StreamerId` (default ''), `PreferredQuality` (only with SFU).

Text: `ss` = signalling URL; defaults to `ws://` or `wss://` by page protocol, plus page hostname, plus port unless 80 or empty.

Example for this PC (LAN, sharp):
```
http://<PC-IP>/?AutoConnect=true&AutoPlayVideo=true&StartVideoMuted=true&HoveringMouse=false&MatchViewportRes=true&KeyboardInput=true&MouseInput=true&GamepadInput=true&PreferredCodec=H264&StreamerId=DefaultStreamer
```
- `StartVideoMuted=true` is a common way to get autoplay past browser policy (estimate; the browser rule was not fetched).
- `MatchViewportRes` changes the encoder size as the window changes (possible re-init cost; UNVERIFIED).

### 7.3 Pointer lock and first-person mouse look

- `MouseControllerLocked.ts`: lock requested only from a `click` on the video parent (`requestPointerLock()`); exit on unlock; sends `KeyUp` for held keys on unlock [REPO].
- Movement uses `event.movementX` and `event.movementY` (relative deltas), not `clientX/Y` [REPO].
- Each `mousemove` sends one `MouseMove` message immediately with `[normalizedX, normalizedY, deltaX, deltaY]`. No batching, no throttling, no `requestAnimationFrame` [REPO].
- The request is `requestPointerLock()` with **no** `{ unadjustedMovement: true }` option. So OS mouse acceleration applies. For FPS aim this can feel uneven. MDN's Pointer Lock example requests `{ unadjustedMovement: true }`, and its comment says "Some platforms may not support unadjusted movement" [WEB]. On the PC, turning off "Enhance pointer precision" is an easy test (UNVERIFIED effect on the stream).
- No latency comments in Epic's code. No open issue about mouse latency was found in the first page of the tracker (only dependency bumps, UE5.6-5.8 backports) (partial check).
- `HoveringMouse=true` only sends the cursor after a click. Not wanted for FPS.

### 7.4 Fullscreen

- The Fullscreen API is not in MDN's secure-context list [WEB]. The frontend readme mentions "a left-side control that maximizes the video" (UNVERIFIED as a fullscreen implementation).

---

## 8. Known problems in 5.5 to 5.8, and session issues

### 8.1 Maturity

- Epic: the SFU is experimental [EPIC hosting guide].
- Epic PS2 overview: Blueprint nodes and the C++ API changed; redirectors do not work because the plugin name changed [EPIC].
- Matchmaker removed in 5.5 [REPO].
- Third-party (Eagle 3D Streaming, not Epic) reports: PS2 browser-to-UE messaging not supported, and "video not starting on 5.5/5.6" for some regions with PSP2 (3P, UNVERIFIED for 5.8).

### 8.2 RenderOffScreen

- Epic recommends `-RenderOffscreen` for headless use and says it avoids stream loss when minimised [EPIC].
- Epic's Pixel Streaming toolbar page says editor streaming with off-screen rendering is "experimental and may be unstable" (secondhand, via search snippet; UNVERIFIED).
- No 5.8-specific RenderOffScreen bug was found. The tracker check covered only the first page of issues, which were dependency bumps (partial).

### 8.3 Audio

- `-AudioMixer` for no audio device (Epic reference, original plugin) [EPIC].
- PS2 audio CVars: `DisableTransmitAudio` false, `AudioGain` 1.0, `DisableAudioSync` true, `DumpDebugAudio` (debug) [LOCAL].
- A Windows session with no audio endpoint (see 8.5) may need `-AudioMixer` (UNVERIFIED). Test.

### 8.4 NVENC driver and session limits

- NVENC support: RTX 3060 row, H264 and HEVC encode yes, AV1 encode no [WEB].
- Session cap: 12 (NVIDIA page) vs 8 (UE code comment and Epic hosting guide) (section 4.1).
- The UE code counts sessions through NVML (`NvmlEncoder::GetEncoderSessionCount(0)`), so it reads the real count [LOCAL, `EpicRtcVideoEncoderInitializer.cpp` line 110].
- Minimum driver version: not found (UNVERIFIED). Keep the current NVIDIA driver.

### 8.5 Session 0, SSH and GPU/audio

- Microsoft: "All services run in Terminal Services session 0. Therefore, if an interactive service displays a user interface, it is visible only to the user who connected to session 0." Services "cannot directly interact with a user as of Windows Vista" [WEB, Interactive services].
- Microsoft: `schtasks /create ... /it` "Specifies to run the scheduled task only when the run as user (the user account under which the task runs) is logged on to the computer." Use with `/ru`. `/sc onlogon` runs at logon. `schtasks /run` runs it now [WEB, schtasks create].
- Third-party (vendor guide, UNVERIFIED on this PC): processes started over OpenSSH inherit a non-interactive session, so window and screenshot APIs see nothing. The fix is to launch from an interactive-session task. A Microsoft Q&A reply suggests `psexec -i` gave crashes or blank windows (3P).
- **Practical approach:**
  1. Enable automatic logon for the Windows user (or keep the user logged in) so the desktop session exists after reboot (standard Windows setting; UNVERIFIED here).
  2. Create the task from SSH, for example: `schtasks /create /tn "Ephemera PS" /tr "F:\Ephemera\launch_ps.bat" /sc onlogon /ru <user> /it /rl highest /f`, where `launch_ps.bat` holds the launch command in section 3.1 (quoting and `/rl highest` UNVERIFIED on this build).
  3. Start it with `schtasks /run /tn "Ephemera PS"`.
  4. Check: `query session` shows the console session; `tasklist /v` shows the game's session ID. The game's session must match the console session.
- Sign-out or RDP disconnect may end the desktop session. Lock is different (standard Windows behaviour; UNVERIFIED for this GPU path).

### 8.6 Windows Firewall

- First run of a new executable prompts for network access. Allow private networks. To allow ports in advance: `netsh advfirewall firewall add rule` (standard syntax; not run here, UNVERIFIED). Open only TCP 80 (and 443 if used) and the chosen UDP range.

---

## Unverified items (summary)

- `F:\UE_5.8` on the PC contains `Engine\Plugins\Media\PixelStreaming2`. Not inspected.
- Asset filenames of the release `UE5.8-0.1.0`.
- The `Matchmaker` folder in the UE5.8 branch: README says removed; contents not read.
- Location of Wilbur's `config.json`; any bind-address flag; any allowed-origins flag for the external server.
- Exact `--peer_options` JSON shape for ICE servers.
- Whether `-RenderOffscreen` and `-RenderOffScreen` differ (believed case-insensitive).
- Whether the PS2 `DecoupleFrameRate` / `DecoupleFramerate` spelling matters on the command line.
- Whether PS2 needs `-AudioMixer` in a non-interactive session.
- Which NVENC session cap applies (12 or 8) on this card and driver.
- AV1 hardware path on the 3060 (none) and software AV1/VP9 CPU cost at 1080p60.
- Whether `LatencyMode` changes NVENC behaviour at runtime.
- Whether browser-sent `WebRTCMinBitrate`/`MaxBitrate`/`MinQP` settings reach the streamer.
- Pointer lock and fullscreen behaviour over plain HTTP in each browser.
- Safari AV1 decode.
- Cloudflare Tunnel: UDP for public hostnames, WebSocket behaviour.
- ngrok UDP support.
- Cloudflare TURN wiring into Wilbur.
- Orchestrator-to-Wilbur routing for `StreamerId`.
- Single GPU capacity for one instance at 40 fps and multiple visitors (no measurement).
- Keyboard layout handling (AZERTY etc.) through the frontend.
- Windows firewall prompt details and `netsh` rule syntax on this build.
- `schtasks ... /rl highest` quoting in a real run.

---

## Source index

Epic documentation (dev.epicgames.com):
- Getting Started with Pixel Streaming (5.8): https://dev.epicgames.com/documentation/en-us/unreal-engine/getting-started-with-pixel-streaming-in-unreal-engine
- Pixel Streaming reference (original plugin): https://dev.epicgames.com/documentation/unreal-engine/unreal-engine-pixel-streaming-reference
- Pixel Streaming 2 overview: https://dev.epicgames.com/documentation/en-us/unreal-engine/pixel-streaming-2-overview-in-unreal-engine
- Hosting and networking guide: https://dev.epicgames.com/documentation/en-us/unreal-engine/hosting-and-networking-guide-for-pixel-streaming-in-unreal-engine
- Pixel Streaming plugin index: https://dev.epicgames.com/documentation/unreal-engine/API/PluginIndex/PixelStreaming2

Infrastructure repo (EpicGamesExt/PixelStreamingInfrastructure, branch UE5.8):
- Repo: https://github.com/EpicGamesExt/PixelStreamingInfrastructure
- Branch and tag listings: https://api.github.com/repos/EpicGamesExt/PixelStreamingInfrastructure/branches and `/tags`
- Signalling README: https://raw.githubusercontent.com/EpicGamesExt/PixelStreamingInfrastructure/UE5.8/SignallingWebServer/README.md
- Windows start/common scripts: `.../UE5.8/SignallingWebServer/platform_scripts/cmd/start.bat`, `common.bat`, `start_with_stun.bat`
- Node version: `.../UE5.8/NODE_VERSION` (= v22.14.0); release version: `.../UE5.8/RELEASE_VERSION` (= 0.1.0)
- SFU README: `.../UE5.8/SFU/README.md`
- Matchmaker README: `.../UE5.8/Matchmaker/README.md` (says removed)
- Frontend README: `.../UE5.8/Frontend/README.md`; typescript: `.../UE5.8/Frontend/implementations/typescript/readme.md`
- Frontend config: `.../UE5.8/Frontend/library/src/Config/Config.ts`
- Frontend mouse: `.../UE5.8/Frontend/library/src/Inputs/MouseControllerLocked.ts`
- Frontend keyboard: `.../UE5.8/Frontend/library/src/Inputs/KeyboardController.ts`
- STUN/TURN wiki (no content found): https://github.com/EpicGames/PixelStreamingInfrastructure/wiki/STUN-TURN-Configuration

Local engine (`/Volumes/External/code/tests/outerwilds/.unreal/UE_5.8/Engine/`):
- `Plugins/Media/PixelStreaming/PixelStreaming.uplugin`
- `Plugins/Media/PixelStreaming2/PixelStreaming2.uplugin`
- `Plugins/Media/PixelStreaming2/Config/DefaultPixelStreaming2.ini`
- `Plugins/Media/PixelStreaming2/Resources/WebServers/get_ps_servers.bat` and `.sh`
- `Plugins/Media/PixelStreaming2/Source/PixelStreaming2Settings/Private/PixelStreaming2PluginSettings.cpp` (CVars lines 240-980; CLI tables lines 165-240; command-line parsing lines 1234-1460)
- `Plugins/Media/PixelStreaming2/Source/PixelStreaming2Settings/Internal/PixelStreaming2PluginSettings.h` (tooltips)
- `Plugins/Media/PixelStreaming2/Source/PixelStreaming2RTC/Private/EpicRtcStreamer.cpp` (lines 169, 467, 805, 1058-1060, 2156)
- `Plugins/Media/PixelStreaming2/Source/PixelStreaming2RTC/Private/EpicRtcVideoEncoder.cpp` (lines 160-200: CBR, multipass, presets)
- `Plugins/Media/PixelStreaming2/Source/PixelStreaming2RTC/Private/EpicRtcVideoEncoderInitializer.cpp` (lines 18-140: encoder choice, NVENC session fallback to VP8)
- `Plugins/Media/PixelStreaming2/Source/PixelStreaming2/Internal/UtilsVideo.h` (simulcast layers)
- `Plugins/Experimental/AVCodecs/AVCodecsCore/Source/AVCodecsCore/Public/AVConfig.h` (EAVPreset, EAVLatencyMode)
- `Plugins/Experimental/AVCodecs/NVCodecs/Source/NVENC/Private/Video/Encoders/Configs/VideoEncoderConfigNVENC.cpp` (tuning mapping lines 102-124, QP lines 122-148, constants lines 90-91)
- `Plugins/Media/PixelStreaming2/Source/PixelStreaming2Editor/Private/PixelStreaming2EditorModule.cpp` (editor-only `-PixelStreamingID=`)

Project (read only): `/Volumes/External/code/tests/outerwilds/unreal/Ephemera.uproject` (EngineAssociation 5.8; no PixelStreaming plugin yet; no PixelStreaming config; no PixelStreaming source).

Web (official):
- NVIDIA video encode/decode matrix: https://developer.nvidia.com/video-encode-and-decode-gpu-support-matrix-new
- MDN, WebRTC codecs: https://developer.mozilla.org/en-US/docs/Web/Media/Guides/Formats/WebRTC_codecs
- MDN, Secure Contexts: https://developer.mozilla.org/en-US/docs/Web/Security/Secure_Contexts
- MDN, features restricted to secure contexts: https://developer.mozilla.org/en-US/docs/Web/Security/Secure_Contexts/features_restricted_to_secure_contexts
- MDN, Pointer Lock API: https://developer.mozilla.org/en-US/docs/Web/API/Pointer_Lock_API
- Cloudflare Realtime TURN: https://developers.cloudflare.com/realtime/turn/
- Cloudflare Tunnel: https://developers.cloudflare.com/cloudflare-one/networks/connectors/cloudflare-tunnel/
- Tailscale Funnel: https://tailscale.com/kb/1223/funnel
- Microsoft, schtasks create: https://learn.microsoft.com/en-us/windows-server/administration/windows-commands/schtasks-create
- Microsoft, Interactive services: https://learn.microsoft.com/en-us/windows/win32/services/interactive-services

Third-party (secondary only): Eagle 3D Streaming docs and blogs (mouse, PS2 compatibility, performance guide with no numbers); dev.to tunnel comparisons; Microsoft Q&A and vendor guide on OpenSSH sessions.
