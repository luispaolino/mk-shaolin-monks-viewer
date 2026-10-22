"""Create a Windows release package from an explicit file list."""
import argparse
import hashlib
from pathlib import Path
import re
import zipfile


NAME = 'ShaolinMonksRecomp'


def runtime_files(root):
    files = [(root / 'resources/readme.txt', 'readme.txt'),
             (root / 'LICENSE', 'licenses/LICENSE.txt')]
    files.extend((path, path.relative_to(root).as_posix())
                 for path in (root / 'licenses').rglob('*')
                 if path.is_file() and path.suffix in ('.txt', '.md'))
    return sorted(files, key=lambda item: item[1])


def checksum(output, artifact):
    record = f'{hashlib.sha256(artifact.read_bytes()).hexdigest()}  {artifact.name}\n'
    destination = output / 'checksums.txt'
    existing = destination.read_text().splitlines() if destination.exists() else []
    existing = [line for line in existing if not line.endswith('  ' + artifact.name)]
    destination.write_text(''.join(line + '\n' for line in existing) + record, encoding='utf-8')


def portable_package(root, exe, output, version):
    if not re.fullmatch(r'\d+\.\d+\.\d+(?:-[A-Za-z0-9.]+)?', version):
        raise ValueError('Use a version such as 0.1.0 or 0.1.0-preview.1')
    output.mkdir(parents=True, exist_ok=True)
    destination = output / f'{NAME}-{version}-Windows-x64.zip'
    with zipfile.ZipFile(destination, 'w', compression=zipfile.ZIP_DEFLATED) as archive:
        archive.write(exe, NAME + '.exe')
        for source, name in runtime_files(root):
            archive.write(source, name)
    checksum(output, destination)
    return destination


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    parser.add_argument('--version', default='0.1.0')
    parser.add_argument('--output', type=Path)
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    exe = args.exe.resolve(strict=True)
    output = args.output.resolve() if args.output else root / 'dist'
    print(portable_package(root, exe, output, args.version))


if __name__ == '__main__':
    main()
