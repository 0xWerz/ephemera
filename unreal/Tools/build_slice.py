"""Builds the vertical slice's content: scanned surfaces, terrain material, bodies, terrain, input, map.

Run headless through `unreal.py slice`. Re-running is safe: every step replaces what it made.
Design numbers live in docs/unreal/DIRECTION.md.
"""
import hashlib
import pathlib
import math
import os
import random
from pathlib import Path

import unreal

PROJECT = Path(unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir()))
SOURCE = PROJECT / "SourceAssets/polyhaven/textures"
ROOT = "/Game/Ephemera"
ASSETS = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
TOOLS = unreal.AssetToolsHelpers.get_asset_tools()
EDIT = unreal.MaterialEditingLibrary


def connect(source, output, target, pin):
    """Wires two material nodes. A wrong pin name fails quietly in Unreal and leaves the input at its
    default, which renders as something plausible and wrong; here it stops the build."""
    if not EDIT.connect_material_expressions(source, output, target, pin):
        raise RuntimeError(f"could not connect {source.get_name()}.{output or 'out'} to {target.get_name()}.{pin or 'in'}")
    return True


def connect_property(source, output, material_property):
    if not EDIT.connect_material_property(source, output, material_property):
        raise RuntimeError(f"could not connect {source.get_name()}.{output or 'out'} to {material_property}")
    return True

# Scanned surfaces (Poly Haven, CC0): label/id as downloaded by fetch_polyhaven.py.
SURFACES = {
    # Dawnhollow: an Icelandic world. Moss carpets, moss among lava rock, black sand, basalt, snow.
    # Each ground has a scan made standing on it and one made from the air.
    "moss": "grass_meadow/leafy_grass", "moss_far": "forest_floor/aerial_grass_rock",
    "field": "grass_meadow/rocky_terrain_02", "field_far": "forest_floor/aerial_rocks_02",
    "sand": "iceland/damp_sand", "sand_far": "sand/aerial_beach_01",
    "basalt": "iceland/dark_rock_02", "basalt_far": "rock_cliff/aerial_rocks_04",
    "snow": "snow_ice/snow_02", "snow_far": "snow_ice/snow_01",
    # Hush: grey dust over rock.
    "regolith": "lunar_grey/moon_01",
    "moonrock": "iceland/dark_rock",
}


def log(message):
    unreal.log(f"[slice] {message}")


def stamp_of(*parts):
    """A fingerprint of everything that defines a generated asset (shader code, numbers, texture paths)."""
    def flat(value):
        if isinstance(value, dict):
            return "{" + ",".join(f"{k}:{flat(v)}" for k, v in sorted(value.items())) + "}"
        if isinstance(value, (list, tuple)):
            return "[" + ",".join(flat(v) for v in value) + "]"
        if isinstance(value, unreal.Object):
            return value.get_path_name()
        return repr(value)
    return hashlib.sha1("|".join(flat(part) for part in parts).encode()).hexdigest()


def unchanged(path, stamp):
    """Returns the existing asset if it was generated from the same inputs, else None.

    Regenerating a material makes Unreal recompile every shader that uses it, which is most of the
    wait after a content build. Bump the version string passed to stamp_of when a material's node
    graph changes in a way its inputs do not show.
    """
    if not ASSETS.does_asset_exist(path):
        return None
    asset = ASSETS.load_asset(path)
    return asset if asset and ASSETS.get_metadata_tag(asset, "EphemeraStamp") == stamp else None


def finish_material(material, stamp, name):
    EDIT.recompile_material(material)
    ASSETS.set_metadata_tag(material, "EphemeraStamp", stamp)
    ASSETS.save_loaded_asset(material)
    log(f"material {name}: rebuilt")
    return material


# --------------------------------------------------------------------------------- textures ---

def roughness_from_normals(maps):
    """Bakes the normal map's lost detail into the roughness of each smaller mip (Toksvig).

    Seen from further off, a texel stands for many bumps, whose averaged normal is shorter than one;
    that shortening is exactly how much rougher the surface must be drawn there, or every bump that
    has shrunk below a pixel glitters as it comes and goes (the ground's flicker, worst on wet sand
    and rock). The engine bakes it when told which normal map goes with the roughness channel.
    """
    arm, normal = maps.get("arm"), maps.get("n")
    if arm and normal and (arm.get_editor_property("composite_texture") != normal
                           or arm.get_editor_property("composite_texture_mode") != unreal.CompositeTextureMode.CTM_NORMAL_ROUGHNESS_TO_GREEN):
        arm.set_editor_property("composite_texture", normal)
        arm.set_editor_property("composite_texture_mode", unreal.CompositeTextureMode.CTM_NORMAL_ROUGHNESS_TO_GREEN)
        arm.set_editor_property("composite_power", 1.0)
        ASSETS.save_loaded_asset(arm)
        log(f"roughness from normals: {arm.get_name()}")
    return maps


def import_surface(name, relative):
    """Imports diffuse, DirectX normal, AO/roughness/metal and height maps. Returns {'d','n','arm'[,'h']} textures."""
    folder = SOURCE / relative
    destination = f"{ROOT}/Surfaces/{name}"
    wanted = {"d": "_diff_", "n": "_nor_dx_", "arm": "_arm_"}
    # The height map ('h') is what the ground's relief is read from; not every scan comes with one.
    if any("_disp_" in f.name for f in folder.iterdir()):
        wanted["h"] = "_disp_"
    result = {}
    have = {key: f"{destination}/T_{name}_{key.upper()}" for key in wanted}
    if all(ASSETS.does_asset_exist(path) for path in have.values()):
        loaded = {key: ASSETS.load_asset(path) for key, path in have.items()}
        if all(t.get_editor_property("never_stream") for t in loaded.values()):
            return roughness_from_normals(loaded)
    tasks = []
    for key, marker in wanted.items():
        files = [f for f in sorted(folder.iterdir()) if marker in f.name]
        if not files:
            raise RuntimeError(f"{relative}: no {marker} map")
        asset_name = f"T_{name}_{key.upper()}"
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(files[0]))
        task.set_editor_property("destination_path", destination)
        task.set_editor_property("destination_name", asset_name)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", False)
        tasks.append((key, asset_name, task))
    TOOLS.import_asset_tasks([t for _, _, t in tasks])
    for key, asset_name, _ in tasks:
        texture = ASSETS.load_asset(f"{destination}/{asset_name}")
        if not texture:
            raise RuntimeError(f"{relative}: import of {asset_name} failed")
        # The terrain shader samples these in a Custom node, which cannot read virtual textures.
        texture.set_editor_property("virtual_texture_streaming", False)
        # The texture streamer cannot measure Custom-node sampling either, and drops these to a
        # blur when memory gets tight: keep the handful of terrain surfaces fully loaded.
        texture.set_editor_property("never_stream", True)
        if key == "n":
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
            texture.set_editor_property("srgb", False)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD_NORMAL_MAP)
        elif key == "arm":
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
            texture.set_editor_property("srgb", False)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        elif key == "h":
            texture.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_GRAYSCALE)
            texture.set_editor_property("srgb", False)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        else:
            texture.set_editor_property("srgb", True)
            texture.set_editor_property("lod_group", unreal.TextureGroup.TEXTUREGROUP_WORLD)
        ASSETS.save_loaded_asset(texture)
        result[key] = texture
    log(f"surface {name}: {relative}")
    return roughness_from_normals(result)


def import_map(file, destination, name, compression=None):
    """Imports one data texture (not colour) as it is: linear, uncompressed unless told otherwise, with mips."""
    path = f"{destination}/{name}"
    stamp = stamp_of("map-v1", str(file), pathlib.Path(file).stat().st_size, pathlib.Path(file).stat().st_mtime_ns, str(compression))
    kept = unchanged(path, stamp)
    if kept:
        return kept
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", str(file))
    task.set_editor_property("destination_path", destination)
    task.set_editor_property("destination_name", name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    TOOLS.import_asset_tasks([task])
    texture = ASSETS.load_asset(path)
    if not texture:
        raise RuntimeError(f"import of {file} failed")
    texture.set_editor_property("virtual_texture_streaming", False)
    texture.set_editor_property("never_stream", True)
    texture.set_editor_property("srgb", False)
    texture.set_editor_property("compression_settings", compression or unreal.TextureCompressionSettings.TC_VECTOR_DISPLACEMENTMAP)
    ASSETS.set_metadata_tag(texture, "EphemeraStamp", stamp)
    ASSETS.save_loaded_asset(texture)
    log(f"texture {name}")
    return texture


# --------------------------------------------------------------------------------- materials ---

TERRAIN_CODE = r"""
// P: planet-local position (cm). N: planet-local surface normal.
// Vertex colour (VC3 = RGB, VCA = A): R slope, G moisture, A height.
// Dist: distance from the camera (cm). Four scanned layers, each projected along the three local
// axes and blended by the normal (triplanar), so nothing stretches anywhere on the globe.
float4 VC = float4(VC3, VCA);
float3 pm = P * 0.01;
float3 w = pow(abs(N), 6.0);
w /= (w.x + w.y + w.z);
float2 ux = pm.zy, uy = pm.xz, uz = pm.xy;
#define SS View.MaterialTextureBilinearWrapedSampler
#define TRI(T, S) (Texture2DSample(T, SS, ux * (S)) * w.x + Texture2DSample(T, SS, uy * (S)) * w.y + Texture2DSample(T, SS, uz * (S)) * w.z)
#define NRM(T, S, OUT) { \
    float3 tx = UnpackNormalMap(Texture2DSample(T, SS, ux * (S))).xyz; \
    float3 ty = UnpackNormalMap(Texture2DSample(T, SS, uy * (S))).xyz; \
    float3 tz = UnpackNormalMap(Texture2DSample(T, SS, uz * (S))).xyz; \
    tx = float3(tx.xy + N.zy, abs(tx.z) * N.x); \
    ty = float3(ty.xy + N.xz, abs(ty.z) * N.y); \
    tz = float3(tz.xy + N.xy, abs(tz.z) * N.z); \
    OUT = normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z); }

const float tile = 1.0 / Tiling;                 // one texture repeat every Tiling metres
// Far from the camera the same surface is also sampled 7x larger, which hides the repeat.
float farBlend = saturate((Dist - 1500.0) / 9000.0) * 0.65;

float slope = VC.r;
float moisture = VC.g;
// Large soft patches so the meadow is not one flat green.
float patch = Texture2DSample(MeadowD, SS, pm.xy * 0.011 + pm.zz * 0.007).g;
float rockW = smoothstep(RockStart, RockStart + 0.10, slope + (patch - 0.5) * 0.08);
float dirtW = smoothstep(RockStart * 0.45, RockStart * 0.9, slope + (patch - 0.5) * 0.10) * (1.0 - rockW);
float forestW = smoothstep(0.52, 0.68, moisture + (patch - 0.5) * 0.35) * (1.0 - rockW) * (1.0 - dirtW);
float meadowW = saturate(1.0 - rockW - dirtW - forestW);

float3 colM = lerp(TRI(MeadowD, tile).rgb, TRI(MeadowD, tile / 7.0).rgb, farBlend);
float3 colF = lerp(TRI(ForestD, tile).rgb, TRI(ForestD, tile / 7.0).rgb, farBlend);
float3 colD = lerp(TRI(DirtD, tile).rgb, TRI(DirtD, tile / 7.0).rgb, farBlend);
float3 colR = lerp(TRI(RockD, tile * 0.5).rgb, TRI(RockD, tile / 9.0).rgb, farBlend) * 0.72;
// Grass and undergrowth exist as geometry only near the camera. Farther off the ground itself
// takes on their colour, or meadows turn to bare pale soil a stone's throw away and the whole
// world reads as desert from orbit.
float veg = saturate((Dist - 2500.0) / 5000.0) * VegAmount;
colM = lerp(colM, MeadowTint * lerp(0.75, 1.15, patch), veg * 0.85);
colF = lerp(colF, ForestTint * lerp(0.75, 1.1, patch), veg * 0.85);
float3 base = colM * meadowW + colF * forestW + colD * dirtW + colR * rockW;
// Gentle large-scale tone shift (drier on ridges, lusher in hollows).
base *= lerp(0.86, 1.10, patch) * lerp(1.06, 0.92, VC.a);

float3 nM, nF, nD, nR;
NRM(MeadowN, tile, nM)
NRM(ForestN, tile, nF)
NRM(DirtN, tile, nD)
NRM(RockN, tile * 0.5, nR)
Nrm = normalize(nM * meadowW + nF * forestW + nD * dirtW + nR * rockW);
// Detail normals fade with distance so far hills don't sparkle.
Nrm = normalize(lerp(Nrm, N, saturate((Dist - 20000.0) / 60000.0)));

ORM = TRI(MeadowA, tile).rgb * meadowW + TRI(ForestA, tile).rgb * forestW + TRI(DirtA, tile).rgb * dirtW + TRI(RockA, tile * 0.5).rgb * rockW;
return base;
"""


def make_terrain_material(name, layers, tiling_m=3.0, rock_start=0.14, far_tints=None):
    """layers: dict meadow/forest/dirt/rock -> {'d','n','arm'}.

    far_tints: (meadow, forest) linear colours the ground takes on with distance, standing in for
    the grass and undergrowth that are only built near the camera. None = keep the scanned colour.
    """
    keep_scan = far_tints is None
    far_tints = far_tints or ((0.2, 0.2, 0.2), (0.2, 0.2, 0.2))
    path = f"{ROOT}/Materials/{name}"
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset(name, f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("tangent_space_normal", False)

    def expr(cls, x=-900, y=0):
        return EDIT.create_material_expression(material, cls, x, y)

    world = expr(unreal.MaterialExpressionWorldPosition)
    local_pos = expr(unreal.MaterialExpressionTransformPosition)
    local_pos.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local_pos.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, "", local_pos, "")
    normal_ws = expr(unreal.MaterialExpressionVertexNormalWS)
    local_normal = expr(unreal.MaterialExpressionTransform)
    local_normal.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    local_normal.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    connect(normal_ws, "", local_normal, "")
    vertex = expr(unreal.MaterialExpressionVertexColor)
    camera = expr(unreal.MaterialExpressionCameraPositionWS)
    distance = expr(unreal.MaterialExpressionDistance)
    connect(world, "", distance, "A")
    connect(camera, "", distance, "B")
    tiling = expr(unreal.MaterialExpressionScalarParameter)
    tiling.set_editor_property("parameter_name", "Tiling")
    tiling.set_editor_property("default_value", tiling_m)
    rock = expr(unreal.MaterialExpressionScalarParameter)
    rock.set_editor_property("parameter_name", "RockStart")
    rock.set_editor_property("default_value", rock_start)
    veg_amount = expr(unreal.MaterialExpressionScalarParameter)
    veg_amount.set_editor_property("parameter_name", "VegAmount")
    veg_amount.set_editor_property("default_value", 0.0 if keep_scan else 1.0)
    meadow_tint = expr(unreal.MaterialExpressionVectorParameter)
    meadow_tint.set_editor_property("parameter_name", "MeadowTint")
    meadow_tint.set_editor_property("default_value", unreal.LinearColor(*far_tints[0], 1.0))
    forest_tint = expr(unreal.MaterialExpressionVectorParameter)
    forest_tint.set_editor_property("parameter_name", "ForestTint")
    forest_tint.set_editor_property("default_value", unreal.LinearColor(*far_tints[1], 1.0))

    custom = expr(unreal.MaterialExpressionCustom, -300, 0)
    # (input name, node, output pin). The vertex colour's first pin is RGB; alpha is its own pin.
    sources = [("P", local_pos, ""), ("N", local_normal, ""), ("VC3", vertex, ""), ("VCA", vertex, "A"),
               ("Dist", distance, ""), ("Tiling", tiling, ""), ("RockStart", rock, ""),
               ("MeadowTint", meadow_tint, ""), ("ForestTint", forest_tint, ""), ("VegAmount", veg_amount, "")]
    sampler_types = {"d": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR,
                     "n": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                     "arm": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}
    suffix = {"d": "D", "n": "N", "arm": "A"}
    for layer in ("meadow", "forest", "dirt", "rock"):
        for key in ("d", "n", "arm"):
            node = expr(unreal.MaterialExpressionTextureObjectParameter, -1300, 0)
            node.set_editor_property("parameter_name", f"{layer.title()}{suffix[key]}")
            node.set_editor_property("texture", layers[layer][key])
            node.set_editor_property("sampler_type", sampler_types[key])
            sources.append((f"{layer.title()}{suffix[key]}", node, ""))
    inputs = []
    for input_name, _, _ in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    outputs = []
    for output_name in ("Nrm", "ORM"):
        entry = unreal.CustomOutput()
        entry.set_editor_property("output_name", output_name)
        entry.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        outputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", TERRAIN_CODE)
    for input_name, node, pin in sources:
        if not connect(node, pin, custom, input_name):
            raise RuntimeError(f"{name}: could not connect {input_name}")

    connect_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    world_normal = expr(unreal.MaterialExpressionTransform, -100, 200)
    world_normal.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world_normal.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(custom, "Nrm", world_normal, "")
    connect_property(world_normal, "", unreal.MaterialProperty.MP_NORMAL)
    mask = expr(unreal.MaterialExpressionComponentMask, -100, 400)
    mask.set_editor_property("r", True)
    mask.set_editor_property("g", False)
    mask.set_editor_property("b", False)
    connect(custom, "ORM", mask, "")
    connect_property(mask, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    rough = expr(unreal.MaterialExpressionComponentMask, -100, 520)
    rough.set_editor_property("r", False)
    rough.set_editor_property("g", True)
    rough.set_editor_property("b", False)
    connect(custom, "ORM", rough, "")
    connect_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)

    EDIT.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE, True)
    EDIT.recompile_material(material)
    ASSETS.save_loaded_asset(material)
    log(f"material {name}")
    return material


STARS_CODE = r"""
// Dir: direction in the fixed-star frame. Point stars on a 3D grid (two layers), colour by temperature,
// and the galaxy's band.
// About four thousand stars over the whole sphere, as the naked eye has (it was one in every grid
// cell, some 1.5 million: 15,000 single-pixel dots on a frame that blinked under the anti-aliasing,
// which read as a pixelated sky). Each star is drawn at least about a pixel wide, with its light spread
// rather than cut, so it holds still; the brightest few bloom a little.
float3 d = normalize(Dir);
float px = max(length(ddx(d)), length(ddy(d)));      // the angle one pixel spans
// A hash without sine (Hoskins, "Hash without Sine"): the same stars on every graphics card.
struct FStarHash
{
    float3 H(float3 p3)
    {
        p3 = frac(p3 * float3(0.1031, 0.1030, 0.0973));
        p3 += dot(p3, p3.yxz + 33.33);
        return frac((p3.xxy + p3.yxx) * p3.zyx);
    }
};
FStarHash StarHash;
float3 pts = 0;
for (int layer = 0; layer < 2; layer++)
{
    float scale = layer == 0 ? 70.0 : 150.0;
    float spawn = layer == 0 ? 0.045 : 0.008;
    float3 p = d * scale;
    float3 cell = floor(p);
    float3 f = frac(p) - 0.5;
    float3 h = StarHash.H(cell + layer * 17.0);
    float3 h2 = frac(h * 97.31 + h.yzx * 13.7);
    if (h2.z > spawn) continue;
    float dist = length(f - (h - 0.5) * 0.7);
    float magnitude = pow(h2.x, layer == 0 ? 5.0 : 8.0);          // most are faint, a few blaze
    float radius = lerp(0.035, 0.09, magnitude) / (1.0 + layer * 0.35);
    // At least about three pixels across (the anti-aliasing shifts the image a fraction of a pixel every
    // frame: anything smaller is averaged into nothing).
    float foot = 1.5 * px * scale;
    float reach = min(max(radius, foot), 0.14);
    float flux = max((radius * radius) / (reach * reach), 0.3);
    float core = saturate(1.0 - dist / reach);
    core = core * core * flux;          // a soft disc: a steep one is averaged away by the anti-aliasing's jitter
    // 3000 K orange .. 6500 K white .. 20000 K blue-white
    float t = h2.y;
    float3 tint = t < 0.5 ? lerp(float3(1.0, 0.62, 0.36), float3(1.0, 0.96, 0.9), t * 2.0)
                          : lerp(float3(1.0, 0.96, 0.9), float3(0.68, 0.78, 1.0), (t - 0.5) * 2.0);
    pts += tint * core * lerp(0.8, 16.0, magnitude) * (layer == 0 ? 1.0 : 0.8);
}
// The galaxy. This world sits in a spiral's disc, a little way out: seen from inside, the disc is a
// band of unresolved starlight right round the sky, widest and warmest toward the core, split
// along its middle by dark lanes of dust, with a few clouds of gas lit rose and teal by the young
// stars in them. It is what says, on any night, how large the place is. It is faint: no brighter
// than the moonlit land (at twice that it was a glowing smear the night was lit for).
float3 pole = normalize(float3(0.35, 0.25, 0.9));
float3 toCore = normalize(cross(pole, float3(0.0, 0.0, 1.0)));
float3 gy = cross(pole, toCore);
float lat = dot(d, pole);
float lon = atan2(dot(d, gy), dot(d, toCore));
struct FSkyNoise
{
    float Hash(float3 c) { float3 q = frac(c * 0.1031); q += dot(q, q.yzx + 33.33); return frac((q.x + q.y) * q.z); }
    float Value(float3 q)
    {
        float3 c = floor(q); float3 f = frac(q); f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(Hash(c), Hash(c + float3(1, 0, 0)), f.x), lerp(Hash(c + float3(0, 1, 0)), Hash(c + float3(1, 1, 0)), f.x), f.y),
                    lerp(lerp(Hash(c + float3(0, 0, 1)), Hash(c + float3(1, 0, 1)), f.x), lerp(Hash(c + float3(0, 1, 1)), Hash(c + float3(1, 1, 1)), f.x), f.y), f.z);
    }
    float Fbm(float3 q) { return Value(q) * 0.5 + Value(q * 2.03 + 7.1) * 0.25 + Value(q * 4.1 + 3.7) * 0.125 + Value(q * 8.3 + 1.9) * 0.0625 + Value(q * 16.7 + 5.3) * 0.03125; }
};
FSkyNoise Sky;
float coreW = exp(-lon * lon / 0.55);
float width = lerp(0.085, 0.20, coreW);
float band = exp(-pow(lat / width, 2.0));
float halo = exp(-pow(lat / (width * 2.6), 2.0));
float clumps = Sky.Fbm(d * 7.0 + 11.0);
float dust = smoothstep(0.42, 0.66, Sky.Fbm(d * 11.0 + float3(3.1, 9.7, 5.3))) * exp(-pow((lat - (Sky.Value(d * 4.0) - 0.5) * 0.05) / (width * 0.42), 2.0));
float3 light = lerp(float3(0.50, 0.58, 0.82), float3(1.0, 0.84, 0.62), coreW);
float3 gal = light * (band * (0.10 + 0.24 * coreW) * lerp(0.55, 1.45, clumps) + halo * 0.022) * (1.0 - 0.82 * dust);
float gas = smoothstep(0.60, 0.80, Sky.Fbm(d * 5.0 + float3(40.0, 13.0, 27.0))) * band;
gal += lerp(float3(0.80, 0.22, 0.34), float3(0.20, 0.62, 0.66), Sky.Value(d * 3.0 + 5.0)) * gas * 0.07 * (1.0 - dust);
// The point stars crowd toward the band.
pts *= 1.0 + band * 1.6 * (1.0 - 0.6 * dust);
// And the night sky is never quite black: airglow, faint blue-grey everywhere.
return (pts * 3.0 + gal * 0.25 + float3(0.006, 0.008, 0.013)) * Brightness;
"""


