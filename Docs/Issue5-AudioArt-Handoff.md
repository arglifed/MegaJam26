# Issue #5 Handoff: Audio & Art Asset Contract

**Owner:** Hendrixsan (Audio & Art lane)
**Issue:** https://github.com/arglifed/MegaJam26/issues/5

The C++ side of this issue is already done. The pawn and track generator expose every hook the issue asks for, and each one falls back safely while its asset is missing. This doc lists the **exact names, types and constraints** your assets need to match. If a name is off by one character, the parameter is silently ignored.

---

## 1. `MS_DirectorScore` (MetaSound Source)

**Location:** `Content/MegaJam26/Audio/MS_DirectorScore`. **Status:** built for Issue #24 ([#24](https://github.com/arglifed/MegaJam26/issues/24)).

**The score owns all music and SFX (Option A).** While it's playing, `MusicAudioComponent` stays silent and every C++ `PlaySound2D` one-shot is replaced by a named score trigger, so nothing plays twice. Clear **Score Meta Sound** on the pawn to get the old C++ audio back as a fallback. It is the default in C++ (`ConstructorHelpers`), so `BP_FlowPawn` needs no edit.

**The asset is generated. Don't hand-edit it.** Rebuild it with `Scripts/build_director_score.py` (headless; the run command is in the script header). Change the script, not the asset, so the graph stays reviewable as text.

### Which sounds play: C++ is the source of truth

The pawn's sound properties (`RunStartSound`, `FilmFramePickupSound`, `DirectorsCutEnterSound`, …) still choose the sounds. In BeginPlay C++ hands each one to the score as a **WaveAsset** input. The genre-to-track mapping also stays in C++ (`SceneMusicTracks`): the score just loops whatever `MusicWave` it receives. To swap a sound, change the property on `BP_FlowPawn`. It has to be a **SoundWave**: a SoundCue override leaves that cue silent in the score.

### Inputs (names are case-sensitive)

| Input | Type | Sent by C++ | Used for |
|---|---|---|---|
| `ActiveTake` | Float 0/1 | Every frame | Smoothed ~100 ms in the graph → high-pass 20 Hz → 250 Hz (Director's Take thins the low end, no clicks) |
| `FlowRatio` | Float 0–1 | Every frame | Music low-pass 4 kHz (stopped/crashed) → 20 kHz (full speed) |
| `SpeedRatio` | Float 0–1 | Every frame | Not wired yet (free for an intensity layer) |
| `OnHardCut`, `OnGraze`, `OnAspectSwitch` | Trigger | On event | Kept for compatibility; not wired, since each fires for several events |
| `OnCrashed` | Trigger | Crash (also God Mode hits) | Crash cue (`CrashWave`) |
| `MusicWave` | WaveAsset | Scene change | Current genre's track (from `SceneMusicTracks`) |
| `OnSceneChanged` | Trigger | Scene change | Restarts the looping music player with the new `MusicWave` |
| `SceneGenre` | Int32 0–4 | Scene change | Informational (Action, Western, Noir, Chase, Montage) |
| `MusicPitchSemitones` | Float | Every frame | Pause / title / bullet-time pitch, = 12·log2(C++ pitch) |
| `MusicVolume` | Float | Every frame | Music gain in the mixer (pause 0.42, title 0.68, play 0.90, overdrive ~1.05) |
| `ChainPitchSemitones` | Float | Just before a chain cue | Pitch for `OnBeatSplice` / `OnBeatCut` (rises with SpliceChain) |

### SFX cues (one trigger, one wave input, its own Wave Player)

| Trigger | Wave input (default) | Pitch | Gain | Fired by |
|---|---|---|---|---|
| `OnRunStart` | `RunStartWave` (ClapperboardHit) | 0 | 1.0 | Run start, clapperboard transition |
| `OnFilmFrame` | `FilmFrameWave` (FuturisticSell) | 0 | 1.0 | Film Frame pickup (off-beat) |
| `OnDollyBoost` | `DollyBoostWave` (PositiveRise) | 0 **and** +8.1 st layer | 1.0 | Dolly Boost strip |
| `OnDirectorsCutEnter` | `DirectorsCutEnterWave` (**CinematicWhoosh**) | 0 | 1.05 | **Activating** 21:9 / 2.76:1 only |
| `OnDirectorsCutExit` | `DirectorsCutExitWave` (ClapperboardHit) | −2.8 st | 0.5 | Back to 4:3 (manual or meter empty) |
| `OnBeatSplice` | `BeatSpliceWave` (PositiveRise) | chain | 1.0 | Beat-Splice harvest, on-beat Western Beat-Slash |
| `OnBeatCut` | `BeatCutWave` (FuturisticSell) | chain | 0.95 | On-beat cut |
| `OnCrashed` | `CrashWave` (VocalCut) | 0 | 1.0 | Crash |
| `OnCompleted` | `VictoryWave` (PositiveRise) | −5 st | 1.0 | Run reaches Completed (victory) |

### Behaviour notes

- **Retriggers restart.** Each cue has one Wave Player, so a cue fired again before it ends restarts instead of overlapping (unlike `PlaySound2D`). This is most noticeable on fast chained splices.
- **Pause:** the score is a UI sound (`bIsUISound = true`) and the pawn ticks while paused, so music keeps playing, ducked to pitch 0.76 / volume 0.42.
- **Mix:** music plus the first 7 cues go into an 8-input stereo mixer, which feeds a 4-input mixer with the last 3 cues, then out. Adjust gains in the `cues` table in the script.
- Keep DAW projects and master WAVs in `RawAssets/` (git-ignored). Original filenames for licensing are in `RawAssets/Audio/SOURCE_IDS.txt`.

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

- [x] `MS_DirectorScore` built (Issue #24): the score owns music + SFX, inputs as above
- [x] Take A / Take B change smoothed inside the graph
- [x] SFX cues built inside the MetaSound
- [ ] `SM_Obstacle_TakeA`, `SM_Obstacle_TakeB`, `SM_FloorTile`, `SM_WallSegment`: 100³, centred pivot, simple collision
- [ ] 4 scene set-piece props
- [ ] `BP_TrackGenerator` created and set on `BP_FlowGameMode`
- [x] `Score Meta Sound` is the C++ default, inherited by `BP_FlowPawn`
- [ ] Play `L_FlowTest`: check the cut crossfade, the graze, crash and victory stings, and that meshes have no offset or z-fighting
