# -*- coding: utf-8 -*-
"""按窗口标题精确前置 Qt 主窗口。"""
import ctypes
from ctypes import wintypes

user32 = ctypes.windll.user32
HWND_TOPMOST = wintypes.HWND(-1)
HWND_NOTOPMOST = wintypes.HWND(-2)
target = user32.FindWindowW("Qt6120QWindowIcon", "微商系统 - 客户端")
print("main hwnd:", target)
if target:
    user32.ShowWindow(target, 9)
    user32.SetWindowPos(target, HWND_TOPMOST, 200, 80, 1240, 800, 0x0040)
    user32.SetWindowPos(target, HWND_NOTOPMOST, 200, 80, 1240, 800, 0x0040)
    user32.SetForegroundWindow(target)
    user32.BringWindowToTop(target)
    print("foreground:", user32.GetForegroundWindow() == target)
