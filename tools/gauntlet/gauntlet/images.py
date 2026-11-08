"""Screenshots: read a PNG, judge it sane, compare two with a tolerance.

read_png(path) decodes 8-bit greyscale, RGB, RGBA and palette PNGs without PIL (zlib only);
sanity(path) reports the size and whether the image is a single colour;
compare(a, b) returns the mean absolute difference over all channels and the 99.5th percentile
of the per-pixel largest channel difference, the two numbers UnrealDC's golden.py gates on
(mean <= 2.0, p99.5 <= 32). PIL is used when it is installed, the pure reader when it is not.
"""
import struct
import zlib

MEAN_LIMIT = 2.0
P995_LIMIT = 32.0

try:
    from PIL import Image
except ImportError:
    Image = None


def _paeth(a, b, c):
    p = a + b - c
    pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
    if pa <= pb and pa <= pc:
        return a
    return b if pb <= pc else c


def _unfilter(data, width, height, channels):
    stride = width * channels
    rows = []
    previous = bytearray(stride)
    offset = 0
    for _ in range(height):
        kind = data[offset]
        offset += 1
        row = bytearray(data[offset:offset + stride])
        offset += stride
        if kind == 1:
            for i in range(channels, stride):
                row[i] = (row[i] + row[i - channels]) & 255
        elif kind == 2:
            for i in range(stride):
                row[i] = (row[i] + previous[i]) & 255
        elif kind == 3:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                row[i] = (row[i] + ((left + previous[i]) >> 1)) & 255
        elif kind == 4:
            for i in range(stride):
                left = row[i - channels] if i >= channels else 0
                upleft = previous[i - channels] if i >= channels else 0
                row[i] = (row[i] + _paeth(left, previous[i], upleft)) & 255
        elif kind != 0:
            raise ValueError("bad PNG filter %d" % kind)
        rows.append(bytes(row))
        previous = row
    return rows


def read_png(path):
    with open(path, "rb") as f:
        blob = f.read()
    if blob[:8] != b"\x89PNG\r\n\x1a\n":
        raise ValueError("%s is not a PNG" % path)
    offset = 8
    width = height = 0
    depth = colour = interlace = 0
    palette = None
    idat = []
    while offset < len(blob):
        length, kind = struct.unpack(">I4s", blob[offset:offset + 8])
        body = blob[offset + 8:offset + 8 + length]
        offset += 12 + length
        if kind == b"IHDR":
            width, height, depth, colour, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif kind == b"PLTE":
            palette = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif kind == b"IDAT":
            idat.append(body)
        elif kind == b"IEND":
            break
    if depth != 8 or interlace:
        raise ValueError("%s: only 8-bit non-interlaced PNGs are read here" % path)
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[colour]
    rows = _unfilter(zlib.decompress(b"".join(idat)), width, height, channels)
    rgb = []
    for row in rows:
        if colour == 2:
            rgb.append(row)
        elif colour == 6:
            rgb.append(bytes(v for i, v in enumerate(row) if i % 4 != 3))
        elif colour == 0:
            rgb.append(bytes(v for v in row for _ in range(3)))
        elif colour == 4:
            rgb.append(bytes(row[i] for i in range(0, len(row), 2) for _ in range(3)))
        else:
            rgb.append(bytes(c for index in row for c in palette[index]))
    return width, height, rgb


def _load_rgb(path):
    if Image is not None:
        image = Image.open(path).convert("RGB")
        width, height = image.size
        raw = image.tobytes()
        return width, height, [raw[y * width * 3:(y + 1) * width * 3] for y in range(height)]
    return read_png(path)


def sanity(path):
    try:
        width, height, rows = _load_rgb(path)
    except (OSError, ValueError, KeyError) as e:
        return {"ok": False, "reason": str(e)}
    if width == 0 or height == 0:
        return {"ok": False, "reason": "empty image", "width": width, "height": height}
    colours = set()
    for row in rows[::max(1, height // 64)]:
        for x in range(0, len(row), 3 * max(1, width // 64)):
            colours.add(row[x:x + 3])
            if len(colours) > 64:
                break
        if len(colours) > 64:
            break
    single = len(colours) == 1
    return {"ok": not single, "width": width, "height": height, "distinct_colours_sampled": len(colours),
            "single_colour": single, "reason": "a single colour" if single else None}


def compare(a, b):
    wa, ha, rows_a = _load_rgb(a)
    wb, hb, rows_b = _load_rgb(b)
    if (wa, ha) != (wb, hb):
        return {"same_size": False, "size_a": [wa, ha], "size_b": [wb, hb], "mean": None, "p99.5": None,
                "pass": False}
    histogram = [0] * 256
    total = 0
    for row_a, row_b in zip(rows_a, rows_b):
        if row_a == row_b:
            histogram[0] += wa
            continue
        for x in range(0, wa * 3, 3):
            d0 = abs(row_a[x] - row_b[x])
            d1 = abs(row_a[x + 1] - row_b[x + 1])
            d2 = abs(row_a[x + 2] - row_b[x + 2])
            total += d0 + d1 + d2
            histogram[max(d0, d1, d2)] += 1
    pixels = wa * ha
    mean = total / (pixels * 3)
    threshold = pixels * 0.995
    seen = 0
    p995 = 0
    for value, count in enumerate(histogram):
        seen += count
        if seen >= threshold:
            p995 = value
            break
    ok = mean <= MEAN_LIMIT and p995 <= P995_LIMIT
    return {"same_size": True, "mean": round(mean, 3), "p99.5": float(p995), "pass": ok,
            "identical": total == 0 and histogram[0] == pixels}


def write_png(path, width, height, rows):
    raw = b"".join(b"\x00" + row for row in rows)

    def chunk(kind, body):
        return struct.pack(">I", len(body)) + kind + body + struct.pack(">I", zlib.crc32(kind + body) & 0xffffffff)
    with open(path, "wb") as f:
        f.write(b"\x89PNG\r\n\x1a\n")
        f.write(chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)))
        f.write(chunk(b"IDAT", zlib.compress(raw)))
        f.write(chunk(b"IEND", b""))
