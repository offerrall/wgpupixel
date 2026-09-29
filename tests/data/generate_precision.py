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


def png(name, gray=False, icc=None, gamma=None, orientation=None, samples=INTEGER):
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
    raw = bytearray()
    for y in range(HEIGHT):
        raw += b'\0'
        for pixel in samples[y * WIDTH:(y + 1) * WIDTH]:
            values = (pixel[0], pixel[3]) if gray else pixel
            raw += struct.pack('>' + 'H' * len(values), *values)
    data += chunk(b'IDAT', zlib.compress(raw)) + chunk(b'IEND', b'')
    (ROOT / name).write_bytes(data)


def tiff(name, floating=False, planar=False, big=False, associated=False, alpha=True,
         icc=None, orientation=1, gray=False, samples=None, depth=None):
    endian = '>' if big else '<'
    samples = samples if samples is not None else FLOAT if floating else INTEGER
    channels = (1 if gray else 3) + int(alpha)
    depth = depth if depth is not None else 32 if floating else 16
    values = [(pixel[0], pixel[3]) if gray and alpha else (pixel[0],) if gray else pixel[:channels]
              for pixel in samples]
    if associated and not floating:
        values = [tuple(round(c * pixel[-1] / (2**depth - 1)) for c in pixel[:-1]) + (pixel[-1],)
                  for pixel in values]
    if planar:
        values = [[pixel[c] for pixel in values] for c in range(channels)]
    raw = b''.join(struct.pack(endian + ('f' if floating else 'H' if depth == 16 else 'B') * len(v), *v)
                   for v in values)
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


def retag(data, changes):
    """Rebuild an ICC directory, retaining the deterministic original header."""
    count, = struct.unpack_from('>I', data, 128)
    tags = {}
    for entry in range(132, 132 + 12 * count, 12):
        tag, offset, size = struct.unpack_from('>4sII', data, entry)
        tags[tag] = data[offset:offset + size]
    tags.update(changes)
    tags = {tag: value for tag, value in tags.items() if value is not None}
    header = bytearray(data[:128])
    header[84:100] = bytes(16)
    directory, body = bytearray(), bytearray()
    for tag, value in sorted(tags.items()):
        directory += struct.pack('>4sII', tag, 132 + 12 * len(tags) + len(body), len(value))
        body += value + bytes(-len(value) % 4)
    result = header + struct.pack('>I', len(tags)) + directory + body
    struct.pack_into('>I', result, 0, len(result))
    return bytes(result)


def matmul(a, b):
    return [[sum(x * y for x, y in zip(row, col)) for col in zip(*b)] for row in a]


def inverse(a):
    # Double-precision Gauss-Jordan elimination, independent of LittleCMS.
    rows = [list(row) + [float(i == j) for j in range(3)] for i, row in enumerate(a)]
    for i in range(3):
        pivot = max(range(i, 3), key=lambda j: abs(rows[j][i]))
        rows[i], rows[pivot] = rows[pivot], rows[i]
        scale = rows[i][i]
        rows[i] = [v / scale for v in rows[i]]
        for j in range(3):
            if i != j:
                scale = rows[j][i]
                rows[j] = [v - scale * p for v, p in zip(rows[j], rows[i])]
    return [row[3:] for row in rows]


def xyz(x, y):
    return [[x / y], [1.0], [(1 - x - y) / y]]


def rgb_matrix(primaries):
    matrix = [list(row) for row in zip(*(sum(xyz(x, y), []) for x, y in primaries))]
    scale = matmul(inverse(matrix), xyz(0.3127, 0.3290))
    return [[v * scale[c][0] for c, v in enumerate(row)] for row in matrix]


# Bradford adaptation, D65 RGB to the D50 ICC PCS. ICC XYZ tags are s15Fixed16.
bradford = [[0.8951, 0.2664, -0.1614], [-0.7502, 1.7135, 0.0367],
            [0.0389, -0.0685, 1.0296]]
d65 = matmul(bradford, xyz(0.3127, 0.3290))
d50 = matmul(bradford, [[0.9642], [1.0], [0.8249]])
adapt = matmul(inverse(bradford), [[v * d50[r][0] / d65[r][0] for v in row]
                                  for r, row in enumerate(bradford)])
srgb_matrix = matmul(adapt, rgb_matrix([(0.64, 0.33), (0.30, 0.60), (0.15, 0.06)]))
adobe_matrix = matmul(adapt, rgb_matrix([(0.64, 0.33), (0.21, 0.71), (0.15, 0.06)]))
adobe_matrix = [[round(v * 65536) / 65536 for v in row] for row in adobe_matrix]
gamma = 563 / 256
trc = b'curv' + bytes(4) + struct.pack('>IH', 1, 563)
changes = {c + b'TRC': trc for c in (b'r', b'g', b'b')}
for c, column in zip((b'r', b'g', b'b'), zip(*adobe_matrix)):
    changes[c + b'XYZ'] = b'XYZ ' + bytes(4) + struct.pack('>3i', *(round(v * 65536) for v in column))
adobe = retag(rgb_profile, changes)
WIDE = [(35954, 61462, 20734, 65535), (35954, 61462, 20734, 32769),
        (36000, 62000, 22000, 65535), (0, 65535, 0, 65535),
        (65535, 0, 0, 40001), (40000, 40001, 40002, 65535)]
