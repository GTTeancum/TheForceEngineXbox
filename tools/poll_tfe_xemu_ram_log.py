#!/usr/bin/env python
from __future__ import print_function

import argparse
import io
import os
import re
import socket
import struct
import time


MIRROR_BYTES = 16384
LOG_MIRROR_CAPACITY = 524287


def strip_ansi(text):
    return re.sub(r"\x1b\[[0-9;?]*[A-Za-z]", "", text)


def connect_monitor(port, timeout):
    end = time.time() + timeout
    last = None
    while time.time() < end:
        sock = None
        try:
            sock = socket.socket()
            sock.settimeout(1.5)
            sock.connect(("127.0.0.1", port))
            time.sleep(0.1)
            try:
                sock.recv(65536)
            except Exception:
                pass
            return sock
        except Exception as exc:
            last = exc
            if sock:
                try:
                    sock.close()
                except Exception:
                    pass
            time.sleep(0.25)
    raise RuntimeError("monitor port %d not ready: %s" % (port, last))


def monitor_cmd(sock, command, wait=0.25):
    sock.sendall((command + "\r\n").encode("ascii"))
    time.sleep(wait)
    data = b""
    sock.settimeout(0.8)
    try:
        while True:
            chunk = sock.recv(65536)
            if not chunk:
                break
            data += chunk
    except Exception:
        pass
    return strip_ansi(data.decode("utf-8", errors="replace"))


def parse_words(text):
    words = []
    for line in text.splitlines():
        if ":" not in line:
            continue
        _addr, values = line.split(":", 1)
        for token in re.findall(r"\b(?:0x)?[0-9a-fA-F]{8}\b", values):
            try:
                words.append(int(token, 16))
            except ValueError:
                pass
    return words


def read_words(sock, va, words, phys_delta):
    command = "x"
    addr = va
    if phys_delta is not None:
        command = "xp"
        addr = va - phys_delta
    reply = monitor_cmd(sock, "%s/%dwx 0x%08x" % (command, words, addr), 0.25)
    return parse_words(reply)


def read_u32(sock, va, phys_delta):
    words = read_words(sock, va, 1, phys_delta)
    return words[0] if words else None


