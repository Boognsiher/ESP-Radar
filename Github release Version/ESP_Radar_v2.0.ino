#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <WebServer.h>
#include <TFT_eSPI.h>
#include "ESP_Radar_types.h"
#include "ESP_Radar_logo.h"

// ============================================
// NUR DIES ANPASSEN (Fallback-Werte falls Flash leer)
// ============================================
const char* WLAN_LIST[][2] = {
  {"YourSSID1",   "YourPassword1"},
  {"YourSSID2",   "YourPassword2"},
  {"",              ""},
  {"",              ""},
  {"",              ""},
};
// ============================================

// Konfigurierbare Credentials (aus Flash geladen)
String cfg_fa_key      = "";        // FlightAware API Key
String cfg_osk_client_id  = "";       // OpenSky OAuth2 Client ID
String cfg_osk_client_secret = "";    // OpenSky OAuth2 Client Secret
String cfg_airport     = "ZRH";    // Heimflughafen IATA Code

// WLAN-Liste (aus Flash geladen - per Web-UI aenderbar)
String wlan_ssid[5];
String wlan_pass[5];

// Konfiguration (aus Flash geladen - per Web-UI aenderbar)
float cfg_lat         = 47.3769;
float cfg_lon         =  8.5417;
float cfg_radius      = 50.0;
int   cfg_credits_tot = 100;
int   cfg_daten_sek   = 30;   // Globales Daten-Intervall (Hintergrund-Timer)
bool  cfg_fa_aktiv    = false; // FlightAware Standard = OFF
String cfg_track_cs   = "";   // Flug verfolgen: Callsign (leer = deaktiviert)

WebServer webServer(80);

// Globaler Hintergrund-Timer fuer Datenabruf
unsigned long datenTimerStart = 0;
int standbyZaehler = 5;  // Anzahl automatischer Datenabrufe bis Standby (0 = kein Auto-Laden)
#define LAT       cfg_lat
#define LON       cfg_lon
#define RADIUS_KM cfg_radius

int fa_credits_used  = 0;
int fa_credits_total = 100;
TFT_eSPI tft = TFT_eSPI();
bool tftReady = false;

#define LOG_LINES 20
String logBuf[LOG_LINES];
int    logIdx = 0;
void addLog(String s){ logBuf[logIdx % LOG_LINES] = s; logIdx++; }

#define MAX_CACHE 1000
RouteCache cache[MAX_CACHE];
int cacheAnzahl = 0;

Flugzeug letzteList[20];
int      letzteAnzahl = 0;
int      spotStartIdx = -1;  // globaler Index des startenden Fliegers
int      spotLandIdx  = -1;  // globaler Index des landenden Fliegers

int stat_fa_heute  = 0;
unsigned long bootTime = 0;
String verbundenesWlan = "";

// ============================================================
// FLUGHAFEN-DATENBANK: Koordinaten + Pistenrichtungen
// ============================================================
const AirportInfo FLUGHAFEN_DB[] = {
  {"ZRH", 47.4647, 8.5492,  {140.0, 280.0, 340.0, -1}},
  {"GVA", 46.2381, 6.1089,  {230.0,  50.0, -1}},
  {"BSL", 47.5896, 7.5300,  {152.0, 340.0, -1}},
  {"BRN", 46.9141, 7.4977,  {140.0, 320.0, -1}},
  {"SIR", 46.2196, 7.3268,  {60.0,  -1}},
  {"LHR", 51.4775,-0.4614,  {90.0,  270.0, -1}},
  {"CDG", 49.0097, 2.5479,  {90.0,  130.0, -1}},
  {"AMS", 52.3086, 4.7639,  {60.0,  180.0, 240.0, 270.0, -1}},
  {"FRA", 50.0333, 8.5706,  {70.0,  180.0, -1}},
  {"MUC", 48.3537,11.7750,  {90.0,  -1}},
  {"VIE", 48.1102,16.5697,  {110.0, 160.0, -1}},
  {"FCO", 41.8003,12.2389,  {160.0, 70.0,  -1}},
  {"MAD", 40.4719,-3.5626,  {180.0, 140.0, 320.0, -1}},
  {"BCN", 41.2971, 2.0785,  {70.0,  200.0, -1}},
  {"BRU", 50.9010, 4.4844,  {50.0,  130.0, 270.0, -1}},
  {"CPH", 55.6181,12.6561,  {40.0,  130.0, -1}},
  {"OSL", 60.1939,11.1004,  {10.0,  190.0, -1}},
  {"ARN", 59.6519,17.9186,  {10.0,  80.0,  -1}},
  {"HEL", 60.3172,24.9633,  {40.0,  130.0, 320.0, -1}},
  {"WAW", 52.1657,20.9671,  {110.0, 330.0, -1}},
  {"PRG", 50.1008,14.2600,  {50.0,  130.0, -1}},
  {"BUD", 47.4369,19.2556,  {130.0, 310.0, -1}},
  {"ATH", 37.9364,23.9445,  {30.0,  210.0, -1}},
  {"LIS", 38.7756,-9.1354,  {30.0,  210.0, -1}},
  {"DUB", 53.4213,-6.2700,  {110.0, 280.0, -1}},
};
const int FLUGHAFEN_DB_COUNT = sizeof(FLUGHAFEN_DB)/sizeof(FLUGHAFEN_DB[0]);
const char* FACTS[] = {
  "Ein A380 wiegt 575 Tonnen",
  "Piloten essen verschiedene Mahlzeiten",
  "Reisehoehe: ~11km ueber Meer",
  "Luft im Cockpit alle 3 Min. erneuert",
  "ZRH: ~300 Starts pro Tag",
  "Kruemmung der Erde erfordert Ausgleich",
  "Triebwerk saugt 1.2t Luft/Sek.",
  "Schwarze Box ist orange",
  "Blitze treffen Flugzeuge 1-2x/Jahr",
  "Kerosin gefriert bei -47 Grad"
};
const int FACTS_COUNT = sizeof(FACTS)/sizeof(FACTS[0]);

void clearScreen(){
  tft.fillScreen(TFT_BLACK);
  if(!tftReady) return;
  tft.startWrite();
  tft.writecommand(0x2B);
  tft.writedata(0x00); tft.writedata(0x50);
  tft.writedata(0x01); tft.writedata(0x3F);
  tft.endWrite();
}

// Logo aus 1-Bit Bitmap zeichnen (weiss auf schwarz)
void zeichneLogo(int x, int y, int scale=1){
  for(int row=0; row<LOGO_H; row++){
    for(int col=0; col<LOGO_W; col++){
      int byteIdx = row * LOGO_BYTES_PER_ROW + (col / 8);
      int bitIdx  = 7 - (col % 8);
      if(pgm_read_byte(&LOGO_BMP[byteIdx]) & (1 << bitIdx)){
        if(scale == 1) tft.drawPixel(x + col, y + row, TFT_WHITE);
        else tft.fillRect(x + col*scale, y + row*scale, scale, scale, TFT_WHITE);
      }
    }
  }
}

