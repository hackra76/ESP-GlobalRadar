import urllib.request
import struct
import zlib

url = "https://tilecache.rainviewer.com/v2/radar/63b17d5d304d/256/7/71/44/2/1_1.png"
req = urllib.request.Request(url, headers={'User-Agent': 'ESP-GlobalRadar/2.0'})
with urllib.request.urlopen(req) as resp:
    data = resp.read()

print("PNG total length:", len(data))
# PNG header: 8 bytes
idx = 8
while idx < len(data):
    length, ctype = struct.unpack(">I4s", data[idx:idx+8])
    ctype = ctype.decode('latin1')
    chunk_data = data[idx+8:idx+8+length]
    crc = data[idx+8+length:idx+12+length]
    print(f"Chunk: {ctype}, length: {length}")
    if ctype == "IHDR":
        w, h, bit_depth, color_type, comp, filt, interlace = struct.unpack(">IIBBBBB", chunk_data)
        print(f"  IHDR: {w}x{h}, bit_depth={bit_depth}, color_type={color_type} (3=Palette, 6=RGBA, 2=RGB), interlace={interlace}")
    elif ctype == "PLTE":
        print(f"  PLTE palette entries: {length // 3}")
    elif ctype == "tRNS":
        print(f"  tRNS transparency entries: {length}")
    idx += 12 + length
