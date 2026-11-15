# Credits

## Art

All meshes and textures in `Content/MegaJam26/Art/` (`SM_*`, `MI_*`, `T_*`) were generated for this project with
[Meshy AI](https://www.meshy.ai) (text-to-3D, `meshy-t2.5` smart topology, textured with `meshy-6`) in November 2026.
Usage is governed by [Meshy's Terms of Service](https://www.meshy.ai/terms-of-use).
Prompts and per-asset notes: `RawAssets/Art/MANIFEST.md` and `RawAssets/Art/Vehicles/MANIFEST.md` (git-ignored, kept
locally by the art lane).

`M_MeshyPBR` was built for this project by `Scripts/import_issue5_assets.py`.

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
