# Issue #5 Handoff: Audio & Art Asset Contract

**Owner:** Hendrixsan (Audio & Art lane)
**Issue:** https://github.com/arglifed/MegaJam26/issues/5

The C++ side of this issue is already done. The pawn and track generator expose every hook the issue asks for, and each one falls back safely while its asset is missing. This doc lists the **exact names, types and constraints** your assets need to match. If a name is off by one character, the parameter is silently ignored.

---

## 1. `MS_DirectorScore` (MetaSound Source)

**Location:** `Content/MegaJam26/Audio/MS_DirectorScore`

### Inputs (names are case-sensitive)

| Input name       | MetaSound type | Range     | Sent by C++ | Meaning |
|------------------|----------------|-----------|-------------|---------|
| `ActiveTake`     | Float          | 0.0 / 1.0 | Every frame | 0 = Take A (Studio Cut), 1 = Take B (Director's Cut) |
| `FlowRatio`      | Float          | 0.0 – 1.0 | Every frame | "Flow" intensity: current speed vs. terminal velocity |
| `SpeedRatio`     | Float          | 0.0 – 1.0 | Every frame | Raw forward speed / terminal velocity (0 when crashed) |
| `OnHardCut`      | Trigger        | –         | On event    | Player swapped takes (hard cut); also fires once as the **victory sting** when the run completes |
| `OnAspectSwitch` | Trigger        | –         | On event    | Player toggled aspect ratio (letterbox) |
| `OnGraze`        | Trigger        | –         | On event    | Near-miss graze, film-frame pickup, or a successful match cut |
| `OnCrashed`      | Trigger        | –         | On event    | Player hit an obstacle |

Source: `Source/MegaJam26/MegaJamFlowPawn.cpp`: `PushAudioParameters()` and `TriggerAudioEvent()`.

### Behaviour notes

- **`ActiveTake` snaps** between 0 and 1; C++ doesn't ramp it. Do the crossfade **inside the graph** (for example, an `Interpolate To` or `Linear To` node, ~50–150 ms) so cuts don't click.
- **`FlowRatio` sits at 0.35 before the run starts, rises with speed, and goes to 1.0 on completion and 0.0 on crash.** It's a good driver for stem intensity, filter cutoff and layer gain.
- The `OnHardCut` victory sting fires once when the run reaches **Completed**. If you want a separate victory cue, ask for an `OnCompleted` trigger and it can be added in C++.
- **Don't** build one-shot SFX as separate SoundCues that C++ would have to reference. Put them inside `MS_DirectorScore`, triggered by the inputs above. That keeps the integration to a single asset.
- The audio component is set as a UI sound (`bIsUISound = true`): no spatialization, and it keeps playing while paused.
- **Stems:** import into `Content/MegaJam26/Audio/` (e.g. `SW_TakeA_Stem`, `SW_TakeB_Stem`). Keep DAW projects and master WAVs in `RawAssets/`, which is git-ignored.

### Hooking it up

On `BP_FlowPawn` (`Content/MegaJam26/Blueprints/BP_FlowPawn`) → Class Defaults → **MegaJam|Audio → Score Meta Sound** = `MS_DirectorScore`.
It auto-plays on BeginPlay. Leave it empty and the game runs silently with no errors.

---

## 2. Modular Meshes

**Location:** `Content/MegaJam26/Art/`

### Hard rules

1. **100 × 100 × 100 UE-unit bounding box, pivot at the exact centre.** C++ places every instance by its centre and scales it by `size / 100`. A corner pivot or an off-size mesh will look offset and its collision will be wrong.
2. **Must tolerate non-uniform scaling.** The generator stretches meshes a lot on individual axes. For example, the floor is `(ChunkLength/100, CorridorWidth/100, 1)`, walls are `(…, 1, 10)` and obstacles are around `(…, 3–18, 2–9)`. Use **world-aligned / triplanar materials** or tileable detail. Baked-in bevels and decals will smear.
3. **Simple collision is required** on floor, wall and obstacle meshes (a box primitive, or a UCX_ hull). Obstacles block the player, and a mesh without simple collision can be driven straight through.
4. **+X = forward** (the run direction), +Y = right, +Z = up.

### Mesh → C++ slot mapping

All slots live on `AMegaJamTrackGenerator` under **MegaJam|Art**. Any empty slot falls back to the engine cube (or cylinder).

| Asset (issue name)    | C++ property          | Where it's used |
|-----------------------|-----------------------|-----------------|
| `SM_FloorTile`        | `FloorTileMesh`       | Track floor, stretched along chunk length |
| `SM_WallSegment`      | `WallSegmentMesh`     | Left/right corridor walls (scaled ×10 in Z) |
| `SM_Obstacle_TakeA`   | `ObstacleTakeAMesh`   | Obstacles solid in **Take A** (Studio Cut) |
| `SM_Obstacle_TakeB`   | `ObstacleTakeBMesh`   | Obstacles solid in **Take B** (Director's Cut) |
| Scene 1 prop          | `SoundstageTrussMesh` | Set pieces, **Soundstage** (0–35 s) |
| Scene 2 prop          | `SkyscraperMesh`      | Set pieces, **Overpass Night** (35–75 s) |
| Scene 3 prop          | `FilmReelMesh`        | Set pieces, **Cutting Room** (75–115 s) |
| Scene 4 prop          | `ApertureRingMesh`    | Set pieces, **Final Cut** (115–150 s); also the finish **wrap arch** |
| *(optional)*          | `FilmFramePickupMesh` | Spinning film-frame pickup rings |

Suggested prop names: `SM_Prop_SoundstageTruss`, `SM_Prop_Skyscraper`, `SM_Prop_FilmReel`, `SM_Prop_ApertureRing`, `SM_Pickup_FilmFrame`.

Set pieces are long side rails: e.g. scaled `(ChunkLength/100, 0.6, 0.6)` at ±940 Y. So a prop should read well as a **repeating strip along X**, not as a single hero object.

### Hooking it up (needs one new Blueprint)

The track generator is **spawned by the GameMode** (`AMegaJamGameMode::TrackGeneratorClass`), not placed in the map, so the meshes need a Blueprint subclass to live on:

1. Create **`BP_TrackGenerator`** in `Content/MegaJam26/Blueprints/`, with parent class `MegaJamTrackGenerator`.
2. In its Class Defaults → **MegaJam|Art**, assign the meshes above.
3. In `BP_FlowGameMode` → Class Defaults → **Track Generator Class** = `BP_TrackGenerator`.

These steps edit the shared binary assets `BP_FlowGameMode` and `BP_FlowPawn`. Coordinate before saving them (see `AGENTS.md` §5).

---

## 3. Checklist

- [ ] `MS_DirectorScore` with all 7 inputs, named exactly as above
- [ ] Take A / Take B crossfade smoothed inside the graph
- [ ] SFX for the 4 triggers built inside the MetaSound
- [ ] `SM_Obstacle_TakeA`, `SM_Obstacle_TakeB`, `SM_FloorTile`, `SM_WallSegment`: 100³, centred pivot, simple collision
- [ ] 4 scene set-piece props
- [ ] `BP_TrackGenerator` created and set on `BP_FlowGameMode`
- [ ] `Score Meta Sound` assigned on `BP_FlowPawn`
- [ ] Play `L_FlowTest`: check the cut crossfade, the graze, crash and victory stings, and that meshes have no offset or z-fighting
