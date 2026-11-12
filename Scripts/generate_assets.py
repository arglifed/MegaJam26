"""
MegaJam26 headless asset generator.

Creates (idempotently) every starter asset under /Game/MegaJam26 and then patches Config/DefaultEngine.ini
so the generated test map and game mode become the project defaults.

Run from the project root (PowerShell):

    & "C:\\Program Files\\Epic Games\\UE_5.8\\Engine\\Binaries\\Win64\\UnrealEditor-Cmd.exe" `
        "$PWD\\MegaJam26.uproject" -ExecutePythonScript="$PWD\\Scripts\\generate_assets.py" `
        -unattended -nop4 -nosplash -NullRHI -stdout -FullStdOutLogOutput

Notes:
  * Every step is wrapped so one failure does not stop the rest. A summary is logged at the end
    (search the log for "[MegaJamGen]").
  * Existing assets are reused and their settings re-applied, so the script is safe to re-run.
    The map is only built if it does not exist yet (delete L_FlowTest to rebuild it).
"""

import os
import re
import traceback

import unreal

ROOT = "/Game/MegaJam26"
CORE = ROOT + "/Core"
VFX = ROOT + "/VFX"
BLUEPRINTS = ROOT + "/Blueprints"
MAPS = ROOT + "/Maps"
MAP_NAME = "L_FlowTest"
MAP_PATH = MAPS + "/" + MAP_NAME

eal = unreal.EditorAssetLibrary
asset_tools = unreal.AssetToolsHelpers.get_asset_tools()

failures = []

# Optional selective run, e.g. $env:MEGAJAM_GEN_ONLY = "mpc,ghost" (steps: mpc, ghost). Empty = generate everything.
ONLY = set(s.strip().lower() for s in os.environ.get("MEGAJAM_GEN_ONLY", "").split(",") if s.strip())


def log(msg):
    unreal.log("[MegaJamGen] " + msg)


def fail(step, exc):
    failures.append(step)
    unreal.log_error("[MegaJamGen] FAILED: %s -> %s" % (step, exc))
    unreal.log_error(traceback.format_exc())


def first_attr(*names):
    """Return the first unreal.<name> that exists, else None."""
    for n in names:
        if hasattr(unreal, n):
            return getattr(unreal, n)
    return None


def get_or_create(name, folder, asset_class, factory):
    path = "%s/%s" % (folder, name)
    if eal.does_asset_exist(path):
        log("reusing %s" % path)
        return unreal.load_asset(path)
    asset = asset_tools.create_asset(name, folder, asset_class, factory)
    if asset is None:
        raise RuntimeError("create_asset returned None for " + path)
    log("created %s" % path)
    return asset


def save(asset):
    # only_if_is_dirty=False so freshly created / CDO-edited assets are always written.
    if not eal.save_loaded_asset(asset, False):
        raise RuntimeError("save failed for " + asset.get_path_name())


def make_key(key_name):
    key = unreal.Key()
    key.set_editor_property("key_name", key_name)
    return key


# ----------------------------------------------------------------------------------------------
# Input Actions and Mapping Context
# ----------------------------------------------------------------------------------------------
def make_action(name, value_type, trigger_when_paused=False):
    factory_cls = first_attr("InputAction_Factory", "InputActionFactory")
    factory = factory_cls() if factory_cls else None
    action = get_or_create(name, CORE, unreal.InputAction, factory)
    action.set_editor_property("value_type", value_type)
    if trigger_when_paused:
        action.set_editor_property("trigger_when_paused", True)
    save(action)
    return action


def new_mapping(imc, action, key_name, modifiers=None):
    mapping = unreal.EnhancedActionKeyMapping()
    mapping.set_editor_property("action", action)
    mapping.set_editor_property("key", make_key(key_name))
    if modifiers:
        mapping.set_editor_property("modifiers", modifiers)
    return mapping


def set_mappings(imc, mappings):
    try:
        container = imc.get_editor_property("default_key_mappings")
        container.set_editor_property("mappings", mappings)
        imc.set_editor_property("default_key_mappings", container)
    except Exception:
        # Older layout exposes the array directly.
        imc.set_editor_property("mappings", mappings)


