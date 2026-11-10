#!/usr/bin/env python3
"""Writes a copy of Knytt.exe that scales its picture pixel for pixel.

Usage: patch-game.py <original Knytt.exe> <Scale.sft> <output Knytt.exe>

Knytt.exe is a small unpacker followed by a pack of runtime files and the game data.
Two things change in the copy:
  - Scale.sft joins the pack, where the runtime loads it as a sound filter.
  - The Stretch flag is set in the game's header, so the runtime stretches the picture,
    unfiltered, to the window Scale.sft gives it. The window also gets a frame that
    resizes and a maximize button.
"""
import struct
import sys
import zlib

PACK_MAGIC = b"wwwwI\x87G\x12"
APP_HEADER = 0x2223
STRETCH = 0x10            # in the first flags word
NO_MAXIMIZE_BOX = 0x20    # in the second
NO_THICK_FRAME = 0x40

src, dll, dst = sys.argv[1:]
d = open(src, "rb").read()
name = b"Scale.sft"

pack = d.index(PACK_MAGIC)
size, = struct.unpack_from("<I", d, pack + 12)
count, = struct.unpack_from("<I", d, pack + 28)
game = pack + size - 32   # the size counts 32 bytes of the game data
assert d[game:game + 4] == b"PAME", "not the Knytt 1.0.2 release"

# The game data is a list of chunks: id, flags, size, then the body (zlib if flags is 1).
at = game + 16
cid, flags, csize = struct.unpack_from("<HHI", d, at)
assert (cid, flags) == (APP_HEADER, 1)
header = bytearray(zlib.decompress(d[at + 16:at + 8 + csize]))
header[4] |= STRETCH
header[6] &= ~(NO_MAXIMIZE_BOX | NO_THICK_FRAME)
body = zlib.compress(bytes(header), 9)
chunk = struct.pack("<HHIII", cid, flags, len(body) + 8, len(header), len(body)) + body

filt = zlib.compress(open(dll, "rb").read(), 9)
entry = struct.pack("<H", len(name)) + name + struct.pack("<I", len(filt)) + filt

out = bytearray(d[:game] + entry + d[game:at] + chunk + d[at + 8 + csize:])
struct.pack_into("<I", out, pack + 12, size + len(entry))
struct.pack_into("<I", out, pack + 28, count + 1)
open(dst, "wb").write(out)
