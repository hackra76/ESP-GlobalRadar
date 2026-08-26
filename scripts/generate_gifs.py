import math
import os
import random
from PIL import Image, ImageDraw, ImageFont

# Constants matching ESP32 C++ firmware
RADAR_IMG_W = 800
RADAR_IMG_H = 550
LAT_TOP = 50.7000
LAT_BOTTOM = 46.0500
LON_LEFT = 13.6000
LON_RIGHT = 23.7900
TFT_W = 240
TFT_H = 240

# Slovak Border polygon coordinates from main.cpp
SK_BORDER = [
    (16.96, 48.48), (16.90, 48.38), (16.85, 48.28), (16.95, 48.21), (17.06, 48.14),
    (17.11, 48.08), (17.16, 48.02), (17.40, 47.90), (17.65, 47.78), (17.87, 47.77),
    (18.10, 47.76), (18.20, 47.77), (18.30, 47.78), (18.52, 47.78), (18.75, 47.79),
    (18.78, 47.93), (18.82, 48.08), (18.91, 48.12), (19.00, 48.16), (19.41, 48.12),
    (19.82, 48.08), (19.94, 48.17), (20.07, 48.27), (20.26, 48.38), (20.45, 48.50),
    (20.53, 48.52), (20.61, 48.55), (20.83, 48.53), (21.05, 48.52), (21.36, 48.44),
    (21.68, 48.37), (21.91, 48.40), (22.15, 48.44), (22.14, 48.30), (22.14, 48.16),
    (22.35, 48.62), (22.56, 49.08), (22.47, 49.10), (22.38, 49.12), (21.84, 49.27),
    (21.31, 49.42), (21.08, 49.41), (20.85, 49.40), (20.75, 49.40), (20.65, 49.41),
    (20.37, 49.37), (20.10, 49.33), (19.84, 49.38), (19.58, 49.44), (19.40, 49.48),
    (19.22, 49.52), (19.03, 49.51), (18.84, 49.51), (18.64, 49.48), (18.45, 49.45),
    (18.25, 49.31), (18.06, 49.18), (17.95, 49.06), (17.85, 48.95), (17.73, 48.91),
    (17.62, 48.87), (17.47, 48.86), (17.32, 48.85), (17.19, 48.81), (17.07, 48.77),
    (17.01, 48.62), (16.96, 48.48)
]

CITIES = [
    ("BA", 48.1486, 17.1077, True),  ("TT", 48.3775, 17.5883, False),
    ("NR", 48.3061, 18.0864, True),  ("TN", 48.8945, 18.0444, False),
    ("ZA", 49.2231, 18.7397, True),  ("BB", 48.7363, 19.1462, True),
    ("PO", 48.9984, 21.2393, True),  ("KE", 48.7164, 21.2611, True),
    ("BJ", 49.2918, 21.2727, False), ("PP", 49.0595, 20.2978, False),
    ("MI", 48.7547, 21.9195, False), ("LC", 48.3294, 19.6648, False)
]

# Color constants (RGB)
CLR_BG = (0, 0, 0)
CLR_BORDER = (0, 255, 255) # Cyan
CLR_CITY_TEXT = (255, 165, 0) # Orange
CLR_CITY_CROSS = (255, 0, 0) # Red
CLR_GRID_GREEN = (0, 200, 0)
CLR_GRID_DIM = (0, 80, 0)
CLR_CARDINAL = (180, 220, 180)
CLR_SCALE_GREEN = (0, 255, 0)
CLR_WHITE = (255, 255, 255)
CLR_PLANE_CIVIL = (0, 130, 255)
CLR_PLANE_MIL = (255, 40, 40)
CLR_PLANE_EMG_1 = (255, 30, 30)
CLR_PLANE_EMG_2 = (255, 255, 0)
CLR_TAG_BG = (12, 16, 22)
CLR_TAG_BORDER_CIVIL = (35, 65, 100)
CLR_TAG_BORDER_MIL = (180, 40, 40)
CLR_TAG_ROUTE = (255, 130, 255)
CLR_TAG_TYPE = (90, 200, 255)
CLR_TAG_SPEED = (150, 235, 150)
CLR_TAG_ALT = (255, 255, 0)
CLR_VRATE_UP = (30, 220, 30)
CLR_VRATE_DOWN = (235, 40, 40)
CLR_EDGE_CIVIL = (255, 150, 0)
CLR_EDGE_MIL = (255, 40, 40)