def make_imc(steer, boost, brake, pause):
    factory_cls = first_attr("InputMappingContext_Factory", "InputMappingContextFactory")
    factory = factory_cls() if factory_cls else None
    imc = get_or_create("IMC_Flow", CORE, unreal.InputMappingContext, factory)

    def negate():
        return unreal.new_object(unreal.InputModifierNegate, outer=imc)

    def swizzle_yxz():
        swz = unreal.new_object(unreal.InputModifierSwizzleAxis, outer=imc)
        swz.set_editor_property("order", unreal.InputAxisSwizzle.YXZ)
        return swz

    mappings = [
        # Steer (Axis2D): X = right, Y = up
        new_mapping(imc, steer, "D"),                                         # +X
        new_mapping(imc, steer, "A", [negate()]),                             # -X
        new_mapping(imc, steer, "W", [swizzle_yxz()]),                        # +Y (swizzle YXZ)
        new_mapping(imc, steer, "S", [swizzle_yxz(), negate()]),              # -Y (swizzle YXZ, then negate)
        new_mapping(imc, steer, "Gamepad_Left2D"),
        # Boost (Bool)
        new_mapping(imc, boost, "SpaceBar"),
        new_mapping(imc, boost, "Gamepad_FaceButton_Bottom"),
        # Brake (Axis1D)
        new_mapping(imc, brake, "LeftShift"),
        new_mapping(imc, brake, "Gamepad_LeftTriggerAxis"),
        # Pause (Bool)
        new_mapping(imc, pause, "Escape"),
        new_mapping(imc, pause, "Gamepad_Special_Right"),
    ]
    set_mappings(imc, mappings)
    save(imc)
    return imc


# ----------------------------------------------------------------------------------------------
# Material Parameter Collection
# ----------------------------------------------------------------------------------------------
def make_mpc():
    mpc = get_or_create("MPC_FlowState", VFX, unreal.MaterialParameterCollection,
                        unreal.MaterialParameterCollectionFactoryNew())
    params = []
    for pname, default in (("FlowRatio", 0.0), ("Multiplier", 1.0), ("AudioEnvelope", 0.0),
                           ("ActiveTake", 0.0), ("AspectMode", 0.0)):
        p = unreal.CollectionScalarParameter()
        p.set_editor_property("parameter_name", pname)
        p.set_editor_property("default_value", default)
        params.append(p)
    mpc.set_editor_property("scalar_parameters", params)
    save(mpc)
    return mpc


# ----------------------------------------------------------------------------------------------
# Take ghost material (Director's Cut obstacles)
# ----------------------------------------------------------------------------------------------
def make_ghost_material(mpc):
    """
    M_TakeGhost: translucent, unlit, emissive.
    Parameters set per obstacle pool by AMegaJamTrackGenerator through a dynamic material instance:
        TakeIndex (scalar): 0 = Take A (Studio), 1 = Take B (Director's)
        TakeColor (vector): pool tint
    Reads MPC_FlowState.ActiveTake: Active = 1 - |ActiveTake - TakeIndex|.
    Active-take obstacles render solid/bright, inactive-take obstacles render faint/ghosted.
    """
    mel = unreal.MaterialEditingLibrary
    path = VFX + "/M_TakeGhost"
    existed = eal.does_asset_exist(path)
    mat = get_or_create("M_TakeGhost", VFX, unreal.Material, unreal.MaterialFactoryNew())
    if existed:
        mel.delete_all_material_expressions(mat)

    mat.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    mat.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_UNLIT)
    try:
        mat.set_editor_property("used_with_instanced_static_meshes", True)
    except Exception as exc:
        unreal.log_warning("[MegaJamGen] could not set used_with_instanced_static_meshes: %s" % exc)

    def node(cls, x, y):
        return mel.create_material_expression(mat, cls, x, y)

    active_take = node(unreal.MaterialExpressionCollectionParameter, -1200, -200)
    active_take.set_editor_property("collection", mpc)
    active_take.set_editor_property("parameter_name", "ActiveTake")

    take_index = node(unreal.MaterialExpressionScalarParameter, -1200, 0)
    take_index.set_editor_property("parameter_name", "TakeIndex")
    take_index.set_editor_property("default_value", 0.0)

    diff = node(unreal.MaterialExpressionSubtract, -950, -100)
    mel.connect_material_expressions(active_take, "", diff, "A")
    mel.connect_material_expressions(take_index, "", diff, "B")

    absolute = node(unreal.MaterialExpressionAbs, -750, -100)
    mel.connect_material_expressions(diff, "", absolute, "")

    active = node(unreal.MaterialExpressionOneMinus, -550, -100)
    mel.connect_material_expressions(absolute, "", active, "")

    # Opacity: 0.08 (ghost) .. 0.9 (solid)
    opacity = node(unreal.MaterialExpressionLinearInterpolate, -300, 100)
    opacity.set_editor_property("const_a", 0.08)
    opacity.set_editor_property("const_b", 0.9)
    mel.connect_material_expressions(active, "", opacity, "Alpha")
    mel.connect_material_property(opacity, "", unreal.MaterialProperty.MP_OPACITY)

    # Emissive: TakeColor * (0.4 ghost .. 5.0 bright)
    glow = node(unreal.MaterialExpressionLinearInterpolate, -550, 250)
    glow.set_editor_property("const_a", 0.4)
    glow.set_editor_property("const_b", 5.0)
    mel.connect_material_expressions(active, "", glow, "Alpha")

    color = node(unreal.MaterialExpressionVectorParameter, -550, 400)
    color.set_editor_property("parameter_name", "TakeColor")
    color.set_editor_property("default_value", unreal.LinearColor(1.0, 0.5, 0.1, 1.0))

    emissive = node(unreal.MaterialExpressionMultiply, -300, 300)
    mel.connect_material_expressions(color, "", emissive, "A")
    mel.connect_material_expressions(glow, "", emissive, "B")
    mel.connect_material_property(emissive, "", unreal.MaterialProperty.MP_EMISSIVE_COLOR)

    mel.recompile_material(mat)
    save(mat)
    return mat


