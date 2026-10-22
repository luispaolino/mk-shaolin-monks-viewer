"""Exercise the native settings window on a private desktop."""
import argparse
import ctypes
from ctypes import wintypes
from pathlib import Path
import tempfile
import time
import tomllib
from recomp_error_dialog_test import PrivateProcess


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--exe', required=True, type=Path)
    args = parser.parse_args()
    user = ctypes.WinDLL('user32', use_last_error=True)
    kernel = ctypes.WinDLL('kernel32')
    callback_type = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
    user.CreateDesktopW.argtypes = [wintypes.LPCWSTR, wintypes.LPCWSTR, ctypes.c_void_p, wintypes.DWORD, wintypes.DWORD, ctypes.c_void_p]
    user.CreateDesktopW.restype = wintypes.HANDLE
    user.CloseDesktop.argtypes = [wintypes.HANDLE]
    user.GetThreadDesktop.argtypes = [wintypes.DWORD]
    user.GetThreadDesktop.restype = wintypes.HANDLE
    user.SetThreadDesktop.argtypes = [wintypes.HANDLE]
    user.EnumDesktopWindows.argtypes = [wintypes.HANDLE, callback_type, wintypes.LPARAM]
    user.GetWindowThreadProcessId.argtypes = [wintypes.HWND, ctypes.POINTER(wintypes.DWORD)]
    user.GetWindowTextW.argtypes = [wintypes.HWND, wintypes.LPWSTR, ctypes.c_int]
    user.GetDlgItem.argtypes = [wintypes.HWND, ctypes.c_int]
    user.GetDlgItem.restype = wintypes.HWND
    user.IsWindowVisible.argtypes = [wintypes.HWND]
    user.SendMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    user.PostMessageW.argtypes = [wintypes.HWND, wintypes.UINT, wintypes.WPARAM, wintypes.LPARAM]
    desktop_name = 'ShaolinLauncher' + str(time.time_ns())
    desktop = user.CreateDesktopW(desktop_name, None, None, 0, 0x01ff, None)
    assert desktop
    previous = user.GetThreadDesktop(kernel.GetCurrentThreadId())
    assert user.SetThreadDesktop(desktop)
    try:
        with tempfile.TemporaryDirectory(prefix='shaolin-launcher-') as directory:
            root = Path(directory)
            data = root / 'data'
            data.mkdir()
            settings = data / 'settings.toml'
            settings.write_text('fps_cap = 60\ncustom_key = "preserve"\n[controller]\ndeadzone = 0.23\n')
            before = settings.read_bytes()

            def wait_window(process, title):
                windows = []

                def observe(window, _):
                    pid = wintypes.DWORD()
                    user.GetWindowThreadProcessId(window, ctypes.byref(pid))
                    text = ctypes.create_unicode_buffer(256)
                    user.GetWindowTextW(window, text, len(text))
                    if pid.value == process.pid and text.value == title and user.IsWindowVisible(window):
                        windows.append(window)
                    return True

                observer = callback_type(observe)
                deadline = time.monotonic() + 15
                while process.poll() is None and not windows and time.monotonic() < deadline:
                    user.EnumDesktopWindows(desktop, observer, 0)
                    time.sleep(.05)
                assert windows, 'The expected dialog did not open: ' + title
                return windows[0]

            for save in (False, True):
                with (root / 'process.log').open('wb') as output:
                    process = PrivateProcess([str(args.exe.resolve()), '--configure', '--data-dir', str(data)], desktop_name, output, root)
                    windows = []

                    def observe(window, _):
                        pid = wintypes.DWORD()
                        user.GetWindowThreadProcessId(window, ctypes.byref(pid))
                        text = ctypes.create_unicode_buffer(256)
                        user.GetWindowTextW(window, text, len(text))
                        if pid.value == process.pid and text.value == 'Shaolin Monks Recomp - Settings' and user.IsWindowVisible(window):
                            windows.append(window)
                        return True

                    observer = callback_type(observe)
                    try:
                        deadline = time.monotonic() + 15
                        while process.poll() is None and not windows and time.monotonic() < deadline:
                            user.EnumDesktopWindows(desktop, observer, 0)
                            time.sleep(.05)
                        assert windows, 'The settings launcher did not open'
                        window = windows[0]
                        if save:
                            # Select French through the same language field the user edits.
                            user.SendMessageW(user.GetDlgItem(window, 2101), 0x014e, 1, 0)
                            sensitivity = ctypes.create_unicode_buffer('145')
                            user.SendMessageW(user.GetDlgItem(window, 2901), 0x000c, 0, ctypes.addressof(sensitivity))
                            user.SendMessageW(user.GetDlgItem(window, 2913), 0x014e, 7, 0)
                            user.PostMessageW(window, 0x0111, 2201, 0)
                        else:
                            user.PostMessageW(window, 0x0010, 0, 0)
                        assert process.wait(timeout=10) == 0
                    finally:
                        if process.poll() is None:
                            process.terminate()
                            process.wait(timeout=5)
                        process.close()
                if not save:
                    assert settings.read_bytes() == before, 'Closing the launcher must preserve settings'
                else:
                    config = tomllib.loads(settings.read_text())
                    assert config['language'] == 'fr'
                    assert config['custom_key'] == 'preserve'
                    assert abs(config['controller']['deadzone'] - .23) < .0001
                    assert config['mouse']['sensitivity'] == 145
                    assert config['mouse']['bindings']['side1'] == 'left_trigger'
            saved = settings.read_bytes()
            with (root / 'temporary-session.log').open('wb') as output:
                process = PrivateProcess([str(args.exe.resolve()), '--data-dir', str(data), '--disc', str(root / 'missing.iso')], desktop_name, output, root)
                try:
                    window = wait_window(process, 'Shaolin Monks Recomp - Settings')
                    user.SendMessageW(user.GetDlgItem(window, 2101), 0x014e, 2, 0)
                    user.PostMessageW(window, 0x0111, 2202, 0)
                    question = wait_window(process, 'Unsaved settings')
                    user.PostMessageW(question, 0x0111, 2, 0)
                    time.sleep(.15)
                    assert process.poll() is None and settings.read_bytes() == saved
                    user.PostMessageW(window, 0x0111, 2202, 0)
                    question = wait_window(process, 'Unsaved settings')
                    user.PostMessageW(question, 0x0111, 7, 0)
                    error = wait_window(process, 'Shaolin Monks Recomp')
                    assert settings.read_bytes() == saved, 'Play without saving must preserve the file'
                    time.sleep(.3)
                    user.PostMessageW(error, 0x0010, 0, 0)
                    assert process.wait(timeout=10) == 3
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=5)
                    process.close()
            assert 'Error dialog closed. Application data folder: ' + str(data) in (
                root / 'temporary-session.log').read_text(encoding='utf-8', errors='replace')
            assert settings.read_bytes() == saved
    finally:
        user.SetThreadDesktop(previous)
        user.CloseDesktop(desktop)
    print('PASS: launcher save, unsaved confirmation, cancel, temporary play, preserved settings, and post-error folder handling')


if __name__ == '__main__':
    main()
