# SPLICE // DIRECTOR'S CUT

Design document for our 2026 Epic AI MegaJam entry. Built on the Audio-Kinetic Flow template (see `AGENTS.md`). Convention: `+X` is forward, `Y` is lateral, `Z` is vertical.

New C++ classes listed in this document are explicitly permitted under Section 4 of `AGENTS.md`.

## 1. Take-Splicing

```cpp
UENUM(BlueprintType)
enum class EMegaJamTakeState : uint8 { StudioTake, DirectorsTake };
```

A "Take" is one of two overlapping realities on the track: **Take A = Studio Cut**, **Take B = Director's Cut**.

- Pressing `IA_Boost` calls `TriggerHardCut()`, gated by a `0.25f` cooldown (`CutCooldown`).
- `TriggerHardCut()`:
  - toggles `CurrentTake`
  - applies a brief forward impulse (`CutSpeedImpulse = 600.f`)
  - fires `OnTakeCut(EMegaJamTakeState NewTake)`
- Obstacles belonging to the **active** take have collision enabled.
- Obstacles in the **inactive** take have collision disabled (phased out) and render ghosted via the MPC.

## 2. Aspect Ratio Clamping

```cpp
UENUM(BlueprintType)
enum class EMegaJamAspectMode : uint8 { Wide_21_9, Box_4_3 };
```

Pressing `IA_Brake` (toggle, `ETriggerEvent::Started`) switches framing between `Wide_21_9` and `Box_4_3` and fires `OnAspectChanged(EMegaJamAspectMode NewAspect)`. The mode clamps the pawn's playable world-space `Y` and `Z` corridor in `StepPhysics`.

| Mode | Y bounds | Z bounds | Steering |
|---|---|---|---|
| `Wide_21_9` | `[-900.f, 900.f]` | `[60.f, 200.f]` | Lateral strafe force x `1.35f`; vertical steering locked |
| `Box_4_3` | `[-450.f, 450.f]` | `[60.f, 800.f]` | `IA_Steer.X` steers laterally (`+Y/-Y`); `IA_Steer.Y` steers vertically (`+Z/-Z`) |

## 3. `AMegaJamTrackGenerator` (new C++ Actor)

Procedurally spawns corridor chunks using `UInstancedStaticMeshComponent` pools:

- `FloorISM`
- `WallISM`
- `TakeA_ObstaclesISM`
- `TakeB_ObstaclesISM`

Chunk behaviour:

- `ChunkLength = 2000.f`
- Keeps `15` active chunks ahead of the pawn along `+X`, recycling chunks behind it.

Take handling:

- Listens to the pawn's `OnTakeCut` delegate.
- Toggles `SetCollisionEnabled` between the two obstacle pools:
  - `TakeA_ObstaclesISM`: enabled in `StudioTake`, `NoCollision` in `DirectorsTake`
  - `TakeB_ObstaclesISM`: enabled in `DirectorsTake`, `NoCollision` in `StudioTake`

Film Scenes, sequenced by run time:

| Scene | Run time | Name |
|---|---|---|
| 1 | 0-35s | Soundstage |
| 2 | 35-75s | Overpass Night |
| 3 | 75-115s | Cutting Room |
| 4 | 115-150s | Final Cut |

Obstacle density scales with scene index and `FlowRatio`.

## 4. Subsystem & GameMode Additions

- `UMegaJamFlowSubsystem` pushes these scalars to `FlowMPC` every tick:
  - `ActiveTake` (`0.0f` or `1.0f`)
  - `AspectMode` (`0.0f` or `1.0f`)
- `AMegaJamGameMode`:
  - `int32 CurrentTakeNumber = 1`, incremented in `ResetRun()`
  - `FString GetFormattedTimecode() const` returns `MM:SS:FF` at 24fps

## Canonical Genre & Vehicle Mapping

Corridor Total Width: 2400 units (`MEGAJAM_CORRIDOR_HALF_WIDTH = 1200.0f`).

| Genre | Vehicle | Obstacle theme |
|---|---|---|
| Action | Motorcycle | Cyber-barriers |
| Western | Horse | Timber barricade |
| Noir | Spy car (vintage 1960s, open top) | Roadblocks / enemy sedans |
| Chase | Snowboard | Snowdrifts / ice boulders |
| Montage (Final Cut) | Hang glider | Neon monoliths / laser gates |

