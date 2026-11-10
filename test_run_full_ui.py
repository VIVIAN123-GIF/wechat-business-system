# -*- coding: utf-8 -*-
"""
一体化 Qt 实测：鼠标点击 + SendInput 登录，登录后逐页截图。
全程单次进程，qt_client 作为子进程存活。
"""
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
print("qt pid:", qt.pid)

hwnd = None
for _ in range(30):
    hwnd = user32.FindWindowW("Qt6120QWindowIcon", "微商系统 - 客户端")
    if hwnd:
        break
    time.sleep(0.3)
print("hwnd:", hwnd)
assert hwnd
user32.ShowWindow(hwnd, 9)
user32.SetWindowPos(wintypes.HWND(-1), 180, 60, 1280, 820, 0x0040)
user32.SetWindowPos(wintypes.HWND(-2), 180, 60, 1280, 820, 0x0040)
time.sleep(0.8)

# ---- SendInput 键盘 ----
INPUT_KEYBOARD, KEYEVENTF_KEYUP = 1, 0x0002
ULONG_PTR = ctypes.POINTER(ctypes.c_ulong)


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD), ("wScan", wintypes.WORD),
                ("dwFlags", wintypes.DWORD), ("time", wintypes.DWORD),
                ("dwExtraInfo", ULONG_PTR)]


class INPUT(ctypes.Structure):
    class _I(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT)]
    _anonymous_ = ("i",)
    _fields_ = [("type", wintypes.DWORD), ("i", _I)]


def send_vk(vk, up=False):
    inp = INPUT(); inp.type = INPUT_KEYBOARD
    inp.ki.wVk = vk
    inp.ki.dwFlags = KEYEVENTF_KEYUP if up else 0
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(inp))


def type_text(text):
    for ch in text:
        code = user32.VkKeyScanW(ord(ch))
        vk, sh = code & 0xFF, (code >> 8) & 1
        if sh:
            send_vk(0x10); time.sleep(0.03)
        send_vk(vk); time.sleep(0.04); send_vk(vk, True); time.sleep(0.05)
        if sh:
            send_vk(0x10, True); time.sleep(0.03)


def shot(name):
    from PIL import ImageGrab
    img = ImageGrab.grab()
    p = os.path.join(SHOTS, name + ".png")
    img.save(p)
    print("saved", p)


def click_px(x, y):
    user32.SetCursorPos(int(x), int(y)); time.sleep(0.15)
    user32.mouse_event(0x0002, 0, 0, 0, 0); time.sleep(0.05)
    user32.mouse_event(0x0004, 0, 0, 0, 0); time.sleep(0.25)


# ---- 登录 ----
click_px(1048, 505)          # 密码框
time.sleep(0.2)
type_text("Admin#2026!")
time.sleep(0.2)
click_px(886, 543)           # 登录按钮
print("login clicked, waiting...")
time.sleep(3.5)
shot("01-after-login")

time.sleep(0.5)
qt.terminate()
try:
    qt.wait(timeout=5)
except subprocess.TimeoutExpired:
    qt.kill()
print("done")
