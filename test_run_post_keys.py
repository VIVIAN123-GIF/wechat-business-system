# -*- coding: utf-8 -*-
"""
通过 PostMessage 向 Qt 主窗口投递键盘消息，
不依赖前台焦点，完成输入密码并登录。
"""
import ctypes
from ctypes import wintypes
import time

user32 = ctypes.windll.user32
WM_KEYDOWN = 0x0100
WM_KEYUP = 0x0101
WM_CHAR = 0x0102
VK_TAB = 0x09
VK_RETURN = 0x0D

hwnd = user32.FindWindowW("Qt6120QWindowIcon", "微商系统 - 客户端")
print("hwnd:", hwod if False else hwnd)


def post(msg, wparam, lparam=0x00010001):
    user32.PostMessageW(hwnd, msg, wparam, lparam)


def tap_key(vk):
    post(WM_KEYDOWN, vk, 0x00010001)
    post(WM_KEYUP, vk, 0xC0000001)
    time.sleep(0.15)


# 焦点起始在邀请码：Tab 两次到密码框
tap_key(VK_TAB)
tap_key(VK_TAB)
time.sleep(0.3)

# 逐字符发送密码（同时发 KEYDOWN + CHAR + KEYUP）
for ch in "Admin#2026!":
    vk = user32.VkKeyScanW(ord(ch))
    post(WM_KEYDOWN, vk & 0xFF, 0x00010001)
    post(WM_CHAR, ord(ch), 0x00010001)
    post(WM_KEYUP, vk & 0xFF, 0xC0000001)
    time.sleep(0.08)

time.sleep(0.3)
# Tab 到登录按钮并回车
tap_key(VK_TAB)
time.sleep(0.2)
post(WM_KEYDOWN, VK_RETURN, 0x001C0001)
post(WM_KEYUP, VK_RETURN, 0xC01C0001)
print("keys posted, waiting for login response")
time.sleep(2.5)