// Navigationsleiste unten: ‹ LINKS · MITTE · RECHTS ›
void zeichneNavBar(const char* links, const char* mitte, const char* rechts){
  tft.fillRect(0, 214, 240, 14, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(0x333333);
  tft.setCursor(2, 221); tft.print("<"); tft.print(links);
  tft.setTextColor(0x666666);
  int mw = strlen(mitte) * 6;
  tft.setCursor(120 - mw/2, 221); tft.print(mitte);
  tft.setTextColor(0x333333);
  int rw = (strlen(rechts) + 1) * 6;
  tft.setCursor(238 - rw, 221); tft.print(rechts); tft.print(">");
}

// ============================================================
// HILFSFUNKTIONEN
// ============================================================
float gradToRad(float g){ return g*PI/180.0; }

float berechnDistanz(float la1,float lo1,float la2,float lo2){
  float R=6371.0,dL=gradToRad(la2-la1),dO=gradToRad(lo2-lo1);
  float a=sin(dL/2)*sin(dL/2)+cos(gradToRad(la1))*cos(gradToRad(la2))*sin(dO/2)*sin(dO/2);
  return R*2*atan2(sqrt(a),sqrt(1-a));
}

bool sucheCache(String cs,String &v,String &n){
  for(int i=0;i<cacheAnzahl;i++)
    if(cache[i].callsign==cs){v=cache[i].von;n=cache[i].nach;return true;}
  return false;
}

String uptimeStr(){
  unsigned long s=(millis()-bootTime)/1000;
  return String(s/3600)+"h "+String((s%3600)/60)+"m "+String(s%60)+"s";
}

// ============================================================
// MULTI-WLAN: verbindet mit dem staerksten bekannten Netz
// ============================================================
bool verbindeWlan(){
  tft.fillRect(0,130,240,20,TFT_BLACK);
  tft.setTextColor(TFT_WHITE); tft.setCursor(8,130);
  tft.print("Scanne WLANs...");
  int n=WiFi.scanNetworks();
  if(n<=0) return false;

  int bestIdx=-1, bestRssi=-999;
  for(int i=0;i<n;i++){
    for(int k=0;k<5;k++){
      if(wlan_ssid[k].length()==0) continue;
      if(WiFi.SSID(i)==wlan_ssid[k] && WiFi.RSSI(i)>bestRssi){
        bestRssi=WiFi.RSSI(i); bestIdx=k;
      }
    }
  }
  WiFi.scanDelete();
  if(bestIdx<0) return false;

  tft.fillRect(0,130,240,20,TFT_BLACK);
  tft.setTextColor(TFT_WHITE); tft.setCursor(8,130);
  tft.print("Verbinde: "); tft.print(wlan_ssid[bestIdx]);

  WiFi.begin(wlan_ssid[bestIdx].c_str(), wlan_pass[bestIdx].c_str());
  int v=0;
  while(WiFi.status()!=WL_CONNECTED && v<20){ delay(500); v++; }
  if(WiFi.status()==WL_CONNECTED){
    verbundenesWlan=wlan_ssid[bestIdx];
    addLog("[WLAN] "+verbundenesWlan+" ("+String(bestRssi)+" dBm)");
    return true;
  }
  return false;
}

// ============================================================
// STANDORT per IP-Geolokalisierung (ip-api.com - kostenlos)
// Genauigkeit ~1-5 km. Wird beim Start einmalig aufgerufen.
// Kann per Web-UI jederzeit manuell ueberschrieben werden.
// ============================================================
bool bestimmeStandort(){
  HTTPClient http;
  http.begin("http://ip-api.com/json/?fields=lat,lon,city,status");
  http.setTimeout(8000);
  if(http.GET()!=200){ http.end(); return false; }
  StaticJsonDocument<256> doc;
  if(deserializeJson(doc,http.getStream())){ http.end(); return false; }
  http.end();
  if(String(doc["status"].as<const char*>())!="success") return false;
  float la=doc["lat"].as<float>(), lo=doc["lon"].as<float>();
  if(la==0 && lo==0) return false;
  cfg_lat=la; cfg_lon=lo;
  Preferences konfigPrefs;
  konfigPrefs.begin("esp_radar", false);
  konfigPrefs.putFloat("lat",cfg_lat);
  konfigPrefs.putFloat("lon",cfg_lon);
  konfigPrefs.end();
  addLog("[GEO] "+String(doc["city"].as<String>())+" ("+String(la,3)+","+String(lo,3)+")");
  return true;
}


// ============================================================
// FLIGHTAWARE (nur wenn Cache + ZRH nichts liefern)
// ============================================================
bool holeRouteFlightAware(String cs,String &v,String &n){
  if(fa_credits_total-fa_credits_used<=0){
    return false;
  }
  HTTPClient http;
  http.begin("https://aeroapi.flightaware.com/aeroapi/flights/"+cs);
  http.addHeader("x-apikey", cfg_fa_key);
  http.setTimeout(8000);
  int faCode = http.GET();
  if(faCode!=200){ http.end(); return false; }
  String faPayload = http.getString();
  http.end();
  DynamicJsonDocument doc(16384);
  DeserializationError ferr=deserializeJson(doc,faPayload);
  JsonArray fl=doc["flights"];
  if(fl.isNull()||fl.size()==0) return false;
  v=fl[0]["origin"]["code_iata"].as<String>();
  n=fl[0]["destination"]["code_iata"].as<String>();
  fa_credits_used++;
  Preferences konfigPrefs;
  konfigPrefs.begin("esp_radar", false);
  konfigPrefs.putInt("fa_used",fa_credits_used);
  konfigPrefs.end();
  addLog("[FA] "+cs+" -> "+v+">"+n+" (-1 Cr)");
  return (v.length()>0 && n.length()>0);
}

// OpenSky Flights API - kostenlos, nutzt icao24
// ============================================================
// OPENSKY OAUTH2 TOKEN MANAGEMENT
// ============================================================
String osk_access_token = "";
unsigned long osk_token_expires = 0;  // millis() wann Token abläuft

bool holeOskToken(){
  if(cfg_osk_client_id.length()==0 || cfg_osk_client_secret.length()==0) return false;
  HTTPClient http;
  http.begin("https://auth.opensky-network.org/auth/realms/opensky-network/protocol/openid-connect/token");
  http.setTimeout(10000);
  http.addHeader("Content-Type","application/x-www-form-urlencoded");
  String body = "grant_type=client_credentials&client_id=" + cfg_osk_client_id + "&client_secret=" + cfg_osk_client_secret;
  int code = http.POST(body);
  if(code != 200){ http.end(); addLog("[OSK] Token Fehler: "+String(code)); return false; }
  String payload = http.getString();
  http.end();
  StaticJsonDocument<512> doc;
  if(deserializeJson(doc, payload)) return false;
  osk_access_token = doc["access_token"].as<String>();
  int expires_in = doc["expires_in"].as<int>();  // typisch 1800s (30min)
  osk_token_expires = millis() + (unsigned long)(expires_in - 60) * 1000;  // 1min Puffer
  addLog("[OSK] Token OK, gueltig "+String(expires_in)+"s");
  return osk_access_token.length() > 0;
}

bool pruefeOskToken(){
  if(osk_access_token.length()==0 || millis() > osk_token_expires)
    return holeOskToken();
  return true;
}

bool holeRouteOpenSky(String icao24, String cs, String &v, String &n){
  if(icao24.length()==0) return false;
  if(!pruefeOskToken()) return false;
  icao24.toLowerCase();
  unsigned long now = millis()/1000 + 1700000000;
  unsigned long begin = now - 7200;
  String url = "https://opensky-network.org/api/flights/aircraft?icao24="+icao24+"&begin="+String(begin)+"&end="+String(now);
  HTTPClient http;
  http.begin(url);
  http.setTimeout(8000);
  http.addHeader("Authorization", "Bearer " + osk_access_token);
  int code = http.GET();
  if(code == 401){ osk_access_token = ""; http.end(); return false; }  // Token abgelaufen
  if(code != 200){ http.end(); return false; }
  String payload = http.getString();
  http.end();
  if(payload.length()<10) return false;
  DynamicJsonDocument doc(4096);
  if(deserializeJson(doc,payload)) return false;
  JsonArray arr = doc.as<JsonArray>();
  if(arr.isNull()||arr.size()==0) return false;
  JsonObject flight = arr[arr.size()-1];
  String dep = flight["estDepartureAirport"].as<String>();
  String arr2 = flight["estArrivalAirport"].as<String>();
  dep.trim(); arr2.trim();
  if(dep.length()<3 || arr2.length()<3) return false;
  v = dep; n = arr2;
  addLog("[OSK] "+cs+" -> "+v+">"+n);
  return true;
}

// icao24 Lookup Cache (RAM only, nicht persistent)
String icao24Cache[20];
String callsignCache[20];

void speichereIcao24(String cs, String icao24){
  for(int i=0;i<20;i++){
    if(callsignCache[i]==cs) return; // bereits vorhanden
    if(callsignCache[i].length()==0){
      callsignCache[i]=cs;
      icao24Cache[i]=icao24;
      return;
    }
  }
}

String holeIcao24(String cs){
  for(int i=0;i<20;i++)
    if(callsignCache[i]==cs) return icao24Cache[i];
  return "";
}

int stat_osk_heute = 0;

int holeRoute(String cs, String icao24, String &v, String &n){
  if(sucheCache(cs,v,n))            return 1;
  if(holeRouteOpenSky(icao24,cs,v,n)){ speichereCache(cs,v,n); stat_osk_heute++; return 4; }
  if(cfg_fa_aktiv && holeRouteFlightAware(cs,v,n)){ speichereCache(cs,v,n); stat_fa_heute++; return 3; }
  return 0;
}

// Fake-Route basierend auf vertical_rate und Distanz zu ZRH
void ergaenzeFakeRoute(Flugzeug &f){
  if(f.von.length()>0 && f.nach.length()>0) return;
  if(f.hoehe_m > 4000) return;
  if(f.distanz_km > 20) return;
  if(f.vert_rate > 1.0){
    f.von = cfg_airport; f.nach = "?"; f.routeQuelle = 5;
  } else if(f.vert_rate < -1.0){
    f.von = "?"; f.nach = cfg_airport; f.routeQuelle = 5;
  }
}

// Pistenlinien für einen Flughafen zeichnen
// fx/fy = Pixel-Position des Flughafens auf dem Radar
// scale = Pixel pro km
// laenge_km = Laenge der Pistenlinie in km (je Richtung)
void zeichnePistenLinien(int fx, int fy, float scale, float laenge_km, const float* pisten){
  int px_halb = (int)(laenge_km * scale);
  for(int p=0; p<6; p++){
    if(pisten[p] < 0) break;
    float rad = gradToRad(pisten[p]);
    float dx = sin(rad), dy = -cos(rad);
    // Beide Richtungen ab Flughafen-Mittelpunkt
    int x1 = fx - (int)(dx * px_halb);
    int y1 = fy + (int)(dy * px_halb);
    int x2 = fx + (int)(dx * px_halb);
    int y2 = fy - (int)(dy * px_halb);
    // Gestrichelt zeichnen
    float len = sqrt((float)((x2-x1)*(x2-x1)+(y2-y1)*(y2-y1)));
    if(len < 1) continue;
    float stepx = (x2-x1)/len;
    float stepy = (y2-y1)/len;
    for(float d=0; d<len; d+=12){
      int sx = x1 + (int)(stepx*d);
      int sy = y1 + (int)(stepy*d);
      int ex = x1 + (int)(stepx*min(d+8, len));
      int ey = y1 + (int)(stepy*min(d+8, len));
      tft.drawLine(sx,sy,ex,ey,0x1A2A1A);
    }
  }
}

// Flughafen in DB suchen
const AirportInfo* sucheFlughafen(const String& iata){
  for(int i=0; i<FLUGHAFEN_DB_COUNT; i++)
    if(String(FLUGHAFEN_DB[i].iata) == iata) return &FLUGHAFEN_DB[i];
  return nullptr;
}


void zeichneHeader(int nF, const char* modus){
  tft.fillRect(0,0,240,24,TFT_NAVY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);  tft.setCursor(4,12);  tft.print("ESP-Radar");
  tft.setTextColor(0x9999FF);  tft.setCursor(108,12);
  tft.print(nF); tft.print(" Flieger");
  // Timer rechts oben (ersetzt modus-Text)
  unsigned long datenMs = (unsigned long)cfg_daten_sek * 1000;
  unsigned long elapsed = millis() - datenTimerStart;
  int sl = (int)((datenMs - min(elapsed, datenMs)) / 1000) + 1;
  tft.setTextColor(0x334433); tft.setCursor(196,12);
  tft.print(sl); tft.print("s");
}

#define SEITEN_MS      15000UL   // 15s pro Listenseite
#define LIST_PRO_SEITE 3

// Zeilenhöhe: 55px pro Eintrag
// Layout pro Eintrag:
//   y+0  .. y+17  Zeile 1: Callsign (textSize 2) + Route (textSize 2)
//   y+20 .. y+30  Zeile 2: Distanz / Höhe / Speed / Airline (textSize 1)
//   y+38 .. y+54  Trennlinie
#define LIST_ZEILE_H 65

void zeigeListeSeite(Flugzeug liste[],int anzahl,int seite){
  clearScreen();
  int seiten = max(1, (anzahl + LIST_PRO_SEITE - 1) / LIST_PRO_SEITE);
  if(seiten > 1){
    char modBuf[8]; sprintf(modBuf,"L%d/%d",seite+1,seiten);
    zeichneHeader(anzahl, modBuf);
  } else {
    zeichneHeader(anzahl,"LIST");
  }
  int y=34;
  int von = seite * LIST_PRO_SEITE;
  int bis = min(anzahl, von + LIST_PRO_SEITE);
  for(int i=von;i<bis;i++){
    Flugzeug &f=liste[i];
    int row=i-von;
    tft.fillRect(0,y,240,LIST_ZEILE_H-2,TFT_BLACK);

    // --- Zeile 1: Callsign (links, gross) + Route (rechts, gross) ---
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE); tft.setCursor(4,y+4);
    tft.print(f.callsign.substring(0,7));

    if(f.von.length()>0 && f.nach.length()>0){
      uint16_t rc=f.routeQuelle==1?TFT_GREEN:f.routeQuelle==3?TFT_YELLOW:f.routeQuelle==4?0x00CCFF:f.routeQuelle==5?0x555555:0xAAAA;
      tft.setTextColor(rc); tft.setCursor(132,y+4);
      tft.print(f.von.substring(0,3));
      tft.setTextColor(0x3333); tft.print(">");
      tft.setTextColor(rc);    tft.print(f.nach.substring(0,3));
    } else {
      tft.setTextColor(0x2222); tft.setCursor(186,y+4); tft.print("---");
    }

// --- Zeile 2: Airline ---
    tft.setTextSize(1);
    tft.setTextColor(0x335533); tft.setCursor(4,y+26);
    tft.print(holeAirlineName(f.callsign).substring(0,10));

    // --- Zeile 3: Distanz | Höhe | Speed | vert_rate ---
    tft.setTextColor(0x6688FF); tft.setCursor(4,y+40);
    tft.print((int)f.distanz_km); tft.print("km");

    tft.setTextColor(0x44CCAA); tft.setCursor(60,y+40);
    tft.print(String(f.hoehe_m/1000.0,1)); tft.print("km");

    tft.setTextColor(0x555555); tft.setCursor(110,y+40);
    tft.print((int)f.geschw_kmh); tft.print("kh");

    if(f.vert_rate > 0.5){
      tft.setTextColor(0x00CC44); tft.setCursor(160,y+40);
      tft.print("^+"); tft.print(String(f.vert_rate,1)); tft.print("m/s");
    } else if(f.vert_rate < -0.5){
      tft.setTextColor(0xCC4400); tft.setCursor(160,y+40);
      tft.print("v"); tft.print(String(f.vert_rate,1)); tft.print("m/s");
    }

    // Trennlinie
    tft.drawFastHLine(0,y+LIST_ZEILE_H-2,240,0x111122);
    y+=LIST_ZEILE_H;
  }
}

