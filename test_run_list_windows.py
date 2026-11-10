# -*- coding: utf-8 -*-
"""枚举 qt_client 所有窗口，打印 hwnd/标题/类名/尺寸，识别真正的主窗口。"""
import ctypes
from ctypes import wintypes
import subprocess

user32 = ctypes.windll.user32
ps = subprocess.run(["powershell", "-NoProfile", "-Command",
                     "(Get-Process -Name qt_client -ErrorAction SilentlyContinue).Id"],
                    capture_output=True, text=True)
pids = [int(x) for x in ps.stdout.split() if x.strip().isdigit()]

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
r = wintypes.RECT()
for pid in pids:
    def cb(hwnd, _):
        wpid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid:
            user32.GetWindowRect(hwnd, ctypes.byref(r))
            n = user32.GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(n + 1)
            user32.GetWindowTextW(hwnd, buf, n + 1)
            cn = ctypes.create_unicode_buffer(256)
            user32.GetClassNameW(hwnd, cn, 256)
            print(f"hwnd={hwnd} visible={user32.IsWindowVisible(hwnd)} "
                  f"title='{buf.value}' class='{cn.value}' "
                  f"rect=({r.left},{r.top},{r.right},{r.bottom})")
        return True
    user32.EnumWindows(EnumWindowsProc(cb), 0)