# Fonts
try:
    font_path_bold = "C:\\Windows\\Fonts\\segoeuib.ttf"
    font_path_regular = "C:\\Windows\\Fonts\\segoeui.ttf"
    if not os.path.exists(font_path_bold):
        font_path_bold = "C:\\Windows\\Fonts\\arialbd.ttf"
        font_path_regular = "C:\\Windows\\Fonts\\arial.ttf"
    FONT_TAG = ImageFont.truetype(font_path_bold, 11)
    FONT_TAG_SM = ImageFont.truetype(font_path_bold, 10)
    FONT_CARDINAL = ImageFont.truetype(font_path_bold, 11)
    FONT_SCALE = ImageFont.truetype(font_path_bold, 11)
    FONT_EMG = ImageFont.truetype(font_path_bold, 11)
except Exception:
    FONT_TAG = ImageFont.load_default()
    FONT_TAG_SM = ImageFont.load_default()
    FONT_CARDINAL = ImageFont.load_default()
    FONT_SCALE = ImageFont.load_default()
    FONT_EMG = ImageFont.load_default()

def lonToX(lon):
    return round((lon - LON_LEFT) * (RADAR_IMG_W - 1) / (LON_RIGHT - LON_LEFT))

def latToY(lat):
    return round((LAT_TOP - lat) * (RADAR_IMG_H - 1) / (LAT_TOP - LAT_BOTTOM))

class CropBox:
    def __init__(self, x1, y1, x2, y2):
        self.x1 = x1
        self.y1 = y1
        self.x2 = x2
        self.y2 = y2
    def w(self):
        return self.x2 - self.x1 + 1
    def h(self):
        return self.y2 - self.y1 + 1

def makeCrop(centerLat, centerLon, radiusKm):
    degLat = radiusKm / 111.32
    degLon = radiusKm / (111.32 * math.cos(math.radians(centerLat)))
    cx = lonToX(centerLon)
    cy = latToY(centerLat)
    spanX = int(round(degLon * (RADAR_IMG_W - 1) / (LON_RIGHT - LON_LEFT)))
    spanY = int(round(degLat * (RADAR_IMG_H - 1) / (LAT_TOP - LAT_BOTTOM)))
    span = max(spanX, spanY)
    if span < 10:
        span = 10
    return CropBox(cx - span, cy - span, cx + span, cy + span)

def mapXToScreenX(mapX, crop):
    return (mapX - crop.x1) * TFT_W / crop.w()

def mapYToScreenY(mapY, crop):
    return (mapY - crop.y1) * TFT_H / crop.h()

# Synthetic radar precipitation generator (smooth dBZ blobs with SHMÚ color palette)
SHMU_PALETTE = [
    (0, 0, 0, 0),       # No rain
    (56, 142, 60, 180), # Light rain (green)
    (104, 159, 56, 200),
    (251, 192, 45, 220),# Moderate (yellow)
    (245, 124, 0, 235), # Heavy (orange)
    (211, 47, 47, 245), # Storm (red)
    (194, 24, 91, 255), # Intense hail (purple)
    (255, 255, 255, 255)# Extreme (white)
]

