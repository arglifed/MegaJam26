"""
MegaJam26 Issue #5: imports every Meshy mesh in RawAssets/Art into /Game/MegaJam26/Art (flat, per AGENTS.md).

Run the texture downscale first (1K copies keep the LFS footprint small), then this script headless with the editor
CLOSED, from the project root (PowerShell):

    python Scripts\\downscale_art_textures.py
    & "E:\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "$PWD\\MegaJam26.uproject" -ExecutePythonScript="$PWD\\Scripts\\import_issue5_assets.py" `
        -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput

On a 16 GB machine add  -ini:Engine:[DevOptions.Shaders]:NumUnusedShaderCompilingThreads=6  (2 shader workers
instead of 5): the default worker count exhausted the page file on the first run.

Search the log for "[ArtImport]". Every asset is saved as soon as it is created, and a re-run SKIPS assets that already
exist (so a crashed run resumes). To rebuild from scratch, delete /Game/MegaJam26/Art (or pass FORCE=1 in the env);
FORCE_MESHES=1 re-imports only the meshes and their material instances.
Meshes are rebuilt from their scaled mesh description, so shading comes out faceted (flat), matching the art brief.

Source layout:  RawAssets/Art/<Category>/<Name>/<Name>.fbx  (+ <Name>_<map>.png, 1K copies in RawAssets/Art/.import_1k)
Creates, per mesh <Name> (vehicles get a Vehicle_ prefix):
    SM_<Name>                       static mesh, box simple collision, sized below
    MI_<Name>                       instance of M_MeshyPBR
    T_<Name>_BaseColor / _Normal / _Metallic / _Roughness   (+ _Emissive for the few glowing assets)
plus the shared master material M_MeshyPBR (BaseColor * BaseColorTint, Normal, Metallic, Roughness,
Emissive * EmissiveStrength).

Sizing (Docs/Issue5-AudioArt-Handoff.md: C++ places meshes by their centre and scales them by size / 100):
    Obstacles  -> stretched to exactly 100 x 100 x 100 cm (non-uniform), so the collision box matches the art
    Props / SetPieces / Dressing -> uniform, longest side 100 cm
    Vehicles   -> uniform, real-world length (VEHICLE_LENGTH_CM)
Pivots: Meshy exports are already centred on their bounding box; the script re-centres if one is off by > 0.5 cm.
"""

import os
import traceback

import unreal

ART = "/Game/MegaJam26/Art"
PROJECT_DIR = unreal.Paths.convert_relative_path_to_full(unreal.Paths.project_dir())
RAW = os.path.join(PROJECT_DIR, "RawAssets", "Art")
TEX_DIR = os.path.join(RAW, ".import_1k")
CATEGORIES = ("Obstacles", "Props", "SetPieces", "Dressing", "Vehicles")
EMISSIVE = {"Dressing_Final_FloatingCrystal", "Obstacle_Chase_IceBoulders", "Dressing_Noir_StreetLamp"}
VEHICLE_LENGTH_CM = {"Motorcycle": 210.0, "Horse": 240.0, "SpyCar": 450.0, "Snowboard": 155.0, "HangGlider": 900.0}
MAPS = {  # file suffix -> (asset suffix, material parameter)
    "base_color": ("BaseColor", "BaseColor"),
    "normal": ("Normal", "Normal"),
    "metallic": ("Metallic", "Metallic"),
    "roughness": ("Roughness", "Roughness"),
    "emission": ("Emissive", "Emissive"),
}

eal = unreal.EditorAssetLibrary
mel = unreal.MaterialEditingLibrary
tools = unreal.AssetToolsHelpers.get_asset_tools()
sme = unreal.get_editor_subsystem(unreal.StaticMeshEditorSubsystem)
errors = []
report = []
FORCE = os.environ.get("FORCE") == "1"


def exists(path):
    return (not FORCE) and eal.does_asset_exist(path)


def save(asset):
    eal.save_loaded_asset(asset, only_if_is_dirty=False)


def log(msg):
    unreal.log("[ArtImport] " + msg)


def err(msg):
    errors.append(msg)
    unreal.log_error("[ArtImport] ERROR: " + msg)


def replace(path):
    if eal.does_asset_exist(path):
        eal.delete_asset(path)


def run_task(filename, dest_name, options=None):
    task = unreal.AssetImportTask()
    task.set_editor_property("filename", filename)
    task.set_editor_property("destination_path", ART)
    task.set_editor_property("destination_name", dest_name)
    task.set_editor_property("automated", True)
    task.set_editor_property("replace_existing", True)
    task.set_editor_property("save", False)
    if options is not None:
        task.set_editor_property("options", options)
    tools.import_asset_tasks([task])
    return eal.load_asset(ART + "/" + dest_name)


