# Antigravity Agent Directives: Epic AI MegaJam 2026 Template

## 1. Workflow Mandate (No Black-Box Generation)
- **Explore-Plan-Execute:** You must NEVER generate code or folders immediately. Always run in plan mode (`agy --mode=plan`) to map out C++ headers, `.Build.cs` dependencies, and folder structures, and wait for human approval before execution.
- **Visibility:** Do not use heavy abstractions or opaque third-party wrapper libraries. Physics math, velocity calculations, and audio data bridges must be transparent, flat, and readable in C++.

## 2. Directory Structure Constraints
- **Root Sandbox:** ALL project assets, blueprints, and content must be contained within a single root folder: `Content/MegaJam26/`.
- **Flat Hierarchy:** Strictly use a flat subdirectory structure:
  * `Content/MegaJam26/Core` (GameMode, PlayerController, GameInstance/Subsystems)
  * `Content/MegaJam26/Blueprints` (Pawn and Track/Obstacle Actor blueprints)
  * `Content/MegaJam26/VFX` (Materials, Material Parameter Collections, Niagara Systems)
  * `Content/MegaJam26/UI` (UMG Widgets: MainMenu, HUD, Pause, GameOver)
  * `Content/MegaJam26/Audio` (MetaSounds, Stems, Audio Synesthesia NRT assets)
  * `Content/MegaJam26/Maps`
- **Zero Clutter:** Never place loose assets in the top-level `Content/` directory.
- **No Modular Game Features:** Do NOT generate modular Game Feature plugins. Keep the project monolithic for a 96-hour jam timeline.

## 3. Build Configuration & Engine Modules
- **Plugin & Module Dependencies:** Ensure `.uproject` and `MegaJam26.Build.cs` explicitly enable and link:
  * `EnhancedInput`
  * `UMG`
  * `Niagara`
  * `AudioSynesthesia`
  * `MetasoundEngine`

## 4. C++ Architecture Rules (Audio-Kinetic Flow Template)
- **No ACharacter:** Do NOT use `ACharacter` or `UCharacterMovementComponent`.
- **Core Base Classes Only:** Generate ONLY these four foundational C++ classes:
  1. `AMegaJamGameMode`: Handles run state (PreRun, ActiveFlow, Crashed, Completed), run timer, and score/multiplier math.
  2. `AMegaJamPlayerController`: Manages Enhanced Input mapping contexts and UI focus switching (Game vs. Pause/Menu).
  3. `AMegaJamFlowPawn` (inherits from `APawn`): 
     - Uses a collision primitive (`USphereComponent` or `UBoxComponent`) as the RootComponent with raw C++ physics/velocity control in `Tick()`.
     - Computes a normalized `FlowRatio` (`0.0f` to `1.0f` mapping current velocity to terminal velocity).
     - Includes a near-miss overlap detector component (`USphereComponent`) to trigger `OnObstacleGrazed` events for multiplier boosts.
  4. `UMegaJamFlowSubsystem` (inherits from `UTickableWorldSubsystem`):
     - Acts as the central data bridge between gameplay physics, audio stems, and shaders.
     - Exposes `FlowRatio`, `CurrentMultiplier`, and `AudioEnvelopeValue` to Blueprints so a global Material Parameter Collection (MPC) and MetaSound parameters can be updated every frame without spaghetti references.
- **Blueprint & Shader Integration:** Expose tunable physics variables (Acceleration, Drag, StrafeForce, TerminalVelocity, GrazeRadius) via `UPROPERTY(EditAnywhere, BlueprintReadWrite, Category="MegaJam|Physics")` and reactive events via `UFUNCTION(BlueprintImplementableEvent, Category="MegaJam|Flow")`.
- **Enhanced Input:** Bind the Enhanced Input subsystem in `AMegaJamFlowPawn` and `AMegaJamPlayerController` with virtual hooks for `Input_Steer` (2D Vector), `Input_Boost` (Bool), and `Input_Brake` (Float).


## 5. Multi-Dev Co-Op Rules (Binary Asset Safety)
- **Never Overwrite Binary Assets:** Do NOT overwrite, regenerate, delete, or rename any `.uasset` or `.umap` binary file unless the user has explicitly named that specific file. These files are Git LFS-tracked, cannot be merged, and are shared with other developers (e.g. Hendrixsan).
- **Prefer Modular C++:** Implement gameplay logic in modular C++ classes and components (`UActorComponent` / `USceneComponent` subclasses, subsystems) rather than monolithic Blueprint graph logic. Keep Blueprints thin: data/tuning, asset references, and `BlueprintImplementableEvent` hooks only. This keeps changes diff-able and merge-friendly.
- **No MCP/Python Asset Saves:** Never modify or save `.uasset` or `.umap` binary files via MCP or Python (e.g. Unreal MCP tools, `unreal` editor scripting, `Scripts/*.py`) unless the file is explicitly named in the prompt. Prefer modular C++ classes/components.
- **Ask Before Touching Shared Assets:** If a task would require editing an existing `.uasset`/`.umap`, stop and ask which file may be modified, or create a new uniquely-named asset instead.

## 6. Issue-Driven Workflow & Branching
- **Branch Per Issue:** Always create and check out a dedicated feature branch named after the GitHub issue (e.g., `feature/issue-1-track-gen`) before writing any code.
- **Issue Is the Spec:** Treat the GitHub issue text as the primary task specification. If the issue is ambiguous or conflicts with these rules, ask before proceeding.