void zeigeRadar(Flugzeug liste[],int anzahl){
  clearScreen();
  zeichneHeader(anzahl,"RDR");
  int cx=120,cy=124,cr=88;
  // Distanzringe
  for(int r=1;r<=3;r++) tft.drawCircle(cx,cy,(int)(cr*r/3.0),0x112244);
  // Ringbeschriftungen
  tft.setTextSize(1); tft.setTextColor(0x223344);
  tft.setCursor(cx+3, cy-(int)(cr*1/3.0)-8); 
  tft.print(String((int)(RADIUS_KM/3))+"km");
  tft.setCursor(cx+3, cy-(int)(cr*2/3.0)-8); 
  tft.print(String((int)(RADIUS_KM*2/3))+"km");
  tft.setCursor(cx+3, cy-cr-8); 
  tft.print(String((int)RADIUS_KM)+"km");
  // Himmelsrichtungen
  tft.setTextColor(0x222244); tft.setTextSize(1);
  tft.setCursor(cx-3,cy-cr-2); tft.print("N");
  tft.setCursor(cx+cr+2,cy-3); tft.print("O");
  tft.setCursor(cx-3,cy+cr+2); tft.print("S");
  tft.setCursor(cx-cr-8,cy-3); tft.print("W");
  // Pistenlinien: Flughafen in DB suchen, Position berechnen
  const AirportInfo* fh = sucheFlughafen(cfg_airport);
  if(fh){
    float scale = (float)cr / RADIUS_KM;
    float dLon = (fh->lon - LON) * cos(gradToRad(LAT));
    float dLat = fh->lat - LAT;
    int fx = cx + (int)(dLon * 111.0 * scale);
    int fy = cy - (int)(dLat * 111.0 * scale);
    zeichnePistenLinien(fx, fy, scale, 5.0, fh->pisten);  // 5km je Richtung = 10km total
  }
  // Mittelpunkt (du)
  tft.fillCircle(cx,cy,4,TFT_BLUE);
  tft.setTextColor(0x2255FF); tft.setCursor(cx+6,cy-3); tft.print("DU");
  // Flugzeuge
  for(int i=0;i<anzahl;i++){
    if(liste[i].hoehe_m < 500) continue;
    float dLat=liste[i].lat-LAT;
    float dLon=(liste[i].lon-LON)*cos(gradToRad(LAT));
    float dKm=berechnDistanz(LAT,LON,liste[i].lat,liste[i].lon);
    if(dKm>RADIUS_KM) continue;
    float scale=(float)cr/RADIUS_KM;
    int px=cx+(int)(dLon*111.0*scale);
    int py=cy-(int)(dLat*111.0*scale);
    px=constrain(px,5,234); py=constrain(py,22,228);
    // Farbe nach Hoehe: Gruen=<3000m, Gelb=3000-8000m, Rot=>8000m
    uint16_t col = liste[i].hoehe_m < 3000 ? TFT_GREEN :
                   liste[i].hoehe_m < 8000 ? TFT_YELLOW : TFT_RED;
    // Richtungsstrich (8px in Flugrichtung)
    float krad = gradToRad(liste[i].kurs);
    int lx = px + (int)(sin(krad)*14);
    int ly = py - (int)(cos(krad)*14);
    tft.drawLine(px, py, lx, ly, col);
    // Punkt
    tft.fillCircle(px,py,3,col);
    // Callsign
    tft.setTextColor(liste[i].hoehe_m < 3000 ? 0x226622 :
                     liste[i].hoehe_m < 8000 ? 0x666600 : 0x662222);
    tft.setCursor(px+5,py-3);
    tft.print(liste[i].callsign.substring(0,6));
  }
}

// ============================================================
// AIRLINE LOOKUP: ICAO-Prefix -> Klarname
// ============================================================
const AirlineName AIRLINES[] = {
  {"SWR", "Swiss"},
  {"EZY", "easyJet"},
  {"DLH", "Lufthansa"},
  {"BAW", "British Airways"},
  {"AFR", "Air France"},
  {"KLM", "KLM"},
  {"AUA", "Austrian"},
  {"IBE", "Iberia"},
  {"VLG", "Vueling"},
  {"RYR", "Ryanair"},
  {"TOM", "TUI"},
  {"UAE", "Emirates"},
  {"QTR", "Qatar Airways"},
  {"ETH", "Ethiopian"},
  {"THY", "Turkish Airlines"},
  {"AZA", "ITA Airways"},
  {"TAP", "TAP Portugal"},
  {"SAS", "Scandinavian"},
  {"FIN", "Finnair"},
  {"CSN", "China Southern"},
  {"CCA", "Air China"},
  {"AAL", "American Airlines"},
  {"UAL", "United Airlines"},
  {"DAL", "Delta Air Lines"},
  {"WZZ", "Wizz Air"},
  {"BTI", "airBaltic"},
  {"LOT", "LOT Polish"},
  {"BEL", "Brussels Airlines"},
  {"LGL", "Luxair"},
  {"BER", "Air Berlin"},
  {"HOP", "HOP! Air France"},
  {"GWI", "Germanwings"},
  {"EWG", "Eurowings"},
  {"CFG", "Condor"},
  {"SVR", "Smart Wings"},
  {"EDW", "Edelweiss"},
};
const int AIRLINES_COUNT = sizeof(AIRLINES)/sizeof(AIRLINES[0]);

String holeAirlineName(String callsign){
  callsign.trim(); callsign.toUpperCase();
  for(int i=0;i<AIRLINES_COUNT;i++){
    if(callsign.startsWith(AIRLINES[i].prefix))
      return String(AIRLINES[i].name);
  }
  // Fallback: ersten 3 Buchstaben als Prefix zeigen
  if(callsign.length()>=3) return callsign.substring(0,3);
  return "";
}

// ============================================================
// SPOTLIGHT: Gerade gestarteter Flieger gross anzeigen
// Auswahl: niedrigste Hoehe UND hoechste Geschwindigkeit -> Score
// ============================================================
void zeigeSpotlight(Flugzeug liste[], int anzahl){
  // Besten startenden Flieger finden (niedrig + schnell + von ZRH)
  int startIdx = -1;
  float startScore = -1;
  // Besten landenden Flieger finden (niedrig + langsam + nach ZRH)
  int landIdx = -1;
  float landScore = -1;

  // Grenzwerte: nur Flieger die wirklich starten oder landen
  // Start: 100-4000m Hoehe, >200 km/h, Route von ZRH
  // Land:  100-4000m Hoehe, <350 km/h, Route nach ZRH
  for(int i=0;i<anzahl;i++){
    float h   = liste[i].hoehe_m;
    float spd = liste[i].geschw_kmh;
    if(h < 100 || h > 4000) continue;
    if(spd <= 0) continue;
    bool vonZRH   = liste[i].von  == cfg_airport;
    bool nachZRH  = liste[i].nach == cfg_airport;
    bool hatRoute = liste[i].von.length()>0 && liste[i].nach.length()>0;
    if(!hatRoute) continue;
    if(vonZRH && spd > 200){
      float score = spd / (h + 1.0);
      if(score > startScore){ startScore = score; startIdx = i; }
    }
    if(nachZRH && spd < 350){
      float score = 1.0 / (h + 1.0);
      if(score > landScore){ landScore = score; landIdx = i; }
    }
  }

  // Globale Indices speichern fuer SpotlightRadar
  spotStartIdx = startIdx;
  spotLandIdx  = landIdx;

  clearScreen();
  zeichneHeader(anzahl, "SPT");

  // Trennlinie Mitte
  tft.drawFastHLine(0, 115, 240, 0x112244);

  // ---- OBERE HÄLFTE: STARTEND ----
  tft.setTextColor(0x0066FF); tft.setTextSize(1);
  tft.setCursor(4, 33); tft.print("START");

  if(startIdx >= 0){
    Flugzeug &f = liste[startIdx];
    String airlineName = holeAirlineName(f.callsign);
    // Callsign
    tft.setTextSize(2); tft.setTextColor(TFT_WHITE);
    tft.setCursor(50, 31); tft.print(f.callsign.substring(0,8));
    // Airline + vert_rate
    tft.setTextSize(1); tft.setTextColor(0x4488CC);
    tft.setCursor(50, 50); tft.print(airlineName);
    tft.setTextSize(2);
    if(f.vert_rate > 0.5){ tft.setTextColor(0x00CC44); tft.setCursor(130,44); tft.print("^+"); tft.print(String(f.vert_rate,1)); tft.print("m/s"); }
    else if(f.vert_rate < -0.5){ tft.setTextColor(0xCC4400); tft.setCursor(130,44); tft.print("v"); tft.print(String(f.vert_rate,1)); tft.print("m/s"); }
    tft.setTextSize(1);
    // Route
    if(f.von.length()>0 && f.nach.length()>0){
      uint16_t rc=f.routeQuelle==2?TFT_GREEN:f.routeQuelle==3?TFT_YELLOW:0x888800;
      tft.setTextSize(2);
      tft.setTextColor(rc);
      tft.setCursor(50, 62); tft.print(f.von+" > "+f.nach);
    }
    // Details
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE); tft.setCursor(4,88); tft.print(String(f.hoehe_m/1000.0,1)); tft.print("km");
    tft.setTextColor(TFT_WHITE); tft.setCursor(80,88); tft.print((int)f.distanz_km); tft.print("km");
    tft.setTextColor(TFT_WHITE); tft.setCursor(160,88); tft.print((int)f.geschw_kmh);tft.print("kh");
  } else {
    tft.setTextColor(0x333333); tft.setTextSize(2);
    tft.setCursor(50, 70); tft.print("Kein Start erkannt");
  }

  // ---- UNTERE HÄLFTE: LANDEND ----
  tft.setTextColor(0x00CC44); tft.setTextSize(1);
  tft.setCursor(4, 122); tft.print("LAND");

  if(landIdx >= 0){
    Flugzeug &f = liste[landIdx];
    String airlineName = holeAirlineName(f.callsign);
    // Callsign
    tft.setTextSize(2); tft.setTextColor(TFT_WHITE);
    tft.setCursor(50, 120); tft.print(f.callsign.substring(0,8));
    // Airline + vert_rate
    tft.setTextSize(1); tft.setTextColor(0x4488CC);
    tft.setCursor(50, 139); tft.print(airlineName);
    tft.setTextSize(2);
    if(f.vert_rate > 0.5){ tft.setTextColor(0x00CC44); tft.setCursor(130,133); tft.print("^+"); tft.print(String(f.vert_rate,1)); tft.print("m/s"); }
    else if(f.vert_rate < -0.5){ tft.setTextColor(0xCC4400); tft.setCursor(130,133); tft.print("v"); tft.print(String(f.vert_rate,1)); tft.print("m/s"); }
   tft.setTextSize(1);
    // Route
    if(f.von.length()>0 && f.nach.length()>0){
      uint16_t rc=f.routeQuelle==2?TFT_GREEN:f.routeQuelle==3?TFT_YELLOW:0x888800;
      tft.setTextSize(2);
      tft.setTextColor(rc);
      tft.setCursor(50, 151); tft.print(f.von+" > "+f.nach);
    }
    // Details
    tft.setTextSize(2);
    tft.setTextColor(TFT_WHITE); tft.setCursor(4,177); tft.print(String(f.hoehe_m/1000.0,1)); tft.print("km");
    tft.setTextColor(TFT_WHITE); tft.setCursor(80,177); tft.print((int)f.distanz_km); tft.print("km");
    tft.setTextColor(TFT_WHITE); tft.setCursor(160,177); tft.print((int)f.geschw_kmh);tft.print("kh");
  } else {
    tft.setTextColor(0x333333); tft.setTextSize(2);
    tft.setCursor(50, 158); tft.print("Keine Landung erkannt");
  }

  addLog("[SPT] Start:"+String(startIdx>=0?liste[startIdx].callsign:"---")+" Land:"+String(landIdx>=0?liste[landIdx].callsign:"---"));
}

