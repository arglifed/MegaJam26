# Credits

## Art

### Generated with Meshy AI

**Credit:** 3D models generated with [Meshy AI](https://www.meshy.ai) by Chris Hendrickson (`ninjacom`) for MegaJam26.

**Licence.** [Meshy Terms of Service](https://www.meshy.ai/terms-of-use), version effective 2026-09-19, read 2026-11-15.
Section 3.2 sets the rights by plan:

- **Free plan:** Meshy owns the output and licenses it to the user under
  [Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/), on the
  condition that Meshy is credited. This covers the **Motorcycle**, made on a standard free account.
- **Paid plan:** the user owns the output. The other 30 Meshy models were made on the account created 2026-11-13
  through the 2026 Epic AI MegaJam partner perk, "one free month of Meshy Pro with 1,000 credits" (per the jam's
  itch.io page). Meshy Pro is a paid plan, so we own these 30.

We credit Meshy for every model anyway. That meets the CC BY 4.0 condition on the Motorcycle, and CC BY 4.0 allows
redistribution and modification, so committing all of these assets to the repo is allowed. Per section 2.4 of the terms we
have not removed any AI identifiers Meshy may embed in the outputs.

**How they were made.** All 31 were generated 2026-11-13 and 2026-11-14 from our own text prompts. No third-party
images, photos or brand references were used as input, and prompts avoided real product or brand names (the Spy Car
shows no badges). Meshy delivered 2K PBR textures (base colour, normal, metallic, roughness), which
`Scripts/downscale_art_textures.py` reduced to 1K on import.

| Route | Assets |
|---|---|
| Text prompt to a generated 2D image, then image-to-3D, then remeshed to about 8,000 triangles | `SM_Vehicle_Motorcycle` |
| Text-to-image (Meshy, `nano-banana`) design, then image-to-3D (`meshy-t2.5` smart topology, about 8,000 triangles, `meshy-7` PBR textures) | `SM_Vehicle_HangGlider` (regenerated 2026-11-14, #87) |
| Text-to-3D, `meshy-t2.5` smart topology at 8,000 triangles, textured with `meshy-6` | `SM_Vehicle_Horse`, `SM_Vehicle_SpyCar`, `SM_Vehicle_Snowboard` |
| Text-to-3D, `meshy-t2.5` smart topology at 3,000 triangles, textured with `meshy-6` | 7 obstacles (below) |
| Text-to-3D, `meshy-t2.5` smart topology at 6,000 triangles, textured with `meshy-6` | 5 props and pickup, 3 set pieces, 11 dressing (below) |

**Generated assets (31).** Each `SM_*` below has a matching `MI_*` material instance and `T_*_{BaseColor,Normal,Metallic,Roughness}` textures in `Content/MegaJam26/Art/` (three also have an `_Emissive` map).

- **Vehicles (5):** `SM_Vehicle_Motorcycle`, `SM_Vehicle_Horse`, `SM_Vehicle_SpyCar`, `SM_Vehicle_Snowboard`, `SM_Vehicle_HangGlider`
- **Obstacles (7):** `SM_Obstacle_Action_CyberBarrier`, `SM_Obstacle_Western_TimberBarricade`, `SM_Obstacle_Noir_Roadblock`,
  `SM_Obstacle_Chase_Snowdrift`, `SM_Obstacle_Chase_IceBoulders`, `SM_Obstacle_Final_Monolith`, `SM_Obstacle_Final_LaserGate`
- **Props and pickup (5):** `SM_Prop_Western_GraveCross`, `SM_Prop_Western_Tombstone`, `SM_Prop_Noir_BlackSedan`,
  `SM_Prop_CameraCrane`, `SM_Pickup_FilmFrame`
- **Set pieces (3):** `SM_SetPiece_WrapArch`, `SM_SetPiece_FilmReel`, `SM_SetPiece_ApertureRing`
- **Dressing (11):** `SM_Dressing_LightingTruss`, `SM_Dressing_BackdropFlat`, `SM_Dressing_StudioSpotlight`,
  `SM_Dressing_BoomMic`, `SM_Dressing_Western_Cactus`, `SM_Dressing_Western_Tumbleweed`, `SM_Dressing_Noir_StreetLamp`,
  `SM_Dressing_Noir_Skyscraper`, `SM_Dressing_Chase_PineTree`, `SM_Dressing_Chase_SnowRock`,
  `SM_Dressing_Final_FloatingCrystal`

**Jam rules.** The 2026 Epic AI MegaJam allows any AI tools, including asset generation, and needs no separate AI
disclosure. The submission's optional "Tools you used" field lists Meshy. No other 3D or texture generator (Tripo,
Hyper3D Rodin, PBRgen) was used for shipped art.

### Made for this project (not Meshy)

| Asset | How it was made |
|---|---|
| `SM_MovieMagicOrb` (`Art/`), `M_MovieMagicOrb` (`VFX/`) | Procedural mesh and material built by `Scripts/build_issue108_orb.py` (#108) |
| `SM_FloorTile`, `SM_WallSegment` (+ `MI_FloorTile`, `MI_WallSegment`) | Copies of the Unreal Engine cube, using our world-aligned material `M_TrackSurface` |
| `M_TrackSurface` | Built for this project |
| `M_MeshyPBR` and every `MI_*` material instance | Built by `Scripts/import_issue5_assets.py` |

Unreal Engine content is used under the [Unreal Engine EULA](https://www.unrealengine.com/eula).

Per-asset notes (triangle counts, sizes, credits spent, Meshy task IDs) are kept locally by the art lane in the
git-ignored `RawAssets/Art/MANIFEST.md` and `RawAssets/Art/Vehicles/MANIFEST.md`. This file is the record of source and
licence and does not depend on them.

## Audio

**Credit:** Music and sound effects by [WeLoveIndies](https://www.weloveindies.com).

**Licence.** WeLoveIndies' project-based licence for the 2026 Epic AI MegaJam
([weloveindies.com/en/epicmegajam](https://www.weloveindies.com/en/epicmegajam)), read 2026-11-15, under their
[Terms and Conditions of Use](https://www.weloveindies.com/en/agb) (dated 10.08.2026). In short:

- The assets may be used in this jam project, non-commercially, and stay licensed to MegaJam26 after the jam ends.
- They may not be reused in any other project. If the game ever earns money, it needs a WeLoveIndies commercial licence first.
- The terms' Free & Demo tier allows at most 10 music tracks and 100 sounds per project. We use 5 and 5.
- The terms forbid redistributing the audio itself (sharing, re-selling, resampling). The imported `SW_*.uasset` files
  are committed to this repo, which is private. **Do not make the repo public with them in it** without removing them or
  asking WeLoveIndies first.
- No composer or artist names are listed for these tracks on WeLoveIndies, so the credit goes to the library.
- The jam's itch.io page lists WeLoveIndies as a partner perk ("sound and music for non-commercial use") and asks that
  assets you didn't make be listed in the submission. The same list goes on the itch.io page.
- The T&C's Free & Demo tier asks releases to include the
  [WeLoveIndies intro](https://www.weloveindies.com/Trailer/WLI_Intro.mp4). The jam's MegaJam licence page doesn't
  mention it. The team decided (2026-11-15) to credit WeLoveIndies in text here and on the submission page instead of
  showing the intro video.

| Asset in `Content/MegaJam26/Audio/` | Original WeLoveIndies filename | Used for |
|---|---|---|
| `SW_Music_TooClever` | `kf015215-too-clever.wav` | Action scene music |
| `SW_Music_Brixxx` | `kf020908-brixxx.wav` | Western scene music |
| `SW_Music_LucidDreaming` | `kf027512-lucid-dreaming.wav` | Noir scene music |
| `SW_Music_Outbreak` | `kf027507-outbreak.wav` | Chase scene music |
| `SW_Music_TheSnatcher` | `kf012802-the-snatcher.wav` | Final Cut scene music |
| `SW_SFX_ClapperboardHit` | `object-wood-clapperboard-hit-reverb-long-tail-01-01.wav` | Run start, Director's Cut exit |
| `SW_SFX_FuturisticSell` | `ui-game-futuristic-sell-item-01.wav` | Film Frame pickup, on-beat cut |
| `SW_SFX_PositiveRise` | `ui-game-positive-activate-pitch-rise-high-05.wav` | Dolly Boost, Beat-Splice, victory |
| `SW_SFX_VocalCut` | `human-vocal-cut-03.wav` | Crash |
| `SW_SFX_CinematicWhoosh` | `cinematic-whoosh-low-slow-02.wav` | Director's Cut enter |

The music stems were converted from 24-bit to 16-bit (44.1 kHz stereo) before import. `MS_DirectorScore` (the MetaSound
that plays them) was built for this project. The audio lane also keeps a local copy of the filename mapping in the
git-ignored `RawAssets/Audio/SOURCE_IDS.txt`; this table is the record.
