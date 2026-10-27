# Shaolin Monks Viewer

<p align="center">
  <strong>Explore the characters, stages, animation, and artwork of Mortal Kombat: Shaolin Monks.</strong><br />
  A standalone Windows x64 asset viewer for the original USA/Europe Xbox release.
</p>

<p align="center">
  <a href="#download">Download</a> &middot;
  <a href="#features">Features</a> &middot;
  <a href="#quick-start">Quick Start</a> &middot;
  <a href="#source-and-dependencies">Source and Dependencies</a> &middot;
  <a href="docs/VIEWER.md">Viewer Manual</a>
</p>

## Viewer Demo

[![Watch the Shaolin Monks Viewer demo](https://img.youtube.com/vi/5hmiLelFoY4/hqdefault.jpg)](https://youtu.be/5hmiLelFoY4)

[Watch the viewer demo on YouTube](https://youtu.be/5hmiLelFoY4).

## Overview

Shaolin Monks Viewer reads your original game source to preview models, textures, skeletons, animation clips, stages, music, and concept art. It is an asset-inspection and export tool; it does not require a recompiled game executable or an emulator.

You supply your own USA/Europe Xbox ISO/XISO or complete extracted game source. The viewer reads those files without modifying them. Original game files and extracted assets are not included in this repository or download.

## Download

| Windows x64 package | Size |
| --- | --- |
| [ShaolinMonksViewer-v2026.10.27-Windows-x64.zip](release/v2026.10.27/ShaolinMonksViewer-v2026.10.27-Windows-x64.zip) | 0.991 MiB download / 1.872 MiB expanded |

[SHA-256 checksums](release/v2026.10.27/SHA256SUMS.txt) &middot; [Build notes](release/v2026.10.27/RELEASE_NOTES.md) &middot; [Source and build provenance](release/v2026.10.27/PROVENANCE.json)

Built **October 27, 2026 (UTC)** from the matching standalone source, with all
**17 CTest checks passing**. Includes embedded application artwork and **Help > About**.
The dated package label is separate from the executable's development build date.
The [original October 19 package](release/v1.0.0/RELEASE_NOTES.md) remains available.

## Features

- **Characters and animation.** Browse textured models, skeleton joints, named clips, looping playback, frame scrubbing, playback speed, and optional root motion.
- **Character Debug.** Inspect supported movement and attack-graph behavior in a separate preview scene. Recovered behavior and viewer approximations are documented in the manual.
- **Stages and environments.** Inspect campaign maps, versus arenas, props, textures, mesh animation, material channels, stored lights, fog, and particle previews.
- **Concept art.** Browse character art, environment art, storyboards, and design studies read from the original game.
- **Music and export.** Preview supported source music and export supported geometry and animation to FBX.

Preview rendering and Character Debug do not establish full original-game behavior. See the [viewer manual](docs/VIEWER.md) for format support, controls, export details, and known limitations.

## Quick Start

1. Download and extract the Windows x64 ZIP.
2. Run `ShaolinMonksViewer.exe`.
3. Choose **Open game source** and select a supported ISO/XISO, `GAMEDATA.WAD`, or `default.xbe`.
4. Browse the character, stage, artwork, or other asset categories.

An extracted source needs all eight archive banks and the original `default.xbe` together. The executable supplies metadata used by the viewer; its original game code is not executed. Windows x64 and Direct3D 11 are required; the viewer includes a software-rendering fallback.

## Source and Dependencies

The viewer is written in C++20 with Win32, Direct3D 11, and Windows audio APIs. It uses Dear ImGui for its interface, miniz for compressed data, and a small MIT-licensed Xbox disc reader from the pinned XboxXBERecomp SDK.

Only the SDK's `disc.cpp` and `disc.h` are needed for disc reading. The viewer does not link the game runtime, generated game translation, SDL, or Direct3D 12. The required library source subsets are included under `third_party/`, with their notices and pinned hashes. No game build or external SDK checkout is required.

## Build

With Windows x64, Python 3 and Visual Studio C++ tools installed:

```powershell
python tools/build_viewer.py --test
```

**Validated:** standalone Windows x64 Release build and all 17 CTest checks passed.

See [build instructions](docs/BUILDING.md) for prerequisites, output paths and
the synthetic/headless test workflow. The latest ZIP was built from the exact
source commit recorded in its provenance. Physical hardware and full original-game
behavior are outside these automated checks.

## Licensing and Credits

Project source remains **GPL-2.0-or-later**; see [LICENSE](LICENSE). Required third-party copyright and license notices are retained in `licenses/` and with reused library source. These source licenses do not grant rights to the original game's code, assets, names, or artwork.
