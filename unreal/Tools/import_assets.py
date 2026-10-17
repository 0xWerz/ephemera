"""Run INSIDE Unreal Editor with Tools/unreal.py import (not system Python)."""
import json
import hashlib
import os
import sys
from pathlib import Path
import unreal

PROJECT = Path(unreal.Paths.project_dir()).resolve()
sys.path.insert(0,str(PROJECT / "Tools"))
from materials import create_effect_material, ensure_mesh_usage
SNAPSHOT = PROJECT / "Migration"
DATA = PROJECT / "Content/Data"
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
MESH_EDITOR = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
ASSET_TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
CHECKPOINT_PATH = PROJECT / "Saved/Import/checkpoint.json"
CHECKPOINT = json.loads(CHECKPOINT_PATH.read_text()) if CHECKPOINT_PATH.exists() else {}


def record_import(source, asset):
    CHECKPOINT[str(source)] = {
        "sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "asset": asset.get_path_name(),
    }
    CHECKPOINT_PATH.parent.mkdir(parents=True, exist_ok=True)
    pending = CHECKPOINT_PATH.with_suffix(".pending")
    pending.write_text(json.dumps(CHECKPOINT, indent=2))
    pending.replace(CHECKPOINT_PATH)


def previous_import(source, destination, expected_type):
    saved = CHECKPOINT.get(str(source))
    if saved and saved["sha256"] == hashlib.sha256(source.read_bytes()).hexdigest():
        if ASSETS.does_asset_exist(saved["asset"]):
            asset = ASSETS.load_asset(saved["asset"])
            if isinstance(asset, expected_type):
                return asset
    # Adopt completed assets from an interrupted first import before checkpoints
    # were introduced. Verify their recorded source and saved modification time.
    if saved or not ASSETS.does_directory_exist(destination):
        return None
    matches = []
    for path in ASSETS.list_assets(destination, recursive=True):
        asset = ASSETS.load_asset(path)
        if not isinstance(asset, expected_type):
            continue
        data = asset.get_editor_property("asset_import_data")
        sources = data.extract_filenames() if data else []
        package = path.split(".")[0].removeprefix("/Game/")
        file = PROJECT / "Content" / (package + ".uasset")
        if (len(sources) == 1 and Path(sources[0]).resolve() == source.resolve()
                and file.exists() and file.stat().st_mtime >= source.stat().st_mtime):
            matches.append(asset)
    return matches[0] if len(matches) == 1 else None


def mesh_options(collision, nanite):
    # Retain Epic's glTF material conversion, while avoiding render acceleration
    # data and simplified collision for the separate, invisible collision meshes.
    pipeline = ASSETS.load_asset("/Interchange/Pipelines/DefaultGLTFAssetsPipeline")
    mesh = pipeline.get_editor_property("mesh_pipeline")
    mesh.set_editor_property("build_nanite", nanite)
    mesh.set_editor_property("collision", False)
    mesh.set_editor_property("distance_field_resolution_scale", 0.0 if collision else 1.0)
    materials = pipeline.get_editor_property("material_pipeline")
    materials.set_editor_property("import_materials", not collision)
    materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", not collision)
    options = unreal.InterchangePipelineStackOverride()
    options.set_editor_property("override_pipelines", [
        unreal.SoftObjectPath(pipeline.get_path_name()),
        unreal.SoftObjectPath("/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline"),
    ])
    return options


def import_file(source, destination, expected_type, options=None):
    existing = previous_import(source, destination, expected_type)
    if existing:
        return existing
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(source))
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", True)
    task.set_editor_property("async_", False)
    if options:
        task.set_editor_property("options", options)
    ASSET_TOOLS.import_asset_tasks([task])
    candidates = [
        ASSETS.load_asset(path)
        for path in task.get_editor_property("imported_object_paths")
    ]
    matches = [asset for asset in candidates if isinstance(asset, expected_type)]
    # Some Interchange versions don't populate imported_object_paths until a scan.
    if not matches:
        matches = [
            ASSETS.load_asset(path)
            for path in ASSETS.list_assets(destination, recursive=True)
            if isinstance(ASSETS.load_asset(path), expected_type)
        ]
    if len(matches) != 1:
        raise RuntimeError(f"{source.name}: expected one {expected_type.__name__}, got {len(matches)}")
    return matches[0]


