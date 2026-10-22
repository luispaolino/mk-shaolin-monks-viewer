# Building

## Tools and inputs

- Windows x64.
- Visual Studio 2022 or later, with the C++ workload and Windows SDK.
- LLVM 21.1.8, with Clang and LLD.
- Python 3.11 or later and Git.
- The original USA/Europe `default.xbe`, outside this repository.

The build checks this executable's SHA-256:
`48240902bf4a04eb89376217c529531032d3370749bcce91a194878cc49db74a`.

The Visual Studio CMake and Ninja tools must be installed.
The build script loads the Visual Studio developer environment automatically.
The build downloads the dependencies specified by the pinned SDK.

## SDK

Run these commands from the repository root:

```powershell
git clone https://github.com/TGP482/XboxXBERecomp.git third_party/XboxXBERecomp
git -C third_party/XboxXBERecomp checkout 40e9c35451305f09ca9d86f677e4b9c2c37ff26b
```

## Compile and test

Replace the example paths with your local paths:

```powershell
python tools/build_recomp.py --xbe "C:/Games/ShaolinMonks/default.xbe" --sdk third_party/XboxXBERecomp --llvm "C:/Program Files/LLVM" --test
```

The executable is `build/release/ShaolinMonksRecomp.exe`.
After a successful build, the script creates the Windows ZIP and `checksums.txt` in `dist`.
With `--test`, packaging runs only after the tests pass. `--configure-only` does not create a package.
Build logs are in `build`.
Use `--jobs 2` to limit concurrent compiler processes.
Use `--disc "C:/Games/ShaolinMonks/game.xiso.iso"` to enable the isolated startup regression.
The real-disc tests run in a hidden window with separate runtime data and saves.
The manual menu and Ko-op scripts in `tests` accept `--help`.

You can delete the entire `build` directory after closing the game and compiler.
The next build recreates it and downloads dependencies again.
Keep the original game files, SDK checkout, and LLVM installation available outside `build`.
Default saves and AppData settings remain outside this directory.
Back up custom data first if you deliberately placed it inside `build`.

## Release package

For the independent character, animation, and stage viewer, see [VIEWER.md](VIEWER.md).
Its build and package commands are separate from the game release below.

The build script packages the application automatically. To regenerate the package without recompiling, run:

```powershell
python tools/package_release.py --exe build/release/ShaolinMonksRecomp.exe --version 0.1.0
```

The executable includes the first-run setup. The release package is a portable ZIP file.
The command writes the Windows ZIP and `checksums.txt` to `dist`.
The ZIP contains `ShaolinMonksRecomp.exe`, `readme.txt`, and `licenses/`.
The project license is included as `licenses/LICENSE.txt`.
`README.md`, `UPSTREAM.md`, `AGENTS.md`, and `docs/` remain in the source repository.
It includes files from an explicit list. It excludes adjacent game files, logs, saves, and machine settings.
Use the GitHub source download for the corresponding release tag.

Inspect the archive contents before publication.
Publish the corresponding project source and the required dependency notices with a binary release.
The generated translation remains a build output derived from the user's game executable.
The project license does not relicense the original game.

## Source layout

| Directory | Purpose |
| --- | --- |
| `src` | Host integration and compatibility code |
| `config` | Supported executable profile and ABI metadata |
| `resources` | Windows icon, manifest, and version information |
| `tests` | Automated and manual regression checks |
| `tools` | Build, SDK adaptation, and packaging scripts |
| `docs` | User and developer instructions |
| `licenses` | Dependency licenses and notices |

Keep build output, game files, and runtime data outside version control.
