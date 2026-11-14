# DIRECTOR'S CUT

**Four Swords Pictures Presents**  
*Drop the clapperboard. Roll camera.*

**Director's Cut** is a high-speed, rhythm-infused arcade racer built for the Unreal Engine 5 MegaJam. Spanning a continuous, procedurally generated 9,000-meter track, you are thrust into the starring role of a blockbuster movie that seamlessly smash-cuts across five distinct cinematic genres. Stay in the flow state, parry obstacles on the beat, and cash in your adrenaline to dominate the Box-Office Scoreboard.

## 🎬 The Five Takes
Never stop moving. You will warp through five unique movie sets, each featuring its own bespoke vehicle, custom BPM track, aspect ratio, and survival mechanics:

1. **Scene 01: Action (120 BPM)** – Snap into 21:9 widescreen on a Riderless Motorcycle. Hit the Dolly Boost strips and execute the Beat-Splice barrier punch to trigger electric-cyan EMP shockwaves.
2. **Scene 02: Western (136 BPM)** – The camera locks into a 2.76:1 Ultra-Panavision center-lock. Mount your Horse, dodge timber barricades, and unleash the Quick-Draw Katana Beat-Slash to cleave obstacles in half.
3. **Scene 03: Noir (125 BPM)** – Bleed into stark black-and-white. Drift the Spy Car through traffic to build a 2x Adrenaline Graze on near-misses, then cash it in for a mind-bending Hitchcock Vertigo Dolly-Zoom.
4. **Scene 04: Chase (110 BPM)** – Hit the massive mountain drop-in and carve the downhill slopes on a Snowboard. Survive a 60% fast-forward speed surge and plow through snowdrifts with the invulnerable Comet Smash.
5. **Scene 05: Final Cut (120 BPM)** – Kick off the board and dive into the canyon on a Hang Glider. Manually cycle through your entire arsenal of genre powers to survive the final aerial stretch.

## ⚡ Gameplay Features
* **Grounded Arcade Physics:** Heavy terrestrial stunt hops, flush floor carving, and tight analog evasion mechanics. No floaty hovering—unless you are in the Glider.
* **Context-Aware Combat:** A single, smart action button routes your attacks based on your current genre and Director's Cut overdrive state.
* **Multi-POV Camera:** Cycle between over-the-hood Chase Standard, Wide Arcade, and 1st-Person Bumper cams on the fly.
* **The Box-Office Scoreboard:** Fly through the Golden Wrap Arch at 8,940 meters to receive your final Box-Office Gross and scene-by-scene S/A/B/C/D letter grades. Sub-52-second clears are required for the coveted S-Rank.

## 🎮 Controls (Keyboard / Gamepad)
* **W / A / S / D** – 4:3 Stunt Steer & Hop
* **SpaceBar / Shift** – Hard Cut / Contextual Scene Power (Beat-Splice, Slash, Graze Cash-In, Smash)
* **Left Ctrl / C** – Director's Cut Overdrive
* **Tab / R** – Montage Power Cycle (Scene 05)
* **V** – Cycle Camera POV

## 🏗️ Development, Credits & The Glass Box Architecture
Developed by **Anthony Lewis (`arglifed`)** handling Gameplay & Systems, and **Chris Hendrickson (`ninjacom`)** handling Art & Audio.

This project was built targeting the **Glass Box Award**, utilizing transparent, heavily documented procedural C++ systems, strict lane separation, and highly optimized parallel AI collaboration workflows to prevent merge conflicts:

* **C++ Lane (Gameplay & Systems):** All procedural track generation, physics, contextual input routing, and scoring math are handled in pure C++ (`AMegaJamTrackGenerator`, `AMegaJamFlowPawn`, `AMegaJamGameMode`, `UMegaJamFlowSubsystem`). Variables are cleanly exposed to the Editor via `UPROPERTY` structs for live tuning.
* **Blueprint Lane (Art & Audio):** All mesh integration, material swapping, MetaSound beat-syncing, and Niagara VFX are handled in Blueprints (`BP_TrackGenerator`, `BP_FlowPawn`), consuming the exposed C++ hooks.

### AI Collaboration Workflow
To prevent merge conflicts and binary corruption, the team utilized parallel AI agents with hard cross-agent boundaries:
* **Backend:** Automated pure-C++ implementation via local LLM IDE integration (Gemini 4 Argon). Zero `.uasset` modifications allowed.
* **Frontend:** Art polish, mesh scaling, and material tweaks executed via Claude, isolating asset updates to a parallel branch.
* **Handoff:** Transparent GitHub Issue tracking and PR merging ensured the C++ foundation was locked and synced before Blueprint nodes were ever wired.

## 🛠️ Building from Source
1. Clone the repository: `git clone https://github.com/arglifed/MegaJam26.git`
2. Generate Visual Studio project files via your `.uproject`.
3. Compile the `MegaJam26Editor Win64 Development` target.
4. Open the Editor and launch `L_FlowTest` to roll camera.
