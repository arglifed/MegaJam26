"""
MegaJam26 Issues #23 / #56 / #57: wires the imported art into the game. Run after import_issue5_assets.py and after
the C++ module is built, headless with the editor CLOSED (same command line as import_issue5_assets.py).

Creates   /Game/MegaJam26/Blueprints/BP_TrackGenerator   (parent AMegaJamTrackGenerator, MegaJam|Art slots filled)
Edits     /Game/MegaJam26/Blueprints/BP_FlowGameMode     Track Generator Class = BP_TrackGenerator
Edits     /Game/MegaJam26/Blueprints/BP_FlowPawn         SceneVehicleMeshes + SceneSeatOffsets
(The two edited Blueprints are shared binary assets; the user approved editing them for this task. AGENTS.md section 5.)

Edits     /Game/MegaJam26/Art/MI_Obstacle_Action_CyberBarrier, MI_Vehicle_SpyCar   BaseColorTint (issue #87)

Search the log for "[ArtWire]". Safe to re-run: it only sets the properties listed below.
Set ART_ONLY=1 in the environment to leave the two shared Blueprints (BP_FlowGameMode, BP_FlowPawn) unsaved, for runs
that only change BP_TrackGenerator or the art material instances.
"""

import os
import traceback

import unreal

ART = "/Game/MegaJam26/Art/"
BPS = "/Game/MegaJam26/Blueprints"
eal = unreal.EditorAssetLibrary
errors = []


def log(msg):
    unreal.log("[ArtWire] " + msg)


def err(msg):
    errors.append(msg)
    unreal.log_error("[ArtWire] ERROR: " + msg)


def mesh(name):
    asset = eal.load_asset(ART + name)
    if not asset:
        err("missing mesh " + name)
    return asset


def cdo_of(bp):
    return unreal.get_default_object(bp.generated_class())


def finish(bp):
    unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    eal.save_loaded_asset(bp, only_if_is_dirty=False)