// ============================================================
// SPOTLIGHT-RADAR: 20km Radius, nur Start/Land Flieger
// Wird nur angezeigt wenn mind. 1 Spotlight-Flieger vorhanden
// ============================================================
#define SPOT_RADAR_KM 20.0

void zeigeSpotlightRadar(Flugzeug liste[], int anzahl){
  clearScreen();
  zeichneHeader(anzahl,"APR");
  int cx=120, cy=124, cr=88;
  // Distanzringe (5km, 10km, 20km)
  for(int r=1;r<=3;r++) tft.drawCircle(cx,cy,(int)(cr*r/3.0),0x112244);
  tft.setTextSize(1); tft.setTextColor(0x223344);
  tft.setCursor(cx+3, cy-(int)(cr*1/3.0)-8); tft.print("7km");
  tft.setCursor(cx+3, cy-(int)(cr*2/3.0)-8); tft.print("13km");
  tft.setCursor(cx+3, cy-cr-8);              tft.print("20km");
  // Himmelsrichtungen
  tft.setTextColor(0x222244); tft.setTextSize(1);
  tft.setCursor(cx-3,cy-cr-2); tft.print("N");
  tft.setCursor(cx+cr+2,cy-3); tft.print("O");
  tft.setCursor(cx-3,cy+cr+2); tft.print("S");
  tft.setCursor(cx-cr-8,cy-3); tft.print("W");
  // Pistenlinien: Flughafen in DB suchen
  const AirportInfo* fh2 = sucheFlughafen(cfg_airport);
  if(fh2){
    float scale2 = (float)cr / SPOT_RADAR_KM;
    float dLon2 = (fh2->lon - LON) * cos(gradToRad(LAT));
    float dLat2 = fh2->lat - LAT;
    int fx2 = cx + (int)(dLon2 * 111.0 * scale2);
    int fy2 = cy - (int)(dLat2 * 111.0 * scale2);
    zeichnePistenLinien(fx2, fy2, scale2, 5.0, fh2->pisten);
  }
  // Mittelpunkt
  tft.fillCircle(cx,cy,4,TFT_BLUE);
  tft.setTextColor(0x2255FF); tft.setCursor(cx+6,cy-3); tft.print(cfg_airport);
  // Nur die zwei Spotlight-Flieger zeichnen
  int indices[2] = {spotStartIdx, spotLandIdx};
  for(int s=0;s<2;s++){
    int i = indices[s];
    if(i < 0 || i >= anzahl) continue;
    float dKm = berechnDistanz(LAT,LON,liste[i].lat,liste[i].lon);
    if(dKm > SPOT_RADAR_KM) continue;
    float scale = (float)cr / SPOT_RADAR_KM;
    float dLon = (liste[i].lon-LON)*cos(gradToRad(LAT));
    float dLat = liste[i].lat-LAT;
    int px = cx+(int)(dLon*111.0*scale);
    int py = cy-(int)(dLat*111.0*scale);
    px=constrain(px,5,234); py=constrain(py,22,228);
    // Farbe nach Hoehe
    uint16_t col = liste[i].hoehe_m < 3000 ? TFT_GREEN :
                   liste[i].hoehe_m < 8000 ? TFT_YELLOW : TFT_RED;
    // Richtungsstrich
    float krad = gradToRad(liste[i].kurs);
    int lx = px + (int)(sin(krad)*10);
    int ly = py - (int)(cos(krad)*10);
    tft.drawLine(px, py, lx, ly, col);
    // Punkt
    tft.fillCircle(px,py,5,col);
    // Callsign + Hoehe
    tft.setTextColor(col); tft.setCursor(px+7,py-4);
    tft.print(liste[i].callsign.substring(0,7));
    tft.setTextColor(0x444444); tft.setCursor(px+7,py+5);
    tft.print(String(liste[i].hoehe_m/1000.0,1)); tft.print("k");
  }
}

// ============================================================
// FLUG VERFOLGEN: Einzelner Callsign gross anzeigen
// Screen 4 - wird uebersprungen wenn cfg_track_cs leer
// ============================================================
void zeigeTrack(Flugzeug liste[], int anzahl){
  clearScreen();
  zeichneHeader(anzahl, "TRK");

  if(cfg_track_cs.length() == 0){
    tft.setTextColor(0x333333); tft.setTextSize(2);
    tft.setCursor(20,110); tft.print("Kein Flug");
    tft.setCursor(20,135); tft.print("eingestellt");
    return;
  }

  // Flug in Liste suchen
  int idx = -1;
  for(int i=0;i<anzahl;i++){
    String cs = liste[i].callsign; cs.trim(); cs.toUpperCase();
    if(cs == cfg_track_cs){ idx = i; break; }
  }

  tft.setTextSize(1); tft.setTextColor(0x445566);
  tft.setCursor(4,33); tft.print("Verfolge: "); tft.print(cfg_track_cs);

  if(idx < 0){
    tft.setTextColor(0x553333); tft.setTextSize(2);
    tft.setCursor(20,90);  tft.print(cfg_track_cs);
    tft.setTextColor(0x333333); tft.setTextSize(1);
    tft.setCursor(20,118); tft.print("Nicht im Empfangsbereich");
    tft.setCursor(20,134); tft.print("oder ausserhalb Radius");
    return;
  }

  Flugzeug &f = liste[idx];
  String airlineName = holeAirlineName(f.callsign);

  // Callsign gross
  tft.setTextSize(3); tft.setTextColor(TFT_WHITE);
  tft.setCursor(4,50); tft.print(f.callsign.substring(0,8));

  // Airline
  tft.setTextSize(1); tft.setTextColor(0x4488CC);
  tft.setCursor(4,86); tft.print(airlineName);

  // Route
  if(f.von.length()>0 && f.nach.length()>0){
    uint16_t rc = f.routeQuelle==1?TFT_GREEN:f.routeQuelle==3?TFT_YELLOW:f.routeQuelle==4?0x00CCFF:0x888800;
    tft.setTextSize(2); tft.setTextColor(rc);
    tft.setCursor(4,100); tft.print(f.von+" > "+f.nach);
  } else {
    tft.setTextColor(0x333333); tft.setTextSize(2);
    tft.setCursor(4,100); tft.print("Route unbekannt");
  }

  // Trennlinie
  tft.drawFastHLine(0,122,240,0x112244);

  // Details Zeile 1: Hoehe + Speed
  tft.setTextSize(2); tft.setTextColor(0x44CCAA);
  tft.setCursor(4,132); tft.print(String(f.hoehe_m/1000.0,1)); tft.print("km");
  tft.setTextColor(0x5566AA);
  tft.setCursor(120,132); tft.print((int)f.geschw_kmh); tft.print("kh");

  // Details Zeile 2: Distanz + vert_rate
  tft.setTextColor(0x6688FF);
  tft.setCursor(4,158); tft.print((int)f.distanz_km); tft.print("km");
  if(f.vert_rate > 0.5){
    tft.setTextColor(0x00CC44); tft.setCursor(120,158);
    tft.print("^+"); tft.print(String(f.vert_rate,1)); tft.print("m/s");
  } else if(f.vert_rate < -0.5){
    tft.setTextColor(0xCC4400); tft.setCursor(120,158);
    tft.print("v"); tft.print(String(f.vert_rate,1)); tft.print("m/s");
  } else {
    tft.setTextColor(0x445566); tft.setCursor(120,158);
    tft.print("~Level");
  }

  // Kurs
  tft.setTextSize(1); tft.setTextColor(0x445566);
  tft.setCursor(4,186); tft.print("Kurs: "); tft.print((int)f.kurs); tft.print((char)247);

  addLog("[TRK] "+f.callsign+" "+String((int)f.hoehe_m)+"m "+String((int)f.geschw_kmh)+"kh");
}
void ladeKonfig(){
  Preferences konfigPrefs;
  konfigPrefs.begin("esp_radar", false);

  fa_credits_used  = konfigPrefs.getInt("fa_used",   0);
  cfg_lat          = konfigPrefs.getFloat("lat",      47.3769);
  cfg_lon          = konfigPrefs.getFloat("lon",       8.5417);
  cfg_radius       = konfigPrefs.getFloat("radius",   50.0);
  cfg_credits_tot  = konfigPrefs.getInt("cr_tot",    100);
  cfg_daten_sek    = konfigPrefs.getInt("daten_sek",  30);
  cfg_fa_aktiv     = konfigPrefs.getBool("fa_aktiv",  false);
  cfg_track_cs     = konfigPrefs.getString("track_cs", ""); cfg_track_cs.trim(); cfg_track_cs.toUpperCase();
  cfg_fa_key       = konfigPrefs.getString("fa_key",   "");   cfg_fa_key.trim();
  cfg_osk_client_id     = konfigPrefs.getString("osk_id",   "");   cfg_osk_client_id.trim();
  cfg_osk_client_secret = konfigPrefs.getString("osk_sec",  "");   cfg_osk_client_secret.trim();
  cfg_airport      = konfigPrefs.getString("airport",  "ZRH"); cfg_airport.trim();

  fa_credits_total = cfg_credits_tot;
  // WLAN aus Flash laden
  for(int i=0;i<5;i++){
    String ks="w"+String(i)+"s", kp="w"+String(i)+"p";
    wlan_ssid[i]=konfigPrefs.getString(ks.c_str(), String(WLAN_LIST[i][0]));
    wlan_pass[i]=konfigPrefs.getString(kp.c_str(), String(WLAN_LIST[i][1]));
  }
  konfigPrefs.end();
  // Cache wird separat in ladeCache() geladen
}

