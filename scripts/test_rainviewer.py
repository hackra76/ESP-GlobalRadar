import urllib.request
import json
import math

req = urllib.request.Request('https://api.rainviewer.com/public/weather-maps.json', headers={'User-Agent': 'ESP-GlobalRadar/2.0'})
with urllib.request.urlopen(req) as resp:
    data = json.loads(resp.read().decode('utf-8'))

host = data['host']
past = data['radar']['past']
latest = past[-1]
print(f"Host: {host}")
print(f"Latest timestamp: {latest['time']}, path: {latest['path']}")

# Let's test tile calculation for lat=48.6690 (Banska Bystrica / Central Slovakia)
lat = 48.6690
lon = 19.6990

for z in [5, 6, 7, 8]:
    n = 2 ** z
    x = int((lon + 180.0) / 360.0 * n)
    lat_rad = math.radians(lat)
    y = int((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n)
    
    # Exact sub-tile pixel offsets
    px = ((lon + 180.0) / 360.0 * n - x) * 256.0
    py = ((1.0 - math.asinh(math.tan(lat_rad)) / math.pi) / 2.0 * n - y) * 256.0
    
    tile_url = f"{host}{latest['path']}/256/{z}/{x}/{y}/2/1_1.png"
    print(f"Zoom {z}: x={x}, y={y}, px={px:.1f}, py={py:.1f} -> {tile_url}")
    try:
        t_req = urllib.request.Request(tile_url, headers={'User-Agent': 'ESP-GlobalRadar/2.0'})
        with urllib.request.urlopen(t_req) as t_resp:
            t_data = t_resp.read()
            print(f"  -> HTTP {t_resp.status}, Size: {len(t_data)} bytes")
    except Exception as e:
        print(f"  -> Error: {e}")
