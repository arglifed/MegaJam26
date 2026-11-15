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
ONLY=HangGlider (comma-separated raw names) re-imports just those models with their textures and instances.
Meshes are rebuilt from their scaled mesh description, so shading comes out faceted (flat), matching the art brief.

Source layout:  RawAssets/Art/<Category>/<Name>/<Name>.fbx  (+ <Name>_<map>.png, 1K copies in RawAssets/Art/.import_1k)
Creates, per mesh <Name> (vehicles get a Vehicle_ prefix):
    SM_<Name>                       static mesh, box simple collision, sized below
    MI_<Name>                       instance of M_MeshyPBR
    T_<Name>_BaseColor / _Normal / _Metallic / _Roughness   (+ _Emissive for the few glowing assets)
plus the shared master material M_MeshyPBR (masked, lit): BaseColor * BaseColorTint, Normal, Metallic, Roughness,
Emissive * EmissiveStrength, and the take contract the track generator drives:
    TakeIndex (0 = Take A, 1 = Take B), TakeColor (adds a glow; black by default), TakeGhosting (0 = always solid,
    1 = dither-fade to GhostOpacity while MPC_FlowState.ActiveTake is the other take), GhostGlow / TakeGlow.
FORCE_MASTER=1 rebuilds that graph in place (instances keep their parent). MASTER_ONLY=1 rebuilds just M_MeshyPBR and
saves nothing else. RoughnessMin (default 0) floors the roughness map, for instances that read too mirror-like.
Also creates the track surface: M_TrackSurface (world-aligned grid, so it survives any stretch), MI_FloorTile,
MI_WallSegment and SM_FloorTile / SM_WallSegment (copies of the engine cube: 100^3, centred, box collision).

Sizing (Docs/Issue5-AudioArt-Handoff.md: C++ places meshes by their centre and scales them by size / 100):
    Obstacles  -> stretched to exactly 100 x 100 x 100 cm (non-uniform), so the collision box matches the art
    Props / SetPieces / Dressing -> uniform, longest side 100 cm
    Vehicles   -> uniform, real-world length (VEHICLE_LENGTH_CM), turned to face +X (VEHICLE_BASIS)
