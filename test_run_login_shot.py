# -*- coding: utf-8 -*-
"""启动 qt_client，前置并截登录页，用于确认控件坐标。"""
import os, time, subprocess, ctypes
from ctypes import wintypes

ROOT = r"D:\OneDrive\Desktop\object_course"
SHOTS = os.path.join(ROOT, "test-runs")
os.makedirs(SHOTS, exist_ok=True)
user32 = ctypes.windll.user32

env = dict(os.environ)
env["PATH"] = r"C:\Qt\6.12.0\mingw_64\bin;" + env["PATH"]
qt = subprocess.Popen([os.path.join(ROOT, "build-qt-real", "qt_client.exe")],
                      cwd=ROOT, env=env)
print("pid:", qt.pid)
hwnd = None
for _ in range(30):
    hwnd = user32.FindWindowW("Qt6120QWindowIcon", "微商系统 - 客户端")
    if hwnd:
        break
    time.sleep(0.3)
user32.ShowWindow(hwnd, 9)
user32.SetWindowPos(wintypes.HWND(-1), 180, 60, 1280, 820, 0x0040)
user32.SetWindowPos(wintypes.HWND(-2), 180, 60, 1280, 820, 0x0040)
time.sleep(1)
from PIL import ImageGrab
img = ImageGrab.grab()
p = os.path.join(SHOTS, "00-login-page.png")
img.save(p)
print("saved", p, img.size)
time.sleep(0.5)
qt.terminate()
try:
    qt.wait(timeout=5)
except subprocess.TimeoutExpired:
    qt.kill()
