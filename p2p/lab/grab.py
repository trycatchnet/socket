#!/usr/bin/env python3
"""Ask a real STUN server for a Binding Response and keep both messages.

No root, no lab, no tcpdump: it opens a UDP socket, sends a Binding Request,
and writes the exact bytes of the request and the reply into the corpus. The
reply is the valuable one - it comes from a production server and carries the
attributes your decoder has not met yet.

Usage: ./grab.py [host] [port] [corpus_dir]
Default: stun.l.google.com 19302 ../stun/tests/corpus
"""

import os
import socket
import struct
import sys

STUN_MAGIC = 0x2112A442
BINDING_REQUEST = 0x0001


def classify(payload):
    if len(payload) < 20:
        return "short"
    msg_type, _length, cookie = struct.unpack("!HHI", payload[0:8])
    if cookie != STUN_MAGIC:
        return "notstun"
    cls = ((msg_type >> 7) & 0x02) | ((msg_type >> 4) & 0x01)
    return {0: "request", 1: "indication", 2: "success", 3: "error"}[cls]


def save(corpus_dir, payload):
    """Write one message, numbered after whatever is already in the corpus."""
    existing = [f for f in os.listdir(corpus_dir) if f.endswith(".bin")]
    index = len(existing)
    name = f"{index:03d}_{classify(payload)}_{len(payload)}b.bin"
    with open(os.path.join(corpus_dir, name), "wb") as fh:
        fh.write(payload)
    print(f"{name:32s} {len(payload)} bytes")
    return name


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "stun.l.google.com"
    port = int(sys.argv[2]) if len(sys.argv) > 2 else 19302
    corpus_dir = sys.argv[3] if len(sys.argv) > 3 else "../stun/tests/corpus"
    os.makedirs(corpus_dir, exist_ok=True)

    # 20-byte header, no attributes: type, length, cookie, transaction id
    tid = os.urandom(12)
    request = struct.pack("!HHI", BINDING_REQUEST, 0, STUN_MAGIC) + tid

    sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    sock.settimeout(3.0)
    try:
        sock.sendto(request, (host, port))
        response, peer = sock.recvfrom(2048)
    except socket.timeout:
        sys.exit(f"grab: no answer from {host}:{port} within 3s")
    except OSError as err:
        sys.exit(f"grab: {err}")
    finally:
        sock.close()

    if response[4:20] != request[4:20]:
        print("warning: transaction id did not match, saving anyway")

    print(f"talked to {peer[0]}:{peer[1]}")
    save(corpus_dir, request)
    save(corpus_dir, response)
    print(f"\ncorpus is at {corpus_dir} - now run: cd ../stun && make test")


if __name__ == "__main__":
    main()