def read_bytes(sock, va, byte_count, phys_delta):
    raw = bytearray()
    offset = 0
    while offset < byte_count:
        chunk = min(2048, byte_count - offset)
        words = read_words(sock, va + offset, (chunk + 3) // 4, phys_delta)
        if not words:
            break
        for word in words:
            raw.extend((
                word & 0xff,
                (word >> 8) & 0xff,
                (word >> 16) & 0xff,
                (word >> 24) & 0xff,
            ))
        offset += chunk
    return bytes(raw[:byte_count])


def read_xbe_sections(xbe_path):
    with open(xbe_path, "rb") as handle:
        data = handle.read()
    base = struct.unpack_from("<I", data, 0x104)[0]
    count = struct.unpack_from("<I", data, 0x11C)[0]
    table = struct.unpack_from("<I", data, 0x120)[0] - base
    sections = {}
    for index in range(count):
        header = table + index * 0x38
        va = struct.unpack_from("<I", data, header + 0x04)[0]
        size = struct.unpack_from("<I", data, header + 0x08)[0]
        name_va = struct.unpack_from("<I", data, header + 0x14)[0]
        name_off = name_va - base
        name_end = data.find(b"\x00", name_off)
        if name_off < 0 or name_end < 0:
            continue
        name = data[name_off:name_end].decode("ascii", errors="replace")
        sections[name] = (va, size)
    return sections


def read_map_segment_sections(map_path):
    segments = {}
    pattern = re.compile(r"^\s*([0-9a-fA-F]{4}):[0-9a-fA-F]{8}\s+[0-9a-fA-F]+H\s+(\S+)\s+")
    with io.open(map_path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            match = pattern.match(line)
            if match:
                segment = int(match.group(1), 16)
                segments.setdefault(segment, set()).add(match.group(2))
    return segments


def section_for_segment(section_names):
    if ".bss" in section_names or ".data" in section_names:
        return ".data"
    if ".rdata" in section_names:
        return ".rdata"
    for name in section_names:
        if name.startswith(".text"):
            return ".text"
    for name in section_names:
        if name in ("D3D", "D3DX", "DSOUND", "XGRPH", "XNET", "XONLINE"):
            return name
    return None


def resolve_symbol(map_path, xbe_sections, map_segments, symbol):
    names = [symbol]
    if not symbol.startswith("_"):
        names.append("_" + symbol)
    else:
        names.append(symbol[1:])

    patterns = [
        re.compile(r"\b([0-9a-fA-F]{4}):([0-9a-fA-F]{8})\s+%s\b\s+([0-9a-fA-F]{8})\b" % re.escape(name))
        for name in names
    ]
    with io.open(map_path, "r", encoding="utf-8", errors="replace") as handle:
        for line in handle:
            for pattern in patterns:
                match = pattern.search(line)
                if match:
                    segment = int(match.group(1), 16)
                    offset = int(match.group(2), 16)
                    pe_va = int(match.group(3), 16)
                    section_name = section_for_segment(map_segments.get(segment, set()))
                    if xbe_sections and section_name:
                        section = xbe_sections.get(section_name)
                        if section:
                            return section[0] + offset
                    return pe_va
    raise RuntimeError("symbol not found in map: %s" % symbol)


def resolve_symbols(map_path, xbe_path):
    xbe_sections = read_xbe_sections(xbe_path)
    map_segments = read_map_segment_sections(map_path)
    required = [
        "g_TFEXBLogWriteOffset",
        "g_TFEXBLogMirror",
        "g_TFEXBLogLastLine",
    ]
    return dict((name, resolve_symbol(map_path, xbe_sections, map_segments, name)) for name in required)


def score_probe(sock, symbols, phys_delta):
    offset = read_u32(sock, symbols["g_TFEXBLogWriteOffset"], phys_delta)
    if offset is None or offset > LOG_MIRROR_CAPACITY * 128:
        return -1, offset, ""
    raw = read_bytes(sock, symbols["g_TFEXBLogLastLine"], 512, phys_delta)
    text = raw.replace(b"\x00", b"").decode("ascii", errors="replace")
    score = text.count("[") + text.count("]")
    if "Main" in text or "System" in text or "RenderBackend" in text:
        score += 10
    if offset > 0:
        score += 3
    return score, offset, text


def choose_phys_delta(sock, symbols, requested):
    if requested == "0":
        return None
    if requested != "auto":
        return int(requested, 0)

    candidates = [None, 0x2A4000, 0x287000, 0x286000, 0x285000, 0x284000,
                  0x283000, 0x282000, 0x281000, 0x280000, 0x264000]
    best = (-1, None)
    for candidate in candidates:
        score, _offset, _text = score_probe(sock, symbols, candidate)
        if score > best[0]:
            best = (score, candidate)
    if best[0] < 0:
        raise RuntimeError("could not resolve virtual-to-physical delta")
    return best[1]


def decode_mirror(raw, offset):
    if not raw:
        return ""
    raw = raw.replace(b"\x00", b"")
    text = raw.decode("ascii", errors="replace")
    return text


def poll_port(port, symbols, args):
    sock = connect_monitor(port, args.timeout)
    try:
        phys_delta = choose_phys_delta(sock, symbols, args.phys_delta)
        offset = read_u32(sock, symbols["g_TFEXBLogWriteOffset"], phys_delta)
        last = read_bytes(sock, symbols["g_TFEXBLogLastLine"], 512, phys_delta)
        raw = b""
        if not args.header_only:
            mirror_offset = offset or 0
            if mirror_offset >= LOG_MIRROR_CAPACITY:
                mirror_offset = LOG_MIRROR_CAPACITY
            start = mirror_offset - MIRROR_BYTES
            if start < 0:
                start = 0
            raw = read_bytes(sock, symbols["g_TFEXBLogMirror"] + start, MIRROR_BYTES, phys_delta)
        return phys_delta, offset, last, decode_mirror(raw, offset or 0)
    finally:
        sock.close()


def main():
    parser = argparse.ArgumentParser(description="Poll The Force Engine Xbox RAM log mirror from XEMU HMP monitors.")
    parser.add_argument("--ports", required=True, help="Comma-separated HMP monitor ports.")
    parser.add_argument("--map", default=os.path.join("build", "xbox", "release", "default.exe.map"))
    parser.add_argument("--xbe", default=os.path.join("build", "xbox", "release", "default.xbe"))
    parser.add_argument("--out-dir", default=os.path.join("build", "xemu", "tfe_ram_logs"))
    parser.add_argument("--phys-delta", default="0", help="'0' for virtual x/ reads, 'auto', or a hex VA-physical delta.")
    parser.add_argument("--timeout", type=float, default=5.0)
    parser.add_argument("--header-only", action="store_true")
    args = parser.parse_args()

    symbols = resolve_symbols(args.map, args.xbe)
    if not os.path.isdir(args.out_dir):
        os.makedirs(args.out_dir)

    for item in args.ports.split(","):
        port = int(item.strip())
        phys_delta, offset, last, text = poll_port(port, symbols, args)
        last_text = last.replace(b"\x00", b"").decode("ascii", errors="replace")
        out_path = os.path.join(args.out_dir, "port%d_tfe_ram_log.txt" % port)
        with io.open(out_path, "w", encoding="utf-8", errors="replace") as handle:
            handle.write("port=%d\n" % port)
            handle.write("phys_delta=%s\n" % ("none" if phys_delta is None else "0x%08x" % phys_delta))
            handle.write("write_offset=%s\n" % (offset if offset is not None else "none"))
            handle.write("last_line=%s\n\n" % last_text)
            handle.write(text)
        print(out_path)


if __name__ == "__main__":
    main()
