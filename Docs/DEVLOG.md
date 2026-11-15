# Director's Cut: how we built it (MegaJam 2026 dev log)

<!--
Living draft for issue #121 (Glass Box award). Freeze: 5:00 PM PST, Nov 15.
- Fill the TODO comment spots by hand. Edit anything outside the STATS markers freely.
- The STATS block is rewritten by: python Scripts/devlog_stats.py
- Public post rules: the repo is private, so no repo links, and images go to itch directly.
  No emails, secrets or raw transcript text.
-->

**Team Four Swords Pictures:** Anthony (gameplay C++) and Hendrixsan (art and audio).
**Built:** Nov 12–15, 2026, in Unreal Engine 5.8, for the Epic AI MegaJam.

<!-- TODO: one-paragraph pitch, plus a hero screenshot or GIF (capture fresh, or pick from %LOCALAPPDATA%/Temp/claude87) -->
*Director's Cut* is a high-speed, rhythm-infused arcade racer. You play the lead in a blockbuster that smash-cuts across five movie genres on one continuous, procedurally generated 9,000 m track. Each genre has its own vehicle, BPM, aspect ratio and special move.

## How we worked

### Two lanes, one repo
- **Gameplay lane (Anthony):** all game logic in modular C++. That covers the flow pawn, game mode, the track generator, the HUD drawn in C++ (`UMegaJamCinemaHUD`) and the MetaSound bridge. Blueprints stay thin: tuning values and asset references only.
- **Art and audio lane (Hendrixsan):** Meshy-generated meshes, materials and textures, plus music and SFX wired through MetaSounds. Kept in `Content/MegaJam26/Art` and `/Audio`.
- **The rule that made it work:** `AGENTS.md` told every AI agent never to overwrite, regenerate or rename a shared `.uasset`/`.umap` unless that file is named in the prompt. Binary assets can't be merged, so the two lanes never collided. Changes moved between lanes as C++ hooks and written contracts, for example the #55 art pipeline contract: genre table, seat offsets, obstacle skins.
- **Issue-driven:** every task was a GitHub issue, every change a branch and PR named after it. The issue text was the spec the AI agents worked from.
<!-- TODO: a sentence or two on how the hand-offs felt in practice -->

### The AI toolchain
- **Anthony:** <!-- TODO: Anthony's tools, e.g. Gemini 4 Argon in the IDE, and how he used plan mode -->
- **Hendrixsan:** Claude Code in a PowerShell terminal and in Unreal's built-in terminal, plus Claude Desktop chat for <!-- TODO: what the chats were for -->.
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
<!-- TODO: anything that lands after 1:30 PM -->

## Case studies

### 1. From a text prompt to a mesh in the game: the Meshy vehicle pipeline
<!-- TODO: picture: before/after contact sheet (claude87/sheet_s1..s5.png) -->
- Every prompt was written to give game-ready props. Each one ended with "stylized low-poly, clean flat shading, bold simple shapes, game-ready, single object, centered, no background, no text".
- Vehicles had to be **riderless**, because the player is the "star" and the orb rides on top. Getting Meshy to leave off the rider took several rewrites ("no rider, no jockey, no human, no figure on top").
- Smart-topology meshes at a set triangle budget: 8k for vehicles, 3k for obstacles, 6k for props. Textures were downscaled from 2K to 1K on import by a script.
- The Hang Glider's wing kept coming out with holes. The fix was a two-step redo: text → 2D design image → image-to-3D.
<!-- TODO: a "what we learned" line -->

### 2. Letting the AI play-test the game
<!-- TODO: picture: one of the automated capture sheets (claude87/orb_review_povs.png or shots/arch_*.png) -->
- Claude drove the full Unreal editor from Python. It started PIE, began a run, turned on God Mode, skipped scenes with the same call as F7, then slowed time and took high-res screenshots at set distances along the track.
- This let art changes be checked in all five scenes and several camera views without anyone sitting at the keyboard. It's how placement bugs and material problems were found and fixed (#87, #108).
<!-- TODO: one concrete bug this caught -->

