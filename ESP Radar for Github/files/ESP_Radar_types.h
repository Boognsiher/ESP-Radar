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
  String von, nach;
  bool   neueRoute;
  int    routeQuelle;
};

struct ZrhFlug {
  String callsign;
  String von;
  String nach;
};

struct AirlineName {
  const char* prefix;
  const char* name;
};
