#!/usr/bin/env python3
"""Reader for Refractor RFA archives (BF1942). Stdlib only.

Format spec: docs/formats/rfa.md

Usage:
  rfa.py list    <archive.rfa>
  rfa.py extract <archive.rfa> <out_dir>
"""
import os
import struct
import sys


def _ext_len(src, ip, base):
    # LZO run-length extension: each zero byte adds 255, final byte is added.
    n = base
    while src[ip] == 0:
        n += 255
        ip += 1
    return n + src[ip], ip + 1


def lzo1x_decompress(src, out_len):
    out = bytearray()
    ip = 0
    state = 0  # 0: next op may be a literal run; 1-3: trailing literals just copied; 4: long literal run copied

    def copy_match(dist, length):
        start = len(out) - dist
        if start < 0:
            raise ValueError("LZO: match distance before start of output")
        for i in range(length):  # byte by byte: matches may overlap their own output
            out.append(out[start + i])

    if src[0] > 17:
        t = src[0] - 17
        out += src[1:1 + t]
        ip = 1 + t
        state = 4 if t >= 4 else t

    while True:
        t = src[ip]
        ip += 1
        if t < 16:
            if state == 0:
                if t == 0:
                    t, ip = _ext_len(src, ip, 15)
                t += 3
                out += src[ip:ip + t]
                ip += t
                state = 4
                continue
            if state == 4:
                dist, length = 1 + 0x800 + (t >> 2) + (src[ip] << 2), 3
            else:
                dist, length = 1 + (t >> 2) + (src[ip] << 2), 2
            ip += 1
            nxt = t & 3
        elif t >= 64:
            dist = 1 + ((t >> 2) & 7) + (src[ip] << 3)
            length = (t >> 5) + 1
            ip += 1
            nxt = t & 3
        elif t >= 32:
            length = t & 31
            if length == 0:
                length, ip = _ext_len(src, ip, 31)
            length += 2
            d = src[ip] | (src[ip + 1] << 8)
            ip += 2
            dist = (d >> 2) + 1
            nxt = d & 3
        else:  # 16..31: M4 match or end-of-stream marker
            length = t & 7
            if length == 0:
                length, ip = _ext_len(src, ip, 7)
            length += 2
            d = src[ip] | (src[ip + 1] << 8)
            ip += 2
            dist = 0x4000 + ((t & 8) << 11) + (d >> 2)
            if dist == 0x4000:
                break
            nxt = d & 3
        copy_match(dist, length)
        out += src[ip:ip + nxt]
        ip += nxt
        state = nxt

    if len(out) != out_len:
        raise ValueError(f"LZO: got {len(out)} bytes, expected {out_len}")
    return bytes(out)


def read_dir(data):
    dir_off, compressed = struct.unpack_from("<II", data, 0)
    (count,) = struct.unpack_from("<I", data, dir_off)
    o = dir_off + 4
    entries = []
    for _ in range(count):
        (n,) = struct.unpack_from("<I", data, o)
        name = data[o + 4:o + 4 + n].decode("latin-1")
        o += 4 + n
        stored, size, off = struct.unpack_from("<3I", data, o)
        o += 24
        entries.append((name, stored, size, off))
    return bool(compressed), entries


def read_index(f):
    """Directory of an open archive without reading file data; offsets stay absolute."""
    f.seek(0)
    dir_off, compressed = struct.unpack("<II", f.read(8))
    f.seek(dir_off)
    return read_dir(struct.pack("<II", 8, compressed) + f.read())  # re-point header at the tail


def read_file(data, compressed, size, off):
    if not compressed:
        out = data[off:off + size]
        if len(out) != size:
            raise ValueError(f"truncated file: {len(out)} != {size}")
        return out
    (nseg,) = struct.unpack_from("<I", data, off)
    base = off + 4 + 12 * nseg
    out = bytearray()
    for i in range(nseg):
        cs, us, rel = struct.unpack_from("<3I", data, off + 4 + 12 * i)
        out += lzo1x_decompress(data[base + rel:base + rel + cs], us)
    if len(out) != size:
        raise ValueError(f"size mismatch: {len(out)} != {size}")
    return bytes(out)


def safe_path(out_dir, name):
    # Archive names are untrusted: refuse absolute paths and '..' escapes.
    root = os.path.realpath(out_dir)
    path = os.path.realpath(os.path.join(root, name.replace("\\", "/").lstrip("/")))
    if os.path.commonpath([root, path]) != root:
        raise ValueError(f"unsafe path in archive: {name!r}")
    return path


def main(argv):
    if len(argv) < 3 or argv[1] not in ("list", "extract"):
        sys.exit(__doc__)
    with open(argv[2], "rb") as f:
        data = f.read()
    compressed, entries = read_dir(data)
    if argv[1] == "list":
        for name, stored, size, _ in entries:
            print(f"{size:>10} {stored:>10}  {name}")
        return
    if len(argv) < 4:
        sys.exit(__doc__)
    for name, _, size, off in entries:
        path = safe_path(argv[3], name)
        os.makedirs(os.path.dirname(path), exist_ok=True)
        with open(path, "wb") as f:
            f.write(read_file(data, compressed, size, off))
    print(f"{len(entries)} files -> {argv[3]}")


if __name__ == "__main__":
    main(sys.argv)
