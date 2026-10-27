# Build the standalone viewer

## Requirements

- Windows x64.
- Visual Studio C++ Build Tools or Visual Studio with the Desktop C++ workload,
  Windows SDK, and bundled CMake/Ninja tools.
- Python 3.

The required ImGui, miniz, and Xbox disc-reader source files are included under
`third_party/`. The build verifies their pinned content using
`third_party/manifest.json`. No game port, generated game code, original game
executable, external SDK checkout, LLVM installation, or network download is
required to compile the viewer.

From the repository root:

```powershell
python tools/build_viewer.py --test
```

The script discovers Visual Studio and configures the x64 developer environment.
The executable is `build/viewer/ShaolinMonksViewer.exe`. All build intermediates
stay under `build/`. `--jobs 2` limits compiler parallelism; `--build-dir` selects
a different output directory.

The 17 CTest checks use synthetic data, hidden or windowless WARP
rendering, queued test input, and silent audio mode. Their temporary fixtures are
isolated under the build directory. They do not launch a visible game or require
original game data. These checks do not establish physical-device behavior or
complete original-game compatibility.

For an already initialized x64 Visual Studio developer shell, the equivalent
configuration is:

```powershell
cmake -S . -B build/viewer -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build/viewer --parallel 4
ctest --test-dir build/viewer --output-on-failure
```

## Packages

```powershell
python tools/package_viewer.py --exe build/viewer/ShaolinMonksViewer.exe
```

This creates a local package under `dist/` from the specified executable,
player instructions, and license notices. Inspect it before distribution.
The latest checked-in build is under `release/v2026.10.27/`, with its exact
source commit, build-input inventory, test outcomes and SHA-256 checksums.
The separately retained `release/v1.0.0/` artifact contains the original
October 19 executable and retains its own provenance.

Users supply their own supported original game files when running the viewer.
Never include those files, extracted assets, saves, logs, or captures in a package.
