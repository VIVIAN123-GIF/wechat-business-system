# -*- coding: utf-8 -*-
"""
Mock 服务端：用录制的真实响应回答 qt_client，稳定不崩溃，
用于长时间实测 Qt 各页面的响应行为。
"""
import socket, struct, json, os, threading, time

MAGIC, VERSION = 0x4D495F53, 1
HEADER = struct.Struct(">IIII")
ROOT = r"D:\OneDrive\Desktop\object_course"

with open(os.path.join(ROOT, "test-run-mock-data.json"), encoding="utf-8") as f:
    DATA = json.load(f)

REQ_TO_RESP = {
    1006: 2006, 1001: 2001, 1011: 2011, 1040: 2040, 1070: 2070,
    1120: 2120, 1191: 2191, 1202: 2202, 1210: 2210, 1150: 2150,
    1093: 2093, 1200: 2200, 1002: 2002, 1140: 2140, 1160: 2160,
    1060: 2060, 1020: 2020,
}

# 写操作给通用成功响应
GENERIC_OK = {
    1010: 2010, 1012: 2012, 1013: 2013, 1014: 2014, 1030: 2030,
    1050: 2050, 1065: 2065, 1080: 2080, 1081: 2081, 1090: 2090,
    1091: 2091, 1092: 2092, 1095: 2095, 1096: 2096, 1100: 2100,
    1101: 2101, 1110: 2110, 1130: 2130, 1131: 2131, 1141: 2141,
    1151: 2151, 1161: 2161, 1170: 2170, 1180: 2180, 1181: 2181,
    1190: 2190, 1201: 2201, 1203: 2203, 1204: 2204,
}

log_lock = threading.Lock()


def log(msg):
    with log_lock:
        print(f"[{time.strftime('%H:%M:%S')}] {msg}", flush=True)


def serve(conn, addr):
    log(f"client connected {addr}")
    buf = b""
    try:
        while True:
            d = conn.recv(65536)
            if not d:
                break
            buf += d
            while len(buf) >= 16:
                magic, ver, code, n = HEADER.unpack_from(buf, 0)
                if magic != MAGIC:
                    conn.close()
                    return
                if len(buf) < 16 + n:
                    break
                body = buf[16:16+n]
                buf = buf[16+n:]
                log(f"recv req={code} body={n}B")
                if code in REQ_TO_RESP:
                    rc = REQ_TO_RESP[code]
                    payload = DATA[str(rc)].encode("utf-8")
                elif code in GENERIC_OK:
                    rc = GENERIC_OK[code]
                    payload = json.dumps({"ok": True}).encode("utf-8")
                else:
                    rc = 4001
                    payload = json.dumps({"err_code": 1, "msg": "unknown"}).encode("utf-8")
                conn.sendall(HEADER.pack(MAGIC, VERSION, rc, len(payload)) + payload)
                log(f"sent resp={rc} {len(payload)}B")
    except ConnectionError:
        pass
    finally:
        conn.close()
        log(f"client disconnected {addr}")


def main():
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", 8888))
    srv.listen(8)
    log("mock server listening on 8888")
    while True:
        conn, addr = srv.accept()
        threading.Thread(target=serve, args=(conn, addr), daemon=True).start()


if __name__ == "__main__":
    main()