# ----------------------------------------------------------------------------------------------
# Blueprints
# ----------------------------------------------------------------------------------------------
def make_blueprint(name, cpp_class_path):
    parent = unreal.load_class(None, cpp_class_path)
    if parent is None:
        raise RuntimeError("could not load C++ class " + cpp_class_path)
    path = "%s/%s" % (BLUEPRINTS, name)
    if eal.does_asset_exist(path):
        log("reusing %s" % path)
        bp = unreal.load_asset(path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", parent)
        bp = asset_tools.create_asset(name, BLUEPRINTS, unreal.Blueprint, factory)
        if bp is None:
            raise RuntimeError("create_asset returned None for " + path)
        log("created %s" % path)
    return bp


def blueprint_cdo(bp):
    bp_class = eal.load_blueprint_class(bp.get_path_name().split(".")[0])
    return bp_class, unreal.get_default_object(bp_class)


def compile_and_save(bp):
    try:
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    except Exception as exc:  # compile helper is optional; saving still works
        unreal.log_warning("[MegaJamGen] compile_blueprint unavailable: %s" % exc)
    save(bp)


# ----------------------------------------------------------------------------------------------
# Test map
# ----------------------------------------------------------------------------------------------
def build_map(game_mode_class):
    if eal.does_asset_exist(MAP_PATH):
        log("map %s already exists, skipping build" % MAP_PATH)
        return

    level_subsystem = unreal.get_editor_subsystem(unreal.LevelEditorSubsystem)
    actor_subsystem = unreal.get_editor_subsystem(unreal.EditorActorSubsystem)

    if not level_subsystem.new_level(MAP_PATH):
        raise RuntimeError("new_level failed for " + MAP_PATH)

    cube = unreal.load_asset("/Engine/BasicShapes/Cube.Cube")

    def vec(x, y, z):
        return unreal.Vector(x, y, z)

    def rot(pitch, yaw, roll=0.0):
        r = unreal.Rotator()
        r.set_editor_property("pitch", pitch)
        r.set_editor_property("yaw", yaw)
        r.set_editor_property("roll", roll)
        return r

    def spawn(cls, location, rotation=None):
        return actor_subsystem.spawn_actor_from_class(cls, location, rotation if rotation else rot(0, 0))

    def spawn_cube(label, location, scale):
        actor = spawn(unreal.StaticMeshActor, location)
        actor.get_editor_property("static_mesh_component").set_static_mesh(cube)
        actor.set_actor_scale3d(scale)
        actor.set_actor_label(label)
        return actor

    # Floor: 20000 x 2000 x 100 cm, top surface at z = 0, spans x = -1000 .. 19000.
    spawn_cube("Floor", vec(9000, 0, -50), vec(200, 20, 1))

    # Corridor walls (100 cm thick, 1000 cm tall) at y = +/-1000.
    spawn_cube("Wall_Left", vec(9000, -1000, 500), vec(200, 1, 10))
    spawn_cube("Wall_Right", vec(9000, 1000, 500), vec(200, 1, 10))

    # Obstacles: some in the flight line (crash), some just off it (graze).
    obstacles = [
        (3000, 250), (4500, -250), (6000, 0), (7500, 300), (9000, -300),
        (10500, 0), (12000, 250), (13500, -250), (15000, 0), (16500, 300),
    ]
    for index, (ox, oy) in enumerate(obstacles):
        spawn_cube("Obstacle_%02d" % index, vec(ox, oy, 150), vec(2, 2, 3))

    # Lighting
    sun = spawn(unreal.DirectionalLight, vec(0, 0, 1000), rot(-45, 30))
    sun.get_component_by_class(unreal.DirectionalLightComponent).set_mobility(unreal.ComponentMobility.MOVABLE)
    sky_light = spawn(unreal.SkyLight, vec(0, 0, 1200))
    sky_light.get_component_by_class(unreal.SkyLightComponent).set_mobility(unreal.ComponentMobility.MOVABLE)
    spawn(unreal.SkyAtmosphere, vec(0, 0, 0))

    # Player start, facing +X (the pawn flies along its forward axis).
    spawn(unreal.PlayerStart, vec(0, 0, 150), rot(0, 0))

    # World Settings game mode override.
    world = unreal.EditorLevelLibrary.get_editor_world()
    world.get_world_settings().set_editor_property("default_game_mode", game_mode_class)

    if not level_subsystem.save_current_level():
        raise RuntimeError("save_current_level failed")
    log("built and saved " + MAP_PATH)


# ----------------------------------------------------------------------------------------------
# Config patch
# ----------------------------------------------------------------------------------------------
def patch_engine_ini():
    ini_path = os.path.join(unreal.Paths.project_config_dir(), "DefaultEngine.ini")
    ini_path = os.path.normpath(unreal.Paths.convert_relative_path_to_full(ini_path))
    with open(ini_path, "r", encoding="utf-8", newline="") as handle:
        text = handle.read()

    map_ref = "%s/%s.%s" % (MAPS, MAP_NAME, MAP_NAME)
    mode_ref = "%s/BP_FlowGameMode.BP_FlowGameMode_C" % BLUEPRINTS
    desired = {
        "GameDefaultMap": map_ref,
        "EditorStartupMap": map_ref,
        "GlobalDefaultGameMode": mode_ref,
    }

    section = "[/Script/EngineSettings.GameMapsSettings]"
    newline = "\r\n" if "\r\n" in text else "\n"
    if section not in text:
        text = text.rstrip() + newline + newline + section + newline
    head, tail = text.split(section, 1)
    # Section body runs until the next "[" header or end of file.
    match = re.search(r"\r?\n\[", tail)
    body, rest = (tail[:match.start()], tail[match.start():]) if match else (tail, "")

    lines = [ln for ln in body.splitlines() if ln.strip()]
    kept = [ln for ln in lines if ln.split("=", 1)[0].strip() not in desired]
    kept += ["%s=%s" % (key, value) for key, value in desired.items()]
    new_body = newline + newline.join(kept) + newline

    with open(ini_path, "w", encoding="utf-8", newline="") as handle:
        handle.write(head + section + new_body + rest)
    log("patched " + ini_path)


# ----------------------------------------------------------------------------------------------
# Main
# ----------------------------------------------------------------------------------------------
def main_selected():
    """Runs only the steps listed in MEGAJAM_GEN_ONLY (comma separated): mpc, ghost."""
    unknown = ONLY - {"mpc", "ghost"}
    if unknown:
        raise RuntimeError("unknown MEGAJAM_GEN_ONLY step(s): " + ", ".join(sorted(unknown)))

    mpc = None
    try:
        if "mpc" in ONLY:
            mpc = make_mpc()
        elif "ghost" in ONLY:
            # The ghost material only references the MPC; load it, do not re-save it.
            mpc = unreal.load_asset(VFX + "/MPC_FlowState")
            if mpc is None:
                raise RuntimeError("MPC_FlowState does not exist; run with mpc first")
    except Exception as exc:
        fail("MPC_FlowState", exc)

    try:
        if "ghost" in ONLY and mpc is not None:
            make_ghost_material(mpc)
    except Exception as exc:
        fail("M_TakeGhost", exc)

    if failures:
        unreal.log_error("[MegaJamGen] DONE WITH FAILURES: " + ", ".join(failures))
    else:
        log("DONE: selected steps (%s) generated successfully" % ", ".join(sorted(ONLY)))


def main():
    for folder in (CORE, VFX, BLUEPRINTS, MAPS):
        eal.make_directory(folder)

    # Selective run: only touch the steps named in MEGAJAM_GEN_ONLY. Nothing else is loaded or re-saved,
    # so unrelated binary assets stay untouched (AGENTS.md section 5).
    if ONLY:
        main_selected()
        return

    steer = boost = brake = pause = imc = mpc = None
    try:
        steer = make_action("IA_Steer", unreal.InputActionValueType.AXIS2D)
        boost = make_action("IA_Boost", unreal.InputActionValueType.BOOLEAN)
        brake = make_action("IA_Brake", unreal.InputActionValueType.AXIS1D)
        pause = make_action("IA_Pause", unreal.InputActionValueType.BOOLEAN, trigger_when_paused=True)
    except Exception as exc:
        fail("input actions", exc)

    try:
        imc = make_imc(steer, boost, brake, pause)
    except Exception as exc:
        fail("IMC_Flow", exc)

    try:
        mpc = make_mpc()
    except Exception as exc:
        fail("MPC_FlowState", exc)

    try:
        if mpc is not None:
            make_ghost_material(mpc)
    except Exception as exc:
        fail("M_TakeGhost", exc)

    pawn_class = controller_class = mode_class = None
    try:
        bp = make_blueprint("BP_FlowPawn", "/Script/MegaJam26.MegaJamFlowPawn")
        compile_and_save(bp)
        pawn_class, cdo = blueprint_cdo(bp)
        cdo.set_editor_property("steer_action", steer)
        cdo.set_editor_property("boost_action", boost)
        cdo.set_editor_property("brake_action", brake)
        save(bp)
    except Exception as exc:
        fail("BP_FlowPawn", exc)

    try:
        bp = make_blueprint("BP_FlowPlayerController", "/Script/MegaJam26.MegaJamPlayerController")
        compile_and_save(bp)
        controller_class, cdo = blueprint_cdo(bp)
        cdo.set_editor_property("default_mapping_context", imc)
        cdo.set_editor_property("pause_action", pause)
        save(bp)
    except Exception as exc:
        fail("BP_FlowPlayerController", exc)

    try:
        bp = make_blueprint("BP_FlowGameMode", "/Script/MegaJam26.MegaJamGameMode")
        compile_and_save(bp)
        mode_class, cdo = blueprint_cdo(bp)
        cdo.set_editor_property("default_pawn_class", pawn_class)
        cdo.set_editor_property("player_controller_class", controller_class)
        cdo.set_editor_property("default_flow_mpc", mpc)
        save(bp)
    except Exception as exc:
        fail("BP_FlowGameMode", exc)

    map_built = False
    try:
        if mode_class is None:
            raise RuntimeError("BP_FlowGameMode missing, cannot build map")
        build_map(mode_class)
        map_built = True
    except Exception as exc:
        fail("L_FlowTest", exc)

    # Only point the project defaults at assets that actually exist.
    if map_built and not failures:
        try:
            patch_engine_ini()
        except Exception as exc:
            fail("DefaultEngine.ini patch", exc)
    else:
        unreal.log_warning("[MegaJamGen] Skipping DefaultEngine.ini patch because a step failed.")

    if failures:
        unreal.log_error("[MegaJamGen] DONE WITH FAILURES: " + ", ".join(failures))
    else:
        log("DONE: all assets generated successfully")


try:
    main()
finally:
    # Let a headless run terminate itself.
    unreal.SystemLibrary.execute_console_command(None, "QUIT_EDITOR")
