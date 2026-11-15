# Credits

## Art

### Generated with Meshy AI

**Credit:** 3D models generated with [Meshy AI](https://www.meshy.ai) by Chris Hendrickson (`ninjacom`) for MegaJam26.

**Licence.** [Meshy Terms of Service](https://www.meshy.ai/terms-of-use), version effective 2026-09-19, read 2026-11-15.
Section 3.2 sets the rights by plan:

- **Free plan:** Meshy owns the output and licenses it to the user under
  [Creative Commons Attribution 4.0 International (CC BY 4.0)](https://creativecommons.org/licenses/by/4.0/), on the
  condition that Meshy is credited. This covers the **Motorcycle**, made on a standard free account.
- **Paid plan:** the user owns the output. The other 30 Meshy models were made on the Meshy account provided for the
  2026 Epic AI MegaJam (1,100 monthly credits), created 2026-11-13. The terms don't say whether a jam account counts
  as a paid plan. If it doesn't, the free-plan terms above apply to these models too.

We credit Meshy for every model, which meets the CC BY 4.0 condition whichever plan applies. CC BY 4.0 allows
redistribution and modification, so committing these assets to the repo is allowed. Per section 2.4 of the terms we
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

Original filenames and sources for the music and SFX are listed in `RawAssets/Audio/SOURCE_IDS.txt`.
