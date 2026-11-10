# -*- coding: utf-8 -*-
"""后台保活：启动 qt_client，直到出现 stop flag 才退出。"""
import os, time, subprocess

ROOT = r"D:\OneDrive\Desktop\object_course"
stop = os.path.join(ROOT, "test-run-qt-stop.flag")
if os.path.exists(stop):
    os.remove(stop)

env = dict(os.environ)
env["PATH"] = r"C:\Qt\6.12.0\mingw_64\bin;" + env["PATH"]
qt = subprocess.Popen([os.path.join(ROOT, "build-qt-real", "qt_client.exe")],
                      cwd=ROOT, env=env)
print("qt pid:", qt.pid, flush=True)
deadline = time.time() + 600
while time.time() < deadline:
    if qt.poll() is not None:
        print("qt exited by itself, code:", qt.returncode, flush=True)
        break
    if os.path.exists(stop):
        qt.terminate()
        try:
            qt.wait(timeout=5)
        except subprocess.TimeoutExpired:
            qt.kill()
        print("qt stopped by flag", flush=True)
        break
    time.sleep(1)
