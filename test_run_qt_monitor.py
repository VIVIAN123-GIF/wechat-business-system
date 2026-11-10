# -*- coding: utf-8 -*-
"""启动 qt_client 并监控其存活与窗口状态，记录与 server 崩溃的时间关系。"""
import subprocess
import time
import os
import ctypes
from ctypes import wintypes

ROOT = r"D:\OneDrive\Desktop\object_course"
EXE = os.path.join(ROOT, "build-qt-real", "qt_client.exe")

user32 = ctypes.windll.user32
EnumWindows = user32.EnumWindows
EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
GetWindowThreadProcessId = user32.GetWindowThreadProcessId
IsWindowVisible = user32.IsWindowVisible
GetWindowTextLengthW = user32.GetWindowTextLengthW
GetWindowTextW = user32.GetWindowTextW


def windows_of(pid):
    found = []

    def cb(hwnd, _):
        wpid = wintypes.DWORD()
        GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid:
            n = GetWindowTextLengthW(hwnd)
            buf = ctypes.create_unicode_buffer(n + 1)
            GetWindowTextW(hwnd, buf, n + 1)
            found.append((hwnd, bool(IsWindowVisible(hwnd)), buf.value))
        return True

    EnumWindows(EnumWindowsProc(cb), 0)
    return found


p = subprocess.Popen([EXE], cwd=ROOT)
print(f"qt_client pid={p.pid} started at {time.strftime('%H:%M:%S')}")
for i in range(20):
    time.sleep(2)
    rc = p.poll()
    wins = windows_of(p.pid) if rc is None else []
    stamp = time.strftime("%H:%M:%S")
    if rc is None:
        print(f"[{stamp}] alive, top-level windows={wins}")
    else:
        print(f"[{stamp}] EXITED code={rc}")
        break
if p.poll() is None:
    p.terminate()