def make_terrain_material():
    path = "/Game/Migrated/Materials/M_Terrain"
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        return material
    material = ASSET_TOOLS.create_asset(
        "M_Terrain", "/Game/Migrated/Materials", unreal.Material, unreal.MaterialFactoryNew()
    )
    edit = unreal.MaterialEditingLibrary
    vertex = edit.create_material_expression(material, unreal.MaterialExpressionVertexColor, -500, 0)
    edit.connect_material_property(vertex, "RGB", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = edit.create_material_expression(material, unreal.MaterialExpressionConstant, -500, 160)
    rough.set_editor_property("r", 0.88)
    edit.connect_material_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    material.set_editor_property("two_sided", True)
    edit.recompile_material(material)
    ASSETS.save_loaded_asset(material)
    return material


def make_world_parameters():
    path = "/Game/Migrated/Materials/MPC_Aubade"
    collection = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if not collection:
        collection = ASSET_TOOLS.create_asset(
            "MPC_Aubade", "/Game/Migrated/Materials",
            unreal.MaterialParameterCollection, unreal.MaterialParameterCollectionFactoryNew()
        )
        values = []
        for name in ("WorldTime", "Resonance", "Night"):
            value = unreal.CollectionScalarParameter()
            value.set_editor_property("parameter_name", name)
            value.set_editor_property("default_value", 0.0)
            values.append(value)
        collection.set_editor_property("scalar_parameters", values)
        ASSETS.save_loaded_asset(collection)
    return collection


def check_axis_conversion():
    """A calibration mesh makes the importer verify its own scale and axis convention."""
    mesh = import_file(SNAPSHOT / "axis_calibration.glb", "/Game/Migrated/Calibration", unreal.StaticMesh)
    bounds = mesh.get_bounds()
    origin, extent = bounds.origin, bounds.box_extent
    # Four vertices: origin, +X 1 m, +Y 2 m, -Z 3 m in Godot.
    # Our Unreal coordinates: origin, +Y 100 cm, +Z 200 cm, +X 300 cm.
    expected = (150.0, 50.0, 100.0)
    current = (extent.x, extent.y, extent.z)
    if max(abs(a-b) for a,b in zip(current, expected)) < 0.1:
        if max(abs(a-b) for a,b in zip((origin.x,origin.y,origin.z),expected)) > 0.1:
            raise RuntimeError(f"Unexpected glTF axis signs: {origin}")
        return 0.0
    # Interchange's glTF convention usually maps (x, y, z) -> (x, z, y).
    # A +90 degree yaw converts its mesh-local coordinates to our (-z, x, y).
    if max(abs(a-b) for a,b in zip(current, (50.0,150.0,100.0))) < 0.1:
        if abs(origin.x-50)>0.1 or abs(origin.y+150)>0.1:
            raise RuntimeError(f"Unexpected glTF axis signs: {origin}")
        return 90.0
    raise RuntimeError(f"Unexpected glTF scale/axes: origin={origin}, extent={extent}")


def main():
    world = json.loads((DATA / "world.json").read_text())
    if world["failures"]:
        raise RuntimeError("The source export contains failures.")
    for folder in ("Meshes", "Materials", "Audio", "Calibration"):
        ASSETS.make_directory("/Game/Migrated/" + folder)
    yaw = check_axis_conversion()
    terrain = make_terrain_material()
    collection = make_world_parameters()
    effects = {kind:create_effect_material(kind,collection) for kind in
               ("water","flame","flame_stylized","sun","gas_giant","rings","hologram","starfield")}
    manifest = {"schema": 1, "mesh_yaw": yaw, "meshes": {}, "audio": {}}
    translucent = {"water", "clouds", "rings", "hologram", "foliage_card", "flame", "flame_stylized"}
    nanite = os.environ.get("EPHEMERA_NANITE", "0") == "1"
    for index, item in enumerate(world["meshes"]):
        use_nanite = nanite and not item["collision"] and not bool(set(item["kinds"]) & translucent)
        source = SNAPSHOT / item["file"]
        asset = import_file(source, "/Game/Migrated/Meshes/" + item["id"],
                            unreal.StaticMesh, mesh_options(item["collision"], use_nanite))
        if item["collision"]:
            setup = asset.get_editor_property("body_setup")
            setup.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            asset.set_editor_property("allow_cpu_access", True)
            build = MESH_EDITOR.get_lod_build_settings(asset, 0)
            build.set_editor_property("distance_field_resolution_scale", 0.0)
            MESH_EDITOR.set_lod_build_settings(asset, 0, build)
        else:
            for slot, kind in enumerate(item["kinds"]):
                if kind == "terrain":
                    asset.set_material(slot, terrain)
                elif kind in effects:
                    asset.set_material(slot,effects[kind])
            for slot in asset.get_editor_property("static_materials"):
                ensure_mesh_usage(slot.get_editor_property("material_interface"), use_nanite)
        settings = asset.get_editor_property("nanite_settings")
        settings.set_editor_property("enabled", use_nanite)
        asset.set_editor_property("nanite_settings", settings)
        ASSETS.save_loaded_asset(asset)
        record_import(source, asset)
        manifest["meshes"][item["id"]] = asset.get_path_name()
        if index % 25 == 0:
            unreal.log(f"Ephemera: imported {index+1}/{len(world['meshes'])} meshes")
    # Native SoundFactory supports WAV and Ogg. Keep the source Audio.get_stream
    # preference for SFX when a music file shares its name.
    source_audio = PROJECT.parent / "ephemera/assets/audio"
    for source in sorted(source_audio.rglob("*.wav")) + sorted(source_audio.rglob("*.ogg")):
        folder = "/Game/Migrated/Audio/" + ("Music/" if source.suffix == ".ogg" else "") + source.stem
        asset = import_file(source, folder, unreal.SoundWave)
        if source.stem.endswith("_loop") or source.stem.startswith("stem_"):
            asset.set_editor_property("looping", True)
        ASSETS.save_loaded_asset(asset)
        record_import(source, asset)
        manifest["audio"].setdefault(source.stem, asset.get_path_name())
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    map_path = "/Game/Maps/Aubade"
    if not ASSETS.does_asset_exist(map_path):
        ASSETS.make_directory("/Game/Maps")
        if not levels.new_level(map_path):
            raise RuntimeError("Could not create Aubade map")
    else:
        if not levels.load_level(map_path):
            raise RuntimeError("Could not load Aubade map")
    # GameMode constructs the native universe on play. The authored map stays lightweight.
    if not levels.save_current_level():
        raise RuntimeError("Could not save Aubade map")
    pending = DATA / "assets.pending"
    pending.write_text(json.dumps(manifest, indent=2))
    pending.replace(DATA / "assets.json")
    unreal.log("Ephemera import complete. Open /Game/Maps/Aubade and press Play.")


if __name__ == "__main__":
    main()
