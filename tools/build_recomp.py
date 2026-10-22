"""Configure/build the separate AMD64 host with an explicit local toolchain."""
import argparse
import os
from pathlib import Path
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--xbe', required=True, type=Path)
    parser.add_argument('--sdk', required=True, type=Path)
    parser.add_argument('--llvm', required=True, type=Path)
    parser.add_argument('--jobs', type=int, default=2, choices=range(1, 9))
    parser.add_argument('--configure-only', action='store_true')
    parser.add_argument('--build-dir', type=Path, help='Build output directory')
    parser.add_argument('--test', action='store_true', help='Run CTest after compilation')
    parser.add_argument('--disc', type=Path, help='Enable the local real-disc Reset regression')
    args = parser.parse_args()
    root = Path(__file__).resolve().parents[1]
    llvm, sdk, xbe = args.llvm.resolve(), args.sdk.resolve(), args.xbe.resolve()
    for file in (xbe, llvm / 'bin/clang++.exe', llvm / 'bin/lld-link.exe', sdk / 'cmake/XbeRecomp.cmake'):
        if not file.is_file():
            parser.error(f'Required file is missing: {file}')
    revision = subprocess.run(['git', '-C', str(sdk), 'rev-parse', 'HEAD'], capture_output=True, text=True, check=True).stdout.strip()
    if revision != '40e9c35451305f09ca9d86f677e4b9c2c37ff26b':
        parser.error('The SDK revision differs from the reviewed integration')
    vswhere = Path(os.environ.get('ProgramFiles(x86)', 'C:/Program Files (x86)')) / 'Microsoft Visual Studio/Installer/vswhere.exe'
    located = subprocess.run([str(vswhere), '-latest', '-products', '*', '-requires', 'Microsoft.VisualStudio.Component.VC.Tools.x86.x64', '-property', 'installationPath'], capture_output=True, text=True, check=True).stdout.strip()
    if not located:
        parser.error('Visual Studio with the C++ workload was not found')
    vs = Path(located)
    build = args.build_dir.resolve() if args.build_dir else root / 'build/release'
    build.parent.mkdir(parents=True, exist_ok=True)
    # Capture the developer environment internally. Do not print it: it may contain credentials.
    setup = build.parent / 'vs_env.cmd'
    setup.write_text(f'@echo off\ncall "{vs / "Common7/Tools/VsDevCmd.bat"}" -arch=amd64 -host_arch=amd64 >nul\nif errorlevel 1 exit /b 1\nset\n', encoding='utf-8')
    configured_env = subprocess.run(['cmd.exe', '/d', '/c', 'call', str(setup)], capture_output=True, text=True, check=True)
    env = os.environ.copy()
    for line in configured_env.stdout.splitlines():
        if '=' in line and not line.startswith('='):
            key, value = line.split('=', 1)
            env[key] = value
    cmake_root = vs / 'Common7/IDE/CommonExtensions/Microsoft/CMake'
    env['PATH'] = str(llvm / 'bin') + os.pathsep + str(cmake_root / 'Ninja') + os.pathsep + env.get('Path', env.get('PATH', ''))
    env.pop('Path', None)
    cmake = cmake_root / 'CMake/bin/cmake.exe'
    commands = [([str(cmake), '-S', str(root), '-B', str(build), '-G', 'Ninja',
                 f'-DCMAKE_C_COMPILER={llvm / "bin/clang.exe"}', f'-DCMAKE_CXX_COMPILER={llvm / "bin/clang++.exe"}',
                 f'-DCMAKE_LINKER={llvm / "bin/lld-link.exe"}', f'-DCMAKE_MAKE_PROGRAM={cmake_root / "Ninja/ninja.exe"}',
                 f'-DSHAOLIN_RECOMP_SDK={sdk}', f'-DSHAOLIN_GAME_XBE={xbe}',
                 '-DCMAKE_BUILD_TYPE=Release', '-DCMAKE_CXX_FLAGS_RELEASE=-O1', '-DCMAKE_C_FLAGS_RELEASE=-O1',
                 '-DSHAOLIN_GENERATED_OPT=-O1'], 'configure.log')]
    if not args.configure_only:
        commands.append(([str(cmake), '--build', str(build), '--parallel', str(args.jobs)], 'build.log'))
    if args.test and not args.configure_only:
        commands.append(([str(cmake_root / 'CMake/bin/ctest.exe'), '--test-dir', str(build), '--output-on-failure'], 'ctest.log'))
    if not args.configure_only:
        commands.append(([sys.executable, str(root / 'tools/package_release.py'),
                          '--exe', str(build / 'ShaolinMonksRecomp.exe')], 'package.log'))
    if args.disc:
        commands[0][0].append(f'-DSHAOLIN_TEST_DISC={args.disc.resolve()}')
    for command, name in commands:
        logfile = build.parent / name
        print(f'{name}: {logfile}', flush=True)
        with logfile.open('w', encoding='utf-8') as log:
            result = subprocess.run(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT,
                                    creationflags=subprocess.CREATE_NO_WINDOW | subprocess.BELOW_NORMAL_PRIORITY_CLASS)
        if result.returncode:
            raise SystemExit(result.returncode)
    print('Completed requested build steps.', flush=True)
    if not args.configure_only:
        print(f'Executable: {build / "ShaolinMonksRecomp.exe"}', flush=True)
        print(f'Release ZIP and checksums: {root / "dist"}', flush=True)


if __name__ == '__main__':
    main()