# ---------------------------------------------------------------------------------------------------------------------
# Textures
# ---------------------------------------------------------------------------------------------------------------------
def import_texture(name, map_key):
    suffix, _ = MAPS[map_key]
    src = os.path.join(TEX_DIR, "%s_%s.png" % (name, map_key))
    if not os.path.isfile(src):
        return None
    asset_name = "T_%s_%s" % (asset_base(name), suffix)
    if exists(ART + "/" + asset_name):
        return eal.load_asset(ART + "/" + asset_name)
    replace(ART + "/" + asset_name)
    tex = run_task(src, asset_name)
    if not tex:
        err("texture import failed: " + src)
        return None
    if map_key == "normal":
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_NORMALMAP)
        tex.set_editor_property("srgb", False)
        tex.set_editor_property("flip_green_channel", True)  # glTF/Meshy normals are OpenGL (+Y); Unreal is DirectX
    elif map_key in ("metallic", "roughness"):
        tex.set_editor_property("compression_settings", unreal.TextureCompressionSettings.TC_MASKS)
        tex.set_editor_property("srgb", False)
    save(tex)
    return tex


# ---------------------------------------------------------------------------------------------------------------------
# Master material
# ---------------------------------------------------------------------------------------------------------------------
def build_master(defaults):
    path = ART + "/M_MeshyPBR"
    if exists(path):
        return eal.load_asset(path)
    replace(path)
    mat = tools.create_asset("M_MeshyPBR", ART, unreal.Material, unreal.MaterialFactoryNew())
    S = unreal.MaterialSamplerType

    def tex_param(param, tex, sampler, y):
        e = mel.create_material_expression(mat, unreal.MaterialExpressionTextureSampleParameter2D, -700, y)
        e.set_editor_property("parameter_name", param)
        e.set_editor_property("texture", tex)
        e.set_editor_property("sampler_type", sampler)
        return e

    base = tex_param("BaseColor", defaults["base_color"], S.SAMPLERTYPE_COLOR, -400)
    tint = mel.create_material_expression(mat, unreal.MaterialExpressionVectorParameter, -700, -600)
    tint.set_editor_property("parameter_name", "BaseColorTint")
    tint.set_editor_property("default_value", unreal.LinearColor(1, 1, 1, 1))
    base_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, -450)
    mel.connect_material_expressions(base, "RGB", base_mul, "A")
    mel.connect_material_expressions(tint, "", base_mul, "B")
    mel.connect_material_property(base_mul, "", unreal.MaterialProperty.MP_BASE_COLOR)

    metal = tex_param("Metallic", defaults["metallic"], S.SAMPLERTYPE_MASKS, -100)
    mel.connect_material_property(metal, "R", unreal.MaterialProperty.MP_METALLIC)
    rough = tex_param("Roughness", defaults["roughness"], S.SAMPLERTYPE_MASKS, 150)
    mel.connect_material_property(rough, "R", unreal.MaterialProperty.MP_ROUGHNESS)
    normal = tex_param("Normal", defaults["normal"], S.SAMPLERTYPE_NORMAL, 400)
    mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    emis = tex_param("Emissive", defaults["emission"], S.SAMPLERTYPE_COLOR, 650)
    strength = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 900)
    strength.set_editor_property("parameter_name", "EmissiveStrength")
    strength.set_editor_property("default_value", 0.0)
    emis_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 700)
    mel.connect_material_expressions(emis, "RGB", emis_mul, "A")
    mel.connect_material_expressions(strength, "", emis_mul, "B")
    mel.connect_material_property(emis_mul, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    save(mat)
    log("Built " + path)
    return mat


def build_instance(name, master, textures):
    mi_name = "MI_" + asset_base(name)
    replace(ART + "/" + mi_name)
    mi = tools.create_asset(mi_name, ART, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master)
    for map_key, tex in textures.items():
        mel.set_material_instance_texture_parameter_value(mi, MAPS[map_key][1], tex)
    if "emission" in textures:
        mel.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", 1.0)
    save(mi)
    return mi


# ---------------------------------------------------------------------------------------------------------------------
# Meshes
# ---------------------------------------------------------------------------------------------------------------------
def asset_base(name):
    return ("Vehicle_" + name) if name in VEHICLE_LENGTH_CM else name


def fbx_options(translation=None):
    ui = unreal.FbxImportUI()
    ui.set_editor_property("import_mesh", True)
    ui.set_editor_property("import_as_skeletal", False)
    ui.set_editor_property("import_animations", False)
    ui.set_editor_property("import_materials", False)
    ui.set_editor_property("import_textures", False)
    ui.set_editor_property("mesh_type_to_import", unreal.FBXImportType.FBXIT_STATIC_MESH)
    data = ui.get_editor_property("static_mesh_import_data")
    data.set_editor_property("combine_meshes", True)
    data.set_editor_property("auto_generate_collision", False)
    data.set_editor_property("generate_lightmap_u_vs", True)
    if translation is not None:
        data.set_editor_property("import_translation", translation)
    return ui


def bounds(mesh):
    box = mesh.get_bounding_box()
    mn, mx = box.min, box.max
    size = unreal.Vector(mx.x - mn.x, mx.y - mn.y, mx.z - mn.z)
    centre = unreal.Vector((mx.x + mn.x) * 0.5, (mx.y + mn.y) * 0.5, (mx.z + mn.z) * 0.5)
    return size, centre


def fmt(v):
    return "(%.1f, %.1f, %.1f)" % (v.x, v.y, v.z)


def import_mesh(category, name):
    fbx = os.path.join(RAW, category, name, name + ".fbx")
    sm_name = "SM_" + asset_base(name)
    path = ART + "/" + sm_name
    replace(path)
    mesh = run_task(fbx, sm_name, fbx_options())
    if not mesh:
        err("mesh import failed: " + fbx)
        return None
    size, centre = bounds(mesh)
    if max(abs(centre.x), abs(centre.y), abs(centre.z)) > 0.5:  # re-centre the pivot on the bounding box
        log("%s: re-centring, source centre %s" % (sm_name, fmt(centre)))
        replace(path)
        mesh = run_task(fbx, sm_name, fbx_options(unreal.Vector(-centre.x, -centre.y, -centre.z)))
        size, centre = bounds(mesh)

    if category == "Obstacles":
        scale = unreal.Vector(100.0 / size.x, 100.0 / size.y, 100.0 / size.z)
    else:
        target = VEHICLE_LENGTH_CM.get(name, 100.0)
        s = target / max(size.x, size.y, size.z)
        scale = unreal.Vector(s, s, s)
    # Bake the scale into the source vertices. (MeshBuildSettings.build_scale3d only reached the collision box in
    # UE 5.8 headless runs; the render bounds stayed unscaled, so it is not used.)
    desc = mesh.get_static_mesh_description(0)
    done, vid = 0, 0
    total = desc.get_vertex_count()
    while done < total:
        v = unreal.VertexID(vid)
        if desc.is_vertex_valid(v):
            p = desc.get_vertex_position(v)
            desc.set_vertex_position(v, unreal.Vector(p.x * scale.x, p.y * scale.y, p.z * scale.z))
            done += 1
        vid += 1
    mesh.build_from_static_mesh_descriptions([desc], build_simple_collision=False)

    sme.remove_collisions(mesh)
    sme.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)

    final_size, final_centre = bounds(mesh)
    tris = sme.get_number_verts(mesh, 0)
    report.append("%-40s src %s  ->  final %s cm, centre %s, verts %d"
                  % (sm_name, fmt(size), fmt(final_size), fmt(final_centre), tris))
    return mesh


