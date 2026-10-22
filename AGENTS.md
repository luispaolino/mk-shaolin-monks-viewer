# Shaolin Monks Viewer agent guide

This repository contains the standalone asset viewer. Keep code and documentation
focused on inspecting original game files; do not add game-port promotion or
unrelated host/launcher/generated translation code.

- Use C++20, Win32, Direct3D 11 and the included pinned libraries.
- Preserve GPL-2.0-or-later terms and all required third-party notices.
- Preserve original game files and never include game images or extracted assets.
- Keep source, identifiers, scripts and documentation in English.
- Keep build products in ignored `build/` or `dist/` directories.
- Preserve intentional existing behavior and unrelated owner changes.
- Use `git --no-optional-locks` for read-only Git inspection.
- Do not stage, commit, tag, push, rewrite history, or publish without user authorization.
- Validate changes with the independent build and affected CTest checks.
- Automated tests must remain hidden/windowless, use synthetic fixtures and silent
  audio, and never poll controllers, move the system cursor, or steal focus.
- Keep the existing October release executable's provenance separate from new
  source builds. Do not claim binary/source reproducibility without evidence.

Read `docs/BUILDING.md`, `docs/VIEWER.md` and `UPSTREAM.md` for build, feature and
dependency details. Library attribution is factual provenance, not advertising.
