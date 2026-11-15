# Director's Cut: how we built it (MegaJam 2026 dev log)

<!--
Living draft for issue #121 (Glass Box award). Freeze: 5:00 PM PST, Nov 15.
- Fill the TODO comment spots by hand. Edit anything outside the STATS markers freely.
- The STATS block is rewritten by: python Scripts/devlog_stats.py
- Public post rules: the repo is private, so no repo links, and images go to itch directly.
  No emails, secrets or raw transcript text.
-->

**Team Four Swords Pictures:** arglifed (gameplay C++) and Hendrixsan (art and audio).
**Built:** Nov 12–15, 2026, in Unreal Engine 5.8, for the Epic AI MegaJam.

<!-- HERO IMAGE: Docs/devlog_images/itch_cover_1260x1000.png (the itch cover: all five vehicles, one panel per genre;
     rebuilt with python Scripts/make_itch_cover.py). Alternative: playtest_captures/orb_review_povs.png.
     Optional upgrade: a fresh in-game capture or GIF with the HUD visible. -->

**Drop the clapperboard. Roll camera.** *Director's Cut* is a rhythm arcade racer where you're the star of a blockbuster that won't stop rolling. The whole film is one continuous 9,000-metre take that smash-cuts across five genres. Each genre swaps your vehicle, the music's tempo, the colour grade and even the aspect ratio of the screen:
- a widescreen **Action** chase on a motorcycle
- an Ultra-Panavision **Western** on horseback
- a black-and-white **Noir** run in a spy car
- a downhill **Chase** on a snowboard
- a **Final Cut** hang-glider dive through a golden film-aperture tunnel

<!-- SCREENSHOTS (gallery, in race order), all in Docs/devlog_images/screenshots/:
     01_action_motorcycle.png, 02_western_horse.png, 03_noir_spycar.png,
     04_chase_snowboard.png, 05_finish_arch.png (the Golden Wrap Arch at the end of the Final Cut) -->

You steer, hop and graze obstacles on the beat to stay in flow. Each genre gives you its own special move: a barrier punch, a katana slash, an adrenaline cash-in or a comet smash. Your Director's Cut overdrive flips the set into its alternate take. Reach the Golden Wrap Arch and the Box-Office Scoreboard grades every scene from S to D.

Two people built it in four days. One worked in C++ and one on art and audio, each with an AI partner. The rest of this post shows how we did it: the rules we gave the AIs, what they made, what broke, and the numbers.

## How we worked

### Two lanes, one repo
We split the work on day one, and the reason was hardware. Hendrixsan's machine is a gaming laptop almost ten years old; arglifed's is a desktop tower with modern components. In Unreal, iteration speed is how fast you can compile, load the editor and play-test, and arglifed could do all three much faster. So arglifed took the core gameplay, the work that needs the most build-and-play loops. Hendrixsan took art and sound and supported the build in every other way: generating assets, writing specs and contracts, running automated play-test captures, and doing the submission paperwork.

- **Gameplay lane (arglifed):** all game logic in modular C++. That covers the flow pawn, game mode, the track generator, the HUD drawn in C++ (`UMegaJamCinemaHUD`) and the MetaSound bridge. Blueprints stay thin: tuning values and asset references only.
- **Art and audio lane (Hendrixsan):** Meshy-generated meshes, materials and textures, plus music and SFX wired through MetaSounds. Kept in `Content/MegaJam26/Art` and `/Audio`.
- **The rule that made it work:** `AGENTS.md` told every AI agent never to overwrite, regenerate or rename a shared `.uasset`/`.umap` unless that file is named in the prompt. Binary assets can't be merged, so the two lanes never collided. Changes moved between lanes as C++ hooks and written contracts, for example the #55 art pipeline contract: genre table, seat offsets, obstacle skins.
- **Issue-driven:** every task was a GitHub issue, every change a branch and PR named after it. The issue text was the spec the AI agents worked from.

