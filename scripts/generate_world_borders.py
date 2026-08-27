import json
import urllib.request
import math
import os

print("Fetching world country borders GeoJSON...")
# Using a clean, highly reliable simplified world countries GeoJSON dataset
url = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_50m_admin_0_boundary_lines_land.geojson"

req = urllib.request.Request(url, headers={"User-Agent": "ESP-GlobalRadar/2.0"})
try:
    with urllib.request.urlopen(req) as resp:
        geojson = json.loads(resp.read().decode('utf-8'))
except Exception as e:
    print(f"Fallback to 110m dataset due to: {e}")
    url = "https://raw.githubusercontent.com/nvkelso/natural-earth-vector/master/geojson/ne_110m_admin_0_boundary_lines_land.geojson"
    req = urllib.request.Request(url, headers={"User-Agent": "ESP-GlobalRadar/2.0"})
    with urllib.request.urlopen(req) as resp:
        geojson = json.loads(resp.read().decode('utf-8'))

# Douglas-Peucker simplification
def point_line_dist(pt, start, end):
    if start == end:
        return math.hypot(pt[0] - start[0], pt[1] - start[1])
    n = abs((end[1] - start[1]) * pt[0] - (end[0] - start[0]) * pt[1] + end[0] * start[1] - end[1] * start[0])
    d = math.hypot(end[1] - start[1], end[0] - start[0])
    return n / d if d > 0 else 0

def simplify_dp(points, tolerance):
    if len(points) <= 2:
        return points
    max_d = 0.0
    index = 0
    for i in range(1, len(points) - 1):
        d = point_line_dist(points[i], points[0], points[-1])
        if d > max_d:
            max_d = d
            index = i
    if max_d > tolerance:
        left = simplify_dp(points[:index + 1], tolerance)
        right = simplify_dp(points[index:], tolerance)
        return left[:-1] + right
    else:
        return [points[0], points[-1]]

raw_lines = []
for feat in geojson["features"]:
    geom = feat.get("geometry")
    if not geom:
        continue
    gtype = geom["type"]
    coords = geom["coordinates"]
    if gtype == "LineString":
        raw_lines.append(coords)
    elif gtype == "MultiLineString":
        for line in coords:
            raw_lines.append(line)
    elif gtype == "Polygon":
        for ring in coords:
            raw_lines.append(ring)
    elif gtype == "MultiPolygon":
        for poly in coords:
            for ring in poly:
                raw_lines.append(ring)

print(f"Total raw lines: {len(raw_lines)}")

# Simplify lines (tolerance in degrees, ~0.08 deg = ~8 km precision)
TOLERANCE = 0.08
simplified_lines = []
for line in raw_lines:
    pts = [(p[0], p[1]) for p in line] # (lon, lat)
    simp = simplify_dp(pts, TOLERANCE)
    if len(simp) >= 2:
        simplified_lines.append(simp)

print(f"Simplified lines: {len(simplified_lines)}")

# Break lines longer than 100 points into smaller segments for better bounding box culling
MAX_SEG_PTS = 80
segments = []
for line in simplified_lines:
    for i in range(0, len(line) - 1, MAX_SEG_PTS - 1):
        chunk = line[i : i + MAX_SEG_PTS]
        if len(chunk) >= 2:
            lons = [p[0] for p in chunk]
            lats = [p[1] for p in chunk]
            min_lon = int(math.floor(min(lons) * 100))
            max_lon = int(math.ceil(max(lons) * 100))
            min_lat = int(math.floor(min(lats) * 100))
            max_lat = int(math.ceil(max(lats) * 100))
            segments.append({
                "min_lon": min_lon,
                "max_lon": max_lon,
                "min_lat": min_lat,
                "max_lat": max_lat,
                "points": [(int(round(p[1] * 100)), int(round(p[0] * 100))) for p in chunk] # (lat*100, lon*100)
            })

print(f"Final segments: {len(segments)}")
total_pts = sum(len(s["points"]) for s in segments)
print(f"Total points across all segments: {total_pts}")

header_path = os.path.join(os.path.dirname(__file__), "..", "include", "world_borders.h")

with open(header_path, "w", encoding="utf-8") as f:
    f.write("// =======================================================================================\n")
    f.write("// Auto-generated Global World Borders Dataset (Natural Earth 50m Land Boundaries)\n")
    f.write(f"// Total segments: {len(segments)}, Total vertices: {total_pts}\n")
    f.write("// Coordinates format: fixed-point int16_t (degrees * 100)\n")
    f.write("// =======================================================================================\n\n")
    f.write("#pragma once\n#include <Arduino.h>\n\n")
    
    f.write("struct BorderSegment {\n")
    f.write("  int16_t minLat;\n")
    f.write("  int16_t maxLat;\n")
    f.write("  int16_t minLon;\n")
    f.write("  int16_t maxLon;\n")
    f.write("  uint16_t pointCount;\n")
    f.write("  const int16_t (*points)[2]; // Array of {lat*100, lon*100}\n")
    f.write("};\n\n")

    for idx, seg in enumerate(segments):
        pts_str = ", ".join(f"{{{p[0]}, {p[1]}}}" for p in seg["points"])
        f.write(f"static const int16_t BORDER_PTS_{idx}[][2] PROGMEM = {{{pts_str}}};\n")

    f.write(f"\nstatic const BorderSegment WORLD_BORDER_SEGMENTS[{len(segments)}] PROGMEM = {{\n")
    for idx, seg in enumerate(segments):
        f.write(f"  {{{seg['min_lat']}, {seg['max_lat']}, {seg['min_lon']}, {seg['max_lon']}, {len(seg['points'])}, BORDER_PTS_{idx}}}")
        if idx < len(segments) - 1:
            f.write(",\n")
        else:
            f.write("\n")
    f.write("};\n\n")
    f.write(f"static constexpr size_t WORLD_BORDER_SEGMENT_COUNT = {len(segments)};\n")

print(f"Generated {header_path} successfully!")