Pivots: Meshy exports are already centred on their bounding box; the script re-centres if one is off by > 0.5 cm.
Vehicles are the exception (issue #56): pivot on the ground (Z = 0), centred in X and Y.
Existing meshes and material instances are re-imported in place, never deleted, so Blueprint references survive.
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
# Longest side in cm. The car and glider are smaller than life so they do not block the view down the corridor.
VEHICLE_LENGTH_CM = {"Motorcycle": 210.0, "Horse": 240.0, "SpyCar": 360.0, "Snowboard": 155.0, "HangGlider": 480.0}
# Turns each Meshy vehicle so its front faces +X with Z up (checked against vertex plots of the raw imports).
VEHICLE_BASIS = {
    "Motorcycle": lambda x, y, z: (-x, -y, z),   # front fork was at -X: half turn
    "Horse": lambda x, y, z: (y, -x, z),         # head was at +Y: quarter turn
    "SpyCar": lambda x, y, z: (-x, -y, z),       # play-test showed the grille at -X: half turn
    "Snowboard": lambda x, y, z: (x, y, z),      # symmetric
    # Regenerated 2026-11-14 (#87) as a real 3D delta wing: span along Y, nose at -X after import, so a half turn.
    # (The old flat cutout, kept in RawAssets/Art/Vehicles/HangGlider/_old_flat_cutout, needed (z, x, y).)
    "HangGlider": lambda x, y, z: (-x, -y, z),
    # Pickups are drawn pitched 90 degrees and squashed in Z (they were flat cylinders), so the frame's face goes in
    # the local XY plane with its long side along Y; POST_SCALE thickens it so it survives the squash.
    "Pickup_FilmFrame": lambda x, y, z: (z, x, y),
}
POST_SCALE = {"Pickup_FilmFrame": (1.0, 1.0, 10.0)}
# Issue #119: these three had NaN render bounds in this PC's derived-data cache (see the build call in import_mesh).
# The cache key comes from the mesh content, so a re-import of identical geometry would just fetch the bad entry
# again. A 0.1 mm offset on all three axes gives them a new key; it is far too small to see.
DDC_REKEY_OFFSET_CM = {"Snowboard": 0.01, "Dressing_Chase_SnowRock": 0.01, "Dressing_StudioSpotlight": 0.01}
# Per-instance scalar overrides. Pickups keep a strong take glow: the chain-colour pulse is how players read them.
MI_SCALARS = {"Pickup_FilmFrame": {"TakeGlow": 0.3}}
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
# ONLY=HangGlider,Horse re-imports just those raw names (mesh, textures and instance), whatever exists, and skips the
# rest, so one regenerated model does not re-save every other asset.
ONLY = {n.strip() for n in os.environ.get("ONLY", "").split(",") if n.strip()}


def exists(path):
    # With ONLY, main() only visits the named entries, and those are always re-imported.
    return (not FORCE) and (not ONLY) and eal.does_asset_exist(path)


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
    mat = eal.load_asset(path) if eal.does_asset_exist(path) else None
    if mat and not (FORCE or os.environ.get("FORCE_MASTER") == "1"):
        return mat
    if mat:
        mel.delete_all_material_expressions(mat)  # rebuild in place so the MI_* instances keep their parent
    else:
        mat = tools.create_asset("M_MeshyPBR", ART, unreal.Material, unreal.MaterialFactoryNew())
    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_MASKED)
    mat.set_editor_property("used_with_instanced_static_meshes", True)
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
    # Issue #87: Meshy roughness maps sit near 0.4, so stretched obstacles mirror the sky and the cream backdrop
    # flats. RoughnessMin (default 0 = unchanged) lets an instance floor it to read matte.
    rough_min = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 300)
    rough_min.set_editor_property("parameter_name", "RoughnessMin")
    rough_min.set_editor_property("default_value", 0.0)
    rough_max = mel.create_material_expression(mat, unreal.MaterialExpressionMax, -300, 200)
    mel.connect_material_expressions(rough, "R", rough_max, "A")
    mel.connect_material_expressions(rough_min, "", rough_max, "B")
    mel.connect_material_property(rough_max, "", unreal.MaterialProperty.MP_ROUGHNESS)
    normal = tex_param("Normal", defaults["normal"], S.SAMPLERTYPE_NORMAL, 400)
    mel.connect_material_property(normal, "RGB", unreal.MaterialProperty.MP_NORMAL)

    emis = tex_param("Emissive", defaults["emission"], S.SAMPLERTYPE_COLOR, 650)
    strength = mel.create_material_expression(mat, unreal.MaterialExpressionScalarParameter, -700, 900)
    strength.set_editor_property("parameter_name", "EmissiveStrength")
    strength.set_editor_property("default_value", 0.0)
    emis_mul = mel.create_material_expression(mat, unreal.MaterialExpressionMultiply, -300, 700)
    mel.connect_material_expressions(emis, "RGB", emis_mul, "A")
    mel.connect_material_expressions(strength, "", emis_mul, "B")

    # ---- Take contract (mirrors M_TakeGhost: Active = 1 - |MPC_FlowState.ActiveTake - TakeIndex|) ----
    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    def scalar(name, default, x, y):
        e = node(unreal.MaterialExpressionScalarParameter, x, y)
        e.set_editor_property("parameter_name", name)
        e.set_editor_property("default_value", default)
        return e

    def link(a, a_out, b, b_in):
        if not mel.connect_material_expressions(a, a_out, b, b_in):
            err("M_MeshyPBR: could not connect %s -> %s.%s" % (a.get_name(), b.get_name(), b_in))

    mpc = eal.load_asset("/Game/MegaJam26/VFX/MPC_FlowState") if eal.does_asset_exist("/Game/MegaJam26/VFX/MPC_FlowState") else None
    if mpc:
        active_take = node(unreal.MaterialExpressionCollectionParameter, -1900, 1100)
        active_take.set_editor_property("collection", mpc)
        active_take.set_editor_property("parameter_name", "ActiveTake")
    else:
        err("MPC_FlowState not found: take ghosting will treat Take A as always active")
        active_take = node(unreal.MaterialExpressionConstant, -1900, 1100)
    take_index = scalar("TakeIndex", 0.0, -1900, 1250)
    diff = node(unreal.MaterialExpressionSubtract, -1650, 1150)
    link(active_take, "", diff, "A")
    link(take_index, "", diff, "B")
    absolute = node(unreal.MaterialExpressionAbs, -1500, 1150)
    link(diff, "", absolute, "")
    one_minus = node(unreal.MaterialExpressionOneMinus, -1350, 1150)
    link(absolute, "", one_minus, "")
    matches = node(unreal.MaterialExpressionSaturate, -1200, 1150)
    link(one_minus, "", matches, "")

    ghosting = scalar("TakeGhosting", 0.0, -1200, 1300)
    active = node(unreal.MaterialExpressionLinearInterpolate, -950, 1150)  # 1 unless ghosting is on and the take differs
    active.set_editor_property("const_a", 1.0)
    link(matches, "", active, "B")
    link(ghosting, "", active, "Alpha")

    ghost_opacity = scalar("GhostOpacity", 0.3, -950, 1400)
    opacity = node(unreal.MaterialExpressionLinearInterpolate, -700, 1250)
    opacity.set_editor_property("const_b", 1.0)
    link(ghost_opacity, "", opacity, "A")
    link(active, "", opacity, "Alpha")
    dither_fn = eal.load_asset("/Engine/Functions/Engine_MaterialFunctions02/Utility/DitherTemporalAA")
    dither = node(unreal.MaterialExpressionMaterialFunctionCall, -450, 1250)
    dither.set_editor_property("material_function", dither_fn)
    link(opacity, "", dither, "Alpha Threshold")
    if not mel.connect_material_property(dither, "", unreal.MaterialProperty.MP_OPACITY_MASK):
        err("M_MeshyPBR: could not connect the dither to Opacity Mask")

    take_color = node(unreal.MaterialExpressionVectorParameter, -950, 1600)
    take_color.set_editor_property("parameter_name", "TakeColor")
    take_color.set_editor_property("default_value", unreal.LinearColor(0, 0, 0, 1))
    ghost_glow = scalar("GhostGlow", 0.2, -950, 1850)
    take_glow = scalar("TakeGlow", 0.03, -950, 1950)
    glow = node(unreal.MaterialExpressionLinearInterpolate, -700, 1800)
    link(ghost_glow, "", glow, "A")
    link(take_glow, "", glow, "B")
    link(active, "", glow, "Alpha")
    take_emis = node(unreal.MaterialExpressionMultiply, -450, 1650)
    link(take_color, "", take_emis, "A")
    link(glow, "", take_emis, "B")
    emissive = node(unreal.MaterialExpressionAdd, -150, 900)
    link(emis_mul, "", emissive, "A")
    link(take_emis, "", emissive, "B")
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    save(mat)
    log("Built " + path)
    return mat


