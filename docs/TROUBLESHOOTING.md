# Troubleshooting

## Game selection

Start `ShaolinMonksRecomp.exe --setup` to select another image, ZIP, or extracted game folder.
The filename extension does not determine image compatibility.
A valid Xbox image must contain the supported original executable and the required game data and movies.
Setup rejects missing, empty, unreadable, or out-of-bounds required files and identifies the affected filename.
It also checks the main archive and movie headers.
These checks do not hash every game asset. They do not establish complete integrity of modified game data.
Renaming an unsupported image does not convert it.
The port reads ISO and XISO files directly. It does not extract them.
The port keeps temporary Xbox cache partitions under the application cache directory, separately from saves.

For a command-line check without a game window:

```powershell
ShaolinMonksRecomp.exe --validate-disc "C:/Games/ShaolinMonks/game.xiso.iso"
```

Use `--register-disc` instead to check the image and remember its path.
A failed check preserves the previous selection.
You can also select `default.xbe` inside the complete game folder.
The executable alone does not contain the game's other assets.
ZIP imports use `%LOCALAPPDATA%\Shaolin Monks Recomp\game`.
The importer rejects unsafe paths, duplicate names, encrypted archives, and corrupt file data.
An invalid or cancelled import preserves the previous selection.
Extract split archives before you select the game. The importer accepts ZIP files, including ZIP64, with stored or Deflate compression.

## Logs and crashes

The error dialog identifies the log for the current session.
Closing the dialog opens `%LOCALAPPDATA%\Shaolin Monks Recomp` in Explorer.
Logs are under `%LOCALAPPDATA%\Shaolin Monks Recomp\logs`.
Each session log starts with the Windows version, CPU, RAM, detected GPUs, display settings, and the port version.
Crash reports are under `%LOCALAPPDATA%\Shaolin Monks Recomp\crashes` when the crash handler can write one.
A forced process termination, power failure, or severe process corruption can prevent the dialog or crash report.

Include the session log and steps to reproduce the problem; the log already contains system details.
Review crash reports before sharing them. A report can contain process memory and local file paths.
Do not attach the original game image or extracted game files.

## Saves and configuration

The settings launcher opens before the game. Use `--configure` to open only the launcher.
Use `--no-launcher` to start with saved settings.
The launcher asks before saving changes when you select **Play** or close it.
Select **No** to leave the settings file unchanged.
The **Original game** checkbox applies its options once at startup. Disable it to use the game's saved options.

Saves remain in the Windows Saved Games folder under `Shaolin Monks\hdd`.
The original console save structure remains intact.
The port preserves backups before replacing existing save data.
It refuses a write when the required backup cannot be created.

The port copies a legacy `shaolin-recomp.toml` beside the executable into AppData when no destination settings exist.
It preserves the original file and existing destination settings.
Use `--paths` to display the resolved application data paths.
Use `--data-dir "C:/MyData"` to override application data for that run.
Use `--save-dir "C:/MySaves"` to override saves separately.
The `--portable-saves` option uses an `hdd` directory beside the executable.

Background diagnostics isolate application data in `runtime-data` beside the test executable.
They use separate saves and suppress interactive dialogs.