void ladeCache(){
  Preferences cachePrefs;
  cachePrefs.begin("esp_cache", true);  // read-only
  int gespeichert=cachePrefs.getInt("cAnz",0);
  int geladen=0;
  for(int i=0;i<gespeichert;i++){
    String key="c"+String(i);
    String cs=cachePrefs.getString((key+"s").c_str(),"");
    String v =cachePrefs.getString((key+"v").c_str(),"");
    String n =cachePrefs.getString((key+"n").c_str(),"");
    if(cs.length()>0 && v.length()>0 && n.length()>0)
      cache[geladen++]={cs,v,n};
  }
  cacheAnzahl=geladen;
  cachePrefs.end();
}

void speichereKonfig(){
  Preferences konfigPrefs;
  konfigPrefs.begin("esp_radar", false);
  konfigPrefs.putFloat("lat",     cfg_lat);
  konfigPrefs.putFloat("lon",     cfg_lon);
  konfigPrefs.putFloat("radius",  cfg_radius);
  konfigPrefs.putInt("daten_sek", cfg_daten_sek);
  konfigPrefs.putInt("cr_tot",    cfg_credits_tot);
  konfigPrefs.putBool("fa_aktiv", cfg_fa_aktiv);
  konfigPrefs.putString("fa_key",   cfg_fa_key);
  konfigPrefs.putString("osk_id",  cfg_osk_client_id);
  konfigPrefs.putString("osk_sec", cfg_osk_client_secret);
  konfigPrefs.putString("airport",  cfg_airport);
  konfigPrefs.putString("track_cs", cfg_track_cs);
  fa_credits_total=cfg_credits_tot;
  konfigPrefs.end();
}

void speichereWlan(){
  Preferences konfigPrefs;
  konfigPrefs.begin("esp_radar", false);
  for(int i=0;i<5;i++){
    String ks="w"+String(i)+"s", kp="w"+String(i)+"p";
    konfigPrefs.putString(ks.c_str(), wlan_ssid[i]);
    konfigPrefs.putString(kp.c_str(), wlan_pass[i]);
  }
  konfigPrefs.end();
}

void speichereCache(String cs,String v,String n){
  if(v.length()==0 || n.length()==0) return;
  if(v=="null" || n=="null") return;
  cs=cs.substring(0,6);
  for(int i=0;i<cacheAnzahl;i++)
    if(cache[i].callsign==cs) return;
  if(cacheAnzahl<MAX_CACHE){
    cache[cacheAnzahl++]={cs,v,n};
    String key="c"+String(cacheAnzahl-1);
    Preferences cachePrefs;
    cachePrefs.begin("esp_cache", false);
    cachePrefs.putString((key+"s").c_str(),cs);
    cachePrefs.putString((key+"v").c_str(),v);
    cachePrefs.putString((key+"n").c_str(),n);
    cachePrefs.putInt("cAnz",cacheAnzahl);
    cachePrefs.end();
  }
}

void handleResetCredits(){
  Preferences konfigPrefs;
  konfigPrefs.begin("esp_radar", false);
  fa_credits_used=0; konfigPrefs.putInt("fa_used",0);
  konfigPrefs.end();
  // Cache separat loeschen
  Preferences cachePrefs;
  cachePrefs.begin("esp_cache", false);
  cachePrefs.end();
  cacheAnzahl=0;
  webServer.send(200,"text/plain","OK");
}

void handleCacheExport(){
  String csv="CALLSIGN,VON,NACH\n";
  for(int i=0;i<cacheAnzahl;i++){
    csv+=cache[i].callsign+","+cache[i].von+","+cache[i].nach+"\n";
  }
  webServer.sendHeader("Content-Disposition","attachment; filename=esp-radar_cache.csv");
  webServer.send(200,"text/csv",csv);
}

void handleCacheImport(){
  if(!webServer.hasArg("data")){ webServer.send(400,"text/plain","Kein Data"); return; }
  String csv=webServer.arg("data");
  int imported=0;
  int pos=csv.indexOf('\n')+1; // Header überspringen
  while(pos<(int)csv.length() && cacheAnzahl<MAX_CACHE){
    int end=csv.indexOf('\n',pos);
    if(end<0) end=csv.length();
    String line=csv.substring(pos,end);
    line.trim();
    if(line.length()==0){ pos=end+1; continue; }
    int c1=line.indexOf(','), c2=line.lastIndexOf(',');
    if(c1<0||c2<=c1){ pos=end+1; continue; }
    String cs=line.substring(0,c1);
    String v=line.substring(c1+1,c2);
    String n=line.substring(c2+1);
    cs.trim(); v.trim(); n.trim();
    if(cs.length()>0&&v.length()>0&&n.length()>0){
      // Nicht doppelt speichern
      bool exists=false;
      for(int i=0;i<cacheAnzahl;i++) if(cache[i].callsign==cs){ exists=true; break; }
      if(!exists){
        cache[cacheAnzahl++]={cs,v,n};
        String key="c"+String(cacheAnzahl-1);
        Preferences cachePrefs;
        cachePrefs.begin("esp_cache", false);
        cachePrefs.putString((key+"s").c_str(),cs);
        cachePrefs.putString((key+"v").c_str(),v);
        cachePrefs.putString((key+"n").c_str(),n);
        cachePrefs.end();
        imported++;
      }
    }
    pos=end+1;
  }
  Preferences cachePrefs;
  cachePrefs.begin("esp_cache", false);
  cachePrefs.putInt("cAnz",cacheAnzahl);
  cachePrefs.end();
  webServer.send(200,"text/plain","Importiert: "+String(imported)+" Eintraege. Total: "+String(cacheAnzahl));
}

// ============================================================
// DATEN HOLEN (OpenSky)
// ============================================================

void holeDaten(){
  if(WiFi.status()!=WL_CONNECTED){
    clearScreen();
    tft.setTextColor(TFT_RED); tft.setCursor(5,10);
    tft.println("WLAN getrennt!"); delay(3000); ESP.restart(); return;
  }
  tft.fillRect(0,32,240,12,TFT_BLACK);
  tft.setTextColor(0x2244); tft.setCursor(4,42); tft.print("Lade Positionen...");
  float lamin=LAT-(RADIUS_KM/111.0), lamax=LAT+(RADIUS_KM/111.0);
  float lomin=LON-(RADIUS_KM/74.0),  lomax=LON+(RADIUS_KM/74.0);
  String url="https://opensky-network.org/api/states/all?lamin="+
    String(lamin,4)+"&lomin="+String(lomin,4)+
    "&lamax="+String(lamax,4)+"&lomax="+String(lomax,4);
  HTTPClient http; http.begin(url); http.setTimeout(10000);
  int httpCode = http.GET();
  if(httpCode!=200){
    addLog("[SKY] HTTP: "+String(httpCode));
    http.end(); return;
  }
  String payload = http.getString();
  http.end();
  DynamicJsonDocument filter(64);
  filter["states"] = true;
  DynamicJsonDocument doc(16384);
  DeserializationError derr=deserializeJson(doc,payload,DeserializationOption::Filter(filter));
  if(derr){ addLog("[SKY] Fehler: "+String(derr.c_str())); return; }
  JsonArray states=doc["states"];
  if(states.isNull()){ letzteAnzahl=0; return; }
  Flugzeug liste[20]; int anzahl=0;
  for(JsonArray s:states){
    if(anzahl>=20) break;
    float fla=s[6].isNull()?0:s[6].as<float>();
    float flo=s[5].isNull()?0:s[5].as<float>();
    if(fla==0&&flo==0) continue;
    float dist=berechnDistanz(LAT,LON,fla,flo);
    if(dist>RADIUS_KM) continue;
    String cs=s[1].as<String>(); cs.trim();
    String icao24=s[0].as<String>(); icao24.trim();
    // Nur kommerzielle Flüge filtern
    if(cs.length()<5) continue;
    if(!isAlpha(cs[0])||!isAlpha(cs[1])||!isAlpha(cs[2])) continue;
    if(!isDigit(cs[3])) continue;
    // Privatflugzeuge ausfiltern
    if(cs.startsWith("HB")) continue;
    if(cs.startsWith("D-")) continue;
    if(cs.startsWith("OE")) continue;
    if(cs.startsWith("F-")) continue;
    liste[anzahl].callsign  =cs;
    liste[anzahl].hoehe_m   =s[7].isNull()?0:s[7].as<float>();
    liste[anzahl].geschw_kmh=s[9].isNull()?0:s[9].as<float>()*3.6;
    liste[anzahl].kurs      =s[10].isNull()?0:s[10].as<float>();
    liste[anzahl].vert_rate =s[11].isNull()?0:s[11].as<float>();
    liste[anzahl].distanz_km=dist;
    liste[anzahl].lat=fla; liste[anzahl].lon=flo;
if(liste[anzahl].geschw_kmh < 50 && liste[anzahl].hoehe_m < 1000 && abs(liste[anzahl].vert_rate) < 1.0) continue;
    liste[anzahl].routeQuelle=0;
    speichereIcao24(cs, icao24); // icao24 für spätere Routenabfrage merken
    anzahl++;
  }
    // Sortierung: 1. fliegend mit Route, 2. fliegend ohne Route, 3. gelandete
  auto sortKey = [](Flugzeug &f) -> int {
    bool fliegend = f.hoehe_m >= 500;
    bool hatRoute = f.von.length()>0 && f.nach.length()>0;
    if(fliegend && hatRoute)  return 0;
    if(fliegend && !hatRoute) return 1;
    return 2;
  };
  for(int i=0;i<anzahl-1;i++)
    for(int j=0;j<anzahl-i-1;j++){
      int ki=sortKey(liste[j]), kj=sortKey(liste[j+1]);
      if(ki>kj || (ki==kj && liste[j].distanz_km>liste[j+1].distanz_km))
        {Flugzeug t=liste[j];liste[j]=liste[j+1];liste[j+1]=t;}
    }
  for(int i=0;i<min(anzahl,7);i++){
    String cs=liste[i].callsign;
    if(cs.length()==0) continue;
    String v,n;
    String icao24=holeIcao24(cs);
    int q=holeRoute(cs,icao24,v,n);
    if(q>0){ liste[i].von=v; liste[i].nach=n; liste[i].routeQuelle=q; }
    ergaenzeFakeRoute(liste[i]);
  }
  for(int i=0;i<anzahl;i++) letzteList[i]=liste[i];
  letzteAnzahl=anzahl;
  addLog("[SKY] "+String(anzahl)+" Flugzeuge im Radius");
  zeigeListeSeite(letzteList,letzteAnzahl,0);
}