### 3. The movie magic orb (#108)
<!-- TODO: picture: orb on each vehicle (claude87/orb_review_seats.png) -->
- <!-- TODO: what the orb is, how its mesh and glow material were made, and how the seat offsets were fitted per vehicle using the automated captures -->

## What went wrong (and how we fixed it)
- **NaN cascade (Sat).** Zero-length vectors, zero-scale instance transforms and bad camera matrices set off engine ensures and render glitches. We fixed them in a run of PRs (#95–#103) and then made safe transforms the rule (#123/#124).
- **Beat sync drift.** The audio beat clock fell out of step with the MetaSound score after F7 warps and pitch changes. Fixed in #75 and #77.
- **Riderless vehicles.** Meshy kept adding riders, as described in case study 1.
<!-- TODO: anything else: build hangs (-NoUBA), RAM limits on a 16 GB PC, LFS quota -->

## By the numbers

<!-- STATS:BEGIN -->
_Generated 2026-11-15 13:44 Eastern Standard Time by `Scripts/devlog_stats.py` from `origin/main`. Jam window starts 2026-11-12; earlier activity is listed as pre-jam._

### AI usage

| Person | Tool | Sessions | Prompts | Model turns | Tool calls | Input tok | Output tok | Cache write | Cache read | Models |
|---|---|---:|---:|---:|---:|---:|---:|---:|---:|---|
| Hendrixsan | Claude Code (PowerShell) | 16 | 158 | 1,019 | 1,286 | 2,247 | 863,353 | 5,041,566 | 191,669,885 | claude-opus-5-5 (800), claude-fable-5-2 (219) |
| Hendrixsan | Claude Code (UE Terminal) | 2 | 9 | 29 | 27 | 62 | 21,538 | 57,755 | 2,091,207 | claude-opus-5-5 (29) |
| Anthony | Gemini 4 Argon (IDE integration) | TODO | TODO | - | - | TODO | TODO | - | - | TODO |

_Cache read is the context re-sent to the model on each turn and billed at a discount. It is reported separately so it doesn't inflate the other numbers. "Model turns" counts unique assistant messages._

#### Claude Code per day

| Day | Sessions | Prompts | Model turns | Output tok |
|---|---:|---:|---:|---:|
| Thu Nov 12 | 4 | 30 | 113 | 70,911 |
| Fri Nov 13 | 7 | 52 | 388 | 308,371 |
| Sat Nov 14 | 5 | 70 | 393 | 368,372 |
| Sun Nov 15 | 5 | 15 | 154 | 137,237 |

### Meshy (3D generation)

- Jam account: Meshy Pro (jam partner perk), 1,100 starting credits, 587 left on 2026-11-15, so 513 spent
- Tasks on jam account: 68: 66 text-to-3d (preview + refine passes, including retries), 1 text-to-image and 1 image-to-3d (the Hang Glider redo)
- Models in game: 31 (30 on the jam account, plus the Motorcycle made on a free account)

### Repository

- **156 commits** on main (Nov 12: 28, Nov 13: 49, Nov 14: 67, Nov 15: 12)
- **25** commits carry a Claude co-author line
- **57 pull requests** (57 merged): arglifed 48, ninjacom 9
- **71 issues** (68 closed): arglifed 53, ninjacom 18
- **13,032 lines of C++** in 15 files under `Source/`
- **219 assets** (`.uasset`) and 1 map under `Content/MegaJam26/`
- **7 scripts** in `Scripts/`
<!-- STATS:END -->

## Declarations

- **Prepared before the jam (Nov 12):** the commit "Pre-jam UE 5.8 C++ Audio-Kinetic Flow Template & MCP setup". That is the four baseline C++ classes (GameMode, PlayerController, FlowPawn, FlowSubsystem), the module setup and the MCP configuration. Everything else was made during the jam. <!-- TODO: list any prompts or scripts written before the jam -->
- **AI-generated assets:** all 31 meshes come from Meshy AI, from our own text prompts. 30 were made on Meshy Pro (the jam perk) and the Motorcycle on a free account (CC BY 4.0, credit Meshy).
- **Licensed audio:** all music and SFX come from WeLoveIndies under its Epic MegaJam project licence.
- Full credits are on the game page. <!-- TODO: paste the Docs/CREDITS.md summary for the itch post -->