# ---------------------------------------------------------------------------------------------------------------------
# Track surface: floor and wall (world-aligned grid, no textures, so it tiles at any stretch)
# ---------------------------------------------------------------------------------------------------------------------
def build_track_surface():
    path = ART + "/M_TrackSurface"
    mat = eal.load_asset(path) if eal.does_asset_exist(path) else None
    if not mat or FORCE or os.environ.get("FORCE_MASTER") == "1":
        if mat:
            mel.delete_all_material_expressions(mat)
        else:
            mat = tools.create_asset("M_TrackSurface", ART, unreal.Material, unreal.MaterialFactoryNew())
        mat.set_editor_property("used_with_instanced_static_meshes", True)

        def node(cls, x, y):
            return mel.create_material_expression(mat, cls, x, y)

        def link(a, b, b_in):
            if not mel.connect_material_expressions(a, "", b, b_in):
                err("M_TrackSurface: could not connect %s -> %s.%s" % (a.get_name(), b.get_name(), b_in))

        def scalar(name, default, x, y):
            e = node(unreal.MaterialExpressionScalarParameter, x, y)
            e.set_editor_property("parameter_name", name)
            e.set_editor_property("default_value", default)
            return e

        def vector(name, rgb, x, y):
            e = node(unreal.MaterialExpressionVectorParameter, x, y)
            e.set_editor_property("parameter_name", name)
            e.set_editor_property("default_value", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))
            return e

        # Distance to the nearest grid plane on each world axis: 0 at a cell centre, 1 on a grid line.
        wp = node(unreal.MaterialExpressionWorldPosition, -2200, 0)
        cells = node(unreal.MaterialExpressionDivide, -2000, 0)
        link(wp, cells, "A")
        link(scalar("GridSize", 400.0, -2200, 150), cells, "B")
        frac = node(unreal.MaterialExpressionFrac, -1850, 0)
        link(cells, frac, "")
        centred = node(unreal.MaterialExpressionSubtract, -1700, 0)
        centred.set_editor_property("const_b", 0.5)
        link(frac, centred, "A")
        absolute = node(unreal.MaterialExpressionAbs, -1550, 0)
        link(centred, absolute, "")
        edge = node(unreal.MaterialExpressionMultiply, -1400, 0)
        edge.set_editor_property("const_b", 2.0)
        link(absolute, edge, "A")

        width = scalar("LineWidth", 0.03, -1700, 250)
        inv_width = node(unreal.MaterialExpressionOneMinus, -1500, 250)
        link(width, inv_width, "")
        over = node(unreal.MaterialExpressionSubtract, -1250, 0)
        link(edge, over, "A")
        link(inv_width, over, "B")
        ramp = node(unreal.MaterialExpressionDivide, -1100, 0)
        link(over, ramp, "A")
        link(width, ramp, "B")
        lines = node(unreal.MaterialExpressionSaturate, -950, 0)
        link(ramp, lines, "")

        # Drop the axis a face points along (its coordinate is constant across the face).
        normal = node(unreal.MaterialExpressionVertexNormalWS, -1700, 450)
        normal_abs = node(unreal.MaterialExpressionAbs, -1500, 450)
        link(normal, normal_abs, "")
        side = node(unreal.MaterialExpressionSubtract, -1350, 450)
        side.set_editor_property("const_a", 0.5)
        link(normal_abs, side, "B")
        side_sharp = node(unreal.MaterialExpressionMultiply, -1200, 450)
        side_sharp.set_editor_property("const_b", 1000.0)
        link(side, side_sharp, "A")
        side_mask = node(unreal.MaterialExpressionSaturate, -1050, 450)
        link(side_sharp, side_mask, "")

        masked = node(unreal.MaterialExpressionMultiply, -800, 150)
        link(lines, masked, "A")
        link(side_mask, masked, "B")
        chans = []
        for i, ch in enumerate("rgb"):
            m = node(unreal.MaterialExpressionComponentMask, -650, 50 + i * 120)
            for c in "rgba":
                m.set_editor_property(c, c == ch)
            link(masked, m, "")
            chans.append(m)
        max_rg = node(unreal.MaterialExpressionMax, -480, 100)
        link(chans[0], max_rg, "A")
        link(chans[1], max_rg, "B")
        line_mask = node(unreal.MaterialExpressionMax, -330, 150)
        link(max_rg, line_mask, "A")
        link(chans[2], line_mask, "B")

        surface = vector("SurfaceColor", (0.03, 0.03, 0.035), -480, -300)
        line_color = vector("LineColor", (0.25, 0.27, 0.32), -480, -120)
        base = node(unreal.MaterialExpressionLinearInterpolate, -150, -200)
        link(surface, base, "A")
        link(line_color, base, "B")
        link(line_mask, base, "Alpha")
        mel.connect_material_property(base, "", unreal.MaterialProperty.MP_BASE_COLOR)

        lit_lines = node(unreal.MaterialExpressionMultiply, -150, 150)
        link(line_color, lit_lines, "A")
        link(line_mask, lit_lines, "B")
        glow = node(unreal.MaterialExpressionMultiply, 0, 250)
        link(lit_lines, glow, "A")
        link(scalar("LineGlow", 0.5, -330, 350), glow, "B")
        mel.connect_material_property(glow, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)
        mel.connect_material_property(scalar("Roughness", 0.55, -150, 450), "", unreal.MaterialProperty.MP_ROUGHNESS)

        mel.recompile_material(mat)
        save(mat)
        log("Built " + path)

    looks = {  # mesh -> (instance, surface colour, line colour, grid size)
        "SM_FloorTile": ("MI_FloorTile", (0.035, 0.035, 0.04), (0.28, 0.30, 0.36), 400.0),
        "SM_WallSegment": ("MI_WallSegment", (0.02, 0.02, 0.025), (0.16, 0.17, 0.22), 200.0),
    }
    for sm_name, (mi_name, surface_rgb, line_rgb, grid) in looks.items():
        mi_path = ART + "/" + mi_name
        if eal.does_asset_exist(mi_path):
            mi = eal.load_asset(mi_path)
        else:
            mi = tools.create_asset(mi_name, ART, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
        mel.set_material_instance_parent(mi, mat)
        mel.set_material_instance_vector_parameter_value(mi, "SurfaceColor", unreal.LinearColor(*surface_rgb, 1))
        mel.set_material_instance_vector_parameter_value(mi, "LineColor", unreal.LinearColor(*line_rgb, 1))
        mel.set_material_instance_scalar_parameter_value(mi, "GridSize", grid)
        save(mi)
        sm_path = ART + "/" + sm_name
        mesh = eal.load_asset(sm_path) if eal.does_asset_exist(sm_path) else eal.duplicate_asset("/Engine/BasicShapes/Cube", sm_path)
        if not mesh:
            err("could not create " + sm_path)
            continue
        mesh.set_material(0, mi)
        save(mesh)
        size, centre = bounds(mesh)
        report.append("%-40s engine cube copy, size %s cm, centre %s, collision shapes %d"
                      % (sm_name, fmt(size), fmt(centre), sme.get_simple_collision_count(mesh)))


def build_instance(name, master, textures):
    mi_name = "MI_" + asset_base(name)
    if eal.does_asset_exist(ART + "/" + mi_name):
        mi = eal.load_asset(ART + "/" + mi_name)
    else:
        mi = tools.create_asset(mi_name, ART, unreal.MaterialInstanceConstant, unreal.MaterialInstanceConstantFactoryNew())
    mel.set_material_instance_parent(mi, master)
    for map_key, tex in textures.items():
        mel.set_material_instance_texture_parameter_value(mi, MAPS[map_key][1], tex)
    if "emission" in textures:
        mel.set_material_instance_scalar_parameter_value(mi, "EmissiveStrength", 1.0)
    for param, value in MI_SCALARS.get(name, {}).items():
        mel.set_material_instance_scalar_parameter_value(mi, param, value)
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
    mesh = run_task(fbx, sm_name, fbx_options())
    if not mesh:
        err("mesh import failed: " + fbx)
        return None
    size, centre = bounds(mesh)
    if max(abs(centre.x), abs(centre.y), abs(centre.z)) > 0.5:  # re-centre the pivot on the bounding box
        log("%s: re-centring, source centre %s" % (sm_name, fmt(centre)))
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
    basis = VEHICLE_BASIS.get(name)
    verts = []
    done, vid = 0, 0
    total = desc.get_vertex_count()
    while done < total:
        v = unreal.VertexID(vid)
        if desc.is_vertex_valid(v):
            p = desc.get_vertex_position(v)
            q = (p.x * scale.x, p.y * scale.y, p.z * scale.z)
            q = basis(*q) if basis else q
            k = POST_SCALE.get(name, (1.0, 1.0, 1.0))
            verts.append((v, (q[0] * k[0], q[1] * k[1], q[2] * k[2])))
            done += 1
        vid += 1
    shift = (0.0, 0.0, 0.0)
    if name in VEHICLE_LENGTH_CM:  # vehicles: pivot on the ground, centred in X and Y
        lo = [min(q[i] for _, q in verts) for i in range(3)]
        hi = [max(q[i] for _, q in verts) for i in range(3)]
        shift = (-(lo[0] + hi[0]) * 0.5, -(lo[1] + hi[1]) * 0.5, -lo[2])
    nudge = DDC_REKEY_OFFSET_CM.get(name, 0.0)
    shift = (shift[0] + nudge, shift[1] + nudge, shift[2] + nudge)
    for v, q in verts:
        desc.set_vertex_position(v, unreal.Vector(q[0] + shift[0], q[1] + shift[1], q[2] + shift[2]))
    # fast_build must be False (issue #119). The Python default is a fast build, and in UE 5.8 that path serialises the
    # render data into the derived-data cache BEFORE its bounds are set, so the cached bounds are uninitialised
    # memory. Later sessions and the cook read that entry back ("FStaticMeshRenderData::Serialize found NaN in Bounds"
    # when the garbage happens to be NaN). The full build sets the bounds first.
    mesh.build_from_static_mesh_descriptions([desc], build_simple_collision=False, fast_build=False)

    sme.remove_collisions(mesh)
    sme.add_simple_collisions(mesh, unreal.ScriptCollisionShapeType.BOX)

    final_size, final_centre = bounds(mesh)
    tris = sme.get_number_verts(mesh, 0)
    report.append("%-40s src %s  ->  final %s cm, centre %s, verts %d"
                  % (sm_name, fmt(size), fmt(final_size), fmt(final_centre), tris))
    return mesh


# ---------------------------------------------------------------------------------------------------------------------
def rebuild_master_only():
    # MASTER_ONLY=1: rebuild the M_MeshyPBR graph in place (instances keep their parent and overrides) and touch
    # nothing else. Defaults come from the instances that already exist, so no texture is re-imported.
    master = eal.load_asset(ART + "/M_MeshyPBR")
    if not master:
        err("M_MeshyPBR not found; run a full import first")
        return
    defaults = {k: mel.get_material_default_texture_parameter_value(master, MAPS[k][1]) for k in MAPS}
    os.environ["FORCE_MASTER"] = "1"
    build_master(defaults)


def main():
    if os.environ.get("MASTER_ONLY") == "1":
        rebuild_master_only()
        return
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
            if os.path.isfile(os.path.join(cat_dir, name, name + ".fbx")) and (not ONLY or name in ONLY):
                entries.append((cat, name))
    log("Found %d meshes%s" % (len(entries), " (ONLY=%s)" % ",".join(sorted(ONLY)) if ONLY else ""))

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

    try:
        if not ONLY:
            build_track_surface()
    except Exception as exc:  # noqa: BLE001
        err("track surface: %s\n%s" % (exc, traceback.format_exc()))
    for line in report:
        log(line)


try:
    main()
except Exception as exc:  # noqa: BLE001
    err("%s\n%s" % (exc, traceback.format_exc()))
log("DONE with %d error(s)" % len(errors))
