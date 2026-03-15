#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Preferences.h>
#include <FS.h>
#include <SPIFFS.h>
#include <WebServer.h>   // ESP32 eingebaut, kein Download noetig
#include <TFT_eSPI.h>
#include "ESP_Radar_types.h"

// ============================================
// NUR DIES ANPASSEN
// ============================================
const char* FA_API_KEY = "DEIN_FLIGHTAWARE_API_KEY";

// Bis zu 5 WLANs - ESP32 verbindet sich mit dem staerksten
// Nicht benoetigte Eintraege leer lassen: {"", ""}
const char* WLAN_LIST[][2] = {
  {"WLAN_NAME_1",   "WLAN_PASSWORT_1"},   // z.B. Heimnetz
  {"WLAN_NAME_2",   "WLAN_PASSWORT_2"},   // z.B. Handy-Hotspot
  {"",              ""},
  {"",              ""},
  {"",              ""},
};
// ============================================

// WLAN-Liste (aus Flash geladen - per Web-UI aenderbar)
String wlan_ssid[5];
String wlan_pass[5];

// Konfiguration (aus Flash geladen - per Web-UI aenderbar)
float cfg_lat         = 47.3769;
float cfg_lon         =  8.5417;
float cfg_radius      = 50.0;
int   cfg_liste_sek   = 30;
int   cfg_radar_sek   = 10;
int   cfg_credits_tot = 100;
int   cfg_spot_sek   = 10;

WebServer webServer(80);
unsigned long LISTE_MS = 30000;
unsigned long RADAR_MS = 10000;
unsigned long SPOT_MS  = 10000;
#define LAT       cfg_lat
#define LON       cfg_lon
#define RADIUS_KM cfg_radius

Preferences prefs;
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

unsigned long wechselStart = 0;
bool          istListe     = true;
int stat_zrh_heute = 0;
int stat_fa_heute  = 0;
unsigned long bootTime = 0;
String verbundenesWlan = "";

// ============================================================
// TIMER-BALKEN
// ============================================================
void zeichneTimerBalken(unsigned long elapsed, unsigned long total, bool liste){
  const int BY=215, BX=10, BW=220;
  const int DOTS=20; // Anzahl Punkte
  // Nur neu zeichnen wenn sich etwas geändert hat
  static int lastDots = -1;
  float rem = 1.0-(float)elapsed/(float)total;
  if(rem<0) rem=0;
  int activeDots = (int)(DOTS*rem);
  if(activeDots == lastDots) return; // Kein Flackern wenn gleich
  lastDots = activeDots;
  tft.fillRect(0,BY-1,240,12,TFT_BLACK);
  int spacing = BW/DOTS;
  uint16_t col=liste?0x0044AA:0x004422;
  for(int i=0;i<DOTS;i++){
    int px = BX + i*spacing + spacing/2;
    if(i < activeDots){
      // Aktive Punkte — heller wenn am Anfang
      float brightness = (float)i/DOTS;
      uint16_t c = liste ? 
        (brightness>0.5?0x0088FF:0x0044AA) : 
        (brightness>0.5?0x00CC44:0x004422);
      tft.fillCircle(px, BY+4, 3, c);
    } else {
      // Inaktive Punkte — sehr dunkel
      tft.fillCircle(px, BY+4, 2, 0x111111);
    }
  }
  // Sekunden klein in der Ecke
  int sl=(int)((total-elapsed)/1000)+1; if(sl<0)sl=0;
  tft.setTextSize(1);
  tft.setTextColor(0x333333);
  tft.fillRect(200,BY-1,40,12,TFT_BLACK);
  tft.setCursor(205,BY+4); tft.print(sl); tft.print("s");
}
void clearScreen(){
  tft.fillScreen(TFT_BLACK);
  if(!tftReady) return;
  tft.startWrite();
  tft.writecommand(0x2B);
  tft.writedata(0x00); tft.writedata(0x50);
  tft.writedata(0x01); tft.writedata(0x3F);
  tft.endWrite();
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
    verbundenesWlan=WLAN_LIST[bestIdx][0];
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
  prefs.putFloat("lat",cfg_lat); prefs.putFloat("lon",cfg_lon);
  addLog("[GEO] "+String(doc["city"].as<String>())+" ("+String(la,3)+","+String(lo,3)+")");
  return true;
}

// ============================================================
// ZRH-FLUGPLAN (kostenlos, einmal pro Zyklus)
// ============================================================
#define MAX_ZRH 120
ZrhFlug zrhFlights[MAX_ZRH];
int zrhAnzahl = 0;

