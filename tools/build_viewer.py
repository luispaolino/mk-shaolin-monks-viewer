"""Build the independent viewer using the project's existing pinned dependencies."""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--build-dir', type=Path, default=Path('build/viewer'))
    parser.add_argument('--deps', type=Path, default=Path('build/release/_deps'))
    parser.add_argument('--sdk', type=Path, default=Path('third_party/XboxXBERecomp'))
    parser.add_argument('--test', action='store_true')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    build = args.build_dir.resolve()
    deps, sdk = args.deps.resolve(), args.sdk.resolve()
    revision = subprocess.check_output(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], text=True).strip()
    if revision != '40e9c35451305f09ca9d86f677e4b9c2c37ff26b':
        parser.error('The SDK revision differs from the pinned integration')
    vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    installation = subprocess.check_output([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], text=True).strip()
    if not installation:
        parser.error('Visual Studio C++ tools are required')
    vs = Path(installation)
    cmake_root = vs / 'Common7/IDE/CommonExtensions/Microsoft/CMake'
    build.mkdir(parents=True, exist_ok=True)
    setup = build / 'viewer_environment.cmd'
    setup.write_text(f'@echo off\ncall "{vs / "Common7/Tools/VsDevCmd.bat"}" -arch=amd64 -host_arch=amd64 >nul\nif errorlevel 1 exit /b 1\nset\n', encoding='utf-8')
    result = subprocess.run(['cmd.exe', '/d', '/c', 'call', str(setup)], check=True, capture_output=True, text=True)
    env = os.environ.copy()
    for line in result.stdout.splitlines():
        if '=' in line and not line.startswith('='):
            key, val = line.split('=', 1)
            env[key] = val
    cmake = str(cmake_root / 'CMake/bin/cmake.exe')
    for command in ([cmake, '-S', str(root / 'tools/viewer'), '-B', str(build), '-G', 'Ninja', '-DCMAKE_BUILD_TYPE=Release', f'-DCMAKE_MAKE_PROGRAM={cmake_root / "Ninja/ninja.exe"}', f'-DSHAOLIN_RECOMP_SDK={sdk}', f'-DSHAOLIN_VIEWER_DEPS={deps}'], [cmake, '--build', str(build), '--parallel', '4']):
        subprocess.run(command, cwd=root, env=env, check=True, creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
    if args.test:
        subprocess.run([str(cmake_root / 'CMake/bin/ctest.exe'), '--test-dir', str(build), '--output-on-failure'], cwd=root, env=env, check=True, creationflags=subprocess.CREATE_NO_WINDOW)
    print(f'Viewer: {build / "ShaolinMonksViewer.exe"}')


if __name__ == '__main__':
    main()
