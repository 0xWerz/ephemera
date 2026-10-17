"""Prints where some of Dawnhollow's standing rocks are (for pointing a test camera at them). Run in the editor:
python3 -B Tools/unreal.py slice --script Tools/where_rocks.py"""
import unreal

body = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).load_asset("/Game/Ephemera/Bodies/DA_Dawnhollow")
for index, batch in enumerate(body.get_editor_property("scatter")):
    instances = batch.get_editor_property("instances")
    mesh = batch.get_editor_property("mesh")
    material = batch.get_editor_property("material")
    step = max(len(instances) // 4, 1)
    for transform in list(instances)[::step][:4]:
        p = transform.translation
        r = (p.x * p.x + p.y * p.y + p.z * p.z) ** 0.5
        s = transform.scale3d
        unreal.log(f"[rock] batch {index} {mesh.get_name()} {material.get_name() if material else '-'} n={len(instances)} dir {p.x / r:.5f},{p.y / r:.5f},{p.z / r:.5f} alt_m {(r - 500000.0) / 100.0:.1f} scale {s.x:.1f},{s.z:.1f}")
unreal.log("[slice] done")
