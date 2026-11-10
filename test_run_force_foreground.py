# -*- coding: utf-8 -*-
"""用 AttachThreadInput + TOPMOST 强制把 qt_client 主窗口提到最前。"""
import ctypes
from ctypes import wintypes
import subprocess

user32 = ctypes.windll.user32
kernel32 = ctypes.windll.kernel32
HWND_TOPMOST = wintypes.HWND(-1)
HWND_NOTOPMOST = wintypes.HWND(-2)
SWP_SHOWWINDOW = 0x0040

ps = subprocess.run(["powershell", "-NoProfile", "-Command",
                     "(Get-Process -Name qt_client -ErrorAction SilentlyContinue).Id"],
                    capture_output=True, text=True)
pids = [int(x) for x in ps.stdout.split() if x.strip().isdigit()]

EnumWindowsProc = ctypes.WINFUNCTYPE(wintypes.BOOL, wintypes.HWND, wintypes.LPARAM)
target = None
r = wintypes.RECT()
for pid in pids:
    def find(hwnd, _):
        global target
        wpid = wintypes.DWORD()
        user32.GetWindowThreadProcessId(hwnd, ctypes.byref(wpid))
        if wpid.value == pid and user32.IsWindowVisible(hwnd):
            user32.GetWindowRect(hwnd, ctypes.byref(r))
            area = (r.right - r.left) * (r.bottom - r.top)
            if area > 10000:
                target = hwnd
        return True
    user32.EnumWindows(EnumWindowsProc(find), 0)

print("target hwnd:", target)
if target:
    user32.ShowWindow(target, 9)
    # 置顶再取消置顶，保持前台
    user32.SetWindowPos(target, HWND_TOPMOST, 200, 100, 1200, 760,
                        SWP_SHOWWINDOW)
    user32.SetWindowPos(target, HWND_NOTOPMOST, 200, 100, 1200, 760,
                        SWP_SHOWWINDOW)
    fg = user32.GetForegroundWindow()
    cur_tid = kernel32.GetCurrentThreadId()
    fg_tid = user32.GetWindowThreadProcessId(fg, None)
    tgt_tid = user32.GetWindowThreadProcessId(target, None)
    user32.AttachThreadInput(cur_tid, fg_tid, True)
    user32.AttachThreadInput(cur_tid, tgt_tid, True)
    user32.BringWindowToTop(target)
    user32.SetForegroundWindow(target)
    user32.AttachThreadInput(cur_tid, fg_tid, False)
    user32.AttachThreadInput(cur_tid, tgt_tid, False)
    print("foreground now:", user32.GetForegroundWindow() == target)
