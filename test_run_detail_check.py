# -*- coding: utf-8 -*-
"""最小复现：单独验证 1002 商品详情响应码。"""
import socket, struct, hashlib, json

MAGIC, VERSION = 0x4D495F53, 1
HEADER = struct.Struct(">IIII")

def hp(u, p):
    return hashlib.sha256(("微商系统$" + u + "#" + p).encode("utf-8")).hexdigest()

def pkt(code, body):
    b = json.dumps(body).encode()
    return HEADER.pack(MAGIC, VERSION, code, len(b)) + b

s = socket.create_connection(("127.0.0.1", 8888), timeout=5)
s.sendall(pkt(1006, {"username": "admin", "password": hp("admin", "Admin#2026!")}))
hdr = s.recv(16)
_, _, rc, n = HEADER.unpack(hdr)
body = b""
while len(body) < n:
    body += s.recv(65536)
login = json.loads(body)
print("login resp", rc)
token = login["token"]

s.sendall(pkt(1002, {"product_id": 1, "token": token}))
chunks = b""
while len(chunks) < 16:
    chunks += s.recv(65536)
_, _, rc2, n2 = HEADER.unpack_from(chunks, 0)
rest = chunks[16:]
while len(rest) < n2:
    rest += s.recv(65536)
detail = json.loads(rest[:n2])
print("detail resp code:", rc2, "expected 2002")
print("detail keys:", list(detail.keys())[:8])
s.close()
