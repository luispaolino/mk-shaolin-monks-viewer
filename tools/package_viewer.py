"""Package only the standalone viewer and its required notices."""
import argparse
import hashlib
from pathlib import Path
import zipfile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--exe', type=Path, default=Path('build/viewer/ShaolinMonksViewer.exe'))
    parser.add_argument('--output-dir', type=Path, default=Path('dist'))
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    files = {
        'ShaolinMonksViewer.exe': args.exe.resolve(),
        'readme.txt': root / 'resources/viewer-readme.txt',
        'licenses/LICENSE.txt': root / 'LICENSE',
        'licenses/XboxXBERecomp-MIT.txt': root / 'licenses/XboxXBERecomp-MIT.txt',
        'licenses/ImGui-components.txt': root / 'licenses/ImGui-components.txt',
        'licenses/THIRD_PARTY.txt': root / 'licenses/THIRD_PARTY.txt',
        'licenses/miniz-MIT.txt': root / 'licenses/miniz-MIT.txt',
    }
    for name, path in files.items():
        if not path.is_file():
            parser.error(f'Missing package input: {name}')
    destination = args.output_dir.resolve()
    destination.mkdir(parents=True, exist_ok=True)
    archive = destination / 'ShaolinMonksViewer-Windows-x64.zip'
    with zipfile.ZipFile(archive, 'w', compression=zipfile.ZIP_DEFLATED) as package:
        for name, path in files.items():
            package.write(path, name)
    digest = hashlib.sha256(archive.read_bytes()).hexdigest()
    (destination / 'ShaolinMonksViewer-checksums.txt').write_text(
        f'{digest}  {archive.name}\n', encoding='ascii')
    print(archive)


if __name__ == '__main__':
    main()
