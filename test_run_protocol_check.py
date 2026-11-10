# -*- coding: utf-8 -*-
"""
协议级黑盒测试：直接连接 8888 端口，验证服务端各接口响应。
重点验证：同一连接背靠背连发多个请求时，服务端是否全部正常响应。
"""
import socket
import struct
import hashlib
import json
import time

MAGIC = 0x4D495F53
VERSION = 0x00000001
HEADER = struct.Struct(">IIII")


def hash_password(username: str, password: str) -> str:
    raw = "微商系统$" + username + "#" + password
    return hashlib.sha256(raw.encode("utf-8")).hexdigest()


def build_packet(code: int, body: dict) -> bytes:
    payload = json.dumps(body, ensure_ascii=False).encode("utf-8")
    return HEADER.pack(MAGIC, VERSION, code, len(payload)) + payload


class ProtocolClient:
    def __init__(self, host="127.0.0.1", port=8888):
        self.sock = socket.create_connection((host, port), timeout=5)
        self.buf = b""

    def send(self, code: int, body: dict):
        self.sock.sendall(build_packet(code, body))

    def recv_one(self):
        while len(self.buf) < 16:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("server closed")
            self.buf += chunk
        _, _, resp_code, length = HEADER.unpack_from(self.buf, 0)
        while len(self.buf) < 16 + length:
            chunk = self.sock.recv(65536)
            if not chunk:
                raise ConnectionError("server closed")
            self.buf += chunk
        payload = self.buf[16:16 + length]
        self.buf = self.buf[16 + length:]
        return resp_code, json.loads(payload.decode("utf-8"))

    def close(self):
        self.sock.close()


def timed(label, code, body, pc: ProtocolClient):
    t0 = time.perf_counter()
    pc.send(code, body)
    resp_code, resp = pc.recv_one()
    ms = (time.perf_counter() - t0) * 1000
    summary = {}
    if isinstance(resp, dict):
        for key in ("products", "orders", "items", "addresses", "returns",
                    "tickets", "groups", "skus", "notifications", "promotions"):
            if key in resp and isinstance(resp[key], list):
                summary[key] = len(resp[key])
    print(f"[{label}] req={code} resp={resp_code} {ms:7.2f}ms {summary}")
    return resp_code, resp


def main():
    pc = ProtocolClient()
    print("== 1. 单请求接口逐个测试 ==")
    rcode, login = timed("login", 1006,
                         {"username": "admin",
                          "password": hash_password("admin", "Admin#2026!")}, pc)
    assert rcode == 2006, "login failed"
    token = login.get("token", "")
    uid = login.get("user_id", 0)
    print(f"    user_id={uid} level={login.get('level')} role={login.get('role')} token_len={len(token)}")

    def auth(body):
        body = dict(body)
        body["token"] = token
        return body

    timed("product list", 1001, auth({}), pc)
    timed("cart list", 1011, auth({"user_id": uid}), pc)
    timed("order query", 1040, auth({"user_id": uid}), pc)
    timed("return list", 1070, auth({"user_id": uid}), pc)
    timed("address list", 1120, auth({"user_id": uid}), pc)
    timed("admin products", 1191, auth({"user_id": uid}), pc)
    timed("service tickets", 1202, auth({"user_id": uid}), pc)
    timed("promo admin list", 1210, auth({"action": "list"}), pc)
    timed("notifications", 1150, auth({"user_id": uid}), pc)
    timed("groups", 1093, auth({"user_id": uid}), pc)

    print()
    print("== 2. 背靠背连发3个请求（模拟 Qt 页面 refresh 连发） ==")
    requests = [
        (1191, auth({"user_id": uid}), "admin products"),
        (1040, auth({"user_id": uid}), "order query"),
        (1070, auth({"user_id": uid}), "return list"),
    ]
    t0 = time.perf_counter()
    for code, body, _ in requests:
        pc.send(code, body)
    print(f"    3 packets sent in {(time.perf_counter() - t0) * 1000:.2f}ms")
    for _, _, label in requests:
        rcode, resp = pc.recv_one()
        counts = {k: len(v) for k, v in resp.items() if isinstance(v, list)}
        print(f"    recv for [{label}]: resp={rcode} {counts}")

    print()
    print("== 3. 详情页场景：detail 后背靠背 sku + groups ==")
    pc.send(1002, auth({"product_id": 1}))
    rc, detail = pc.recv_one()
    print(f"    detail resp={rcode} name={detail.get('name')}")
    pc.send(1200, auth({"product_id": 1}))
    pc.send(1093, auth({"user_id": uid}))
    rc1, r1 = pc.recv_one()
    rc2, r2 = pc.recv_one()
    print(f"    sku resp={rc1} skus={len(r1.get('skus', []))}")
    print(f"    groups resp={rc2} groups={len(r2.get('groups', []))}")

    pc.close()
    print()
    print("ALL SERVER-SIDE CHECKS PASSED")


if __name__ == "__main__":
    main()