//bool ladeZrhFlugplan(){
 // Serial.println("Starte ZRH Flugplan...");
  //zrhAnzahl=0;
  //HTTPClient http; http.setTimeout(10000);
  //const char* richt[2]={"dep","arr"};
  //for(int r=0;r<2;r++){
//   String url="https://www.flughafen-zuerich.ch/api/timetable/";
//    url+=richt[r]; url+="?lang=de&tab="; url+=richt[r];
//    http.begin(url);
//    http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
//    http.addHeader("Accept","application/json");
//    http.addHeader("X-Requested-With","XMLHttpRequest");
//    http.addHeader("User-Agent","Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36");
//    if(http.GET()!=200){ 
//      Serial.println("ZRH HTTP Fehler: " + String(http.GET()));
//      http.end(); continue; 
//    }
//    Serial.println("ZRH HTTP OK - " + String(richt[r]));
//    String payload = http.getString();
//  http.end();
//  Serial.println("JSON Groesse: " + String(payload.length()));
//  DynamicJsonDocument filter(64);
//  filter["states"] = true;
//  DynamicJsonDocument doc(16384);
//  DeserializationError derr=deserializeJson(doc,payload,DeserializationOption::Filter(filter));
//    if(derr){
//   Serial.println("ZRH JSON Fehler: " + String(derr.c_str()));
//      continue;
//    }
//    JsonArray fl=doc["flights"];
//   if(fl.isNull()) continue;
//    for(JsonObject f:fl){
 //     if(zrhAnzahl>=MAX_ZRH) break;
 //     String al=f["airline"].as<String>(); al.trim();
//      String nr=f["flightNumber"].as<String>(); nr.trim();
//      if(al.length()==0||nr.length()==0) continue;
//      String cs=al+nr; cs.toUpperCase();
 //     String iata=f["iataCode"].as<String>(); iata.trim(); iata.toUpperCase();
 //     if(iata.length()<3) continue;
 //     ZrhFlug z; z.callsign=cs;
//      if(r==0){z.von="ZRH";z.nach=iata;}
//      else    {z.von=iata; z.nach="ZRH";}
//      zrhFlights[zrhAnzahl++]=z;
//    }
 // }
 // Serial.println("ZRH Flugplan fertig: " + String(zrhAnzahl) + " Fluege");
//  addLog("[ZRH] "+String(zrhAnzahl)+" Fluege geladen");
 // return zrhAnzahl>0;
//}

// ============================================================
// FLIGHTAWARE (nur wenn Cache + ZRH nichts liefern)
// ============================================================
bool holeRouteFlightAware(String cs,String &v,String &n){
  Serial.println("FA Anfrage: " + cs);
  if(fa_credits_total-fa_credits_used<=0){
    Serial.println("FA: keine Credits mehr!");
    return false;
  }
  HTTPClient http;
  http.begin("https://aeroapi.flightaware.com/aeroapi/flights/"+cs);
  http.addHeader("x-apikey",FA_API_KEY);
  http.setTimeout(8000);
  int faCode = http.GET();
  Serial.println("FA HTTP Code: " + String(faCode));
  if(faCode!=200){ http.end(); return false; }
  String faPayload = http.getString();
  http.end();
  Serial.println("FA Response: " + faPayload.substring(0,200));
  DynamicJsonDocument doc(8192);
  DeserializationError ferr=deserializeJson(doc,faPayload);
  Serial.println("FA JSON Fehler: " + String(ferr.c_str()));
  JsonArray fl=doc["flights"];
  Serial.println("FA flights null: " + String(fl.isNull()));
  if(fl.isNull()||fl.size()==0) return false;
  Serial.println("FA flights size: " + String(fl.size()));
  v=fl[0]["origin"]["code_iata"].as<String>();
  n=fl[0]["destination"]["code_iata"].as<String>();
  Serial.println("FA origin: " + v + " nach: " + n);
  fa_credits_used++;
  prefs.putInt("fa_used",fa_credits_used);
  addLog("[FA] "+cs+" -> "+v+">"+n+" (-1 Cr)");
  return (v.length()>0 && n.length()>0);
}