# ---------------------------------------------------------------------------------------------------------------------
def main():
    if not os.path.isdir(TEX_DIR):
        err("missing %s: run Scripts/downscale_art_textures.py first" % TEX_DIR)
        return
    # Use the classic FBX importer: its options (no materials/textures, no auto collision, translation) are explicit.
    unreal.SystemLibrary.execute_console_command(None, "Interchange.FeatureFlags.Import.FBX 0")

    entries = []
    for cat in CATEGORIES:
        cat_dir = os.path.join(RAW, cat)
        if not os.path.isdir(cat_dir):
            continue
        for name in sorted(os.listdir(cat_dir)):
            if os.path.isfile(os.path.join(cat_dir, name, name + ".fbx")):
                entries.append((cat, name))
    log("Found %d meshes" % len(entries))

    all_textures = {}
    for _, name in entries:
        keys = [k for k in MAPS if k != "emission" or name in EMISSIVE]
        all_textures[name] = {k: t for k in keys for t in [import_texture(name, k)] if t}

    defaults = {}
    for k in MAPS:  # master defaults: the first imported texture of each kind
        defaults[k] = next((tx[k] for tx in all_textures.values() if k in tx), None)
    master = build_master(defaults)

    for cat, name in entries:
        if os.environ.get("FORCE_MESHES") != "1" and exists(ART + "/SM_" + asset_base(name)):
            log("skip (exists) SM_" + asset_base(name))
            continue
        try:
            mesh = import_mesh(cat, name)
            if not mesh:
                continue
            mi = build_instance(name, master, all_textures[name])
            for i in range(len(mesh.get_editor_property("static_materials"))):
                mesh.set_material(i, mi)
            save(mesh)
            log("saved " + mesh.get_path_name())
        except Exception as exc:  # noqa: BLE001 - keep going, the run is headless
            err("%s: %s\n%s" % (name, exc, traceback.format_exc()))

    eal.save_directory(ART, only_if_is_dirty=False, recursive=False)
    for line in report:
        log(line)


try:
    main()
except Exception as exc:  # noqa: BLE001
    err("%s\n%s" % (exc, traceback.format_exc()))
log("DONE with %d error(s)" % len(errors))