def generate_weather_layer(crop, t_offset=0):
    img = Image.new("RGBA", (TFT_W, TFT_H), (0, 0, 0, 0))
    draw = ImageDraw.Draw(img)
    
    # Generate realistic storm cells located over Slovakia (near Nitra/Banská Bystrica/Tatry)
    cells = [
        {"lat": 48.55 + 0.04 * math.sin(t_offset * 0.1), "lon": 18.45 + 0.05 * math.cos(t_offset * 0.1), "r_km": 32, "peak": 5},
        {"lat": 48.78 - 0.03 * math.cos(t_offset * 0.08), "lon": 19.10 + 0.06 * math.sin(t_offset * 0.08), "r_km": 42, "peak": 6},
        {"lat": 48.25 + 0.02 * math.sin(t_offset * 0.15), "lon": 17.65 + 0.03 * math.cos(t_offset * 0.15), "r_km": 24, "peak": 4},
        {"lat": 49.05, "lon": 20.15, "r_km": 36, "peak": 5}
    ]
    
    for cell in cells:
        sx = mapXToScreenX(lonToX(cell["lon"]), crop)
        sy = mapYToScreenY(latToY(cell["lat"]), crop)
        sr = (cell["r_km"] / 50.0) * (TFT_W / (2 * crop.w() / 150.0))
        
        for ring in range(cell["peak"], 0, -1):
            radius = sr * (ring / cell["peak"])
            color = SHMU_PALETTE[ring]
            draw.ellipse([sx - radius, sy - radius, sx + radius, sy + radius], fill=color)
            
    return img

def draw_sk_border(draw, crop):
    for i in range(len(SK_BORDER) - 1):
        sx1 = mapXToScreenX(lonToX(SK_BORDER[i][0]), crop)
        sy1 = mapYToScreenY(latToY(SK_BORDER[i][1]), crop)
        sx2 = mapXToScreenX(lonToX(SK_BORDER[i+1][0]), crop)
        sy2 = mapYToScreenY(latToY(SK_BORDER[i+1][1]), crop)
        draw.line([(sx1, sy1), (sx2, sy2)], fill=CLR_BORDER, width=1)