// ============================================================
// WEB-UI: FIX 2 - HTML in Chunks senden (kein grosser String)
// sendContent() schreibt direkt in den TCP-Puffer.
// Kein html += -> kein Heap-Fragmentierungsproblem.
// ============================================================
void sc(const String& s){ webServer.sendContent(s); }
void sc(const char*  s){ webServer.sendContent(s); }

void sendPageHeader(int activeTab){
  webServer.setContentLength(CONTENT_LENGTH_UNKNOWN);
  webServer.send(200,"text/html; charset=utf-8","");
  sc("<!DOCTYPE html><html lang=de><head><meta charset=UTF-8><meta name=viewport content='width=device-width,initial-scale=1'><title>ESP-Radar</title><style>");
  sc("*{box-sizing:border-box;margin:0;padding:0}body{background:#0d0d0d;color:#ccc;font-family:system-ui,sans-serif;font-size:15px}");
  sc("header{background:#001040;padding:12px 16px;display:flex;align-items:center}header h1{color:#00ccff;font-size:17px;font-weight:500}");
  sc(".ip{font-size:11px;color:#446;margin-left:auto;font-family:monospace}");
  sc(".tabs{display:flex;border-bottom:1px solid #1a2030;background:#080808}");
  sc(".tab{flex:1;padding:10px 0;text-align:center;font-size:13px;color:#445;cursor:pointer;border:none;background:none;text-decoration:none;display:block}");
  sc(".tab.a{color:#00ccff;border-bottom:2px solid #0066cc}");
  sc(".sl{font-size:10px;color:#446;text-transform:uppercase;letter-spacing:.08em;margin:14px 0 6px}");
  sc(".f{background:#111a2a;border:1px solid #1a2a3a;border-radius:8px;padding:8px 10px;margin-bottom:6px}");
  sc(".f label{font-size:11px;color:#446;display:block;margin-bottom:3px}");
  sc(".f input,.f textarea{width:100%;background:transparent;border:none;border-bottom:1px solid #0066cc;color:#fff;font-size:13px;padding:2px 0;font-family:monospace;outline:none}");
  sc(".r2{display:grid;grid-template-columns:1fr 1fr;gap:6px}.p{padding:14px}");
  sc(".bw{height:5px;background:#112;border-radius:3px;overflow:hidden;margin-top:6px}.bf{height:100%;border-radius:3px}");
  sc(".bl{display:flex;justify-content:space-between;font-size:11px;color:#446;margin-top:3px}");
  sc(".btn{width:100%;padding:10px;border-radius:8px;border:1px solid;font-size:13px;font-weight:500;cursor:pointer;margin-top:6px}");
  sc(".bs{background:#003080;border-color:#0044aa;color:#66aaff}.br{background:#1a0000;border-color:#440000;color:#ff4444;margin-top:4px}");
  sc(".bo{background:#1a1000;border-color:#443300;color:#ffaa22;margin-top:4px}.bg{background:#001800;border-color:#004400;color:#44cc88;margin-top:4px}");
  sc(".sr{display:flex;justify-content:space-between;padding:8px 0;border-bottom:1px solid #1a2030;font-size:13px}");
  sc(".sv{font-family:monospace;color:#aaa}.pg{font-size:11px;padding:2px 8px;border-radius:20px;background:#0a2a10;color:#22cc44;border:1px solid #0f4a1a}");
  sc(".log{background:#040a14;border-radius:6px;padding:8px;font-family:monospace;font-size:11px;line-height:1.7;max-height:280px;overflow-y:auto;margin-top:6px}");
  sc(".z{color:#22cc44}.fa{color:#ffaa22}.sk{color:#4488ff}.dp{color:#00ccff}.dm{color:#334}");
  sc(".toast{position:fixed;bottom:16px;left:50%;transform:translateX(-50%);background:#003a00;color:#44ff88;padding:8px 18px;border-radius:8px;font-size:13px;display:none}");
  sc("</style></head><body>");
  sc("<header><h1>ESP-Radar</h1><span class=ip>"); sc(WiFi.localIP().toString()); sc("</span></header>");
  sc("<div class=tabs>");
  const char* tabs[4]={"Einstellungen","WLAN","Status","Log"};
  for(int i=0;i<4;i++){
    String cls=i==activeTab?"tab a":"tab";
    sc("<a class='"+cls+"' href='/?t="+String(i)+"'>"+String(tabs[i])+"</a>");
  }
  sc("</div><div class=p>");
}

