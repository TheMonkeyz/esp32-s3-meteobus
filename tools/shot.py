#!/usr/bin/env python3
"""Screens from a serial log: each "SHOT name w h" ... "SHOTEND" block (the display's `shot` console command, base64
RGB565 rows) becomes <out>/<name>.png.   python tools/shot.py serial_log.txt [out_dir]"""
import base64, os, struct, sys, zlib


def png(path, w, h, rgb):
    def chunk(t, d):
        c = struct.pack('>I', len(d)) + t + d
        return c + struct.pack('>I', zlib.crc32(t + d) & 0xffffffff)
    raw = b''.join(b'\0' + rgb[y * w * 3:(y + 1) * w * 3] for y in range(h))
    with open(path, 'wb') as f:
        f.write(b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', w, h, 8, 2, 0, 0, 0))
                + chunk(b'IDAT', zlib.compress(raw, 6)) + chunk(b'IEND', b''))


def main():
    log, out = sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else 'shots'
    os.makedirs(out, exist_ok=True)
    cur = None
    for line in open(log, encoding='utf-8', errors='replace'):
        line = line.strip().lstrip('﻿')
        if line.startswith('SHOT ') and not line.startswith('SHOTEND'):
            _, name, w, h = line.split()[:4]
            cur = [name, int(w), int(h), []]
        elif cur and line.startswith('S:'):
            try:
                cur[3].append(base64.b64decode(line[2:]))
            except Exception:
                cur[3].append(b'')
        elif cur and line.startswith('SHOTEND'):
            name, w, h, rows = cur
            rgb = bytearray()
            for y in range(h):
                r = rows[y] if y < len(rows) and len(rows[y]) == w * 2 else bytes(w * 2)
                for x in range(w):
                    v = r[2 * x] | r[2 * x + 1] << 8
                    rgb += bytes(((v >> 11 & 31) * 255 // 31, (v >> 5 & 63) * 255 // 63, (v & 31) * 255 // 31))
            p = os.path.join(out, name + '.png')
            png(p, w, h, bytes(rgb))
            print(p, f'{w}x{h}', f'{len(rows)} rows' + ('' if len(rows) == h else f' (expected {h})'))
            cur = None


if __name__ == '__main__':
    main()
