// ESP-Radar_types.h - Struct definitionen
// Diese Datei wird zuerst kompiliert

#pragma once

struct RouteCache {
  String callsign;
  String von;
  String nach;
};

struct Flugzeug {
  String callsign;
  float  hoehe_m, geschw_kmh, distanz_km;
  float  lat, lon;
  float  kurs;
  float  vert_rate;
  String von, nach;
  int    routeQuelle;
};

struct AirlineName {
  const char* prefix;
  const char* name;
};

struct AirportInfo {
  const char* iata;
  float lat, lon;
  float pisten[6];
};
