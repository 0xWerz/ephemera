Ephemera's Unreal Engine 5.8 source migration lives here. The installer checks Epic's
live Mac catalog for the latest stable release (5.8.3 verified on October 8, 2026).
Emberwake and the existing
Godot version remain available through their original launch scripts.

**Status: playable development port; migration in progress.** Unreal Engine 5.8.3 is installed in this
workspace. All 264,429 selected engine files passed Epic manifest hash checks,
and a headless editor startup executed Python successfully with temporary files
on the external drive. The Godot scene export and standalone C++ simulation
checks have also passed. EphemeraEditor builds successfully with the bundled
.NET 10 SDK and existing Xcode 26.0.1 toolchain. All 816 meshes and 163 audio
assets (WAV effects and Ogg music) are imported, with the starting map saved.
Both native automation tests and thirteen rendered runtime checks pass. The
runtime check verifies spawning, ground collision, keyboard walking, the
Inheritance panel, loop reset, and a rendered screenshot. Gameplay migration
remains incomplete, and a standalone packaged application has not been verified.

The exported snapshot contains:

- Aubade, all seven main worlds, and the seven moving Chorus islands.
- 816 GLB mesh assets, 24,821 rendered instances, and 436 collision shapes.
- 66 interaction points, 81 local lights, and six placard dialogue actions.
- All 65 Inheritance entries, 28 dialogue trees, and 29 writing records.
- Original orbits, gravity, water, oxygen, depth-dependent time dilation, and spawn transforms.

The native C++ implementation includes:

- Deterministic orbits and reference-frame transport, with Unreal's double-precision
  world coordinates and native directional CharacterMovement gravity.
- Separate world and proper-time clocks; Slow Deep dilation; loop resets;
  persistent knowledge and temporary loop flags.
- Walking, jumping, jetpack movement, oxygen, fuel, impact damage, and a six-axis
  Seedling controller with thrust spooling, velocity matching, and hull damage.
- UMG interfaces for conversations, reading, the Inheritance, pause, and a basic
  distance chart. Dialogue conditions select morning, afternoon, or evening branches.
- The launch winch, probe entanglement and pickup, quantum Eyestones and tree-ring
  movement, handheld photographs, antenna signal identification, marshmallows,
  discoveries, and the primary nightfall condition.
- Hierarchical instancing for repeated scenery, native collision components, and
  Unreal material graphs for animated water, flames, Aubade, Chorus bands and rings,
  and a procedural starfield. A Material Parameter Collection drives resonance.
- Lumen software GI/reflections and TSR configured in DefaultEngine.ini.
  Nanite can be enabled for eligible meshes during import.

