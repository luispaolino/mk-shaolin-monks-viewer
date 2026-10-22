# Tools and dependencies

## XboxXBERecomp

The active port uses [XboxXBERecomp](https://github.com/TGP482/XboxXBERecomp), copyright 2026 TGP482, under the MIT license.
The supported SDK revision is `40e9c35451305f09ca9d86f677e4b9c2c37ff26b`.

The build script prepares adapted SDK files inside the build directory. It does not change the SDK checkout.
Local adaptations cover graphics, audio compatibility, movie playback, input, storage, diagnostics, and application startup.
The SDL report mapping in `src/controller.cpp` derives from this SDK.
See [licenses/XboxXBERecomp-MIT.txt](licenses/XboxXBERecomp-MIT.txt) for the complete permission notice.
See [licenses/THIRD_PARTY.txt](licenses/THIRD_PARTY.txt) for the SDK dependency notices.

The game executable includes SDL, Dear ImGui, toml++, and miniz.
The native settings launcher uses Windows system APIs and adds no third-party library.
The separate standalone viewer uses Win32, Direct3D 11, Dear ImGui, miniz,
and the pinned SDK's read-only disc reader. It does not link SDL or the game runtime.
The notices also cover the embedded ImGui font and stb code, plus SDL's HIDAPI and YUV conversion code.
The recompiler tools use Zydis and Zycore. Those libraries are not linked into the game executable.
LLVM and [Ghidra](https://github.com/NationalSecurityAgency/ghidra) are development tools; Ghidra was used to analyze the original Xbox XBE. Neither is included in the Windows release package.

## References

Public xemu, Cxbx-Reloaded, nxdk, and Microsoft documentation informed behavior and ABI checks.
Reference consultation does not imply that those projects endorse this port.
The current renderer and host use the SDK and the local adaptations described above.
This source tree excludes original game images, assets, generated translations, analysis databases, and proprietary SDK files.

## miniz

The ZIP importer uses [miniz 3.1.0](https://github.com/richgel999/miniz), under the MIT license.
The build pins revision `174573d60290f447c13a2b1b3405de2b96e27d6c` and checks the download checksum.
See [licenses/miniz-MIT.txt](licenses/miniz-MIT.txt) for the copyright and permission notice.
