import math

def simulate_mapping(centerLat, centerLon, radiusKm, z, png_colored_pixels):
    n = 2**z
    wx = (centerLon + 180.0) / 360.0 * n
    latRad = math.radians(centerLat)
    wy = (1.0 - math.asinh(math.tan(latRad)) / math.pi) / 2.0 * n
    tx = int(math.floor(wx))
    ty = int(math.floor(wy))
    px = (wx - tx) * 256.0
    py = (wy - ty) * 256.0
    
    m_per_tile_px = (40075017.0 * math.cos(latRad)) / (256.0 * (1 << z))
    m_per_screen_px = (radiusKm * 1000.0) / 120.0
    tile_scale = m_per_tile_px / m_per_screen_px
    
    screen_colored = []
    for sy in range(240):
        dY_px = sy - 120.0
        for sx in range(240):
            dX_px = sx - 120.0
            if dX_px*dX_px + dY_px*dY_px > 14400:
                continue
            t_x = int(round(px + dX_px / tile_scale))
            t_y = int(round(py + dY_px / tile_scale))
            if (t_x, t_y) in png_colored_pixels:
                screen_colored.append((sx, sy, t_x, t_y))
    print(f"Radius {radiusKm} km (z={z}): {len(screen_colored)} pixels on round 240x240 screen!")

# Test with our decompressed pixels
import urllib.request, zlib, struct
url_z7 = "https://tilecache.rainviewer.com/v2/radar/cb850b8703d9/256/7/71/44/2/1_1.png"
req = urllib.request.Request(url_z7, headers={"User-Agent": "ESP-GlobalRadar/2.0"})
data = urllib.request.urlopen(req).read()
pos = 8
idat = bytearray()
while pos < len(data):
    length, chunk_type = struct.unpack(">I4s", data[pos:pos+8])
    pos += 8
    chunk_data = data[pos:pos+length]
    pos += length + 4
    if chunk_type == b'IDAT': idat.extend(chunk_data)
decomp = zlib.decompress(idat)

px_set_z7 = set()
for y in range(256):
    line_start = y * (1 + 256 * 4) + 1
    for x in range(256):
        r, g, b, a = decomp[line_start + x*4 : line_start + x*4 + 4]
        if a > 0: px_set_z7.add((x, y))

for rad in [25, 50, 100]:
    simulate_mapping(48.6690, 19.6990, rad, 7, px_set_z7)

# Now check z=6 (250 km)
url_z6 = "https://tilecache.rainviewer.com/v2/radar/cb850b8703d9/256/6/35/22/2/1_1.png"
req = urllib.request.Request(url_z6, headers={"User-Agent": "ESP-GlobalRadar/2.0"})
data = urllib.request.urlopen(req).read()
pos = 8
idat = bytearray()
while pos < len(data):
    length, chunk_type = struct.unpack(">I4s", data[pos:pos+8])
    pos += 8
    chunk_data = data[pos:pos+length]
    pos += length + 4
    if chunk_type == b'IDAT': idat.extend(chunk_data)
decomp6 = zlib.decompress(idat)
px_set_z6 = set()
for y in range(256):
    line_start = y * (1 + 256 * 4) + 1
    for x in range(256):
        r, g, b, a = decomp6[line_start + x*4 : line_start + x*4 + 4]
        if a > 0: px_set_z6.add((x, y))

simulate_mapping(48.6690, 19.6990, 250, 6, px_set_z6)

# Now check z=5 (500 km)
url_z5 = "https://tilecache.rainviewer.com/v2/radar/cb850b8703d9/256/5/17/11/2/1_1.png"
req = urllib.request.Request(url_z5, headers={"User-Agent": "ESP-GlobalRadar/2.0"})
data = urllib.request.urlopen(req).read()
pos = 8
idat = bytearray()
while pos < len(data):
    length, chunk_type = struct.unpack(">I4s", data[pos:pos+8])
    pos += 8
    chunk_data = data[pos:pos+length]
    pos += length + 4
    if chunk_type == b'IDAT': idat.extend(chunk_data)
decomp5 = zlib.decompress(idat)
px_set_z5 = set()
for y in range(256):
    line_start = y * (1 + 256 * 4) + 1
    for x in range(256):
        r, g, b, a = decomp5[line_start + x*4 : line_start + x*4 + 4]
        if a > 0: px_set_z5.add((x, y))

simulate_mapping(48.6690, 19.6990, 500, 5, px_set_z5)