The runtime checks cover the initial spawn and the behaviors listed above;
the complete game and performance improvements remain to be playtested. Unreal supports the
[directional movement gravity](https://dev.epicgames.com/documentation/unreal-engine/API/Runtime/Engine/GameFramework/UCharacterMovementComponent/SetGravityDirection?application_version=5.8)
and [Enhanced Input](https://dev.epicgames.com/documentation/en-us/unreal-engine/enhanced-input-in-unreal-engine)
used here. Nanite and rendering support depend on hardware; consult
[Epic's macOS requirements](https://dev.epicgames.com/documentation/en-us/unreal-engine/macos-development-requirements-for-unreal-engine).

**Run the pipeline from the workspace root.**

Run the prepared Mac installer from the workspace root:

    ./install_unreal.command

It prompts for Epic sign-in if needed, downloads Epic's Mac build through the
[Legendary CLI](https://github.com/derrod/legendary), and verifies installed file
hashes. Open the printed login link in your browser and paste its authorizationCode
into the terminal. Input is hidden; never paste passwords or codes into chat.
Complete Unreal account/license setup on Epic's website if your account needs it.

The install includes the Mac editor, C++ engine source, and templates. Optional
engine debug symbols and support files for other platforms are omitted. The
engine's ordinary editor, rendering, animation, physics, and plugin features
remain available. The selected Epic build is recorded in
.unreal/Installer/selected-release.json. The installer refuses to silently use an
older engine branch if Epic's catalog reports a newer stable branch.

The downloader and its Python environment are installed in .unreal/Installer/.
Download caches, temporary files, and private Epic session state are also inside
.unreal/. The Mac sandbox denies installer writes outside this workspace.
Existing Python, uv, Xcode, and system libraries are only read. The installer does
not install the Epic Games Launcher or global prerequisites. It does not launch a
browser; a browser you open yourself uses its own profile and storage settings.

The engine destination is:

    .unreal/UE_5.8/Engine/...

The launcher refuses engine installations outside this workspace because Unreal
also writes intermediates inside its own installation. The installed Mac engine
required approximately 16 GB of downloads and occupies approximately 45 GB.
Build products, imported assets, and shader caches need additional space here.

Check local status or sign in separately:

    ./install_unreal.command --status
    ./install_unreal.command --sign-in

Rerun the installer after interruption to resume. Repair an existing installation:

    ./install_unreal.command --repair

The source snapshot is already generated. After engine installation:

    ./play_unreal.sh build
    ./play_unreal.sh import
    ./play_unreal.sh test
    ./play_unreal.sh smoke
    ./play_ephemera_unreal.command

To opt into Nanite on supported hardware, use:

    ./play_unreal.sh import --nanite

An alternate engine location inside this workspace can be supplied with the
--engine option, or with UNREAL_ENGINE.

The import command creates Unreal assets and /Game/Maps/Aubade. It checkpoints
completed imports with source hashes, so interrupted imports can resume. Invisible
collision meshes retain their complete triangle geometry and skip Nanite and
distance-field generation. The completed manifest is published only after the
starting map saves successfully.

Double-click play_ephemera_unreal.command in the workspace root, or run
./play_unreal.sh play, to launch the current port directly in a game window.
./play_unreal.sh open opens the editor instead. The native GameMode constructs
the universe. Use these launchers to retain the storage restrictions.

The smoke command launches a rendered game, checks world construction, grounding,
walking, menu behavior, and loop reset, then saves Saved/Smoke/runtime.json and
Saved/Smoke/spawn.png and exits. Its save data is isolated from the player's save.

To refresh the export after changing the Godot game:

    python3 -B unreal/Tools/prepare.py
    python3 -B unreal/Tests/validate_migration.py
    ./play_unreal.sh import

The preparation tool builds a shadow project with a separate import cache and uses the
workspace's existing focus-safe Godot executable. It does not run the player's
Godot session. The importer checks an asymmetric calibration mesh before accepting
Unreal's glTF axis conversion and centimetre scale, then writes Content/Data/assets.json.
Meshes are imported in the editor and cooked as native assets; the packaged game
does not depend on runtime glTF import.

To package after the native build and gameplay verification:

    ./play_unreal.sh package

The package goes to unreal/Builds/.

**Storage boundary**

Generated assets, snapshots, compiler caches, temporary files, .NET/NuGet state,
logs, save files, test output, and build artifacts are directed into this workspace.
On this Mac, both launchers additionally use an OS sandbox that denies file writes
outside the workspace. Existing applications and system SDKs can be read.
The existing focus-safe Godot helper has a local _sc_ marker so its editor
settings can be stored beside that helper on the external disk.

The launcher reads the engine's bundled compressed cache and writes new derived
data to a project-local filesystem cache. Automatic Zen startup is disabled.
A dependency attempting to write elsewhere will fail
instead of silently using the internal drive.

Build, import, and headless test application support paths are redirected per
process through CFFIXED_USER_HOME; the user's HOME remains unchanged. Rendered
game/editor launches read the existing Cocoa registration for Apple's installed
Metal compiler. Their UserDir, settings, temporary files, DDC, and xcrun cache
remain external, and the same OS write guard applies. UBA uses a local cache.
.NET's named mutex storage is hardcoded to /tmp, so the build command creates a
separate copy of UnrealBuildTool under Engine/Binaries/DotNET/EphemeraBuildSupport.
It compiles Epic's installed EpicGames.Core sources with SingleInstanceMutex
adapted to workspace flock files. Original engine files remain unchanged. The
adapter preserves cross-process locking and cancellation, and releases locks
when processes exit or crash. IDE hot reload is disabled for this build command.
Use the launcher to build; direct invocation of Epic's Build.sh still uses its
original storage paths. Packaging and builds initiated inside the editor have
not yet been adapted or verified under the storage restriction.

**Validation performed**

Saved/validation.log records six passing checks:

- Exact world/island inventory and an error-free export manifest.
- Every group, mesh, and placement reference resolves; GLB headers and sizes are valid.
- Native simulation invariants compile and execute with C++17, warnings treated as errors.
- Forty native orbit samples match the actual Godot outputs within 3 mm.
- Terrain, procedural sun/flames, collision, dense scenery, and calibration geometry survive export.
- Dialogue destinations, writing references, story links, and transforms are valid.

The selected UE 5.8.3 engine files passed hash verification, and
.unreal/Installer/engine-smoke.json records a successful headless Python startup.
Saved/Build.log records a successful native Mac Development editor build.
Tests/validate_build_storage.py passed settings relocation, process exclusion,
cancellation, normal release, and crash-release checks under the write guard.

Saved/Automation/index.json records two passing Unreal automation tests for native
coordinate conversion and imported content, including complete collision geometry,
material Nanite/instancing flags, and music/antenna recordings. Saved/Smoke/runtime.json
records thirteen passing rendered runtime checks: fifteen bodies, 24,821 scenery
instances, all 436 exported colliders, spawn/player/ship/HUD, grounding, keyboard
walking, panel behavior, loop reset, and screenshot output. Saved/Smoke/spawn.png
was visually inspected; no material compilation failures or missing usage flags
were present in the final rendered smoke log. Smoke saves are separate from player saves.
Python tool syntax, the project descriptor, and the launch script syntax were also checked.

**Work still required before replacing the original game**

- Playtest all worlds, transitions, landings, loop resets, conversations, and the ending.
- Complete the deployed Scout, remote/backwards photography and its observation
  rules; the current native tool takes a handheld scene capture.
- Port the interactive three-dimensional Orrery, target selection, autopilot,
  landing camera, cockpit instruments, and full ship-interior behavior.
- Finish NPC head/limb/antenna animation, visual aging and cocoons, Gulper patrols
  and repulsion, lamp signalling, credits, and the complete alternate ending presentations.
- Replace the remaining Godot atmosphere/cloud/corona shaders and GPU particle
  systems with reviewed Unreal materials and Niagara effects. Niagara is enabled;
  authored Niagara systems are not supplied yet.
- Restore terrain texture-array detail, height blends, and normals. Terrain
  colors are currently baked from splat weights into vertex colors.
- Finish controller navigation, settings, glyphs, DualSense effects, accessibility,
  and the original context-sensitive control layout.
- Wire the remaining gameplay sound effects and finish the audio mix and transitions.
- Review lighting, translucency, exposure, sphere collision, and performance in
  Unreal on the target hardware. Nanite and Lumen do not establish a performance
  or visual improvement until these measurements exist.

The first Unreal controls are WASD, mouse look, Space/Ctrl vertical thrust,
Shift boost, E interact/leave seat, F lamp, Q/Z roll, R velocity match,
C handheld photograph, V/Y antennae, I Inheritance, M/Tab chart, and Esc pause.
Controller bindings use Enhanced Input; their final mapping remains to be playtested.

Original audio and terrain credits remain in ../ephemera/assets/audio/CREDITS.md
and ../ephemera/assets/textures/terrain/CREDITS.md. This migration does not change
the project's fan-game identity or the ownership of its source content.
