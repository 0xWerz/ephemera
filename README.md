# Ephemera

A photorealistic space exploration game in Unreal Engine 5.8, built with Claude Code. Walk around
Dawnhollow, an Iceland-like ocean planet of moss-covered lava, basalt cliffs and black-sand coves,
use your jetpack, and fly out to its moon.

![Ephemera](media/1_cover.jpg)

> **Note:** For now, Ephemera is a walking and flying sandbox. The focus is entirely on the world itself: graphics,
> environment design, lighting, sky and ocean, and how it feels to move through it.

## Download (Windows)

Get the latest release from the **[Releases page](../../releases/latest)**. Download **both** files
into the same folder, then run the `.exe`:

- `Ephemera-Setup-<version>.exe` (the installer)
- `Ephemera-Setup-<version>-1.bin` (the game's data; it must sit next to the installer)

It installs for your user only, with no administrator prompt. The installer isn't code-signed, so Windows
may show "Windows protected your PC". Click **More info**, then **Run anyway**.

**Requirements:** Windows 10/11 64-bit and a DirectX 12 graphics card. It's developed and tested on an
RTX 3060. On NVIDIA RTX cards you can switch the upscaler to DLSS in the settings menu for a big frame-rate
boost.

## Controls

| | Keyboard / mouse | Gamepad |
|---|---|---|
| Move / look | WASD / mouse | left / right stick |
| Jump (hold for the jetpack) | Space | A / ✕ |
| Jets down | Ctrl | left trigger |
| Boost | Shift | right bumper |
| Interact | E | X / □ |
| Lamp | F | right stick click |
| Free flight (scroll for speed) | V | D-pad up |
| Level the horizon in free flight | G | |
| Menu / FPS counter | Esc / F3 | |

## Screenshots

All in-game, rendered in real time in Unreal Engine 5.8.

![Dawnhollow and its moon](media/2_planet_and_moon.jpg)
![The bay](media/4_bay.jpg)
![Cliffs](media/6_cliffs.jpg)
![The moon](media/7_moon.jpg)

### Space

<table>
<tr><td><img src="media/gallery/space/01_eclipse_ring.jpg" width="100%"></td><td><img src="media/gallery/space/02_limb_sunrise.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/space/03_hush_and_dawnhollow.jpg" width="100%"></td><td><img src="media/gallery/space/04_crescent_over_hush.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/space/05_planet_rise.jpg" width="100%"></td><td><img src="media/gallery/space/06_hush_terminator.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/space/07_hush_whole.jpg" width="100%"></td><td><img src="media/gallery/space/08_coast.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/space/09_terminator.jpg" width="100%"></td><td><img src="media/gallery/space/10_dawnhollow.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/space/11_moon_limb.jpg" width="100%"></td><td><img src="media/gallery/space/12_terminator_clouds.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/space/13_coast_clouds.jpg" width="100%"></td><td><img src="media/gallery/space/14_dawnhollow_clouds.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/space/15_horizon_clouds.jpg" width="100%"></td><td><img src="media/gallery/space/16_eclipse_ring_clouds.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/space/17_from_hush.jpg" width="100%"></td><td><img src="media/gallery/space/18_hush_close.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/space/19_limb_sunrise.jpg" width="100%"></td><td><img src="media/gallery/space/20_low_orbit.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/space/21_planet_day.jpg" width="100%"></td><td><img src="media/gallery/space/22_planet_terminator.jpg" width="100%"></td></tr>
</table>

### On the surface

<table>
<tr><td><img src="media/gallery/surface/01_bay.jpg" width="100%"></td><td><img src="media/gallery/surface/02_bay_overcast.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/03_fjord_wall.jpg" width="100%"></td><td><img src="media/gallery/surface/05_hollow_rim.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/06_moss_heath.jpg" width="100%"></td><td><img src="media/gallery/surface/07_rift.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/08_tables_ground.jpg" width="100%"></td><td><img src="media/gallery/surface/09_tables_overcast.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/10_tables_plain.jpg" width="100%"></td><td><img src="media/gallery/surface/11_tables_plain_low.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/12_tables_wide.jpg" width="100%"></td><td><img src="media/gallery/surface/16_hollow_aerial.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/17_table_cliffs.jpg" width="100%"></td><td><img src="media/gallery/surface/18_table_cliffs_wide.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/25_planet.jpg" width="100%"></td><td><img src="media/gallery/surface/26_hollow_top.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/27_meadow_day.jpg" width="100%"></td><td><img src="media/gallery/surface/28_sea.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/29_meadow_night.jpg" width="100%"></td><td><img src="media/gallery/surface/30_lamp_at_night.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/35_basalt_valley.jpg" width="100%"></td><td><img src="media/gallery/surface/36_basalt_grassland.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/37_rock_outcrop.jpg" width="100%"></td><td><img src="media/gallery/surface/38_spawn_slope.jpg" width="100%"></td></tr>
<tr><td><img src="media/gallery/surface/39_bay_clear.jpg" width="100%"></td></tr>
</table>

## Source code

The full source is here, under the MIT licence (see `LICENSE`), so anyone can learn from it, improve it and
build on it.

- `unreal/Source/`: the game's C++ (Unreal Engine 5.8): the solar system, gravity and reference frames, the
  player and jetpack, sky, sea, sound and UI.
- `unreal/Tools/`: the Python and shell tools that build the game's content from scratch: terrain
  generation, materials and shaders (`build_slice.py`), wave and sound bakes, packaging and the installer.
- `unreal/Config/`, `unreal/Ephemera.uproject`: the Unreal project.
- `docs/unreal/DIRECTION.md`: the design and technical decisions, and why. `docs/research/`: the research
  notes behind them.

Not included: the imported assets (`Content/`, several GB), which the tools rebuild. Textures come from
Poly Haven (CC0, re-downloaded by `unreal/Tools/fetch_polyhaven.py`). The sound recordings come from the
Sonniss GDC Game Audio Bundles, which are free to download but may not be redistributed as files, so they
aren't here: `unreal/SourceAssets/audio/CREDITS.md` lists which ones are used. See `unreal/README.md` for
building it.

## Credits

- Textures and surfaces: [Poly Haven](https://polyhaven.com) (CC0).
- Sound: recordings from the Sonniss GDC Game Audio Bundles, plus CC0 footsteps from OpenGameArt and Kenney.
- Upscaling (optional): NVIDIA DLSS, shipped in the packaged game under NVIDIA's RTX SDK licence. It is not part of
  the source; `unreal/Tools/fetch_dlss.ps1` downloads it from NVIDIA.
- Built with [Claude Code](https://claude.com/claude-code).

Inspired by Outer Wilds. This is a non-commercial fan project with its own worlds, art and code;
it doesn't use any of the original's assets or code.
