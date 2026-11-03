"""CI-only fake tray instance: Exit removes its window before helper cleanup ends."""
import ctypes as c
from ctypes import wintypes as w
import os
from pathlib import Path
import sys
import time


def run(marker):
    assert os.environ.get('GITHUB_ACTIONS') == 'true'
    user = c.WinDLL('user32', use_last_error=True)
    kernel = c.WinDLL('kernel32', use_last_error=True)
    callback = c.WINFUNCTYPE(c.c_ssize_t, w.HWND, w.UINT, w.WPARAM, w.LPARAM)

    class WindowClass(c.Structure):
        _fields_ = [('style', w.UINT), ('proc', callback), ('class_extra', c.c_int),
                    ('window_extra', c.c_int), ('instance', w.HINSTANCE),
                    ('icon', w.HICON), ('cursor', w.HANDLE), ('background', w.HBRUSH),
                    ('menu', w.LPCWSTR), ('name', w.LPCWSTR)]

    kernel.GetModuleHandleW.restype = w.HMODULE
    kernel.CreateMutexW.argtypes = [c.c_void_p, w.BOOL, w.LPCWSTR]
    kernel.CreateMutexW.restype = w.HANDLE
    kernel.CloseHandle.argtypes = [w.HANDLE]
    user.DefWindowProcW.argtypes = [w.HWND, w.UINT, w.WPARAM, w.LPARAM]
    user.DefWindowProcW.restype = c.c_ssize_t
    user.DestroyWindow.argtypes = [w.HWND]
    user.CreateWindowExW.argtypes = [w.DWORD, w.LPCWSTR, w.LPCWSTR, w.DWORD,
                                   c.c_int, c.c_int, c.c_int, c.c_int,
                                   w.HWND, w.HMENU, w.HINSTANCE, c.c_void_p]
    user.CreateWindowExW.restype = w.HWND
    user.RegisterClassW.argtypes = [c.POINTER(WindowClass)]
    stopped = False

    @callback
    def procedure(window, message, param, detail):
        nonlocal stopped
        if message == 0x111 and param == 3:
            stopped = True
            user.DestroyWindow(window)
            user.PostQuitMessage(0)
            return 0
        return user.DefWindowProcW(window, message, param, detail)

    handles = [kernel.CreateMutexW(None, False, name) for name in
               (r'Local\SideScreen.App', r'Local\SideScreen.DriverRuntime')]
    assert all(handles)
    instance = kernel.GetModuleHandleW(None)
    wc = WindowClass(proc=procedure, instance=instance, name='ExtraPane.Tray')
    assert user.RegisterClassW(c.byref(wc))
    window = user.CreateWindowExW(0, wc.name, '', 0, 0, 0, 0, 0, None, None, instance, None)
    assert window
    Path(marker + '.ready').touch()
    message = w.MSG()
    while user.GetMessageW(c.byref(message), None, 0, 0) > 0:
        user.TranslateMessage(c.byref(message))
        user.DispatchMessageW(c.byref(message))
    assert stopped, 'Installer did not send tray Exit'
    # Simulate an elevated helper that takes time to release driver state.
    time.sleep(1)
    Path(marker + '.stopped').touch()
    for handle in handles:
        kernel.CloseHandle(handle)


if __name__ == '__main__':
    run(sys.argv[1])
