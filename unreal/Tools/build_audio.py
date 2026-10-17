"""Imports the game's sounds (SourceAssets/audio/*.wav, made by Tools/audio/make.py) and lists them
in one asset the game reads (DA_Audio, named in Config/DefaultGame.ini).

Run inside the editor:  python3 -B Tools/unreal.py slice --script Tools/build_audio.py
(build_slice.py also calls build() at the end of a full content build.)

A sound's name is its meaning (Player/EphemeraAudio.h). Names starting "loop_" are imported as
loops that keep their place while silent; everything is kept in memory whole, so the first step
taken is heard on the frame it lands and not when a file has been fetched.
"""
import pathlib

import unreal

ROOT = "/Game/Ephemera/Audio"
SOURCE = pathlib.Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())) / "SourceAssets/audio"
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()


# Withheld: names that must not be imported even if a file of that name turns up. The first sound set was made from
# filtered noise and the owner turned it down on hearing it (13 and 14 October 2026: "static noise everywhere", and of
# the jets "bleeds my ears"). Tools/audio/pro.py has since replaced it with professional recordings and removed the
# old files; what has no recording yet stays silent, which is better than a hiss. (The suit's hum is one: silence in
# vacuum is the right sound until there is a recording of breathing in a helmet worth having. High thin air is the
# other: the recording tried for it was an even haze of noise.)
WITHHELD = ("loop_suit", "loop_thin",
            # The score is composed music now (Tools/audio/score.py). Single notes dropped over it at random, a swell
            # and whale calls belonged to the old score of held tones; over written music they are wrong notes.
            "mote_", "awe", "voice_space")


def build():
    files = [path for path in sorted(SOURCE.glob("*.wav")) if not path.stem.startswith(WITHHELD)]
    if not files:
        unreal.log_warning(f"[audio] no sounds in {SOURCE}: run Tools/audio/run.sh make.py first")
        return 0
    tasks = []
    for path in files:
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(path))
        task.set_editor_property("destination_path", ROOT)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append(task)
    TOOLS.import_asset_tasks(tasks)
    sounds = []
    for path in files:
        sound = ASSETS.load_asset(f"{ROOT}/{path.stem}")
        if not isinstance(sound, unreal.SoundWave):
            unreal.log_error(f"[audio] {path.name} was not imported")
            continue
        loop = path.stem.startswith("loop_")
        sound.set_editor_property("looping", loop)
        if loop:
            sound.set_editor_property("virtualization_mode", unreal.VirtualizationMode.PLAY_WHEN_SILENT)
        # Long pieces (the score, the wind) stream; everything short is in memory from the start.
        long = sound.get_editor_property("duration") > 12.0
        sound.set_editor_property("loading_behavior", unreal.SoundWaveLoadingBehavior.RETAIN_ON_LOAD if long else unreal.SoundWaveLoadingBehavior.FORCE_INLINE)
        ASSETS.save_loaded_asset(sound)
        sounds.append(sound)
    # Sounds whose recording has gone are dropped from the project too.
    kept = {sound.get_name() for sound in sounds} | {"DA_Audio"}
    for asset in ASSETS.list_assets(ROOT, recursive=False):
        name = asset.split("/")[-1].split(".")[0]
        if name not in kept:
            ASSETS.delete_asset(asset.split(".")[0])
    path = f"{ROOT}/DA_Audio"
    listing = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else TOOLS.create_asset("DA_Audio", ROOT, unreal.EphemeraAudioSet, unreal.DataAssetFactory())
    listing.set_editor_property("sounds", sounds)
    ASSETS.save_loaded_asset(listing)
    unreal.log(f"[audio] {len(sounds)} sounds imported to {ROOT}")
    return len(sounds)


if __name__ == "__main__":
    build()
    # The key bindings are made by the same kind of build; do them here too so a change to either needs one run.
    unreal.EphemeraInputBuilder.build_input_assets("/Game/Ephemera/Input")
    unreal.log("[slice] done")
