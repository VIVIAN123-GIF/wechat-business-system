# -*- coding: utf-8 -*-
"""枚举 qt_client 窗口位置，强制恢复并移动到屏幕内。"""
import ctypes
from ctypes import wintypes
import subprocess

user32 = ctypes.windll.user32
SW_RESTORE = 9
SW_SHOW = 5

# 先找 qt_client pid
ps = subprocess.run(["powershell", "-NoProfile", "-Command",
                     "(Get-Process -Name qt_client -ErrorAction SilentlyContinue).Id"],
                    capture_output=True, text=True)
pids = [int(x) for x in ps.stdout.split() if x.strip().isdigit()]
print("pids:", pids)

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
rect = wintypes.RECT()
for pid in pids:
    def cb(hwnd, _):
        wpid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid:
            user32.GetWindowRect(hwnd, ctypes.byref(rect))
            print(f"hwnd={hwnd} visible={user32.IsWindowVisible(hwnd)} "
                  f"minimized={user32.IsIconic(hwnd)} "
                  f"rect=({rect.left},{rect.top},{rect.right},{rect.bottom})")
            user32.ShowWindow(hwnd, SW_RESTORE)
            user32.MoveWindow(hwnd, 200, 120, 1200, 760, True)
            user32.SetForegroundWindow(hwnd)
        return True
    user32.EnumWindows(EnumWindowsProc(cb), 0)