### Talking to each other
We met in person on the first night to brainstorm the game. Ideas went on the table: five movie genres, one continuous take, aspect ratio as a mechanic. By the end of the night we'd split the work into the two lanes. After that we mostly talked on Discord, to plan, write and work through issues and to coordinate merges. GitHub was where the work itself lived.

### Handing work across the lanes
The hand-offs were smooth and easy, mostly because we were both using AI. Each of us could write a precise issue for the other side and have our own agent read the other's issue as a spec. A few patterns kept repeating:
- **Gameplay asks for art.** arglifed opened "Art Integration" issues listing which meshes, VFX and UI the C++ side expected and where it would look for them (#40, #41, #46, #47). Hendrixsan answered with a written **art pipeline contract** (#55): the genre table, seat offsets for each vehicle, obstacle skins and canonical dimensions. Both agents then built against that contract.
- **Art finds engine bugs.** Hendrixsan's play-test captures turned up problems in the C++ side: corridor walls tall enough to hide the studio dressing (#88), the beat clock drifting away from the MetaSound score (#74), matrix ensures still firing in PIE (#123). These were filed as issues for arglifed, with screenshots and repro steps.
- **Gameplay builds for art.** When some generated props had nowhere to go, arglifed built a prop scatter system for each scene to place them (#90 for #87). When the orb got its new glow (#108), arglifed added a dynamic point light to match (#112) in parallel.
- **Holding a merge.** Big physics changes waited for the art merge (#80, "Held for Art Merge"). Neither lane ever had to resolve a conflict in a binary asset.

### The AI toolchain
- **arglifed:** the Antigravity IDE for the C++ lane, using four tools:
  - **Gemini 4 Argon** for gameplay architecture, procedural track and skyline generation, the camera and aspect-ratio systems, and PR merges.
  - **Gemini 4 Flash** for targeted bug fixes, HUD and tutorial patches, and GitHub CLI and script tasks.
  - **Gemini web chat** for system planning, prompt engineering, play-test checklists and submission copy.
  - **Qwen2.5-coder 7B** running locally in Ollama on an RTX 5080, for offline C++ error parsing and script checks.

  The coding agents were never allowed to touch a `.uasset` or `.umap`. Every task went through a structured plan-then-execute prompt on its own feature branch. Even environment polish (the procedural skyline, the soundstage dressing, the tutorial gates) was built as readable C++ that runs at runtime, never as edits to the binary map.
- **Hendrixsan:** Claude Code in a PowerShell terminal and in Unreal's built-in terminal for the art and audio lane:
  - import scripts and material wiring
  - Meshy generation through its MCP server
  - automated play-test captures
  - issue and PR write-ups
  - Claude Desktop chat for brainstorming, drafting Meshy prompts and checking licence terms
- **Meshy AI:** text → 3D (and text → image → 3D) for all 31 meshes. See the Meshy case study below.
- **Explore → plan → execute:** agents had to plan before touching code and wait for human approval. `AGENTS.md` bans "black-box generation".

## Timeline

<!-- Taken from merged PRs. Trim to the highlights; add a picture per day if possible. -->

**Thu Nov 12: the skeleton.** We started from our pre-jam template (declared below). Within the first afternoon:
- the Director's Cut systems and the procedural track generator (#2)
- the C++ cinema HUD, camera juice and the MetaSound bridge (#6)
- 4-scene set dressing (#7)
- a complete game loop (#9, #10)
- aspect-ratio push/pull (#14)

**Fri Nov 13: five movies.**
- Pause menu, camera shake and hitstop (#16).
- First audio wiring and the asset hand-off contract (#13). The Action vertical slice with on-beat cuts (#25).
- Western (#42), Alpine/Chase (#54), Noir Spy Car (#63) and the Final Cut cliff-jump (#65), one after another.
- The Box-Office Scoreboard (#67) and per-scene BPM math (#71).
- The Meshy art pipeline came online (#61) and the director score MetaSound took over music and SFX (#44).

**Sat Nov 14: play-test, break, fix.**
- Beat-clock sync fixes (#75), then a long run of play-test polish (#81, #83, #85).
- Grounded physics and stunts (#89), per-scene prop scatter (#91).
- A cluster of NaN and zero-scale crashes, hunted down one by one (#95, #97, #99, #103).
- Art polish (#86, #109), fog and vista silhouettes (#107).
- The movie magic orb (#114) and the interactive Rehearsal tutorial (#118).

**Sun Nov 15: ship it.**
- Last NaN bounds fixes (#122, #124).
- The packaging script (#125).
- Credits and licence checks for every asset (#126, #127).
- This dev log.
- Gamepad support: PS5 DualSense and 6-button USB joysticks, with controller prompts in the HUD (#130).
- A procedural studio skyline, volumetric clouds and darker fog, all spawned from C++ by the track generator (#131).
- HUD spacing polish, the title changed to DIRECTOR'S CUT!, and one more camera NaN fix (#132).
- The Rehearsal tutorial gained a Director's Cut step: you have to use the overdrive before the tutorial lets you go on (#133). A follow-up fix makes sure only the Director's Cut button passes that step (#134).
- Per-genre "bad take" title cards on a crash: THE END, HERO DOWN, CASE CLOSED, WHITEOUT, BURIED, TO BE CONTINUED... (#135).
- A last pass on the Rehearsal tutorial: its steps run in the right order, the HUD shows the right prompt first, and the Director's Cut step completes cleanly when you press it (#136).
<!-- TODO: at the freeze, add anything merged after #135/#136 (gh pr list --state merged) -->

## Case studies

### 1. From a text prompt to a mesh in the game: the Meshy pipeline
<!-- PICTURE: Docs/devlog_images/art_review_sheets/meshes_textures/glider_old_views.png next to glider_new_views.png (the Hang Glider redo) -->
All 31 meshes in the game came from Meshy AI. Thirty were generated by Claude Code through Meshy's API (MCP server); the Motorcycle was made by hand on a free account (text → 2D image → 3D). The pipeline settled into these steps:

1. **Write a prompt for a game-ready prop.** Every prompt ended with the same style tail: *"stylized low-poly, clean flat shading, bold simple shapes, game-ready, single object, centered, no background, no text or labels"*. That kept 31 assets made over two days looking like one set.
2. **Generate with a triangle budget.** Meshy's smart-topology model gave clean low-poly meshes at a fixed budget: 8,000 triangles for vehicles, 3,000 for obstacles, 6,000 for props and set pieces. Textures came as 2K PBR maps (base colour, normal, metallic, roughness).
3. **Import with a script, never by hand.** A Python script imports every mesh and builds its material instance. A second script scales textures down from 2K to 1K. It can rebuild any one asset (`ONLY=<names>`), so a fix never means clicking through the editor again.
4. **Check in game, then fix.** Each batch was checked with the automated play-test captures (case study 2).

What fought back:
- **Riderless vehicles.** The player is a glowing orb riding each vehicle, so the vehicles had to be empty. Meshy kept giving the horse a rider. The prompt grew from "riderless saddled horse" to *"a lone unmounted horse with nobody on it … no rider, no jockey, no human, no figure on top"*.
- **The Hang Glider.** After several text-to-3D attempts, the one in the game was a 1.7 cm-thick flat cut-out. The fix was a two-step route: generate a 2D design image first, then turn that image into 3D. The wing came out with real thickness and camber.
- **Shiny obstacles read as the wrong colour.** The Cyber Barrier looked cream and the Monolith beige. Low Meshy roughness was mirroring the sky and the cream backdrop flats. A `RoughnessMin` clamp on the shared material fixed both without touching the textures.
- **Scale and facing.** The Spy Car was 4.5 m long and facing backwards, blocking the road; the first Hang Glider was 9 m wide. Each was fixed in the import script, so the next rebuild kept the fix.

**Lesson:** Meshy is excellent at *what* an object is and unreliable on *how it's built*. Nail the style with a fixed prompt tail, give it a triangle budget, and plan for a scripted import and a visual check rather than trusting the first result.

### 2. Letting the AI play-test the game
<!-- PICTURE: Docs/devlog_images/playtest_captures/sheet_s1..s5.png (one per scene) or arch_sheet.png (the finish arch) -->
On a laptop almost ten years old, play-testing every art change by hand across a 9 km track and five scenes would have eaten the jam. Instead, Claude Code play-tested the game itself:

- It launches the full Unreal editor on `L_FlowTest` with a Python script, starts Play-In-Editor and begins a run.
- It turns on God Mode (the pawn doesn't steer itself and would keep crashing). Then it skips scenes with the same call as the F7 debug key.
- It reads the run distance, slows time and takes high-res screenshots at set distances. For close-ups it puts its own camera in the level.
- It closes the editor and stitches the shots into contact sheets: every scene, every camera view, side by side.

With God Mode on, getting from the start of scene 5 to the finish arch at 8,940 m took about 40 seconds instead of 3 minutes. One full capture pass took a few minutes and needed nobody at the keyboard.

**What it caught:**
- The cream Cyber Barrier from case study 1. The script froze time (time dilation 0.0001), then changed one material parameter after another on the live obstacles between screenshots, and finally hid the mesh to prove which object was which. That traced the colour to roughness and reflections, not the texture.
- Studio dressing poking through the walls into the play area.
- Corridor walls tall enough to hide all the set dressing (#88).
- After the #119 mesh fix, old-vs-new captures confirmed the rebuilt meshes looked identical.

**Gotchas we hit along the way:**
- Editor asset loading fails during play.
- Some properties can only be set on the Blueprint's defaults before play starts.
- Python can't spawn actors mid-play, so the camera goes in before play starts.
- High-res shots sometimes arrive one frame late.
- Two editors don't fit in 16 GB of RAM, so the user's own editor has to be closed first.

### 3. The movie magic orb (#108)
<!-- PICTURE: Docs/devlog_images/playtest_captures/orb_review_seats.png (the orb on every vehicle) -->
The player character is the orb: the star of the film, riding whatever vehicle the current genre hands it. Until the last evening it was the engine's default checker sphere.

- **Built from code, not generated.** `SM_MovieMagicOrb` is a procedural mesh made by a Python script, so no Meshy credits were spent. It has three parts:
  - a glowing core
  - two crossing film-strip belts with frames running round them in opposite directions
  - six twinkling glints

  It is 100 cm across with a centred pivot. The pawn's existing 0.55 scale lands it at 55 cm, inside the 35 cm-radius collision sphere, with no C++ change.
- **Driven by gameplay through the existing contract.** The material reads the colour values the pawn already writes every frame: chain tint, flow, burst flash and beat pulse. So the orb pulses on the beat and changes colour with your combo, with no new code. The three parts are told apart by texture coordinate, so one material drives all of them.
- **Seat offsets fitted from the meshes.** Where the orb sits on each vehicle was first fitted from each mesh's top-surface profile, then checked in automated captures from every camera. Examples:
  - Motorcycle: in the saddle at (-42, 0, 72) cm
  - Horse: on the saddle at (-15, 0, 181)
  - Spy Car: in the cockpit, showing over the doors
  - Snowboard: on the deck with the board still readable under it
  - Hang Glider: inside the A-frame
- **Cross-lane in parallel.** While the orb was being built, arglifed added a dynamic point light that follows it (#112). The art PR only hooked the orb up to the pawn Blueprint in a time slot agreed with arglifed, so nothing was overwritten.

## What went wrong (and how we fixed it)

**Engine and code**
- **The NaN cascade (Sat).** Once the game ran end to end, the log filled with NaN errors and engine ensures:
  - zero-length vectors on the track start
  - camera matrices built from bad values on the title screen
  - instanced meshes placed at zero scale
  - a legacy floor collision that locked the pawn for the first 190 m

  arglifed fixed them one at a time in a run of PRs (#95, #97, #99, #103). A last flood of about 4,200 `InverseFast` ensures per play session sat on the render thread with no gameplay callstack. It turned out to be a zero-scale family again: something the art lane first thought was a mesh bug (#119) and split out once the evidence pointed elsewhere (#123). The final fix made safe instance transforms a rule in code (#124): every procedural scale is clamped to at least 0.05.
- **NaN mesh bounds (#119).** Three meshes logged "NaN in Bounds" on every load. The import script used Unreal's fast mesh build, which in UE 5.8 writes the render data to the derived-data cache *before* the bounds are set, so garbage got cached. Re-importing didn't help, because the cache key comes from the mesh content. The fix was a full build plus a 0.1 mm offset to force a fresh cache key. A vertex-by-vertex check confirmed the meshes were otherwise identical (#122).
- **Beat-sync drift.** The beat clock behind on-beat moves ran free from level start, while the music restarted at every scene change and every F7 warp and changed pitch in bullet time. So "on the beat" slowly stopped matching what you heard. The art and audio lane found this by reading the C++ and wrote up the three causes (#74). arglifed tied the clock to the score's playback and added NaN guards for pitch and warp maths (#75, #77).

**Assets**
- **Meshy's habits.** Riders on the vehicles, a paper-thin Hang Glider, mirror-shiny obstacles, cars at the wrong scale. Covered in case study 1.
- **Git LFS quota.** Binary assets live in Git LFS, which has a storage quota. On the first afternoon we kept raw source files (WAV masters, source meshes, 2K textures) out of the repo altogether (`RawAssets/` is git-ignored). Devlog screenshots stay out too.

**Hardware and tooling**
- **One editor at a time.** On Hendrixsan's 16 GB laptop, two copies of the Unreal editor didn't fit in memory, so the automated play-test could only run with the user's editor closed. A cold editor load took about 8 minutes, and one capture run was killed by a 5-minute timeout before the editor had finished starting. Later runs allowed for that.
- **Stuck builds.** Some C++ builds hung, with the compiler sitting at a few seconds of CPU forever. Killing the build and rebuilding with Unreal's distributed build accelerator turned off (`-NoUBA`) got past it every time.
- **Lost scratch work.** Claude Code's per-session scratch folder is wiped when a session restarts, and the first capture scripts went with it. After that, reusable scripts lived in a durable temp folder and were noted down so the next session could find them.

## By the numbers

<!-- STATS:BEGIN -->
_Generated 2026-11-15 16:05 Eastern Standard Time by `Scripts/devlog_stats.py` from `origin/main`. Jam window starts 2026-11-12; earlier activity is listed as pre-jam._

### AI usage

| Person | Tool | Sessions | Prompts | Model turns | Tool calls | Input tok | Output tok | Cache write | Cache read | Models |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| Hendrixsan | Claude Code (PowerShell) | 16 | 165 | 1,058 | 1,322 | 2,334 | 896,328 | 5,249,818 | 197,249,984 | claude-opus-5-5 (839), claude-fable-5-2 (219) |
| Hendrixsan | Claude Code (UE Terminal) | 4 | 26 | 106 | 94 | 248 | 57,679 | 195,222 | 8,593,309 | claude-opus-5-5 (106) |
| arglifed | Antigravity IDE (C++ architecture, track/skyline generation, camera systems, PR merges) | ~18 | ~65 | - | - | n/a | n/a | - | - | Gemini 4 Argon |
| arglifed | Antigravity IDE (targeted bug fixes, HUD/tutorial patches, GitHub CLI and scripts) | ~12 | ~40 | - | - | n/a | n/a | - | - | Gemini 4 Flash |
| arglifed | Gemini web chat (system planning, prompt engineering, play-test checklists, submission copy) | ~15 | ~95 | - | - | n/a | n/a | - | - | Gemini |
| arglifed | Ollama, local on an RTX 5080 (offline C++ error parsing, script checks) | ~8 | ~30 | - | - | n/a | n/a | - | - | Qwen2.5-coder 7B |

_Cache read is the context re-sent to the model on each turn and billed at a discount. It is reported separately so it doesn't inflate the other numbers. "Model turns" counts unique assistant messages._

#### Claude Code per day

| Day | Sessions | Prompts | Model turns | Output tok |
|---|---:|---:|---:|---:|
| Thu Nov 12 | 4 | 30 | 113 | 70,911 |
| Fri Nov 13 | 7 | 52 | 388 | 308,371 |
| Sat Nov 14 | 5 | 70 | 393 | 368,372 |
| Sun Nov 15 | 7 | 39 | 270 | 206,353 |

### Meshy (3D generation)

- Jam account: Meshy Pro: 1,000 credits from the jam partner perk on top of the free plan's 100, so 1,100 in total. 587 left on 2026-11-15, so 513 spent
- Tasks on jam account: 68: 66 text-to-3d (preview + refine passes, including retries), 1 text-to-image and 1 image-to-3d (the Hang Glider redo)
- Models in game: 31 (30 on the jam account, plus the Motorcycle made on a free account)

### Repository

- **170 commits** on main (Nov 12: 28, Nov 13: 49, Nov 14: 67, Nov 15: 26)
- **26** commits carry a Claude co-author line
- **65 pull requests** (64 merged): arglifed 54, ninjacom 11
- **71 issues** (70 closed): arglifed 53, ninjacom 18
- **13,447 lines of C++** in 15 files under `Source/`
- **219 assets** (`.uasset`) and 1 map under `Content/MegaJam26/`
- **7 scripts** in `Scripts/`
<!-- STATS:END -->

## Declarations

- **Prepared before the jam (Nov 12):** the commit "Pre-jam UE 5.8 C++ Audio-Kinetic Flow Template & MCP setup". That is the four baseline C++ classes (GameMode, PlayerController, FlowPawn, FlowSubsystem), the module setup and the MCP configuration. It also included the first `AGENTS.md` guardrails and the Antigravity and Ollama IDE setup. Everything else was made during the jam.
- **AI-generated assets:** all 31 meshes come from Meshy AI, from our own text prompts. 30 were made on Meshy Pro (the jam perk) and the Motorcycle on a free account (CC BY 4.0, credit Meshy).
- **Licensed audio:** all music and SFX come from WeLoveIndies under its Epic MegaJam project licence.
- Full credits follow below and are also on the game page.

## Credits

**Team Four Swords Pictures**
- **arglifed:** gameplay, systems and C++
- **Hendrixsan:** art, audio and tooling

**3D models: [Meshy AI](https://www.meshy.ai).** All 31 models were generated with Meshy AI from our own text prompts, with no third-party images or brand references:
- **Vehicles:** Motorcycle, Horse, Spy Car, Snowboard, Hang Glider
- **Obstacles:** Cyber Barrier, Timber Barricade, Roadblock, Snowdrift, Ice Boulders, Monolith, Laser Gate
- **Props and pickup:** Grave Cross, Tombstone, Black Sedan, Camera Crane, Film Frame
- **Set pieces:** Wrap Arch, Film Reel, Aperture Ring
- **Studio dressing:** Lighting Truss, Backdrop Flat, Studio Spotlight, Boom Mic, Cactus, Tumbleweed, Street Lamp, Skyscraper, Pine Tree, Snow Rock, Floating Crystal

The Motorcycle was made on a free Meshy account and is used under [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/) with credit to Meshy. The other 30 were made on Meshy Pro through the jam's partner perk.

**Music and sound effects: [WeLoveIndies](https://www.weloveindies.com).** Used under the WeLoveIndies licence for the 2026 Epic AI MegaJam project (non-commercial).
- **Music:** "Too Clever" (Action), "Brixxx" (Western), "Lucid Dreaming" (Noir), "Outbreak" (Chase), "The Snatcher" (Final Cut)
- **Sound effects:** clapperboard hit, futuristic sell, positive pitch rise, vocal cut, cinematic whoosh

**Made by us:** the movie magic orb (a procedural mesh and material), the track materials, the MetaSound score that plays and syncs the music, all C++ gameplay code and all tooling scripts.

**Built with:** Unreal Engine 5.8, used under the Unreal Engine EULA.
**AI tools:** Claude Code and Claude Desktop (Anthropic), Gemini 4 Argon (arglifed's IDE), and Meshy AI.