// OpenSky Flights API - kostenlos, nutzt icao24
bool holeRouteOpenSky(String icao24, String cs, String &v, String &n){
  if(icao24.length()==0) return false;
  icao24.toLowerCase();
  // Zeitfenster: letzte 2 Stunden
  unsigned long now = millis()/1000 + 1700000000; // Annäherung
  unsigned long begin = now - 7200;
  String url = "https://opensky-network.org/api/flights/aircraft?icao24="+icao24+"&begin="+String(begin)+"&end="+String(now);
  Serial.println("OSK URL: " + url.substring(0,60)+"...");
  HTTPClient http; 
  http.begin(url); 
  http.setTimeout(8000);
  http.setAuthorization("DEIN_OPENSKY_USERNAME", "DEIN_OPENSKY_PASSWORT");
  int code = http.GET();
  Serial.println("OSK Code: " + String(code));
  if(code!=200){ http.end(); return false; }
  String payload = http.getString();
  http.end();
  Serial.println("OSK Response: " + payload.substring(0,150));
  if(payload.length()<10) return false;
  DynamicJsonDocument doc(4096);
  DeserializationError err = deserializeJson(doc,payload);
  if(err) return false;
  JsonArray arr = doc.as<JsonArray>();
  if(arr.isNull()||arr.size()==0) return false;
  // Neuesten Flug nehmen
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

bool sucheZrhFlugplan(String cs,String &v,String &n){
  cs.trim(); cs.toUpperCase();
  for(int i=0;i<zrhAnzahl;i++)
    if(zrhFlights[i].callsign==cs){v=zrhFlights[i].von;n=zrhFlights[i].nach;return true;}
  return false;
}

int holeRoute(String cs, String icao24, String &v, String &n){
  if(sucheCache(cs,v,n))            return 1;
  if(sucheZrhFlugplan(cs,v,n)){
    speichereCache(cs,v,n); stat_zrh_heute++;
    addLog("[ZRH] "+cs+" -> "+v+">"+n);
    return 2;
  }
  if(holeRouteOpenSky(icao24,cs,v,n)){ speichereCache(cs,v,n); stat_osk_heute++; return 4; }
  if(holeRouteFlightAware(cs,v,n)){ speichereCache(cs,v,n); stat_fa_heute++; return 3; }
  return 0;
}
// ============================================================
// DISPLAY: HEADER + LISTE + RADAR
// ============================================================
void zeichneHeader(int nF,const char* modus){
  tft.fillRect(0,0,240,18,TFT_NAVY);
  tft.setTextSize(1);
  tft.setTextColor(TFT_CYAN);   tft.setCursor(4,11);   tft.print("ESP-Radar");
  tft.setTextColor(0x9999FF);   tft.setCursor(108,11);
  tft.print(nF); tft.print(" Flieger");
  tft.setTextColor(TFT_GREEN);  tft.setCursor(195,11); tft.print(modus);
  tft.fillRect(0,18,240,12,0x0808);
  int avail=fa_credits_total-fa_credits_used;
  int pct=(avail*100)/fa_credits_total;
  uint16_t col=pct>30?TFT_GREEN:pct>10?TFT_ORANGE:TFT_RED;
  tft.setTextColor(0x5555); tft.setCursor(4,27); tft.print("FA:");
  tft.fillRect(24,20,100,8,0x2222);
  tft.fillRect(24,20,pct,8,col);
  tft.setTextColor(col); tft.setCursor(130,27); tft.print(avail);
  tft.setTextColor(0x5555); tft.print("/"); tft.print(fa_credits_total); tft.print(" Credits");
}

#define LIST_PRO_SEITE 4
#define SEITEN_MS      15000UL   // 15s pro Listenseite

#define LIST_PRO_SEITE 5

void zeigeListeSeite(Flugzeug liste[],int anzahl,bool neuesGeholt,int seite){
  clearScreen();
  int seiten = max(1, (anzahl + LIST_PRO_SEITE - 1) / LIST_PRO_SEITE);
  if(seiten > 1){
    char modBuf[8]; sprintf(modBuf,"L%d/%d",seite+1,seiten);
    zeichneHeader(anzahl, modBuf);
  } else {
    zeichneHeader(anzahl,"LIST");
  }
  int y=32;
  if(neuesGeholt && seite==0){
    tft.fillRect(0,y,240,12,0x301000);
    tft.setTextColor(TFT_ORANGE); tft.setCursor(4,y+9);
    tft.print("-1 Credit");
    y+=12;
  }
  tft.setTextColor(0x3344);
  tft.setCursor(4,y+7);   tft.print("CALLSIGN");
  tft.setCursor(86,y+7);  tft.print("DIST");
  tft.setCursor(116,y+7); tft.print("ALT");
  tft.setCursor(148,y+7); tft.print("ROUTE");
  y+=14;
  int von = seite * LIST_PRO_SEITE;
  int bis = min(anzahl, von + LIST_PRO_SEITE);
  for(int i=von;i<bis;i++){
    Flugzeug &f=liste[i];
    int row=i-von;
    tft.fillRect(0,y,240,30,row%2==0?0x050510:TFT_BLACK);
    // Callsign
    tft.setTextColor(TFT_WHITE); tft.setCursor(4,y+8);
    tft.print(f.callsign.substring(0,8));
    // Distanz
    tft.setTextColor(0x6688FF); tft.setCursor(86,y+8);
    tft.print((int)f.distanz_km); tft.print("km");
    // Hoehe
    tft.setTextColor(0x44CCAA); tft.setCursor(116,y+8);
    tft.print(String(f.hoehe_m/1000.0,1)); tft.print("k");
    // Route
    if(f.von.length()>0 && f.nach.length()>0){
      uint16_t rc=f.routeQuelle==2?TFT_GREEN:f.routeQuelle==3?TFT_YELLOW:f.routeQuelle==4?0x00CCFF:0xAAAA;
      tft.setTextColor(rc); tft.setCursor(148,y+8);
      tft.print(f.von.substring(0,3));
      tft.setTextColor(0x4444); tft.print(">");
      tft.setTextColor(rc);
      tft.print(f.nach.substring(0,3));
    } else {
      tft.setTextColor(0x3333); tft.setCursor(148,y+8); tft.print("---");
    }
    // Geschwindigkeit 2. Zeile
    tft.setTextColor(0x333333); tft.setCursor(4,y+20);
    tft.print((int)f.geschw_kmh); tft.print(" km/h");
    // Airline Name 2. Zeile
    tft.setTextColor(0x224422); tft.setCursor(60,y+20);
    tft.print(holeAirlineName(f.callsign));
    y+=30;
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
  // Mittelpunkt (du)
  tft.fillCircle(cx,cy,4,TFT_BLUE);
  tft.setTextColor(0x2255FF); tft.setCursor(cx+6,cy-3); tft.print("DU");
  // Flugzeuge
    for(int i=0;i<anzahl;i++){
    if(liste[i].hoehe_m < 500) continue;  // nur über 500m
    float dLat=liste[i].lat-LAT;
    float dLon=(liste[i].lon-LON)*cos(gradToRad(LAT));
    float dKm=berechnDistanz(LAT,LON,liste[i].lat,liste[i].lon);
    if(dKm>RADIUS_KM) continue;
    float scale=(float)cr/RADIUS_KM;
    int px=cx+(int)(dLon*111.0*scale);
    int py=cy-(int)(dLat*111.0*scale);
    px=constrain(px,5,234); py=constrain(py,22,228);
    tft.fillCircle(px,py,3,TFT_GREEN);
    tft.setTextColor(0x226622); tft.setCursor(px+5,py-3);
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

  for(int i=0;i<anzahl;i++){
    if(liste[i].hoehe_m <= 0 || liste[i].geschw_kmh <= 0) continue;
    float score = liste[i].geschw_kmh / (liste[i].hoehe_m + 1.0);
    bool vonZRH = liste[i].von == "ZRH";
    bool nachZRH = liste[i].nach == "ZRH";
    bool hatRoute = liste[i].von.length()>0 && liste[i].nach.length()>0;
    if(hatRoute){
      if(vonZRH && score > startScore){ startScore = score; startIdx = i; }
      if(nachZRH && i != startIdx && score > landScore){ landScore = score; landIdx = i; }
    } else {
      if(startIdx < 0 && score > 0.5){ startIdx = i; }
      if(landIdx < 0 && i != startIdx && liste[i].hoehe_m < 3000){ landIdx = i; }
    }
  }

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
    // Airline
    tft.setTextSize(1); tft.setTextColor(0x4488CC);
    tft.setCursor(50, 50); tft.print(airlineName);
    // Route
    if(f.von.length()>0 && f.nach.length()>0){
      uint16_t rc=f.routeQuelle==2?TFT_GREEN:f.routeQuelle==3?TFT_YELLOW:0x888800;
      tft.setTextColor(rc);
      tft.setCursor(50, 62); tft.print(f.von+" > "+f.nach);
    }
    // Details
    tft.setTextColor(0x44CCAA); tft.setCursor(4,78); tft.print("ALT");
    tft.setTextColor(TFT_WHITE); tft.setCursor(4,88); tft.print(String(f.hoehe_m/1000.0,1)); tft.print("km");
    tft.setTextColor(0x6688FF); tft.setCursor(80,78); tft.print("DIST");
    tft.setTextColor(TFT_WHITE); tft.setCursor(80,88); tft.print((int)f.distanz_km); tft.print("km");
    tft.setTextColor(0xFF8844); tft.setCursor(160,78); tft.print("SPD");
    tft.setTextColor(TFT_WHITE); tft.setCursor(160,88); tft.print((int)f.geschw_kmh);
  } else {
    tft.setTextColor(0x333333); tft.setTextSize(1);
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
    // Airline
    tft.setTextSize(1); tft.setTextColor(0x4488CC);
    tft.setCursor(50, 139); tft.print(airlineName);
    // Route
    if(f.von.length()>0 && f.nach.length()>0){
      uint16_t rc=f.routeQuelle==2?TFT_GREEN:f.routeQuelle==3?TFT_YELLOW:0x888800;
      tft.setTextColor(rc);
      tft.setCursor(50, 151); tft.print(f.von+" > "+f.nach);
    }
    // Details
    tft.setTextColor(0x44CCAA); tft.setCursor(4,167); tft.print("ALT");
    tft.setTextColor(TFT_WHITE); tft.setCursor(4,177); tft.print(String(f.hoehe_m/1000.0,1)); tft.print("km");
    tft.setTextColor(0x6688FF); tft.setCursor(80,167); tft.print("DIST");
    tft.setTextColor(TFT_WHITE); tft.setCursor(80,177); tft.print((int)f.distanz_km); tft.print("km");
    tft.setTextColor(0xFF8844); tft.setCursor(160,167); tft.print("SPD");
    tft.setTextColor(TFT_WHITE); tft.setCursor(160,177); tft.print((int)f.geschw_kmh);
  } else {
    tft.setTextColor(0x333333); tft.setTextSize(1);
    tft.setCursor(50, 158); tft.print("Keine Landung erkannt");
  }

  addLog("[SPT] Start:"+String(startIdx>=0?liste[startIdx].callsign:"---")+" Land:"+String(landIdx>=0?liste[landIdx].callsign:"---"));
}

// ============================================================
// KONFIGURATION LADEN / SPEICHERN
// ============================================================
void ladeKonfig(){
  prefs.begin("esp_radar",false);
  fa_credits_used  = prefs.getInt("fa_used",   0);
  cfg_lat          = prefs.getFloat("lat",      47.3769);
  cfg_lon          = prefs.getFloat("lon",       8.5417);
  cfg_radius       = prefs.getFloat("radius",   50.0);
  cfg_liste_sek    = prefs.getInt("liste_sek",  30);
  cfg_radar_sek    = prefs.getInt("radar_sek",  10);
  cfg_credits_tot  = prefs.getInt("cr_tot",    100);
  cfg_spot_sek     = prefs.getInt("spot_sek",   10);
  fa_credits_total = cfg_credits_tot;
  LISTE_MS=(unsigned long)cfg_liste_sek*1000;
  RADAR_MS=(unsigned long)cfg_radar_sek*1000;
  SPOT_MS =(unsigned long)cfg_spot_sek *1000;
  // WLAN aus Flash laden (Fallback auf hardcoded Liste)
  for(int i=0;i<5;i++){
    String ks="w"+String(i)+"s", kp="w"+String(i)+"p";
    wlan_ssid[i]=prefs.getString(ks.c_str(), String(WLAN_LIST[i][0]));
    wlan_pass[i]=prefs.getString(kp.c_str(), String(WLAN_LIST[i][1]));
  }
  // Cache aus Flash laden
  cacheAnzahl=prefs.getInt("cAnz",0);
  for(int i=0;i<cacheAnzahl;i++){
    String key="c"+String(i);
    cache[i].callsign=prefs.getString((key+"s").c_str(),"");
    cache[i].von     =prefs.getString((key+"v").c_str(),"");
    cache[i].nach    =prefs.getString((key+"n").c_str(),"");
  }
}

void speichereKonfig(){
  prefs.putFloat("lat",     cfg_lat);
  prefs.putFloat("lon",     cfg_lon);
  prefs.putFloat("radius",  cfg_radius);
  prefs.putInt("liste_sek", cfg_liste_sek);
  prefs.putInt("radar_sek", cfg_radar_sek);
  prefs.putInt("spot_sek",  cfg_spot_sek);
  prefs.putInt("cr_tot",    cfg_credits_tot);
  fa_credits_total=cfg_credits_tot;
  LISTE_MS=(unsigned long)cfg_liste_sek*1000;
  RADAR_MS=(unsigned long)cfg_radar_sek*1000;
  SPOT_MS =(unsigned long)cfg_spot_sek *1000;
}

void speichereWlan(){
  for(int i=0;i<5;i++){
    String ks="w"+String(i)+"s", kp="w"+String(i)+"p";
    prefs.putString(ks.c_str(), wlan_ssid[i]);
    prefs.putString(kp.c_str(), wlan_pass[i]);
  }
}

void speichereCache(String cs,String v,String n){
  if(v.length()==0 || n.length()==0) return; // Keine leeren Routen speichern
  cs=cs.substring(0,6);
  for(int i=0;i<cacheAnzahl;i++)
    if(cache[i].callsign==cs) return;
  if(cacheAnzahl<MAX_CACHE){
    cache[cacheAnzahl++]={cs,v,n};
    String key="c"+String(cacheAnzahl-1);
    prefs.putString((key+"s").c_str(),cs);
    prefs.putString((key+"v").c_str(),v);
    prefs.putString((key+"n").c_str(),n);
    prefs.putInt("cAnz",cacheAnzahl);
  }
}

void handleResetCredits(){
  fa_credits_used=0; prefs.putInt("fa_used",0);
  cacheAnzahl=0; prefs.putInt("cAnz",0);
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
        prefs.putString((key+"s").c_str(),cs);
        prefs.putString((key+"v").c_str(),v);
        prefs.putString((key+"n").c_str(),n);
        imported++;
      }
    }
    pos=end+1;
  }
  prefs.putInt("cAnz",cacheAnzahl);
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
  tft.setTextColor(0x2244); tft.setCursor(4,42); tft.print("Lade ZRH-Flugplan...");
  //ladeZrhFlugplan();
  tft.fillRect(0,32,240,12,TFT_BLACK);
  tft.setTextColor(0x2244); tft.setCursor(4,42); tft.print("Lade Positionen...");
  float lamin=LAT-(RADIUS_KM/111.0), lamax=LAT+(RADIUS_KM/111.0);
  float lomin=LON-(RADIUS_KM/74.0),  lomax=LON+(RADIUS_KM/74.0);
  String url="https://opensky-network.org/api/states/all?lamin="+
    String(lamin,4)+"&lomin="+String(lomin,4)+
    "&lamax="+String(lamax,4)+"&lomax="+String(lomax,4);
  Serial.println("URL: " + url);
  HTTPClient http; http.begin(url); http.setTimeout(10000);
  int httpCode = http.GET();
  Serial.println("HTTP Code: " + String(httpCode));
  if(httpCode!=200){
    addLog("[SKY] HTTP: "+String(httpCode));
    http.end(); return;
  }
  String payload = http.getString();
  http.end();
  Serial.println("JSON Groesse: " + String(payload.length()));
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
    liste[anzahl].distanz_km=dist;
    liste[anzahl].lat=fla; liste[anzahl].lon=flo;
    liste[anzahl].neueRoute=false; liste[anzahl].routeQuelle=0;
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
  bool neuesGeholt=false;
  for(int i=0;i<min(anzahl,7);i++){
    String cs=liste[i].callsign;
    if(cs.length()==0) continue;
    String v,n;
    String icao24=holeIcao24(cs);
    int q=holeRoute(cs,icao24,v,n);
    Serial.println("Route " + cs + ": q=" + String(q) + " v=" + v + " n=" + n);
    if(q>0){ liste[i].von=v; liste[i].nach=n; liste[i].routeQuelle=q; }
    if(q==3||q==4){ liste[i].neueRoute=true; neuesGeholt=true; }
  }
  for(int i=0;i<anzahl;i++) letzteList[i]=liste[i];
  letzteAnzahl=anzahl;
  Serial.println("[SKY] " + String(anzahl) + " Flugzeuge gefunden");
  addLog("[SKY] "+String(anzahl)+" Flugzeuge im Radius");
  zeigeListeSeite(letzteList,letzteAnzahl,neuesGeholt,0);
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
    sc("<div class=sl>Anzeigedauer</div><div class=r2>");
    sc("<div class=f><label>Liste (Sek.)</label><input id=liste type=number min=10 max=300 value="); sc(String(cfg_liste_sek)); sc("></div>");
    sc("<div class=f><label>Radar (Sek.)</label><input id=radar type=number min=5 max=120 value="); sc(String(cfg_radar_sek)); sc("></div></div>");
    sc("<div class=sl>FlightAware</div>");
    sc("<div class=f><label>Credits (Limit)</label><input id=credits type=number min=0 max=9999 value="); sc(String(cfg_credits_tot)); sc("></div>");
    sc("<div class=bl><span>Verbraucht: "); sc(String(fa_credits_used)); sc("/"); sc(String(fa_credits_total)); sc("</span><span>"); sc(String(pct)); sc("%</span></div>");
    sc("<div class=bw><div class=bf style='width:"); sc(String(pct)); sc("%;background:"); sc(bCol); sc("'></div></div>");
    sc("<button class='btn bs' onclick='save()'>Speichern &amp; neu starten</button>");
    sc("<button class='btn br' onclick='resetCr()'>Credits auf 0 zuruecksetzen</button>");
    sc("<script>");
    sc("function toast(m,c){var e=document.getElementById('t');e.textContent=m;e.style.background=c||'#003a00';e.style.display='block';setTimeout(function(){e.style.display='none'},2500);}");
    sc("function save(){var b=new URLSearchParams({lat:document.getElementById('lat').value,lon:document.getElementById('lon').value,radius:document.getElementById('radius').value,liste:document.getElementById('liste').value,radar:document.getElementById('radar').value,credits:document.getElementById('credits').value});fetch('/save',{method:'POST',body:b}).then(function(){toast('Gespeichert!');setTimeout(function(){location.reload()},3500)}).catch(function(){toast('Fehler','#3a0000')});}");
    sc("function resetCr(){if(!confirm('Credits auf 0?'))return;fetch('/resetcredits').then(function(){toast('OK');setTimeout(function(){location.reload()},1500)});}");
    sc("function geo(){toast('Bestimme...');fetch('/geoloc').then(function(r){return r.json()}).then(function(d){document.getElementById('lat').value=d.lat.toFixed(4);document.getElementById('lon').value=d.lon.toFixed(4);toast('OK: '+d.city)}).catch(function(){toast('Fehler','#3a0000')});}");
    sc("</script>");
  } else if(activeTab==1){
    sc("<div class=sl>WLANs (bis zu 5)</div>");
    for(int i=0;i<5;i++){
      sc("<div class=r2 style='margin-bottom:5px'>");
      sc("<div class=f><input id=ss"+String(i)+" placeholder='SSID "+String(i+1)+"' value='"); sc(wlan_ssid[i]); sc("'></div>");
      sc("<div class=f><input id=sp"+String(i)+" type=password placeholder='Passwort' value='"); sc(wlan_pass[i]); sc("'></div>");
      sc("</div>");
    }
    sc("<button class='btn bs' onclick='saveW()'>Speichern &amp; neu starten</button>");
    sc("<script>");
    sc("function toast(m,c){var e=document.getElementById('t');e.textContent=m;e.style.background=c||'#003a00';e.style.display='block';setTimeout(function(){e.style.display='none'},2500);}");
    sc("function saveW(){var b=new URLSearchParams({ss0:document.getElementById('ss0').value,sp0:document.getElementById('sp0').value,ss1:document.getElementById('ss1').value,sp1:document.getElementById('sp1').value,ss2:document.getElementById('ss2').value,sp2:document.getElementById('sp2').value,ss3:document.getElementById('ss3').value,sp3:document.getElementById('sp3').value,ss4:document.getElementById('ss4').value,sp4:document.getElementById('sp4').value});fetch('/savewlan',{method:'POST',body:b}).then(function(){toast('Gespeichert!');setTimeout(function(){location.reload()},3500)}).catch(function(){toast('Fehler','#3a0000')});}");
    sc("</script>");
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
  if(webServer.hasArg("liste"))   cfg_liste_sek  =webServer.arg("liste").toInt();
  if(webServer.hasArg("radar"))   cfg_radar_sek  =webServer.arg("radar").toInt();
  if(webServer.hasArg("credits")) cfg_credits_tot=webServer.arg("credits").toInt();
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
  webServer.on("/reboot",      HTTP_GET,  handleReboot);
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
  Serial.begin(115200);
  delay(2000);
  Serial.println("START");

  tft.init();
  Serial.println("TFT init OK");

  tft.setRotation(1);
  clearScreen();
  Serial.println("TFT fill OK");

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
  Serial.println("TFT offset OK");

  tft.setTextColor(TFT_CYAN); tft.setTextSize(2);
  tft.setCursor(14,80); tft.println("ESP-Radar");
  tft.setTextColor(TFT_WHITE); tft.setTextSize(1);
  tft.setCursor(8,108); tft.println("v1.0 - ESP-Radar");
  Serial.println("TFT text OK");

  ladeKonfig();
  Serial.println("Konfig OK");

  WiFi.mode(WIFI_STA);
  if(!verbindeWlan()){
    Serial.println("WLAN FEHLER!");
    clearScreen();
    tft.setTextColor(TFT_RED); tft.setCursor(5,60);
    tft.println("Kein WLAN gefunden!");
    tft.setTextColor(TFT_WHITE); tft.setCursor(5,80);
    tft.println("Pruefe WLAN_LIST im Code.");
    while(true) delay(1000);
  }
  Serial.println("WLAN OK: " + WiFi.localIP().toString());

  starteWebServer();
  Serial.println("Webserver OK");
  bootTime=millis();

  clearScreen();
  tft.setTextColor(TFT_GREEN);  tft.setCursor(10,90);  tft.println("WLAN: "+verbundenesWlan);
  tft.setTextColor(TFT_WHITE);  tft.setCursor(10,108); tft.println("Webkonfig unter:");
  tft.setTextColor(TFT_CYAN);   tft.setTextSize(2);
  tft.setCursor(10,128); tft.println(WiFi.localIP().toString());
  tft.setTextSize(1);
  tft.setTextColor(0x5555); tft.setCursor(10,158); tft.println("(im Browser oeffnen)");
  Serial.println("IP angezeigt: " + WiFi.localIP().toString());

 float storedLat=prefs.getFloat("lat",0);
  if(storedLat==0){
    tft.setTextColor(TFT_YELLOW); tft.setCursor(10,175); tft.print("Bestimme Standort...");
    if(bestimmeStandort()){
      tft.fillRect(0,175,240,12,TFT_BLACK);
      tft.setTextColor(TFT_GREEN); tft.setCursor(10,175);
      tft.print("Standort OK ("); tft.print(String(cfg_lat,2)); tft.print(","); tft.print(String(cfg_lon,2)); tft.print(")");
    }
  }
  Serial.println("Standort: " + String(cfg_lat,4) + ", " + String(cfg_lon,4));

  delay(3000);
  Serial.println("Starte holeDaten...");
  holeDaten();
}