png('precision-wide-icc.png', icc=adobe, samples=WIDE)
tiff('precision-wide-icc.tiff', icc=adobe, samples=WIDE)
tiff('precision-wide-associated-icc.tiff', icc=adobe, samples=WIDE, associated=True)

# Emit the independently derived expectations, using the actual serialized matrix
# and gamma. No codec or color-management implementation participates in this oracle.
to_srgb = matmul(inverse(srgb_matrix), adobe_matrix)
NONLINEAR = [(2, .5, .25, 1), (.123456789, .6, 1.4, .37), (-.125, .5, .75, 1),
             (2.5, -.25, 1/3, .5), (3, 2, 4, 1), (.01, .02, .03, 0)]
tiff('precision-float-parametric-icc.tiff', floating=True, icc=adobe, samples=NONLINEAR)
expected = []
for pixel in NONLINEAR:
    stored = struct.unpack('<4f', struct.pack('<4f', *pixel))
    linear = matmul(to_srgb, [[max(c, 0)**gamma] for c in stored[:3]])
    expected.append([v[0] * stored[3] for v in linear] + [stored[3]])
(ROOT / 'precision-float-parametric-expected.txt').write_text(
    ''.join(' '.join(format(v, '.17g') for v in row) + '\n' for row in expected))
for associated in (False, True):
    expected = []
    for pixel in WIDE:
        alpha = pixel[3] / 65535
        encoded = [round(c * alpha) / 65535 / alpha if associated else c / 65535 for c in pixel[:3]]
        linear = matmul(to_srgb, [[c**gamma] for c in encoded])
        expected.append([v[0] * alpha for v in linear] + [alpha])
    name = 'precision-wide-associated-expected.txt' if associated else 'precision-wide-expected.txt'
    (ROOT / name).write_text(''.join(' '.join(format(v, '.17g') for v in row) + '\n' for row in expected))

table = b'curv' + bytes(4) + struct.pack('>I17H', 17, *(round((i / 16)**2.2 * 65535) for i in range(17)))
table_rgb = retag(rgb_profile, {c + b'TRC': table for c in (b'r', b'g', b'b')})
# Valid lut16Type: identity input/output tables and an eight-vertex XYZ CLUT.
lut = b'mft2' + bytes(4) + bytes([3, 3, 2, 0])
lut += struct.pack('>9i', 65536, 0, 0, 0, 65536, 0, 0, 0, 65536)
lut += struct.pack('>HH', 2, 2) + struct.pack('>6H', 0, 65535, 0, 65535, 0, 65535)
lut += b''.join(struct.pack('>3H', r, g, b) for r in (0, 32768) for g in (0, 32768) for b in (0, 32768))
lut += struct.pack('>6H', 0, 65535, 0, 65535, 0, 65535)
hybrid = retag(rgb_profile, {b'A2B0': lut})
lut_only = retag(hybrid, {c + suffix: None for c in (b'r', b'g', b'b') for suffix in (b'TRC', b'XYZ')})
tiff('precision-float-table-icc.tiff', floating=True, icc=table_rgb)
tiff('precision-float-lut-icc.tiff', floating=True, icc=lut_only)
tiff('precision-float-hybrid-icc.tiff', floating=True, icc=hybrid)
# Integer table/LUT profiles remain supported (their input domain is bounded).
png('precision-table-icc.png', icc=table_rgb)
png('precision-lut-icc.png', icc=lut_only)
linear_rgb = profile('wide-icc.png', linear=True)
tiff('precision-float-associated-icc.tiff', floating=True, associated=True, icc=linear_rgb)
# Same associated RGB at different alpha must give identical transformed RGB bits.
tiff('precision-float-alpha-independent-icc.tiff', floating=True, associated=True, icc=linear_rgb,
     samples=[(0.123456789, -0.03125, 4.5, a) for a in (0, 0.37, 1, 0.125, 0.75, 0.00001)])
tiff('precision-float-huge-icc.tiff', floating=True, icc=linear_rgb,
     samples=[(1e30, 0.5, 0.25, 1)] * 6)
tiff('precision-float-overflow-icc.tiff', floating=True, icc=adobe,
     samples=[(1e17, 0.5, 0.25, 1)] * 6)
tiff('precision-float-nonfinite-icc.tiff', floating=True, icc=linear_rgb,
     samples=[(float('nan'), 0.5, 0.25, 1)] * 6)
EIGHT = [(1, 10, 11, 255), (80, 160, 240, 128), (255, 80, 40, 1),
         (255, 16, 240, 0), (128, 129, 130, 255), (17, 200, 250, 192)]
tiff('precision-8-associated.tiff', samples=EIGHT, depth=8, associated=True)
tiff('precision-8-associated-icc.tiff', samples=EIGHT, depth=8, associated=True, icc=rgb_profile)
tiff('precision-8-gray.tiff', samples=EIGHT, depth=8, gray=True)

for direction in (2, 3, 4, 5, 7, 8):
    tiff(f'precision-float-orientation-{direction}.tiff', floating=True,
         associated=True, orientation=direction)

# Cross all four worker partitions with a repeating six-color sequence.
WIDTH, HEIGHT = 1024, 1025
png('precision-wide-threaded-icc.png', icc=adobe,
    samples=WIDE * ((WIDTH * HEIGHT + len(WIDE) - 1) // len(WIDE)))
