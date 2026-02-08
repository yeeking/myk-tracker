#!/usr/bin/env python3
import socket
import struct
import sys
import time 

_DEF_HOST = "127.0.0.1"
_DEF_PORT = 9000


def _align4(n: int) -> int:
    return (n + 3) & ~3


def _osc_string(s: str) -> bytes:
    data = s.encode("utf-8") + b"\x00"
    pad = _align4(len(data)) - len(data)
    if pad:
        data += b"\x00" * pad
    return data


def _osc_message(address: str, value) -> bytes:
    if isinstance(value, str):
        type_tag = ",s"
        payload = _osc_string(value)
    elif isinstance(value, int):
        type_tag = ",i"
        payload = struct.pack(">i", value)
    else:
        raise TypeError(f"Unsupported value type: {type(value).__name__}")
    return _osc_string(address) + _osc_string(type_tag) + payload


def main(argv: list[str]) -> int:
    host = _DEF_HOST
    port = _DEF_PORT

    i = 1
    while i < len(argv):
        arg = argv[i]
        if arg == "--host" and i + 1 < len(argv):
            host = argv[i + 1]
            i += 2
            continue
        if arg == "--port" and i + 1 < len(argv):
            port = int(argv[i + 1])
            i += 2
            continue
        if arg in {"-h", "--help"}:
            print("Usage: test_osc_client.py [--host HOST] [--port PORT]")
            return 0
        i += 1

    messages = ["one", "two", "three", 4, 99, 123]

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sendto = sock.sendto
    addr = (host, port)
    for value in messages:
        print(f"Sending {value}")
        sendto(_osc_message("/lcd", value), addr)
        time.sleep(2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main(sys.argv))
