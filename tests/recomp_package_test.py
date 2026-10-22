"""Check that release packages exclude game files and private runtime data."""
import importlib.util
import hashlib
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
import zipfile


class ReleasePackageTest(unittest.TestCase):
    def test_packages_only_explicit_runtime_files(self):
        script = Path(__file__).resolve().parents[1] / 'tools/package_release.py'
        self.assertTrue(script.is_file(), 'The release packager is missing')
        spec = importlib.util.spec_from_file_location('package_release', script)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            (root / 'licenses').mkdir()
            (root / 'docs').mkdir()
            for name in ('LICENSE', 'README.md', 'UPSTREAM.md'):
                (root / name).write_text('Required release documentation.')
            (root / 'resources').mkdir()
            (root / 'resources/readme.txt').write_text('Player setup instructions.')
            (root / 'licenses/SDK-MIT.txt').write_text('Required license.')
            (root / 'docs/CONTROLLERS.md').write_text('Controller instructions.')
            (root / 'docs/images').mkdir()
            cover = root / 'docs/images/shaolin-monks-recomp.png'
            cover.write_bytes(b'project-logo-fixture')
            (root / 'docs/images/private.png').write_bytes(b'must not be distributed')
            exe = root / 'build/ShaolinMonksRecomp.exe'
            exe.parent.mkdir()
            exe.write_bytes(b'MZ-test-only-executable')
            for name in ('default.xbe', 'game.xiso.iso', 'private.log', 'save.dat', 'settings.toml'):
                (exe.parent / name).write_bytes(b'must not be distributed')
            output = root / 'dist'
            package = module.portable_package(root, exe, output, '0.1.0')
            with zipfile.ZipFile(package) as archive:
                names = set(archive.namelist())
                self.assertEqual(names, {'ShaolinMonksRecomp.exe', 'readme.txt',
                                         'licenses/LICENSE.txt', 'licenses/SDK-MIT.txt'})
                self.assertEqual(archive.read('ShaolinMonksRecomp.exe'), exe.read_bytes())
                self.assertEqual(archive.read('readme.txt'), (root / 'resources/readme.txt').read_bytes())
                self.assertEqual(archive.read('licenses/LICENSE.txt'), (root / 'LICENSE').read_bytes())
            self.assertTrue((output / 'checksums.txt').is_file())

    def test_command_creates_only_runtime_archive_and_checksum(self):
        script = Path(__file__).resolve().parents[1] / 'tools/package_release.py'
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder)
            exe = root / 'ShaolinMonksRecomp.exe'
            exe.write_bytes(b'MZ-test-only-executable')
            output = root / 'dist'
            command = [sys.executable, str(script), '--exe', str(exe),
                       '--output', str(output), '--version', '0.1.0']
            for _ in range(2):
                subprocess.run(command, check=True, capture_output=True)
                self.assertEqual({path.name for path in output.iterdir()},
                                 {'ShaolinMonksRecomp-0.1.0-Windows-x64.zip', 'checksums.txt'})
                package = output / 'ShaolinMonksRecomp-0.1.0-Windows-x64.zip'
                digest = hashlib.sha256(package.read_bytes()).hexdigest()
                self.assertEqual((output / 'checksums.txt').read_text(),
                                 f'{digest}  {package.name}\n')


if __name__ == '__main__':
    unittest.main()
