#pragma once
#include <Arduino.h>

struct City {
  const char* name;
  float lat;
  float lon;
  bool isMajor; // true: shown on all zooms >= 100km, false: shown on <= 50km
};

static const City CITIES[] PROGMEM = {
  // === Slovakia ===
  {"BA", 48.1486f, 17.1077f, true},   {"KE", 48.7164f, 21.2611f, true},
  {"ZA", 49.2231f, 18.7397f, true},   {"BB", 48.7363f, 19.1462f, true},
  {"NR", 48.3061f, 18.0864f, true},   {"TT", 48.3775f, 17.5883f, false},
  {"TN", 48.8945f, 18.0444f, false},  {"PO", 48.9984f, 21.2393f, true},
  {"PP", 49.0595f, 20.2978f, false},  {"ZV", 48.5760f, 19.1260f, false},
  {"MT", 49.0667f, 18.9167f, false},  {"PD", 48.7733f, 18.6250f, false},
  {"LM", 49.0833f, 19.6167f, false},  {"SNV", 48.9436f, 20.5614f, false},
  {"MI", 48.7544f, 21.9194f, false},  {"HE", 48.9372f, 21.9069f, false},
  {"KN", 47.7636f, 18.1275f, false},  {"LV", 48.2156f, 18.6078f, false},
  {"NZ", 47.9856f, 18.1611f, false},  {"DS", 47.9944f, 17.6167f, false},
  {"GA", 48.1900f, 17.7278f, false},  {"MA", 48.4361f, 17.0219f, false},
  {"SI", 48.8444f, 17.2256f, false},  {"SE", 48.6792f, 17.3667f, false},
  {"CA", 49.4389f, 18.7917f, false},  {"DK", 49.2064f, 19.2986f, false},
  {"NO", 49.4000f, 19.4833f, false},  {"TS", 49.3603f, 19.5539f, false},
  {"BR", 48.8042f, 19.6417f, false},  {"RS", 48.3828f, 20.0222f, false},
  {"RA", 48.5917f, 20.1306f, false},  {"RV", 48.6575f, 20.5336f, false},
  {"VK", 48.2083f, 19.3417f, false},  {"BS", 48.4586f, 18.8931f, false},
  {"ZC", 48.5344f, 18.7611f, false},  {"ZH", 48.5833f, 18.8500f, false},
  {"BJ", 49.2917f, 21.2750f, false},  {"SB", 49.1039f, 21.1008f, false},
  {"LE", 49.0217f, 20.5908f, false},  {"KK", 49.1367f, 20.4300f, false},
  {"SL", 49.3014f, 20.6892f, false},  {"SK", 49.2986f, 21.5658f, false},
  {"SP", 49.2028f, 21.6500f, false},  {"VT", 48.8872f, 21.6842f, false},
  {"SV", 48.9833f, 22.2333f, false},  {"SO", 48.7167f, 22.1833f, false},
  {"GL", 48.8556f, 20.9333f, false},

  // === Czech Republic ===
  {"PRG", 50.0755f, 14.4378f, true},  {"BRN", 49.1951f, 16.6068f, true},
  {"OST", 49.8209f, 18.2625f, true},  {"PLZ", 49.7384f, 13.3736f, true},
  {"LIB", 50.7663f, 15.0543f, false}, {"OLO", 49.5938f, 17.2509f, true},
  {"CB",  48.9745f, 14.4743f, true},  {"HK",  50.2104f, 15.8252f, false},
  {"PCE", 50.0343f, 15.7812f, false}, {"ZLN", 49.2245f, 17.6627f, false},
  {"UHL", 49.0698f, 17.4597f, false}, {"KAR", 50.2319f, 12.8720f, false},
  {"TEP", 50.6404f, 13.8245f, false}, {"DEC", 50.7822f, 14.2148f, false},
  {"JIH", 49.3961f, 15.5912f, false}, {"TAB", 49.4144f, 14.6578f, false},

  // === Austria ===
  {"VIE", 48.2082f, 16.3738f, true},  {"GRZ", 47.0707f, 15.4395f, true},
  {"LNZ", 48.3069f, 14.2858f, true},  {"SZG", 47.8095f, 13.0550f, true},
  {"INN", 47.2692f, 11.4041f, true},  {"KLU", 46.6247f, 14.3053f, false},
  {"STP", 48.2044f, 15.6229f, false}, {"WNS", 47.8153f, 16.2465f, false},

  // === Hungary ===
  {"BUD", 47.4979f, 19.0402f, true},  {"DEB", 47.5316f, 21.6273f, true},
  {"SZE", 46.2530f, 20.1414f, true},  {"MIS", 48.1035f, 20.7784f, true},
  {"PEC", 46.0727f, 18.2323f, true},  {"GYR", 47.6875f, 17.6504f, true},
  {"NYI", 47.9554f, 21.7167f, false}, {"SOP", 47.6850f, 16.5905f, false},
  {"SFE", 47.1899f, 18.4107f, false}, {"KEC", 46.9069f, 19.6894f, false},

  // === Poland ===
  {"WAW", 52.2297f, 21.0122f, true},  {"KRK", 50.0647f, 19.9450f, true},
  {"WRO", 51.1079f, 17.0385f, true},  {"POZ", 52.4064f, 16.9252f, true},
  {"GDA", 54.3520f, 18.6466f, true},  {"KTW", 50.2649f, 19.0238f, true},
  {"RZE", 50.0412f, 21.9991f, true},  {"LBL", 51.2465f, 22.5684f, true},
  {"SZZ", 53.4285f, 14.5528f, true},  {"BIA", 53.1325f, 23.1688f, false},

  // === United Kingdom & Ireland ===
  {"LON", 51.5074f, -0.1278f, true},  {"MAN", 53.4808f, -2.2426f, true},
  {"BHX", 52.4862f, -1.8904f, true},  {"EDI", 55.9533f, -3.1883f, true},
  {"GLA", 55.8642f, -4.2518f, true},  {"LPL", 53.4084f, -2.9916f, true},
  {"BRS", 51.4545f, -2.5879f, true},  {"NCL", 54.9783f, -1.6178f, true},
  {"SOU", 50.9097f, -1.4044f, false}, {"LBA", 53.8008f, -1.5491f, false},
  {"NWI", 52.6309f, 1.2974f, false},  {"OXF", 51.7520f, -1.2577f, false},
  {"CAM", 52.2053f, 0.1218f, false},  {"EXT", 50.7184f, -3.5339f, false},
  {"PLY", 50.3755f, -4.1427f, false}, {"ABZ", 57.1497f, -2.0943f, false},
  {"CWL", 51.4816f, -3.1791f, true},  {"BFS", 54.5973f, -5.9301f, true},
  {"DUB", 53.3498f, -6.2603f, true},  {"ORK", 51.8985f, -8.4756f, false},

  // === Germany ===
  {"BER", 52.5200f, 13.4050f, true},  {"FRA", 50.1109f, 8.6821f, true},
  {"MUC", 48.1351f, 11.5820f, true},  {"HAM", 53.5511f, 9.9937f, true},
  {"CGN", 50.9375f, 6.9603f, true},   {"DUS", 51.2277f, 6.7735f, true},
  {"STR", 48.7758f, 9.1829f, true},   {"LEJ", 51.3397f, 12.3731f, true},
  {"NUE", 49.4521f, 11.0767f, true},  {"BRE", 53.0793f, 8.8017f, false},
  {"HAJ", 52.3759f, 9.7320f, false},  {"DRS", 51.0504f, 13.7373f, true},

  // === France, Benelux, Switzerland ===
  {"PAR", 48.8566f, 2.3522f, true},   {"LYS", 45.7640f, 4.8357f, true},
  {"MRS", 43.2965f, 5.3698f, true},   {"NCE", 43.7102f, 7.2620f, true},
  {"TLS", 43.6047f, 1.4442f, true},   {"BOD", 44.8378f, -0.5792f, true},
  {"LIL", 50.6292f, 3.0573f, true},   {"SXB", 48.5734f, 7.7521f, true},
  {"AMS", 52.3676f, 4.9041f, true},   {"RTM", 51.9244f, 4.4777f, true},
  {"BRU", 50.8503f, 4.3517f, true},   {"ANR", 51.2194f, 4.4025f, false},
  {"LUX", 49.8153f, 6.1296f, true},   {"ZUR", 47.3769f, 8.5417f, true},
  {"GEN", 46.2044f, 6.1432f, true},   {"BSL", 47.5596f, 7.5886f, true},

  // === Southern & Northern Europe ===
  {"ROM", 41.9028f, 12.4964f, true},  {"MIL", 45.4642f, 9.1900f, true},
  {"NAP", 40.8518f, 14.2681f, true},  {"TRN", 45.0703f, 7.6869f, true},
  {"VCE", 45.4408f, 12.3155f, true},  {"BLQ", 44.4949f, 11.3426f, true},
  {"FLR", 43.7696f, 11.2558f, true},  {"MAD", 40.4168f, -3.7038f, true},
  {"BCN", 41.3879f, 2.1686f, true},   {"VLC", 39.4699f, -0.3763f, true},
  {"SVQ", 37.3891f, -5.9845f, true},  {"AGP", 36.7213f, -4.4214f, false},
  {"LIS", 38.7223f, -9.1393f, true},  {"OPO", 41.1579f, -8.6291f, true},
  {"CPH", 55.6761f, 12.5683f, true},  {"STO", 59.3293f, 18.0686f, true},
  {"OSL", 59.9139f, 10.7522f, true},  {"HEL", 60.1699f, 24.9384f, true},
  {"ATH", 37.9838f, 23.7275f, true},  {"IST", 41.0082f, 28.9784f, true},
  {"SOF", 42.6977f, 23.3219f, true},  {"OTP", 44.4268f, 26.1025f, true},
  {"BEG", 44.7866f, 20.4489f, true},  {"ZAG", 45.8150f, 15.9819f, true},
  {"LJU", 46.0569f, 14.5058f, true},  {"SJJ", 43.8563f, 18.4131f, true},
  {"KBP", 50.4501f, 30.5234f, true},

  // === Global Hubs ===
  {"NYC", 40.7128f, -74.0060f, true}, {"LAX", 34.0522f, -118.2437f, true},
  {"ORD", 41.8781f, -87.6298f, true}, {"MIA", 25.7617f, -80.1918f, true},
  {"DXB", 25.2048f, 55.2708f, true},  {"DOH", 25.2854f, 51.5310f, true},
  {"TYO", 35.6762f, 139.6503f, true}, {"SIN", 1.3521f, 103.8198f, true},
  {"SYD", -33.8688f, 151.2093f, true},{"HKG", 22.3193f, 114.1694f, true}
};

static constexpr size_t CITY_COUNT = sizeof(CITIES) / sizeof(CITIES[0]);