def wire_track_generator():
    path = BPS + "/BP_TrackGenerator"
    if eal.does_asset_exist(path):
        bp = eal.load_asset(path)
    else:
        factory = unreal.BlueprintFactory()
        factory.set_editor_property("parent_class", unreal.MegaJamTrackGenerator)
        bp = unreal.AssetToolsHelpers.get_asset_tools().create_asset("BP_TrackGenerator", BPS, unreal.Blueprint, factory)
        unreal.BlueprintEditorLibrary.compile_blueprint(bp)
    cdo = cdo_of(bp)

    slots = {
        "floor_tile_mesh": "SM_FloorTile",
        "wall_segment_mesh": "SM_WallSegment",
        "soundstage_truss_mesh": "SM_Dressing_LightingTruss",
        "skyscraper_mesh": "SM_Dressing_Noir_Skyscraper",
        "film_reel_mesh": "SM_SetPiece_FilmReel",
        "aperture_ring_mesh": "SM_SetPiece_ApertureRing",
        "film_frame_pickup_mesh": "SM_Pickup_FilmFrame",
        "wrap_arch_mesh": "SM_SetPiece_WrapArch",
    }
    for prop, name in slots.items():
        cdo.set_editor_property(prop, mesh(name))

    # Scene order: 0 Action, 1 Western, 2 Noir, 3 Chase, 4 Final Cut.
    # The laser gate is not an obstacle skin: it is an open frame, but obstacle collision is a solid box (it is used as
    # Final Cut scatter dressing below).
    take_a = ["SM_Obstacle_Action_CyberBarrier", "SM_Obstacle_Western_TimberBarricade", "SM_Obstacle_Noir_Roadblock",
              "SM_Obstacle_Chase_Snowdrift", "SM_Obstacle_Final_Monolith"]
    take_b = ["SM_Obstacle_Action_CyberBarrier", "SM_Obstacle_Western_TimberBarricade", "SM_Obstacle_Noir_Roadblock",
              "SM_Obstacle_Chase_IceBoulders", "SM_Obstacle_Final_Monolith"]
    cdo.set_editor_property("scene_obstacle_meshes_take_a", [mesh(n) for n in take_a])
    cdo.set_editor_property("scene_obstacle_meshes_take_b", [mesh(n) for n in take_b])

    # Issue #87: real proportions of the obstacle art (import squashed each into a 100 cm cube), as X = depth along the
    # track, Y = width, Z = height, from RawAssets/Art/MANIFEST.md (Meshy reports width x height x depth). C++ draws a
    # stretched obstacle box as a small grid of copies near these proportions instead of one smeared copy.
    native = {
        "SM_Obstacle_Action_CyberBarrier": (0.84, 0.90, 0.95),
        "SM_Obstacle_Western_TimberBarricade": (0.74, 0.96, 0.74),  # really a 0.22-deep panel: deepened, so it is not stacked in rows
        "SM_Obstacle_Noir_Roadblock": (0.47, 0.96, 0.58),
        "SM_Obstacle_Chase_Snowdrift": (0.95, 0.95, 0.44),
        "SM_Obstacle_Chase_IceBoulders": (0.48, 0.96, 0.57),
        "SM_Obstacle_Final_Monolith": (0.28, 0.30, 0.95),
    }
    cdo.set_editor_property("obstacle_art_native_size", {mesh(n): unreal.Vector(*v) for n, v in native.items()})

    dressing = ["SM_Dressing_StudioSpotlight", "SM_Dressing_BoomMic", "SM_Prop_CameraCrane", "SM_Dressing_BackdropFlat",
                "SM_Dressing_LightingTruss"]
    cdo.set_editor_property("studio_dressing_meshes", [mesh(n) for n in dressing])
    cdo.set_editor_property("studio_dressing_scale", 14.0)
    cdo.set_editor_property("use_mesh_materials", True)

    # Issue #87: per-scene props scattered outside the corridor walls (spawner from #90). The art is about 100 cm on
    # its longest side, so the scale is the prop's size in metres; C++ rests each one on Z = 0 clear of the wall.
    # Each entry is rolled once per 32 m chunk: (mesh, min scale, max scale, spawn probability).
    G = unreal.MegaJamSceneGenre
    scatter = {
        G.WESTERN: [("SM_Prop_Western_Tombstone", 3.0, 4.5, 0.6), ("SM_Prop_Western_GraveCross", 4.0, 6.0, 0.5),
                    ("SM_Dressing_Western_Cactus", 7.0, 11.0, 0.8), ("SM_Dressing_Western_Tumbleweed", 2.5, 4.0, 0.5)],
        G.NOIR: [("SM_Prop_Noir_BlackSedan", 6.0, 7.0, 0.5), ("SM_Dressing_Noir_StreetLamp", 9.0, 12.0, 0.9)],
        G.CHASE: [("SM_Dressing_Chase_PineTree", 10.0, 18.0, 0.9), ("SM_Dressing_Chase_SnowRock", 4.0, 9.0, 0.7)],
        # The laser gate is an open frame, so it cannot skin a solid obstacle box; it dresses the roadside instead.
        G.MONTAGE: [("SM_Dressing_Final_FloatingCrystal", 5.0, 10.0, 0.8), ("SM_Obstacle_Final_LaserGate", 6.0, 9.0, 0.4)],
    }
    scatter_map = {}
    for genre, props in scatter.items():
        configs = []
        for name, lo, hi, chance in props:
            # Constructor arguments: the struct fields are EditDefaultsOnly, so they cannot be set after construction.
            configs.append(unreal.ScenePropConfig(prop_mesh=mesh(name), min_scale=lo, max_scale=hi,
                                                  spawn_probability=chance))
        prop_list = unreal.ScenePropList(props=configs)
        scatter_map[genre] = prop_list
    cdo.set_editor_property("scene_scatter_props", scatter_map)

    finish(bp)
    log("BP_TrackGenerator saved: %d slots, %d + %d obstacle skins, %d dressing meshes, %d scatter props in %d scenes"
        % (len(slots), len(take_a), len(take_b), len(dressing), sum(len(p) for p in scatter.values()), len(scatter)))
    saved = cdo_of(eal.load_asset(path)).get_editor_property("scene_scatter_props")
    for genre, prop_list in saved.items():
        log("  scatter %s: %s" % (genre, ", ".join(
            "%s x%.1f-%.1f p=%.1f" % (c.prop_mesh.get_name(), c.min_scale, c.max_scale, c.spawn_probability)
            for c in prop_list.props)))
    return bp


