extends Node
## Run with Godot's headless binary and --path ../ephemera. Source game is read-only.

class SnapshotMain extends Main:
	func _ready() -> void:
		pass

var destination := ""
var main: Main
var groups: Array = []
var meshes: Array = []
var placements: Array = []
var colliders: Array = []
var interactions: Array = []
var lights: Array = []
var group_nodes := {}
var mesh_ids := {}
var body_ids := {}
var failures: Array = []
var material_cache := {}
var anchor_overrides := {}

func _ready() -> void:
	var args := OS.get_cmdline_user_args()
	if args.is_empty():
		printerr("Usage: --script export_world.gd -- /absolute/output/directory")
		get_tree().quit(2)
		return
	destination = args[0]
	call_deferred("_export")

func vec(v: Vector3) -> Array:
	return [v.x, v.y, v.z]

func xf(t: Transform3D) -> Dictionary:
	return {"p": vec(t.origin), "x": vec(t.basis.x), "y": vec(t.basis.y), "z": vec(t.basis.z)}

func json_value(v: Variant) -> Variant:
	if v is Vector3:
		return vec(v)
	if v is Color:
		return [v.r, v.g, v.b, v.a]
	if v is Transform3D:
		return xf(v)
	if v is Dictionary:
		var d := {}
		for k in v:
			d[str(k)] = json_value(v[k])
		return d
	if v is Array:
		var a := []
		for q in v:
			a.append(json_value(q))
		return a
	return v

func write_json(name: String, data: Variant) -> void:
	var f := FileAccess.open(destination.path_join(name), FileAccess.WRITE)
	if f == null:
		failures.append("Cannot write " + name)
		return
	f.store_string(JSON.stringify(json_value(data)))

func add_group(n: Node3D, kind: String, data := {}) -> String:
	if group_nodes.has(n):
		return group_nodes[n]
	var id := "g%05d" % groups.size()
	var parent := ""
	var a := n.get_parent()
	while a != null:
		if group_nodes.has(a):
			parent = group_nodes[a]
			break
		a = a.get_parent()
	var transform := n.global_transform
	if parent != "":
		transform = (a as Node3D).global_transform.affine_inverse() * n.global_transform
	groups.append({"id": id, "parent": parent, "name": str(n.name), "kind": kind,
		"transform": xf(transform), "data": data})
	group_nodes[n] = id
	return id

func body_data(b: CelestialBody) -> Dictionary:
	var data := {}
	for key in ["body_id", "display_name", "radius", "surface_gravity", "max_gravity",
		"soi_radius", "has_oxygen", "atmosphere_height", "is_star", "orbit_radius",
		"orbit_period", "orbit_phase", "orbit_ecc", "orbit_tilt", "orbit_peri_angle",
		"orbit_peri_time", "spin_period", "spin_phase", "axial_tilt", "water_radius",
		"hazard_radius", "hazard_dps", "dilation_depths", "water_buoyancy", "dry_zones", "map_color"]:
		data[key] = b.get(key)
	data["orbit_parent"] = b.orbit_parent.body_id if b.orbit_parent else ""
	data["visual_parent"] = b.visual_parent.body_id if b.visual_parent else ""
	data["custom_transform"] = b.transform_fn.is_valid()
	if b.transform_fn.is_valid():
		# Custom frame paths (drifting seed islands) retain one-second samples for the day.
		var samples := []
		for second in 1321:
			samples.append(xf(b.transform_at(float(second))))
		data["samples"] = samples
	return data

func classify(n: Node) -> void:
	if n is CelestialBody:
		body_ids[n.body_id] = add_group(n, "body", body_data(n))
	elif n is Ship:
		add_group(n, "ship", {"pilot_eye": xf(Assets.rel_transform(n.pilot_eye, n)) if n.pilot_eye else xf(Transform3D.IDENTITY)})
	elif n is NPC:
		add_group(n, "npc", {"id": n.npc_id, "dayling": n.is_dayling,
			"fixed_age": n.fixed_age, "cocoon_at": n.cocoon_at})
		if n.collider:
			anchor_overrides[n.collider] = n
	elif n is MemoryStone:
		add_group(n, "text", {"id": n.text_id, "kind": n.kind})
	elif n is Discovery:
		add_group(n, "discovery", {"id": n.log_id, "radius": n.radius})
	elif n is QuantumShard:
		add_group(n, "quantum", {"id": n.tree_id, "anchors": n.anchors, "at": n.at})
	elif n is Probe:
		add_group(n, "probe", {"heart": xf(n.heart_xf), "crash": xf(n.crash_xf)})
	elif n is Gulper:
		add_group(n, "gulper", {"center_local": n.center_local, "radius": n.radius,
			"depth_r": n.depth_r, "city_local": n.city_local, "city_avoid": n.city_avoid, "angle": n.angle})
	elif n is DriftTail:
		add_group(n, "drift_tail")
	elif n is Campfire:
		add_group(n, "campfire")
	elif n is WinchLift and n.lift:
		add_group(n.lift, "lift", {"travel": n.travel, "rest": xf(n.rest)})
	elif n is QuantumRing:
		var ring_data := {"slots": n.slots, "gap": n.gap, "trees": []}
		for tree in n.trees:
			ring_data.trees.append(add_group(tree, "quantum_tree") if tree != null else "")
		add_group(n, "quantum_ring", ring_data)
	for c in n.get_children():
		classify(c)

