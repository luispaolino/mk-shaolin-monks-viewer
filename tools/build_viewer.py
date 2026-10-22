"""Build the standalone viewer using the included, pinned library sources."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/viewer'))
    parser.add_argument('--jobs', type=int, default=4)
    parser.add_argument('--test', action='store_true')
    args = parser.parse_args()
    if os.name != 'nt':
        parser.error('The viewer build requires 64-bit Windows and Visual Studio C++ tools')
    if args.jobs < 1:
        parser.error('--jobs must be positive')
    root = Path(__file__).resolve().parents[1]
    for record in json.loads((root / 'third_party/manifest.json').read_text(encoding='utf-8'))['files']:
        path = root / record['path']
        if not path.is_file() or hashlib.sha256(path.read_bytes().replace(b'\r\n', b'\n')).hexdigest() != record['sha256_lf']:
            parser.error('Pinned dependency differs or is missing: ' + record['path'])
    build = args.build_dir.resolve()
    vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    hidden = subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS
    installation = subprocess.check_output([
        str(vswhere), '-latest', '-products', '*', '-requires',
        'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath',
    ], text=True, creationflags=hidden).strip()
    if not installation:
        parser.error('Visual Studio C++ tools and the Windows SDK are required')
    vs = Path(installation)
    cmake_root = vs / 'Common7/IDE/CommonExtensions/Microsoft/CMake'
    build.mkdir(parents=True, exist_ok=True)
    setup = build / 'viewer_environment.cmd'
    setup.write_text(f'@echo off\ncall "{vs / "Common7/Tools/VsDevCmd.bat"}" -arch=amd64 -host_arch=amd64 >nul\nif errorlevel 1 exit /b 1\nset\n', encoding='utf-8')
    result = subprocess.run(['cmd.exe', '/d', '/c', 'call', str(setup)], check=True, capture_output=True, text=True, creationflags=hidden)
    env = os.environ.copy()
    for line in result.stdout.splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            env[key] = value
    cmake = str(cmake_root / 'CMake/bin/cmake.exe')
    commands = [
        [cmake, '-S', str(root), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_MAKE_PROGRAM={cmake_root / "Ninja/ninja.exe"}'],
        [cmake, '--build', str(build), '--parallel', str(args.jobs)],
    ]
    for command in commands:
        subprocess.run(command, cwd=root, env=env, check=True, creationflags=hidden)
    if args.test:
        test_temp = build / 'test-temp'
        test_temp.mkdir(exist_ok=True)
        test_env = env.copy()
        test_env['TEMP'] = test_env['TMP'] = str(test_temp)
        subprocess.run([
            str(cmake_root / 'CMake/bin/ctest.exe'), '--test-dir', str(build),
            '--output-on-failure', '--parallel', '2',
        ], cwd=root, env=test_env, check=True, creationflags=hidden)
    print(f'Viewer: {build / "ShaolinMonksViewer.exe"}')


if __name__ == '__main__':
    main()
