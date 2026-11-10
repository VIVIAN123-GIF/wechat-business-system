# -*- coding: utf-8 -*-
"""30秒内从真实 server 录制各接口响应，存为 mock 数据。"""
import socket, struct, hashlib, json, time, os

MAGIC, VERSION = 0x4D495F53, 1
HEADER = struct.Struct(">IIII")
ROOT = r"D:\OneDrive\Desktop\object_course"

def hp(u, p):
    return hashlib.sha256(("微商系统$" + u + "#" + p).encode("utf-8")).hexdigest()

def pkt(code, body):
    b = json.dumps(body).encode()
    return HEADER.pack(MAGIC, VERSION, code, len(b)) + b

s = socket.create_connection(("127.0.0.1", 8888), timeout=5)
buf = b""

def recv():
    global buf
    while len(buf) < 16:
        d = s.recv(65536)
        if not d: raise ConnectionError("closed")
        buf += d
    _, _, rc, n = HEADER.unpack_from(buf, 0)
    while len(buf) < 16 + n:
        d = s.recv(65536)
        if not d: raise ConnectionError("closed")
        buf += d
    payload = buf[16:16+n]
    buf = buf[16+n:]
    return rc, payload.decode("utf-8")

s.sendall(pkt(1006, {"username": "admin", "password": hp("admin", "Admin#2026!")}))
rc, body = recv()
assert rc == 2006
login = json.loads(body)
token, uid = login["token"], login["user_id"]
recorded = {"2006": body}

def call(code, req):
    req = dict(req)
    req["token"] = token
    s.sendall(pkt(code, req))
    rc, body = recv()
    recorded[str(rc)] = body
    print(code, "->", rc, len(body), "bytes")

# 串行录制，避免乱序问题
call(1001, {})
call(1011, {"user_id": uid})
call(1040, {"user_id": uid})
call(1070, {"user_id": uid})
call(1120, {"user_id": uid})
call(1191, {"user_id": uid})
call(1202, {"user_id": uid})
call(1210, {"action": "list"})
call(1150, {"user_id": uid})
call(1093, {"user_id": uid})
call(1200, {"product_id": 1})
call(1002, {"product_id": 1})
call(1140, {"user_id": uid})
call(1160, {"user_id": uid})
call(1060, {"days": 7, "product_id": 0, "user_id": uid})
call(1020, {})

with open(os.path.join(ROOT, "test-run-mock-data.json"), "w", encoding="utf-8") as f:
    json.dump(recorded, f, ensure_ascii=False, indent=1)
print("saved", len(recorded), "responses")
s.close()