func shader_kind(m: Material) -> String:
	if m is ShaderMaterial and m.shader:
		return m.shader.resource_path.get_file().get_basename()
	return ""

func standard_material(m: Material) -> StandardMaterial3D:
	if m is StandardMaterial3D:
		# Never mutate the source resource.
		return m.duplicate()
	var out := StandardMaterial3D.new()
	out.roughness = 0.8
	out.cull_mode = BaseMaterial3D.CULL_DISABLED
	var kind := shader_kind(m)
	out.resource_name = "UE_" + kind
	var colors := {"terrain": Color(0.36, 0.42, 0.23), "water": Color(0.06, 0.25, 0.35, 0.5),
		"sun": Color(1.0, 0.48, 0.12), "gas_giant": Color(0.67, 0.44, 0.26),
		"rings": Color(0.8, 0.67, 0.4, 0.6), "ice": Color(0.45, 0.7, 0.82),
		"hologram": Color(0.2, 0.9, 0.82, 0.5), "clouds": Color(0.9, 0.83, 0.7, 0.25),
		"flame": Color(1.0, 0.4, 0.05), "flame_stylized": Color(1.0, 0.5, 0.08)}
	out.albedo_color = colors.get(kind, Color(0.6, 0.68, 0.4))
	if m is ShaderMaterial:
		for parameter in ["albedo_tex", "albedo_texture", "texture_albedo"]:
			var texture = m.get_shader_parameter(parameter)
			if texture is Texture2D:
				out.albedo_texture = texture
		var tint = m.get_shader_parameter("albedo_color")
		if tint is Color:
			out.albedo_color = tint
		elif tint is Vector4:
			out.albedo_color = Color(tint.x, tint.y, tint.z, tint.w)
	if kind in ["terrain", "foliage", "foliage_card"]:
		out.vertex_color_use_as_albedo = true
		if kind != "terrain":
			out.vertex_color_use_as_albedo = bool(m.get_shader_parameter("use_vc"))
	if kind == "foliage_card":
		out.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA_SCISSOR
	if kind in ["water", "rings", "clouds", "hologram"]:
		out.transparency = BaseMaterial3D.TRANSPARENCY_ALPHA
	if kind in ["sun", "flame", "flame_stylized", "hologram"]:
		out.emission_enabled = true
		out.emission = out.albedo_color
		out.emission_energy_multiplier = 4.0
	return out

