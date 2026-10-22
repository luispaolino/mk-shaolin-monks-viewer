# Library provenance

The viewer uses these library sources, included under `third_party/`:

| Library | Version / revision | Used content |
| --- | --- | --- |
| Dear ImGui | 1.91.0, `8199457a7d9e453f8d3d9cadc14683fb54a858b5` | Core, Win32 and Direct3D 11 backends, required headers |
| miniz | 3.1.0; declared archive revision `174573d60290f447c13a2b1b3405de2b96e27d6c` | Compression/ZIP library source and CMake inputs |
| XboxXBERecomp SDK disc reader | `40e9c35451305f09ca9d86f677e4b9c2c37ff26b` | Only `runtime/kernel/disc.cpp` and `disc.h` |

The two MIT-licensed disc-reader files total 8,655 original bytes. They read Xbox
ISO/XISO directory and file data; they do not recompile or execute game code.
Their includes are standard C++ and OS headers. No other SDK source or checkout
is needed by this standalone build.

The miniz source was supplied by the existing archive checkout, which has no
independent `.git` directory. Its declared original archive SHA-256 is
`dc188cb49e913f14b4583f4a8aae830035e71627570a036ec573a21f5678d65a`.
Do not mistake a surrounding project's Git HEAD for the miniz revision.

`third_party/manifest.json` records the included source subset and content hashes,
normalizing CRLF to LF for Git portability. The build checks that manifest before
compilation. Source subsets retain their copyright and license notices.

The viewer uses Windows APIs, Direct3D 11 and XAudio2. It does not link a game
host, generated game translation, SDL, or Direct3D 12. Users supply their own
supported game files at runtime; original game code is not executed by the viewer.