def make_stars_material():
    path = f"{ROOT}/Materials/M_Stars"
    stamp = stamp_of("stars-v11", STARS_CODE)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset("M_Stars", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("two_sided", True)
    # A sky layer: drawn behind everything and exempt from fog and aerial perspective. Without
    # this the sphere counts as scenery 60 km away and the haze buries the whole sky.
    material.set_editor_property("is_sky", True)
    world = EDIT.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -900, 0)
    camera = EDIT.create_material_expression(material, unreal.MaterialExpressionCameraPositionWS, -900, 160)
    ray = EDIT.create_material_expression(material, unreal.MaterialExpressionSubtract, -700, 0)
    connect(world, "", ray, "A")
    connect(camera, "", ray, "B")
    # Into the sphere's own space: the sphere is rotated to the inertial sky, so the stars turn with it.
    local = EDIT.create_material_expression(material, unreal.MaterialExpressionTransform, -500, 0)
    local.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    connect(ray, "", local, "")
    brightness = EDIT.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -500, 200)
    brightness.set_editor_property("parameter_name", "Brightness")
    # On-screen brightness (middle grey is 0.18): the brightest stars bloom a little, most are faint.
    brightness.set_editor_property("default_value", 1.0)
    custom = EDIT.create_material_expression(material, unreal.MaterialExpressionCustom, -250, 0)
    inputs = []
    for name in ("Dir", "Brightness"):
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", STARS_CODE)
    connect(local, "", custom, "Dir")
    connect(brightness, "", custom, "Brightness")
    # The stars keep a fixed brightness on screen whatever the exposure, so they stay visible in
    # space beside a sunlit planet or the sun (a real camera would lose them; this is a game about
    # space). The sky rig fades them under a lit sky through the Visibility parameter.
    on_screen = EDIT.create_material_expression(material, unreal.MaterialExpressionEyeAdaptationInverse, -50, -120)
    connect(custom, "", on_screen, "LightValueInput")
    visibility = EDIT.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -50, 120)
    visibility.set_editor_property("parameter_name", "Visibility")
    visibility.set_editor_property("default_value", 1.0)
    stars = EDIT.create_material_expression(material, unreal.MaterialExpressionMultiply, 150, 0)
    connect(on_screen, "", stars, "A")
    connect(visibility, "", stars, "B")
    # With a sky layer in the scene the engine leaves the sky's pixels to it: the atmosphere's own
    # light (blue by day, the glow at dusk and along a planet's limb, nothing in open space) is
    # added here.
    air = EDIT.create_material_expression(material, unreal.MaterialExpressionSkyAtmosphereViewLuminance, 150, 260)
    sky = EDIT.create_material_expression(material, unreal.MaterialExpressionAdd, 350, 0)
    connect(stars, "", sky, "A")
    connect(air, "", sky, "B")
    connect_property(sky, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish_material(material, stamp, "M_Stars")


def make_star_material():
    path = f"{ROOT}/Materials/M_Star"
    stamp = stamp_of("star-v2", 2.5e6)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset("M_Star", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    colour = EDIT.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -400, 0)
    colour.set_editor_property("constant", unreal.LinearColor(1.0, 0.72, 0.38, 1.0))
    # Physical exposure: the disc must sit far above a sunlit ground (~10^4 nits) to read as a sun.
    power = EDIT.create_material_expression(material, unreal.MaterialExpressionConstant, -400, 160)
    power.set_editor_property("r", 2.5e6)
    emissive = EDIT.create_material_expression(material, unreal.MaterialExpressionMultiply, -200, 0)
    connect(colour, "", emissive, "A")
    connect(power, "", emissive, "B")
    connect_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish_material(material, stamp, "M_Star")


# ------------------------------------------------------------------------------------ planet ---

# The planet's ground is worked out twice: once per vertex, to move the surface (DISPLACE_CODE), and
# once per pixel, to shade it (PLANET_CODE). These three pieces are the part both must agree on.

PLANET_CELLS = r"""
// Whole-number hashing. The relief is computed in two shaders that must agree exactly, and a
// sine-based hash does not survive being compiled twice; integers do.
struct FCells
{
    float3 Hash3(float3 c)
    {
        uint3 u = uint3(int3(c) + 1048576);
        u *= uint3(1597334673u, 3812015801u, 2798796415u);
        uint a = (u.x ^ u.y ^ u.z) * 1597334673u;
        uint b = a * 747796405u + 2891336453u;
        uint d = b * 747796405u + 2891336453u;
        return float3(a >> 8, b >> 8, d >> 8) * (1.0 / 16777216.0);
    }
    // Joints in rock. x: 0 on a joint, rising inside the block. y: the block's own random number.
    float2 Blocks(float3 q)
    {
        float3 ip = floor(q);
        float3 fp = q - ip;
        float f1 = 9.0, f2 = 9.0, id = 0.0;
        for (int k = -1; k <= 1; k++)
        for (int j = -1; j <= 1; j++)
        for (int i = -1; i <= 1; i++)
        {
            float3 g = float3(i, j, k);
            float3 o = Hash3(ip + g);
            float3 r = g + o - fp;
            float d = dot(r, r);
            if (d < f1) { f2 = f1; f1 = d; id = o.z; }
            else if (d < f2) { f2 = d; }
        }
        return float2(sqrt(f2) - sqrt(f1), id);
    }
};
FCells Cells;
#undef HASH
#define HASH(c) Cells.Hash3(c).x
"""

PLANET_WEIGHTS = r"""
// The terrain's own values, read here per pixel from its cube grid (Attr: EphemeraPlanetBuilder::
// BuildTerrainAttributes; AttrRes: the grid's cells along a face) with a Catmull-Rom cubic, so they change
// smoothly over the land and still pass through every value the grid holds. Read from the mesh's
// vertices they changed in straight lines across each triangle, and everything they decide (which ground,
// how much rock relief) drew every triangle as a flat facet with a crease along its edges (2026-10-18).
// The grid is the planet builder's: six faces, each warped by tan() (DirectionOf), two cells of apron.
struct FAttr
{
    float4 Read(Texture2DArray T, SamplerState S, float3 d, float slice, float res)
    {
        float3 a = abs(d);
        float3 n, u, v;
        float face;
        if (a.x >= a.y && a.x >= a.z)
        {
            if (d.x > 0.0) { face = 0.0; n = float3(1, 0, 0); u = float3(0, 1, 0); v = float3(0, 0, 1); }
            else           { face = 1.0; n = float3(-1, 0, 0); u = float3(0, 0, 1); v = float3(0, 1, 0); }
        }
        else if (a.y >= a.z)
        {
            if (d.y > 0.0) { face = 2.0; n = float3(0, 1, 0); u = float3(0, 0, 1); v = float3(1, 0, 0); }
            else           { face = 3.0; n = float3(0, -1, 0); u = float3(1, 0, 0); v = float3(0, 0, 1); }
        }
        else
        {
            if (d.z > 0.0) { face = 4.0; n = float3(0, 0, 1); u = float3(1, 0, 0); v = float3(0, 1, 0); }
            else           { face = 5.0; n = float3(0, 0, -1); u = float3(0, 1, 0); v = float3(1, 0, 0); }
        }
        float3 q = d / dot(d, n);
        float2 t = atan(float2(dot(q, u), dot(q, v))) * (4.0 / 3.14159265);
        float size = res + 5.0;
        // Texel space: grid point i is texel i + 2, whose centre is at i + 2.5.
        float2 c = (t * res + res) * 0.5 + 2.0;
        // Catmull-Rom in nine bilinear reads (M. Pettineo's method): smooth, and through every grid value
        // exactly, so a rocky spot is as rocky as its vertex said.
        float2 p1 = floor(c) + 0.5;
        float2 f = frac(c);
        float2 w0 = f * (-0.5 + f * (1.0 - 0.5 * f));
        float2 w1 = 1.0 + f * f * (-2.5 + 1.5 * f);
        float2 w2 = f * (0.5 + f * (2.0 - 1.5 * f));
        float2 w3 = f * f * (-0.5 + 0.5 * f);
        float2 w12 = w1 + w2;
        float2 t0 = (p1 - 1.0) / size;
        float2 t12 = (p1 + w2 / w12) / size;
        float2 t3 = (p1 + 2.0) / size;
        float sl = face + slice;
        float4 r = 0.0;
        r += T.SampleLevel(S, float3(t0.x, t0.y, sl), 0.0) * w0.x * w0.y;
        r += T.SampleLevel(S, float3(t12.x, t0.y, sl), 0.0) * w12.x * w0.y;
        r += T.SampleLevel(S, float3(t3.x, t0.y, sl), 0.0) * w3.x * w0.y;
        r += T.SampleLevel(S, float3(t0.x, t12.y, sl), 0.0) * w0.x * w12.y;
        r += T.SampleLevel(S, float3(t12.x, t12.y, sl), 0.0) * w12.x * w12.y;
        r += T.SampleLevel(S, float3(t3.x, t12.y, sl), 0.0) * w3.x * w12.y;
        r += T.SampleLevel(S, float3(t0.x, t3.y, sl), 0.0) * w0.x * w3.y;
        r += T.SampleLevel(S, float3(t12.x, t3.y, sl), 0.0) * w12.x * w3.y;
        r += T.SampleLevel(S, float3(t3.x, t3.y, sl), 0.0) * w3.x * w3.y;
        return saturate(r);
    }
};
FAttr TerrainAttr;
float3 attrDir = normalize(pb);
float4 attrA = TerrainAttr.Read(Attr, View.MaterialTextureBilinearClampedSampler, attrDir, 0.0, AttrRes);
float slope = attrA.r;
// From the baked terrain: where water runs, fresh lava, glacier ice, and what covers the ground
// (-1 bare sand and gravel .. 0 lichened lava .. 1 deep moss).
float flow = attrA.g;
float young = attrA.b;
float ice = attrA.a;
float cover = TerrainAttr.Read(Attr, View.MaterialTextureBilinearClampedSampler, attrDir, 6.0, AttrRes).r * 2.0 - 1.0;
float3 up = normalize(pb);
float lat = abs(up.z);
float driftA, driftB, driftC, driftD, driftE;
VNOISE(driftA, pb / 900.0)
VNOISE(driftB, pb / 140.0 + 17.0)
VNOISE(driftC, pb / 2600.0 + 41.0)
VNOISE(driftD, pb / 27.0 + 5.0)
VNOISE(driftE, pb / 8.0 + 29.0)
float drift = driftA * 0.65 + driftB * 0.35;

float snowLine = lerp(SnowLine, SnowLine * 0.12, smoothstep(0.45, 0.95, lat)) + (driftC - 0.5) * 220.0;
float wSnow = smoothstep(snowLine - 30.0, snowLine + 80.0, alt + (driftB - 0.5) * 40.0) * (1.0 - smoothstep(0.30, 0.58, slope));
wSnow = max(wSnow, smoothstep(0.25, 0.75, ice));
float steep = smoothstep(RockStart, RockStart + 0.13, slope + (driftB - 0.5) * 0.10);
// Black sand: the shore up to where the highest tide reaches, the outwash plains and the flats.
float beach = 1.0 - smoothstep(4.0, 9.0 + driftB * 5.0, alt);
float sandRaw = saturate(max(beach, smoothstep(0.15, 0.75, -cover + (driftB - 0.5) * 0.5)));
// Moss where the land is old and damp, thinning with height; lichened lava where it is not.
float mossy = saturate(cover + (drift - 0.5) * 0.9 + flow * 0.6) * (1.0 - smoothstep(260.0, 380.0, alt + (driftB - 0.5) * 60.0));
// Even on the moss lands the lava shows through in patches a few metres across.
mossy -= 0.45 * smoothstep(0.58, 0.8, driftE * 0.6 + driftD * 0.4);
float fieldRaw = 1.0 - smoothstep(0.25, 0.60, mossy);
float rockRaw = max(steep, young);
float wRock = rockRaw * (1.0 - wSnow);
float wSand = sandRaw * (1.0 - wSnow) * (1.0 - rockRaw);
float rest = (1.0 - wSnow) * (1.0 - rockRaw) * (1.0 - sandRaw);
float wField = fieldRaw * rest;
float wMoss = rest - wField;
// A stream's bed, whatever grows beside it: washed gravel and sand where the water idles, a
// tumble of rocks where it comes down steeply.
float streamBed = smoothstep(0.62, 0.80, flow);
float idle = 1.0 - smoothstep(0.15, 0.32, slope);
wSand += (wMoss + wField) * streamBed * idle;
wField = wField * (1.0 - streamBed * idle) + wMoss * streamBed * (1.0 - idle);
wMoss *= 1.0 - streamBed;
"""

# Sizes of the relief, in metres. RELIEF_MAX_M is the most the surface is ever moved.
RELIEF_MAX_M = 7.0
DISPLACE_FADE_PX = (110.0, 45.0)
PLANET_RELIEF = r"""
// Relief: what the terrain mesh, with a point every few metres, cannot hold. Metres the surface
// stands out from the mesh along its normal; never inward, so nothing scattered on the mesh floats.
#define BED_M 5.0
#define BLOCK_M 2.6
#define BLOCK_TALL_M 40.0
#define BED_RELIEF_M 1.7
#define ROCK_MID_M 2.0
#define ROCK_NEAR_M 0.22
#define ROCK_FACE_M 0.55
#define FIELD_MID_M 2.4
#define FIELD_NEAR_M 0.06
#define MOSS_MID_M 1.6
#define MOSS_NEAR_M 0.03
#define SAND_MID_M 0.10
#define SAND_NEAR_M 0.02
#define ICE_M 3.2
#define ICE_CREVASSE_M 13.0
float3 wb = pow(abs(N), 6.0);
wb /= (wb.x + wb.y + wb.z);
// shape: the relief large enough to light as geometry. fine: the scans' own close-up relief, which
// their normal maps already shade.
float shape = 0.0;
float fine = 0.0;
float crag = 0.0;
float crack = 1.0;
// Where this point stands in the lie of the ground, as the aerial scans have it: 0 in a hollow
// between rocks or hummocks, 1 on a crown. (Used only to shade; the relief does not read it.)
float lieSum = 0.0;
float lieW = 0.0;
// Cliffs are stacks of lava flows. Each flow is a bed some metres thick: the massive ones stand
// proud as ledges and are broken by upright joints into blocks and columns, wide in one flow and
// narrow in the next; between them lie thin, crumbling layers that weather back into the face.
float bedPos = (alt + (driftB - 0.5) * 14.0 + (driftE - 0.5) * 1.6) / BED_M;
bedPos += 0.33 * sin(bedPos * 2.3) + 0.21 * sin(bedPos * 0.83 + 1.7);
float bedI = floor(bedPos);
float bedF = bedPos - bedI;
float3 bedR = Cells.Hash3(float3(bedI, 3.0, 7.0));
float massive = smoothstep(0.22, 0.38, bedR.x);
float ledge = smoothstep(0.0, 0.07, bedF) * (1.0 - smoothstep(0.88, 1.0, bedF));
// (Not the young cones: cinder is a heap, not a stack of flows.)
float steepLand = steep * (1.0 - wSnow) * (1.0 - young);
[branch] if (steepLand > 0.004)
{
    // Tangent to the ground a block is BLOCK_M across (more or less, by the flow); it is far
    // taller, and the pattern shifts from bed to bed so one flow's joints do not carry on into the next.
    float across = BLOCK_M * lerp(0.55, 1.7, bedR.z);
    // No joint in rock runs ruler-straight: the pattern is bent a little at the scale of a block.
    float bend;
    VNOISE(bend, pb / 1.7 + 61.0)
    float3 q = pb / across + up * (alt * (1.0 / BLOCK_TALL_M - 1.0 / across)) + bedR * 9.0 + (driftE - 0.5) * 0.7 + (bend - 0.5) * 0.32;
    float2 blk = Cells.Blocks(q);
    // Joints stand open only in a face steep enough to shed what would fill them, and they are
    // weathered: a notch with worn shoulders, a quarter of the way into the bed, not a slot cut to
    // its back. One block in three has no open joint round it at all.
    crack = lerp(1.0, smoothstep(0.0, 0.11, blk.x), massive * smoothstep(0.40, 0.55, slope) * smoothstep(0.25, 0.45, blk.y));
    shape += steepLand * ledge * lerp(0.76, 1.0, crack) * lerp(0.12, lerp(0.62, 1.0, blk.y), massive) * BED_RELIEF_M;
}
[branch] if (wRock > 0.004)
{
    // Three sizes of the rock's own surface: the lie of the outcrop, the face of each block
    // (the close-up scan read four times larger), and the grain of the stone.
    float hr = TAPH(RockFH, MidTiling);
    shape += wRock * (hr * ROCK_MID_M + TAPH(RockH, Tiling * 4.0) * ROCK_FACE_M);
    lieSum += wRock * hr;
    lieW += wRock;
    fine += wRock * TAPH(RockH, Tiling) * ROCK_NEAR_M;
}
// Open ground keeps to the mesh over most of its area, so what grows and lies on it shows; it
// rises only into the rocks of the lava field and the hummocks of the moss.
[branch] if (wField > 0.004)
{
    float hf = TAPH(FieldFH, MidTiling);
    shape += wField * max(hf - 0.45, 0.0) * FIELD_MID_M;
    fine += wField * TAPH(FieldH, Tiling) * FIELD_NEAR_M;
    crag += wField * smoothstep(0.60, 0.74, hf);
    lieSum += wField * hf;
    lieW += wField;
}
[branch] if (wMoss > 0.004)
{
    float hm = TAPH(MossFH, MidTiling);
    shape += wMoss * max(hm - 0.58, 0.0) * MOSS_MID_M;
    lieSum += wMoss * hm;
    lieW += wMoss;
    fine += wMoss * TAPH(MossH, Tiling) * MOSS_NEAR_M;
}
[branch] if (wSand > 0.004)
{
    shape += wSand * TAPH(SandFH, MidTiling) * SAND_MID_M;
    fine += wSand * TAPH(SandH, Tiling) * SAND_NEAR_M;
}
// Glacier ice. It stands a few metres proud of the rock it lies on and is never smooth: wind
// packs its surface into low ridges, and wherever it flows over steeper ground it tears open in
// crevasses that run across the slope, in fields, with whole ice between them. On the steepest
// ground it breaks up altogether into blocks (an icefall).
float crevasse = 0.0;
[branch] if (ice > 0.25)
{
    float iceLand = smoothstep(0.25, 0.7, ice);
    float tornA, tornB;
    VNOISE(tornA, pb / 55.0 + 71.0)
    VNOISE(tornB, pb / 5.0 + 83.0)
    float stretched = smoothstep(0.16, 0.40, slope) * smoothstep(0.35, 0.6, tornA);
    float across = (alt + (driftB - 0.5) * 34.0 + (tornB - 0.5) * 3.5) / ICE_CREVASSE_M;
    float slot = smoothstep(0.78, 0.97, abs(frac(across) - 0.5) * 2.0);
    crevasse = slot * stretched * iceLand;
    float icefall = smoothstep(0.58, 0.74, slope) * smoothstep(0.45, 0.7, tornA) * iceLand;
    float blocks = 1.0;
    [branch] if (icefall > 0.004)
    {
        float2 cell = Cells.Blocks(pb / 15.0 + up * (alt * (1.0 / 11.0 - 1.0 / 15.0)) + (tornB - 0.5) * 0.5);
        blocks = lerp(1.0, smoothstep(0.0, 0.12, cell.x) * lerp(0.6, 1.0, cell.y), icefall);
        crevasse = max(crevasse, (1.0 - smoothstep(0.0, 0.12, cell.x)) * icefall * 0.7);
    }
    float packed;
    VNOISE(packed, pb / 1.6 + up * (alt * (1.0 / 0.5 - 1.0 / 1.6)) + 97.0)
    shape += iceLand * (ICE_M * (1.0 - crevasse) * blocks + abs(packed - 0.5) * 0.5);
}
// Water runs in a bed it has cleared: there the ground keeps to the mesh, and the water (a sheet
// lying just over the mesh, see BuildStreams) shows; the banks rise through it to either side.
float cleared = smoothstep(0.52, 0.80, flow);
// (Only a gentle bed is swept smooth; a steep one keeps its rocks and the water runs between them.)
float swept = cleared * (1.0 - 0.85 * smoothstep(0.15, 0.32, slope));
shape *= 1.0 - 0.9 * swept;
fine *= 1.0 - 0.5 * swept;
// (The game's copy of that threshold reads the flow through FEphemeraTerrain::FlowAsShaded: this
// value arrives in a vertex colour, which the engine stores sRGB-encoded.)
// Micro relief: no ground is a plane. It rolls over tens of metres and is hummocked at a stride's
// length, except where sand, tide or ice has smoothed it. What stands on the ground is placed by
// the game on this same function (FEphemeraTerrain::MicroReliefM): change one and change the other.
float m1, m2, m3;
VNOISE(m1, pb / 17.0 + 3.0)
VNOISE(m2, pb / 6.3 + 11.0)
VNOISE(m3, pb / 2.1 + 23.0)
float open = smoothstep(-0.55, -0.05, cover) * (1.0 - smoothstep(0.2, 0.6, ice)) * smoothstep(1.0, 6.0, alt) * (1.0 - cleared);
shape += (m1 * 0.9 + m2 * 0.45 + abs(m3 - 0.5) * 0.44) * open * Micro;
"""

PLANET_CODE = r"""
// A whole world's ground in one material. P, N: planet-local position (cm) and surface normal.
// UVd: x = height above sea level (m), y = continentalness. Vertex colour (VC3): R slope, G moisture.
// Five scanned layers (moss, rocky lava field, black sand, basalt, snow) are placed by altitude,
// slope, latitude and slow drift noise, and sampled world-aligned along the three local axes.
//
// Detail at every distance: each layer has two scans. One was taken standing on the ground (a
// few metres across; D, Nm, A) and is shown close to, at Tiling metres per repeat. The other was
// taken from the air (tens of metres across; FD, FN) and is shown further off, at MidTiling and
// then FarTiling. Each is used at something like its true size, so the ground is sharp underfoot
// and still has believable structure from a hilltop or from low orbit. (A close-up scan enlarged
// fifty times is a field of giant cobbles; an aerial one shrunk to four metres is a blur.)
//
// Colour is art-directed: every layer has one colour (what that ground is, as linear albedo), and
// the scan only varies it (its own colour divided by its local average). Near and far therefore agree,
// and a world's palette is set in one place.
float3 pm = P * 0.01;
float3 w = pow(abs(N), 6.0);
w /= (w.x + w.y + w.z);
#define SS View.MaterialTextureBilinearWrapedSampler
struct FGround
{
    float3 pm, dpx, dpy, w, N;
    float3 Colour(Texture2D T, SamplerState S, float metres)
    {
        float k = 1.0 / metres;
        return (Texture2DSampleGrad(T, S, pm.zy * k, dpx.zy * k, dpy.zy * k) * w.x
              + Texture2DSampleGrad(T, S, pm.xz * k, dpx.xz * k, dpy.xz * k) * w.y
              + Texture2DSampleGrad(T, S, pm.xy * k, dpx.xy * k, dpy.xy * k) * w.z).rgb;
    }
    // The scan's variation about its own local average: its colour divided by itself blurred to
    // an eighth of a repeat. A scan is rarely even from edge to edge (one side lighter, a patch of
    // shade), and laid out hundreds of metres wide those slow differences show as a chequerboard
    // of tiles; dividing them out leaves only the detail, and the repeat disappears.
    float3 Detail(Texture2D T, SamplerState S, float metres)
    {
        float k = 1.0 / metres;
        uint tw, th;
        T.GetDimensions(tw, th);
        float broadMip = max(log2((float)tw) - 3.0, 0.0);
        float3 broad = (Texture2DSampleLevel(T, S, pm.zy * k, broadMip) * w.x
                      + Texture2DSampleLevel(T, S, pm.xz * k, broadMip) * w.y
                      + Texture2DSampleLevel(T, S, pm.xy * k, broadMip) * w.z).rgb;
        return Colour(T, S, metres) / (broad + 0.004);
    }
    float3 Normal(Texture2D T, SamplerState S, float metres, float strength)
    {
        float k = 1.0 / metres;
        float3 tx = UnpackNormalMap(Texture2DSampleGrad(T, S, pm.zy * k, dpx.zy * k, dpy.zy * k)).xyz;
        float3 ty = UnpackNormalMap(Texture2DSampleGrad(T, S, pm.xz * k, dpx.xz * k, dpy.xz * k)).xyz;
        float3 tz = UnpackNormalMap(Texture2DSampleGrad(T, S, pm.xy * k, dpx.xy * k, dpy.xy * k)).xyz;
        tx = float3(tx.xy * strength + N.zy, abs(tx.z) * N.x);
        ty = float3(ty.xy * strength + N.xz, abs(ty.z) * N.y);
        tz = float3(tz.xy * strength + N.xy, abs(tz.z) * N.z);
        return normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z);
    }
};
FGround G;
G.pm = pm; G.dpx = ddx(pm); G.dpy = ddy(pm); G.w = w; G.N = N;
#define HASH(c) frac(sin(dot(c, float3(127.1, 311.7, 74.7))) * 43758.5453)
#define VNOISE(OUT, Q) { float3 c_ = floor(Q); float3 f_ = frac(Q); f_ = f_ * f_ * (3.0 - 2.0 * f_); \
    OUT = lerp(lerp(lerp(HASH(c_), HASH(c_ + float3(1, 0, 0)), f_.x), lerp(HASH(c_ + float3(0, 1, 0)), HASH(c_ + float3(1, 1, 0)), f_.x), f_.y), \
               lerp(lerp(HASH(c_ + float3(0, 0, 1)), HASH(c_ + float3(1, 0, 1)), f_.x), lerp(HASH(c_ + float3(0, 1, 1)), HASH(c_ + float3(1, 1, 1)), f_.x), f_.y), f_.z); }

float alt = UVd.x;
float3 pb = Pb * 0.01;
""" + PLANET_CELLS + PLANET_WEIGHTS + r"""
// The height of the relief here, read from the scans with the screen's own level of detail.
float3 dbx = ddx(pb), dby = ddy(pb);
#define TAPH(T, M) dot(float3(Texture2DSampleGrad(T, SS, pb.zy / (M), dbx.zy / (M), dby.zy / (M)).r, \
    Texture2DSampleGrad(T, SS, pb.xz / (M), dbx.xz / (M), dby.xz / (M)).r, \
    Texture2DSampleGrad(T, SS, pb.xy / (M), dbx.xy / (M), dby.xy / (M)).r), wb)
""" + PLANET_RELIEF + r"""
// The same relief shades the ground: how it changes across the screen tilts the normal, so light
// and shadow fall on the ledges and blocks the surface has actually been moved into.
float3 r1 = cross(dby, N), r2 = cross(N, dbx);
float det = dot(dbx, r1);
float3 grad = (ddx(shape) * r1 + ddy(shape) * r2) * (sign(det) / max(abs(det), 1e-9));
grad *= min(1.0, 3.0 / (length(grad) + 1e-5));
// Where the relief is not being drawn as geometry (ReliefShade below 1, set by the game on each
// piece of terrain), its shading alone is toned down: light and shadow on shapes that are not
// there, seen at a low angle, read as a restless pattern on flat ground.
grad *= ReliefShade;
// Far off, a ledge is narrower than a pixel and its shading is sampled, not seen: rows of dots
// along every cliff. The relief's light and shadow is eased back there to what survives.
grad *= lerp(1.0, 0.35, smoothstep(150.0, 700.0, Dist * 0.01));
float3 Nb = normalize(N - grad);
G.N = Nb;
G.w = pow(abs(Nb), 6.0);
G.w /= (G.w.x + G.w.y + G.w.z);
// Rock that breaks through the lava field is bare; the level tops of ledges hold moss.
wRock += crag;
wField = max(wField - crag, 0.0);
float shelf = wRock * smoothstep(0.80, 0.94, dot(Nb, up)) * steepLand * saturate(cover + 0.35) * (1.0 - young);
wRock -= shelf;
wMoss += shelf;

// Which sizes of the scans this pixel is shown, by its distance in metres.
float dM = Dist * 0.01;
float bNear = 1.0 - smoothstep(25.0, 120.0, dM);
float bFar = smoothstep(350.0, 2200.0, dM);
float bMid = saturate(1.0 - bNear - bFar);

float3 col = 0;
float3 n = 0;
float3 arm = 0;
// One layer: its colour times the scan's variation, its normal and its AO/roughness, at the two
// sizes in play. From far away fine roughness no longer shows, only how matt the ground is as a whole.
#define LAYER(Wt, D, Nm, A, FD, FN, C, FarRough, Relief) [branch] if (Wt > 0.004) { \
    float3 c_ = 0; float3 q_ = 0; float3 a_ = 0; \
    [branch] if (bNear > 0.003) { c_ += G.Detail(D, SS, Tiling) * bNear; q_ += G.Normal(Nm, SS, Tiling, 1.0) * bNear; a_ += G.Colour(A, SS, Tiling) * bNear; } \
    [branch] if (bMid > 0.003) { c_ += G.Detail(FD, SS, MidTiling) * bMid; q_ += G.Normal(FN, SS, MidTiling, 1.0) * bMid; a_ += float3(1.0, FarRough, 0.0) * bMid; } \
    [branch] if (bFar > 0.003) { c_ += G.Detail(FD, SS, FarTiling) * bFar; q_ += G.Normal(FN, SS, FarTiling, FarRelief * Relief) * bFar; a_ += float3(1.0, FarRough, 0.0) * bFar; } \
    col += C * clamp(c_, 0.45, 2.0) * Wt; n += q_ * Wt; arm += a_ * Wt; }
// Moss is not one green: drier, yellower patches a few tens of metres across, and darker damp ones.
float3 mossHue = MossC * lerp(0.75, 1.25, driftA) * lerp(float3(1.0, 1.0, 1.0), float3(1.25, 1.04, 0.72), smoothstep(0.52, 0.8, driftD));
// Nor is it one carpet. Over a stretch of fifty metres it is deep and dark in one place and thin
// and pale in the next, and here and there grey lichen has the better of it: a slope of moss seen
// from across a valley is a patchwork, and an even green is a lawn.
float patch;
VNOISE(patch, pb / 55.0 + 63.0)
float deep = smoothstep(0.22, 0.78, patch);
mossHue *= lerp(0.70, 1.24, deep);
float lichen = 0.42 * smoothstep(0.50, 0.78, driftB * 0.6 + (1.0 - patch) * 0.4);
mossHue = lerp(mossHue, dot(mossHue, float3(0.30, 0.59, 0.11)) * float3(1.08, 1.02, 0.90) * 1.15, lichen);
LAYER(wMoss, MossD, MossN, MossA, MossFD, MossFN, mossHue, 0.95, 1.0)
LAYER(wField, FieldD, FieldN, FieldA, FieldFD, FieldFN, FieldC * lerp(0.80, 1.18, deep), 0.93, 1.0)
LAYER(wSand, SandD, SandN, SandA, SandFD, SandFN, SandC, 0.88, 0.6)
LAYER(wRock, RockD, RockN, RockA, RockFD, RockFN, RockC, 0.90, 1.0)
// Snow and ice are smooth at a distance: the scan's wind-ripples, enlarged, would read as scratches.
LAYER(wSnow, SnowD, SnowN, SnowA, SnowFD, SnowFN, SnowC, 0.85, 0.15)
// Moss and lichened lava are matt, whatever a scan's roughness map says of the one patch it was
// taken from: a carpet of small leaves has no surface for the sky to glance off, and a sheen on it
// reads as a film of plastic over the land.
arm.g = lerp(arm.g, max(arm.g, 0.92), saturate(wMoss));
arm.g = lerp(arm.g, max(arm.g, 0.84), saturate(wField));
// Hollows are darker than crowns. The close scans carry that in their own occlusion maps, but only
// for a hand's breadth; this is the same thing at the size of the rocks and hummocks themselves
// and of the roll of the open ground, which is what gives a slope its modelling under a grey sky.
float lie = lieW > 0.01 ? lieSum / lieW : 0.5;
float hollow = lerp(0.55, 1.0, smoothstep(0.12, 0.60, lie)) * lerp(lerp(0.74, 1.0, smoothstep(0.22, 0.62, m1 * 0.6 + m2 * 0.4)), 1.0, 1.0 - open);
hollow = lerp(hollow, 1.0, bFar * 0.75);
arm.r *= hollow;

// Slow changes of tone across the land, so no two valleys are quite the same colour.
float tone = lerp(1.0, lerp(0.78, 1.16, driftB) * lerp(0.9, 1.08, driftC), 1.0 - wSnow);
// Sand is wet where the sea washes it: darker first, and only then a dull streak of sky. Only up
// close: from the air a shore is not a mirror.
float wet = Wetness * wSand * (1.0 - smoothstep(0.3, 3.5, alt)) * (1.0 - bFar);
arm.g = lerp(arm.g, max(arm.g * 0.35, 0.42), wet);
// And where the sea is on it now, or was a moment ago, the sand is a mirror: the film of water left on it holds
// the sky and whatever stands on the shore. Where that is depends on the tide, which runs metres here. Tide: the
// way to the moon in the planet's own space and, in w, how far it raises the sea under it (the bulge the sea
// itself is drawn with; the game sets it, AEphemeraBody::SetTide).
float toMoon = dot(normalize(P), normalize(Tide.xyz + float3(0.0, 0.0, 0.00001)));
float seaNow = Tide.w * (1.5 * toMoon * toMoon - 0.5);
float film = Wetness * wSand * (1.0 - smoothstep(0.04, 0.65, alt - seaNow)) * (1.0 - bFar);
arm.g = lerp(arm.g, lerp(0.09, 0.28, smoothstep(20.0, 120.0, dM)), film);   // past twenty metres the film's mirror is glitter, not detail
n = lerp(n, normalize(N) * length(n), film * 0.85);         // the film lies flat over the sand's ripples
Nrm = normalize(n + N * 0.0001);
// Blue carries how strongly the surface mirrors at a glancing angle. Each kind of ground has its
// own: least for moss (all pits and shadow), most for bare stone. A pit mirrors nothing, so the
// scan's occlusion takes it down further; and seen from far off, ground is rough at every scale
// and loses the pale sheen a smooth plane would have.
float mirror = (wMoss * 0.18 + wField * 0.28 + wSand * 0.33 + wRock * 0.35 + wSnow * 0.25) / max(wMoss + wField + wSand + wRock + wSnow, 0.001);
ORM = float3(arm.r, arm.g, mirror * lerp(1.0, 0.6, bFar) * lerp(0.45, 1.0, arm.r));
ORM.b = lerp(ORM.b, 0.25, wet);
ORM.b = lerp(ORM.b, 0.3, film);
col *= tone * lerp(1.0, 0.6, wet) * lerp(1.0, 0.8, film);
// (A hollow gathers what is dark and damp, too: a little of its shade is in the ground's own colour.)
col *= lerp(0.84, 1.0, hollow);
// Only from far off is an ice cap white. Stood upon, old snow is grey with the ash of the last
// eruptions, laid in drifts by the wind, and a camera set for dark lava would burn clean snow out.
col *= lerp(1.0, lerp(0.34, 0.66, smoothstep(0.30, 0.72, driftD * 0.55 + driftE * 0.45)), wSnow * (1.0 - bFar));
// Cliffs show the lava flows they are built of: each bed its own tone, dark in the joints.
float jointed = ledge * crack;
col *= lerp(1.0, lerp(0.74, 1.12, bedR.y) * lerp(0.86, 1.0, jointed), steepLand * wRock);
// The crumbling layers between flows are baked red-brown where the next flow ran over them.
col *= lerp(float3(1.0, 1.0, 1.0), float3(1.42, 1.02, 0.80), (1.0 - massive) * steepLand * wRock);
// Water seeps down a face in dark streaks, and pale lichen takes the dry rock between them.
float seepA, seepB;
VNOISE(seepA, pb / 3.1 + up * (alt * (1.0 / 70.0 - 1.0 / 3.1)) + 51.0)
VNOISE(seepB, pb / 9.0 + up * (alt * (1.0 / 160.0 - 1.0 / 9.0)) + 77.0)
float seep = smoothstep(0.52, 0.78, seepA * 0.6 + seepB * 0.4);
col *= lerp(1.0, lerp(1.10, 0.62, seep), steepLand * wRock * smoothstep(0.40, 0.58, slope));
ORM.r *= lerp(1.0, lerp(0.62, 1.0, jointed), steepLand);
// Fresh lava is close to black; glacier ice is blue-white where it is thick and bare.
col *= lerp(1.0, 0.45, young * (1.0 - wSnow));
col = lerp(col, IceC.rgb * lerp(0.85, 1.1, driftB), smoothstep(0.55, 1.0, ice) * (1.0 - smoothstep(0.2, 0.45, slope)) * 0.6);
// Inside a crevasse the ice is clean and old and deep blue; the light that reaches the bottom has
// come a long way through it. Close to, the walls are glassy.
col = lerp(col, IceC.rgb * float3(0.30, 0.52, 0.70), crevasse * 0.75);
ORM.g = lerp(ORM.g, 0.28, crevasse * (1.0 - bFar));
ORM.r *= lerp(1.0, 0.5, crevasse);
// A glacier's edge is its dirtiest part: the rock it has ground up melts out there, and the
// last of the ice lies grey-brown under it.
float margin = smoothstep(0.22, 0.36, ice) * (1.0 - smoothstep(0.42, 0.66, ice));
col = lerp(col, RockC.rgb * float3(1.5, 1.35, 1.2) * lerp(0.8, 1.3, driftE), margin * 0.8 * (1.0 - bFar * 0.6));
// Running water: damp, darker ground beside a stream, and the stream itself.
float damp = smoothstep(0.15, 0.6, flow) * (1.0 - wSnow);
col *= lerp(1.0, 0.72, damp);
// Rock is wet only where there is water: in the seeps down a face and beside a stream. There it
// is dark (above) with a dull sheen; all other rock is dry and matt.
float wetRock = wRock * Wetness * saturate(seep * steepLand * smoothstep(0.40, 0.58, slope) + damp);
ORM.g = lerp(ORM.g, max(ORM.g * 0.6, 0.38), wetRock);
ORM.b = lerp(ORM.b, 0.25, wetRock);
// A stream's bed: dark, wet stones. (Open water painted onto the ground is a mirror with no
// surface of its own, and over displaced ground it breaks up into dots; streams want real water.)
float river = smoothstep(0.80, 0.96, flow) * (1.0 - smoothstep(0.5, 0.9, ice));
col = lerp(col, col * 0.55 + WaterC.rgb, river);
ORM.g = lerp(ORM.g, 0.45, river);
return col;
"""

# Fine relief the terrain mesh is too coarse to hold (it has a point every few metres): the engine
# splits the mesh into pixel-sized triangles and moves each along its normal by this height.
DISPLACE_CODE = r"""
// P: planet-local position (cm) of a point on the terrain mesh. Dist: to the camera (cm).
float3 pb = P * 0.01;
#define SS View.MaterialTextureBilinearWrapedSampler
""" + PLANET_CODE[PLANET_CODE.index("#define VNOISE(OUT, Q)"):PLANET_CODE.index("float alt = UVd.x;")] + r"""
float alt = UVd.x;
""" + PLANET_CELLS + PLANET_WEIGHTS + r"""
// The scans are read at the level of detail the split mesh can show at this distance: about three
// pixels between its points, against a texture 4096 across.
float lodBase = log2(max(Dist * 0.01, 1.0) * 0.0018 * 4096.0) - 0.5;
#define TAPH(T, M) dot(float3(Texture2DSampleLevel(T, SS, pb.zy / (M), clamp(lodBase - log2(M), 0.0, 10.0)).r, \
    Texture2DSampleLevel(T, SS, pb.xz / (M), clamp(lodBase - log2(M), 0.0, 10.0)).r, \
    Texture2DSampleLevel(T, SS, pb.xy / (M), clamp(lodBase - log2(M), 0.0, 10.0)).r), wb)
""" + PLANET_RELIEF + f"""
return saturate((shape + fine) / {RELIEF_MAX_M});
"""


# The near ground (UEphemeraGroundSurfaceComponent): a grid of rings under the camera (the near sea's mesh,
# UEphemeraPlanetBuilder::BuildSeaGrid) laid on the land and raised into its relief by the material, which
# the terrain chunks could only shade. Its numbers reach the game through the body (dress_ground_grid).
GROUND_GRID = {"rings": 6, "half_cells": 96, "spacing_m": 0.25}

NEAR_GROUND_CODE = r"""
// One vertex of the near ground's grid (UEphemeraGroundSurfaceComponent). Laid out exactly as the near sea's
// (SEA_GRID_CODE): Flat, where it was built (cm); Ring, its ring and that ring's spacing (m); GroundFace, the
// lattice's cube face and in w the planet's radius (m); GroundU, GroundV, that face's axes and in w the
// camera's place along them (m of arc); GroundHole.w, how far the hole in the chunks reaches. Returns how far
// to move the vertex (cm, planet space); Rest: where the land is under it before its relief; Up: the land's
// normal there.
struct FGroundGrid
{
    // The terrain's height (m) along a direction: Heights is EphemeraPlanetBuilder::BuildTerrainHeights (the
    // attributes' grid, floats, two cells of apron), read point by point through a Catmull-Rom cubic, exactly
    // as the game reads its height field (UEphemeraHeightField::HeightM). Hardware filtering would interpolate
    // with 8-bit weights: on a steep cell, steps of centimetres, a staircase on every slope.
    // Also the land's normal there, from the cubic's own slope (the same sixteen texels): Nrm.
    float Height(Texture2DArray T, float3 d, float res, out float3 Nrm, float R)
    {
        float3 a = abs(d);
        float3 n, u, v;
        float face;
        if (a.x >= a.y && a.x >= a.z)
        {
            if (d.x > 0.0) { face = 0.0; n = float3(1, 0, 0); u = float3(0, 1, 0); v = float3(0, 0, 1); }
            else           { face = 1.0; n = float3(-1, 0, 0); u = float3(0, 0, 1); v = float3(0, 1, 0); }
        }
        else if (a.y >= a.z)
        {
            if (d.y > 0.0) { face = 2.0; n = float3(0, 1, 0); u = float3(0, 0, 1); v = float3(1, 0, 0); }
            else           { face = 3.0; n = float3(0, -1, 0); u = float3(1, 0, 0); v = float3(0, 0, 1); }
        }
        else
        {
            if (d.z > 0.0) { face = 4.0; n = float3(0, 0, 1); u = float3(1, 0, 0); v = float3(0, 1, 0); }
            else           { face = 5.0; n = float3(0, 0, -1); u = float3(0, 1, 0); v = float3(1, 0, 0); }
        }
        float3 q = d / dot(d, n);
        float2 ang = atan(float2(dot(q, u), dot(q, v)));
        float2 t = ang * (4.0 / 3.14159265);
        float2 g = (t * res + res) * 0.5 + 2.0;
        float2 i0 = floor(g);
        float2 f = g - i0;
        float4 wx = float4(f.x * (-0.5 + f.x * (1.0 - 0.5 * f.x)), 1.0 + f.x * f.x * (-2.5 + 1.5 * f.x),
                           f.x * (0.5 + f.x * (2.0 - 1.5 * f.x)), f.x * f.x * (-0.5 + 0.5 * f.x));
        float4 wy = float4(f.y * (-0.5 + f.y * (1.0 - 0.5 * f.y)), 1.0 + f.y * f.y * (-2.5 + 1.5 * f.y),
                           f.y * (0.5 + f.y * (2.0 - 1.5 * f.y)), f.y * f.y * (-0.5 + 0.5 * f.y));
        float4 dx = float4(-0.5 + f.x * (2.0 - 1.5 * f.x), f.x * (-5.0 + 4.5 * f.x), 0.5 + f.x * (4.0 - 4.5 * f.x), f.x * (-1.0 + 1.5 * f.x));
        float4 dy = float4(-0.5 + f.y * (2.0 - 1.5 * f.y), f.y * (-5.0 + 4.5 * f.y), 0.5 + f.y * (4.0 - 4.5 * f.y), f.y * (-1.0 + 1.5 * f.y));
        int2 o = int2(i0) - 1;
        int s = int(face);
        float h = 0.0, hx = 0.0, hy = 0.0;
        [unroll] for (int b = 0; b < 4; ++b)
        {
            float4 row = float4(T.Load(int4(o.x, o.y + b, s, 0)).r, T.Load(int4(o.x + 1, o.y + b, s, 0)).r,
                                T.Load(int4(o.x + 2, o.y + b, s, 0)).r, T.Load(int4(o.x + 3, o.y + b, s, 0)).r);
            h += dot(row, wx) * wy[b];
            hx += dot(row, dx) * wy[b];
            hy += dot(row, wx) * dy[b];
        }
        // The surface S = d (R + h) as the grid coordinates move one cell: the direction turns (one cell is
        // pi/2/res of angle across the face, through the tan() warp) and the height changes by hx, hy.
        float3 qq = n + tan(ang.x) * u + tan(ang.y) * v;
        float ql = length(qq);
        float dA = 3.14159265 / (2.0 * res);
        float3 dqx = u * dA / (cos(ang.x) * cos(ang.x));
        float3 dqy = v * dA / (cos(ang.y) * cos(ang.y));
        float3 ddx_ = (dqx - d * dot(d, dqx)) / ql;
        float3 ddy_ = (dqy - d * dot(d, dqy)) / ql;
        float3 sx = ddx_ * (R + h) + d * hx;
        float3 sy = ddy_ * (R + h) + d * hy;
        Nrm = normalize(cross(sx, sy));
        Nrm = dot(Nrm, d) < 0.0 ? -Nrm : Nrm;
        return h;
    }
};
FGroundGrid Grid;
float g_spacing = Ring.y;
float2 g_cell = round(Flat.xy * 0.01 / g_spacing);
float g_reach = max(abs(g_cell.x), abs(g_cell.y));
float g_morph = saturate((g_reach - $MORPH_FROM) / ($MORPH_TO - $MORPH_FROM));
g_cell -= (g_cell - 2.0 * floor(g_cell * 0.5)) * g_morph;
float2 g_centre = floor(float2(GroundU.w, GroundV.w) / (2.0 * g_spacing)) * (2.0 * g_spacing);
float2 g_arc = g_centre + g_cell * g_spacing;
float g_R = GroundFace.w;
float3 g_up = normalize(GroundFace.xyz + tan(g_arc.x / g_R) * GroundU.xyz + tan(g_arc.y / g_R) * GroundV.xyz);
float3 g_n;
float g_h = Grid.Height(Heights, g_up, AttrRes, g_n, g_R);
float g_spacingNow = g_spacing * (1.0 + g_morph);
// What the relief (DISPLACE_CODE) asks of a point: where it is, the land's normal and height there, and how
// far it is from the camera (along the ground: the eye's height is left out).
float3 P = g_up * (g_R + g_h) * 100.0;
float3 N = g_n;
float2 UVd = float2(g_h, 0.0);
float2 g_off = g_arc - float2(GroundU.w, GroundV.w);
float Dist = length(g_off) * 100.0;
$RELIEF
// The relief stands up only where the grid is fine enough to hold it (about a metre between points: the
// blocks and ledges are two or three), dying away over the next ring; and the scans' own close-up relief
// only on the finest rings. By the hole's edge the grid is the plain land, as the chunks are.
float g_hole = max(GroundHole.w, 1.0);
float g_near = 1.0 - smoothstep(0.45 * g_hole, 0.8 * g_hole, max(abs(g_off.x), abs(g_off.y)));
float g_gain = (1.0 - smoothstep(1.0, 3.0, g_spacingNow)) * g_near;
float g_fine = 1.0 - smoothstep(0.3, 0.6, g_spacingNow);
// Where things grow, the ground stands up only by the part of its relief the game knows too (the micro relief,
// FEphemeraTerrain::MicroReliefM: grass and stones are placed on it). The rest is read from the scans, which
// the game cannot: raised as geometry it buried every plant and stone on the moss and the lava field. On rock
// and ice, where nothing is placed, all of it stands up.
float g_micro = (m1 * 0.9 + m2 * 0.45 + abs(m3 - 0.5) * 0.44) * open * Micro;
float g_grows = saturate(wMoss + wField + wSand);
float g_relief = NearRelief > 1.5 ? g_micro : g_micro + (shape - g_micro) * (1.0 - g_grows) + fine * g_fine * (1.0 - g_grows);
Rest = P;
Up = N;
return P + N * (g_relief * g_gain * min(NearRelief, 1.0) * 100.0) - Flat;
"""

GROUND_HOLE_CODE = r"""
// The chunks' hole (UEphemeraGroundSurfaceComponent): 0 where the near ground's grid is the land, within
// GroundHole.w metres of arc of the camera along both axes of the grid's lattice face (as NEAR_GROUND_CODE
// lays the grid out, so the hole is always inside it); 1 elsewhere, and everywhere while there is no grid.
float3 up = normalize(P);
float along = dot(up, GroundFace.xyz);
float2 arc = GroundFace.w * float2(atan2(dot(up, GroundU.xyz), max(along, 0.0001)), atan2(dot(up, GroundV.xyz), max(along, 0.0001)));
float reach = max(abs(arc.x - GroundU.w), abs(arc.y - GroundV.w));
return (GroundHole.w > 0.0 && along > 0.0 && reach < GroundHole.w) ? 0.0 : 1.0;
"""


# The near ground's pixels: the ground's own, but its relief is geometry. The relief's light (PLANET_CODE) is its
# slope across the screen, measured on the land before the relief moved it; on the near ground, across a steep
# face of a block, that land folds up under a few pixels and the slope came out at random from one 2x2 block of
# pixels to the next: a checkerboard over every rock face. Where the land folds so, the relief's own light is left
# out (the land's normal alone; the face is still shadowed by the real geometry). (Its facets instead, from the
# screen slope of the moved surface: a low-polygon look over every ledge.)
NEAR_PIXEL_CODE = PLANET_CODE.replace("float3 Nb = normalize(N - grad);\n", """float3 dpx = ddx(P), dpy = ddy(P);
float spread = abs(dot(cross(dpx, dpy), N));
float folded = 1.0 - smoothstep(0.08, 0.35, abs(det) / max(spread, 1e-9));
float3 Nb = normalize(N - grad * (1.0 - folded));
// The scans are still projected along the way the face really looks (its facet), or a steep face takes the
// slope's projection and its stone is smeared down it.
float3 facet = normalize(cross(dpy, dpx));
facet = dot(facet, N) < -0.2 ? -facet : facet;
float3 Nw = normalize(lerp(Nb, facet, folded));
""").replace("G.w = pow(abs(Nb), 6.0);", "G.w = pow(abs(Nw), 6.0);")
assert NEAR_PIXEL_CODE != PLANET_CODE


def near_ground_code():
    """NEAR_GROUND_CODE with the relief (DISPLACE_CODE's, up to its last line) and the grid's numbers filled in."""
    import string
    tail = DISPLACE_CODE.rindex("return saturate(")
    cells = GROUND_GRID["half_cells"]
    # The joints and crevasses are notches a few tens of centimetres wide: sampled by vertices half a metre or
    # a metre apart they came out as black spikes along every ledge. Here they are no narrower than the grid
    # can carry (two of its spacings), measured in each pattern's own units.
    relief = DISPLACE_CODE[:tail]
    # And no edge of the relief is sharper than three of the grid's spacings, nor any scan read finer than two
    # of its texels to a spacing: a ledge's edge or a joint that steps between two neighbouring vertices makes a
    # row of long thin triangles, and their shadows were black sawteeth along every ledge.
    for narrow, wide in (("smoothstep(0.0, 0.11, blk.x)", "smoothstep(0.0, max(0.11, 3.0 * g_spacingNow / across), blk.x)"),
                         ("smoothstep(0.0, 0.12, cell.x)", "smoothstep(0.0, max(0.12, 3.0 * g_spacingNow / 15.0), cell.x)"),
                         ("smoothstep(0.0, 0.07, bedF)", "smoothstep(0.0, max(0.07, 3.0 * g_spacingNow / BED_M), bedF)"),
                         ("smoothstep(0.88, 1.0, bedF)", "smoothstep(min(0.88, 1.0 - 3.0 * g_spacingNow / BED_M), 1.0, bedF)"),
                         ("clamp(lodBase - log2(M), 0.0, 10.0)", "clamp(max(lodBase - log2(M), log2(g_spacingNow * 2.0 * 4096.0 / (M))), 0.0, 10.0)")):
        if narrow not in relief:
            raise RuntimeError(f"near ground: the relief no longer has {narrow}")
        relief = relief.replace(narrow, wide)
    return string.Template(NEAR_GROUND_CODE).substitute(RELIEF=relief, MORPH_FROM=_hlsl(0.75 * cells),
                                                        MORPH_TO=_hlsl(cells - 4.0))


def world_parameters():
    """Numbers that every material of a world can read while the game runs (a material parameter collection).

    MoonTide: the way to the tide-raising moon in the planet's own space and, in w, the tide's amplitude in
    metres. SkyState: x is how far into night the eye is (0 day .. 1 night), set by the sky rig. The collection
    is made once and then kept, entries only ever added: materials find an entry by an id it is given when
    made, and a new collection would leave every material built before it reading nothing.
    """
    path = f"{ROOT}/Materials/MPC_World"
    if ASSETS.does_asset_exist(path):
        collection = ASSETS.load_asset(path)
    else:
        collection = TOOLS.create_asset("MPC_World", f"{ROOT}/Materials", unreal.MaterialParameterCollection,
                                        unreal.MaterialParameterCollectionFactoryNew())
    entries = list(collection.get_editor_property("vector_parameters"))
    names = [str(e.get_editor_property("parameter_name")) for e in entries]
    wanted = {"MoonTide": unreal.LinearColor(1.0, 0.0, 0.0, 0.0), "SkyState": unreal.LinearColor(0.0, 0.0, 0.0, 0.0)}
    added = False
    for name, default in wanted.items():
        if name not in names:
            entry = unreal.CollectionVectorParameter()
            entry.set_editor_property("parameter_name", name)
            entry.set_editor_property("default_value", default)
            entries.append(entry)
            added = True
    if added:
        collection.set_editor_property("vector_parameters", entries)
        ASSETS.save_loaded_asset(collection)
    return collection


def make_planet_material(name, layers, far_layers, tiling_m=3.2, rock_start=0.16, snow_line_m=420.0, colours=None, wetness=1.0,
                         mid_tiling_m=64.0, far_tiling_m=420.0, far_relief=1.6, micro_relief=1.0, attributes=None,
                         variant=None, land=None):
    """variant: None, the ground as the terrain chunks draw it; "far", the same with a hole cut where the near
    ground lies (GROUND_HOLE_CODE); "near", the near ground's grid (NEAR_GROUND_CODE; land: the terrain's
    heights from terrain_heights()).
    micro_relief: scale of the small relief (must match the body definition's micro_relief).
    attributes: (texture array, grid cells per face) from terrain_attributes(): the terrain's own values per pixel.
    layers: moss/field/sand/rock/snow -> {'d','n','arm','h'}, scanned close to. far_layers: the same five scanned
    from the air ({'d','n','h'}). colours: each layer's colour (linear albedo). Snow needs no height map.

    wetness: 1 where a sea wets the shore, 0 on a dry world. far_relief: how strongly the largest
    reading of the scans' normal maps models the land seen from altitude.
    """
    colours = colours or {}
    path = f"{ROOT}/Materials/{name}"
    stamp = stamp_of("planet-v26", DISPLACE_FADE_PX, PLANET_CODE, layers, far_layers, tiling_m, rock_start, snow_line_m, colours, wetness,
                     mid_tiling_m, far_tiling_m, far_relief, micro_relief, DISPLACE_CODE, attributes,
                     *(() if not variant else ("ground-v10", variant, land, GROUND_GRID, NEAR_GROUND_CODE, GROUND_HOLE_CODE, NEAR_PIXEL_CODE)))
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset(name, f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("tangent_space_normal", False)

    def expr(cls, x=-900, y=0):
        return EDIT.create_material_expression(material, cls, x, y)

    def scalar(parameter, value):
        node = expr(unreal.MaterialExpressionScalarParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", value)
        return node

    def colour(parameter, value):
        node = expr(unreal.MaterialExpressionVectorParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
        return node

    world = expr(unreal.MaterialExpressionWorldPosition)
    local_pos = expr(unreal.MaterialExpressionTransformPosition)
    local_pos.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local_pos.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, "", local_pos, "")
    normal_ws = expr(unreal.MaterialExpressionVertexNormalWS)
    local_normal = expr(unreal.MaterialExpressionTransform)
    local_normal.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    local_normal.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    connect(normal_ws, "", local_normal, "")
    vertex = expr(unreal.MaterialExpressionVertexColor)
    data_uv = expr(unreal.MaterialExpressionTextureCoordinate)
    camera = expr(unreal.MaterialExpressionCameraPositionWS)
    distance = expr(unreal.MaterialExpressionDistance)
    connect(world, "", distance, "A")
    connect(camera, "", distance, "B")
    # Where the pixel was on the terrain mesh, before the relief moved it: the relief is a function
    # of that place, and has to be asked the same question when shading as when moving the surface.
    base_world = expr(unreal.MaterialExpressionWorldPosition)
    base_world.set_editor_property("world_position_shader_offset", unreal.WorldPositionIncludedOffsets.WPT_EXCLUDE_ALL_SHADER_OFFSETS)
    base_pos = expr(unreal.MaterialExpressionTransformPosition)
    base_pos.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    base_pos.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(base_world, "", base_pos, "")
    heights = []
    for layer in ("moss", "field", "sand", "rock"):
        for prefix, source in (("", layers), ("F", far_layers)):
            node = expr(unreal.MaterialExpressionTextureObjectParameter, -1300, 0)
            node.set_editor_property("parameter_name", f"{layer.title()}{prefix}H")
            node.set_editor_property("texture", source[layer]["h"])
            node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
            heights.append((f"{layer.title()}{prefix}H", node, ""))
    # Where the moon is and how high it raises the sea, from the game, for every piece of ground at once.
    tide = expr(unreal.MaterialExpressionCollectionParameter)
    tide.set_editor_property("collection", world_parameters())
    tide.set_editor_property("parameter_name", "MoonTide")
    # Per piece of terrain, from the game (AEphemeraBody): 1 while the relief is geometry.
    relief_shade = scalar("ReliefShade", 1.0)
    relief_shade.set_editor_property("use_custom_primitive_data", True)
    relief_shade.set_editor_property("primitive_data_index", 0)
    if not attributes:
        raise RuntimeError(f"{name}: no terrain attributes (terrain_attributes())")
    attr_node = expr(unreal.MaterialExpressionTextureObject, -1300, 900)
    attr_node.set_editor_property("texture", attributes[0])
    attr_node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    sources = [
        ("P", local_pos, ""), ("Pb", base_pos, ""), ("N", local_normal, ""), ("UVd", data_uv, ""), ("VC3", vertex, ""), ("VCA", vertex, "A"),
        ("Dist", distance, ""), *heights,
        ("Attr", attr_node, ""), ("AttrRes", scalar("AttrRes", float(attributes[1])), ""),
        ("IceC", colour("IceColour", (0.50, 0.66, 0.74)), ""), ("WaterC", colour("WaterColour", (0.010, 0.030, 0.036)), ""),
        ("Tiling", scalar("Tiling", tiling_m), ""), ("RockStart", scalar("RockStart", rock_start), ""),
        ("Micro", scalar("MicroRelief", micro_relief), ""), ("ReliefShade", relief_shade, ""),
        ("SnowLine", scalar("SnowLine", snow_line_m), ""),
        ("MidTiling", scalar("MidTiling", mid_tiling_m), ""), ("FarTiling", scalar("FarTiling", far_tiling_m), ""),
        ("FarRelief", scalar("FarRelief", far_relief), ""), ("Wetness", scalar("Wetness", wetness), ""),
        ("Tide", tide, ""),
        # Iceland from the air: vivid moss, grey-green lichen on old lava, black sand, dark basalt, snow.
        # As measured albedo (dry basalt throws back 5 to 8% of the light, moss and turf 3 to 8%): set
        # darker than that, the ground's own colour sinks under the sky's reflection in it and the
        # whole land looks varnished.
        ("MossC", colour("MossColour", colours.get("moss", (0.050, 0.074, 0.032))), ""),
        ("FieldC", colour("FieldColour", colours.get("field", (0.064, 0.070, 0.055))), ""),
        ("SandC", colour("SandColour", colours.get("sand", (0.033, 0.033, 0.036))), ""),
        ("RockC", colour("RockColour", colours.get("rock", (0.046, 0.046, 0.052))), ""),
        # An ice cap is old snow and firn, not fresh powder: about 0.65-0.75 (fresh snow, 0.8-0.9, clipped to flat
        # white from orbit and showed none of the cap's tone).
        ("SnowC", colour("SnowColour", colours.get("snow", (0.66, 0.70, 0.76))), ""),
    ]
    ground = {}
    if variant:
        # Where the near ground lies (UEphemeraGroundSurfaceComponent sets these on both its own material and
        # the chunks'), as the near sea's grid is told.
        for parameter, default in (("GroundFace", (1.0, 0.0, 0.0, 5000.0)), ("GroundU", (0.0, 1.0, 0.0, 0.0)),
                                   ("GroundV", (0.0, 0.0, 1.0, 0.0)), ("GroundHole", (0.0, 0.0, 0.0, 0.0))):
            node = colour(parameter, (0.0, 0.0, 0.0))
            node.set_editor_property("default_value", unreal.LinearColor(*default))
            ground[parameter] = node
    if variant == "near":
        if not land:
            raise RuntimeError(f"{name}: the near ground needs the terrain's heights (terrain_heights())")
        by_name = {input_name: (node, pin) for input_name, node, pin in sources}
        height_node = expr(unreal.MaterialExpressionTextureObject, -1300, 1200)
        height_node.set_editor_property("texture", land[0])
        height_node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
        flat = expr(unreal.MaterialExpressionPreSkinnedPosition, -900, 900)
        ring = expr(unreal.MaterialExpressionTextureCoordinate, -900, 1000)
        ring.set_editor_property("coordinate_index", 1)
        vertex_sources = [("Flat", flat, ""), ("Ring", ring, ""), ("Heights", height_node, ""), ("NearRelief", scalar("NearRelief", 1.0), ""),
                          *[(k, *by_name[k]) for k in ("Tiling", "MidTiling", "RockStart", "SnowLine", "Micro", "Attr", "AttrRes")],
                          *heights,
                          *[(k, node, "RGBA") for k, node in ground.items()]]
        place = expr(unreal.MaterialExpressionCustom, -300, 900)
        place_inputs = []
        for input_name, _, _ in vertex_sources:
            entry = unreal.CustomInput()
            entry.set_editor_property("input_name", input_name)
            place_inputs.append(entry)
        extra = []
        for output_name in ("Rest", "Up"):
            entry = unreal.CustomOutput()
            entry.set_editor_property("output_name", output_name)
            entry.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
            extra.append(entry)
        place.set_editor_property("inputs", place_inputs)
        place.set_editor_property("additional_outputs", extra)
        place.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        place.set_editor_property("code", near_ground_code())
        for input_name, node, pin in vertex_sources:
            if not connect(node, pin, place, input_name):
                raise RuntimeError(f"{name}: could not connect {input_name} to the near ground's vertices")
        resting = expr(unreal.MaterialExpressionVertexInterpolator, -100, 900)
        connect(place, "Rest", resting, "VS")
        facing = expr(unreal.MaterialExpressionVertexInterpolator, -100, 1000)
        connect(place, "Up", facing, "VS")
        # The pixels ask the land's questions of where it rests under them, as the chunks' pixels do of their mesh.
        height_here = expr(unreal.MaterialExpressionCustom, -300, 1100)
        entries = []
        for input_name in ("Pb", "GroundFace"):
            entry = unreal.CustomInput()
            entry.set_editor_property("input_name", input_name)
            entries.append(entry)
        height_here.set_editor_property("inputs", entries)
        height_here.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT2)
        height_here.set_editor_property("code", "return float2(length(Pb) * 0.01 - GroundFace.w, 0.0);")
        connect(resting, "", height_here, "Pb")
        connect(ground["GroundFace"], "RGBA", height_here, "GroundFace")
        # Its relief is geometry for a couple of hundred metres; past that only shading, as on the chunks
        # (ReliefShade, set by the game as on them), so where the grid meets the chunks the light agrees.
        shade = expr(unreal.MaterialExpressionCustom, -300, 1200)
        entries = []
        for input_name in ("Shade", "Dist"):
            entry = unreal.CustomInput()
            entry.set_editor_property("input_name", input_name)
            entries.append(entry)
        shade.set_editor_property("inputs", entries)
        shade.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        shade.set_editor_property("code", "return lerp(1.0, Shade, smoothstep(12000.0, 22000.0, Dist));")
        connect(relief_shade, "", shade, "Shade")
        connect(distance, "", shade, "Dist")
        swap = {"Pb": (resting, ""), "N": (facing, ""), "UVd": (height_here, ""), "ReliefShade": (shade, "")}
        sources = [(input_name, *swap.get(input_name, (node, pin))) for input_name, node, pin in sources]
    sampler_types = {"d": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "n": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                     "arm": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}
    suffix = {"d": "D", "n": "N", "arm": "A"}
    for layer in ("moss", "field", "sand", "rock", "snow"):
        for key in ("d", "n", "arm"):
            node = expr(unreal.MaterialExpressionTextureObjectParameter, -1300, 0)
            node.set_editor_property("parameter_name", f"{layer.title()}{suffix[key]}")
            node.set_editor_property("texture", layers[layer][key])
            node.set_editor_property("sampler_type", sampler_types[key])
            sources.append((f"{layer.title()}{suffix[key]}", node, ""))
        for key in ("d", "n"):
            node = expr(unreal.MaterialExpressionTextureObjectParameter, -1300, 0)
            node.set_editor_property("parameter_name", f"{layer.title()}F{suffix[key]}")
            node.set_editor_property("texture", far_layers[layer][key])
            node.set_editor_property("sampler_type", sampler_types[key])
            sources.append((f"{layer.title()}F{suffix[key]}", node, ""))
    custom = expr(unreal.MaterialExpressionCustom, -300, 0)
    inputs = []
    for input_name, _, _ in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    outputs = []
    for output_name in ("Nrm", "ORM"):
        entry = unreal.CustomOutput()
        entry.set_editor_property("output_name", output_name)
        entry.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        outputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", PLANET_CODE if variant != "near" else NEAR_PIXEL_CODE)
    for input_name, node, pin in sources:
        if not connect(node, pin, custom, input_name):
            raise RuntimeError(f"{name}: could not connect {input_name}")
    connect_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    world_normal = expr(unreal.MaterialExpressionTransform, -100, 200)
    world_normal.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world_normal.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(custom, "Nrm", world_normal, "")
    connect_property(world_normal, "", unreal.MaterialProperty.MP_NORMAL)
    for channel, prop in (("r", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION), ("g", unreal.MaterialProperty.MP_ROUGHNESS),
                          ("b", unreal.MaterialProperty.MP_SPECULAR)):
        mask = expr(unreal.MaterialExpressionComponentMask, -100, 400)
        for c in ("r", "g", "b"):
            mask.set_editor_property(c, c == channel)
        connect(custom, "ORM", mask, "")
        connect_property(mask, "", prop)
    if variant == "near":
        # The grid's vertices go where NEAR_GROUND_CODE puts them: on the land under the camera, raised by its relief.
        place_world = expr(unreal.MaterialExpressionTransform, -100, 900)
        place_world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
        place_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
        connect(place, "", place_world, "")
        connect_property(place_world, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        return finish_material(material, stamp, name)
    if variant == "far":
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
        hole = expr(unreal.MaterialExpressionCustom, -300, 1300)
        hole_sources = [("P", local_pos, ""), *[(k, node, "RGBA") for k, node in ground.items()]]
        hole_inputs = []
        for input_name, _, _ in hole_sources:
            entry = unreal.CustomInput()
            entry.set_editor_property("input_name", input_name)
            hole_inputs.append(entry)
        hole.set_editor_property("inputs", hole_inputs)
        hole.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        hole.set_editor_property("code", GROUND_HOLE_CODE)
        for input_name, node, pin in hole_sources:
            connect(node, pin, hole, input_name)
        connect_property(hole, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    # The relief itself: Nanite splits the mesh into pixel-sized triangles and moves each point
    # outward by this height (0..1 of RELIEF_MAX_M).
    by_name = {input_name: (node, pin) for input_name, node, pin in sources}
    shape = expr(unreal.MaterialExpressionCustom, -300, 700)
    shape_sources = [("P", local_pos, ""), *[(k, *by_name[k]) for k in ("N", "UVd", "VC3", "VCA", "Dist", "Tiling", "MidTiling", "RockStart", "SnowLine", "Micro", "Attr", "AttrRes")],
                     *heights]
    shape_inputs = []
    for input_name, _, _ in shape_sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        shape_inputs.append(entry)
    shape.set_editor_property("inputs", shape_inputs)
    shape.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    shape.set_editor_property("code", DISPLACE_CODE)
    for input_name, node, pin in shape_sources:
        connect(node, pin, shape, input_name)
    if not unreal.EphemeraMaterialBuilder.connect_displacement(shape, ""):
        raise RuntimeError(f"{name}: could not connect the displacement")
    material.set_editor_property("enable_tessellation", True)
    scaling = unreal.DisplacementScaling()
    scaling.set_editor_property("magnitude", RELIEF_MAX_M * 100.0)
    scaling.set_editor_property("center", 0.0)
    material.set_editor_property("displacement_scaling", scaling)
    # Relief is split into pixel-sized triangles only where it can be seen: once the largest it can be
    # (RELIEF_MAX_M) is fewer than DISPLACE_FADE_PX[0] pixels on screen it eases off, and below [1] the
    # ground is drawn as its plain mesh (about 70 and 170 m away at 1440p). Splitting the whole land out to
    # the horizon cost this game's graphics card several times the frame.
    fade = unreal.DisplacementFadeRange()
    fade.set_editor_property("start_size_pixels", DISPLACE_FADE_PX[0])
    fade.set_editor_property("end_size_pixels", DISPLACE_FADE_PX[1])
    material.set_editor_property("enable_displacement_fade", True)
    material.set_editor_property("displacement_fade_range", fade)
    EDIT.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE, True)
    return finish_material(material, stamp, name)


# The parts of the ground shader every world shares: world-aligned sampling at three sizes, and the
# layer macro that turns one scan into colour, normal and roughness for the distances in play.
GROUND_SAMPLING = PLANET_CODE[PLANET_CODE.index("float3 pm = P * 0.01;"):PLANET_CODE.index("float alt = UVd.x;")]
GROUND_LAYERS = PLANET_CODE[PLANET_CODE.index("// Which sizes of the scans this pixel is shown"):PLANET_CODE.index("// Moss is not one green")]

MOON_CODE = r"""
// The ground of an airless, cratered world. P, N: planet-local position (cm) and surface normal.
// Vertex colour (VC3): R slope, G mare (0 highland .. 1 mare), B fresh ejecta (0..1).
// Two scans: dust (regolith) everywhere it can lie, bare rock where the ground is too steep for
// it. The colour is the world's own: pale highland, dark mare, brightened by the blankets and
// rays of the youngest craters. Regolith is one of the dullest surfaces there is: almost no
// mirror reflection at any angle, which is why a real moon never looks wet.
""" + GROUND_SAMPLING + r"""
float slope = VC3.r;
float mare = VC3.g;
float fresh = VC3.b;
float driftB, driftC;
VNOISE(driftB, pm / 140.0 + 17.0)
VNOISE(driftC, pm / 900.0 + 41.0)
float wRock = smoothstep(0.20, 0.36, slope + (driftB - 0.5) * 0.08);
float wDust = 1.0 - wRock;
float3 ground = lerp(Highland.rgb, Mare.rgb, mare);
ground *= 1.0 + fresh * FreshGain;
""" + GROUND_LAYERS + r"""LAYER(wDust, DustD, DustN, DustA, DustD, DustN, ground, 0.97, 1.0)
// Steep walls keep shedding their dust: the rock there is newly bared, and paler.
LAYER(wRock, RockD, RockN, RockA, RockD, RockN, ground * 1.15, 0.95, 1.0)
float tone = lerp(0.88, 1.10, driftB) * lerp(0.93, 1.06, driftC);
Nrm = normalize(n + N * 0.0001);
ORM = float3(arm.r, max(arm.g, 0.92), 0.14);
// Seen from another world's night, this one is in full sun, and a camera opened up for the dark
// would draw it as a white-hot disc, a second sun. The eye does not: the moon in a night sky is a
// world you can see the face of. So ground far from the camera is never drawn brighter than
// DiscCap allows at the exposure in use (Unexposed is what the engine multiplies by to undo that
// exposure): by day and in space nothing changes, and it does not touch the ground you stand on.
// (Its colour cannot simply be darkened: the engine keeps a surface's colour in eight bits, and a ten-
// thousandth of a grey is not a number it can hold. The first try came out in dark red blotches. So
// where the limit bites, the ground stops being lit by the engine at all and shines by itself instead,
// with the light the sun would have given it, less whatever the limit takes off.)
float held = min(1.0, DiscCap * Unexposed.r);
float own = smoothstep(250000.0, 700000.0, Dist) * (1.0 - held);
float engineLit = 1.0 - smoothstep(0.0, 0.85, own);
float facing = saturate(dot(Nrm, normalize(SunLocal)));
Glow = (1.0 - engineLit) * col * tone * facing * SunLight.rgb * held;
ORM.b *= engineLit;
return col * tone * engineLit;
"""


def make_moon_material(name, dust, rock, highland, mare, tiling_m=5.0, mid_tiling_m=60.0, far_tiling_m=700.0,
                       far_relief=1.4, fresh_gain=0.9):
    """dust, rock: {'d','n','arm'} scans. highland, mare: the two grounds' colours (linear albedo)."""
    path = f"{ROOT}/Materials/{name}"
    stamp = stamp_of("moon-v5", MOON_CODE, dust, rock, highland, mare, tiling_m, mid_tiling_m, far_tiling_m, far_relief, fresh_gain)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset(name, f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("tangent_space_normal", False)

    def expr(cls, x=-900, y=0):
        return EDIT.create_material_expression(material, cls, x, y)

    def scalar(parameter, value):
        node = expr(unreal.MaterialExpressionScalarParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", value)
        return node

    def colour(parameter, value):
        node = expr(unreal.MaterialExpressionVectorParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
        return node

    world = expr(unreal.MaterialExpressionWorldPosition)
    local_pos = expr(unreal.MaterialExpressionTransformPosition)
    local_pos.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local_pos.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, "", local_pos, "")
    normal_ws = expr(unreal.MaterialExpressionVertexNormalWS)
    local_normal = expr(unreal.MaterialExpressionTransform)
    local_normal.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    local_normal.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    connect(normal_ws, "", local_normal, "")
    camera = expr(unreal.MaterialExpressionCameraPositionWS)
    distance = expr(unreal.MaterialExpressionDistance)
    connect(world, "", distance, "A")
    connect(camera, "", distance, "B")
    # What the engine would multiply a light value by to undo the camera's exposure (see DiscCap in MOON_CODE).
    unexposed = expr(unreal.MaterialExpressionEyeAdaptationInverse)     # of a light value of 1, which is what it takes when given none
    sun_local = expr(unreal.MaterialExpressionTransform)
    sun_local.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    sun_local.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    connect(expr(unreal.MaterialExpressionSkyAtmosphereLightDirection), "", sun_local, "")
    sources = [
        ("P", local_pos, ""), ("N", local_normal, ""), ("VC3", expr(unreal.MaterialExpressionVertexColor), ""), ("Dist", distance, ""),
        ("Tiling", scalar("Tiling", tiling_m), ""), ("MidTiling", scalar("MidTiling", mid_tiling_m), ""),
        ("FarTiling", scalar("FarTiling", far_tiling_m), ""), ("FarRelief", scalar("FarRelief", far_relief), ""),
        ("Highland", colour("Highland", highland), ""), ("Mare", colour("Mare", mare), ""),
        # How much brighter than old ground the freshest ejecta is (0.9 = nearly twice).
        ("FreshGain", scalar("FreshGain", fresh_gain), ""),
        ("DiscCap", scalar("DiscCap", 0.00033), ""), ("Unexposed", unexposed, ""),
        # Toward the star, in the moon's own space; and the star's light on a white surface facing it (lux over pi, tinted).
        ("SunLocal", sun_local, ""), ("SunLight", colour("SunLight", (30240.0, 28120.0, 24800.0)), ""),
    ]
    sampler_types = {"d": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "n": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                     "arm": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}
    suffix = {"d": "D", "n": "N", "arm": "A"}
    for layer, scans in (("Dust", dust), ("Rock", rock)):
        for key in ("d", "n", "arm"):
            node = expr(unreal.MaterialExpressionTextureObjectParameter, -1300, 0)
            node.set_editor_property("parameter_name", f"{layer}{suffix[key]}")
            node.set_editor_property("texture", scans[key])
            node.set_editor_property("sampler_type", sampler_types[key])
            sources.append((f"{layer}{suffix[key]}", node, ""))
    custom = expr(unreal.MaterialExpressionCustom, -300, 0)
    inputs = []
    for input_name, _, _ in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    outputs = []
    for output_name in ("Nrm", "ORM", "Glow"):
        entry = unreal.CustomOutput()
        entry.set_editor_property("output_name", output_name)
        entry.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        outputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", MOON_CODE)
    for input_name, node, pin in sources:
        connect(node, pin, custom, input_name)
    connect_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(custom, "Glow", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    world_normal = expr(unreal.MaterialExpressionTransform, -100, 200)
    world_normal.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world_normal.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(custom, "Nrm", world_normal, "")
    connect_property(world_normal, "", unreal.MaterialProperty.MP_NORMAL)
    for channel, prop in (("r", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION), ("g", unreal.MaterialProperty.MP_ROUGHNESS),
                          ("b", unreal.MaterialProperty.MP_SPECULAR)):
        mask = expr(unreal.MaterialExpressionComponentMask, -100, 400)
        for c in ("r", "g", "b"):
            mask.set_editor_property(c, c == channel)
        connect(custom, "ORM", mask, "")
        connect_property(mask, "", prop)
    EDIT.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE, True)
    return finish_material(material, stamp, name)


OCEAN_CODE = r"""
// A cold northern sea. P: planet-local position (cm). Dist: to the camera (cm).
// DepthMap: the world's baked sea depth (a cube texture read by direction; one byte holding
// sqrt(depth / 64 m)). Casc0Slope..Casc3Slope: the simulated sea's cascades (UEphemeraOceanSim). Chop,
// Swell: still wave maps (Tools/terrain/water.py), used only for where gusts and foam lace fall.
// Returns the normal; also gives Base, Rough and Cover.
//
// Near the camera this is real water: the engine's single-layer water, clear in the shallows,
// darkening with depth (Absorb and Scatter, set on the material). Cover is how much of an opaque
// painted surface (Base) lies over that: the surf, and from far away all of it, where the sea is
// drawn from the baked depth map as it always was.
float3 pm = P * 0.01;
// The sea is a sphere about the planet's centre, so its normal is exact from the position alone.
float3 up = normalize(P);
struct FSea
{
    float3 pm, dpx, dpy, w, up, chart;
    float time;
    // One cascade of the simulated sea (UEphemeraOceanSim, Shaders/EphemeraOcean.usf): its slopes, laid on
    // the planet along the three charts (SEA_CHARTS, as the near sea's grid is raised and as the game sums
    // the water), blended with weights whose squares add to one (unrelated seas mixed so are as rough as
    // either). Read with the texture's own mips and anisotropic filtering: a texel's mip holds the mean slope
    // and the mean squared slope beneath it, and their difference is the waves this pixel cannot show,
    // returned as variance for the roughness (LEAN mapping; Bruneton et al. 2010). Foam is averaged.
    // blur: how many pixels a wave must span to be drawn as a wave (Bruneton et al. 2010: two); finer ones
    // become roughness. Without it the ripples a pixel half-resolves glitter pixel by pixel, a grain no sea has.
    void Cascade(Texture2D T, SamplerState S, float patch, float gain, float blur, inout float4 sum, inout float foam)
    {
        float3 c = pow(abs(up), 8.0);
        c /= max(sqrt(dot(c, c)), 1e-12);
        float k = 1.0 / patch;
        float kg = k * blur;
        [branch] if (c.x > 0.02)
        {
            float4 t = T.SampleGrad(S, float2(dot(chart, $XU), dot(chart, $XV)) * k + $XS, float2(dot(dpx, $XU), dot(dpx, $XV)) * kg, float2(dot(dpy, $XU), dot(dpy, $XV)) * kg);
            sum += float4(($XU * t.x + $XV * t.y) * (gain * c.x), max(t.z - dot(t.xy, t.xy), 0.0) * gain * gain * c.x * c.x);
            foam += t.w * c.x * c.x;
        }
        [branch] if (c.y > 0.02)
        {
            float4 t = T.SampleGrad(S, float2(dot(chart, $YU), dot(chart, $YV)) * k + $YS, float2(dot(dpx, $YU), dot(dpx, $YV)) * kg, float2(dot(dpy, $YU), dot(dpy, $YV)) * kg);
            sum += float4(($YU * t.x + $YV * t.y) * (gain * c.y), max(t.z - dot(t.xy, t.xy), 0.0) * gain * gain * c.y * c.y);
            foam += t.w * c.y * c.y;
        }
        [branch] if (c.z > 0.02)
        {
            float4 t = T.SampleGrad(S, float2(dot(chart, $ZU), dot(chart, $ZV)) * k + $ZS, float2(dot(dpx, $ZU), dot(dpx, $ZV)) * kg, float2(dot(dpy, $ZU), dot(dpy, $ZV)) * kg);
            sum += float4(($ZU * t.x + $ZV * t.y) * (gain * c.z), max(t.z - dot(t.xy, t.xy), 0.0) * gain * gain * c.z * c.z);
            foam += t.w * c.z * c.z;
        }
    }
    // The map's height channel alone, very large: used for where the wind is ruffling the water.
    float Patches(Texture2D T, SamplerState S, float metres, float speed)
    {
        float k = 1.0 / metres;
        float d = frac(time * speed * k);
        return Texture2DSampleLevel(T, S, pm.zy * k - d, 2.0).a * w.x + Texture2DSampleLevel(T, S, pm.xz * k - d, 2.0).a * w.y
             + Texture2DSampleLevel(T, S, pm.xy * k - d, 2.0).a * w.z;
    }
};
FSea Sea;
// The charts are read where the water rests at the datum, as the near sea's grid is raised and the game sums it.
Sea.pm = pm; Sea.dpx = ddx(pm); Sea.dpy = ddy(pm); Sea.time = Time; Sea.up = up; Sea.chart = up * SeaRadiusM;
Sea.w = pow(abs(up), 4.0);
Sea.w /= (Sea.w.x + Sea.w.y + Sea.w.z);

// The tide: the sea stands TideAmplitude metres higher under the moon and on the far side from it,
// half as much lower in the belt between. Depth is the baked depth below the datum (from 8 m above
// it, so the ground the tide covers is known) plus the tide here and now.
float toMoon = dot(up, normalize(MoonDir.xyz));
float tide = TideAmplitude * (1.5 * toMoon * toMoon - 0.5);
float enc = TextureCubeSample(DepthMap, DepthMapSampler, up).r;
float depth = max(enc * enc * 72.0 - 8.0 + tide, 0.0);
float dM = Dist * 0.01;

// The waves (UEphemeraOceanSim): four cascades of one simulated sea, from a long swell to ripples a hand
// long. Gusts ruffle the short ones in drifting patches, so no stretch of sea looks like the next; sheltered
// shallows are calmer.
float gust = lerp(0.62, 1.28, smoothstep(0.30, 0.70, Sea.Patches(Swell, SwellSampler, 380.0, 0.5)));
float calm = lerp(0.45, 1.0, smoothstep(0.3, 9.0, depth));
float4 wv = 0.0;
float seaFoam = 0.0;
Sea.Cascade(Casc0Slope, Casc0SlopeSampler, $P0, Band0Gain * calm, 1.0, wv, seaFoam);
Sea.Cascade(Casc1Slope, Casc1SlopeSampler, $P1, Band1Gain * calm, 1.5, wv, seaFoam);
Sea.Cascade(Casc2Slope, Casc2SlopeSampler, $P2, Band2Gain * calm * gust, 2.0, wv, seaFoam);
Sea.Cascade(Casc3Slope, Casc3SlopeSampler, $P3, Band3Gain * calm * gust, 2.5, wv, seaFoam);
float3 slope3 = wv.xyz - up * dot(wv.xyz, up);
// What the eye cannot resolve of the waves still spreads the light they reflect: the slope lost to
// distance becomes roughness (GGX alpha squared = mean square slope), so the glitter under the sun
// is as wide from orbit as it is at the shore. (0.002: ripples shorter than the finest cascade.)
Rough = pow(0.0004 + wv.w + 0.002 * calm * calm, 0.25);
// Whitecaps where the waves fold over (none in this sea's light breeze, WhitecapGain 0; a storm raises it).
float whitecap = saturate((seaFoam - 0.45) * 1.8) * 0.7 * WhitecapGain;

// The painted sea, for far away. Over the shelf the water is paler and greener (light comes back
// from sediment in it); it darkens to the open sea's near-black as the bottom falls away.
float3 water = lerp(Shelf.rgb, Deep.rgb, 1.0 - exp(-depth / ShelfDepth));
// In the last metre or two the wet bed shows through.
water = lerp(water, Bed.rgb, exp(-depth / 1.2) * 0.8);
// Surf: broken white water over the last half metre or so. On a shelving shore that is a band tens
// of metres wide, which is what marks a coast from the air; it is laced, never a solid white.
// It comes and goes: every seven seconds or so the water runs up the shore and draws back, a little
// out of step from one stretch of beach to the next.
float swash = 0.5 + 0.5 * sin(Time * 0.9 + Sea.Patches(Swell, SwellSampler, 140.0, 0.0) * 9.0);
float foam = saturate(1.0 - depth / lerp(0.22, 0.62, swash));
// (Kept narrow: on a flat strand a deep band of surf is a white sheet a hundred metres wide. A second line
// further out read as painted stripes and is gone.)
// The baked depth comes in steps of a hand's length, so on a flat strand whole fields of the bed sit at one
// depth and are all surf or none: a white sheet. Surf belongs where the bed shelves; elsewhere a trace is left.
float shelving = length(float2(ddx(depth), ddy(depth))) / max(length(Sea.dpx) + length(Sea.dpy), 0.0002);
foam *= lerp(0.2, 1.0, smoothstep(0.012, 0.05, shelving));
// Foam is whole only at the water's very edge. Outward it is eaten into lace and then into flecks: the further
// from the edge, the more of it has gone (cells of two sizes decide where). Laid on evenly, holes and all, it
// was a spotted white sheet.
float lace = Sea.Patches(Chop, ChopSampler, 9.0, 0.7) * 0.62 + Sea.Patches(Chop, ChopSampler, 2.3, 0.3) * 0.38;
// And it is a froth of bubbles, not a sheet: it thins to nothing at its edges (a hard threshold cut it out
// like paper) and is full of small holes, a hand across.
float fizz = Sea.Patches(Chop, ChopSampler, 0.55, 0.9);
float eaten = smoothstep(0.0, 0.55, foam - (1.0 - lace) * 0.85) * lerp(0.5, 1.0, fizz);
foam = lerp(foam * 0.45, eaten, 1.0 - smoothstep(60.0, 600.0, dM)) * 0.75;
// From far away the surf is a faint pale edge, not a white line: at full strength it is brighter
// than anything else on the planet and shows straight through thin cloud.
foam *= lerp(1.0, 0.45, smoothstep(2000.0, 9000.0, dM));
foam = max(foam, whitecap * lerp(0.55, 1.0, lace));
float painted = smoothstep(FarStart, FarEnd, dM);
// The sea does not end in a line on the sand: its last hand's depth is so thin that it is the wet sand under
// it with a shine on top, and it becomes the beach by degrees.
float thin = (1.0 - smoothstep(0.02, 0.35, depth)) * (1.0 - painted) * 0.85;
Cover = 1.0 - (1.0 - painted) * (1.0 - foam) * (1.0 - thin);
Base = (water * painted * (1.0 - foam) + float3(0.62, 0.66, 0.66) * foam + Bed.rgb * thin * (1.0 - foam) * (1.0 - painted)) / max(Cover, 0.001);
// By night the breaking water glows: cold coasts carry plankton that flash blue when the surf stirs
// them (it is seen on Norway's and Scotland's shores). Faint, as in life: about a tenth of the night's
// white, a pale blue line along a dark coast. (SkyState.x: how far into night, 0..1.)
float plankton = smoothstep(0.6, 1.0, SkyState.x) * (1.0 - painted);
float stirred = foam * (0.55 + 0.45 * sin(Time * 2.3 + lace * 25.0));
Glow = float3(0.010, 0.10, 0.32) * stirred * plankton * BioGlow;
Rough = lerp(lerp(Rough, 0.6, foam), 0.07, thin * (1.0 - foam));
return normalize(up - slope3);
"""


def make_ocean_material(depth_map, variant="sea"):
    """depth_map: the world's baked sea-depth cube texture (EphemeraPlanetBuilder.build_depth_map).

    variant: "sea", the whole sphere (M_Ocean: the sea as it was before the near sea, and as it still is with
    -EphemeraNoSeaGrid); "far", the same sphere with a hole cut where the near sea's grid lies (M_OceanFar);
    "near", that grid, raised into the baked waves (M_OceanNear, see near_ocean_code). All three shade the
    water with OCEAN_CODE, so where the near sea gives way to the far one they are the same water.
    """
    name = {"sea": "M_Ocean", "far": "M_OceanFar", "near": "M_OceanNear"}[variant]
    path = f"{ROOT}/Materials/{name}"
    waves = water_maps()
    sea = sea_bands()
    code = near_ocean_code(sea) if variant == "near" else ocean_code()
    if variant == "sea":
        stamp = stamp_of("ocean-v23", code, depth_map.get_path_name(), waves, SEA_CASCADES, SEA_GAINS)
    else:
        stamp = stamp_of(f"ocean-{variant}-v6", code, sea_grid_code(sea) if variant == "near" else SEA_HOLE_CODE,
                         depth_map.get_path_name(), waves, SEA_CASCADES, SEA_GAINS)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    # Made afresh each time, like the clouds: clearing a material's nodes leaves its Single Layer
    # Water output node behind, and a second one stops the material compiling.
    if ASSETS.does_asset_exist(path) and not ASSETS.delete_asset(path):
        raise RuntimeError(f"{name}: the old material could not be removed")
    material = TOOLS.create_asset(name, f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError(f"{name}: the material could not be created")
    material.set_editor_property("tangent_space_normal", False)
    # Its waves move within the texture, not with the surface: told so, the temporal upscaler does not smear them.
    try:
        material.set_editor_property("has_pixel_animation", True)
    except Exception as error:
        log(f"{name}: has_pixel_animation not set ({error})")
    if variant == "far":
        # Single layer water may be masked (it may not be translucent): the hole is cut, not blended.
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)

    def expr(cls, x=-900, y=0):
        return EDIT.create_material_expression(material, cls, x, y)

    def scalar(parameter, value):
        node = expr(unreal.MaterialExpressionScalarParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", value)
        return node

    def colour(parameter, value):
        node = expr(unreal.MaterialExpressionVectorParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
        return node

    world = expr(unreal.MaterialExpressionWorldPosition)
    local_pos = expr(unreal.MaterialExpressionTransformPosition)
    local_pos.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local_pos.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, "", local_pos, "")
    camera = expr(unreal.MaterialExpressionCameraPositionWS)
    distance = expr(unreal.MaterialExpressionDistance)
    connect(world, "", distance, "A")
    connect(camera, "", distance, "B")
    depth_node = expr(unreal.MaterialExpressionTextureObject)
    depth_node.set_editor_property("texture", depth_map)
    depth_node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    def cascade_texture(parameter):
        # Made by the game (UEphemeraOceanSim) and handed over at run time; a still wave map until then.
        node = expr(unreal.MaterialExpressionTextureObjectParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("texture", waves["swell"])
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        # The shared sampler with the world's filtering (trilinear, anisotropic): takes none of the material's own.
        node.set_editor_property("sampler_source", unreal.SamplerSourceMode.SSM_WRAP_WORLD_GROUP_SETTINGS)
        return node

    wave_nodes = {}
    for key, texture in waves.items():
        node = expr(unreal.MaterialExpressionTextureObject)
        node.set_editor_property("texture", texture)
        node.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
        wave_nodes[key] = node
    sky_state = expr(unreal.MaterialExpressionCollectionParameter)
    sky_state.set_editor_property("collection", world_parameters())
    sky_state.set_editor_property("parameter_name", "SkyState")
    sources = [
        ("P", local_pos), ("Dist", distance), ("Time", expr(unreal.MaterialExpressionTime)),
        ("DepthMap", depth_node), ("Chop", wave_nodes["chop"]), ("Swell", wave_nodes["swell"]),
        # The sea's datum radius (m): the charts are read on it, as the grid is raised and the game sums the water.
        ("SeaRadiusM", scalar("SeaRadiusM", 5000.0)),
        # Beyond FarEnd metres the sea is the painted one; nearer than FarStart it is all real water.
        ("FarStart", scalar("FarStart", 1500.0)), ("FarEnd", scalar("FarEnd", 7000.0)),
        ("MoonDir", colour("MoonDir", (1.0, 0.0, 0.0))), ("TideAmplitude", scalar("TideAmplitude", 0.0)),
        # Wind over the sea in metres per second: sets how wide the sun's glint spreads.
        ("Wind", scalar("Wind", 3.5)),
        ("SkyState", sky_state), ("BioGlow", scalar("BioGlow", 1.0)), ("WhitecapGain", scalar("WhitecapGain", 0.0)),
        # Open water: cold, clear and deep, nearly black with a trace of blue-green.
        ("Deep", colour("Deep", (0.003, 0.012, 0.024))),
        # Over the shelf: paler and greener. ShelfDepth is the depth (m) by which most of that is gone.
        ("Shelf", colour("Shelf", (0.012, 0.042, 0.048))), ("ShelfDepth", scalar("ShelfDepth", 22.0)),
        # The bed at the water's edge: wet black sand.
        ("Bed", colour("Bed", (0.012, 0.012, 0.013))),
    ]
    for index, gain in enumerate(SEA_GAINS):
        sources.append((f"Band{index}Gain", scalar(f"Band{index}Gain", gain)))
        sources.append((f"Casc{index}Slope", cascade_texture(f"Casc{index}Slope")))
    pins = {}
    if variant == "near":
        # The near sea's own inputs. Its vertices are placed by a second Custom node in the vertex shader
        # (SEA_GRID_CODE), which also says where each one's water rests; that reaches the pixels through a
        # vertex interpolator and stands in for P, so the waves are read where the water rests, as the
        # mesh was raised by them, and the far sea's look is read at the same points.
        sea = sea_bands()

        def vector(parameter):
            node = colour(parameter, (0.0, 0.0, 0.0))
            node.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 1.0, 0.0))
            pins[parameter] = "RGBA"
            return node

        by_name = {input_name: node for input_name, node in sources}
        grid = {"SeaFace": vector("SeaFace"), "SeaU": vector("SeaU"), "SeaV": vector("SeaV"), "SeaHole": vector("SeaHole")}
        grid["SeaFace"].set_editor_property("default_value", unreal.LinearColor(1.0, 0.0, 0.0, 5000.0))
        grid["SeaU"].set_editor_property("default_value", unreal.LinearColor(0.0, 1.0, 0.0, 0.0))
        grid["SeaHole"].set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
        for index, cascade in enumerate(sea["bands"]):
            if cascade["max_spacing_m"] > 0.0:
                grid[f"Casc{index}Move"] = cascade_texture(f"Casc{index}Move")
        flat = expr(unreal.MaterialExpressionPreSkinnedPosition, -900, 900)
        ring = expr(unreal.MaterialExpressionTextureCoordinate, -900, 1000)
        ring.set_editor_property("coordinate_index", 1)
        vertex_sources = [("Flat", flat), ("Ring", ring), ("Time", by_name["Time"]), ("DepthMap", by_name["DepthMap"]),
                          ("MoonDir", by_name["MoonDir"]), ("TideAmplitude", by_name["TideAmplitude"])]
        vertex_sources += [(f"Band{index}Gain", by_name[f"Band{index}Gain"]) for index, cascade in enumerate(sea["bands"])
                           if cascade["max_spacing_m"] > 0.0]
        vertex_sources += list(grid.items())
        place = expr(unreal.MaterialExpressionCustom, -300, 900)
        place_inputs = []
        for input_name, _ in vertex_sources:
            entry = unreal.CustomInput()
            entry.set_editor_property("input_name", input_name)
            place_inputs.append(entry)
        rest = unreal.CustomOutput()
        rest.set_editor_property("output_name", "Rest")
        rest.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        place.set_editor_property("inputs", place_inputs)
        place.set_editor_property("additional_outputs", [rest])
        place.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        place.set_editor_property("code", sea_grid_code(sea))
        for input_name, node in vertex_sources:
            connect(node, pins.get(input_name, ""), place, input_name)
        resting = expr(unreal.MaterialExpressionVertexInterpolator, -100, 900)
        connect(place, "Rest", resting, "VS")
        sources = [(input_name, resting if input_name == "P" else node) for input_name, node in sources]
        sources += [(key, node) for key, node in grid.items() if not key.endswith("Move")]
    custom = expr(unreal.MaterialExpressionCustom, -300, 0)
    inputs = []
    for input_name, _ in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    outputs = []
    for output_name, output_type in (("Base", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("Rough", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
                                     ("Cover", unreal.CustomMaterialOutputType.CMOT_FLOAT1), ("Glow", unreal.CustomMaterialOutputType.CMOT_FLOAT3)):
        entry = unreal.CustomOutput()
        entry.set_editor_property("output_name", output_name)
        entry.set_editor_property("output_type", output_type)
        outputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", code)
    for input_name, node in sources:
        connect(node, pins.get(input_name, ""), custom, input_name)
    world_normal = expr(unreal.MaterialExpressionTransform, -100, 0)
    world_normal.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world_normal.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(custom, "", world_normal, "")
    connect_property(world_normal, "", unreal.MaterialProperty.MP_NORMAL)
    connect_property(custom, "Base", unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(custom, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    connect_property(custom, "Cover", unreal.MaterialProperty.MP_OPACITY)
    connect_property(custom, "Glow", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    # The water itself. How far light gets through it, per centimetre: red is gone within a couple
    # of metres, green and blue carry on; a little is scattered back, more of the blue-green. That
    # gives clear shallows over the black sand and a dark, cold body of water further out.
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    body = expr(unreal.MaterialExpressionSingleLayerWaterMaterialOutput, 200, 300)
    # Coastal shelf water (docs/research/ocean_and_underwater.md 3.6), per centimetre: pure water's absorption
    # plus what a cold coast carries, and enough scattering to give the water a body colour. The first numbers
    # scattered almost nothing, so the deep sea gave back under one percent of the light: black.
    # (The research's coastal-shelf numbers, (0.0005, 0.0007, 0.0006) of scattering, made a bright turquoise
    # lagoon: a cold northern sea is a dark teal-blue that gives back a few percent of the light.)
    connect(colour("Absorb", (0.0045, 0.0020, 0.0014)), "", body, "AbsorptionCoefficients")
    connect(colour("Scatter", (0.00012, 0.00022, 0.00024)), "", body, "ScatteringCoefficients")
    # Light is scattered mostly onward: a low sun behind a wave shows green through its crest.
    connect(scalar("PhaseG", 0.3), "", body, "PhaseG")
    if variant == "far":
        hole = expr(unreal.MaterialExpressionCustom, -300, 1300)
        hole_sources = [("P", local_pos)]
        # The same parameters as the near sea's (UEphemeraSeaSurfaceComponent sets both alike).
        for parameter, default in (("SeaFace", (1.0, 0.0, 0.0, 5000.0)), ("SeaU", (0.0, 1.0, 0.0, 0.0)),
                                   ("SeaV", (0.0, 0.0, 1.0, 0.0)), ("SeaHole", (0.0, 0.0, 0.0, 0.0))):
            node = colour(parameter, (0.0, 0.0, 0.0))
            node.set_editor_property("default_value", unreal.LinearColor(*default))
            hole_sources.append((parameter, node))
        hole_inputs = []
        for input_name, _ in hole_sources:
            entry = unreal.CustomInput()
            entry.set_editor_property("input_name", input_name)
            hole_inputs.append(entry)
        hole.set_editor_property("inputs", hole_inputs)
        hole.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        hole.set_editor_property("code", SEA_HOLE_CODE)
        for input_name, node in hole_sources:
            connect(node, "" if input_name == "P" else "RGBA", hole, input_name)
        connect_property(hole, "", unreal.MaterialProperty.MP_OPACITY_MASK)
    if variant == "near":
        # The grid's vertices go where SEA_GRID_CODE puts them: on the sphere under the camera, raised by
        # the waves and the tide. Its output is in the planet's own space, as everything here is.
        place_world = expr(unreal.MaterialExpressionTransform, -100, 900)
        place_world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
        place_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
        connect(place, "", place_world, "")
        connect_property(place_world, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
        specular = expr(unreal.MaterialExpressionConstant, -100, 480)
        specular.set_editor_property("r", 0.255)        # water: 2% reflectance head-on
        connect_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
        return finish_material(material, stamp, name)
    # The surface itself rises and falls with the tide.
    lift = expr(unreal.MaterialExpressionCustom, -300, 700)
    lift_inputs = []
    for input_name in ("P", "MoonDir", "TideAmplitude"):
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        lift_inputs.append(entry)
    lift.set_editor_property("inputs", lift_inputs)
    lift.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    lift.set_editor_property("code", "float3 up = normalize(P);\nfloat toMoon = dot(up, normalize(MoonDir.xyz));\n"
                             "return up * (TideAmplitude * (1.5 * toMoon * toMoon - 0.5) * 100.0);")
    by_name = {name: node for name, node in sources}
    for input_name in ("P", "MoonDir", "TideAmplitude"):
        connect(by_name[input_name], "", lift, input_name)
    lift_world = expr(unreal.MaterialExpressionTransform, -100, 700)
    lift_world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    lift_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(lift, "", lift_world, "")
    connect_property(lift_world, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    specular = expr(unreal.MaterialExpressionConstant, -100, 480)
    specular.set_editor_property("r", 0.255)        # water: 2% reflectance head-on
    connect_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    if variant == "sea":
        EDIT.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE, True)
    return finish_material(material, stamp, name)


# The moving wave maps' loop and the size their waves are timed for (Tools/terrain/water.py: LOOP, ANIM_METRES).
WAVE_LOOP_S = 24.0
WAVE_LOOP_METRES = 100.0
WAVE_ANIM_SIZE, WAVE_ANIM_FRAMES = 128, 128


def water_maps():
    """The repeating wave maps (Tools/terrain/water.py), as {'chop', 'swell'} textures, and the moving
    ones as {'chop_anim', 'swell_anim'} volume textures (third coordinate: time)."""
    maps = {key: import_map(BAKES.parent / "water" / f"water_{key}.png", f"{ROOT}/Textures/Water", f"T_Water_{key.title()}")
            for key in ("chop", "swell")}
    for key in ("chop", "swell"):
        raw = BAKES.parent / "water" / f"water_{key}_anim.raw"
        name = f"T_Water_{key.title()}Anim"
        path = f"{ROOT}/Textures/Water/{name}"
        stamp = stamp_of("wave-volume-v1", hashlib.sha1(raw.read_bytes()).hexdigest(), WAVE_ANIM_SIZE, WAVE_ANIM_FRAMES)
        texture = unchanged(path, stamp)
        if not texture:
            texture = unreal.EphemeraCloudBuilder.import_wave_volume(f"{ROOT}/Textures/Water", name, str(raw), WAVE_ANIM_SIZE, WAVE_ANIM_FRAMES)
            if not texture:
                raise RuntimeError(f"{name} could not be made from {raw}")
            ASSETS.set_metadata_tag(texture, "EphemeraStamp", stamp)
            ASSETS.save_loaded_asset(texture)
            log(f"wave volume {name}: rebuilt")
        maps[f"{key}_anim"] = texture
    return maps


# ------------------------------------------------------------------------------------ near sea ---
# The sea near the camera as geometry (docs/research/ocean_and_underwater.md 3.3-3.5): a grid of rings kept
# under the camera (UEphemeraSeaSurfaceComponent), raised by its material into waves baked by
# Tools/terrain/ocean_fft.py. The far sea is the old sphere with a hole cut where the grid lies. The game
# sums the same waves on the CPU (UEphemeraSeaSurfaceComponent::MoveM): what SEA_BANDS_CODE and
# SEA_GRID_CODE do to the water, it does too. Change one and change the other.

# The grid (UEphemeraPlanetBuilder::BuildSeaGrid): six rings of 128 x 128 cells from 0.5 m to 16 m, out to
# a kilometre each way. A standing player's horizon is 110 m off, inside the third ring.
SEA_GRID = {"rings": 6, "half_cells": 64, "spacing_m": 0.5}
# The sea's waves: four cascades of one simulated sea (UEphemeraOceanSim, which draws their amplitudes from
# the spectrum each time the game starts; Shaders/EphemeraOcean.usf moves them on every frame). Patch sizes
# share no factor (Dupuy and Bruneton 2012), and each cascade holds no wave longer than a sixth of the next
# smaller patch, so no repeat shows. peak_m: the length of its main waves (shallow water steepens waves
# shorter than twice the depth); break_depth_m: where they have broken (height / 0.78); max_spacing_m: the
# coarsest grid spacing that still raises the mesh by it (about six vertices to its shortest waves; 0: it
# only shades). The game's own sums use the same numbers through the body (dress_sea).
SEA_CASCADES = (
    {"name": "swell", "patch_m": 613.0, "chop": 0.35, "peak_m": 99.0, "break_depth_m": 1.5, "max_spacing_m": 16.0},
    {"name": "wind", "patch_m": 127.0, "chop": 0.9, "peak_m": 11.0, "break_depth_m": 0.58, "max_spacing_m": 2.0},
    {"name": "chop", "patch_m": 29.3, "chop": 0.8, "peak_m": 3.0, "break_depth_m": 0.22, "max_spacing_m": 0.0},
    {"name": "ripple", "patch_m": 6.9, "chop": 0.5, "peak_m": 0.6, "break_depth_m": 0.06, "max_spacing_m": 0.0},
)
# Each cascade's strength as drawn (1 = as simulated); the game is given the same numbers through the body.
SEA_GAINS = (1.0, 1.0, 1.0, 1.0)


def ocean_code():
    """OCEAN_CODE with the charts and the cascades' sizes filled in."""
    import string
    values = _chart_values()
    values.update({f"P{index}": _hlsl(cascade["patch_m"]) for index, cascade in enumerate(SEA_CASCADES)})
    return string.Template(OCEAN_CODE).substitute(values)


def _sea_chart(u, v, turn, shift):
    """A plane the waves are laid on: two axes through the planet's centre turned by `turn` radians (so
    the swell runs a different way on each), and a shift of the patch (fractions of it)."""
    c, s = math.cos(turn), math.sin(turn)
    return (tuple(c * a + s * b for a, b in zip(u, v)), tuple(-s * a + c * b for a, b in zip(u, v)), shift)


# The three charts, one per axis of the planet: the same planes the sea's moving normals are laid on
# (OCEAN_CODE: zy, xz, xy).
SEA_CHARTS = (
    _sea_chart((0.0, 0.0, 1.0), (0.0, 1.0, 0.0), 0.35, (0.0, 0.0)),
    _sea_chart((1.0, 0.0, 0.0), (0.0, 0.0, 1.0), 2.2, (0.37, 0.61)),
    _sea_chart((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), 4.1, (0.73, 0.19)),
)

SEA_BANDS_CODE = r"""
// The near sea's waves (UEphemeraOceanSim): cascades of one sea, each a patch of water that repeats in space,
// simulated for this moment, read where a point of the sea rests before the waves move it. Its texels hold
// sideways move and height (metres, half floats). Laid on the planet along three charts (planes through its
// centre, each turned its own way, so the swell does not run one way all round the world), blended where
// they meet with weights whose squares add to one: two unrelated seas mixed so are as high as either, where
// plain weights would halve the waves along every seam.
struct FSeaBands
{
    float3 pm, up, w;
    float4 Read(Texture2D T, float3 eu, float3 ev, float2 shift, float patch)
    {
        float2 uv = float2(dot(pm, eu), dot(pm, ev)) / patch + shift;
        return T.SampleLevel(View.MaterialTextureBilinearWrapedSampler, uv, 0.0);
    }
    float3 Along(float3 e)
    {
        return e - up * dot(e, up);
    }
    // How far one cascade moves the water resting here: metres, planet space.
    float3 Move(Texture2D T, float patch)
    {
        float3 sum = 0.0;
        [branch] if (w.x > 0.02)
        {
            float4 d = Read(T, $XU, $XV, $XS, patch);
            sum += (up * d.z + Along($XU) * d.x + Along($XV) * d.y) * w.x;
        }
        [branch] if (w.y > 0.02)
        {
            float4 d = Read(T, $YU, $YV, $YS, patch);
            sum += (up * d.z + Along($YU) * d.x + Along($YV) * d.y) * w.y;
        }
        [branch] if (w.z > 0.02)
        {
            float4 d = Read(T, $ZU, $ZV, $ZS, patch);
            sum += (up * d.z + Along($ZU) * d.x + Along($ZV) * d.y) * w.z;
        }
        return sum;
    }
    // Over shallow water a wave slows and steepens (Green's law: height ~ depth^-1/4, felt from half a
    // wavelength down) until it breaks, by a depth of about its own height, and is gone.
    float Shore(float depth, float peak, float breaks)
    {
        return clamp(pow(peak / (2.0 * max(depth, 0.01)), 0.25), 1.0, 1.25) * smoothstep(0.05, breaks, depth);
    }
};
FSeaBands Bands;
Bands.pm = up * SeaFace.w;
Bands.up = up;
float3 seaChart = pow(abs(up), 8.0);
Bands.w = seaChart / max(sqrt(dot(seaChart, seaChart)), 1e-12);
"""

SEA_GRID_CODE = r"""
// One vertex of the near sea's grid (UEphemeraSeaSurfaceComponent; the mesh: UEphemeraPlanetBuilder::
// BuildSeaGrid). Flat: where it was built (cm, flat, about the grid's centre). Ring: which ring, and that
// ring's spacing (m). SeaFace: xyz the axis of the cube face the grid's lattice lies on, w the planet's
// radius (m). SeaU, SeaV: that face's two axes, and in w the camera's place along them (metres of arc).
// SeaHole.w: how far the hole cut in the far sea reaches from the camera along those axes (metres of arc).
// Returns how far to move the vertex (cm, planet space); Rest: where its water rests (cm).
//
// Each ring is snapped to its own lattice on the face (twice its spacing): as the camera moves, vertices
// jump from lattice point to lattice point and never slide over the waves (under a sliding grid the waves
// crawl). Over the outer part of each ring its odd vertices fold onto the even ones, so at its edge it is
// the next ring's lattice, triangle for triangle, and the two meet with no step.
float spacing = Ring.y;
float2 cell = round(Flat.xy * 0.01 / spacing);
float reach = max(abs(cell.x), abs(cell.y));
float morph = saturate((reach - $MORPH_FROM) / ($MORPH_TO - $MORPH_FROM));
cell -= (cell - 2.0 * floor(cell * 0.5)) * morph;
float2 centre = floor(float2(SeaU.w, SeaV.w) / (2.0 * spacing)) * (2.0 * spacing);
float2 arc = centre + cell * spacing;
float R = SeaFace.w;
float3 up = normalize(SeaFace.xyz + tan(arc.x / R) * SeaU.xyz + tan(arc.y / R) * SeaV.xyz);
// The tide, and the depth of the water here, as the sea's pixels read them (OCEAN_CODE).
float toMoon = dot(up, normalize(MoonDir.xyz));
float tide = TideAmplitude * (1.5 * toMoon * toMoon - 0.5);
float enc = TextureCubeSampleLevel(DepthMap, DepthMapSampler, up, 0.0).r;
float depth = max(enc * enc * 72.0 - 8.0 + tide, 0.0);
// The waves die away toward the grid's rim: by the edge of the far sea's hole the grid is flat water,
// just where the far sea is. Both are measured as the grid is laid out, along the face's axes (a square on
// the lattice), not in metres over the sphere: arc along those axes overstates true distance by up to a
// half near a cube edge, and a round hole would there reach past the grid.
float hole = max(SeaHole.w, 1.0);
float nearness = 1.0 - smoothstep(0.62 * hole, 0.96 * hole, max(abs(arc.x - SeaU.w), abs(arc.y - SeaV.w)));
// The spacing this vertex stands for (its ring's, growing to the next ring's as it folds onto it). A band
// is drawn only where that is fine enough for it, about six vertices to its main waves: coarser, its
// waves would be sampled into others that crawl.
float spacingNow = spacing * (1.0 + morph);
$BANDS
float3 move = 0.0;
[branch] if (nearness > 0.0)
{
$MOVES
}
// A trough never digs into the bed.
move += up * max(-0.9 * depth - dot(move, up), 0.0);
// And where there is next to no water the waves die away altogether: kept to troughs alone, their crests
// still rose out of the beach, a lattice of small dark diamonds along every waterline.
move *= smoothstep(0.0, 0.8, depth);
Rest = up * (R + tide) * 100.0;
return Rest + move * 100.0 - Flat;
"""

SEA_HOLE_CODE = r"""
// The far sea's hole (UEphemeraSeaSurfaceComponent): 0 where the near sea's grid is the water, within
// SeaHole.w metres of arc of the camera along both axes of the grid's lattice face (SeaFace, SeaU, SeaV, as
// SEA_GRID_CODE lays the grid out, so the hole is always inside it); 1 elsewhere, and everywhere while
// there is no grid (SeaHole.w = 0).
float3 up = normalize(P);
float along = dot(up, SeaFace.xyz);
float2 arc = SeaFace.w * float2(atan2(dot(up, SeaU.xyz), max(along, 0.0001)), atan2(dot(up, SeaV.xyz), max(along, 0.0001)));
float reach = max(abs(arc.x - SeaU.w), abs(arc.y - SeaV.w));
return (SeaHole.w > 0.0 && along > 0.0 && reach < SeaHole.w) ? 0.0 : 1.0;
"""

_SEA = {}


def sea_bands():
    """The near sea's waves: {"bands": SEA_CASCADES}. Their textures are made by the game (UEphemeraOceanSim)
    and handed to the materials at run time (CascNMove, CascNSlope parameters)."""
    return {"bands": SEA_CASCADES}


def _hlsl(number):
    """A number as an HLSL float literal."""
    text = f"{float(number):.7g}"
    return text if "." in text or "e" in text else text + ".0"


def _chart_values():
    values = {}
    for label, (u, v, shift) in zip("XYZ", SEA_CHARTS):
        values[f"{label}U"] = "float3(%s)" % ", ".join(_hlsl(c) for c in u)
        values[f"{label}V"] = "float3(%s)" % ", ".join(_hlsl(c) for c in v)
        values[f"{label}S"] = "float2(%s)" % ", ".join(_hlsl(c) for c in shift)
    return values


def _sea_bands_code():
    import string
    return string.Template(SEA_BANDS_CODE).substitute(_chart_values())


def sea_grid_code(sea):
    """SEA_GRID_CODE (the near sea's vertices) with the bake's bands and the grid's numbers filled in."""
    import string
    moves = []
    for index, band in enumerate(sea["bands"]):
        limit = band["max_spacing_m"]
        if limit <= 0.0:
            continue        # this cascade only shades
        moves.append(f"    float gain{index} = Band{index}Gain * Bands.Shore(depth, {_hlsl(band['peak_m'])}, {_hlsl(band['break_depth_m'])})"
                     f" * (1.0 - smoothstep({_hlsl(limit)}, {_hlsl(2.0 * limit)}, spacingNow)) * nearness;\n"
                     f"    [branch] if (gain{index} > 0.0)\n"
                     f"        move += Bands.Move(Casc{index}Move, {_hlsl(band['patch_m'])}) * gain{index};")
    cells = SEA_GRID["half_cells"]
    return string.Template(SEA_GRID_CODE).substitute(BANDS=_sea_bands_code(), MOVES="\n".join(moves),
                                                     MORPH_FROM=_hlsl(0.75 * cells), MORPH_TO=_hlsl(cells - 4.0))


def near_ocean_code(sea):
    """OCEAN_CODE for the near sea: the same water (it reads the same cascades, where the grid's water rests)."""
    return ocean_code()


def sea_grid_mesh():
    """The near sea's grid (UEphemeraPlanetBuilder.build_sea_grid); one for every world."""
    folder = f"{ROOT}/Planets"
    name = "SM_SeaGrid"
    stamp = stamp_of("sea-grid-v1", SEA_GRID)
    mesh = unchanged(f"{folder}/{name}", stamp)
    if not mesh:
        mesh = unreal.EphemeraPlanetBuilder.build_sea_grid(folder, name, SEA_GRID["rings"], SEA_GRID["half_cells"], SEA_GRID["spacing_m"])
        if not mesh:
            raise RuntimeError("the sea grid could not be built")
        ASSETS.set_metadata_tag(mesh, "EphemeraStamp", stamp)
        ASSETS.save_loaded_asset(mesh)
        log("sea grid: rebuilt")
    return mesh


def dress_sea(body, depth_map):
    """Gives a world with a sea its near sea: the grid and its material, the far sea's material with the
    hole, and the cascades' numbers (UEphemeraSeaSurfaceComponent, UEphemeraOceanSim)."""
    sea = sea_bands()
    body.set_editor_property("sea_grid_mesh", sea_grid_mesh())
    body.set_editor_property("sea_grid_material", make_ocean_material(depth_map, "near"))
    body.set_editor_property("ocean_far_material", make_ocean_material(depth_map, "far"))
    body.set_editor_property("sea_grid_rings", SEA_GRID["rings"])
    body.set_editor_property("sea_grid_half_cells", SEA_GRID["half_cells"])
    body.set_editor_property("sea_grid_spacing_m", SEA_GRID["spacing_m"])
    bands = []
    for index, band in enumerate(sea["bands"]):
        entry = unreal.EphemeraSeaBand()
        # Waves: filled by the game from the simulation (UEphemeraOceanSim::Build).
        set_all(entry, {"patch_m": band["patch_m"], "choppiness": band["chop"], "gain": SEA_GAINS[index],
                        "peak_m": band["peak_m"], "break_depth_m": band["break_depth_m"], "max_spacing_m": band["max_spacing_m"],
                        "waves": []})
        bands.append(entry)
    body.set_editor_property("sea_bands", bands)
    charts = []
    for u, v, shift in SEA_CHARTS:
        chart = unreal.EphemeraSeaChart()
        set_all(chart, {"axis_u": unreal.Vector(*u), "axis_v": unreal.Vector(*v), "offset": unreal.Vector2D(*shift)})
        charts.append(chart)
    body.set_editor_property("sea_charts", charts)


STREAM_CODE = r"""
// Running water: a sheet lying over a stream's bed. P: planet-local position (cm). N: the sheet's
// normal. VC3: the way downhill (a unit vector packed into 0..1). UVd.y: the bed's slope (1 - cos). Chop: a wave map (slope in red and green,
// slope squared in blue, height in alpha). Returns the normal; also gives Base, Rough and Cover.
//
// The ripples are carried downstream. A pattern slid along a direction that changes from place
// to place tears itself apart, so it is slid only a short way, twice over, half a cycle apart,
// and each copy is faded out as it comes to the end of its run and starts again.
float3 pm = P * 0.01;
float3 up = normalize(P);
float3 down = normalize(VC3 * 2.0 - 1.0 + up * 0.0001);
float slope = UVd.y;
// Metres per second: a glide on the flats, a rush down a steep bed.
float speed = lerp(0.35, 2.8, smoothstep(0.01, 0.30, slope));
struct FStream
{
    float3 pm, dpx, dpy, w;
    // One reading of the wave map at a position shifted by `carried` metres. xyz: slope as a
    // vector; w: squared slope lost to filtering.
    float4 Waves(Texture2D T, SamplerState S, float3 carried, float metres, float strength, float2 skew)
    {
        float k = 1.0 / metres;
        float3 q = (pm - carried) * k;
        float4 sum = 0;
        [branch] if (w.x > 0.02)
        {
            float4 t = Texture2DSampleGrad(T, S, q.zy + skew, dpx.zy * k, dpy.zy * k);
            float2 sl = (t.rg * 2.0 - 1.0) * strength;
            sum += float4(0.0, sl.y, sl.x, max(t.b * strength * strength - dot(sl, sl), 0.0)) * w.x;
        }
        [branch] if (w.y > 0.02)
        {
            float4 t = Texture2DSampleGrad(T, S, q.xz + skew, dpx.xz * k, dpy.xz * k);
            float2 sl = (t.rg * 2.0 - 1.0) * strength;
            sum += float4(sl.x, 0.0, sl.y, max(t.b * strength * strength - dot(sl, sl), 0.0)) * w.y;
        }
        [branch] if (w.z > 0.02)
        {
            float4 t = Texture2DSampleGrad(T, S, q.xy + skew, dpx.xy * k, dpy.xy * k);
            float2 sl = (t.rg * 2.0 - 1.0) * strength;
            sum += float4(sl.x, sl.y, 0.0, max(t.b * strength * strength - dot(sl, sl), 0.0)) * w.z;
        }
        return sum;
    }
    float Froth(Texture2D T, SamplerState S, float3 carried, float metres)
    {
        float3 q = (pm - carried) / metres;
        return Texture2DSampleLevel(T, S, q.zy, 1.0).a * w.x + Texture2DSampleLevel(T, S, q.xz, 1.0).a * w.y + Texture2DSampleLevel(T, S, q.xy, 1.0).a * w.z;
    }
};
FStream Stream;
Stream.pm = pm; Stream.dpx = ddx(pm); Stream.dpy = ddy(pm);
Stream.w = pow(abs(up), 4.0);
Stream.w /= (Stream.w.x + Stream.w.y + Stream.w.z);
const float Run = 2.4;        // seconds each copy is carried before it starts again
float phaseA = frac(Time / Run), phaseB = frac(Time / Run + 0.5);
float fadeA = 1.0 - abs(2.0 * phaseA - 1.0), fadeB = 1.0 - fadeA;
float3 carriedA = down * (speed * Run * phaseA), carriedB = down * (speed * Run * phaseB);
float rush = lerp(0.55, 1.5, smoothstep(0.02, 0.28, slope));
float4 wv = (Stream.Waves(Chop, ChopSampler, carriedA, 2.3, 0.50 * rush, float2(0.0, 0.0)) + Stream.Waves(Chop, ChopSampler, carriedA, 0.8, 0.34 * rush, float2(0.31, 0.57))) * fadeA
          + (Stream.Waves(Chop, ChopSampler, carriedB, 2.3, 0.50 * rush, float2(0.43, 0.19)) + Stream.Waves(Chop, ChopSampler, carriedB, 0.8, 0.34 * rush, float2(0.77, 0.11))) * fadeB;
float3 slope3 = wv.xyz - N * dot(wv.xyz, N);
Rough = pow(0.0004 + wv.w, 0.25);
// White water where the bed is steep, torn into streaks that travel with the current.
float froth = Stream.Froth(Chop, ChopSampler, carriedA, 1.7) * fadeA + Stream.Froth(Chop, ChopSampler, carriedB, 1.7) * fadeB;
float white = smoothstep(0.12, 0.38, slope) * smoothstep(0.46, 0.66, froth + (smoothstep(0.16, 0.45, slope) - 0.5) * 0.3);
Cover = white * 0.9;
Base = float3(0.74, 0.77, 0.78);
Rough = lerp(Rough, 0.7, white);
return normalize(N - slope3);
"""


def make_stream_material():
    waves = water_maps()
    path = f"{ROOT}/Materials/M_Stream"
    stamp = stamp_of("stream-v2", STREAM_CODE, waves)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    # Made afresh each time (see make_ocean_material).
    if ASSETS.does_asset_exist(path) and not ASSETS.delete_asset(path):
        raise RuntimeError("M_Stream: the old material could not be removed")
    material = TOOLS.create_asset("M_Stream", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError("M_Stream: the material could not be created")
    material.set_editor_property("tangent_space_normal", False)

    def expr(cls, x=-900, y=0):
        return EDIT.create_material_expression(material, cls, x, y)

    def colour(parameter, value):
        node = expr(unreal.MaterialExpressionVectorParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
        return node

    world = expr(unreal.MaterialExpressionWorldPosition)
    local_pos = expr(unreal.MaterialExpressionTransformPosition)
    local_pos.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local_pos.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, "", local_pos, "")
    normal_ws = expr(unreal.MaterialExpressionVertexNormalWS)
    local_normal = expr(unreal.MaterialExpressionTransform)
    local_normal.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
    local_normal.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
    connect(normal_ws, "", local_normal, "")
    vertex = expr(unreal.MaterialExpressionVertexColor)
    chop = expr(unreal.MaterialExpressionTextureObject)
    chop.set_editor_property("texture", waves["chop"])
    chop.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    sources = [("P", local_pos, ""), ("N", local_normal, ""), ("VC3", vertex, ""), ("UVd", expr(unreal.MaterialExpressionTextureCoordinate), ""),
               ("Time", expr(unreal.MaterialExpressionTime), ""), ("Chop", chop, "")]
    custom = expr(unreal.MaterialExpressionCustom, -300, 0)
    inputs = []
    for input_name, _, _ in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    outputs = []
    for output_name, output_type in (("Base", unreal.CustomMaterialOutputType.CMOT_FLOAT3), ("Rough", unreal.CustomMaterialOutputType.CMOT_FLOAT1),
                                     ("Cover", unreal.CustomMaterialOutputType.CMOT_FLOAT1)):
        entry = unreal.CustomOutput()
        entry.set_editor_property("output_name", output_name)
        entry.set_editor_property("output_type", output_type)
        outputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", STREAM_CODE)
    for input_name, node, pin in sources:
        connect(node, pin, custom, input_name)
    world_normal = expr(unreal.MaterialExpressionTransform, -100, 0)
    world_normal.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    world_normal.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(custom, "", world_normal, "")
    connect_property(world_normal, "", unreal.MaterialProperty.MP_NORMAL)
    connect_property(custom, "Base", unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(custom, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    connect_property(custom, "Cover", unreal.MaterialProperty.MP_OPACITY)
    # Clear hill water: a hand's depth of it hardly colours the stones beneath.
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_SINGLE_LAYER_WATER)
    body = expr(unreal.MaterialExpressionSingleLayerWaterMaterialOutput, 200, 300)
    connect(colour("Absorb", (0.0040, 0.0014, 0.0012)), "", body, "AbsorptionCoefficients")
    connect(colour("Scatter", (0.000020, 0.000040, 0.000040)), "", body, "ScatteringCoefficients")
    specular = expr(unreal.MaterialExpressionConstant, -100, 480)
    specular.set_editor_property("r", 0.255)
    connect_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    return finish_material(material, stamp, "M_Stream")


CLOUD_CODE = r"""
// WP: the sample's world position (cm). Centre, Rot: where the planet is and how it is turned in the
// current frame (set every frame by the sky rig). NormAlt: 0 at the cloud base, 1 at the top.
// Weather, Shape and Detail are our own textures (EphemeraCloudBuilder), each with its values spread
// evenly over 0..1, so "value > 1 - c" selects a fraction c: Coverage is the share of the planet
// under cloud systems, and inside a system the share of the layer that is cloud follows from it.
float3 d = WP - Centre.xyz;
float3 qv = Rot.xyz;
float3 tq = 2.0 * cross(qv, d);
float3 pm = (d + Rot.w * tq + cross(qv, tq)) * 0.01;
float3 dir = normalize(pm);
// Where the weather is: looked up by direction from the planet's centre, so it turns with the planet.
float weather = Texture3DSampleLevel(Weather, View.MaterialTextureBilinearClampedSampler, dir * 0.46 + 0.5, 0).r;
float fronts = smoothstep(1.0 - Coverage - Soft, 1.0 - Coverage + Soft, weather);
// A repeating texture laid straight over a planet a few repeats across shows as a lattice. The
// lookup is turned off the planet's axes and nudged by the same texture read very large, so no two
// repeats look alike. Wander is that nudge in repeats: keep it well under one, or the lookup folds
// over itself and the clouds are cut into ribbons.
float3 turned = float3(dot(pm, float3(0.81, 0.47, -0.35)), dot(pm, float3(-0.41, 0.87, 0.26)), dot(pm, float3(0.43, -0.07, 0.90)));
float pa = Texture3DSampleLevel(Shape, View.MaterialTextureBilinearWrapedSampler, turned / (Billow * 6.7) + 0.31, 0).r;
float pb = Texture3DSampleLevel(Shape, View.MaterialTextureBilinearWrapedSampler, turned.zxy / (Billow * 5.3) + 0.67, 0).r;
float3 push = float3(pa, pb, 1.0 - 0.5 * (pa + pb)) * Wander;
// Flat bases, rounded tops, and the tops at different heights from one cloud bank to the next
// (between TopLow of the layer and all of it): a deck that all stops at the same height wraps the
// planet's edge as one smooth roll.
float top = lerp(TopLow, 1.0, pa);
// (The base is level but not ruled: it sits a little higher under one bank than the next and comes
// in over a fifth of the layer. A base that begins at one height everywhere, all at once, is the
// underside of a slab.)
float bn = Texture3DSampleLevel(Shape, View.MaterialTextureBilinearWrapedSampler, turned.yzx / (Billow * 1.7) + 0.11, 0).r;
float base = (pb * 0.4 + bn * 0.6) * 0.34;
float profile = smoothstep(base, base + 0.22, NormAlt) * (1.0 - smoothstep(top * 0.45, top, NormAlt));
// Look-development views (-EphemeraSky=cloud.Debug=n): 1 = 500 m stripes along the planet's own x
// axis, 2 = the weather map, 3 = the billow texture laid straight on the planet.
if (Debug > 0.5 && Debug < 1.5) return profile * step(0.5, frac(pm.x / 1000.0)) * Density;
if (Debug > 1.5 && Debug < 2.5) return profile * step(0.5, weather) * Density;
if (Debug > 2.5) return profile * step(0.6, Texture3DSampleLevel(Shape, View.MaterialTextureBilinearWrapedSampler, pm / Billow, 0).r) * Density;
// Fill: even the heart of a system is broken cloud, not one slab.
float cover = fronts * profile * Fill;
// Drift is the wind: how far the billows have moved, in repeats of their texture (0..1, set by the
// sky rig from the game's own clock). The engine's Time is not used: it is large enough that
// adding it to a texture coordinate rounds the coordinate off and cuts the clouds into slabs.
float3 drift = float3(Drift, 0.0, 0.0);
float shape = Texture3DSampleLevel(Shape, View.MaterialTextureBilinearWrapedSampler, turned / Billow + push + drift, 0).r;
float dns = saturate((shape - (1.0 - cover)) / max(cover, 0.001));
// Thin at the edge and thick at the heart: a cloud that is as dense at its rim as at its middle has
// a surface, like a thing cut from foam.
dns = dns * dns * (2.2 - 1.2 * dns);
// How a cloud looks depends on how far away it is. Near the camera: separate billows with frayed
// edges. Far away (from FarStart to FarEnd metres) none of that can be made out, and drawing it
// anyway turns a planet's cloud cover into popcorn: what is seen from orbit is the weather itself,
// soft-edged sheets and bands with broad mottling, thin enough at their edges to see through.
float camM = length(WP - Cam) * 0.01;
float far = smoothstep(FarStart, FarEnd, camM);
// And close to (inside NearEnd metres) a cloud is not a thing with a surface at all: it is mist,
// thinner the nearer it is, and frayed at a finer grain than its billows. Drawn at full thickness
// right up to the eye, the textures it is made from show their own cells and the cloud arrives as
// a wall of soft blocks.
// (Only for what the eye sees: the shadow a cloud casts, and the light inside it, take the cloud as it
// is. Thinned for the shadow too, every cloud's shadow had a hole that went along with the player.)
float near = (1.0 - smoothstep(NearStart, NearEnd, camM)) * (ShDist > 0.5 ? 0.0 : 1.0);
// Wisps eat into the edges and leave the dense cores alone.
float wisps = Texture3DSampleLevel(Detail, View.MaterialTextureBilinearWrapedSampler, turned.yzx / Wisp + push * 2.0 - drift * 3.0, 0).r;
float fine = Texture3DSampleLevel(Detail, View.MaterialTextureBilinearWrapedSampler, turned.zxy / (Wisp * 0.29) + push * 3.0 + drift * 5.0 + 0.37, 0).r;
float eaten = saturate(wisps * (1.0 - 0.45 * near) + fine * 0.55 * near) * min(Erode + 0.2 * near, 0.95) * (1.0 - far);
dns = saturate((dns - eaten) / (1.0 - eaten));
float mottle = Texture3DSampleLevel(Shape, View.MaterialTextureBilinearWrapedSampler, turned.zxy / (Billow * 2.6) + push * 0.5 + 0.43, 0).r;
// From orbit a weather system has an edge: inside it the sky is cloud, broken into cells and
// streets with gaps of clear air; outside it there is none. (Left soft, the whole planet reads as
// frosted glass.) The cover is drawn up to a firm edge and the cloud within it into distinct masses.
float system = smoothstep(0.30, 0.62, fronts);
float cells = Texture3DSampleLevel(Shape, View.MaterialTextureBilinearWrapedSampler, turned / (Billow * 0.85) + push + drift * 0.6 + 0.19, 0).r;
float masses = smoothstep(0.40, 0.62, mottle * 0.55 + cells * 0.45);
float veil = system * profile * masses;
dns = lerp(dns, veil, far * FarVeil);
return dns * Density * lerp(1.0, FarThin, far) * lerp(1.0, NearThin, near * near);
"""

# Where there can be cloud at all, from the weather map alone: one texture read. The engine asks
# this first for every step of a ray and skips the full material wherever it is zero.
CLOUD_COVER_CODE = r"""
float3 d = WP - Centre.xyz;
float3 qv = Rot.xyz;
float3 tq = 2.0 * cross(qv, d);
float3 dir = normalize(d + Rot.w * tq + cross(qv, tq));
float weather = Texture3DSampleLevel(Weather, View.MaterialTextureBilinearClampedSampler, dir * 0.46 + 0.5, 0).r;
float fronts = smoothstep(1.0 - Coverage - Soft, 1.0 - Coverage + Soft, weather);
float profile = smoothstep(0.0, 0.10, NormAlt) * (1.0 - smoothstep(0.40, 1.0, NormAlt));
return float3(fronts * profile, 0.0, 0.0);
"""

# How much of the sun reaches a bit of cloud past the planet itself. The engine does not ask: it lights
# cloud on the night side as it does at noon (tried on 14 October 2026 in all three of its modes: at
# local midnight, the sun 70 degrees under, the clouds stood white in a black sky). So the cloud's own
# brightness is taken down wherever the straight line to the sun passes through the world.
CLOUD_LIT_CODE = r"""
float3 d = WP - Centre.xyz;
float3 L = normalize(Sun);
float along = dot(d, L);
// How far above the ground the line to the sun passes at its lowest (cm); below zero it is through rock.
float clear = sqrt(max(dot(d, d) - along * along, 0.0)) - RadiusCm;
// The shadow's edge is soft: the last light comes round the limb through a great deal of air.
// In the planet's shadow a cloud is lit by the night (the moon and the sky: the sky rig sets Unlit to
// their light over the sun's, about two in a hundred thousand), not by the noon sun the engine would
// give it. Ten orders of brightness lie between, so the dusk edge is blended in stops, not linearly,
// and widely, or it is a cliff.
float lit = along >= 0.0 ? 1.0 : smoothstep(-40000.0, 150000.0, clear);
return exp2(lerp(log2(max(Unlit, 1e-7)), 0.0, lit));
"""

CLOUD_TEXTURES = f"{ROOT}/Textures/Clouds"
# Terrains baked offline by Tools/terrain/bake.py.
BAKES = pathlib.Path(__file__).resolve().parents[1] / "SourceAssets" / "terrain"


def cloud_textures():
    """The weather map and the two noise volumes, generated once (bump the stamp to regenerate)."""
    stamp = "cloud-textures-v2|seed 11"
    names = ("T_CloudWeather", "T_CloudShape", "T_CloudDetail")
    marker = unchanged(f"{CLOUD_TEXTURES}/{names[0]}", stamp)
    if not marker or not all(ASSETS.does_asset_exist(f"{CLOUD_TEXTURES}/{name}") for name in names):
        if not unreal.EphemeraCloudBuilder.build_cloud_textures(CLOUD_TEXTURES, 11):
            raise RuntimeError("cloud textures could not be built")
        marker = ASSETS.load_asset(f"{CLOUD_TEXTURES}/{names[0]}")
        ASSETS.set_metadata_tag(marker, "EphemeraStamp", stamp)
        ASSETS.save_loaded_asset(marker)
        log("cloud textures: rebuilt")
    textures = [ASSETS.load_asset(f"{CLOUD_TEXTURES}/{name}") for name in names]
    for name, texture in zip(names, textures):
        if not isinstance(texture, unreal.VolumeTexture):
            raise RuntimeError(f"{name} is not a volume texture: {texture}")
    return textures


def make_cloud_material():
    weather_texture, shape_texture, detail_texture = cloud_textures()
    path = f"{ROOT}/Materials/M_Clouds"
    stamp = stamp_of("clouds-v27", CLOUD_CODE, CLOUD_COVER_CODE, CLOUD_LIT_CODE, 2e-5)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    # The asset is made afresh each time: clearing a material's nodes leaves its Volumetric Advanced
    # Output node behind, and a second one beside it stops the material compiling ("can contain
    # only one"), at which point the engine quietly draws no clouds at all.
    if ASSETS.does_asset_exist(path) and not ASSETS.delete_asset(path):
        raise RuntimeError("M_Clouds: the old material could not be removed")
    material = TOOLS.create_asset("M_Clouds", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    if not material:
        raise RuntimeError("M_Clouds: the material could not be created")
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)

    def expr(cls, x=-900, y=0):
        return EDIT.create_material_expression(material, cls, x, y)

    def scalar(parameter, value, y):
        node = expr(unreal.MaterialExpressionScalarParameter, -900, y)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", value)
        return node

    def vector(parameter, value, y):
        node = expr(unreal.MaterialExpressionVectorParameter, -900, y)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", unreal.LinearColor(*value))
        return node

    def volume(texture, y):
        node = expr(unreal.MaterialExpressionTextureObject, -900, y)
        node.set_editor_property("texture", texture)
        return node

    world = expr(unreal.MaterialExpressionWorldPosition, -900, 0)
    attributes = expr(unreal.MaterialExpressionCloudSampleAttribute, -900, 120)
    centre = vector("PlanetCentre", (0.0, 0.0, 0.0, 0.0), 480)
    rotation = vector("PlanetRotation", (0.0, 0.0, 0.0, 1.0), 640)
    weather_node = volume(weather_texture, 260)
    coverage = scalar("Coverage", 0.42, 800)
    soft = scalar("Soft", 0.10, 850)
    sources = [("WP", world, ""), ("Centre", centre, ""), ("Rot", rotation, "RGBA"),
               ("NormAlt", attributes, "NormAltitudeInLayer"), ("ShDist", attributes, "ShadowSampleDistance"),
               ("Drift", scalar("Drift", 0.0, 380), ""),
               ("Cam", expr(unreal.MaterialExpressionCameraPositionWS, -900, 430), ""),
               # Distance (m) over which clouds go from separate billows to the far-off veil; how
               # completely they do (1 = wholly); and how much thinner the veil is than the billows.
               ("FarStart", scalar("FarStart", 3000.0, 1900), ""), ("FarEnd", scalar("FarEnd", 12000.0, 2000), ""),
               ("FarVeil", scalar("FarVeil", 0.7, 2100), ""), ("FarThin", scalar("FarThin", 0.8, 2200), ""),
               # Close to: the distance (m) inside which a cloud thins toward mist, and how thin it is at the eye.
               ("NearStart", scalar("NearStart", 40.0, 2300), ""), ("NearEnd", scalar("NearEnd", 520.0, 2400), ""),
               ("NearThin", scalar("NearThin", 0.16, 2500), ""),
               ("Weather", weather_node, ""), ("Shape", volume(shape_texture, 1100), ""),
               ("Detail", volume(detail_texture, 1200), ""),
               # Share of the planet under cloud systems, and how soft the edge of a system is.
               ("Coverage", coverage, ""), ("Soft", soft, ""),
               # How thick the cloud is: extinction per metre.
               ("Density", scalar("Density", 0.024, 900), ""),
               # The lowest a cloud bank's top can sit, as a fraction of the layer's thickness.
               ("TopLow", scalar("TopLow", 0.35, 950), ""),
               # Size of the billow texture's repeat (its largest billows are a quarter of this) and of
               # the wisp texture's, in metres; and how deep the wisps eat into a cloud's edge.
               ("Billow", scalar("Billow", 1100.0, 1300), ""), ("Wisp", scalar("Wisp", 180.0, 1400), ""),
               ("Erode", scalar("Erode", 0.72, 1500), ""),
               # Most cloud a system's heart can hold, and how far the billow lookup wanders (in repeats).
               ("Fill", scalar("Fill", 0.9, 1600), ""), ("Wander", scalar("Wander", 0.2, 1700), ""),
               ("Debug", scalar("Debug", 0.0, 1800), "")]
    custom = expr(unreal.MaterialExpressionCustom, -300, 0)
    inputs = []
    for input_name, _, _ in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property("code", CLOUD_CODE)
    for input_name, node, pin in sources:
        connect(node, pin, custom, input_name)
    # In a volume material the Subsurface Colour pin is extinction; Base Colour is the cloud's albedo.
    connect_property(custom, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    albedo = expr(unreal.MaterialExpressionConstant3Vector, -300, 300)
    albedo.set_editor_property("constant", unreal.LinearColor(0.96, 0.96, 0.96, 1.0))
    # In the planet's shadow a cloud keeps a thirty-fifth of its brightness: by night it is a dark shape
    # against the stars, faintly grey, and no longer a lamp.
    lit = expr(unreal.MaterialExpressionCustom, -300, 380)
    lit_sources = [("WP", world, ""), ("Centre", centre, ""), ("Sun", expr(unreal.MaterialExpressionSkyAtmosphereLightDirection, -900, 2600), ""),
                   ("RadiusCm", scalar("PlanetRadiusCm", 500000.0, 2700), ""), ("Unlit", scalar("Unlit", 2e-5, 2800), "")]
    lit_inputs = []
    for input_name, _, _ in lit_sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        lit_inputs.append(entry)
    lit.set_editor_property("inputs", lit_inputs)
    lit.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    lit.set_editor_property("code", CLOUD_LIT_CODE)
    for input_name, node, pin in lit_sources:
        connect(node, pin, lit, input_name)
    shaded = expr(unreal.MaterialExpressionMultiply, -100, 320)
    connect(albedo, "", shaded, "A")
    connect(lit, "", shaded, "B")
    connect_property(shaded, "", unreal.MaterialProperty.MP_BASE_COLOR)
    # How light moves through the cloud. Without this node the engine scatters light evenly in all
    # directions, once: every cloud is the same flat white. With it:
    #  - two lobes of scattering, mostly forward (the silver lining when a cloud is between you and
    #    the sun) with some thrown back (the bright sunlit face);
    #  - two further "octaves" of light that has bounced more than once, each dimmer, less shadowed
    #    and more even than the last (Wrenninge's approximation, as Frostbite and Unreal use it):
    #    this is what fills a cloud's shadowed side with soft light instead of leaving it black;
    #  - light bounced up from the ground onto the cloud's base.
    advanced = expr(unreal.MaterialExpressionVolumetricAdvancedMaterialOutput, 100, 500)
    set_all(advanced, {
        "const_phase_g": 0.6, "const_phase_g2": -0.3, "const_phase_blend": 0.3,
        "multi_scattering_approximation_octave_count": 2,
        "const_multi_scattering_contribution": 0.6, "const_multi_scattering_occlusion": 0.45,
        "const_multi_scattering_eccentricity": 0.5,
        "ground_contribution": True, "ray_march_volume_shadow": True,
    })
    cover_sources = [("WP", world, ""), ("Centre", centre, ""), ("Rot", rotation, "RGBA"),
                     ("NormAlt", attributes, "NormAltitudeInLayer"), ("Weather", weather_node, ""),
                     ("Coverage", coverage, ""), ("Soft", soft, "")]
    cover = expr(unreal.MaterialExpressionCustom, -300, 600)
    cover_inputs = []
    for input_name, _, _ in cover_sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        cover_inputs.append(entry)
    cover.set_editor_property("inputs", cover_inputs)
    cover.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    cover.set_editor_property("code", CLOUD_COVER_CODE)
    for input_name, node, pin in cover_sources:
        connect(node, pin, cover, input_name)
    connect(cover, "", advanced, "ConservativeDensity")
    # Without this flag the engine skips the clouds, and its sky-light capture then crashes on the
    # cloud image it expected.
    material.set_editor_property("used_with_volumetric_cloud", True)
    return finish_material(material, stamp, "M_Clouds")


MIST_CODE = r"""
// The thickness of the air at one point of the fog volume, per metre. WP: world position (cm).
// Centre, RadiusCm: the world the camera is at. Mist lies low: it is thickest at sea level and
// thins with height (HeightM), so the valleys and the shore hold it and the high ground stands
// clear of it. It is not even: it lies in banks a few hundred metres across with clearer air between.
float3 q = (WP - Centre) * 0.01;
float alt = length(q) - RadiusCm * 0.01;
#define H(c) frac(sin(dot(c, float3(127.1, 311.7, 74.7))) * 43758.5453)
struct FBank
{
    float At(float3 p)
    {
        float3 c = floor(p);
        float3 f = frac(p);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(H(c), H(c + float3(1, 0, 0)), f.x), lerp(H(c + float3(0, 1, 0)), H(c + float3(1, 1, 0)), f.x), f.y),
                    lerp(lerp(H(c + float3(0, 0, 1)), H(c + float3(1, 0, 1)), f.x), lerp(H(c + float3(0, 1, 1)), H(c + float3(1, 1, 1)), f.x), f.y), f.z);
    }
};
FBank Bank;
float n = Bank.At(q / 310.0 + 7.0) * 0.65 + Bank.At(q / 95.0 + 31.0) * 0.35;
float banks = lerp(1.0, lerp(0.30, 1.85, smoothstep(0.28, 0.74, n)), Banks);
return PerKm * 0.001 * exp(-max(alt, 0.0) / max(HeightM, 1.0)) * banks;
"""


def make_mist_material():
    """The air's thickness for the fog volume (AEphemeraSkyRig's MistVolume): a round world's mist."""
    path = f"{ROOT}/Materials/M_Mist"
    stamp = stamp_of("mist-v1", MIST_CODE)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset("M_Mist", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_VOLUME)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)

    def expr(cls, x=-900, y=0):
        return EDIT.create_material_expression(material, cls, x, y)

    def scalar(parameter, value, y):
        node = expr(unreal.MaterialExpressionScalarParameter, -900, y)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", value)
        return node

    centre = expr(unreal.MaterialExpressionVectorParameter, -900, 0)
    centre.set_editor_property("parameter_name", "Centre")
    centre.set_editor_property("default_value", unreal.LinearColor(0.0, 0.0, 0.0, 0.0))
    sources = [("WP", expr(unreal.MaterialExpressionWorldPosition, -900, -200), ""), ("Centre", centre, ""),
               ("RadiusCm", scalar("RadiusCm", 500000.0, 200), ""), ("PerKm", scalar("PerKm", 0.0, 300), ""),
               ("HeightM", scalar("HeightM", 130.0, 400), ""), ("Banks", scalar("Banks", 1.0, 500), "")]
    custom = expr(unreal.MaterialExpressionCustom, -300, 0)
    inputs = []
    for input_name, _, _ in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    custom.set_editor_property("code", MIST_CODE)
    for input_name, node, pin in sources:
        connect(node, pin, custom, input_name)
    # In a volume material this pin is the extinction (how much light a metre of the air stops).
    connect_property(custom, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    # Water droplets scatter nearly all the light that meets them, the blue a little more.
    albedo = expr(unreal.MaterialExpressionVectorParameter, -300, 300)
    albedo.set_editor_property("parameter_name", "Albedo")
    albedo.set_editor_property("default_value", unreal.LinearColor(0.90, 0.925, 0.96, 1.0))
    connect_property(albedo, "", unreal.MaterialProperty.MP_BASE_COLOR)
    return finish_material(material, stamp, "M_Mist")


# How far the motes' cloud goes each way from the eye. Small on purpose: a mote is seen as a soft dot a few pixels
# wide or not at all (the first set, true to size out to 12 m, were single pixels and the anti-aliasing ate them).
MOTE_REACH_CM = 800.0

MOTE_OFFSET_CODE = r"""
// Where a corner of a mote goes, as a move from where the mesh has it (cm). The mesh is a cloud of small
// squares round the eye (EphemeraPlanetBuilder.build_quad_cloud). Local: this corner's place in the mesh.
// WP: the same in the world. UV: which corner. VC: three numbers drawn for the mote. Cam: the eye.
// Centre: the world's centre. Reach: how far the cloud goes each way (the mesh's extent).
// A mote keeps its place in the world while the eye moves through the cloud: the cloud repeats every
// two Reaches each way, and what is drawn is always the copy nearest the eye. They ride the grass's wind
// (GRASS_WIND_CODE's W), wander a hand's width about, and settle slowly.
// Most are dust, which is only seen where light is behind it or a lamp is on it. A few in every hundred are
// seed fluff (cotton grass lets it go all summer): white tufts the size of a thumbnail that the wind carries
// three times as fast. Those are what is seen crossing a sunlit plain.
float3 seed = Local - float3(UV.x - 0.5, UV.y - 0.5, 0.0);
float3 up = normalize(Cam - Centre.xyz);
float3 across = normalize(cross(up, float3(0.31, 0.12, 0.94)));
float3 along = cross(across, up);
float3 W = normalize(across * 0.85 + along * 0.52);
float ph = VC.g * 6.2832;
float fluff = step(0.968, VC.r);
float3 wander = float3(sin(Time * 0.43 + ph), sin(Time * 0.31 + ph * 1.7), sin(Time * 0.37 + ph * 2.3)) * lerp(16.0, 30.0, fluff);
float3 q = seed + W * (Time * Drift * lerp(1.0, 3.0, fluff)) - up * (Time * Sink) + wander;
float edge = Reach * 2.0;
float3 r = frac((q - Cam) / edge + 0.5) * edge - Reach;
// One too small to fill a pixel is drawn a pixel wide (and that much fainter, in the look): it does not flicker.
float size = max(lerp(lerp(SizeMin, SizeMax, VC.b), lerp(FluffMin, FluffMax, VC.b), fluff), length(r) * PixelAngle);
return Cam + r + (ResolvedView.ViewRight * (UV.x - 0.5) + ResolvedView.ViewUp * (UV.y - 0.5)) * size - WP;
"""

MOTE_LOOK_CODE = r"""
// A mote: a soft round speck, lit as the air round it is lit. UV: across its square. VC: its three numbers.
// WP: where this pixel is. Cam: the eye.
float2 c = UV - 0.5;
float fluff = step(0.968, VC.r);
float speck = saturate(1.0 - length(c) * 2.0);
speck *= speck;
float dist = length(WP - Cam);
float small = saturate(lerp(lerp(SizeMin, SizeMax, VC.b), lerp(FluffMin, FluffMax, VC.b), fluff) / max(dist * PixelAngle, 0.001));
small *= small;
float near = smoothstep(40.0, 130.0, dist);                   // none in the eye
float far = 1.0 - smoothstep(Reach * 0.62, Reach * 0.95, dist);
float turn = lerp(0.55 + 0.45 * sin(Time * (1.3 + VC.b * 2.4) + VC.g * 40.0), 1.0, fluff);     // dust tumbles, and glints as it does
Alpha = saturate(speck * small * near * far * turn * Amount * lerp(0.35 + 0.65 * VC.g, 0.8, fluff));
// Dust is no brighter in the sun than the sky behind it (a white speck against blue sky is a fault on the lens).
return lerp(Colour.rgb, Fluff.rgb, fluff);
"""

PUFF_OFFSET_CODE = r"""
// Where a corner of one tuft of a puff of dust goes, as a move from where the mesh has it (cm). The mesh is
// a few dozen small squares whose places are only numbers drawn for them (a cube 100 cm each way).
// Local: this corner's place in the mesh. WP: the same in the world. UV: which corner. VC: three more numbers.
// Centre: the point on the ground the dust was thrown from. Up: the ground's up. Age: seconds since.
// Spread: how far it runs (cm). Thrown outward along the ground, fast, the air stops it within a second;
// it billows as it slows, lifts a little, and thins.
float3 seed = (Local - float3(UV.x - 0.5, UV.y - 0.5, 0.0)) / 100.0;
float3 up = normalize(Up.xyz);
float3 t1 = normalize(cross(up, abs(up.z) < 0.9 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0)));
float3 t2 = cross(up, t1);
float angle = seed.x * 3.14159;
float3 outward = t1 * cos(angle) + t2 * sin(angle);
float a = max(Age, 0.0);
float run = 1.0 - exp(-a * 3.4);
float reach = Spread * (0.35 + 0.65 * (seed.y * 0.5 + 0.5)) * run;
float tall = seed.z * 0.5 + 0.5;
float size = lerp(SizeStart, SizeEnd, saturate(a / Life)) * (0.6 + 0.4 * VC.r) * (0.5 + Spread / 600.0);
float lift = tall * tall * (Spread * 0.22 * run + 18.0 * a) + size * 0.22;
float3 at = Centre.xyz + outward * reach + up * lift;
return at + (ResolvedView.ViewRight * (UV.x - 0.5) + ResolvedView.ViewUp * (UV.y - 0.5)) * size - WP;
"""

PUFF_LOOK_CODE = r"""
// One tuft of dust: soft, torn at the edges, thinning as it ages. Tint: the ground's own colour.
float2 c = UV - 0.5;
float soft = saturate(1.0 - length(c) * 2.0);
soft = soft * soft * (3.0 - 2.0 * soft);
float life = saturate(Age / Life);
float fade = (1.0 - life) * (1.0 - life);
float rise = smoothstep(0.0, 0.06, life);
float torn = 0.72 + 0.28 * sin(c.x * 21.0 + VC.g * 40.0) * sin(c.y * 18.0 + VC.b * 30.0);
Alpha = soft * torn * fade * rise * Strength * Thick * (0.45 + 0.55 * VC.b);
return Tint.rgb;
"""

FLAME_CODE = r"""
// A jet's flame, drawn on a cone whose point is the flame's tip. L: this pixel's place in the cone (cm; it is
// 100 long, the point at +50, 50 across at the other end). N: the cone's surface normal; Ln: the same in the
// cone's own space. V: toward the eye. Power: how hard the jet burns.
// Nearly white where it leaves the nozzle, blue as it cools toward the tip; deep seen through its middle and
// nothing at its rim (so the cone's edge is never seen); never still.
float along = saturate((50.0 - L.z) / 100.0);
float through = pow(abs(dot(normalize(N), normalize(V))), 1.5);
float unrest = 0.82 + 0.18 * sin(Time * 71.0 + along * 19.0) * sin(Time * 43.0 + 1.7);
float heat = along * along;
float3 hot = float3(0.80, 0.90, 1.0);
float3 cool = float3(0.10, 0.28, 1.0);
float body = smoothstep(0.0, 0.3, along) * (0.2 + 0.8 * heat);
float3 side = lerp(cool, hot, heat) * (through * body);
// The cone's flat end is at the nozzle, and it is what the player sees: looking down past the pack, into the
// flame from behind. Drawn as the rest is, it was a white disc. It is the flame's heart: bright in the
// middle, blue toward the edge and gone before the rim.
float rim = saturate(length(L.xy) / 50.0);
float heart = 1.0 - rim * rim;
heart *= heart;
float3 mouth = lerp(cool, hot, heart * heart) * heart * 0.8;
return lerp(side, mouth, step(Ln.z, -0.5)) * (unrest * Bright * (0.5 + 0.5 * min(Power, 1.5)));
"""


def make_effect_material(name, version, look_code, offset_code=None, scalars=None, vectors=None, lit=True,
                         depth_fade_cm=0.0, on_screen=False):
    """A material for something drawn in the air: motes, dust, flame.

    look_code gives the colour (and, for a lit one, Alpha); offset_code, if any, moves each corner of a
    cloud of squares (EphemeraPlanetBuilder.build_quad_cloud) to where it should be, facing the eye.
    Each is handed whichever of these its text names: Local, WP, UV, VC, Time, Cam, N, V, L and Ln (the pixel's
    place and its surface normal in the mesh's own space) and the scalars and vectors given. lit: lit as the air is (the lighting
    volume translucent things read: sun, shadow, sky, lamps); else it gives its own light, added to what
    is behind it, and on_screen keeps that light the same on screen whatever the camera's exposure.
    """
    import re
    scalars = scalars or {}
    vectors = vectors or {}
    path = f"{ROOT}/Materials/{name}"
    stamp = stamp_of(version, look_code, offset_code, scalars, vectors, lit, depth_fade_cm, on_screen)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset(name, f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("two_sided", True)
    if lit:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        material.set_editor_property("translucency_lighting_mode", unreal.TranslucencyLightingMode.TLM_VOLUMETRIC_NON_DIRECTIONAL)
    else:
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    made = {}
    row = [0]

    def expr(cls, x=-900):
        row[0] += 130
        return EDIT.create_material_expression(material, cls, x, row[0])

    plain = {"Local": unreal.MaterialExpressionPreSkinnedPosition, "WP": unreal.MaterialExpressionWorldPosition,
             "UV": unreal.MaterialExpressionTextureCoordinate, "VC": unreal.MaterialExpressionVertexColor,
             "Time": unreal.MaterialExpressionTime, "Cam": unreal.MaterialExpressionCameraPositionWS,
             "N": unreal.MaterialExpressionVertexNormalWS, "V": unreal.MaterialExpressionCameraVectorWS}

    def source(label):
        if label in made:
            return made[label]
        if label in scalars:
            node = expr(unreal.MaterialExpressionScalarParameter)
            node.set_editor_property("parameter_name", label)
            node.set_editor_property("default_value", scalars[label])
        elif label in vectors:
            node = expr(unreal.MaterialExpressionVectorParameter)
            node.set_editor_property("parameter_name", label)
            node.set_editor_property("default_value", unreal.LinearColor(*vectors[label], 0.0))
        elif label == "Ln":
            node = expr(unreal.MaterialExpressionTransform, -700)
            node.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
            node.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
            connect(source("N"), "", node, "")
        elif label == "L":
            node = expr(unreal.MaterialExpressionTransformPosition, -700)
            node.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
            node.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
            connect(source("WP"), "", node, "")
        else:
            node = expr(plain[label])
        made[label] = node
        return node

    names = list(plain) + ["L", "Ln"] + list(scalars) + list(vectors)

    def custom(code, extra=()):
        node = expr(unreal.MaterialExpressionCustom, -300)
        used = [label for label in names if re.search(rf"\b{label}\b", code)]
        inputs = []
        for label in used:
            entry = unreal.CustomInput()
            entry.set_editor_property("input_name", label)
            inputs.append(entry)
        outputs = []
        for label in extra:
            output = unreal.CustomOutput()
            output.set_editor_property("output_name", label)
            output.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
            outputs.append(output)
        node.set_editor_property("inputs", inputs)
        if outputs:
            node.set_editor_property("additional_outputs", outputs)
        node.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        node.set_editor_property("code", code)
        for label in used:
            connect(source(label), "", node, label)
        return node

    if offset_code:
        connect_property(custom(offset_code), "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    look = custom(look_code, ("Alpha",) if lit else ())
    if lit:
        connect_property(look, "", unreal.MaterialProperty.MP_BASE_COLOR)
        if depth_fade_cm > 0.0:
            # Where it meets the ground it thins out over this distance, not cut off along a line.
            fade = expr(unreal.MaterialExpressionDepthFade, -100)
            fade.set_editor_property("fade_distance_default", depth_fade_cm)
            connect(look, "Alpha", fade, "Opacity")
            connect_property(fade, "", unreal.MaterialProperty.MP_OPACITY)
        else:
            connect_property(look, "Alpha", unreal.MaterialProperty.MP_OPACITY)
    elif on_screen:
        held = expr(unreal.MaterialExpressionEyeAdaptationInverse, -100)
        connect(look, "", held, "LightValueInput")
        connect_property(held, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    else:
        connect_property(look, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish_material(material, stamp, name)


def quad_cloud(name, seed, count, extent_cm, material):
    """A cloud of small squares for an effect material to place (see EphemeraPlanetBuilder.build_quad_cloud)."""
    folder = f"{ROOT}/Effects"
    path = f"{folder}/{name}"
    stamp = stamp_of("quad-cloud-v1", seed, count, extent_cm)
    mesh = unchanged(path, stamp)
    if not mesh:
        mesh = unreal.EphemeraPlanetBuilder.build_quad_cloud(folder, name, seed, count, extent_cm)
        if not mesh:
            raise RuntimeError(f"quad cloud {name} failed")
        ASSETS.set_metadata_tag(mesh, "EphemeraStamp", stamp)
    mesh.set_material(0, material)
    ASSETS.save_loaded_asset(mesh)
    return mesh


def make_effects():
    """What is seen in the air and of the player: motes, the dust of a landing, the jets' flame."""
    motes = make_effect_material("M_Motes", "motes-v4", MOTE_LOOK_CODE, MOTE_OFFSET_CODE,
                                 scalars={"Reach": MOTE_REACH_CM, "Amount": 0.75, "SizeMin": 0.8, "SizeMax": 2.2, "FluffMin": 2.0, "FluffMax": 3.6, "PixelAngle": 0.0032,
                                          "Drift": 38.0, "Sink": 2.5},
                                 vectors={"Centre": (0.0, 0.0, 0.0), "Colour": (0.22, 0.21, 0.17), "Fluff": (0.60, 0.59, 0.54)})
    puff = make_effect_material("M_Puff", "puff-v2", PUFF_LOOK_CODE, PUFF_OFFSET_CODE,
                                scalars={"Age": 0.0, "Life": 1.7, "Spread": 300.0, "Strength": 1.0, "Thick": 0.6, "SizeStart": 35.0, "SizeEnd": 150.0},
                                vectors={"Centre": (0.0, 0.0, 0.0), "Up": (0.0, 0.0, 1.0), "Tint": (0.085, 0.085, 0.09)},
                                depth_fade_cm=35.0)
    flame = make_effect_material("M_JetFlame", "flame-v2", FLAME_CODE, scalars={"Power": 1.0, "Bright": 3.5}, lit=False, on_screen=True)
    return {"motes_mesh": quad_cloud("SM_Motes", 7, 1800, MOTE_REACH_CM, motes), "motes_material": motes,
            "puff_mesh": quad_cloud("SM_Puff", 11, 48, 100.0, puff), "puff_material": puff, "flame_material": flame}


UNDERWATER_CODE = r"""
// Under the sea. Scene: what was drawn, before the camera's own effects (pre-exposed: 0.18 is about mid grey
// on screen). WP: the surface drawn behind this pixel (cm). Cam: the eye. SeaCentre, SeaRadiusCm: the sea now
// (a sphere, raised by the tide). Light: the light falling on the sea, lux (sun and sky by day, moon and sky by
// night). Chop: the moving wave map (height in alpha, third coordinate time), for the caustics.
// The way this pixel looks, from the camera (V points back at it). Where nothing was drawn (open sky) the
// depth is the far plane and the position comes back infinite: worked out from that, the direction was
// not a number, which shows black and poisons the camera's exposure (black underwater, black or white
// flashes on going in and out). So the direction is the camera's own, and the sky is simply far.
float3 dir = -normalize(V);
float dist = length(WP - Cam);
if (!(dist < 1.0e8))
{
    dist = 1.0e8;
}
float3 oc = Cam - SeaCentre.xyz;
float b = dot(oc, dir);
float c = dot(oc, oc) - SeaRadiusCm * SeaRadiusCm;
float tSurf = -b + sqrt(max(b * b - c, 0.0));               // from inside the sphere: the way out
float under = max(SeaRadiusCm - length(oc), 0.0) * 0.01;    // metres of water over the eye
// What is drawn here may be the sea's own surface, seen from below (its waves rise and fall a metre round
// the sphere through the eye): that is the surface too, not something under the water to be lit.
float fromCentre = dist < 1.0e8 ? length(WP - SeaCentre.xyz) : 0.0;
// (Only what is level with the eye or above it: in the shallows the bed itself lies within a metre and a half
// of the surface, and it is the bed, lit, not the surface.)
bool onSurface = abs(fromCentre - SeaRadiusCm) < 150.0 && dot(WP - Cam, normalize(Cam - SeaCentre.xyz)) > -30.0;
bool toSurface = dist > tSurf - 50.0 || onSurface;
if (onSurface) { tSurf = min(dist, tSurf); }
float water = min(dist, tSurf) * 0.01;                      // metres of water this pixel is seen through
// Light in coastal northern water dies red first, then green (absorption and scattering per metre, near
// a coastal Jerlov type): at ten metres red is gone, green a third, blue nearly half.
// Numbers for coastal shelf water (docs/research/ocean_and_underwater.md 3.7): c, how fast an image fades
// (absorption plus scattering), and Kd, how fast daylight is lost going down (over mu = 0.8, the slant of the
// light under the surface). Red is gone by five metres; blue-green fades to dark teal by fifteen or twenty.
float3 sigma = float3(0.60, 0.38, 0.35);
float3 Kd = float3(0.46, 0.19, 0.14) / 0.8;
float3 through = exp(-sigma * water);
// The water's own glow, light scattered toward the eye: a cold teal near the surface going to deep blue
// lower down (gently: the light that reaches ten metres is still most of what the eye adapts to), and by
// night a faint trace of it. Physically decayed with the light, it went black within metres and the
// camera, opening up, burned everything else white.
float3 shallowWater = float3(0.017, 0.073, 0.080);
float3 deepWater = float3(0.0033, 0.017, 0.028);
// It is a real brightness, lit by the light on the sea: the camera's exposure then treats it as it treats
// everything else. (Given as a fixed brightness on screen, the meter could not move it and ran away: black at
// night, the seabed burned white by day, a white flash on going in or coming out.)
float3 glow = lerp(deepWater, shallowWater, exp(-under * 0.07)) * Bright * Light / 3.14159 * View.PreExposure;
float3 colour;
if (toSurface)
{
    // Seen from below, the surface is a mirror of the deep, except for a round window straight up
    // (48.6 degrees each way, Snell's window) through which the whole sky comes, squeezed and bright.
    float3 hit = oc + dir * tSurf;
    float window = smoothstep(0.60, 0.70, dot(dir, normalize(hit)));
    // (Seen on the drawn surface itself, the sky through the window is not in the picture: the water's
    // underside is. The window is then the water's brightest, as it is from below.)
    // The sky through the window, held to about twelve times the water's own brightness: a little dimmer and
    // the eye, adapting to the window, would see the rest of the surface as black, which it never is.
    float skyLum = max(dot(Scene.rgb, float3(0.3, 0.59, 0.11)), 0.0001);
    float glowLum = max(dot(glow, float3(0.3, 0.59, 0.11)), 0.0001);
    float3 above = onSurface ? glow * 3.0 : Scene.rgb * min(0.5, 12.0 * glowLum / skyLum);
    // Outside the window the surface mirrors the water below it: in daylight a bright teal sheet, not black
    // (black above made the camera open up until the bottom burned white).
    colour = lerp(glow * 1.15, above + glow, window);
    colour = colour * through + glow * (1.0 - through);
}
else
{
    // Below, everything is lit through the water over it: the sun's light has crossed that much water
    // on the way down (the scene was lit as if in air).
    float3 upP = normalize(Cam + dir * dist - SeaCentre.xyz);
    float bedDepth = max(SeaRadiusCm - length(Cam + dir * dist - SeaCentre.xyz), 0.0) * 0.01;
    // Caustics: the waves overhead focus the sun into a moving net of light on whatever lies below.
    // Where the surface bulges up it gathers light (a lens), so the net is bright where the wave map's
    // height curves downward: its negative Laplacian, from five readings.
    float3 e1 = normalize(cross(upP, float3(0.31, 0.12, 0.94)));
    float3 e2 = cross(upP, e1);
    float3 pm = (Cam + dir * dist) * 0.01;
    float2 q = float2(dot(pm, e1), dot(pm, e2)) / CausticMetres;
    float when = frac(Time / (WaveLoop * sqrt(CausticMetres / WaveLoopMetres)));
    float hs = 2.0 / 128.0;
    float h0 = Chop.SampleLevel(ChopSampler, float3(q, when), 0).a;
    float lap = Chop.SampleLevel(ChopSampler, float3(q + float2(hs, 0.0), when), 0).a + Chop.SampleLevel(ChopSampler, float3(q - float2(hs, 0.0), when), 0).a
              + Chop.SampleLevel(ChopSampler, float3(q + float2(0.0, hs), when), 0).a + Chop.SampleLevel(ChopSampler, float3(q - float2(0.0, hs), when), 0).a - 4.0 * h0;
    float net = pow(saturate(1.0 - lap * CausticGain), 3.0);
    // Strongest in the shallows under a high sun, gone by twenty metres.
    float sunUp = saturate(dot(upP, normalize(SunDir.xyz)));
    float focus = net * sunUp * Light * exp(-bedDepth * 0.12);
    float3 lit = Scene.rgb * exp(-Kd * 0.5 * bedDepth) * (0.6 + 0.9 * focus);
    colour = lit * through + glow * (1.0 - through);
}
// Nothing out of range may leave here: one infinite or undefined pixel (a light too strong for the picture's
// numbers) poisons the camera's exposure for as long as the eye stays under, and the screen goes black.
if (any(isnan(colour)) || any(isinf(colour)))
{
    colour = glow;
}
return min(colour, 60000.0);
"""


def make_underwater_material():
    """The view from under the sea (AEphemeraSkyRig's post-process while the eye is below the water)."""
    waves = water_maps()
    path = f"{ROOT}/Materials/M_Underwater"
    stamp = stamp_of("underwater-v10", UNDERWATER_CODE, waves["chop_anim"], WAVE_LOOP_S, WAVE_LOOP_METRES)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset("M_Underwater", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("material_domain", unreal.MaterialDomain.MD_POST_PROCESS)
    material.set_editor_property("blendable_location", unreal.BlendableLocation.BL_SCENE_COLOR_BEFORE_DOF)
    row = [0]

    def expr(cls, x=-900):
        row[0] += 130
        return EDIT.create_material_expression(material, cls, x, row[0])

    def scalar(name, value):
        node = expr(unreal.MaterialExpressionScalarParameter)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", value)
        return node

    def vector(name, value):
        node = expr(unreal.MaterialExpressionVectorParameter)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", unreal.LinearColor(*value, 0.0))
        return node

    scene = expr(unreal.MaterialExpressionSceneTexture)
    scene.set_editor_property("scene_texture_id", unreal.SceneTextureId.PPI_POST_PROCESS_INPUT0)
    chop = expr(unreal.MaterialExpressionTextureObject)
    chop.set_editor_property("texture", waves["chop_anim"])
    chop.set_editor_property("sampler_type", unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_COLOR)
    sources = [("Scene", scene, "Color"), ("WP", expr(unreal.MaterialExpressionWorldPosition), ""),
               ("Cam", expr(unreal.MaterialExpressionCameraPositionWS), ""), ("Time", expr(unreal.MaterialExpressionTime), ""),
               ("V", expr(unreal.MaterialExpressionCameraVectorWS), ""),
               ("SeaCentre", vector("SeaCentre", (0.0, 0.0, 0.0)), ""), ("SeaRadiusCm", scalar("SeaRadiusCm", 500000.0), ""),
               ("SunDir", vector("SunDir", (0.0, 0.0, 1.0)), ""), ("Light", scalar("Light", 100000.0), ""), ("Bright", scalar("Bright", 1.0), ""),
               ("Chop", chop, ""), ("WaveLoop", scalar("WaveLoop", WAVE_LOOP_S), ""), ("WaveLoopMetres", scalar("WaveLoopMetres", WAVE_LOOP_METRES), ""),
               ("CausticMetres", scalar("CausticMetres", 3.5), ""), ("CausticGain", scalar("CausticGain", 9.0), "")]
    custom = expr(unreal.MaterialExpressionCustom, -300)
    inputs = []
    for name, _, _ in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", UNDERWATER_CODE)
    for name, node, pin in sources:
        connect(node, pin, custom, name)
    connect_property(custom, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish_material(material, stamp, "M_Underwater")


def make_underlay_material(colour):
    """Plain ground-coloured rock for the sphere that sits just under a dry world's terrain."""
    path = f"{ROOT}/Materials/M_Underlay"
    stamp = stamp_of("underlay-v1", colour)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset("M_Underlay", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    node = EDIT.create_material_expression(material, unreal.MaterialExpressionConstant3Vector, -300, 0)
    node.set_editor_property("constant", unreal.LinearColor(*colour, 1.0))
    connect_property(node, "", unreal.MaterialProperty.MP_BASE_COLOR)
    rough = EDIT.create_material_expression(material, unreal.MaterialExpressionConstant, -300, 200)
    rough.set_editor_property("r", 0.9)
    connect_property(rough, "", unreal.MaterialProperty.MP_ROUGHNESS)
    EDIT.set_base_material_usage(material, unreal.MaterialUsage.MATUSAGE_NANITE, True)
    return finish_material(material, stamp, "M_Underlay")


# ----------------------------------------------------------------------------------- foliage ---

def foliage_parent():
    """Leaves, blades and twigs: alpha-tested, two-sided, lit from behind as real leaves are."""
    path = f"{ROOT}/Materials/M_Foliage"
    stamp = stamp_of("foliage-v4")
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset("M_Foliage", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    material.set_editor_property("two_sided", True)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    material.set_editor_property("opacity_mask_clip_value", 0.3)

    def texture(name, sampler, y):
        node = EDIT.create_material_expression(material, unreal.MaterialExpressionTextureSampleParameter2D, -700, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("sampler_type", sampler)
        return node

    def texture_default(node, path, sampler):
        # The default texture must match the sampler type or the material will not compile.
        default = unreal.load_asset(path)
        if not default:
            raise RuntimeError(f"engine texture missing: {path}")
        node.set_editor_property("texture", default)
        node.set_editor_property("sampler_type", sampler)

    colour = texture("BaseColor", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 0)
    normal = texture("Normal", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 260)
    texture_default(normal, "/Engine/EngineMaterials/DefaultNormal.DefaultNormal", unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    arm = texture("ARM", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 520)
    texture_default(arm, "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    # Root-to-tip shading: plants are dark where they meet the ground and each other, bright at
    # the tips. Height is measured in each instance's own space (plants are instanced; "local"
    # space would be the whole planet's), from the mesh's lowest point, set per material instance.
    world = EDIT.create_material_expression(material, unreal.MaterialExpressionWorldPosition, -1500, 1100)
    local = EDIT.create_material_expression(material, unreal.MaterialExpressionTransformPosition, -1300, 1100)
    local.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    local.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_INSTANCE)
    connect(world, "", local, "")
    shade = EDIT.create_material_expression(material, unreal.MaterialExpressionCustom, -1000, 1100)
    shade_inputs = []
    for input_name in ("P", "RootZ", "RootShade", "ShadeHeight"):
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        shade_inputs.append(entry)
    shade.set_editor_property("inputs", shade_inputs)
    shade.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    shade.set_editor_property("code", "float h = saturate((P.z - RootZ) / max(ShadeHeight, 1.0)); return lerp(RootShade, 1.0, smoothstep(0.0, 1.0, h));")
    root_z = EDIT.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1300, 1350)
    root_z.set_editor_property("parameter_name", "RootZCm")
    root_z.set_editor_property("default_value", 0.0)
    root_shade = EDIT.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1300, 1500)
    root_shade.set_editor_property("parameter_name", "RootShade")
    root_shade.set_editor_property("default_value", 0.3)
    shade_height = EDIT.create_material_expression(material, unreal.MaterialExpressionScalarParameter, -1300, 1650)
    shade_height.set_editor_property("parameter_name", "ShadeHeightCm")
    # Trees shade over their bottom two metres; a small plant shades over all of it.
    shade_height.set_editor_property("default_value", 200.0)
    connect(local, "", shade, "P")
    connect(root_z, "", shade, "RootZ")
    connect(root_shade, "", shade, "RootShade")
    connect(shade_height, "", shade, "ShadeHeight")
    shaded_colour = EDIT.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 0)
    connect(colour, "RGB", shaded_colour, "A")
    connect(shade, "", shaded_colour, "B")
    connect_property(shaded_colour, "", unreal.MaterialProperty.MP_BASE_COLOR)
    alpha = texture("Alpha", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, 1040)
    texture_default(alpha, "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture", unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    connect_property(alpha, "R", unreal.MaterialProperty.MP_OPACITY_MASK)
    shaded_ao = EDIT.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 520)
    connect(arm, "R", shaded_ao, "A")
    connect(shade, "", shaded_ao, "B")
    connect_property(shaded_ao, "", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    connect_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)
    connect_property(arm, "G", unreal.MaterialProperty.MP_ROUGHNESS)
    # Light through the leaf: the leaf's own colour, a little yellower and dimmer.
    through = EDIT.create_material_expression(material, unreal.MaterialExpressionVectorParameter, -700, 780)
    through.set_editor_property("parameter_name", "Transmission")
    # About a third of the light gets through a leaf. More than that lights the shadow side as
    # brightly as the sunlit side and the plant goes flat.
    through.set_editor_property("default_value", unreal.LinearColor(0.34, 0.40, 0.14, 1.0))
    subsurface = EDIT.create_material_expression(material, unreal.MaterialExpressionMultiply, -400, 780)
    connect(shaded_colour, "", subsurface, "A")
    connect(through, "", subsurface, "B")
    connect_property(subsurface, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    for usage in (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, unreal.MaterialUsage.MATUSAGE_NANITE):
        EDIT.set_base_material_usage(material, usage, True)
    return finish_material(material, stamp, "M_Foliage")


FOLIAGE_PARENT = None
STARS = None
MIST = None
EFFECTS = {}


def import_alpha_maps(folder, destination):
    """Imports the scan's opacity maps (fetched by fetch_polyhaven_alpha.py) beside its other textures."""
    tasks = []
    for source in sorted((folder / "textures").glob("*_alpha_*")):
        existing = ASSETS.load_asset(f"{destination}/{source.stem}") if ASSETS.does_asset_exist(f"{destination}/{source.stem}") else None
        if existing:
            # Thin needles and blades average to grey in the smaller mips and fall under the cut-out
            # threshold, so distant trees go bare. Keep each mip's coverage equal to the full-size map's.
            if existing.get_editor_property("alpha_coverage_thresholds").x < 0.01:
                existing.set_editor_property("do_scale_mips_for_alpha_coverage", True)
                existing.set_editor_property("alpha_coverage_thresholds", unreal.Vector4(0.3, 0.0, 0.0, 0.0))
                existing.set_editor_property("srgb", False)
                ASSETS.save_loaded_asset(existing)
            continue
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source))
        task.set_editor_property("destination_path", destination)
        task.set_editor_property("destination_name", source.stem)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        tasks.append(task)
    if tasks:
        TOOLS.import_asset_tasks(tasks)
        import_alpha_maps(folder, destination)      # second pass applies the coverage setting


def fix_foliage_materials(mesh, destination, every_slot=False):
    """Gives leaf, blade and twig slots the foliage material, with the scan's opacity map as cut-out.

    Scans mark leaves as alpha-blended (which Nanite cannot draw) and ship their colour as JPEG
    with the cut-out in a separate map. every_slot: for small plants that are leaf all over,
    convert every slot, not only the blended ones.
    """
    textures = [ASSETS.load_asset(path) for path in ASSETS.list_assets(destination, recursive=True)]
    textures = [t for t in textures if isinstance(t, unreal.Texture2D)]

    def find(marker, stem):
        # "fir_tree_01_twig" has its own maps; "searsia_lucida_leaves" shares "searsia_lucida_*".
        parts = stem.split("_")
        while parts:
            key = "_".join(parts)
            matches = [t for t in textures if marker in t.get_name() and t.get_name().startswith(key)]
            if matches:
                return matches[0]
            parts.pop()
        return None

    slots = mesh.get_editor_property("static_materials")
    changed = False
    for index, slot in enumerate(slots):
        current = slot.get_editor_property("material_interface")
        if not current:
            continue
        already = current.get_name().endswith("_Foliage")
        stem = current.get_name()
        if already:
            stem = stem[len("MI_"):-len("_Foliage")]
        else:
            for prefix in ("MI_", "M_"):
                stem = stem[len(prefix):] if stem.startswith(prefix) else stem
            blended = current.get_blend_mode() in (unreal.BlendMode.BLEND_TRANSLUCENT, unreal.BlendMode.BLEND_ADDITIVE)
            if not (blended or every_slot):
                continue
        colour, normal, arm, alpha = find("_diff", stem), find("_nor", stem), find("_arm", stem), find("_alpha", stem)
        if not colour:
            log(f"foliage {mesh.get_name()}: no colour map for {stem}, left as is")
            continue
        name = f"MI_{stem}_Foliage"
        path = f"{destination}/{name}"
        instance = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else TOOLS.create_asset(
            name, destination, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        EDIT.set_material_instance_parent(instance, FOLIAGE_PARENT)
        EDIT.set_material_instance_texture_parameter_value(instance, "BaseColor", colour)
        if normal:
            EDIT.set_material_instance_texture_parameter_value(instance, "Normal", normal)
        if arm:
            EDIT.set_material_instance_texture_parameter_value(instance, "ARM", arm)
        if alpha:
            EDIT.set_material_instance_texture_parameter_value(instance, "Alpha", alpha)
        # Short plants shade from root to tip over their whole height; trees over their bottom 2 m.
        box = mesh.get_bounding_box()
        EDIT.set_material_instance_scalar_parameter_value(instance, "ShadeHeightCm", min(max(box.max.z - box.min.z, 5.0), 200.0))
        EDIT.set_material_instance_scalar_parameter_value(instance, "RootZCm", box.min.z)
        EDIT.update_material_instance(instance)
        ASSETS.save_loaded_asset(instance)
        if not already:
            mesh.set_material(index, instance)
            changed = True
    if changed:
        ASSETS.save_loaded_asset(mesh)
    return changed


# -------------------------------------------------------------------------------------- grass ---

GRASS_CODE = r"""
// VC.r: height along the blade (0 root, 1 tip). VC.g: per-blade variation. Rand: per-clump random.
// P: planet-local position (cm). Colour varies in broad drifts across the land (metres wide, and
// again tens of metres wide), with only a trace per clump and per blade: per-blade contrast is
// what makes a meadow look like noise.
float t = VC.r;
#define H(c) frac(sin(dot(c, float3(127.1, 311.7, 74.7))) * 43758.5453)
struct FDrift
{
    float At(float3 q)
    {
        float3 c = floor(q);
        float3 f = frac(q);
        f = f * f * (3.0 - 2.0 * f);
        return lerp(lerp(lerp(H(c), H(c + float3(1, 0, 0)), f.x), lerp(H(c + float3(0, 1, 0)), H(c + float3(1, 1, 0)), f.x), f.y),
                    lerp(lerp(H(c + float3(0, 0, 1)), H(c + float3(1, 0, 1)), f.x), lerp(H(c + float3(0, 1, 1)), H(c + float3(1, 1, 1)), f.x), f.y), f.z);
    }
};
FDrift Drift;
float drift = Drift.At(P / 420.0);
float broad = Drift.At(P / 3300.0 + 19.0);
float3 col = lerp(Root, Tip, smoothstep(0.0, 0.85, t));
float dry = saturate((drift - 0.5) * 1.5 + (broad - 0.5) * 1.1 + (Rand - 0.5) * 0.25 + (VC.g - 0.5) * 0.12 + DryBias);
col = lerp(col, DryTip * lerp(0.62, 1.0, t), dry * smoothstep(0.2, 1.0, t));
col *= lerp(0.90, 1.07, drift) * lerp(0.80, 1.12, broad) * lerp(0.95, 1.04, Rand);
// Blades crowd together at the root and shade one another there. Most of that is left to the
// light (Shade, the occlusion); only a little is in the colour, or every blade stands as a pale
// line over a black gap and the sward reads as wire.
Shade = lerp(0.50, 1.0, smoothstep(0.0, 0.6, t));
// Matt, and more so low down, where a blade is dull and dusty.
Rough = lerp(0.93, 0.80, t);
return col * lerp(0.74, 1.0, smoothstep(0.0, 0.6, t));
"""


GRASS_WIND_CODE = r"""
// How far a point of a blade is carried by the wind (cm, in the planet's own space). VC.r: height
// along the blade (0 root, 1 tip). VC.g: the blade's own random. P: planet-local position (cm).
// Rand: per-clump random. Far: how far this point is from the eye (cm).
// The wind comes over the land in long waves (a gust every nine metres or so, in sets), every
// clump answers a little in its own time, and a blade bends from the root: its foot stays where
// it grows and its tip goes furthest, downwind and a little down. Each blade of a tuft has its own
// stiffness and flutter: moved as one, a tuft is a thing sliding about on the ground, not grass.
// The wind dies away smoothly before 36 m (past 40 m the engine stops moving the grass at all, and
// a line where it stops was a ring of still grass going along with you).
float t = VC.r;
float blade = VC.g;
float3 up = normalize(P);
float3 across = normalize(cross(up, float3(0.31, 0.12, 0.94)));
float3 along = cross(across, up);
float3 W = normalize(across * 0.85 + along * 0.52);
float run = dot(P, W) * 0.01;
float side = dot(P, across) * 0.01;
float gust = 0.5 + 0.5 * sin(run / 9.0 - Time * 1.1 + sin(side / 14.0) * 2.0);
gust *= 0.6 + 0.4 * sin(run / 31.0 - Time * 0.45 + 1.3);
// One flutter to a tuft, with a small lag from blade to blade: the tuft moves as a body with ripples
// through it (a frequency of its own for every blade made neighbours drift in and out of step: it boiled).
float flutter = sin(Time * (3.1 + Rand * 0.8) + blade * 0.9 + Rand * 17.0) * 0.20;
float give = 0.65 + 0.7 * blade;
float lean = (0.10 + gust * (1.0 + flutter)) * give * Wind * (1.0 - smoothstep(2200.0, 3600.0, Far));
// A blade bends: it turns about its root and keeps its length (pushed sideways instead, by an amount
// growing with height, it sheared and stretched, and did not look rooted). Its tip turns through
// lean x Bend radians; the stem's lowest quarter is stiff. L: the blade's length, from how high this
// point stands in the clump's own space for how far along the blade it is.
float L = Local.z / max(t, 0.15);
float a = max(lean * Bend * smoothstep(0.0, 0.3, t), 0.001);
float sway = (1.0 - cos(a * t)) / a;
float sink = sin(a * t) / a - t;
return (W * sway + up * sink) * L;
"""


def make_grass_material():
    path = f"{ROOT}/Materials/M_Grass"
    stamp = stamp_of("grass-v8", GRASS_CODE, GRASS_WIND_CODE)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset("M_Grass", f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("two_sided", True)
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_TWO_SIDED_FOLIAGE)
    # The normal is given in world space (see below): the engine then leaves it alone on back faces.
    material.set_editor_property("tangent_space_normal", False)

    def expr(cls, x, y):
        return EDIT.create_material_expression(material, cls, x, y)

    def colour_param(name, value, y):
        node = expr(unreal.MaterialExpressionVectorParameter, -900, y)
        node.set_editor_property("parameter_name", name)
        node.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
        return node

    vertex = expr(unreal.MaterialExpressionVertexColor, -900, 0)
    # Deep green at the root, green tips, dull straw when dry. No blade is lighter than the turf it
    # grows from (the ground's own moss colour): a blade paler than its ground is a bright line, and
    # ten thousand of them are a glare.
    root = colour_param("Root", (0.026, 0.042, 0.014), 200)
    tip = colour_param("Tip", (0.044, 0.072, 0.025), 380)
    dry_tip = colour_param("DryTip", (0.090, 0.083, 0.040), 560)
    dry_bias = expr(unreal.MaterialExpressionScalarParameter, -900, 740)
    dry_bias.set_editor_property("parameter_name", "DryBias")
    dry_bias.set_editor_property("default_value", 0.22)
    random = expr(unreal.MaterialExpressionPerInstanceRandom, -900, 880)
    custom = expr(unreal.MaterialExpressionCustom, -400, 0)
    inputs = []
    world = expr(unreal.MaterialExpressionWorldPosition, -1300, 1100)
    planet = expr(unreal.MaterialExpressionTransformPosition, -1100, 1100)
    planet.set_editor_property("transform_source_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD)
    planet.set_editor_property("transform_type", unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, "", planet, "")
    for name in ("VC", "Root", "Tip", "DryTip", "DryBias", "Rand", "P"):
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        inputs.append(entry)
    outputs = []
    for output_name in ("Shade", "Rough"):
        output = unreal.CustomOutput()
        output.set_editor_property("output_name", output_name)
        output.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        outputs.append(output)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", GRASS_CODE)
    for name, node in (("VC", vertex), ("Root", root), ("Tip", tip), ("DryTip", dry_tip), ("DryBias", dry_bias), ("Rand", random), ("P", planet)):
        connect(node, "", custom, name)
    connect_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(custom, "Shade", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION)
    # A third of the light passes through a blade, tinted by it.
    through = expr(unreal.MaterialExpressionMultiply, -100, 300)
    through_tint = colour_param("Transmission", (0.38, 0.42, 0.12), 1020)
    connect(custom, "", through, "A")
    connect(through_tint, "", through, "B")
    connect_property(through, "", unreal.MaterialProperty.MP_SUBSURFACE_COLOR)
    connect_property(custom, "Rough", unreal.MaterialProperty.MP_ROUGHNESS)
    # Blades mirror little, and least where they stand in each other's shade.
    specular = expr(unreal.MaterialExpressionMultiply, -100, 580)
    specular.set_editor_property("const_b", 0.20)
    connect(custom, "Shade", specular, "A")
    connect_property(specular, "", unreal.MaterialProperty.MP_SPECULAR)
    # A blade is a flat ribbon: lit by its own normal, each one catches the light or misses it, and
    # a meadow glitters. Real grass is seen as a surface, the sward: so each blade is lit mostly as
    # the ground under it would be (straight up from the planet), keeping a third of its own lean.
    # The blade's own part turns to face whoever is looking at its back.
    own = expr(unreal.MaterialExpressionVertexNormalWS, -700, 1300)
    facing = expr(unreal.MaterialExpressionTwoSidedSign, -700, 1400)
    own_facing = expr(unreal.MaterialExpressionMultiply, -500, 1300)
    connect(own, "", own_facing, "A")
    connect(facing, "", own_facing, "B")
    up_local = expr(unreal.MaterialExpressionNormalize, -900, 1500)
    connect(planet, "", up_local, "")
    up_world = expr(unreal.MaterialExpressionTransform, -700, 1500)
    up_world.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    up_world.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(up_local, "", up_world, "")
    sward = expr(unreal.MaterialExpressionLinearInterpolate, -300, 1400)
    sward.set_editor_property("const_alpha", 0.66)
    connect(own_facing, "", sward, "A")
    connect(up_world, "", sward, "B")
    lit_as = expr(unreal.MaterialExpressionNormalize, -100, 1400)
    connect(sward, "", lit_as, "")
    connect_property(lit_as, "", unreal.MaterialProperty.MP_NORMAL)
    # The wind. (Nothing that grows had moved until now, and a still meadow is a photograph of one.)
    wind = expr(unreal.MaterialExpressionCustom, -400, 1700)
    strength = expr(unreal.MaterialExpressionScalarParameter, -900, 1800)
    strength.set_editor_property("parameter_name", "Wind")
    strength.set_editor_property("default_value", 1.0)
    eye_distance = expr(unreal.MaterialExpressionDistance, -700, 2000)
    connect(expr(unreal.MaterialExpressionWorldPosition, -900, 2000), "", eye_distance, "A")
    connect(expr(unreal.MaterialExpressionCameraPositionWS, -900, 2100), "", eye_distance, "B")
    bend = expr(unreal.MaterialExpressionScalarParameter, -900, 2200)
    bend.set_editor_property("parameter_name", "Bend")
    bend.set_editor_property("default_value", 0.5)
    wind_sources = [("VC", vertex), ("P", planet), ("Rand", random), ("Time", expr(unreal.MaterialExpressionTime, -900, 1900)), ("Wind", strength),
                    ("Far", eye_distance), ("Local", expr(unreal.MaterialExpressionPreSkinnedPosition, -900, 2300)), ("Bend", bend)]
    wind_inputs = []
    for name, _ in wind_sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", name)
        wind_inputs.append(entry)
    wind.set_editor_property("inputs", wind_inputs)
    wind.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    wind.set_editor_property("code", GRASS_WIND_CODE)
    for name, node in wind_sources:
        connect(node, "", wind, name)
    carried = expr(unreal.MaterialExpressionTransform, -100, 1700)
    carried.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_LOCAL)
    carried.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_WORLD)
    connect(wind, "", carried, "")
    connect_property(carried, "", unreal.MaterialProperty.MP_WORLD_POSITION_OFFSET)
    # The furthest a point is ever carried (cm): the engine needs it to keep moving grass inside what it draws.
    material.set_editor_property("max_world_position_offset_displacement", 40.0)
    for usage in (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, unreal.MaterialUsage.MATUSAGE_NANITE):
        EDIT.set_base_material_usage(material, usage, True)
    return finish_material(material, stamp, "M_Grass")


def grass_clumps(material):
    """Real-geometry grass in three habits. Returns {name: [meshes]}."""
    folder = f"{ROOT}/Grass"
    habits = {
        # name: (variants, blades, height cm, spread cm, blade width cm, droop)
        # (Blades a little wider than life: a blade thinner than a pixel flickers in and out, and
        # a sward of them reads as sparse wire.)
        "turf": (3, 90, 17.0, 32.0, 0.85, 0.25),
        "meadow": (3, 50, 36.0, 34.0, 0.90, 0.45),
        "tall": (2, 32, 64.0, 26.0, 1.00, 0.60),
    }
    result = {}
    for name, (variants, blades, height, spread, width, droop) in habits.items():
        meshes = []
        for variant in range(variants):
            mesh = unreal.EphemeraPlanetBuilder.build_grass_clump(folder, f"SM_Grass_{name}_{variant}", 100 + variant * 17 + len(name),
                                                                  blades, height, spread, width, droop)
            if not mesh:
                raise RuntimeError(f"grass clump {name} {variant} failed")
            mesh.set_material(0, material)
            ASSETS.save_loaded_asset(mesh)
            meshes.append(mesh)
        result[name] = meshes
    log("grass clumps built")
    return result


# ------------------------------------------------------------------------------------ models ---

MODELS = PROJECT / "SourceAssets/polyhaven/models"


def import_scan(relative, foliage=False, collision=False, all_leaf=False):
    """Imports a scanned glTF as Nanite static meshes. Returns the meshes it contains, by name."""
    folder = MODELS / relative
    name = folder.name
    destination = f"{ROOT}/Scans/{name}"

    def meshes():
        found = [ASSETS.load_asset(path) for path in ASSETS.list_assets(destination, recursive=True)]
        return sorted((a for a in found if isinstance(a, unreal.StaticMesh)), key=lambda a: a.get_name())

    existing = meshes() if ASSETS.does_directory_exist(destination) else []
    if not existing:
        source = sorted(folder.glob("*.gltf"))
        if not source:
            raise RuntimeError(f"{relative}: no .gltf")
        pipeline = ASSETS.load_asset("/Interchange/Pipelines/DefaultGLTFAssetsPipeline")
        mesh_pipeline = pipeline.get_editor_property("mesh_pipeline")
        mesh_pipeline.set_editor_property("build_nanite", True)
        mesh_pipeline.set_editor_property("collision", False)
        materials = pipeline.get_editor_property("material_pipeline")
        materials.set_editor_property("import_materials", True)
        materials.get_editor_property("texture_pipeline").set_editor_property("import_textures", True)
        options = unreal.InterchangePipelineStackOverride()
        options.set_editor_property("override_pipelines", [
            unreal.SoftObjectPath(pipeline.get_path_name()),
            unreal.SoftObjectPath("/Interchange/Pipelines/DefaultGLTFPipeline.DefaultGLTFPipeline"),
        ])
        task = unreal.AssetImportTask()
        task.set_editor_property("filename", str(source[0]))
        task.set_editor_property("destination_path", destination)
        task.set_editor_property("automated", True)
        task.set_editor_property("replace_existing", True)
        task.set_editor_property("save", True)
        task.set_editor_property("async_", False)
        task.set_editor_property("options", options)
        TOOLS.import_asset_tasks([task])
        existing = meshes()
        if not existing:
            raise RuntimeError(f"{relative}: import produced no static mesh")
        for mesh in existing:
            settings = mesh.get_editor_property("nanite_settings")
            settings.set_editor_property("enabled", True)
            if foliage:
                # Thin leaves and blades vanish when Nanite simplifies them unless area is preserved.
                settings.set_editor_property("shape_preservation", unreal.NaniteShapePreservation.PRESERVE_AREA)
            mesh.set_editor_property("nanite_settings", settings)
            if collision:
                setup = mesh.get_editor_property("body_setup")
                setup.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            for slot in mesh.get_editor_property("static_materials"):
                material = slot.get_editor_property("material_interface")
                if material:
                    for usage in (unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES, unreal.MaterialUsage.MATUSAGE_NANITE):
                        if isinstance(material, unreal.MaterialInstanceConstant):
                            EDIT.set_material_usage_override(material, usage, True, True)
                        elif isinstance(material, unreal.Material):
                            EDIT.set_base_material_usage(material, usage, True)
                    ASSETS.save_loaded_asset(material)
            ASSETS.save_loaded_asset(mesh)
    if foliage:
        import_alpha_maps(folder, destination)
        fixed = sum(1 for mesh in existing if fix_foliage_materials(mesh, destination, all_leaf))
        if fixed:
            log(f"foliage {name}: {fixed} meshes given the leaf material")
    for mesh in existing:
        box = mesh.get_bounding_box()
        size = box.max - box.min
        log(f"scan {name}/{mesh.get_name()}: {size.x / 100:.2f} x {size.y / 100:.2f} x {size.z / 100:.2f} m")
    return existing


def scatter_rule(meshes, per_hectare, **values):
    rule = unreal.EphemeraScatterRule()
    rule.set_editor_property("meshes", meshes)
    rule.set_editor_property("density_per_hectare", per_hectare)
    for key, value in values.items():
        rule.set_editor_property(key, value)
    return rule


def cover_rules(meshes, per_m2, **values):
    """One ground-cover rule per mesh, sharing the density between them."""
    rules = []
    for mesh in meshes:
        rule = unreal.EphemeraGroundCoverRule()
        rule.set_editor_property("mesh", mesh)
        rule.set_editor_property("density_per_m2", per_m2 / len(meshes))
        for key, value in values.items():
            rule.set_editor_property(key, value)
        rules.append(rule)
    return rules


AURORA_CODE = r"""
// The aurora: curtains of light marched through, not painted on a shell (Lawlor and Genetti, "Interactive
// Volume Rendering Aurora on the GPU", 2011; docs/research/aurora.md). Real aurora is thin folded sheets of
// light standing along an oval round the magnetic pole, never spokes from it: the sheets are where the
// electrons come down, so each is a curve on the ground drawn upward along the field. The light of a sheet
// is the 557.7 nm oxygen green from a sharp foot to a long fading top, with the 630 nm red, broad and dim,
// above it; the magenta lower fringe is rare and faint. Seen edge-on a sheet gathers a long path of light,
// which is why from orbit aurora are bright thin lines near the limb and faint ribbons looking straight down.
//
// Inputs (metres, the planet's own frame, its centre at the origin): Ro the camera; Rd the view ray through
// this pixel; Frag how far along it this pixel of the bounding sphere lies; Sun the direction to the star;
// Planet, Base, Top: radius of the ground and the curtains' foot and top heights; Depth: scene depth (cm)
// along the view axis and CamFwd: the ray's cosine to that axis. Strength: on-screen brightness.
float3 rd = normalize(Rd);
float3 sun = normalize(Sun);
float rTop = Planet + Top;
float b = dot(Ro, rd);
float c0 = dot(Ro, Ro);
// Only the bounding sphere's near face draws when the camera is outside it (its far face would add the
// same light again).
if (c0 > Container * Container && Frag > -b) return 0;
float discT = b * b - (c0 - rTop * rTop);
if (discT <= 0.0) return 0;
float sq = sqrt(discT);
float t0 = max(-b - sq, 0.0);
float t1 = -b + sq;
// Stop at the ground (the planet's sphere) and at whatever stands in front (terrain, the moon).
float discP = b * b - (c0 - Planet * Planet);
if (discP > 0.0 && -b - sqrt(discP) > 0.0) t1 = min(t1, -b - sqrt(discP));
t1 = min(t1, Depth * 0.01 / max(CamFwd, 0.05));
if (t1 <= t0) return 0;

// The magnetic frame: the dipole axis a little off the spin axis; longitude counted from local midnight.
float3 m = normalize(float3(0.18, -0.12, 1.0));
float3 e = normalize(cross(m, float3(1.0, 0.0, 0.0)));
float3 f = cross(m, e);
float midnight = atan2(dot(-sun, f), dot(-sun, e));

// Smooth value noise (no sines: sums of sines are what make evenly spaced stripes).
#define HASH2(HPOS) frac(sin(dot(HPOS, float2(127.1, 311.7))) * 43758.5453)
#define VNOISE(NPOS, NOUT) { float2 ip = floor(NPOS); float2 fp = frac(NPOS); float2 fw = fp * fp * (3.0 - 2.0 * fp); \
    NOUT = lerp(lerp(HASH2(ip), HASH2(ip + float2(1, 0)), fw.x), lerp(HASH2(ip + float2(0, 1)), HASH2(ip + float2(1, 1)), fw.x), fw.y) * 2.0 - 1.0; }

float H = Top - Base;
// A sheet's own thickness and the pixel's footprint at a distance: a sheet is never drawn thinner than a
// pixel (its light spread wider keeps the same total), which is what keeps it from shimmering.
float sigPhys = 3.0;
float pixAngle = 2.0 * View.ClipToView[1][1] * View.ViewSizeAndInvSize.w;
// Start each pixel's march at a different point (interleaved gradient noise, changing every frame); the
// temporal upscaler averages the steps into a smooth result.
float2 sp = Parameters.SvPosition.xy + 5.588238 * float(View.StateFrameIndexMod8);
float jitter = frac(52.9829189 * frac(dot(sp, float2(0.06711056, 0.00583715))));

// 557.7 nm lies outside every display's colours; this is how it photographs: an emerald green, a little
// toward blue, which turns paler where it is bright (the tone curve).
float3 green = float3(0.30, 1.0, 0.45);
float3 red = float3(1.0, 0.10, 0.22);            // 630.0 nm, as it photographs: crimson
float3 pink = float3(1.0, 0.18, 0.55);           // N2 first-positive red bands of the rare lower border
float3 light = 0;
float s = t0;
float stepLast = 0.0;
[loop] for (int i = 0; i < 128; ++i)
{
    if (s >= t1) break;
    float3 p = Ro + rd * s;
    float r = length(p);
    float h = r - Planet;
    float3 n = p / r;
    float th = acos(clamp(dot(n, m), -1.0, 1.0));
    float ph = atan2(dot(n, f), dot(n, e));
    float2 ring = float2(cos(ph), sin(ph));
    float arcR = Planet * sin(th);
    // The oval: about 19 degrees from the pole, wider on the night side.
    float oval = radians(19.0) + radians(4.0) * cos(ph - midnight);
    // Three arcs, each folded by noise of a few sizes along it (spirals, folds), drifting slowly round.
    float dMin = 1e9;
    float arcNear = 0;
    float rays = 0;
    float gap = 0;
    [unroll] for (int k = 0; k < 3; ++k)
    {
        float kf = float(k);
        float2 q = ring * (3.0 + kf * 1.7) + float2(Time * 0.004 * (1.0 + kf * 0.3), kf * 13.1);
        float n1, n2, n3;
        VNOISE(q, n1);
        VNOISE(q * 3.1 + float2(kf * 7.3, Time * 0.011), n2);
        VNOISE(q * 9.7 + float2(Time * 0.02, kf * 3.9), n3);
        float curve = oval + radians(-1.4 + 1.6 * kf) + radians(1.2) * n1 + radians(0.35) * n2 + radians(0.09) * n3;
        float d = abs(th - curve) * Planet;
        if (d < dMin)
        {
            dMin = d;
            arcNear = kf;
        }
    }
    // Rays: fine vertical streaks, set by the place along the arc only, so they stand up the sheet.
    float2 qr = ring * (arcR / 9.0) + float2(arcNear * 31.0, Time * 0.05);
    float nr1, nr2;
    VNOISE(qr, nr1);
    VNOISE(qr * 0.37 + float2(5.1, Time * 0.02), nr2);
    rays = pow(saturate(1.0 - abs(nr1)), 3.0) * 0.75 + 0.25 + 0.3 * nr2;
    // Breaks along the arc: no curtain runs all the way round lit.
    float2 qg = ring * 2.2 + float2(arcNear * 4.3, Time * 0.003);
    float ng;
    VNOISE(qg, ng);
    gap = smoothstep(-0.35, 0.25, ng);

    float footprint = s * pixAngle;
    float sig = max(sigPhys, 0.7 * footprint);
    float sheet = (sigPhys / sig) * exp(-0.5 * (dMin / sig) * (dMin / sig));
    float sigRed = sig * 6.0;
    float sheetRed = (sigPhys / sigRed) * exp(-0.5 * (dMin / sigRed) * (dMin / sigRed));

    // The light up the sheet: a sharp foot, a long fading top whose height varies from ray to ray; red above.
    float hz = h - Base;
    float topHere = H * (0.45 + 0.35 * rays);
    float g = smoothstep(0.0, 0.04 * H, hz) * exp(-max(hz - 0.06 * H, 0.0) / (0.18 * H)) * (1.0 - smoothstep(topHere * 0.8, topHere, hz));
    float rr = exp(-pow((hz - 0.55 * H) / (0.25 * H), 2.0)) * 0.22;
    float bottom = saturate(1.0 - hz / (0.03 * H)) * step(0.0, hz) * 0.08;
    // Only where it is night: in daylight the sky outshines it.
    float night = smoothstep(0.12, -0.18, dot(n, sun));
    float lit = gap * night;
    float3 here = (sheet * rays * (g * green + bottom * pink) + sheetRed * (0.6 + 0.4 * rays) * rr * red) * lit;
    // March by the distance to the nearest sheet (sphere tracing), never further than its own width.
    float stepLen = clamp(0.6 * (dMin - 2.5 * sig), 0.8 * sig, max(H * 0.15, 0.8 * sig));
    if (i == 0) stepLen *= jitter + 0.05;
    light += here * stepLen;
    s += stepLen;
}
return light * Strength;
"""


def make_aurora_material():
    name = "M_Aurora"
    path = f"{ROOT}/Materials/{name}"
    stamp = stamp_of("aurora-v3", AURORA_CODE)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset(name, f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
    material.set_editor_property("two_sided", True)

    def expr(cls, x=-900, y=0):
        return EDIT.create_material_expression(material, cls, x, y)

    def primitive(parameter, index, value):
        node = expr(unreal.MaterialExpressionScalarParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", value)
        node.set_editor_property("use_custom_primitive_data", True)
        node.set_editor_property("primitive_data_index", index)
        return node

    def to_local(vector_node):
        node = expr(unreal.MaterialExpressionTransform)
        node.set_editor_property("transform_source_type", unreal.MaterialVectorCoordTransformSource.TRANSFORMSOURCE_WORLD)
        node.set_editor_property("transform_type", unreal.MaterialVectorCoordTransform.TRANSFORM_LOCAL)
        connect(vector_node, "", node, "")
        return node

    # The bounding sphere is the engine's 50 cm sphere scaled to Container metres (EphemeraBody.cpp), so one
    # unit of its local space is Container / 50 metres, and its local axes are the planet's own.
    container = primitive("Container", 0, 4000.0)
    planet = primitive("Planet", 1, 5000.0)
    base = primitive("Base", 2, 800.0)
    top = primitive("Top", 3, 3200.0)
    metres = expr(unreal.MaterialExpressionDivide)
    connect(container, "", metres, "A")
    metres.set_editor_property("const_b", 50.0)
    world = expr(unreal.MaterialExpressionWorldPosition)
    camera = expr(unreal.MaterialExpressionCameraPositionWS)
    centre = expr(unreal.MaterialExpressionObjectPositionWS)
    ray = expr(unreal.MaterialExpressionSubtract)
    connect(world, "", ray, "A")
    connect(camera, "", ray, "B")
    from_centre = expr(unreal.MaterialExpressionSubtract)
    connect(camera, "", from_centre, "A")
    connect(centre, "", from_centre, "B")
    ray_local = to_local(ray)
    cam_local = to_local(from_centre)
    cam_m = expr(unreal.MaterialExpressionMultiply)
    connect(cam_local, "", cam_m, "A")
    connect(metres, "", cam_m, "B")
    frag_len = expr(unreal.MaterialExpressionLength)
    connect(ray_local, "", frag_len, "")
    frag_m = expr(unreal.MaterialExpressionMultiply)
    connect(frag_len, "", frag_m, "A")
    connect(metres, "", frag_m, "B")
    sun_local = to_local(expr(unreal.MaterialExpressionSkyAtmosphereLightDirection))
    # The ray's cosine to the view axis turns scene depth (along that axis) into distance along the ray.
    cam_vec = expr(unreal.MaterialExpressionCameraVectorWS)
    fwd = expr(unreal.MaterialExpressionCustom)
    fwd_in = unreal.CustomInput()
    fwd_in.set_editor_property("input_name", "V")
    fwd.set_editor_property("inputs", [fwd_in])
    fwd.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    fwd.set_editor_property("code", "return dot(-normalize(V), View.ViewForward);")
    connect(cam_vec, "", fwd, "V")
    depth = expr(unreal.MaterialExpressionSceneDepth)
    strength = expr(unreal.MaterialExpressionScalarParameter)
    strength.set_editor_property("parameter_name", "Strength")
    # On-screen brightness per metre of sheet seen through (the eye-adaptation node below holds it there
    # whatever the exposure, as for the stars).
    strength.set_editor_property("default_value", 0.012)
    sources = [("Ro", cam_m), ("Rd", ray_local), ("Frag", frag_m), ("Sun", sun_local), ("Container", container),
               ("Planet", planet), ("Base", base), ("Top", top), ("Depth", depth), ("CamFwd", fwd),
               ("Time", expr(unreal.MaterialExpressionTime)), ("Strength", strength)]
    custom = expr(unreal.MaterialExpressionCustom, -300, 0)
    inputs = []
    for input_name, _ in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", AURORA_CODE)
    for input_name, node in sources:
        connect(node, "", custom, input_name)
    held = expr(unreal.MaterialExpressionEyeAdaptationInverse, -50, 0)
    connect(custom, "", held, "LightValueInput")
    connect_property(held, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    return finish_material(material, stamp, name)


STONE_CODE = r"""
// Loose rock on the ground: stones, boulders, outcrops. WP: world position (cm). N: world normal.
// Scans come with their own stone, each a different rock; here every one is redrawn as the land's
// own basalt, with moss on whatever faces the sky, so a boulder belongs to the ground it sits on.
// The world you stand on is still and centred on the origin, so "up" is simply the position.
float3 pm = WP * 0.01;
float3 Nw = normalize(N);
float3 w = pow(abs(Nw), 4.0);
w /= (w.x + w.y + w.z);
float k = 1.0 / Tiling;
#define SS View.MaterialTextureBilinearWrapedSampler
#define TRI(T) (Texture2DSample(T, SS, pm.zy * k) * w.x + Texture2DSample(T, SS, pm.xz * k) * w.y + Texture2DSample(T, SS, pm.xy * k) * w.z)
#define MEAN(T) (Texture2DSampleLevel(T, SS, float2(0.5, 0.5), 12.0).rgb + 0.004)
#define NRM(T, OUT) { \
    float3 tx = UnpackNormalMap(Texture2DSample(T, SS, pm.zy * k)).xyz; \
    float3 ty = UnpackNormalMap(Texture2DSample(T, SS, pm.xz * k)).xyz; \
    float3 tz = UnpackNormalMap(Texture2DSample(T, SS, pm.xy * k)).xyz; \
    tx = float3(tx.xy + Nw.zy, abs(tx.z) * Nw.x); \
    ty = float3(ty.xy + Nw.xz, abs(ty.z) * Nw.y); \
    tz = float3(tz.xy + Nw.xy, abs(tz.z) * Nw.z); \
    OUT = normalize(tx.zyx * w.x + ty.xzy * w.y + tz.xyz * w.z); }
float3 grain = TRI(RockD).rgb / MEAN(RockD);
// A rock as big as a house shows the scan repeating (it is Tiling metres across, and the rock thirty): there
// (Macro 1; stones 0) the stone is drawn a second time, six times larger, and the two are laid over each other
// with a third, larger still, deciding which shows where: no size repeats alone.
float kb = k * 0.17;
#define TRIK(T, K) (Texture2DSample(T, SS, pm.zy * (K)) * w.x + Texture2DSample(T, SS, pm.xz * (K)) * w.y + Texture2DSample(T, SS, pm.xy * (K)) * w.z)
float3 big = TRIK(RockD, kb).rgb / MEAN(RockD);
float which = saturate(dot(TRIK(RockD, k * 0.043).rgb, 0.333) / dot(MEAN(RockD), 0.333) * 1.4 - 0.7);
grain = lerp(grain, lerp(grain, big, 0.35 + 0.4 * which) * lerp(0.72, 1.18, which), Macro);
float3 rock = RockC.rgb * clamp(grain, 0.4, 2.2);
float3 mossTex = TRI(MossD).rgb;
float3 moss = MossC.rgb * clamp(mossTex / MEAN(MossD), 0.45, 2.0);
// Moss lies where rain and dust settle: on top, patchily, and thicker where the scan says it is.
float up = dot(Nw, normalize(pm));
float patchy = dot(Texture2DSample(MossD, SS, pm.xy * k * 0.21).rgb, 0.333) / dot(MEAN(MossD), 0.333);
float mossy = saturate(smoothstep(MossEdge - 0.25, MossEdge + 0.2, up + (patchy - 1.0) * 0.6) * MossAmount);
float3 nr, nm;
NRM(RockN, nr)
NRM(MossN, nm)
Nrm = normalize(lerp(nr, nm, mossy));
float3 arm = TRI(RockA).rgb;
// Dry stone: matt, and mirroring least in its own pits.
ORM = float3(arm.r, lerp(clamp(arm.g, 0.62, 0.95), 0.95, mossy), lerp(0.35, 0.18, mossy) * lerp(0.45, 1.0, arm.r));
// A rock that stands in the sea is wet as high as the high tide throws (the tide here runs five metres): darker
// there, and with a sheen. (SeaRadiusM: the sea's distance from the world's centre; 0 for rocks that stand on land.)
float above = length(pm) - SeaRadiusM;
float wet = step(1.0, SeaRadiusM) * (1.0 - smoothstep(1.5, 7.5, above + (patchy - 1.0) * 2.0));
float3 colour = lerp(rock, moss, mossy * (1.0 - wet));
ORM.g = lerp(ORM.g, 0.42, wet * 0.85);
ORM.b = lerp(ORM.b, 0.4, wet);
return colour * lerp(1.0, 0.55, wet);
"""


def make_stone_material(rock, moss):
    """rock, moss: {'d','n','arm'} scans (the terrain's own basalt and moss)."""
    name = "M_Stone"
    path = f"{ROOT}/Materials/{name}"
    stamp = stamp_of("stone-v8", STONE_CODE, rock, moss)
    kept = unchanged(path, stamp)
    if kept:
        return kept
    material = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
    if material:
        EDIT.delete_all_material_expressions(material)
    else:
        material = TOOLS.create_asset(name, f"{ROOT}/Materials", unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("tangent_space_normal", False)

    def expr(cls, x=-900, y=0):
        return EDIT.create_material_expression(material, cls, x, y)

    def scalar(parameter, value):
        node = expr(unreal.MaterialExpressionScalarParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", value)
        return node

    def colour(parameter, value):
        node = expr(unreal.MaterialExpressionVectorParameter)
        node.set_editor_property("parameter_name", parameter)
        node.set_editor_property("default_value", unreal.LinearColor(*value, 1.0))
        return node

    sources = [("WP", expr(unreal.MaterialExpressionWorldPosition), ""), ("N", expr(unreal.MaterialExpressionVertexNormalWS), ""),
               ("Tiling", scalar("Tiling", 2.5), ""),
               ("RockC", colour("RockColour", (0.046, 0.046, 0.052)), ""), ("MossC", colour("MossColour", (0.050, 0.078, 0.030)), ""),
               # How much of a stone's top the moss has taken (0 none, 1 all it can), and how far down the sides it comes.
               ("MossAmount", scalar("MossAmount", 0.35), ""), ("MossEdge", scalar("MossEdge", 0.55), ""),
               ("Macro", scalar("Macro", 0.0), ""), ("SeaRadiusM", scalar("SeaRadiusM", 0.0), "")]
    sampler_types = {"d": unreal.MaterialSamplerType.SAMPLERTYPE_COLOR, "n": unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL,
                     "arm": unreal.MaterialSamplerType.SAMPLERTYPE_MASKS}
    for label, scans, keys in (("Rock", rock, ("d", "n", "arm")), ("Moss", moss, ("d", "n"))):
        for key in keys:
            node = expr(unreal.MaterialExpressionTextureObjectParameter, -1300, 0)
            suffix = {"d": "D", "n": "N", "arm": "A"}[key]
            node.set_editor_property("parameter_name", f"{label}{suffix}")
            node.set_editor_property("texture", scans[key])
            node.set_editor_property("sampler_type", sampler_types[key])
            sources.append((f"{label}{suffix}", node, ""))
    custom = expr(unreal.MaterialExpressionCustom, -300, 0)
    inputs = []
    for input_name, _, _ in sources:
        entry = unreal.CustomInput()
        entry.set_editor_property("input_name", input_name)
        inputs.append(entry)
    outputs = []
    for output_name in ("Nrm", "ORM"):
        entry = unreal.CustomOutput()
        entry.set_editor_property("output_name", output_name)
        entry.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
        outputs.append(entry)
    custom.set_editor_property("inputs", inputs)
    custom.set_editor_property("additional_outputs", outputs)
    custom.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    custom.set_editor_property("code", STONE_CODE)
    for input_name, node, pin in sources:
        connect(node, pin, custom, input_name)
    connect_property(custom, "", unreal.MaterialProperty.MP_BASE_COLOR)
    connect_property(custom, "Nrm", unreal.MaterialProperty.MP_NORMAL)
    for channel, prop in (("r", unreal.MaterialProperty.MP_AMBIENT_OCCLUSION), ("g", unreal.MaterialProperty.MP_ROUGHNESS),
                          ("b", unreal.MaterialProperty.MP_SPECULAR)):
        mask = expr(unreal.MaterialExpressionComponentMask, -100, 400)
        for c in ("r", "g", "b"):
            mask.set_editor_property(c, c == channel)
        connect(custom, "ORM", mask, "")
        connect_property(mask, "", prop)
    for usage in (unreal.MaterialUsage.MATUSAGE_NANITE, unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES):
        EDIT.set_base_material_usage(material, usage, True)
    return finish_material(material, stamp, name)


def material_instance(parent, name, scalars, colours=None):
    """A variant of a material with some of its numbers and colours changed."""
    path = f"{ROOT}/Materials/{name}"
    instance = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else TOOLS.create_asset(
        name, f"{ROOT}/Materials", unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    instance.set_editor_property("parent", parent)
    for parameter, value in scalars.items():
        EDIT.set_material_instance_scalar_parameter_value(instance, parameter, value)
    for parameter, value in (colours or {}).items():
        EDIT.set_material_instance_vector_parameter_value(instance, parameter, unreal.LinearColor(*value, 1.0))
    ASSETS.save_loaded_asset(instance)
    return instance


def sized_rules(meshes, per_m2, size_min_m, size_max_m, **values):
    """Ground-cover rules for scans of any native size: each mesh is scaled so its largest side runs from size_min_m to size_max_m."""
    rules = []
    for mesh in meshes:
        extent = mesh.get_bounds().box_extent
        native_m = 2.0 * max(extent.x, extent.y, extent.z) / 100.0
        if native_m <= 0.01:
            continue
        rule = unreal.EphemeraGroundCoverRule()
        rule.set_editor_property("mesh", mesh)
        rule.set_editor_property("density_per_m2", per_m2 / len(meshes))
        rule.set_editor_property("scale_min", size_min_m / native_m)
        rule.set_editor_property("scale_max", size_max_m / native_m)
        for key, value in values.items():
            rule.set_editor_property(key, value)
        rules.append(rule)
    return rules


def dress_ground(body, surfaces):
    """What lies and grows on Dawnhollow's ground, placed around the camera as it moves.

    One geology: every stone is the land's own basalt (make_stone_material), mossed over on old
    ground and bare on young. Three sizes, the way a real slope sorts them: a few boulders, more
    rocks, many stones; outcrops on the steeper ground; moss cushions on the moss lands.
    """
    stone = make_stone_material(surfaces["basalt"], surfaces["moss"])
    bare = material_instance(stone, "MI_Stone_Bare", {"MossAmount": 0.0})
    lichened = material_instance(stone, "MI_Stone", {"MossAmount": 0.45, "MossEdge": 0.6})
    cushion = material_instance(stone, "MI_Stone_Moss", {"MossAmount": 1.0, "MossEdge": -0.35})
    stones = import_scan("rocks/rock_moss_set_01") + import_scan("rocks/rock_moss_set_02")
    boulders = import_scan("rocks/boulder_01") + import_scan("rocks/namaqualand_boulder_02") + import_scan("rocks/namaqualand_boulder_03")
    ledges = import_scan("rocks/coastal_cliff_04") + import_scan("rocks/coast_rocks_05")
    lie = dict(align_to_ground=1.0, scale_power=2.2)
    rules = []
    # ---- Near the camera (the first hundred metres). Nothing here is bare: stones of every size
    # down to a fist, grass and heath between them, moss cushions on the moss lands.
    rules += sized_rules(stones, 0.55, 0.10, 0.5, material=bare, cover_max=0.15, max_slope_deg=50.0, random_tilt_deg=30.0, sink_fraction=0.22, **lie)
    rules += sized_rules(stones, 0.28, 0.10, 0.5, material=lichened, cover_min=0.15, max_slope_deg=45.0, random_tilt_deg=30.0, sink_fraction=0.3,
                         patch_frequency=600.0, patch_coverage=0.55, **lie)
    rules += sized_rules(stones, 0.06, 0.4, 1.3, material=lichened, max_slope_deg=45.0, random_tilt_deg=18.0, sink_fraction=0.3,
                         patch_frequency=600.0, patch_coverage=0.6, **lie)
    # Hummocks: the moss lands are not a lawn but a sea of knee-high cushions (thufur), crowded
    # shoulder to shoulder. Stones wholly grown over and sunk past their middles make them.
    rules += sized_rules(stones + boulders, 0.16, 0.7, 1.9, material=cushion, cover_min=0.3, max_slope_deg=34.0, random_tilt_deg=8.0,
                         sink_fraction=0.62, patch_frequency=260.0, patch_coverage=0.75, **lie)
    # Grass everywhere the ground is not bare, short and thick with longer tufts through it; and
    # heath, low and red-brown, in drifts.
    green = make_grass_material()
    grass = grass_clumps(green)
    heath = material_instance(green, "MI_Heath", {"DryBias": 0.25}, {"Root": (0.022, 0.015, 0.011), "Tip": (0.056, 0.030, 0.024), "DryTip": (0.070, 0.046, 0.030)})
    rules += cover_rules(grass["turf"], 15.0, cover_min=0.05, max_slope_deg=42.0, scale_min=0.9, scale_max=1.7, cast_shadow=False)
    rules += cover_rules(grass["meadow"], 4.5, cover_min=0.1, max_slope_deg=40.0, scale_min=0.8, scale_max=1.6,
                         patch_frequency=420.0, patch_coverage=0.72, cast_shadow=False)
    # Sedge: long straw-coloured grass in drifts, bleached by the wind. Paler than the turf, but by
    # less than a stop: it is the mass of it that reads as pale, not each blade as a white line.
    sedge = material_instance(green, "MI_Sedge", {"DryBias": 0.60}, {"Root": (0.030, 0.038, 0.016), "Tip": (0.070, 0.076, 0.036), "DryTip": (0.112, 0.100, 0.052)})
    rules += cover_rules(grass["tall"] + grass["meadow"], 3.2, material=sedge, cover_min=0.0, max_slope_deg=36.0, scale_min=0.9, scale_max=1.8,
                         patch_frequency=300.0, patch_coverage=0.45, cast_shadow=False)
    rules += cover_rules(grass["turf"] + grass["meadow"], 4.0, material=heath, cover_min=-0.2, max_slope_deg=42.0, scale_min=0.9, scale_max=1.7,
                         patch_frequency=520.0, patch_coverage=0.45, cast_shadow=False)
    # ---- Seen from far off.
    # ---- Seen from far off: what has come down from the cliffs. Boulders in loose fields, the
    # rocks between them, and scree thick on the steeper ground under the faces.
    rules += sized_rules(boulders, 0.0034, 1.6, 5.0, material=lichened, far=True, max_slope_deg=45.0, random_tilt_deg=12.0, sink_fraction=0.3,
                         patch_frequency=90.0, patch_coverage=0.6, **lie)
    rules += sized_rules(stones, 0.014, 0.6, 1.7, material=lichened, far=True, max_slope_deg=42.0, random_tilt_deg=18.0, sink_fraction=0.3,
                         patch_frequency=160.0, patch_coverage=0.5, **lie)
    rules += sized_rules(stones + boulders, 0.034, 0.5, 2.2, material=bare, far=True, min_slope_deg=22.0, max_slope_deg=44.0, random_tilt_deg=25.0,
                         sink_fraction=0.25, **lie)
    body.set_editor_property("ground_cover", rules)
    body.set_editor_property("ground_cover_radius_m", 110.0)
    body.set_editor_property("ground_cover_far_radius_m", 650.0)
    log(f"ground cover dawnhollow: {len(rules)} rules")


BASALT = {
    # name: (seed, style, radius cm, tallest cm, column spacing cm). Style 0 a massif, 1 a causeway.
    "SM_Basalt_Massif_0": (11, 0, 650.0, 700.0, 72.0),
    "SM_Basalt_Massif_1": (23, 0, 800.0, 1000.0, 78.0),
    "SM_Basalt_Massif_2": (37, 0, 550.0, 500.0, 66.0),
    "SM_Basalt_Causeway_0": (41, 1, 450.0, 250.0, 55.0),
    "SM_Basalt_Causeway_1": (53, 1, 380.0, 190.0, 52.0),
    "SM_Basalt_Causeway_2": (67, 1, 420.0, 300.0, 58.0),
}


def basalt_columns():
    """Columnar basalt, made here (EphemeraPlanetBuilder.BuildBasaltColumns): {'massifs': [...], 'causeways': [...]}."""
    folder = f"{ROOT}/Rocks/Basalt"
    result = {"massifs": [], "causeways": []}
    for name, values in BASALT.items():
        path = f"{folder}/{name}"
        stamp = stamp_of("basalt-v4", values)
        mesh = ASSETS.load_asset(path) if ASSETS.does_asset_exist(path) else None
        if not mesh or ASSETS.get_metadata_tag(mesh, "EphemeraStamp") != stamp:
            seed, style, radius, height, spacing = values
            mesh = unreal.EphemeraPlanetBuilder.build_basalt_columns(folder, name, seed, style, radius, height, spacing)
            if not mesh:
                raise RuntimeError(f"basalt {name} failed")
            ASSETS.set_metadata_tag(mesh, "EphemeraStamp", stamp)
            ASSETS.save_loaded_asset(mesh)
            log(f"basalt {name}: built")
        result["massifs" if values[1] == 0 else "causeways"].append(mesh)
    return result


def stand_rocks(body, surfaces):
    """What stands up out of Dawnhollow's land and sea, placed once for the whole world (not around the camera).

    On a world this small the ground curves away under you: from eye height the plain ends 130 m off, and
    between that edge and the far mountains there is nothing unless it stands up. So things do: stacks of
    basalt in the shallows along every coast, lone rocks and groups of them on the plains, and low walls of
    outcrop on the gentler slopes. They are the land's own stone (the scanned boulders, redrawn in basalt and
    lichen as every loose stone is), stood up tall, and they can be walked round, climbed and flown into.
    """
    stone = make_stone_material(surfaces["basalt"], surfaces["moss"])
    # Sea stacks: the black sand's own basalt, wet at the foot, with turf on the flattest of their tops (as the
    # stacks off Iceland's south coast have). Dry and pale grey they looked like blocks of concrete stood on a beach.
    bare = material_instance(stone, "MI_Stone_Stack", {"MossAmount": 0.5, "MossEdge": 0.82, "Macro": 1.0,
                                                       "SeaRadiusM": float(body.get_editor_property("radius_m"))},
                             {"RockColour": (0.019, 0.019, 0.022)})
    lichened = material_instance(stone, "MI_Stone_Standing", {"MossAmount": 0.45, "MossEdge": 0.6, "Macro": 1.0},
                                 {"RockColour": (0.030, 0.030, 0.034)})
    boulders = import_scan("rocks/boulder_01") + import_scan("rocks/namaqualand_boulder_02") + import_scan("rocks/namaqualand_boulder_03")
    walls = import_scan("rocks/coastal_cliff_04")
    for mesh in boulders + walls:
        setup = mesh.get_editor_property("body_setup")
        if setup and setup.get_editor_property("collision_trace_flag") != unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE:
            setup.set_editor_property("collision_trace_flag", unreal.CollisionTraceFlag.CTF_USE_COMPLEX_AS_SIMPLE)
            ASSETS.save_loaded_asset(mesh)
    # Columnar basalt: what a thick lava flow cracks into as it cools. Massifs of it stand on the shore and
    # in the shallows (Reynisfjara), and causeways of short columns step down into the sea and break out of the land.
    # Upright, not laid along the slope: the columns stand as the flow cooled, square to its top.
    basalt = basalt_columns()
    columns = material_instance(stone, "MI_Stone_Columns", {"MossAmount": 0.85, "MossEdge": 0.72, "Macro": 0.0, "Tiling": 1.6,
                                                            "SeaRadiusM": float(body.get_editor_property("radius_m"))},
                                {"RockColour": (0.024, 0.024, 0.027)})
    upright = dict(material=columns, align_to_ground=0.0, sink_fraction=0.0, collision=True)
    rules = [
        scatter_rule(basalt["massifs"], 0.9, min_altitude_m=-5.0, max_altitude_m=40.0, max_slope_deg=18.0,
                     scale_min=0.8, scale_max=1.3, random_tilt_deg=3.0, sink_cm=415.0,
                     patch_frequency=70.0, patch_coverage=0.45, plateau_clearance=0.0, **upright),
        scatter_rule(basalt["causeways"], 2.2, min_altitude_m=-2.5, max_altitude_m=6.0, max_slope_deg=13.0,
                     scale_min=0.85, scale_max=1.25, random_tilt_deg=3.0, sink_cm=160.0,
                     patch_frequency=60.0, patch_coverage=0.5, plateau_clearance=0.0, **upright),
        scatter_rule(basalt["causeways"], 0.35, min_altitude_m=6.0, max_altitude_m=200.0, max_slope_deg=12.0,
                     scale_min=0.7, scale_max=1.1, random_tilt_deg=3.0, sink_cm=170.0,
                     patch_frequency=40.0, patch_coverage=0.35, plateau_clearance=1.3, **upright),
        # Sea stacks: in the shallows and on the tide line, in groups, 12 to 45 m tall.
        scatter_rule(boulders, 1.7, material=bare, min_altitude_m=-6.0, max_altitude_m=1.5, max_slope_deg=40.0,
                     scale_min=5.0, scale_max=13.0, upright_min=1.5, upright_max=2.6, random_tilt_deg=7.0, sink_cm=0.0, sink_fraction=0.22,
                     patch_frequency=70.0, patch_coverage=0.42, plateau_clearance=0.0, collision=True),
        # Standing rocks on the open land: alone and in loose groups, 5 to 25 m.
        scatter_rule(boulders, 1.3, material=lichened, min_altitude_m=6.0, max_altitude_m=300.0, max_slope_deg=16.0,
                     scale_min=4.0, scale_max=11.0, upright_min=1.25, upright_max=2.1, random_tilt_deg=5.0, sink_cm=0.0, sink_fraction=0.2,
                     patch_frequency=45.0, patch_coverage=0.34, plateau_clearance=1.3, collision=True),
        # Low walls of outcrop on the gentler slopes: 25 to 50 m long, a few metres high.
        scatter_rule(walls, 0.17, material=lichened, min_altitude_m=5.0, max_altitude_m=320.0, min_slope_deg=5.0, max_slope_deg=26.0,
                     scale_min=0.28, scale_max=0.58, align_to_ground=0.6, random_tilt_deg=3.0, sink_cm=0.0, sink_fraction=0.3,
                     patch_frequency=30.0, patch_coverage=0.5, plateau_clearance=1.3, collision=True),
    ]
    count = unreal.EphemeraPlanetBuilder.build_scatter(body, rules)
    log(f"standing rocks dawnhollow: {count} instances")
    # Where the basalt nearest the spawn stands (for photographs): direction from the centre, and how far off.
    spawn = unreal.Vector(0.92985, -0.17608, 0.32308)
    spawn.normalize()
    near = []
    for batch in body.get_editor_property("scatter"):
        mesh = batch.get_editor_property("mesh")
        if mesh and "Basalt" in mesh.get_name():
            for transform in batch.get_editor_property("instances"):
                at = transform.translation
                d = at.normal()
                near.append((math.acos(max(-1.0, min(1.0, d.dot(spawn)))) * 5000.0, mesh.get_name(), d, at.length() / 100.0 - 5000.0))
    near.sort(key=lambda entry: entry[0])
    for metres, name, d, altitude in near[:12]:
        log(f"basalt near spawn: {metres:.0f} m {name} dir {d.x:.5f},{d.y:.5f},{d.z:.5f} alt {altitude:.1f}")


def dress_dawnhollow(body):
    """Rocks, trees and dead wood are baked; grass, flowers and ferns grow around the camera."""
    global FOLIAGE_PARENT
    FOLIAGE_PARENT = foliage_parent()      # always regenerated, so a fix to it reaches every leaf
    # One geology: grey, lichened granite, the same stone as the terrain's rock layer. (The first pass
    # mixed in orange desert boulders, which read as props dropped on the land.)
    stones = import_scan("rocks/rock_moss_set_01", collision=True) + import_scan("rocks/rock_moss_set_02", collision=True)
    ledges = import_scan("rocks/coastal_cliff_04", collision=True)
    firs = import_scan("trees/fir_tree_01", foliage=True)
    pines = import_scan("stumps_logs/pine_tree_01", foliage=True)
    broadleaf = import_scan("trees/island_tree_01", foliage=True) + import_scan("trees/island_tree_02", foliage=True) \
        + import_scan("trees/tree_small_02", foliage=True)
    saplings = import_scan("trees/fir_sapling_medium", foliage=True) + import_scan("stumps_logs/pine_sapling_medium", foliage=True)
    bushes = import_scan("plants/searsia_lucida", foliage=True) + import_scan("plants/island_tree_03", foliage=True)
    deadwood = import_scan("stumps_logs/dead_tree_trunk_02", collision=True) + import_scan("stumps_logs/dry_branches_medium_01")
    rules = [
        # Boulder fields: the scanned stones scaled up to boulders, a third buried, in clusters.
        scatter_rule(stones, 5.0, max_slope_deg=40.0, scale_min=2.2, scale_max=5.5, align_to_ground=0.85, random_tilt_deg=8.0,
                     sink_cm=10.0, sink_fraction=0.32, patch_frequency=12.0, patch_coverage=0.42, patch_group=0, collision=True),
        # Mid-sized rocks around them, then scree: same cluster pattern, wider and denser as they get smaller.
        scatter_rule(stones, 34.0, max_slope_deg=45.0, scale_min=0.7, scale_max=1.9, align_to_ground=0.9, random_tilt_deg=14.0,
                     sink_cm=4.0, sink_fraction=0.28, patch_frequency=12.0, patch_coverage=0.5, patch_group=0, collision=True, plateau_clearance=0.6),
        scatter_rule(stones, 260.0, max_slope_deg=50.0, scale_min=0.12, scale_max=0.42, align_to_ground=1.0, random_tilt_deg=25.0,
                     sink_cm=1.0, sink_fraction=0.22, patch_frequency=12.0, patch_coverage=0.58, patch_group=0, plateau_clearance=0.4,
                     cull_distance_m=90.0),
        # Ledges of bedrock breaking through on the steeper slopes.
        scatter_rule(ledges, 2.4, min_slope_deg=9.0, max_slope_deg=50.0, scale_min=1.0, scale_max=2.4, align_to_ground=0.95,
                     random_tilt_deg=4.0, sink_cm=20.0, sink_fraction=0.30, patch_frequency=9.0, patch_coverage=0.5, collision=True),
        scatter_rule(firs, 55.0, max_slope_deg=28.0, moisture_min=0.52, scale_min=0.8, scale_max=1.5, random_tilt_deg=3.0,
                     sink_cm=20.0, patch_frequency=8.0, patch_coverage=0.45),
        scatter_rule(pines, 25.0, max_slope_deg=28.0, moisture_min=0.38, moisture_max=0.78, scale_min=0.8, scale_max=1.4,
                     random_tilt_deg=4.0, sink_cm=20.0, patch_frequency=7.0, patch_coverage=0.4),
        scatter_rule(broadleaf, 14.0, max_slope_deg=24.0, moisture_min=0.3, moisture_max=0.7, scale_min=0.8, scale_max=1.3,
                     random_tilt_deg=4.0, sink_cm=15.0, patch_frequency=6.0, patch_coverage=0.3),
        scatter_rule(saplings, 60.0, max_slope_deg=30.0, moisture_min=0.5, scale_min=0.7, scale_max=1.4, sink_cm=8.0,
                     patch_frequency=8.0, patch_coverage=0.5),
        scatter_rule(bushes, 25.0, max_slope_deg=32.0, moisture_min=0.25, scale_min=0.7, scale_max=1.3, sink_cm=10.0,
                     patch_frequency=12.0, patch_coverage=0.5),
        scatter_rule(deadwood, 12.0, max_slope_deg=30.0, moisture_min=0.45, scale_min=0.8, scale_max=1.3, align_to_ground=1.0,
                     sink_cm=6.0, patch_frequency=8.0, patch_coverage=0.5),
    ]
    count = unreal.EphemeraPlanetBuilder.build_scatter(body, rules)
    log(f"scatter dawnhollow: {count} instances")

    # Grass is real geometry (see grass_clumps): no cut-outs, cheap for Nanite, solid at any distance.
    # The scanned flowers and ferns stay as they are; there are few enough of them.
    grass = grass_clumps(make_grass_material())
    dandelions = import_scan("plants/dandelion_01", foliage=True, all_leaf=True)
    celandine = import_scan("plants/celandine_01", foliage=True, all_leaf=True)
    ferns = import_scan("trees/fern_02", foliage=True, all_leaf=True)
    cover = cover_rules(grass["turf"], 13.0, scale_min=0.8, scale_max=1.3, patch_frequency=70.0, patch_coverage=0.96, cast_shadow=False) \
        + cover_rules(grass["meadow"], 9.0, scale_min=0.8, scale_max=1.4, patch_frequency=55.0, patch_coverage=0.8, cast_shadow=False) \
        + cover_rules(grass["tall"], 2.2, scale_min=0.8, scale_max=1.5, moisture_min=0.35, patch_frequency=38.0, patch_coverage=0.42) \
        + cover_rules(dandelions, 0.45, scale_min=0.9, scale_max=1.5, patch_frequency=120.0, patch_coverage=0.35) \
        + cover_rules(celandine, 0.35, scale_min=0.9, scale_max=1.5, moisture_min=0.45, patch_frequency=100.0, patch_coverage=0.3) \
        + cover_rules(ferns, 0.30, scale_min=0.9, scale_max=1.6, moisture_min=0.55, patch_frequency=60.0, patch_coverage=0.4)
    body.set_editor_property("ground_cover", cover)
    body.set_editor_property("ground_cover_radius_m", 42.0)
    ASSETS.save_loaded_asset(body)


# ------------------------------------------------------------------------------------ bodies ---

def definition(asset_name):
    folder = f"{ROOT}/Bodies"
    path = f"{folder}/{asset_name}"
    if ASSETS.does_asset_exist(path):
        return ASSETS.load_asset(path)
    factory = unreal.DataAssetFactory()
    factory.set_editor_property("data_asset_class", unreal.EphemeraBodyDefinition)
    return TOOLS.create_asset(asset_name, folder, unreal.EphemeraBodyDefinition, factory)


def noise(frequency, amplitude, octaves, ridged=0.0):
    layer = unreal.EphemeraNoiseLayer()
    layer.set_editor_property("frequency", frequency)
    layer.set_editor_property("amplitude_m", amplitude)
    layer.set_editor_property("octaves", octaves)
    layer.set_editor_property("ridged", ridged)
    return layer


def feature(name, kind, direction, radius, height, falloff=0.5):
    f = unreal.EphemeraTerrainFeatureDef()
    f.set_editor_property("name", name)
    f.set_editor_property("type", kind)
    f.set_editor_property("direction", unreal.Vector(*direction))
    f.set_editor_property("radius_m", radius)
    f.set_editor_property("height_m", height)
    f.set_editor_property("falloff", falloff)
    return f


def describe(value):
    """A stable text for a value made of numbers, strings, lists, dicts and Unreal structs."""
    if isinstance(value, dict):
        return "{" + ",".join(f"{key}:{describe(value[key])}" for key in sorted(value)) + "}"
    if isinstance(value, (list, tuple)):
        return "[" + ",".join(describe(item) for item in value) + "]"
    if hasattr(value, "export_text"):
        return value.export_text()
    return repr(value)


def set_all(asset, values):
    for key, value in values.items():
        asset.set_editor_property(key, value)


def crater_field(seed, count, radius_m, largest_m, smallest_m):
    """Impact craters for an airless moon: many small, few large, the way real cratering is distributed."""
    rng = random.Random(seed)
    craters = []
    for index in range(count):
        z = rng.uniform(-1.0, 1.0)
        angle = rng.uniform(0.0, 2.0 * math.pi)
        r = math.sqrt(max(0.0, 1.0 - z * z))
        size = smallest_m * (largest_m / smallest_m) ** (rng.random() ** 2.6)
        craters.append(feature(f"Crater{index}", unreal.EphemeraTerrainFeature.CRATER,
                               (r * math.cos(angle), r * math.sin(angle), z), size, size * rng.uniform(0.16, 0.24), 0.45))
    return craters


# EPHEMERA_PREVIEW=1 (`unreal.py slice --preview`): terrain at a quarter of the grid resolution.
# Coarse up close, indistinguishable from orbit, and quick to build: for trying a planet's shape.
PREVIEW = os.environ.get("EPHEMERA_PREVIEW") == "1"


def grid(face_resolution, render_chunks):
    if PREVIEW:
        return {"face_resolution": max(face_resolution // 4, 64), "render_chunks_per_face": max(render_chunks // 2, 2)}
    return {"face_resolution": face_resolution, "render_chunks_per_face": render_chunks}


def build_bodies(materials):
    # Scale (docs/unreal/DIRECTION.md, "Vision reset"): worlds kilometres across, so they read as
    # worlds from orbit; a star far enough away to be a disc a few degrees wide.
    star = definition("DA_Aubade")
    set_all(star, {
        "body_id": "aubade", "display_name": "Aubade", "is_star": True,
        "radius_m": 15000.0, "surface_gravity": 110.0, "max_gravity": 45.0, "influence_radius_m": 1.0e9,
        "terrain_mesh": ASSETS.load_asset("/Engine/BasicShapes/Sphere"),
        "terrain_mesh_scale": 15000.0 * 100.0 / 50.0,
        "terrain_material": materials["star"],
    })
    ASSETS.save_loaded_asset(star)

    # Dawnhollow: seas and a few landmasses; moss highlands, black outwash plains, basalt ranges
    # with snow on top. The Cradle is a broad shelf near the equator of its own frame.
    dawnhollow = definition("DA_Dawnhollow")
    terrain = unreal.EphemeraTerrainSettings()
    dawn_shape = {
        "seed": 7, **grid(1024, 12), "collision_chunks_per_face": 0,
        "continents": True, "continent_frequency": 1.35, "continent_height_m": 170.0, "sea_depth_m": 360.0, "sea_bias": 0.04,
        "mountain_height_m": 540.0, "mountain_frequency": 4.2,
        # Worn hills and valleys over all the land (largest about a kilometre across, nine octaves
        # down to the mesh's own spacing), then only a little fine roughness on top.
        "relief_height_m": 230.0, "relief_frequency": 5.0, "relief_octaves": 9, "relief_erosion": 1.0,
        "layers": [noise(260.0, 1.2, 3)],
        "features": [feature("Cradle", unreal.EphemeraTerrainFeature.PLATEAU, (1.0, 0.0, 0.12), 320.0, 28.0, 1.2)],
    }
    # The land itself is baked offline (Tools/terrain/bake.py): everything above except the grid is
    # only what the world was before, kept for reference.
    baked = BAKES / ("dawnhollow_256.ephf" if PREVIEW else "dawnhollow_1024.ephf")
    if not baked.exists():
        raise RuntimeError(f"{baked} is missing: run Tools/terrain/run.sh bake.py 1024 {BAKES}")
    dawn_shape["baked"] = hashlib.sha1(baked.read_bytes()).hexdigest()[:16]
    set_all(terrain, {key: value for key, value in dawn_shape.items() if key != "baked"})
    set_all(dawnhollow, {
        "body_id": "dawnhollow", "display_name": "Dawnhollow",
        "radius_m": 5000.0, "surface_gravity": 10.5, "max_gravity": 40.0,
        # Dawnhollow's own frame reaches out past Hush's orbit (24 km): between the two there must be
        # no stretch of the star's frame, where both worlds go by at some 380 m/s and a traveller
        # crossing from one to the other is left behind by both.
        "influence_radius_m": 60000.0,
        "breathable": True, "parent_id": "aubade",
        # The story's day is 22 minutes (1320 s) from sunrise to sunrise at the Cradle. A year is five
        # days, so the spin is a little slower than the day: 1/1320 = 1/6600 (orbit) + 1/1650 (spin).
        # The spin phase puts the Cradle on the dawn line at time 0.
        "orbit_radius_m": 400000.0, "orbit_period_s": 6600.0, "orbit_phase_rad": 0.4,
        "spin_period_s": 1650.0, "spin_phase_rad": math.pi / 2.0 - 0.4, "axial_tilt_rad": 0.0,
        # The sky has its own character: the air scatters more green than Earth's, so the day sky is
        # teal and the light that survives a long path at dawn and dusk is rose and violet. A strong
        # absorbing layer deepens the zenith toward indigo.
        "atmosphere_height_m": 1400.0, "rayleigh_scattering": unreal.LinearColor(0.115, 0.455, 0.90, 1.0),
        "mie_scale": 0.00055, "absorption_scale": 2.2, "absorption": unreal.LinearColor(0.42, 1.0, 0.10, 1.0),
        "rayleigh_scale": 0.0331, "mist_per_km": 0.2, "mist_height_m": 150.0, "tide_amplitude_m": 5.0, "tide_body": "hush", "tide_collection": world_parameters(), "aurora_material": make_aurora_material(), "cloud_base_m": 280.0, "cloud_thickness_m": 130.0, "cloud_material": materials["clouds"],
        "micro_relief": 1.0,
        "terrain": terrain, "terrain_material": materials["dawnhollow"], "terrain_mesh_scale": 1.0,
        # Ground dressing belongs to the next phase; the earlier forest does not fit this world.
        "scatter": [], "ground_cover": [],
    })
    dress_ground(dawnhollow, SURFACE_SETS)
    dress_ground_grid(dawnhollow, materials["dawnhollow_near"], materials["dawnhollow_far"])
    ASSETS.save_loaded_asset(dawnhollow)

    hush = definition("DA_Hush")
    craters = unreal.EphemeraCraterSettings()
    # Smallest: 35 m, about eight mesh cells across; anything finer belongs in the surface detail.
    set_all(craters, {"enabled": True, "largest_m": 560.0, "smallest_m": 35.0, "saturation": 0.10,
                      "maria_fraction": 0.28, "maria_saturation": 0.12, "maria_depth_m": 9.0})
    moon = unreal.EphemeraTerrainSettings()
    moon_shape = {
        "seed": 23, **grid(512, 8), "collision_chunks_per_face": 0,
        # A real moon has no rolling hills: a slight swell in the highlands, then craters on
        # craters, from a third of the moon's radius across down to the size the mesh can carry.
        "layers": [noise(2.2, 8.0, 3), noise(30.0, 0.5, 3)],
        "craters": craters,
        "features": [],
    }
    set_all(moon, moon_shape)
    set_all(hush, {
        "body_id": "hush", "display_name": "Hush",
        "radius_m": 1400.0, "surface_gravity": 3.2, "max_gravity": 12.0, "influence_radius_m": 5500.0,
        "breathable": False, "parent_id": "dawnhollow",
        # Tidally locked: one turn per orbit, so one face always looks down on Dawnhollow.
        "orbit_radius_m": 24000.0, "orbit_period_s": 880.0, "orbit_phase_rad": 1.1, "orbit_tilt_rad": 0.12,
        "spin_period_s": 880.0, "atmosphere_height_m": 0.0,
        "terrain": moon, "terrain_material": materials["hush"], "terrain_mesh_scale": 1.0,
        "ocean_material": materials["underlay"],
        "scatter": [], "ground_cover": [],
    })
    ASSETS.save_loaded_asset(hush)

    # The last field is the builder version each body's terrain was made with: bump one to rebuild it
    # after a change to the builder itself (v4 = deeper skirts, v5 = chunks overlap their neighbours).
    # Ocean meshes have their own version (v3 = a plain sphere; the sea's depth is baked into a texture).
    ocean_version = "ocean-mesh-v4"
    stored = ASSETS.get_metadata_tag(dawnhollow, "EphemeraBakeStamp")
    field_path = f"{ROOT}/Planets/Dawnhollow/HF_dawnhollow"
    if stored != dawn_shape["baked"] or not ASSETS.does_asset_exist(field_path):
        if not unreal.EphemeraPlanetBuilder.import_height_field(dawnhollow, str(baked), f"{ROOT}/Planets/Dawnhollow"):
            raise RuntimeError("the baked terrain could not be imported")
        ASSETS.set_metadata_tag(dawnhollow, "EphemeraBakeStamp", dawn_shape["baked"])
        log("baked terrain Dawnhollow: imported")
    else:
        shape = dawnhollow.get_editor_property("terrain")
        shape.set_editor_property("height_field", ASSETS.load_asset(field_path))
        dawnhollow.set_editor_property("terrain", shape)
    for body, folder, builder, described in ((dawnhollow, "Dawnhollow", "v6", dawn_shape), (hush, "Hush", "v5", moon_shape)):
        # Terrain takes minutes to build: only redo it when what shapes it has changed. The stamp is
        # made from the values set here, not from the whole settings struct: a new setting added to
        # the struct, left at its default, must not make every world rebuild.
        shape = body.get_editor_property("terrain")
        stamp = "%s|%s%s|%s|%s" % (builder, PREVIEW, "-nodf" if os.environ.get("EPHEMERA_NO_DF") == "1" else "",
                                  body.get_editor_property("radius_m"), describe(described))
        built = len(body.get_editor_property("terrain_chunks")) > 0
        previous = ASSETS.get_metadata_tag(body, "EphemeraTerrainStamp")
        # Stamps written before 2026-10-13 described the whole settings struct ("(Seed=..."). One
        # made by the same builder version for the same world is taken as current and rewritten.
        if built and previous.split("|")[:3] == stamp.split("|")[:3] and previous.split("|", 3)[-1].startswith("("):
            ASSETS.set_metadata_tag(body, "EphemeraTerrainStamp", stamp)
            previous = stamp
        if built and previous == stamp:
            log(f"terrain {folder}: unchanged, kept")
        else:
            if not unreal.EphemeraPlanetBuilder.build_terrain(body, f"{ROOT}/Planets/{folder}"):
                raise RuntimeError(f"terrain build failed: {folder}")
            ASSETS.set_metadata_tag(body, "EphemeraTerrainStamp", stamp)
            ASSETS.set_metadata_tag(body, "EphemeraOceanStamp", "")
            log(f"terrain {folder}")
        ocean_stamp = f"{ocean_version}|{stamp}"
        if not body.get_editor_property("ocean_mesh") or ASSETS.get_metadata_tag(body, "EphemeraOceanStamp") != ocean_stamp:
            # A sea at the datum for a world with continents; for a dry one, plain rock just below
            # its lowest ground.
            offset = 0.0 if shape.get_editor_property("continents") else body.get_editor_property("terrain_min_m") - 30.0
            if not unreal.EphemeraPlanetBuilder.build_ocean(body, f"{ROOT}/Planets/{folder}", offset):
                raise RuntimeError(f"ocean build failed: {folder}")
            ASSETS.set_metadata_tag(body, "EphemeraOceanStamp", ocean_stamp)
            log(f"ocean {folder} (offset {offset:.0f} m)")
        if shape.get_editor_property("continents"):
            # The sea's material reads the depth of the water from a cube texture baked from the terrain.
            depth_path = f"{ROOT}/Planets/{folder}/T_{body.get_editor_property('body_id')}_Depth"
            depth_stamp = f"depth-v2|{stamp}"
            if not ASSETS.does_asset_exist(depth_path) or ASSETS.get_metadata_tag(body, "EphemeraDepthStamp") != depth_stamp:
                if not unreal.EphemeraPlanetBuilder.build_depth_map(body, f"{ROOT}/Planets/{folder}", 2048):
                    raise RuntimeError(f"depth map build failed: {folder}")
                ASSETS.set_metadata_tag(body, "EphemeraDepthStamp", depth_stamp)
                log(f"depth map {folder}")
            body.set_editor_property("ocean_material", make_ocean_material(ASSETS.load_asset(depth_path)))
            # And the near sea, whose waves are geometry (the game leaves it out with -EphemeraNoSeaGrid).
            dress_sea(body, ASSETS.load_asset(depth_path))
        if shape.get_editor_property("height_field"):
            # Running water lies where the baked terrain gathered it.
            stream_stamp = f"streams-v3|{stamp}"
            if not body.get_editor_property("stream_mesh") or ASSETS.get_metadata_tag(body, "EphemeraStreamStamp") != stream_stamp:
                if not unreal.EphemeraPlanetBuilder.build_streams(body, f"{ROOT}/Planets/{folder}", 0.60, 0.22):
                    raise RuntimeError(f"stream build failed: {folder}")
                ASSETS.set_metadata_tag(body, "EphemeraStreamStamp", stream_stamp)
                log(f"streams {folder}")
            body.set_editor_property("stream_material", make_stream_material())
        if body is dawnhollow:
            # (After its terrain is in place: the rocks are stood on the baked land.)
            stand_rocks(body, SURFACE_SETS)
        ASSETS.save_loaded_asset(body)
    return [star, dawnhollow, hush]


# --------------------------------------------------------------------------------------- map ---

def build_map(bodies):
    levels = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actors = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)
    map_path = f"{ROOT}/Maps/System"
    if ASSETS.does_asset_exist(map_path):
        if not levels.load_level(map_path):
            raise RuntimeError("could not load the System map")
        for actor in actors.get_all_level_actors():
            if isinstance(actor, (unreal.EphemeraBody, unreal.EphemeraSkyRig, unreal.PlayerStart)):
                actors.destroy_actor(actor)
    elif not levels.new_level(map_path):
        raise RuntimeError("could not create the System map")
    for body in bodies:
        actor = actors.spawn_actor_from_class(unreal.EphemeraBody, unreal.Vector(0, 0, 0))
        actor.set_actor_label(str(body.get_editor_property("display_name")))
        actor.set_editor_property("definition", body)
        actor.rebuild()
    rig = actors.spawn_actor_from_class(unreal.EphemeraSkyRig, unreal.Vector(0, 0, 0))
    rig.set_actor_label("Sky")
    rig.set_editor_property("star_material", STARS)
    rig.set_editor_property("mist_material", MIST)
    for name, asset in EFFECTS.items():
        rig.set_editor_property(name, asset)
    start = actors.spawn_actor_from_class(unreal.PlayerStart, unreal.Vector(0, 0, 60000))
    start.set_actor_label("Start (the player is placed on the ground by code)")
    world = unreal.get_editor_subsystem(unreal.UnrealEditorSubsystem).get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", unreal.EphemeraSliceGameMode)
    if not levels.save_current_level():
        raise RuntimeError("could not save the System map")
    log("map saved")


def terrain_attributes():
    """Dawnhollow's own values (slope, flow, young, ice, cover) baked on its cube grid for the ground material
    (EphemeraPlanetBuilder.build_terrain_attributes): (texture array, grid cells per face). Rebuilt when the
    imported height field or the grid changes."""
    body = definition("DA_Dawnhollow")
    shape = body.get_editor_property("terrain")
    field = shape.get_editor_property("height_field")
    if not field:
        raise RuntimeError("Dawnhollow has no imported height field yet: run the slice once without terrain attributes")
    resolution = int(shape.get_editor_property("face_resolution"))
    folder = f"{ROOT}/Planets/Dawnhollow"
    name = "T_dawnhollow_Attr"
    stamp = stamp_of("terrain-attr-v2", field.get_path_name(), ASSETS.get_metadata_tag(body, "EphemeraBakeStamp"), resolution)
    texture = unchanged(f"{folder}/{name}", stamp)
    if not texture:
        texture = unreal.EphemeraPlanetBuilder.build_terrain_attributes(body, folder, name)
        if not texture:
            raise RuntimeError("the terrain attributes could not be built")
        ASSETS.set_metadata_tag(texture, "EphemeraStamp", stamp)
        ASSETS.save_loaded_asset(texture)
        log(f"terrain attributes: rebuilt ({resolution} cells a face)")
    return texture, resolution


def terrain_heights():
    """Dawnhollow's heights on its cube grid for the near ground (EphemeraPlanetBuilder.build_terrain_heights):
    (texture array, grid cells per face). Rebuilt with the height field, as the attributes are."""
    body = definition("DA_Dawnhollow")
    shape = body.get_editor_property("terrain")
    field = shape.get_editor_property("height_field")
    if not field:
        raise RuntimeError("Dawnhollow has no imported height field yet")
    resolution = int(shape.get_editor_property("face_resolution"))
    folder = f"{ROOT}/Planets/Dawnhollow"
    name = "T_dawnhollow_Height"
    # (v2: the game reads the field through a cubic since 2026-10-18; the apron is read that way too.)
    stamp = stamp_of("terrain-height-v2", field.get_path_name(), ASSETS.get_metadata_tag(body, "EphemeraBakeStamp"), resolution)
    texture = unchanged(f"{folder}/{name}", stamp)
    if not texture:
        texture = unreal.EphemeraPlanetBuilder.build_terrain_heights(body, folder, name)
        if not texture:
            raise RuntimeError("the terrain heights could not be built")
        ASSETS.set_metadata_tag(texture, "EphemeraStamp", stamp)
        ASSETS.save_loaded_asset(texture)
        log(f"terrain heights: rebuilt ({resolution} cells a face)")
    return texture, resolution


def ground_grid_mesh():
    """The near ground's grid: the near sea's kind of mesh, finer and wider (UEphemeraPlanetBuilder.build_sea_grid)."""
    folder = f"{ROOT}/Planets"
    name = "SM_GroundGrid"
    stamp = stamp_of("ground-grid-v1", GROUND_GRID)
    mesh = unchanged(f"{folder}/{name}", stamp)
    if not mesh:
        mesh = unreal.EphemeraPlanetBuilder.build_sea_grid(folder, name, GROUND_GRID["rings"], GROUND_GRID["half_cells"], GROUND_GRID["spacing_m"])
        if not mesh:
            raise RuntimeError("the ground grid could not be built")
        ASSETS.set_metadata_tag(mesh, "EphemeraStamp", stamp)
        ASSETS.save_loaded_asset(mesh)
        log("ground grid: rebuilt")
    return mesh


def dress_ground_grid(body, near, far):
    """Gives Dawnhollow its near ground (UEphemeraGroundSurfaceComponent): the grid, its material, and the
    chunks' material with the hole."""
    set_all(body, {"ground_grid_mesh": ground_grid_mesh(), "ground_grid_material": near, "terrain_far_material": far,
                   "ground_grid_rings": GROUND_GRID["rings"], "ground_grid_half_cells": GROUND_GRID["half_cells"],
                   "ground_grid_spacing_m": GROUND_GRID["spacing_m"]})


def main():
    for folder in ("Surfaces", "Materials", "Bodies", "Planets", "Maps", "Input"):
        ASSETS.make_directory(f"{ROOT}/{folder}")
    surfaces = {name: import_surface(name, relative) for name, relative in SURFACES.items()}
    global SURFACE_SETS
    SURFACE_SETS = surfaces
    # Dawnhollow's ground, three ways: as the terrain chunks draw it, the same with the near ground's hole,
    # and the near ground itself (UEphemeraGroundSurfaceComponent).
    dawn_ground = ({"moss": surfaces["moss"], "field": surfaces["field"], "sand": surfaces["sand"], "rock": surfaces["basalt"],
                    "snow": surfaces["snow"]},
                   {"moss": surfaces["moss_far"], "field": surfaces["field_far"], "sand": surfaces["sand_far"], "rock": surfaces["basalt_far"],
                    "snow": surfaces["snow_far"]})
    dawn_options = dict(rock_start=0.34, snow_line_m=400.0, attributes=terrain_attributes())
    materials = {
        "dawnhollow": make_planet_material("M_Planet_Dawnhollow", *dawn_ground, **dawn_options),
        "dawnhollow_far": make_planet_material("M_Planet_Dawnhollow_Far", *dawn_ground, variant="far", **dawn_options),
        "dawnhollow_near": make_planet_material("M_Ground_Dawnhollow_Near", *dawn_ground, variant="near", land=terrain_heights(),
                                                **dawn_options),
        # Hush: pale dusty highlands and dark level maria, bare rock on the steep crater walls.
        # A little darker than the Moon's own two albedos (0.12 and 0.07): the camera in space is
        # set for Dawnhollow's dark lava, and at that setting true lunar grey burns out.
        "hush": make_moon_material("M_Planet_Hush", surfaces["regolith"], surfaces["moonrock"],
                                   highland=(0.105, 0.102, 0.096), mare=(0.047, 0.046, 0.044)),
        "star": make_star_material(),
        "clouds": make_cloud_material(),
        "underlay": make_underlay_material((0.100, 0.097, 0.092)),
    }
    global STARS, MIST, EFFECTS
    STARS = make_stars_material()
    MIST = make_mist_material()
    EFFECTS = make_effects()
    EFFECTS["underwater_material"] = make_underwater_material()
    config = unreal.EphemeraInputBuilder.build_input_assets(f"{ROOT}/Input")
    log(f"input config {config}")
    import sys
    sys.path.insert(0, str(PROJECT / "Tools"))
    import build_audio
    log(f"sounds {build_audio.build()}")
    bodies = build_bodies(materials)
    build_map(bodies)
    log("done")


main()