func export_mesh(mesh: Mesh, overrides: Array = [], collision := false) -> String:
	var material_key := ""
	for m in overrides:
		material_key += str(m.get_instance_id() if m else 0) + "_"
	var key := str(mesh.get_instance_id()) + ":" + material_key + (":collision" if collision else "")
	if mesh_ids.has(key):
		return mesh_ids[key]
	var id := "m%05d" % meshes.size()
	mesh_ids[key] = id
	var copy := ArrayMesh.new()
	var kinds := []
	for surface in mesh.get_surface_count():
		var arrays := mesh.surface_get_arrays(surface)
		var material: Material = overrides[surface] if surface < overrides.size() else mesh.surface_get_material(surface)
		var kind := shader_kind(material)
		kinds.append(kind)
		# Terrain stores six splat weights in COLOR and UV; bake these into actual colors.
		if kind == "terrain" and arrays[Mesh.ARRAY_COLOR] != null:
			var colors: PackedColorArray = arrays[Mesh.ARRAY_COLOR].duplicate()
			var uv: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV]
			var uv2: PackedVector2Array = arrays[Mesh.ARRAY_TEX_UV2]
			var tints = material.get_shader_parameter("layer_tint")
			var palette := [Color(0.34, 0.4, 0.17), Color(0.55, 0.43, 0.24), Color(0.45, 0.52, 0.26),
				Color(0.35, 0.33, 0.3), Color(0.31, 0.29, 0.27), Color(0.5, 0.45, 0.35)]
			for i in colors.size():
				var w := [colors[i].r, colors[i].g, colors[i].b, colors[i].a, uv[i].x, uv[i].y]
				var c := Color(0, 0, 0, 1)
				for j in 6:
					var p: Color = palette[j]
					if tints != null and j < tints.size():
						var tint = tints[j]
						if tint is Color:
							p *= tint
						elif tint is Vector4:
							p *= Color(tint.x, tint.y, tint.z)
					c.r += p.r * w[j]
					c.g += p.g * w[j]
					c.b += p.b * w[j]
				if uv2 != null and i < uv2.size():
					c.r *= uv2[i].x
					c.g *= uv2[i].x
					c.b *= uv2[i].x
				colors[i] = c
			arrays[Mesh.ARRAY_COLOR] = colors
			arrays[Mesh.ARRAY_TEX_UV] = null
			arrays[Mesh.ARRAY_TEX_UV2] = null
		var primitive: int = mesh.surface_get_primitive_type(surface) if mesh is ArrayMesh else Mesh.PRIMITIVE_TRIANGLES
		copy.add_surface_from_arrays(primitive, arrays)
		copy.surface_set_material(surface, standard_material(material))
	var node := MeshInstance3D.new()
	node.name = id
	node.mesh = copy
	var document := GLTFDocument.new()
	var state := GLTFState.new()
	var error := document.append_from_scene(node, state)
	if error == OK:
		error = document.write_to_filesystem(state, destination.path_join("Meshes/" + id + ".glb"))
	node.free()
	if error != OK:
		failures.append("Mesh " + id + ": error " + str(error))
	meshes.append({"id": id, "file": "Meshes/" + id + ".glb", "kinds": kinds, "collision": collision})
	if meshes.size() % 100 == 0:
		print("Exported ", meshes.size(), " unique meshes")
	return id

func relative(n: Node3D, anchor: Node3D) -> Transform3D:
	return anchor.global_transform.affine_inverse() * n.global_transform

func visit(n: Node, anchor: Node3D = null) -> void:
	if n is Player or n is Scout:
		return
	if group_nodes.has(n):
		anchor = n
	if anchor_overrides.has(n):
		anchor = anchor_overrides[n]
	if anchor != null and n is Node3D:
		var group: String = group_nodes[anchor]
		var transform := relative(n, anchor)
		var visible_for_export: bool = n.is_visible_in_tree() or bool(anchor.get_meta("ue_capture_hidden", false))
		if n is MeshInstance3D and n.mesh and visible_for_export:
			var kind := shader_kind(n.material_override)
			# Godot screen-space atmosphere/shadow proxies are replaced by Unreal lighting.
			if not kind in ["atmosphere", "planet_haze", "corona", "sun_halo", "lens_flare"] \
				and not str(n.name).contains("ShadowProxy") and n.cast_shadow != GeometryInstance3D.SHADOW_CASTING_SETTING_SHADOWS_ONLY:
				var overrides := []
				for s in n.mesh.get_surface_count():
					var mat: Material = n.material_override if n.material_override else n.get_surface_override_material(s)
					overrides.append(mat if mat else n.mesh.surface_get_material(s))
				placements.append({"group": group, "mesh": export_mesh(n.mesh, overrides), "transform": xf(transform)})
		elif n is MultiMeshInstance3D and n.multimesh and visible_for_export:
			var mm: MultiMesh = n.multimesh
			var count := mm.instance_count if mm.visible_instance_count < 0 else mm.visible_instance_count
			var transforms := []
			for i in count:
				transforms.append(xf(transform * mm.get_instance_transform(i)))
			var overrides := []
			for s in mm.mesh.get_surface_count():
				overrides.append(n.material_override if n.material_override else mm.mesh.surface_get_material(s))
			placements.append({"group": group, "mesh": export_mesh(mm.mesh, overrides), "instances": transforms})
		elif n is CollisionShape3D and n.shape and not n.disabled and n.get_parent() is PhysicsBody3D:
			var owner_body: PhysicsBody3D = n.get_parent()
			if owner_body.collision_layer & 1:
				var shape: Shape3D = n.shape
				var data := {"group": group, "transform": xf(transform)}
				if shape is ConcavePolygonShape3D:
					var faces: PackedVector3Array = shape.get_faces()
					var arrays := []
					arrays.resize(Mesh.ARRAY_MAX)
					arrays[Mesh.ARRAY_VERTEX] = faces
					var cm := ArrayMesh.new()
					cm.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, arrays)
					data["kind"] = "mesh"
					data["mesh"] = export_mesh(cm, [], true)
				elif shape is BoxShape3D:
					data["kind"] = "box"
					data["size"] = vec(shape.size)
				elif shape is SphereShape3D:
					data["kind"] = "sphere"
					data["radius"] = shape.radius
				elif shape is CapsuleShape3D or shape is CylinderShape3D:
					data["kind"] = "capsule"
					if shape is CylinderShape3D:
						data["kind"] = "cylinder"
					data["radius"] = shape.radius
					data["height"] = shape.height
				elif shape is ConvexPolygonShape3D:
					data["kind"] = "convex"
					var points := []
					for point in shape.points:
						points.append(vec(point))
					data["points"] = points
				if data.has("kind"):
					colliders.append(data)
		elif n is Interactable:
			var data := {"group": group, "transform": xf(transform), "prompt": n.get_prompt(), "enabled": n.enabled}
			if n.has_meta("ue_action"):
				data["action"] = n.get_meta("ue_action")
			for child in n.get_children():
				if child is CollisionShape3D and child.shape is SphereShape3D:
					data["radius"] = child.shape.radius
			interactions.append(data)
		elif n is OmniLight3D:
			lights.append({"group": group, "transform": xf(transform), "color": n.light_color,
				"energy": n.light_energy, "range": n.omni_range, "shadow": n.shadow_enabled})
	for c in n.get_children():
		visit(c, anchor)