// ============================================================
// LOOP
// ============================================================
void loop(){
  // --- LISTE (mit Seitenumbruch alle 15s) ---
  istListe=true;
  int seiten = max(1, (letzteAnzahl + LIST_PRO_SEITE - 1) / LIST_PRO_SEITE);
  for(int seite=0; seite<seiten; seite++){
    addLog("[DSP] Liste Seite "+String(seite+1)+"/"+String(seiten));
    zeigeListeSeite(letzteList, letzteAnzahl, false, seite);
    wechselStart=millis();
    while(millis()-wechselStart<LISTE_MS){
      webServer.handleClient();
      zeichneTimerBalken(millis()-wechselStart,LISTE_MS,true);
      delay(50);
    }
  }
  // --- LADEBILDSCHIRM ---
  clearScreen();
  tft.setTextColor(0x2244); tft.setTextSize(1);
  tft.setCursor(80,100); tft.print("Lade Daten...");
  tft.setTextColor(0x111122); tft.setCursor(60,116);
  tft.print("OpenSky + FlightAware");
  webServer.handleClient();
  // --- DATEN HOLEN ---
  holeDaten();
  // --- RADAR ---
  wechselStart=millis(); istListe=false;
  addLog("[DSP] Radar - "+String(cfg_radar_sek)+"s");
  zeigeRadar(letzteList,letzteAnzahl);
  while(millis()-wechselStart<RADAR_MS){
    webServer.handleClient();
    zeichneTimerBalken(millis()-wechselStart,RADAR_MS,false);
    delay(50);
  }
  // --- SPOTLIGHT ---
  wechselStart=millis();
  addLog("[DSP] Spotlight - "+String(cfg_spot_sek)+"s");
  zeigeSpotlight(letzteList,letzteAnzahl);
  while(millis()-wechselStart<SPOT_MS){
    webServer.handleClient();
    zeichneTimerBalken(millis()-wechselStart,SPOT_MS,false);
    delay(50);
  }
}