void handleRoot(){
  // ... rest
  int activeTab=0;
  if(webServer.hasArg("t")) activeTab=webServer.arg("t").toInt();
  activeTab=constrain(activeTab,0,3);
  int avail=fa_credits_total-fa_credits_used;
  int pct=fa_credits_total>0?(avail*100/fa_credits_total):0;
  String bCol=pct>30?"#22cc44":pct>10?"#ffaa22":"#ff4444";
  sendPageHeader(activeTab);
  if(activeTab==0){
    sc("<div class=sl>Standort</div><div class=r2>");
    sc("<div class=f><label>Breitengrad</label><input id=lat type=number step=0.0001 value="); sc(String(cfg_lat,4)); sc("></div>");
    sc("<div class=f><label>Laengengrad</label><input id=lon type=number step=0.0001 value="); sc(String(cfg_lon,4)); sc("></div></div>");
    sc("<div class=f><label>Radius (km)</label><input id=radius type=number min=5 max=300 value="); sc(String(cfg_radius,0)); sc("></div>");
    sc("<button class='btn bg' onclick='geo()'>Standort automatisch bestimmen</button>");
    sc("<div class=sl>Flughafen</div>");
sc("<select onchange='setCoords(this.value)' style='width:100%;background:#111a2a;border:1px solid #1a2a3a;border-radius:8px;padding:8px 10px;color:#fff;font-size:13px;margin-bottom:6px'>");
sc("<option value=''>-- Flughafen waehlen --</option>");
sc("<optgroup label='Schweiz'>");
sc("<option value='47.4647,8.5492'>ZRH - Zuerich</option>");
sc("<option value='46.2381,6.1089'>GVA - Genf</option>");
sc("<option value='47.5896,7.5300'>BSL - Basel</option>");
sc("<option value='46.9141,7.4977'>BRN - Bern</option>");
sc("<option value='46.2196,7.3268'>SIR - Sion</option>");
sc("</optgroup>");
sc("<optgroup label='Europa'>");
sc("<option value='51.4775,0.4614'>LHR - London</option>");
sc("<option value='49.0097,2.5479'>CDG - Paris</option>");
sc("<option value='52.3086,4.7639'>AMS - Amsterdam</option>");
sc("<option value='50.0333,8.5706'>FRA - Frankfurt</option>");
sc("<option value='48.3537,11.7750'>MUC - Muenchen</option>");
sc("<option value='48.1102,16.5697'>VIE - Wien</option>");
sc("<option value='41.8003,12.2389'>FCO - Rom</option>");
sc("<option value='40.4719,3.5626'>MAD - Madrid</option>");
sc("<option value='41.2971,2.0785'>BCN - Barcelona</option>");
sc("<option value='50.9010,4.4844'>BRU - Bruessel</option>");
sc("<option value='55.6181,12.6561'>CPH - Kopenhagen</option>");
sc("<option value='60.1939,11.1004'>OSL - Oslo</option>");
sc("<option value='59.6519,17.9186'>ARN - Stockholm</option>");
sc("<option value='60.3172,24.9633'>HEL - Helsinki</option>");
sc("<option value='52.1657,20.9671'>WAW - Warschau</option>");
sc("<option value='50.1008,14.2600'>PRG - Prag</option>");
sc("<option value='47.4369,19.2556'>BUD - Budapest</option>");
sc("<option value='37.9364,23.9445'>ATH - Athen</option>");
sc("<option value='38.7756,9.1354'>LIS - Lissabon</option>");
sc("<option value='53.4213,8.6811'>DUB - Dublin</option>");
sc("</optgroup>");
sc("</select>");
    sc("<div class=sl>Daten-Intervall</div>");
    sc("<div class=f><label>Neue Daten alle (Sek.)</label><input id=daten type=number min=10 max=600 value="); sc(String(cfg_daten_sek)); sc("></div>");
    sc("<div class=sl>Flug verfolgen</div>");
    String trackEsc = cfg_track_cs; trackEsc.trim();
    sc("<div class=f><label>Callsign (z.B. SWR123, leer = aus)</label><input id=trackcs type=text maxlength=8 value=\"" + trackEsc + "\"></div>");
    sc("<div class=sl>FlightAware</div>");
    String faKeyEsc = cfg_fa_key; faKeyEsc.replace("\"","&quot;");
    sc("<div class=f><label>API Key</label><input id=fakey type=text value=\"" + faKeyEsc + "\"></div>");
    sc("<div class=f><label>Credits (Limit)</label><input id=credits type=number min=0 max=9999 value="); sc(String(cfg_credits_tot)); sc("></div>");
    sc("<div class=f><label>FlightAware aktiviert</label>");
    sc("<select id=fa style='width:100%;background:transparent;border:none;border-bottom:1px solid #0066cc;color:#fff;font-size:13px;padding:2px 0'>");
    if(cfg_fa_aktiv){ sc("<option value=0>Nein</option><option value=1 selected>Ja</option>"); }
    else            { sc("<option value=0 selected>Nein</option><option value=1>Ja</option>"); }
    sc("</select></div>");
    sc("<div class=bl><span>Verbraucht: "); sc(String(fa_credits_used)); sc("/"); sc(String(fa_credits_total)); sc("</span><span>"); sc(String(pct)); sc("%</span></div>");
    sc("<div class=bw><div class=bf style='width:"); sc(String(pct)); sc("%;background:"); sc(bCol); sc("'></div></div>");
    sc("<div class=sl>OpenSky</div><div class=r2>");
    String oskIdEsc = cfg_osk_client_id; oskIdEsc.replace("\"","&quot;");
    String oskSecEsc = cfg_osk_client_secret; oskSecEsc.replace("\"","&quot;");
    sc("<div class=f><label>Client ID</label><input id=oskid type=text value=\"" + oskIdEsc + "\"></div>");
    sc("<div class=f><label>Client Secret</label><input id=osksec type=password value=\"" + oskSecEsc + "\"></div></div>");
    // Token Status
    sc("<div class=sl style='color:#446'>Token Status</div>");
    if(osk_access_token.length()>0){
      unsigned long restMs = osk_token_expires > millis() ? osk_token_expires - millis() : 0;
      sc("<div class=f><label>Token gueltig noch</label><input type=text readonly value='"+String(restMs/1000)+"s' style='color:#44cc88'></div>");
    } else {
      sc("<div class=f><label>Token Status</label><input type=text readonly value='Kein Token' style='color:#cc4444'></div>");
    }
    sc("<div class=sl>Heimflughafen</div>");
    String airportClean = cfg_airport; airportClean.trim();
    sc("<div class=f><label>IATA Code (z.B. ZRH)</label><input id=airport type=text maxlength=4 value=\"" + airportClean + "\"></div>");
    sc("<button class='btn bs' onclick='save()'>Speichern &amp; neu starten</button>");
    sc("<button class='btn br' onclick='resetCr()'>Credits auf 0 zuruecksetzen</button>");
    sc("<script>");
    sc("function setCoords(v){if(!v)return;var p=v.split(',');document.getElementById('lat').value=p[0];document.getElementById('lon').value=p[1];}");
    sc("function toast(m,c){var e=document.getElementById('t');e.textContent=m;e.style.background=c||'#003a00';e.style.display='block';setTimeout(function(){e.style.display='none'},2500);}");
    sc("function save(){var b=new URLSearchParams({lat:document.getElementById('lat').value,lon:document.getElementById('lon').value,radius:document.getElementById('radius').value,daten:document.getElementById('daten').value,credits:document.getElementById('credits').value,fa:document.getElementById('fa').value,fakey:document.getElementById('fakey').value,oskid:document.getElementById('oskid').value,osksec:document.getElementById('osksec').value,airport:document.getElementById('airport').value,trackcs:document.getElementById('trackcs').value});fetch('/save',{method:'POST',body:b}).then(function(){toast('Gespeichert!');setTimeout(function(){location.reload()},3500)}).catch(function(){toast('Fehler','#3a0000')});}");
    sc("function geo(){toast('Bestimme...');fetch('/geoloc').then(function(r){return r.json()}).then(function(d){document.getElementById('lat').value=d.lat.toFixed(4);document.getElementById('lon').value=d.lon.toFixed(4);toast('OK: '+d.city)}).catch(function(){toast('Fehler','#3a0000')});}");
    sc("</script>");
  } else if(activeTab==1){
    sc("<div class=sl>WLANs (bis zu 5)</div>");
    for(int i=0;i<5;i++){
      String row = "<div class=r2 style='margin-bottom:5px'>"
        "<div class=f><input id=ss"+String(i)+" placeholder='SSID "+String(i+1)+"' value='"+wlan_ssid[i]+"'></div>"
        "<div class=f><input id=sp"+String(i)+" type=password placeholder='Passwort' value='"+wlan_pass[i]+"'></div>"
        "</div>";
      sc(row);
    }
    sc("<button class='btn bs' onclick='saveW()'>Speichern &amp; neu starten</button>");
    sc("<script>function toast(m,c){var e=document.getElementById('t');e.textContent=m;e.style.background=c||'#003a00';e.style.display='block';setTimeout(function(){e.style.display='none'},2500);}");
    sc("function saveW(){var b=new URLSearchParams({ss0:document.getElementById('ss0').value,sp0:document.getElementById('sp0').value,ss1:document.getElementById('ss1').value,sp1:document.getElementById('sp1').value,ss2:document.getElementById('ss2').value,sp2:document.getElementById('sp2').value,ss3:document.getElementById('ss3').value,sp3:document.getElementById('sp3').value,ss4:document.getElementById('ss4').value,sp4:document.getElementById('sp4').value});fetch('/savewlan',{method:'POST',body:b}).then(function(){toast('Gespeichert!');setTimeout(function(){location.reload()},3500)}).catch(function(){toast('Fehler','#3a0000')});}</script>");
  } else if(activeTab==2){
    sc("<div class=sr><span>WLAN</span><span class='sv pg'>"); sc(verbundenesWlan); sc("</span></div>");
    sc("<div class=sr><span>IP</span><span class=sv>"); sc(WiFi.localIP().toString()); sc("</span></div>");
    sc("<div class=sr><span>Standort</span><span class=sv>"); sc(String(cfg_lat,3)); sc(", "); sc(String(cfg_lon,3)); sc("</span></div>");
    sc("<div class=sr><span>Laufzeit</span><span class=sv>"); sc(uptimeStr()); sc("</span></div>");
    sc("<div class=sr><span>Flugzeuge</span><span class=sv>"); sc(String(letzteAnzahl)); sc("</span></div>");
    sc("<div class=sr><span>FA-Anfragen</span><span class='sv' style='color:#ffaa22'>"); sc(String(stat_fa_heute)); sc("</span></div>");
    sc("<div class=sr><span>OSK-Anfragen</span><span class='sv' style='color:#00ccff'>"); sc(String(stat_osk_heute)); sc("</span></div>");
    sc("<div class=sr><span>Route-Cache</span><span class=sv>"); sc(String(cacheAnzahl)); sc("/1000</span></div>");
    sc("<button class='btn bo' onclick='if(confirm(\"Neu starten?\"))fetch(\"/reboot\")'>ESP32 neu starten</button>");
    sc("<button class='btn br' onclick='if(confirm(\"Factory Reset? WLAN bleibt erhalten!\"))fetch(\"/factoryreset\")'>Factory Reset</button>");
    sc("<a href='/cache.csv'><button class='btn bs'>Cache exportieren (CSV)</button></a>");
    sc("<div class=sl>Cache importieren</div>");
    sc("<div class=f><textarea id=csv style='height:70px;resize:none' placeholder='CSV hier einfuegen...'></textarea></div>");
    sc("<button class='btn bg' onclick='imp()'>Cache importieren</button>");
    sc("<script>");
    sc("function toast(m,c){var e=document.getElementById('t');e.textContent=m;e.style.background=c||'#003a00';e.style.display='block';setTimeout(function(){e.style.display='none'},2500);}");
    sc("function imp(){var d=document.getElementById('csv').value;if(!d){toast('Kein CSV','#3a0000');return;}fetch('/cacheimport',{method:'POST',body:new URLSearchParams({data:d})}).then(function(r){return r.text()}).then(function(t){toast(t);setTimeout(function(){location.reload()},2000)}).catch(function(){toast('Fehler','#3a0000')});}");
    sc("</script>");
  } else {
    int total=min(logIdx,LOG_LINES);
    sc("<div class=log>");
    for(int i=total-1;i>=0;i--){
      String line=logBuf[(logIdx-1-i+LOG_LINES)%LOG_LINES];
      String cls="dm";
      if(line.startsWith("[ZRH]")) cls="z";
      else if(line.startsWith("[FA]")) cls="fa";
      else if(line.startsWith("[SKY]")||line.startsWith("[GEO]")||line.startsWith("[WLAN]")) cls="sk";
      else if(line.startsWith("[DSP]")||line.startsWith("[WEB]")) cls="dp";
      sc("<span class="+cls+">"+line+"</span><br>");
    }
    sc("</div><a href='/?t=3'><button class='btn bs' style='margin-top:8px'>Aktualisieren</button></a>");
  }
  sc("</div><div class=toast id=t></div></body></html>");
  webServer.sendContent("");
}

void handleSave(){
  if(webServer.hasArg("lat"))     cfg_lat        =webServer.arg("lat").toFloat();
  if(webServer.hasArg("lon"))     cfg_lon        =webServer.arg("lon").toFloat();
  if(webServer.hasArg("radius"))  cfg_radius     =webServer.arg("radius").toFloat();
  if(webServer.hasArg("daten"))   cfg_daten_sek  =webServer.arg("daten").toInt();
  if(webServer.hasArg("credits")) cfg_credits_tot=webServer.arg("credits").toInt();
  if(webServer.hasArg("fa"))      cfg_fa_aktiv   =(webServer.arg("fa")=="1");
  if(webServer.hasArg("fakey"))   { cfg_fa_key   = webServer.arg("fakey");   cfg_fa_key.trim(); }
  if(webServer.hasArg("oskid"))  { cfg_osk_client_id     = webServer.arg("oskid");  cfg_osk_client_id.trim(); }
  if(webServer.hasArg("osksec")) { cfg_osk_client_secret = webServer.arg("osksec"); cfg_osk_client_secret.trim(); }
  if(webServer.hasArg("airport")) { cfg_airport  = webServer.arg("airport");  cfg_airport.trim(); }
  if(webServer.hasArg("trackcs")) { cfg_track_cs = webServer.arg("trackcs"); cfg_track_cs.trim(); cfg_track_cs.toUpperCase(); }
  speichereKonfig();
  webServer.send(200,"text/plain","OK");
  delay(500); ESP.restart();
}


void handleSaveWlan(){
  for(int i=0;i<5;i++){
    wlan_ssid[i]=webServer.arg("ss"+String(i));
    wlan_pass[i]=webServer.arg("sp"+String(i));
  }
  speichereWlan();
  webServer.send(200,"text/plain","OK");
  delay(500); ESP.restart();
}

void handleFactoryReset(){
  // Konfig loeschen (ausser WLAN)
  Preferences konfigPrefs;
  konfigPrefs.begin("esp_radar", false);
  konfigPrefs.clear();
  konfigPrefs.end();
  // Cache loeschen
  Preferences cachePrefs;
  cachePrefs.begin("esp_cache", false);
  cachePrefs.clear();
  cachePrefs.end();
  // RAM zuruecksetzen
  cacheAnzahl=0; fa_credits_used=0;
  cfg_fa_key=""; cfg_osk_client_id=""; cfg_osk_client_secret="";
  cfg_airport="ZRH"; cfg_fa_aktiv=false;
  webServer.send(200,"text/plain","Factory Reset OK");
  delay(500); ESP.restart();
}

