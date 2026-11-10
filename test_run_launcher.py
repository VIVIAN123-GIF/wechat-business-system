# -*- coding: utf-8 -*-
"""启动 server.exe 并保持 stdin 管道不关闭，防止 getline 立即 EOF。"""
import subprocess
import sys
import time
import os

ROOT = r"D:\OneDrive\Desktop\object_course"
EXE = os.path.join(ROOT, "build-check", "server.exe")
OUT = os.path.join(ROOT, "test-run-server.out.log")
ERR = os.path.join(ROOT, "test-run-server.err.log")

out_f = open(OUT, "wb")
err_f = open(ERR, "wb")
p = subprocess.Popen([EXE], cwd=ROOT, stdin=subprocess.PIPE,
                     stdout=out_f, stderr=err_f)
print(f"server pid={p.pid}")
# 保持运行，stdin 管道不写不关；由外部 stop 文件通知退出
stop_flag = os.path.join(ROOT, "test-run-stop.flag")
deadline = time.time() + 300
while time.time() < deadline:
    rc = p.poll()
    if rc is not None:
        print(f"server EXITED early, returncode={rc}")
        sys.exit(2)
    if os.path.exists(stop_flag):
        p.terminate()
        try:
            p.wait(timeout=5)
        except subprocess.TimeoutExpired:
            p.kill()
        print("server terminated by stop flag")
        sys.exit(0)
    time.sleep(1)
p.terminate()
print("server terminated after deadline")
