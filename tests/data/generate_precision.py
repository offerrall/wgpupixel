#!/usr/bin/env python3
"""Regenerate precision fixtures with Python 3's standard library; see precision.txt."""
import struct
import zlib
from pathlib import Path

ROOT = Path(__file__).resolve().parent
WIDTH, HEIGHT = 3, 2
INTEGER = [(1, 2650, 2651, 65535), (12345, 32767, 54321, 40001),
           (65534, 32768, 257, 1), (65535, 4096, 61680, 0),
           (32768, 32769, 32770, 65535), (17, 40000, 60000, 32769)]
FLOAT = [(2.5, -0.125, 1 + 2**-23, 1), (0.123456789, 0.03125, 4.5, 0.5),
         (0.25, -0.5, 8, 0), (65504, -12.5, 1/3, 1),
         (2**-14, 2**-24, -0.0, 0.25), (1.125, 1.126, 0.1, 0.75)]


def profile(name, linear=False):
    data = (ROOT / name).read_bytes()
    offset = 8
    while offset < len(data):
        size, tag = struct.unpack_from('>I4s', data, offset)
        body = data[offset + 8:offset + 8 + size]
        offset += size + 12
        if tag == b'iCCP':
            result = bytearray(zlib.decompress(body[body.index(b'\0') + 2:]))
            if linear:
                count, = struct.unpack_from('>I', result, 128)
                for entry in range(132, 132 + 12 * count, 12):
                    tag, start, size = struct.unpack_from('>4sII', result, entry)
                    if tag.endswith(b'TRC'):
                        result[start:start + 16] = b'para' + bytes(8) + struct.pack('>I', 65536)
                        struct.pack_into('>I', result, entry + 8, 16)
                result[84:100] = bytes(16)  # No stale profile ID after changing the TRCs.
            return bytes(result)
    raise ValueError('missing ICC profile')


def chunk(tag, body):
    return struct.pack('>I', len(body)) + tag + body + struct.pack('>I', zlib.crc32(tag + body))


def png(name, gray=False, icc=None, gamma=None, orientation=None):
    data = b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>IIBBBBB', WIDTH, HEIGHT, 16,
                                                              4 if gray else 6, 0, 0, 0))
    if icc:
        data += chunk(b'iCCP', b'fixture\0\0' + zlib.compress(icc))
    elif gamma:
        data += chunk(b'gAMA', struct.pack('>I', gamma))
    else:
        data += chunk(b'sRGB', b'\0')
    if orientation:
        data += chunk(b'eXIf', b'II' + struct.pack('<HIHHHII', 42, 8, 1, 274, 3, 1, orientation)
                      + struct.pack('<I', 0))
    raw = b''
    for y in range(HEIGHT):
        raw += b'\0'
        for pixel in INTEGER[y * WIDTH:(y + 1) * WIDTH]:
            values = (pixel[0], pixel[3]) if gray else pixel
            raw += struct.pack('>' + 'H' * len(values), *values)
    data += chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    (ROOT / name).write_bytes(data)


def tiff(name, floating=False, planar=False, big=False, associated=False, alpha=True,
         icc=None, orientation=1, gray=False):
    endian = '>' if big else '<'
    samples = FLOAT if floating else INTEGER
    channels, depth = (2 if gray else 4 if alpha else 3), (32 if floating else 16)
    values = [(pixel[0], pixel[3]) if gray else pixel[:channels] for pixel in samples]
    if associated and not floating:
        values = [tuple(round(c * pixel[-1] / 65535) for c in pixel[:-1]) + (pixel[-1],)
                  for pixel in values]
    if planar:
        values = [[pixel[c] for pixel in values] for c in range(channels)]
    raw = b''.join(struct.pack(endian + ('f' if floating else 'H') * len(v), *v) for v in values)
    strips = channels if planar else 1
    strip_size = len(raw) // strips
    # Baseline, uncompressed TIFF; integer samples are untagged sRGB, floats linear.
    tags = {256: (4, [WIDTH]), 257: (4, [HEIGHT]), 258: (3, [depth] * channels),
            259: (3, [1]), 262: (3, [1 if gray else 2]), 273: (4, [0] * strips), 274: (3, [orientation]),
            277: (3, [channels]), 278: (4, [HEIGHT]), 279: (4, [strip_size] * strips),
            284: (3, [2 if planar else 1]), 339: (3, [3 if floating else 1] * channels)}
    if alpha:
        tags[338] = (3, [1 if associated else 2])
    if icc:
        tags[34675] = (7, icc)
    start = 8 + 2 + len(tags) * 12 + 4
    extra = bytearray()
    entries = bytearray()
    strip_location = None
    for tag, (kind, values) in sorted(tags.items()):
        data = bytes(values) if kind == 7 else struct.pack(endian + ('H' if kind == 3 else 'I') * len(values), *values)
        entries += struct.pack(endian + 'HHI', tag, kind, len(values))
        if len(data) <= 4:
            if tag == 273:
                strip_location = ('entries', len(entries))
            entries += data.ljust(4, b'\0')
        else:
            entries += struct.pack(endian + 'I', start + len(extra))
            if tag == 273:
                strip_location = ('extra', len(extra))
            extra += data
            extra += bytes(len(extra) % 2)
    where, offset = strip_location
    struct.pack_into(endian + 'I' * strips, entries if where == 'entries' else extra,
                     offset, *(start + len(extra) + i * strip_size for i in range(strips)))
    data = (b'MM' if big else b'II') + struct.pack(endian + 'HIH', 42, 8, len(tags))
    (ROOT / name).write_bytes(data + entries + bytes(4) + extra + raw)


