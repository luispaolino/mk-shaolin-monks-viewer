# Shaolin Monks Recomp Agent Guide

This file defines instructions for coding agents in this repository.
Read `README.md` for user features and `docs/BUILDING.md` for build instructions.

## Project scope

- Use **Shaolin Monks Recomp** as the project name and `ShaolinMonksRecomp.exe` as the application filename.
- The target is the supported USA/Europe Xbox release of **Mortal Kombat: Shaolin Monks**.
- The project uses XboxXBERecomp, C++20, Direct3D 12, SDL, and a native Win32 settings launcher.
- Keep code, identifiers, comments, messages, scripts, tests, and documentation in English.
- Preserve intentional localization and character-encoding tests.
- Write comments that explain constraints or non-obvious behavior. Avoid comments that repeat the code.
- Document implemented features. Do not describe full campaign compatibility as verified without gameplay evidence.
- The maintainer reports a completed campaign playthrough. Some graphical issues remain.
- Mention tools and dependencies in project documentation. Avoid comparisons or credits to other game recompilation projects.

## Git and collaboration

- Preserve the owner's staging area and unrelated changes.
- Use `git --no-optional-locks` for read-only status and diff commands.
- Do not stage, unstage, commit, tag, push, or publish without an explicit request for that operation.
- Do not reset, restore, clean, stash, switch branches, or change remotes without an explicit request.
- A request to fix, finish, build, or clean the project does not authorize changes to the Git index.
- Do not create remote issues, pull requests, or messages without an explicit request.
- Keep local investigation notes and generated reports outside maintained documentation.
- Report completed changes, checks, and remaining limitations accurately.

## Files and dependencies

| Path | Maintained content |
| --- | --- |
| `src/` | Host integration, launcher, input, audio, graphics, storage, and compatibility code |
| `config/` | Supported executable profile and ABI metadata |
| `resources/` | Application icon, manifest, and Windows dialog resources |
| `tests/` | Regression tests and manual diagnostic scripts |
| `tools/` | Build, SDK adaptation, and release scripts |
| `docs/` | Instructions and the approved README image |
| `licenses/` | Dependency licenses and notices |

- Keep the SDK at the revision recorded in `UPSTREAM.md` and the build configuration.
- Put permanent SDK adaptations in `tools/prepare_recomp_runtime.py` or maintained integration code.
- Do not change the SDK checkout or generated translation as the permanent fix.
- Treat `build/`, `work/`, `dist/`, and downloaded dependencies as local output.
- Keep machine-specific paths in ignored local wrappers. Quote paths in scripts.
- The ignored `build.bat` is a local convenience. Public build instructions use `tools/build_recomp.py`.

## Game files, user data, and licenses

- Users supply their own original game files. Do not change those files during import or testing.
- Keep disc images, XBE files, extracted assets, and generated translations outside public source and release packages.
- Keep saves, settings, logs, captures, crash dumps, analysis databases, and local toolchains outside public source.
- The approved documentation image is `docs/images/shaolin-monks-recomp.png`. Preserve it unless the owner requests a replacement.
- Preserve the project's GPL-2.0-or-later terms and all required third-party notices.
- The project license does not license the original game's code, assets, names, or artwork.
- Keep saves in the Windows Saved Games folder under `Shaolin Monks\hdd`.
- Keep settings, logs, caches, crash reports, and source selection under `%LOCALAPPDATA%\Shaolin Monks Recomp`.
- Preserve existing saves and settings during updates. Never delete user data as routine cleanup.
- Inspect the exact target before cleanup. Delete only artifacts within the authorized scope.

## Runtime behavior

- Preserve automatic input assignment. Do not restore manual keyboard-player or multiplayer-mode configuration variants.
- In solo play, the keyboard, mouse, and first controller share player 1.
- Ko-op assigns the keyboard and mouse to player 1 with one controller assigned to player 2.
- With two controllers at Ko-op entry, the controllers occupy both player slots.
- Preserve controller connection, disconnection, reconnection, and focus handling.
- Mouse input shares the keyboard's player slot. F10 and focus loss release pointer capture.
- Capture the pointer only while the interactive game window has focus. Keep it inside the selected monitor.
- Preserve visible title-bar controls in windowed mode.
- Show source selection before settings when the remembered game is unavailable. Preserve settings-only `--configure` behavior.
- Keep ISO and XISO reading in the runtime. Do not add extract-xiso or a separate installer dependency.
- Preserve original timing, camera restrictions, save behavior, and visual intent when changing host integration.
- A higher host FPS limit alone does not prove accurate gameplay timing at that rate.
- Support hardware claims with capability checks and reproducible evidence.

## Validation

- Run automated checks in the background with isolated configuration, saves, and logs.
- Use the existing private-desktop infrastructure for native dialog tests.
- Do not steal focus, move or confine the user's cursor, produce audio, or poll physical controllers during automated tests.
- Use synthetic fixtures for file operations. Real-disc regressions may read the external game with isolated writable data.
- Launch a visible game only when the owner requests it.
- Add a focused regression check when a behavior change needs one.
- For documentation, inspect links, filenames, commands, and consistency. Avoid unrelated rebuilds.
- Run the affected CTest checks after runtime changes. Run broader checks when integration risks justify them.
- Report automated checks separately from physical-device, visual, or full-campaign validation.

Run these commands from the repository root. Replace the example paths with local paths.

```powershell
python tools/build_recomp.py --xbe "C:/Games/ShaolinMonks/default.xbe" --sdk third_party/XboxXBERecomp --llvm "C:/Program Files/LLVM" --test
ctest --test-dir build/release --output-on-failure
python tests/recomp_package_test.py
python tools/package_release.py --exe build/release/ShaolinMonksRecomp.exe --version 0.1.0
```

Successful builds create the Windows ZIP and `checksums.txt` automatically. Configuration-only runs skip packaging.
GitHub supplies source archives for release tags.
The Windows package contains the executable, `readme.txt`, and `licenses/`, including the project license text.
Keep `README.md`, `UPSTREAM.md`, `AGENTS.md`, and `docs/` in the source repository, outside the Windows package.
Maintain the packaged player instructions in `resources/readme.txt`. Preserve the explicit package file list.
Do not publish packages as part of local validation.

## AI-assisted changes

AI assistance does not replace maintainer review or testing.
Check proposed changes against the source, supported game behavior, and relevant documentation.
Do not invent test results, hardware coverage, compatibility claims, or upstream attribution.
Keep the README's AI disclosure accurate as the workflow changes.