void handleReboot(){
  webServer.send(200,"text/plain","Rebooting...");
  delay(300); ESP.restart();
}

// GET /geoloc - Standort per IP bestimmen, gibt JSON zurueck
// Der Browser liest die Koordinaten und traegt sie direkt in
// die Felder ein - kein Neustart noetig bis der Nutzer speichert.
void handleGeoLoc(){
  bool ok=bestimmeStandort();
  if(!ok){ webServer.send(500,"application/json","{\"error\":\"Fehler\"}"); return; }
  String city="unbekannt";
  if(logIdx>0){
    String last=logBuf[(logIdx-1)%LOG_LINES];
    int si=last.indexOf("] ")+2, ei=last.indexOf(" (");
    if(si>1&&ei>si) city=last.substring(si,ei);
  }
  String json="{\"lat\":"+String(cfg_lat,4)+",\"lon\":"+String(cfg_lon,4)+",\"city\":\""+city+"\"}";
  webServer.send(200,"application/json",json);
}

void starteWebServer(){
  webServer.on("/",            HTTP_GET,  handleRoot);
  webServer.on("/save",        HTTP_POST, handleSave);
  webServer.on("/savewlan",    HTTP_POST, handleSaveWlan);
  webServer.on("/resetcredits",HTTP_GET,  handleResetCredits);
  webServer.on("/reboot",       HTTP_GET,  handleReboot);
  webServer.on("/factoryreset", HTTP_GET,  handleFactoryReset);
  webServer.on("/geoloc",      HTTP_GET,  handleGeoLoc);
  webServer.on("/cache.csv",   HTTP_GET,  handleCacheExport);
  webServer.on("/cacheimport", HTTP_POST, handleCacheImport);
  webServer.begin();
  addLog("[WEB] Server gestartet: "+WiFi.localIP().toString());
}

// ============================================================
// SETUP
// ============================================================
void setup(){
  delay(2000);

  tft.init();
  tft.setRotation(1);
  clearScreen();

  // GMT130 Offset-Fix
  tft.startWrite();
  tft.writecommand(0x36);
  tft.writedata(0x60);
  tft.writecommand(0x2A);
  tft.writedata(0x00); tft.writedata(0x00);
  tft.writedata(0x00); tft.writedata(0xEF);
  tft.writecommand(0x2B);
  tft.writedata(0x00); tft.writedata(0x50);
  tft.writedata(0x01); tft.writedata(0x3F);
  tft.endWrite();
  tftReady = true;

  // Startbildschirm mit Logo
  zeichneLogo(70, 20);
  tft.setTextColor(TFT_WHITE); tft.setTextSize(1);
  tft.setCursor(72,128); tft.println("v2.0");

  ladeKonfig();
  ladeCache();

  pinMode(21, INPUT_PULLUP);
  pinMode(22, INPUT_PULLUP);

  WiFi.mode(WIFI_STA);
  delay(100);
  if(!verbindeWlan()){
    WiFi.mode(WIFI_AP);
    WiFi.softAP("ESP-Radar", "12345678");
    clearScreen();
    tft.setTextColor(TFT_YELLOW); tft.setTextSize(2);
    tft.setCursor(10,60); tft.println("Kein WLAN!");
    tft.setTextSize(1); tft.setTextColor(TFT_WHITE);
    tft.setCursor(10,90);  tft.println("Hotspot: ESP-Radar");
    tft.setCursor(10,105); tft.println("Passwort: 12345678");
    tft.setCursor(10,120); tft.println("IP: 192.168.4.1");
    starteWebServer();
    while(true){ webServer.handleClient(); delay(10); }
  }

  starteWebServer();
  bootTime=millis();

  clearScreen();
  tft.setTextColor(TFT_GREEN);  tft.setCursor(10,90);  tft.println("WLAN: "+verbundenesWlan);
  tft.setTextColor(TFT_WHITE);  tft.setCursor(10,108); tft.println("Webkonfig unter:");
  tft.setTextColor(TFT_CYAN);   tft.setTextSize(2);
  tft.setCursor(10,128); tft.println(WiFi.localIP().toString());
  tft.setTextSize(1);
  tft.setTextColor(0x5555); tft.setCursor(10,158); tft.println("(im Browser oeffnen)");

  delay(3000);
  holeDaten();
  datenTimerStart = millis();
}

// ============================================================
// LOOP
// ============================================================
void loop(){
  static int currentScreen = 0;
  // Screens: 0=Liste, 1=Radar, 2=Spotlight, 3=SpotlightRadar, 4=Track

  // Knoepfe pruefen
  bool vorwaerts   = (digitalRead(21) == LOW);
  bool rueckwaerts = (digitalRead(22) == LOW);
  if(vorwaerts || rueckwaerts){
    delay(200);
    standbyZaehler = 5;  // Zähler zurücksetzen bei Knopfdruck
  }

  // Screenwechsel per Knopf
  static int lastScreen = -1;
  static int listSeite  = 0;

  // Globaler Hintergrund-Timer: Daten neu laden (nur wenn Zähler > 0)
  unsigned long datenMs = (unsigned long)cfg_daten_sek * 1000;
  if(standbyZaehler > 0 && millis() - datenTimerStart >= datenMs){
    standbyZaehler--;
    clearScreen();
    zeichneLogo(70, 50);
    tft.setTextColor(0x224422); tft.setTextSize(1);
    tft.setCursor(4,162); tft.print(FACTS[random(FACTS_COUNT)]);
    int avail=fa_credits_total-fa_credits_used;
    int pct=fa_credits_total>0?(avail*100/fa_credits_total):0;
    uint16_t col=pct>30?TFT_GREEN:pct>10?TFT_ORANGE:TFT_RED;
    tft.setTextColor(col); tft.setTextSize(1);
    tft.setCursor(60,178); tft.print(avail); tft.print("/"); tft.print(fa_credits_total); tft.print(" FA Credits");
    if(standbyZaehler > 0){
      tft.setTextColor(0x224422); tft.setCursor(60,192);
      tft.print("Standby in "); tft.print(standbyZaehler); tft.print("x");
    } else {
      tft.setTextColor(0x442222); tft.setCursor(60,192);
      tft.print("Standby aktiv");
    }
    webServer.handleClient();
    holeDaten();
    datenTimerStart = millis();
    vorwaerts = false; rueckwaerts = false;
    lastScreen = -1;
  }
  if(vorwaerts){
    if(currentScreen == 0){
      int seiten = max(1, (letzteAnzahl + LIST_PRO_SEITE - 1) / LIST_PRO_SEITE);
      if(listSeite < seiten - 1){ listSeite++; }   // naechste Listenseite
      else { listSeite = 0; currentScreen = 1; }   // letzte Seite -> Radar
    } else if(currentScreen == 1){
      currentScreen = 2;
    } else if(currentScreen == 2){
      if(spotStartIdx >= 0 || spotLandIdx >= 0) currentScreen = 3;
      else if(cfg_track_cs.length()>0) currentScreen = 4;
      else currentScreen = 0;
    } else if(currentScreen == 3){
      currentScreen = cfg_track_cs.length()>0 ? 4 : 0;
    } else if(currentScreen == 4){
      currentScreen = 0;
    }
  }
  if(rueckwaerts){
    if(currentScreen == 0){
      if(listSeite > 0){ listSeite--; }            // vorherige Listenseite
      else {
        listSeite = 0;
        if(cfg_track_cs.length()>0) currentScreen = 4;
        else if(spotStartIdx >= 0 || spotLandIdx >= 0) currentScreen = 3;
        else currentScreen = 2;
      }
    } else if(currentScreen == 1){
      currentScreen = 0; listSeite = 0;
    } else if(currentScreen == 2){
      currentScreen = 1;
    } else if(currentScreen == 3){
      currentScreen = 2;
    } else if(currentScreen == 4){
      if(spotStartIdx >= 0 || spotLandIdx >= 0) currentScreen = 3;
      else currentScreen = 2;
    }
  }

  // Webserver bedienen
  webServer.handleClient();

  // Screen nur neu zeichnen bei Knopfdruck oder Screenwechsel
  static int lastListSeite = -1;
  bool neuZeichnen = (vorwaerts || rueckwaerts || currentScreen != lastScreen || listSeite != lastListSeite);
  if(!neuZeichnen){
    static int lastSl = -1;
    static bool lastStandby = false;
    bool istStandby = (standbyZaehler <= 0);
    int sl = -1;
    if(!istStandby){
      unsigned long datenMs2 = (unsigned long)cfg_daten_sek * 1000;
      unsigned long elapsed2 = millis() - datenTimerStart;
      sl = (int)((datenMs2 - min(elapsed2, datenMs2)) / 1000) + 1;
    }
    if(sl != lastSl || istStandby != lastStandby){
      lastSl = sl;
      lastStandby = istStandby;
      tft.fillRect(190,4,46,16,TFT_NAVY);
      tft.setTextSize(1);
      if(istStandby){
        tft.setTextColor(0x442222);
        tft.setCursor(196,12); tft.print("ZZZ");
      } else {
        tft.setTextColor(0x334433);
        tft.setCursor(196,12); tft.print(sl); tft.print("s");
      }
    }
    delay(50);
    return;
  }
  lastScreen = currentScreen;
  lastListSeite = listSeite;

 if(currentScreen == 0){
    int seiten = max(1, (letzteAnzahl + LIST_PRO_SEITE - 1) / LIST_PRO_SEITE);
    addLog("[DSP] Liste Seite "+String(listSeite+1)+"/"+String(seiten));
    zeigeListeSeite(letzteList, letzteAnzahl, listSeite);
    char listeMitte[12];
    sprintf(listeMitte, "LISTE %d/%d", listSeite+1, seiten);
    zeichneNavBar("TRK", listeMitte, "RADAR MAX");

  } else if(currentScreen == 1){
    addLog("[DSP] Radar");
    zeigeRadar(letzteList, letzteAnzahl);
    zeichneNavBar("LISTE", "RADAR MAX", "SPOT");

  } else if(currentScreen == 2){
    addLog("[DSP] Spotlight");
    zeigeSpotlight(letzteList, letzteAnzahl);
    zeichneNavBar("RADAR", "SPOTLIGHT", "RADAR APR");

  } else if(currentScreen == 3){
    addLog("[DSP] SpotRadar");
    zeigeSpotlightRadar(letzteList, letzteAnzahl);
    zeichneNavBar("SPOT", "RADAR APR", "TRK");

  } else if(currentScreen == 4){
    addLog("[DSP] Track: "+cfg_track_cs);
    zeigeTrack(letzteList, letzteAnzahl);
    zeichneNavBar("APR", "TRACK", "LISTE");
  }

  delay(50);
}