func _export() -> void:
	DirAccess.make_dir_recursive_absolute(destination.path_join("Meshes"))
	main = SnapshotMain.new()
	get_tree().root.add_child(main)
	main.ui_layer = CanvasLayer.new()
	main.add_child(main.ui_layer)
	main._build()
	main.universe.paused = true
	main.process_mode = Node.PROCESS_MODE_DISABLED
	var exhibits: Array = main.dawn_res.get("exhibits", [])
	for i in mini(exhibits.size(), 5):
		for child in exhibits[i].get_children():
			if child is Interactable:
				child.set_meta("ue_action", {"type": "dialogue", "id": "exhibit_%d" % (i + 1)})
	var sign: Node = main.dawn_res.get("sign_tomorrow")
	if sign:
		for child in sign.get_children():
			if child is Interactable:
				child.set_meta("ue_action", {"type": "dialogue", "id": "sign_tomorrow"})
	print("World constructed; exporting native scene data")
	for body in main.universe.bodies:
		classify(body)
	classify(main.ship)
	var blooms: Array = main.planet_builders.dawnhollow.nightblooms
	for mode in blooms.size():
		for bloom in blooms[mode]:
			bloom.set_meta("ue_capture_hidden", true)
			add_group(bloom, "nightbloom", {"night": mode == 1})
	for body in main.universe.bodies:
		visit(body)
	visit(main.ship)
	var calibration_arrays := []
	calibration_arrays.resize(Mesh.ARRAY_MAX)
	calibration_arrays[Mesh.ARRAY_VERTEX] = PackedVector3Array([Vector3.ZERO, Vector3(1,0,0), Vector3(0,2,0), Vector3(0,0,-3)])
	calibration_arrays[Mesh.ARRAY_INDEX] = PackedInt32Array([0,2,1,0,1,3,0,3,2,1,2,3])
	var calibration := ArrayMesh.new()
	calibration.add_surface_from_arrays(Mesh.PRIMITIVE_TRIANGLES, calibration_arrays)
	var calibration_id := export_mesh(calibration)
	DirAccess.copy_absolute(destination.path_join("Meshes/" + calibration_id + ".glb"), destination.path_join("axis_calibration.glb"))
	write_json("world.json", {"schema": 1, "coordinates": "Godot RH Y-up metres",
		"source": "ephemera", "groups": groups, "bodies": body_ids, "meshes": meshes,
		"placements": placements, "colliders": colliders, "interactions": interactions, "lights": lights,
		"spawn_body": "dawnhollow", "spawn": xf(main.dawn_res.wake_xf), "ship_spawn": xf(main.dawn_res.ship_xf),
		"failures": failures})
	write_json("story.json", {"log": Story.LOG, "dialogue": Story.DIALOGUE, "texts": Story.TEXTS})
	var reference := []
	for body in main.universe.bodies:
		var positions := []
		for t in [0.0, 37.0, 420.0, 1080.0, 1319.0]:
			positions.append({"time": t, "position": vec(body.orbit_position(t))})
		reference.append({"body_id": body.body_id, "positions": positions})
	write_json("reference.json", reference)
	print("EXPORT COMPLETE: ", groups.size(), " groups, ", meshes.size(), " meshes, ",
		placements.size(), " placements, ", colliders.size(), " colliders, ", interactions.size(), " interactions")
	get_tree().quit(0 if failures.is_empty() else 1)
