#!/usr/bin/env python3
"""Cut the UDP payloads out of a pcap and drop each one into a corpus directory.

Every file it writes is one STUN message exactly as it appeared on the wire.
That file is the answer key: your decoder has to understand it and your encoder
has to reproduce it byte for byte.

Usage: ./carve.py captures/foo.pcap ../stun/tests/corpus
"""

import os
import struct
import sys

LINKTYPE_ETHERNET = 1
LINKTYPE_LINUX_SLL = 113
LINKTYPE_LINUX_SLL2 = 276
LINKTYPE_RAW = 101

STUN_MAGIC = 0x2112A442


def read_pcap(path):
    """Yield raw link-layer frames from a classic (non-ng) pcap file."""
    with open(path, "rb") as fh:
        magic = fh.read(4)
        if magic == b"\xd4\xc3\xb2\xa1":
            endian, nano = "<", False
        elif magic == b"\xa1\xb2\xc3\xd4":
            endian, nano = ">", False
        elif magic == b"\x4d\x3c\xb2\xa1":
            endian, nano = "<", True
        elif magic == b"\xa1\xb2\x3c\x4d":
            endian, nano = ">", True
        elif magic == b"\x0a\x0d\x0d\x0a":
            sys.exit("carve: this is a pcapng file; capture with tcpdump -w (classic pcap)")
        else:
            sys.exit("carve: not a pcap file")

        _vmaj, _vmin, _tz, _sig, _snap, linktype = struct.unpack(endian + "HHiIII", fh.read(20))

        while True:
            hdr = fh.read(16)
            if len(hdr) < 16:
                return
            ts_sec, ts_frac, caplen, _origlen = struct.unpack(endian + "IIII", hdr)
            data = fh.read(caplen)
            if len(data) < caplen:
                return
            ts = ts_sec + ts_frac / (1e9 if nano else 1e6)
            yield linktype, ts, data


def strip_link(linktype, frame):
    """Return the IP packet inside a link-layer frame, or None."""
    if linktype == LINKTYPE_ETHERNET:
        if len(frame) < 14:
            return None
        ethertype = struct.unpack("!H", frame[12:14])[0]
        if ethertype != 0x0800:          # IPv4 only for now
            return None
        return frame[14:]
    if linktype == LINKTYPE_LINUX_SLL:
        if len(frame) < 16:
            return None
        if struct.unpack("!H", frame[14:16])[0] != 0x0800:
            return None
        return frame[16:]
    if linktype == LINKTYPE_LINUX_SLL2:
        if len(frame) < 20:
            return None
        if struct.unpack("!H", frame[0:2])[0] != 0x0800:
            return None
        return frame[20:]
    if linktype == LINKTYPE_RAW:
        return frame
    return None


def strip_ip_udp(pkt):
    """Return (src, dst, payload) for a UDP datagram, or None."""
    if len(pkt) < 20:
        return None
    version_ihl = pkt[0]
    if version_ihl >> 4 != 4:
        return None
    ihl = (version_ihl & 0x0F) * 4
    if pkt[9] != 17:                     # protocol 17 = UDP
        return None
    total_len = struct.unpack("!H", pkt[2:4])[0]
    src_ip = ".".join(str(b) for b in pkt[12:16])
    dst_ip = ".".join(str(b) for b in pkt[16:20])

    udp = pkt[ihl:total_len] if total_len else pkt[ihl:]
    if len(udp) < 8:
        return None
    src_port, dst_port, udp_len = struct.unpack("!HHH", udp[0:6])
    payload = udp[8:udp_len] if 8 <= udp_len <= len(udp) else udp[8:]
    if not payload:
        return None
    return (f"{src_ip}:{src_port}", f"{dst_ip}:{dst_port}", payload)


def classify(payload):
    """A short tag for the filename, from the two facts any STUN message shows."""
    if len(payload) < 20:
        return "short"
    msg_type, _length, cookie = struct.unpack("!HHI", payload[0:8])
    if cookie != STUN_MAGIC:
        return "notstun"
    # RFC 8489 section 5: the class lives in bits 4 and 8 of the message type
    cls = ((msg_type >> 7) & 0x02) | ((msg_type >> 4) & 0x01)
    return {0: "request", 1: "indication", 2: "success", 3: "error"}[cls]


def main():
    if len(sys.argv) != 3:
        sys.exit(__doc__.strip().splitlines()[-1])
    pcap_path, corpus_dir = sys.argv[1], sys.argv[2]
    os.makedirs(corpus_dir, exist_ok=True)

    seen = set()
    written = skipped = 0
    for index, (linktype, _ts, frame) in enumerate(read_pcap(pcap_path)):
        ip_pkt = strip_link(linktype, frame)
        if ip_pkt is None:
            continue
        parsed = strip_ip_udp(ip_pkt)
        if parsed is None:
            continue
        src, dst, payload = parsed

        digest = payload[:2] + payload[4:20]   # type + cookie + transaction id
        if digest in seen:
            skipped += 1                       # a retransmission, same bytes
            continue
        seen.add(digest)

        tag = classify(payload)
        name = f"{written:03d}_{tag}_{len(payload)}b.bin"
        with open(os.path.join(corpus_dir, name), "wb") as out:
            out.write(payload)
        print(f"{name:32s} {src:22s} -> {dst:22s} {len(payload)} bytes")
        written += 1

    print(f"\n{written} messages written to {corpus_dir}"
          + (f", {skipped} duplicates skipped" if skipped else ""))


if __name__ == "__main__":
    main()