def draw_cities(draw, crop, radiusKm):
    for name, lat, lon, is_major in CITIES:
        if radiusKm >= 100.0 and not is_major:
            continue
        sx = int(round(mapXToScreenX(lonToX(lon), crop)))
        sy = int(round(mapYToScreenY(latToY(lat), crop)))
        if 8 <= sx <= TFT_W - 8 and 8 <= sy <= TFT_H - 8:
            draw.line([(sx - 2, sy), (sx + 2, sy)], fill=CLR_CITY_CROSS, width=1)
            draw.line([(sx, sy - 2), (sx, sy + 2)], fill=CLR_CITY_CROSS, width=1)
            # City text
            bbox = FONT_TAG_SM.getbbox(name)
            tw = bbox[2] - bbox[0]
            draw.text((sx - tw // 2, sy - 13), name, fill=CLR_CITY_TEXT, font=FONT_TAG_SM)

def draw_plane_grid(draw, radiusKm, system_time="14:25"):
    cx, cy = TFT_W // 2, TFT_H // 2
    # Dim crosshair
    draw.line([(cx - 110, cy), (cx + 110, cy)], fill=CLR_GRID_DIM, width=1)
    draw.line([(cx, cy - 110), (cx, cy + 110)], fill=CLR_GRID_DIM, width=1)
    # Range rings (35, 70, 105 px)
    draw.ellipse([cx - 35, cy - 35, cx + 35, cy + 35], outline=CLR_GRID_GREEN, width=1)
    draw.ellipse([cx - 70, cy - 70, cx + 70, cy + 70], outline=CLR_GRID_GREEN, width=1)
    draw.ellipse([cx - 105, cy - 105, cx + 105, cy + 105], outline=CLR_GRID_GREEN, width=1)
    # Cardinal directions (N, S, W, E)
    draw.text((cx - 4, cy - 105 + 5), "N", fill=CLR_CARDINAL, font=FONT_CARDINAL)
    draw.text((cx - 3, cy + 105 - 17), "S", fill=CLR_CARDINAL, font=FONT_CARDINAL)
    draw.text((cx - 105 + 6, cy - 7), "W", fill=CLR_CARDINAL, font=FONT_CARDINAL)
    draw.text((cx + 105 - 15, cy - 7), "E", fill=CLR_CARDINAL, font=FONT_CARDINAL)
    # Top scale
    scale_str = f"{int(radiusKm)} km"
    bbox = FONT_SCALE.getbbox(scale_str)
    tw = bbox[2] - bbox[0]
    draw.text((cx - tw // 2, 4), scale_str, fill=CLR_SCALE_GREEN, font=FONT_SCALE)
    # Bottom clock
    bbox_time = FONT_SCALE.getbbox(system_time)
    tw_time = bbox_time[2] - bbox_time[0]
    draw.text((cx - tw_time // 2, TFT_H - 18), system_time, fill=CLR_WHITE, font=FONT_SCALE)

def draw_weather_overlay_grid(draw, radiusKm, time_str="14:25"):
    cx, cy = TFT_W // 2, TFT_H // 2
    draw.ellipse([cx - 118, cy - 118, cx + 118, cy + 118], outline=(100, 100, 100), width=1)
    draw.ellipse([cx - 60, cy - 60, cx + 60, cy + 60], outline=(70, 70, 70), width=1)
    draw.line([(cx - 6, cy), (cx + 6, cy)], fill=CLR_WHITE, width=1)
    draw.line([(cx, cy - 6), (cx, cy + 6)], fill=CLR_WHITE, width=1)
    # Top scale
    scale_str = f"{int(radiusKm)} km"
    bbox = FONT_SCALE.getbbox(scale_str)
    tw = bbox[2] - bbox[0]
    draw.text((cx - tw // 2, 4), scale_str, fill=CLR_WHITE, font=FONT_SCALE)
    # Bottom radar time
    bbox_time = FONT_SCALE.getbbox(time_str)
    tw_time = bbox_time[2] - bbox_time[0]
    draw.text((cx - tw_time // 2, TFT_H - 18), time_str, fill=CLR_WHITE, font=FONT_SCALE)

def draw_aircraft_symbol(draw, x, y, heading_deg, track_deg, gs_knots, is_mil, is_emg, blink_state):
    rad_h = math.radians(heading_deg)
    sin_h = math.sin(rad_h)
    cos_h = math.cos(rad_h)
    
    kAircraftNoseLenPx = 8
    kAircraftTailLenPx = 3
    kAircraftTailHalfPx = 4
    
    tip_x = x + round(sin_h * kAircraftNoseLenPx)
    tip_y = y - round(cos_h * kAircraftNoseLenPx)
    base_x = x - round(sin_h * kAircraftTailLenPx)
    base_y = y + round(cos_h * kAircraftTailLenPx)
    wing_x = round(cos_h * kAircraftTailHalfPx)
    wing_y = round(sin_h * kAircraftTailHalfPx)
    
    # Course vector
    if gs_knots > 0:
        rad_t = math.radians(track_deg)
        vlen = max(3, min(8, int(gs_knots / 60.0)))
        ex = tip_x + round(math.sin(rad_t) * vlen)
        ey = tip_y - round(math.cos(rad_t) * vlen)
        draw.line([(tip_x, tip_y), (ex, ey)], fill=(180, 205, 230), width=1)
        
    color = CLR_PLANE_CIVIL
    if is_emg:
        color = CLR_PLANE_EMG_1 if blink_state else CLR_PLANE_EMG_2
    elif is_mil:
        color = CLR_PLANE_MIL
        
    poly = [(tip_x, tip_y), (base_x + wing_x, base_y + wing_y), (base_x - wing_x, base_y - wing_y)]
    draw.polygon(poly, fill=color)

def draw_aircraft_tag(draw, x, y, ac, blink_state):
    top_line = ac.get("route") or ac.get("callsign", "")
    type_str = ac.get("type", "")
    speed_kmh = int(round(ac.get("gs_knots", 0) * 1.852))
    line2 = f"{type_str}, {speed_kmh}" if (type_str and speed_kmh > 0) else (type_str or str(speed_kmh))
    alt_str = ac.get("alt", "")
    vrate = ac.get("vrate", 0)
    
    # Calculate widths
    b1 = FONT_TAG.getbbox(top_line)
    w1 = b1[2] - b1[0]
    b2 = FONT_TAG.getbbox(line2)
    w2 = b2[2] - b2[0]
    b3 = FONT_TAG.getbbox(alt_str)
    w3 = b3[2] - b3[0] + (10 if vrate != 0 else 0)
    block_w = max(w1, w2, w3, 24)
    line_h = 12
    block_h = line_h * 3
    
    tag_on_right = x < (TFT_W // 2)
    if tag_on_right:
        anchor_x = x + 15
        anchor_x = min(anchor_x, TFT_W - block_w - 6)
        box_x = anchor_x - 3
    else:
        anchor_x = x - 15 - block_w
        anchor_x = max(anchor_x, 4)
        box_x = anchor_x - 3
        
    ly = max(4, min(TFT_H - block_h - 4, y - block_h // 2))
    box_y = ly - 2
    box_w = block_w + 6
    box_h = block_h + 3
    
    border_col = CLR_TAG_BORDER_CIVIL
    if ac.get("is_emg"):
        border_col = CLR_PLANE_EMG_1 if blink_state else CLR_PLANE_EMG_2
    elif ac.get("is_mil"):
        border_col = CLR_TAG_BORDER_MIL
        
    # Draw rounded contrast box
    draw.rounded_rectangle([box_x, box_y, box_x + box_w, box_y + box_h], radius=3, fill=CLR_TAG_BG, outline=border_col, width=1)
    
    # Line 1: Route / Callsign
    col1 = CLR_TAG_ROUTE if ac.get("route") else CLR_WHITE
    if ac.get("is_mil"):
        col1 = (255, 60, 60)
    if ac.get("is_emg"):
        col1 = CLR_PLANE_EMG_1 if blink_state else CLR_PLANE_EMG_2
        top_line = f"🚨{ac.get('squawk', '7700')} {top_line}"
    draw.text((anchor_x, ly), top_line, fill=col1, font=FONT_TAG)
    ly += line_h
    
    # Line 2: Type (cyan) + Speed (green)
    if type_str:
        draw.text((anchor_x, ly), type_str + ",", fill=CLR_TAG_TYPE, font=FONT_TAG)
        bt = FONT_TAG.getbbox(type_str + ",")
        wt = bt[2] - bt[0] + 4
        draw.text((anchor_x + wt, ly), str(speed_kmh), fill=CLR_TAG_SPEED, font=FONT_TAG)
    else:
        draw.text((anchor_x, ly), str(speed_kmh), fill=CLR_TAG_SPEED, font=FONT_TAG)
    ly += line_h
    
    # Line 3: Altitude (yellow) + Arrow
    draw.text((anchor_x, ly), alt_str, fill=CLR_TAG_ALT, font=FONT_TAG)
    if vrate != 0:
        ba = FONT_TAG.getbbox(alt_str)
        wa = ba[2] - ba[0] + 4
        ax = anchor_x + wa
        ay = ly + 2
        if vrate > 0: # Climbing
            draw.polygon([(ax + 3, ay), (ax, ay + 6), (ax + 6, ay + 6)], fill=CLR_VRATE_UP)
        else: # Descending
            draw.polygon([(ax + 3, ay + 6), (ax, ay), (ax + 6, ay)], fill=CLR_VRATE_DOWN)

def draw_edge_indicator(draw, mapX, mapY, crop, is_mil):
    cx, cy = TFT_W // 2, TFT_H // 2
    dx = mapXToScreenX(mapX, crop) - cx
    dy = mapYToScreenY(mapY, crop) - cy
    dist = math.sqrt(dx * dx + dy * dy)
    if dist < 1.0:
        return
    angle = math.atan2(dy, dx)
    edgeX = cx + int(round(math.cos(angle) * 114.0))
    edgeY = cy + int(round(math.sin(angle) * 114.0))
    color = CLR_EDGE_MIL if is_mil else CLR_EDGE_CIVIL
    draw.ellipse([edgeX - 3, edgeY - 3, edgeX + 3, edgeY + 3], fill=color, outline=(0, 0, 0))

def create_bezel_frame(screen_img):
    """Wraps the 240x240 circular screen in an ultra-sleek dark circular device casing."""
    bezel_size = 280
    out = Image.new("RGBA", (bezel_size, bezel_size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(out)
    
    # Outer dark metal casing
    draw.ellipse([2, 2, bezel_size - 3, bezel_size - 3], fill=(24, 26, 30), outline=(50, 54, 62), width=3)
    draw.ellipse([12, 12, bezel_size - 13, bezel_size - 13], fill=(15, 17, 20), outline=(32, 35, 42), width=2)
    # Inner display bezel ring
    draw.ellipse([18, 18, bezel_size - 19, bezel_size - 19], fill=(0, 0, 0), outline=(60, 65, 75), width=2)
    
    # Circular mask for display
    mask = Image.new("L", (TFT_W, TFT_H), 0)
    mask_draw = ImageDraw.Draw(mask)
    mask_draw.ellipse([0, 0, TFT_W - 1, TFT_H - 1], fill=255)
    
    # Paste display inside
    out.paste(screen_img, (20, 20), mask)
    
    # Subtle glossy reflection on top glass
    glass = Image.new("RGBA", (bezel_size, bezel_size), (0, 0, 0, 0))
    gdraw = ImageDraw.Draw(glass)
    gdraw.arc([22, 22, bezel_size - 23, bezel_size - 23], start=210, end=330, fill=(255, 255, 255, 35), width=2)
    out = Image.alpha_composite(out, glass)
    
    return out

# =========================================================================
# GENERATORS
# =========================================================================

def generate_combined_radar_gif(output_path):
    print("Generating demo_combined_radar.gif...")
    frames = []
    centerLat = 48.6690
    centerLon = 19.6990
    radiusKm = 50.0
    crop = makeCrop(centerLat, centerLon, radiusKm)
    
    # Simulation aircraft
    planes = [
        {"lat": 48.55, "lon": 19.45, "heading": 315, "track": 315, "gs_knots": 460, "vrate": 1, "route": "VIE>AMS", "type": "A21N", "alt": "10400m", "is_mil": False, "is_emg": False, "squawk": "3421"},
        {"lat": 48.82, "lon": 19.95, "heading": 120, "track": 120, "gs_knots": 490, "vrate": 0, "route": "WAW>BGY", "type": "B738", "alt": "11200m", "is_mil": False, "is_emg": False, "squawk": "5102"},
        {"lat": 48.72, "lon": 19.30, "heading": 45, "track": 45, "gs_knots": 540, "vrate": 1, "callsign": "MIG29", "type": "MG29", "alt": "7200m", "is_mil": True, "is_emg": False, "squawk": "7000"},
        {"lat": 48.20, "lon": 19.10, "heading": 60, "track": 60, "gs_knots": 430, "vrate": -1, "route": "PRG>KSC", "type": "A320", "alt": "4800m", "is_mil": False, "is_emg": False, "squawk": "1244"}
    ]
    
    total_frames = 24
    for f in range(total_frames):
        img = Image.new("RGBA", (TFT_W, TFT_H), (0, 0, 0, 255))
        
        # Layer 1: Weather Radar (SHMÚ Clouds)
        weather_layer = generate_weather_layer(crop, f)
        img = Image.alpha_composite(img, weather_layer)
        draw = ImageDraw.Draw(img)
        
        # Layer 2: Slovak Border & Cities & Grid
        draw_sk_border(draw, crop)
        draw_cities(draw, crop, radiusKm)
        draw_plane_grid(draw, radiusKm, system_time="15:40")
        
        # Layer 3: Aircraft Dead Reckoning motion & HUD tags
        blink_state = (f % 4 < 2)
        for p in planes:
            # Extrapolate motion
            dt_s = 0.8
            dist_km = p["gs_knots"] * 1.852 * dt_s / 3600.0
            dLat = dist_km * math.cos(math.radians(p["track"])) / 111.32
            cosLat = math.cos(math.radians(p["lat"]))
            dLon = dist_km * math.sin(math.radians(p["track"])) / (111.32 * cosLat)
            p["lat"] += dLat
            p["lon"] += dLon
            
            mapX = lonToX(p["lon"])
            mapY = latToY(p["lat"])
            sx = int(round(mapXToScreenX(mapX, crop)))
            sy = int(round(mapYToScreenY(mapY, crop)))
            
            cx, cy = TFT_W // 2, TFT_H // 2
            distFromCenter = math.sqrt((sx - cx)**2 + (sy - cy)**2)
            if distFromCenter <= 106.0:
                draw_aircraft_symbol(draw, sx, sy, p["heading"], p["track"], p["gs_knots"], p["is_mil"], p["is_emg"], blink_state)
                draw_aircraft_tag(draw, sx, sy, p, blink_state)
            else:
                draw_edge_indicator(draw, mapX, mapY, crop, p["is_mil"])
                
        bezel = create_bezel_frame(img)
        frames.append(bezel)
        
    frames[0].save(output_path, save_all=True, append_images=frames[1:], duration=120, loop=0, optimize=True)
    print(f"Saved {output_path}")

def generate_plane_radar_gif(output_path):
    print("Generating demo_plane_radar.gif...")
    frames = []
    centerLat = 48.6690
    centerLon = 19.6990
    radiusKm = 50.0
    crop = makeCrop(centerLat, centerLon, radiusKm)
    
    # Airplanes with one emergency flight
    planes = [
        {"lat": 48.62, "lon": 19.60, "heading": 220, "track": 220, "gs_knots": 420, "vrate": -1, "route": "FRA>BUD", "callsign": "DLH402", "type": "A321", "alt": "3200m", "is_mil": False, "is_emg": True, "squawk": "7700"},
        {"lat": 48.50, "lon": 19.85, "heading": 300, "track": 300, "gs_knots": 480, "vrate": 1, "route": "BTS>LTN", "callsign": "WZZ612", "type": "A21N", "alt": "9800m", "is_mil": False, "is_emg": False, "squawk": "4211"},
        {"lat": 48.80, "lon": 19.40, "heading": 135, "track": 135, "gs_knots": 520, "vrate": 0, "callsign": "SVK01", "type": "F16", "alt": "6500m", "is_mil": True, "is_emg": False, "squawk": "7000"},
        {"lat": 48.95, "lon": 19.90, "heading": 80, "track": 80, "gs_knots": 450, "vrate": 0, "route": "MUC>KSC", "type": "CRJ9", "alt": "8900m", "is_mil": False, "is_emg": False, "squawk": "2214"}
    ]
    
    total_frames = 24
    for f in range(total_frames):
        img = Image.new("RGBA", (TFT_W, TFT_H), (0, 0, 0, 255))
        draw = ImageDraw.Draw(img)
        
        # Layer 1: Border & Grid
        draw_sk_border(draw, crop)
        draw_cities(draw, crop, radiusKm)
        draw_plane_grid(draw, radiusKm, system_time="14:32")
        
        # Layer 2: Planes & Emergency Banner
        blink_state = (f % 4 < 2)
        has_emg = False
        emg_info = ""
        
        for p in planes:
            dt_s = 0.8
            dist_km = p["gs_knots"] * 1.852 * dt_s / 3600.0
            dLat = dist_km * math.cos(math.radians(p["track"])) / 111.32
            cosLat = math.cos(math.radians(p["lat"]))
            dLon = dist_km * math.sin(math.radians(p["track"])) / (111.32 * cosLat)
            p["lat"] += dLat
            p["lon"] += dLon
            
            mapX = lonToX(p["lon"])
            mapY = latToY(p["lat"])
            sx = int(round(mapXToScreenX(mapX, crop)))
            sy = int(round(mapYToScreenY(mapY, crop)))
            
            cx, cy = TFT_W // 2, TFT_H // 2
            distFromCenter = math.sqrt((sx - cx)**2 + (sy - cy)**2)
            if distFromCenter <= 106.0:
                draw_aircraft_symbol(draw, sx, sy, p["heading"], p["track"], p["gs_knots"], p["is_mil"], p["is_emg"], blink_state)
                draw_aircraft_tag(draw, sx, sy, p, blink_state)
                if p["is_emg"]:
                    has_emg = True
                    emg_info = f"{p['callsign']} (SQ{p['squawk']})"
            else:
                draw_edge_indicator(draw, mapX, mapY, crop, p["is_mil"])
                
        if has_emg:
            banner_col = CLR_PLANE_EMG_1 if blink_state else CLR_PLANE_EMG_2
            emg_text = f"⚠️ NUDZA: {emg_info}"
            bbox = FONT_EMG.getbbox(emg_text)
            tw = bbox[2] - bbox[0]
            draw.text(((TFT_W - tw) // 2, 38), emg_text, fill=banner_col, font=FONT_EMG)
            
        bezel = create_bezel_frame(img)
        frames.append(bezel)
        
    frames[0].save(output_path, save_all=True, append_images=frames[1:], duration=120, loop=0, optimize=True)
    print(f"Saved {output_path}")

def generate_weather_zoom_gif(output_path):
    print("Generating demo_weather_zoom.gif...")
    frames = []
    centerLat = 48.6690
    centerLon = 19.6990
    zoom_levels = [250.0, 100.0, 50.0, 25.0, 10.0]
    
    for zoom in zoom_levels:
        crop = makeCrop(centerLat, centerLon, zoom)
        for sub in range(5):
            img = Image.new("RGBA", (TFT_W, TFT_H), (0, 0, 0, 255))
            weather_layer = generate_weather_layer(crop, sub)
            img = Image.alpha_composite(img, weather_layer)
            draw = ImageDraw.Draw(img)
            
            draw_sk_border(draw, crop)
            draw_cities(draw, crop, zoom)
            draw_weather_overlay_grid(draw, zoom, time_str="14:35")
            
            bezel = create_bezel_frame(img)
            frames.append(bezel)
            
    frames[0].save(output_path, save_all=True, append_images=frames[1:], duration=350, loop=0, optimize=True)
    print(f"Saved {output_path}")

def generate_carousel_modes_gif(output_path):
    print("Generating demo_modes_carousel.gif...")
    frames = []
    centerLat = 48.6690
    centerLon = 19.6990
    radiusKm = 50.0
    crop = makeCrop(centerLat, centerLon, radiusKm)
    
    planes = [
        {"lat": 48.60, "lon": 19.50, "heading": 320, "track": 320, "gs_knots": 460, "vrate": 1, "route": "VIE>AMS", "type": "A21N", "alt": "10400m", "is_mil": False, "is_emg": False, "squawk": "3421"},
        {"lat": 48.75, "lon": 19.80, "heading": 130, "track": 130, "gs_knots": 490, "vrate": -1, "route": "WAW>BGY", "type": "B738", "alt": "8200m", "is_mil": False, "is_emg": False, "squawk": "5102"},
    ]
    
    # 3 Modes: 0=Combined, 1=Weather, 2=Planes
    modes = [
        ("COMBINED ATC RADAR", 0),
        ("SHMÚ METEORADAR", 1),
        ("ADS-B FLIGHT RADAR", 2)
    ]
    
    for mode_name, mode_idx in modes:
        for f in range(8):
            img = Image.new("RGBA", (TFT_W, TFT_H), (0, 0, 0, 255))
            draw = ImageDraw.Draw(img)
            
            # Weather layer
            if mode_idx in (0, 1):
                weather_layer = generate_weather_layer(crop, f)
                img = Image.alpha_composite(img, weather_layer)
                draw = ImageDraw.Draw(img)
                
            draw_sk_border(draw, crop)
            draw_cities(draw, crop, radiusKm)
            
            if mode_idx in (0, 2):
                draw_plane_grid(draw, radiusKm, system_time="14:40")
                blink_state = (f % 4 < 2)
                for p in planes:
                    sx = int(round(mapXToScreenX(lonToX(p["lon"]), crop)))
                    sy = int(round(mapYToScreenY(latToY(p["lat"]), crop)))
                    draw_aircraft_symbol(draw, sx, sy, p["heading"], p["track"], p["gs_knots"], p["is_mil"], p["is_emg"], blink_state)
                    draw_aircraft_tag(draw, sx, sy, p, blink_state)
            else:
                draw_weather_overlay_grid(draw, radiusKm, time_str="14:40")
                
            bezel = create_bezel_frame(img)
            frames.append(bezel)
            
    frames[0].save(output_path, save_all=True, append_images=frames[1:], duration=220, loop=0, optimize=True)
    print(f"Saved {output_path}")

if __name__ == "__main__":
    os.makedirs("data", exist_ok=True)
    generate_combined_radar_gif("data/demo_combined_radar.gif")
    generate_plane_radar_gif("data/demo_plane_radar.gif")
    generate_weather_zoom_gif("data/demo_weather_zoom.gif")
    generate_carousel_modes_gif("data/demo_modes_carousel.gif")
    print("All GIFs generated successfully!")
