# -*- coding: utf-8 -*-
"""用 SendInput 模拟真实硬件键盘输入，配合前台窗口完成登录。"""
import ctypes
from ctypes import wintypes
import time

user32 = ctypes.windll.user32

INPUT_KEYBOARD = 1
KEYEVENTF_KEYUP = 0x0002
VK_TAB = 0x09
VK_RETURN = 0x0D
VK_SHIFT = 0x10

ULONG_PTR = ctypes.POINTER(ctypes.c_ulong)


class KEYBDINPUT(ctypes.Structure):
    _fields_ = [("wVk", wintypes.WORD),
                ("wScan", wintypes.WORD),
                ("dwFlags", wintypes.DWORD),
                ("time", wintypes.DWORD),
                ("dwExtraInfo", ULONG_PTR)]


class INPUT(ctypes.Structure):
    class _I(ctypes.Union):
        _fields_ = [("ki", KEYBDINPUT)]
    _anonymous_ = ("i",)
    _fields_ = [("type", wintypes.DWORD), ("i", _I)]


def send_vk(vk, up=False):
    inp = INPUT()
    inp.type = INPUT_KEYBOARD
    inp.ki.wVk = vk
    inp.ki.dwFlags = KEYEVENTF_KEYUP if up else 0
    user32.SendInput(1, ctypes.byref(inp), ctypes.sizeof(inp))


def tap(vk):
    send_vk(vk, False)
    time.sleep(0.05)
    send_vk(vk, True)
    time.sleep(0.12)


def type_text(text):
    for ch in text:
        code = user32.VkKeyScanW(ord(ch))
        vk = code & 0xFF
        shift = (code >> 8) & 1
        if shift:
            send_vk(VK_SHIFT, False)
            time.sleep(0.03)
        send_vk(vk, False)
        time.sleep(0.04)
        send_vk(vk, True)
        time.sleep(0.05)
        if shift:
            send_vk(VK_SHIFT, True)
            time.sleep(0.03)


# 确认前台窗口是 Qt
fg = user32.GetForegroundWindow()
title_len = user32.GetWindowTextLengthW(fg)
buf = ctypes.create_unicode_buffer(title_len + 1)
user32.GetWindowTextW(fg, buf, title_len + 1)
print("foreground title:", buf.value)

# 邀请码有焦点：Tab 两次到密码框
tap(VK_TAB)
tap(VK_TAB)
time.sleep(0.2)
type_text("Admin#2026!")
time.sleep(0.2)
tap(VK_TAB)
time.sleep(0.2)
send_vk(VK_RETURN, False)
time.sleep(0.05)
send_vk(VK_RETURN, True)
print("login input sent")
time.sleep(2.5)