def exr(name, half=False, gray=False, alpha=True, nonfinite=False):
    def attribute(name, kind, data):
        return name.encode() + b'\0' + kind.encode() + b'\0' + struct.pack('<I', len(data)) + data
    channels = [('Y', 0)] if gray else [('B', 2), ('G', 1), ('R', 0)]
    if alpha:
        channels.insert(0, ('A', 3))
    chlist = b''.join(n.encode() + b'\0' + struct.pack('<iB3xii', 1 if half else 2, 0, 1, 1)
                      for n, _ in channels) + b'\0'
    box = struct.pack('<4i', 0, 0, WIDTH - 1, HEIGHT - 1)
    header = struct.pack('<II', 20000630, 2)
    for n, kind, data in [('channels', 'chlist', chlist), ('compression', 'compression', b'\0'),
                          ('dataWindow', 'box2i', box), ('displayWindow', 'box2i', box),
                          ('lineOrder', 'lineOrder', b'\0'), ('pixelAspectRatio', 'float', struct.pack('<f', 1)),
                          ('screenWindowCenter', 'v2f', bytes(8)), ('screenWindowWidth', 'float', struct.pack('<f', 1))]:
        header += attribute(n, kind, data)
    header += b'\0'
    blocks = []
    offsets = []
    for y in range(HEIGHT):
        offsets.append(len(header) + HEIGHT * 8 + sum(map(len, blocks)))
        raw = b''.join(struct.pack('<e' if half else '<f',
                                  float('inf') if nonfinite and c == 0 else FLOAT[y * WIDTH + x][c])
                       for _, c in channels for x in range(WIDTH))
        blocks.append(struct.pack('<iI', y, len(raw)) + raw)
    (ROOT / name).write_bytes(header + struct.pack('<' + 'Q' * HEIGHT, *offsets) + b''.join(blocks))


rgb_profile = profile('wide-icc.png')
gray_profile = profile('gray-icc.png')
png('precision.png')
png('precision-icc.png', icc=rgb_profile)
png('precision-gray.png', gray=True)
png('precision-gray-icc.png', gray=True, icc=gray_profile)
png('precision-linear.png', gamma=100000)
png('precision-gamma22.png', gamma=45455)
png('precision-gamma28.png', gamma=35714)
png('precision-oriented.png', orientation=6)
tiff('precision.tiff')
tiff('precision-gray.tiff', gray=True)
tiff('precision-associated-icc.tiff', associated=True, icc=rgb_profile)
tiff('precision-planar-be.tiff', planar=True, big=True)
tiff('precision-associated.tiff', associated=True)
tiff('precision-icc.tiff', icc=rgb_profile)
tiff('precision-float.tiff', floating=True)
tiff('precision-float-associated.tiff', floating=True, associated=True)
tiff('precision-float-planar-be.tiff', floating=True, planar=True, big=True, associated=True)
tiff('precision-float-be.tiff', floating=True, big=True)
tiff('precision-float-rgb.tiff', floating=True, alpha=False)
tiff('precision-float-oriented.tiff', floating=True, associated=True, orientation=6)
tiff('precision-float-icc.tiff', floating=True, icc=profile('wide-icc.png', linear=True))
exr('precision-float.exr')
exr('precision-half.exr', half=True)
exr('precision-rgb.exr', alpha=False)
exr('precision-gray.exr', gray=True)
exr('precision-gray-half.exr', half=True, gray=True, alpha=False)
exr('precision-invalid.exr', nonfinite=True)
exr('precision-invalid-half.exr', half=True, nonfinite=True)
