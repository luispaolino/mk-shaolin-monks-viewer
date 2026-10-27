# Shaolin Monks Viewer v2026.10.27

Built on October 27, 2026 from standalone viewer source commit
`269c3d34001f95d43c52c0b73e32eb049a34d895`.

- Includes the latest local viewer artwork, ten Windows icon sizes and Help > About.
- Keeps the standalone viewer name and developer credit, with no game-port runtime dependency.
- Includes matching source, independent build tooling and the unchanged pinned library subsets.
- Passed all 17 synthetic CTest checks, including hidden About interaction and layout at 100%, 150% and 200% scale.
- ZIP integrity and every packaged file were verified against the explicit packaging inputs.

The Windows x64 archive is 1,039,051 bytes, expanding to 1,962,938 bytes.
It contains the viewer executable, player instructions and required license notices.
Users provide their own original game source. No disc image, extracted game asset,
save, configuration, cache or game-port executable is included.

The original `release/v1.0.0/` archive and provenance remain unchanged.
This dated package does not create a GitHub Release or tag. The embedded build
date is not an embedded Git identifier, and cross-build binary reproducibility
has not been established. Tests use synthetic data, hidden/windowless software
rendering and silent audio; physical hardware and real-game behavior were not
revalidated for this update.

See `PROVENANCE.json`, `SOURCE_SNAPSHOT.json` and `SHA256SUMS.txt` for the exact
source mapping, package inventory and checksums.