def tint_materials():
    # Issue #87: BaseColorTint multiplies the baked base colour (linear RGB). Both textures are a mid grey that reads
    # cream / silver under the sky light; #56 and #57 asked for dark gunmetal and dark, desaturated colours.
    tints = {
        "MI_Obstacle_Action_CyberBarrier": (0.26, 0.30, 0.36),
        "MI_Vehicle_SpyCar": (0.22, 0.23, 0.26),
    }
    mel = unreal.MaterialEditingLibrary
    for name, rgb in tints.items():
        mi = eal.load_asset(ART + name)
        if not mi:
            err("missing material instance " + name)
            continue
        mel.set_material_instance_vector_parameter_value(mi, "BaseColorTint", unreal.LinearColor(rgb[0], rgb[1], rgb[2], 1))
        eal.save_loaded_asset(mi, only_if_is_dirty=False)
        log("%s BaseColorTint = %s" % (name, mel.get_material_instance_vector_parameter_value(mi, "BaseColorTint")))

    # Issue #87: these two read cream / beige instead of gunmetal / black. Two causes, found by toggling inputs in PIE:
    #  - RoughnessMin: the low Meshy roughness made them mirror the sky and the cream backdrop flats (needs
    #    M_MeshyPBR with RoughnessMin: import_issue5_assets.py MASTER_ONLY=1).
    #  - TakeGlow: the active take's faint TakeColor glow (Take A orange) swamps a near-black albedo and reads brown.
    #    The ghosted (inactive) take still shows the take colour through GhostGlow.
    dark = {"MI_Obstacle_Action_CyberBarrier": {"RoughnessMin": 0.85, "TakeGlow": 0.005},
            "MI_Obstacle_Final_Monolith": {"RoughnessMin": 0.85, "TakeGlow": 0.005}}
    for name, scalars in dark.items():
        mi = eal.load_asset(ART + name)
        if not mi:
            err("missing material instance " + name)
            continue
        for param, value in scalars.items():
            mel.set_material_instance_scalar_parameter_value(mi, param, value)
        eal.save_loaded_asset(mi, only_if_is_dirty=False)
        log("%s %s" % (name, {p: round(mel.get_material_instance_scalar_parameter_value(mi, p), 3) for p in scalars}))


def wire_game_mode(track_bp):
    bp = eal.load_asset(BPS + "/BP_FlowGameMode")
    cdo = cdo_of(bp)
    cdo.set_editor_property("track_generator_class", track_bp.generated_class())
    finish(bp)
    log("BP_FlowGameMode.TrackGeneratorClass = %s" % cdo.get_editor_property("track_generator_class").get_name())


def wire_pawn():
    G = unreal.MegaJamSceneGenre
    # Vehicle pivots sit on the ground at the pawn origin; the offset is where the orb rides (seat / hover point), in cm.
    vehicles = {
        G.ACTION: ("SM_Vehicle_Motorcycle", unreal.Vector(0, 0, 85)),
        G.WESTERN: ("SM_Vehicle_Horse", unreal.Vector(0, 0, 165)),
        G.NOIR: ("SM_Vehicle_SpyCar", unreal.Vector(-20, 0, 85)),
        G.CHASE: ("SM_Vehicle_Snowboard", unreal.Vector(0, 0, 45)),
        G.ALPINE: ("SM_Vehicle_Snowboard", unreal.Vector(0, 0, 45)),
        G.MONTAGE: ("SM_Vehicle_HangGlider", unreal.Vector(0, 0, -60)),  # wing at the pawn origin, orb hangs below
    }
    bp = eal.load_asset(BPS + "/BP_FlowPawn")
    cdo = cdo_of(bp)
    cdo.set_editor_property("scene_vehicle_meshes", {g: mesh(n) for g, (n, _) in vehicles.items()})
    cdo.set_editor_property("scene_seat_offsets", {g: off for g, (_, off) in vehicles.items()})
    finish(bp)
    log("BP_FlowPawn: %d vehicle meshes, %d seat offsets"
        % (len(cdo.get_editor_property("scene_vehicle_meshes")), len(cdo.get_editor_property("scene_seat_offsets"))))


try:
    track = wire_track_generator()
    tint_materials()
    if os.environ.get("ART_ONLY") == "1":
        log("ART_ONLY=1: BP_FlowGameMode and BP_FlowPawn left untouched")
    else:
        wire_game_mode(track)
        wire_pawn()
except Exception as exc:  # noqa: BLE001 - log everything, the run is headless
    err("%s\n%s" % (exc, traceback.format_exc()))
log("DONE with %d error(s)" % len(errors))
