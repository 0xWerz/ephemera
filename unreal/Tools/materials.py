"""Native Unreal material graphs for the source game's engine-specific shader surfaces."""
import unreal


def ensure_mesh_usage(material, nanite=False):
    if not material:
        raise RuntimeError("A rendered mesh has an empty material slot")
    edit = unreal.MaterialEditingLibrary
    usages = [unreal.MaterialUsage.MATUSAGE_INSTANCED_STATIC_MESHES]
    if nanite:
        usages.append(unreal.MaterialUsage.MATUSAGE_NANITE)
    changed = False
    for usage in usages:
        if edit.has_material_usage(material, usage):
            continue
        if isinstance(material, unreal.MaterialInstanceConstant):
            edit.set_material_usage_override(material, usage, True, True)
        elif isinstance(material, unreal.Material):
            edit.set_base_material_usage(material, usage, True)
        else:
            raise RuntimeError(f"Unsupported material type: {material.get_path_name()}")
        if not edit.has_material_usage(material, usage):
            raise RuntimeError(f"Could not enable {usage} on {material.get_path_name()}")
        changed = True
    if changed:
        unreal.get_editor_subsystem(unreal.EditorAssetSubsystem).save_loaded_asset(material)


def create_effect_material(kind, collection):
    name = "M_" + kind.title().replace("_", "")
    path = "/Game/Migrated/Materials/" + name
    assets = unreal.get_editor_subsystem(unreal.EditorAssetSubsystem)
    existing = assets.load_asset(path) if assets.does_asset_exist(path) else None
    revision = "2"
    if existing and assets.get_metadata_tag(existing, "EphemeraMaterialRevision") == revision:
        return existing
    material = existing or unreal.AssetToolsHelpers.get_asset_tools().create_asset(
        name, "/Game/Migrated/Materials", unreal.Material, unreal.MaterialFactoryNew()
    )
    edit = unreal.MaterialEditingLibrary
    if existing:
        edit.delete_all_material_expressions(material)
    material.set_editor_property("two_sided", True)

    def expr(cls):
        return edit.create_material_expression(material, cls, -600, 0)

    def constant(value, prop):
        node = expr(unreal.MaterialExpressionConstant)
        node.set_editor_property("r", value)
        edit.connect_material_property(node, "", prop)

    uv = expr(unreal.MaterialExpressionTextureCoordinate)
    time = expr(unreal.MaterialExpressionTime)
    world = expr(unreal.MaterialExpressionWorldPosition)
    origin = expr(unreal.MaterialExpressionObjectPositionWS)
    relative = expr(unreal.MaterialExpressionSubtract)
    edit.connect_material_expressions(world, "", relative, "A")
    edit.connect_material_expressions(origin, "", relative, "B")
    resonance = expr(unreal.MaterialExpressionCollectionParameter)
    resonance.set_editor_property("collection", collection)
    resonance.set_editor_property("parameter_name", "Resonance")

    def custom(code, output=unreal.CustomMaterialOutputType.CMOT_FLOAT3):
        node = expr(unreal.MaterialExpressionCustom)
        inputs = []
        for name in ("P", "UV", "T", "R"):
            input_ = unreal.CustomInput()
            input_.set_editor_property("input_name", name)
            inputs.append(input_)
        node.set_editor_property("inputs", inputs)
        node.set_editor_property("code", code)
        node.set_editor_property("output_type", output)
        for source, name in ((relative,"P"),(uv,"UV"),(time,"T"),(resonance,"R")):
            edit.connect_material_expressions(source, "", node, name)
        return node

    def mask(source, alpha=False):
        node = expr(unreal.MaterialExpressionComponentMask)
        for channel in ("r","g","b"):
            node.set_editor_property(channel, not alpha)
        node.set_editor_property("a", alpha)
        if not edit.connect_material_expressions(source,"",node,""):
            raise RuntimeError(f"Could not connect the {kind} color/opacity mask")
        return node

    if kind == "water":
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        normal = custom(
            "float a=UV.x*160+UV.y*71+T*0.6;"
            "float b=UV.x*39-UV.y*113-T*0.43;"
            "return normalize(float3(sin(a)*0.13+cos(b)*0.08,cos(a)*0.1+sin(b)*0.12,1));"
        )
        color = custom("return float3(0.018,0.16,0.23);")
        edit.connect_material_property(normal,"",unreal.MaterialProperty.MP_NORMAL)
        edit.connect_material_property(color,"",unreal.MaterialProperty.MP_BASE_COLOR)
        constant(0.12,unreal.MaterialProperty.MP_ROUGHNESS)
        constant(0.58,unreal.MaterialProperty.MP_OPACITY)
        constant(0.8,unreal.MaterialProperty.MP_SPECULAR)
    elif kind in ("flame", "flame_stylized"):
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        flame = custom(
            "float h=1-UV.y;"
            "float sway=sin(T*2.3+h*5)*0.055*h;"
            "float width=(1-h)*0.43*(0.8+0.2*sin(T*7+h*19));"
            "float a=saturate((width-abs(UV.x-0.5-sway))*14)*smoothstep(0,0.12,h);"
            "float3 c=lerp(float3(1,0.8,0.3),float3(0.9,0.09,0.005),h);"
            "return float4(c*4,a);",
            unreal.CustomMaterialOutputType.CMOT_FLOAT4
        )
        edit.connect_material_property(mask(flame),"",unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        edit.connect_material_property(mask(flame,True),"",unreal.MaterialProperty.MP_OPACITY)
    elif kind == "sun":
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        color = custom(
            "float3 p=normalize(P);"
            "float cells=sin(dot(p,float3(34,71,23))+T*.18)*sin(dot(p,float3(-51,17,43))-T*.11);"
            "return lerp(float3(1,.24,.025),float3(1,.75,.26),cells*.5+.5)*5;"
        )
        edit.connect_material_property(color,"",unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    elif kind == "gas_giant":
        color = custom(
            "float3 p=normalize(P);"
            "float b=sin(p.z*52+sin(p.x*12+T*.03)*.8)+sin(p.z*123-T*.04)*.25;"
            "return lerp(float3(.19,.12,.08),float3(.73,.57,.35),saturate(b*.35+.5));"
        )
        edit.connect_material_property(color,"",unreal.MaterialProperty.MP_BASE_COLOR)
        constant(0.85,unreal.MaterialProperty.MP_ROUGHNESS)
    elif kind == "rings":
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
        color = custom("return float3(.65,.46,.22)*(1+R*3);")
        edit.connect_material_property(color,"",unreal.MaterialProperty.MP_BASE_COLOR)
        glow = custom("return float3(.85,.49,.14)*R*3;")
        edit.connect_material_property(glow,"",unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        alpha = custom("return .35+.4*pow(sin(length(P)*.004)*.5+.5,3);",unreal.CustomMaterialOutputType.CMOT_FLOAT1)
        edit.connect_material_property(alpha,"",unreal.MaterialProperty.MP_OPACITY)
    elif kind == "starfield":
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        color = custom(
            "float3 p=normalize(P);float3 q=p*650;float3 cell=floor(q);"
            "float n=frac(sin(dot(cell,float3(127.1,311.7,74.7)))*43758.5453);"
            "float d=dot(frac(q)-.5,frac(q)-.5);"
            "float star=pow(saturate(1-d*5),18)*step(.998,n);"
            "float band=pow(saturate(1-abs(p.z+.15*sin(p.x*4)))*.98,28);"
            "return float3(.0003,.0005,.001)+float3(.82,.9,1)*star*2+float3(.004,.003,.005)*band;"
        )
        edit.connect_material_property(color,"",unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    elif kind == "hologram":
        material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_ADDITIVE)
        material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
        color = custom("return float3(.12,.8,.65)*(2+.3*sin(T*2+P.z*.04));")
        edit.connect_material_property(color,"",unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        constant(0.4,unreal.MaterialProperty.MP_OPACITY)
    else:
        raise ValueError(kind)
    edit.recompile_material(material)
    assets.set_metadata_tag(material, "EphemeraMaterialRevision", revision)
    assets.save_loaded_asset(material)
    return material
