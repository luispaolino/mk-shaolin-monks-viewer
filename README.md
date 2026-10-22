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

## Overview

Shaolin Monks Viewer reads your original game source to preview models, textures, skeletons, animation clips, stages, music, and concept art. It is an asset-inspection and export tool; it does not require a recompiled game executable or an emulator.

You supply your own USA/Europe Xbox ISO/XISO or complete extracted game source. The viewer reads those files without modifying them. Original game files and extracted assets are not included in this repository or download.

## Download

| Windows x64 package | Size |
| --- | --- |
| [ShaolinMonksViewer-v1.0.0-Windows-x64.zip](release/v1.0.0/ShaolinMonksViewer-v1.0.0-Windows-x64.zip) | 0.734 MiB download / 1.459 MiB expanded |

[SHA-256 checksums](release/v1.0.0/SHA256SUMS.txt) · [Release notes](release/v1.0.0/RELEASE_NOTES.md) · [Build provenance](release/v1.0.0/PROVENANCE.json)

This package contains the existing viewer build from **October 19, 2026 (UTC)**. `v1.0.0` is the repository package label. The executable has no embedded application version or source-commit identifier; the existing ZIP has not been rebuilt from this repository snapshot. No final release tag or GitHub Release has been published.

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

Only the SDK's `disc.cpp` and `disc.h` are needed for disc reading. The viewer does not link the game runtime, generated game translation, SDL, or Direct3D 12. The current build setup's shared dependency paths are being separated into an independent viewer configuration; build documentation will describe the verified standalone workflow.

## Licensing and Credits

Project source remains **GPL-2.0-or-later**; see [LICENSE](LICENSE). Required third-party copyright and license notices are retained in `licenses/` and with reused library source. These source licenses do not grant rights to the original game's code, assets, names, or artwork.
