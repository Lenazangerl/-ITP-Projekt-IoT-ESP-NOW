#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <WiFiManager.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <WebServer.h>
#include <UniversalTelegramBot.h>
#include <ArduinoJson.h>

// ---------------- HIER ANPASSEN ----------------
#define BOT_TOKEN "BOT-Token"
#define CHAT_ID "USER-ID"

// Wird benutzt, falls WLAN nicht verbunden wird.
#define FALLBACK_ESPNOW_CHANNEL 1

// Webinterface-Access-Point
const char* AP_SSID = "ESP32-Empfaenger";
const char* AP_PASS = "12345678";

// WiFiManager-Setup-Access-Point
// Wird nur geöffnet wenn es keine Verbindung zu Router gibt (Notfallplan) für die Konfiguration
const char* CONFIG_AP_SSID = "ESP32-Setup";
const char* CONFIG_AP_PASS = "12345678";

// ---------------- TELEGRAM ----------------
const unsigned long TELEGRAM_POLL_MS = 2000;
const unsigned long MOTION_COOLDOWN_MS = 30000;
// nur aktiv wenn /auto aktiviert ist 
const unsigned long STATUS_INTERVAL_MS = 300000;

// Merken ob schon gesendet wurde
bool alertsEnabled = true;
bool statusEnabled = false;

// Zeitstempel
unsigned long lastMotionAlertMs = 0;
unsigned long lastStatusSentMs = 0;
unsigned long lastTelegramPollMs = 0;
bool lastMotionState = false;

// ---------------- ESP-NOW PACKET ----------------
// Identifikationsschlüssel vom Sender (alles Ok = gleich)
#define PACKET_MAGIC 0xAABBCCDD
// Struktur abgleich, 2 ist wie eine Schablone da nur Bytes gesendet werden wenn sensoren sich 
// ändern ohne nummer Schablone falsch
#define PACKET_VERSION 2

// Paket Struktur definieren, keine künstlichen Leerzeichen einfügen = Paket so klein wie mögl.
typedef struct __attribute__((packed)) {
  // Buchstaben abgleich
  uint32_t magic;
  // Version
  uint8_t version;
  // Paketnummer
  uint32_t seq;
  // Zeitstempel
  uint32_t uptimeMs;

  // Werte des Pakets
  float distanceCm;
  float temperatureC;
  float humidityPct;
  uint16_t lightRaw;

  uint8_t motion;
  uint8_t tilt;
  uint8_t dark;

  uint8_t sleeping;
  uint8_t historySample;
  uint16_t secondsLeft;
// Name
} SensorPacket;

// ---------------- CURRENT DATA ----------------
// aktuell gültiges Live-Datenpaket
SensorPacket currentPacket; 
// True, sobald das erste Paket der Lebensdauer ankam
bool hasCurrentPacket = false;   
// Zeitstempel (Stoppuhr) des letzten erfolgreichen Empfangs
unsigned long lastReceiveMs = 0;      

// Zwischenablage (Postbox) für frisch empfangene Funkdaten
SensorPacket pendingPacket;  
// Signalisiert dem Hauptprogramm: "Neue Post im Zwischenspeicher!"
volatile bool hasPendingPacket = false;
// Digitales Türschloss gegen Datenkonflikte (Multitasking)
portMUX_TYPE packetMux = portMUX_INITIALIZER_UNLOCKED;

// Speichert MAC des Senders
uint8_t senderMacAddress[6] = {0};
// true wenn mac von Sender erkannt wurde
bool senderMacKnown = false;
// Merkt sich den Zustand (An/Aus) der LED im Webinterface
bool webLedState = true; 

// ---------------- HISTORY ----------------
// Anzahl der Speicherplätze für die Kurzzeit-Grafik (12 Plätze)
const int HOUR_BUCKETS = 12;
// Anzahl der Speicherplätze für die Langzeit-Grafik (168 Plätze)
const int WEEK_BUCKETS = 168;

// Intervall für Kurzzeit: Alle 5 Minuten ein Datenpunkt
const unsigned long HOUR_BUCKET_MS = 5UL * 60UL * 1000UL;
// Intervall für Langzeit: Jede Stunde (60 Min) ein Datenpunkt
const unsigned long WEEK_BUCKET_MS = 60UL * 60UL * 1000UL;

struct HistoryBucket {
  // True, wenn dieser Speicherplatz bereits echte Daten enthält                          
  bool valid;
  // Nummerierung
  uint32_t slot;
  // Zähler für Mittelwert
  uint16_t count;


  // Messungen
  float distanceCm;
  float temperatureC;
  float humidityPct;
  float lightRaw;

  uint16_t motionCount;
  uint16_t tiltCount;
  uint16_t darkCount;
};

// Speicher für die Grafik
HistoryBucket hourHistory[HOUR_BUCKETS];
HistoryBucket weekHistory[WEEK_BUCKETS];

// ---------------- OBJEKTE ----------------
// Erstellt den Webserver auf Standard-Port 80 (für die Webseite)
WebServer server(80); 
// Erstellt einen verschlüsselten WLAN-Client (für sichere SSL-Verbindungen)
WiFiClientSecure secureClient;   
// Erstellt den Telegram-Bot, der die sichere Verbindung nutzt
UniversalTelegramBot bot(BOT_TOKEN, secureClient); 

// ---------------- HTML (ERWEITERT UM DEN LED-SCHALTER) ----------------
// Startet den Webseiten-Text und lagert ihn platzsparend in den Flash-Speicher aus
const char INDEX_HTML[] PROGMEM = R"rawliteral(
<!doctype html>
<html lang="de">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<title>ESP32 Sensordaten</title>
<style>
body{margin:0;font-family:Arial,sans-serif;background:#f4f6f8;color:#17202a}
header{background:#1f2937;color:white;padding:16px 20px;display:flex;justify-content:space-between;align-items:center}
main{max-width:1000px;margin:auto;padding:18px}
.grid{display:grid;grid-template-columns:repeat(auto-fit,minmax(150px,1fr));gap:12px}
.card{background:white;border:1px solid #d9e0e7;border-radius:8px;padding:14px}
.label{font-size:13px;color:#607080}
.value{font-size:24px;font-weight:700;margin-top:6px}
.panel{background:white;border:1px solid #d9e0e7;border-radius:8px;padding:14px;margin-top:14px}
.controls{display:flex;gap:10px;flex-wrap:wrap;margin-bottom:12px}
select{padding:8px;border:1px solid #ccd5df;border-radius:6px;background:white}
canvas{width:100%;height:320px;border:1px solid #e1e7ee;border-radius:6px}
.small{font-size:13px;color:#607080;margin-top:8px}
.awake{color:#15803d}
.sleep{color:#2563eb}
.btn{padding:10px 16px;border:none;border-radius:6px;font-weight:700;cursor:pointer;color:white}
.btn-on{background:#16a34a}.btn-off{background:#dc2626}
</style>
</head>
<body>
<header>
<h2>ESP32 Sensordaten</h2>
<div>
<button id="ledBtn" class="btn btn-on" onclick="toggleLed()">Status-LED: AN</button>
</div>
</header>
<main>
<div class="grid">
<div class="card"><div class="label">Status</div><div class="value" id="mode">-</div></div>
<div class="card"><div class="label">Countdown</div><div class="value" id="countdown">-</div></div>
<div class="card"><div class="label">Bewegung</div><div class="value" id="motion">-</div></div>
<div class="card"><div class="label">Distanz</div><div class="value" id="distance">-</div></div>
<div class="card"><div class="label">Temperatur</div><div class="value" id="temperature">-</div></div>
<div class="card"><div class="label">Luftfeuchte</div><div class="value" id="humidity">-</div></div>
<div class="card"><div class="label">Neigung</div><div class="value" id="tilt">-</div></div>
<div class="card"><div class="label">Licht</div><div class="value" id="light">-</div></div>
</div>

<div class="panel">
<div class="controls">
<select id="metric">
<option value="temperatureC">Temperatur</option>
<option value="humidityPct">Luftfeuchte</option>
<option value="distanceCm">Distanz</option>
<option value="lightRaw">Licht Rohwert</option>
</select>
<select id="range">
<option value="hour">Letzte Stunde, alle 5 min</option>
<option value="week">Letzte Woche, jede Stunde</option>
</select>
</div>
<canvas id="chart" width="900" height="320"></canvas>
<div class="small" id="status">Warte auf Daten...</div>
</div>
</main>

<script>
const labels = {
  temperatureC: "Temperatur C",
  humidityPct: "Luftfeuchte %",
  distanceCm: "Distanz cm",
  lightRaw: "Licht Rohwert"
};

function fmt(v, unit){
  if(v === null || v === undefined || isNaN(v)) return "-";
  return Number(v).toFixed(1) + " " + unit;
}

async function loadCurrent(){
  const r = await fetch("/api/current");
  const d = await r.json();

  if(!d.online){
    document.getElementById("status").textContent = "Noch keine ESP-NOW Daten empfangen.";
    return;
  }

  const modeEl = document.getElementById("mode");
  modeEl.textContent = d.sleeping ? "Sleep" : "Awake";
  modeEl.className = "value " + (d.sleeping ? "sleep" : "awake");

  document.getElementById("countdown").textContent = d.secondsLeft + " s";
  document.getElementById("motion").textContent = d.motion ? "Ja" : "Nein";
  document.getElementById("distance").textContent = fmt(d.distanceCm, "cm");
  document.getElementById("temperature").textContent = fmt(d.temperatureC, "C");
  document.getElementById("humidity").textContent = fmt(d.humidityPct, "%");
  document.getElementById("tilt").textContent = d.tilt ? "Ja" : "Nein";
  document.getElementById("light").textContent = d.dark ? "Dunkel" : "Hell";

  // Ändert das Aussehen des Buttons basierend auf dem LED-Status des Senders
  const btn = document.getElementById("ledBtn");
  if(d.ledEnabled){
    btn.textContent = "Status-LED: AN";
    btn.className = "btn btn-on";
  } else {
    btn.textContent = "Status-LED: AUS";
    btn.className = "btn btn-off";
  }

  document.getElementById("status").textContent =
    (d.sleeping ? "Sender schlaeft" : "Sender ist wach") +
    " | Wechsel in " + d.secondsLeft +
    " s | Letztes Paket vor " + d.ageSec +
    " s | Paket #" + d.seq;
}

async function toggleLed(){
  const r = await fetch("/api/toggle-led", {method: "POST"});
  const d = await r.json();
  const btn = document.getElementById("ledBtn");
  if(d.ledEnabled){
    btn.textContent = "Status-LED: AN";
    btn.className = "btn btn-on";
  } else {
    btn.textContent = "Status-LED: AUS";
    btn.className = "btn btn-off";
  }
}

async function loadHistory(){
  const range = document.getElementById("range").value;
  const metric = document.getElementById("metric").value;
  const r = await fetch("/api/history?range=" + range);
  const data = await r.json();
  drawChart(data.points, metric, labels[metric]);
}

function drawChart(points, metric, title){
  const c = document.getElementById("chart");
  const ctx = c.getContext("2d");
  ctx.clearRect(0,0,c.width,c.height);

  const padL = 55, padR = 18, padT = 28, padB = 38;
  const w = c.width - padL - padR;
  const h = c.height - padT - padB;

  ctx.fillStyle = "#ffffff";
  ctx.fillRect(0,0,c.width,c.height);

  const vals = points.map(p => p[metric]).filter(v => v !== null && !isNaN(v));

  ctx.fillStyle = "#17202a";
  ctx.font = "15px Arial";
  ctx.fillText(title, padL, 18);

  if(vals.length < 1){
    ctx.fillStyle = "#607080";
    ctx.fillText("Noch keine historischen Durchschnittswerte.", padL, 155);
    return;
  }

  let min = Math.min(...vals);
  let max = Math.max(...vals);
  if(min === max){ min -= 1; max += 1; }

  const y = v => padT + h - ((v - min) / (max - min)) * h;
  const x = i => padL + (points.length <= 1 ? 0 : i * w / (points.length - 1));

  ctx.strokeStyle = "#d8e0e8";
  ctx.lineWidth = 1;
  ctx.beginPath();
  for(let i=0;i<=4;i++){
    const gy = padT + i*h/4;
    ctx.moveTo(padL, gy);
    ctx.lineTo(padL+w, gy);
  }
  ctx.stroke();

  ctx.fillStyle = "#607080";
  ctx.font = "12px Arial";
  ctx.fillText(max.toFixed(1), 6, padT+4);
  ctx.fillText(min.toFixed(1), 6, padT+h);

  ctx.strokeStyle = "#2563eb";
  ctx.lineWidth = 2;
  ctx.beginPath();

  let started = false;
  points.forEach((p,i)=>{
    const v = p[metric];
    if(v === null || isNaN(v)){
      started = false;
      return;
    }

    if(!started){
      ctx.moveTo(x(i), y(v));
      started = true;
    } else {
      ctx.lineTo(x(i), y(v));
    }
  });
  ctx.stroke();

  ctx.fillStyle = "#2563eb";
  points.forEach((p,i)=>{
    const v = p[metric];
    if(v === null || isNaN(v)) return;
    ctx.beginPath();
    ctx.arc(x(i), y(v), 3, 0, Math.PI*2);
    ctx.fill();
  });
}

async function refresh(){
  await loadCurrent();
  await loadHistory();
}

document.getElementById("metric").addEventListener("change", loadHistory);
document.getElementById("range").addEventListener("change", loadHistory);

refresh();
setInterval(refresh, 1000);
</script>
</body>
</html>
)rawliteral";

// ---------------- HELPERS ----------------
String boolText(bool value) {
  // gibt true und false als lesbaren Text zurücl
  return value ? "true" : "false";
}

String jsonFloat(float value, int digits) {
  // Funktion: Formatiert Kommazahlen sicher für das Webinterface (JSON-Format)
  // Schützt vor Abstürzen: Wenn der Sensor defekt ist (kein Wert), sende "null"
  if (isnan(value) || isinf(value)) return "null";
  return String(value, digits);
}

// ---------------- NETWORK SETUP MIT WIFI MANAGER ----------------
void setupNetwork() {
  // Schaltet das WLAN zuerst in den Client-Modus (Station Mode)
  WiFi.mode(WIFI_STA);
  // Deaktiviert jegliche Stromparmodus für einen stabilen Betrieb
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  WiFiManager wm;

  // Wenn keine gespeicherten WLAN-Daten vorhanden sind, startet der ESP32
  // das Setup-WLAN ESP32-Setup. Dort kann ein WLAN ausgewaehlt werden.
  wm.setConfigPortalTimeout(180); // Wird autom beendet nach 3min ohne Eingabe

  Serial.println("Starte WiFiManager...");
  Serial.print("Setup-WLAN: ");
  Serial.println(CONFIG_AP_SSID);

  // Versucht ins Heim-WLAN zu kommen; startet bei Fehlschlag das Setup-WLAN
  bool connected = wm.autoConnect(CONFIG_AP_SSID, CONFIG_AP_PASS);

  // Setzt den Funkkanal standardmäßig auf den Notfall-Kanal
  int channel = FALLBACK_ESPNOW_CHANNEL;

  // Wenn die Verbindung zum Heim-WLAN erfolgreich steht:
  if (connected && WiFi.status() == WL_CONNECTED) {
    channel = WiFi.channel(); // Holt den exakten Funkkanal, auf dem dein Router funkt

    Serial.println("WLAN verbunden.");
    Serial.print("WLAN IP: ");
    Serial.println(WiFi.localIP());
  } else {
    Serial.println("Kein WLAN verbunden. Telegram ist deaktiviert.");
  }

  // Danach laeuft der Empfaenger als WLAN-Client und als eigener Access Point.
  WiFi.mode(WIFI_AP_STA);
  // Stromsparmodus deaktivieren
  WiFi.setSleep(false);
  esp_wifi_set_ps(WIFI_PS_NONE);

  // Der AP wird auf dem gleichen Kanal gestartet, den auch ESP-NOW nutzt.
  WiFi.softAP(AP_SSID, AP_PASS, channel);

  Serial.print("AP IP: ");
  Serial.println(WiFi.softAPIP());

  Serial.print("STA MAC fuer Sender: ");
  Serial.println(WiFi.macAddress());

  Serial.print("AP MAC: ");
  Serial.println(WiFi.softAPmacAddress());

  Serial.print("WLAN Kanal fuer Sender: ");
  Serial.println(channel);

  secureClient.setInsecure();
}

// ---------------- TELEGRAM ----------------
// Wenn WLAN nicht verbunden ist
void sendTelegram(const String& text) {
  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("Telegram nicht gesendet: WLAN nicht verbunden");
    return;
  }

  bool ok = bot.sendMessage(CHAT_ID, text, "");

  Serial.print("Telegram sendMessage: ");
  Serial.println(ok ? "OK" : "FEHLER");
}

String buildTelegramStatus() {
  if (!hasCurrentPacket) {
    return "Noch keine Sensordaten empfangen.";
  }

  // Daten zusammensetzen
  String msg = "ESP32 Sensorstatus\n";
  msg += "Modus: ";
  msg += currentPacket.sleeping ? "Sleep\n" : "Awake\n";
  msg += "Countdown: " + String(currentPacket.secondsLeft) + " s\n\n";

  msg += "Temperatur: " + String(currentPacket.temperatureC, 1) + " C\n";
  msg += "Luftfeuchte: " + String(currentPacket.humidityPct, 1) + " %\n";
  msg += "Distanz: " + String(currentPacket.distanceCm, 1) + " cm\n";
  msg += "Licht: ";
  msg += currentPacket.dark ? "Dunkel\n" : "Hell\n";
  msg += "Bewegung: ";
  msg += currentPacket.motion ? "Ja\n" : "Nein\n";
  msg += "Neigung: ";
  msg += currentPacket.tilt ? "Ja\n" : "Nein\n";

  msg += "\nPaket #" + String(currentPacket.seq);
  msg += " | vor " + String((millis() - lastReceiveMs) / 1000) + " s";

  return msg;
}

void handleTelegram() {
  // WLAN nicht connected
  if (WiFi.status() != WL_CONNECTED) return;

  // nur positive Werte, aktuelle Zeit seit Start (Laufzeit)
  unsigned long now = millis();

  // Prüft, ob das Wartezeit-Intervall für die Abfrage abgelaufen ist
  if (now - lastTelegramPollMs >= TELEGRAM_POLL_MS) {
    // Zeitstempel
    lastTelegramPollMs = now;

    // Holt neue Nachrichten vom Telegram-Server
    int numMessages = bot.getUpdates(bot.last_message_received + 1);

    for (int i = 0; i < numMessages; i++) {
      String text = bot.messages[i].text;
      String chatId = bot.messages[i].chat_id;

      Serial.print("Telegram von ");
      Serial.print(chatId);
      Serial.print(": ");
      Serial.println(text);

      if (chatId != String(CHAT_ID)) {
        bot.sendMessage(chatId, "Nicht autorisiert. Deine Chat-ID ist: " + chatId, "");
        continue;
      }

      if (text == "/start" || text == "/help") {
        String help = "ESP32 Telegram Bot\n\n";
        help += "/status - aktuelle Sensordaten\n";
        help += "/alerts - Bewegungsalarm an/aus\n";
        help += "/auto - Auto-Status alle 5 min an/aus\n";
        help += "/help - Hilfe anzeigen";
        bot.sendMessage(CHAT_ID, help, "");
      } else if (text == "/status") {
        bot.sendMessage(CHAT_ID, buildTelegramStatus(), "");
      } else if (text == "/alerts") {
        alertsEnabled = !alertsEnabled;
        bot.sendMessage(CHAT_ID, alertsEnabled ? "Bewegungsalarm aktiviert." : "Bewegungsalarm deaktiviert.", "");
      } else if (text == "/auto") {
        statusEnabled = !statusEnabled;
        bot.sendMessage(CHAT_ID, statusEnabled ? "Auto-Status aktiviert." : "Auto-Status deaktiviert.", "");
      } else {
        bot.sendMessage(CHAT_ID, "Unbekannter Befehl. Tippe /help.", "");
      }
    }
  }

  if (!hasCurrentPacket) return;

  if (alertsEnabled) {
    bool currentMotion = currentPacket.motion;

    if (currentMotion && !lastMotionState && now - lastMotionAlertMs > MOTION_COOLDOWN_MS) {
      String alert = "Bewegung erkannt!\n";
      alert += "Distanz: " + String(currentPacket.distanceCm, 1) + " cm\n";
      alert += "Temperatur: " + String(currentPacket.temperatureC, 1) + " C\n";
      alert += "Neigung: ";
      alert += currentPacket.tilt ? "Ja" : "Nein";

      sendTelegram(alert);
      lastMotionAlertMs = now;
    }

    lastMotionState = currentMotion;
  }

  if (statusEnabled && now - lastStatusSentMs > STATUS_INTERVAL_MS) {
    sendTelegram(buildTelegramStatus());
    lastStatusSentMs = now;
  }
}

// ---------------- HISTORY HELPERS ----------------
// Funktion: Leert einen Speicherbecher und weist ihm eine Nummer zu
void resetBucket(HistoryBucket &b, uint32_t slot) {
  // Löscht die alten Durchschnittswerte
  b.valid = true;
  b.slot = slot;
  b.count = 0;

  b.distanceCm = 0;
  b.temperatureC = 0;
  b.humidityPct = 0;
  b.lightRaw = 0;

  b.motionCount = 0;
  b.tiltCount = 0;
  b.darkCount = 0;
}

// Funktion: Rechnet ein neues Paket in die Mittelwerte mit ein
void addToBucket(HistoryBucket &b, SensorPacket &p) {
  b.count++;

  b.distanceCm += (p.distanceCm - b.distanceCm) / b.count;
  b.temperatureC += (p.temperatureC - b.temperatureC) / b.count;
  b.humidityPct += (p.humidityPct - b.humidityPct) / b.count;
  b.lightRaw += ((float)p.lightRaw - b.lightRaw) / b.count;

  if (p.motion) b.motionCount++;
  if (p.tilt) b.tiltCount++;
  if (p.dark) b.darkCount++;
}

// Funktion: Sortiert Daten in das Stunden- oder Wochen-Array ein
void addToHistoryArray(HistoryBucket buckets[], int bucketCount, unsigned long bucketMs, SensorPacket &p) {
  uint32_t slot = millis() / bucketMs;
  int index = slot % bucketCount;

  if (!buckets[index].valid || buckets[index].slot != slot) {
    resetBucket(buckets[index], slot);
  }

  addToBucket(buckets[index], p);
}

void addToHistory(SensorPacket &p) {
  addToHistoryArray(hourHistory, HOUR_BUCKETS, HOUR_BUCKET_MS, p);
  addToHistoryArray(weekHistory, WEEK_BUCKETS, WEEK_BUCKET_MS, p);
}

// ---------------- ESP-NOW RECEIVE ----------------
void onDataRecv(const esp_now_recv_info_t *info, const uint8_t *incomingData, int len) {
  // Bricht ab, wenn das Paket eine falsche Größe hat (Schutz vor Störungen)
  if (len != sizeof(SensorPacket)) return;

  // Erstellt eine leere Struktur-Variable für die Sensordaten
  SensorPacket packet;
  // Kopiert die rohen Funk-Bytes eins zu eins in unsere Struktur-Variable
  memcpy(&packet, incomingData, sizeof(packet));

  // Bricht ab wenn Buchstaben nicht stimmen
  if (packet.magic != PACKET_MAGIC) return;
  // Bricht ab wenn Version nicht stimmt
  if (packet.version != PACKET_VERSION) return;

  // Wenn die MAC-Adresse des Senders bisher noch unbekannt war:
  if (!senderMacKnown) {
    // Kopiert die 6 Bytes der Sender-Adresse aus den Funk-Metadaten
    memcpy(senderMacAddress, info->src_addr, 6);
    // auf true setzen um nochmal zu vermeiden
    senderMacKnown = true;
  }

  // Sperrt kurzzeitig andere Prozesse aus (Multitasking-Schutz)
  portENTER_CRITICAL(&packetMux);
  // Kopiert das frisch empfangene Paket sicher in die Zwischenablage
  pendingPacket = packet;
  // Setzt die Flagge: "Es liegt ein neues Paket zur Verarbeitung bereit"
  hasPendingPacket = true;
  // Gibt die Ressourcen wieder frei für den normalen Programmablauf
  portEXIT_CRITICAL(&packetMux);
}

void processPendingPacket() {
  if (!hasPendingPacket) return;

  SensorPacket packet;

  portENTER_CRITICAL(&packetMux);
  packet = pendingPacket;
  hasPendingPacket = false;
  portEXIT_CRITICAL(&packetMux);

  currentPacket = packet;
  hasCurrentPacket = true;
  lastReceiveMs = millis();

  if (packet.historySample) {
    addToHistory(packet);
  }

  Serial.print("Paket #");
  Serial.print(packet.seq);
  Serial.print(packet.sleeping ? " Sleep " : " Awake ");
  Serial.print("Rest: ");
  Serial.print(packet.secondsLeft);
  Serial.print("s Temp: ");
  Serial.print(packet.temperatureC);
  Serial.print(" Dist: ");
  Serial.println(packet.distanceCm);
}

// ---------------- API ----------------
void handleRoot() {
  server.send_P(200, "text/html", INDEX_HTML);
}

void handleCurrent() {
  if (!hasCurrentPacket) {
    server.send(200, "application/json", "{\"online\":false}");
    return;
  }

  String json = "{";
  json += "\"online\":true,";
  json += "\"seq\":" + String(currentPacket.seq) + ",";
  json += "\"ageSec\":" + String((millis() - lastReceiveMs) / 1000) + ",";
  json += "\"sleeping\":" + boolText(currentPacket.sleeping) + ",";
  json += "\"secondsLeft\":" + String(currentPacket.secondsLeft) + ",";
  json += "\"distanceCm\":" + jsonFloat(currentPacket.distanceCm, 1) + ",";
  json += "\"temperatureC\":" + jsonFloat(currentPacket.temperatureC, 1) + ",";
  json += "\"humidityPct\":" + jsonFloat(currentPacket.humidityPct, 1) + ",";
  json += "\"lightRaw\":" + String(currentPacket.lightRaw) + ",";
  json += "\"motion\":" + boolText(currentPacket.motion) + ",";
  json += "\"tilt\":" + boolText(currentPacket.tilt) + ",";
  json += "\"dark\":" + boolText(currentPacket.dark) + ",";
  // NEU: Schickt den aktuellen LED-Zustand ans Webinterface raus
  json += "\"ledEnabled\":" + boolText(webLedState);
  json += "}";

  server.send(200, "application/json", json);
}

// NEU: Endpunkt um die LED umzuschalten und den Befehl per Funk zurückzuschicken
void handleToggleLed() {
  webLedState = !webLedState;

  if (senderMacKnown) {
    // Falls der Sender noch kein eingetragener Peer im Empfänger ist, fügen wir ihn hinzu
    if (!esp_now_is_peer_exist(senderMacAddress)) {
      esp_now_peer_info_t peerInfo = {};
      memcpy(peerInfo.peer_addr, senderMacAddress, 6);
      peerInfo.channel = WiFi.channel();
      peerInfo.encrypt = false;
      esp_now_add_peer(&peerInfo);
    }

    // Sendet 1 (= AN) oder 0 (= AUS) via ESP-NOW zurück an den Sender
    uint8_t command = webLedState ? 1 : 0;
    esp_now_send(senderMacAddress, &command, 1);
    Serial.print("ESP-NOW Befehl an Sender abgesetzt: ");
    Serial.println(command);
  } else {
    Serial.println("Fehler: Sender-MAC noch unbekannt. Warte auf erstes Paket.");
  }

  String response = "{\"ledEnabled\":" + boolText(webLedState) + "}";
  server.send(200, "application/json", response);
}

void appendBucketJson(String &json, HistoryBucket &b, String label) {
  json += "{";
  json += "\"label\":\"" + label + "\",";

  if (!b.valid || b.count == 0) {
    json += "\"distanceCm\":null,";
    json += "\"temperatureC\":null,";
    json += "\"humidityPct\":null,";
    json += "\"lightRaw\":null,";
    json += "\"motion\":null,";
    json += "\"tilt\":null,";
    json += "\"dark\":null";
  } else {
    json += "\"distanceCm\":" + jsonFloat(b.distanceCm, 1) + ",";
    json += "\"temperatureC\":" + jsonFloat(b.temperatureC, 1) + ",";
    json += "\"humidityPct\":" + jsonFloat(b.humidityPct, 1) + ",";
    json += "\"lightRaw\":" + jsonFloat(b.lightRaw, 0) + ",";
    json += "\"motion\":" + boolText(b.motionCount > b.count / 2) + ",";
    json += "\"tilt\":" + boolText(b.tiltCount > b.count / 2) + ",";
    json += "\"dark\":" + boolText(b.darkCount > b.count / 2);
  }

  json += "}";
}

void sendHistoryJson(HistoryBucket buckets[], int bucketCount, unsigned long bucketMs, const char* unitLabel) {
  uint32_t currentSlot = millis() / bucketMs;
  uint32_t firstSlot = currentSlot >= (uint32_t)(bucketCount - 1) ? currentSlot - (bucketCount - 1) : 0;

  String json;
  json.reserve(18000);
  json = "{\"points\":[";

  for (uint32_t slot = firstSlot; slot <= currentSlot; slot++) {
    if (slot > firstSlot) json += ",";

    int index = slot % bucketCount;
    int age = currentSlot - slot;

    String label = age == 0 ? "jetzt" : "-" + String(age) + unitLabel;

    if (buckets[index].valid && buckets[index].slot == slot) {
      appendBucketJson(json, buckets[index], label);
    } else {
      HistoryBucket emptyBucket = {};
      appendBucketJson(json, emptyBucket, label);
    }
  }

  json += "]}";
  server.send(200, "application/json", json);
}

void handleHistory() {
  String range = server.arg("range");

  if (range == "week") {
    sendHistoryJson(weekHistory, WEEK_BUCKETS, WEEK_BUCKET_MS, "h");
  } else {
    sendHistoryJson(hourHistory, HOUR_BUCKETS, HOUR_BUCKET_MS, "x5min");
  }
}

void handleResetWifi() {
  server.send(200, "text/plain", "WLAN-Daten werden geloescht. ESP startet neu.");
  delay(1000);

  WiFiManager wm;
  wm.resetSettings();

  ESP.restart();
}

void setupServer() {
  server.on("/", handleRoot);
  server.on("/api/current", handleCurrent);
  server.on("/api/history", handleHistory);
  
  // NEU: Registriert den POST-Endpunkt für das UI-Element
  server.on("/api/toggle-led", HTTP_POST, handleToggleLed);

  // Damit kann das gespeicherte WLAN spaeter geloescht werden:
  // http://192.168.4.1/reset-wifi
  server.on("/reset-wifi", handleResetWifi);

  server.begin();
  Serial.println("Webserver gestartet");
}

// ---------------- SETUP / LOOP ----------------
void setup() {
  Serial.begin(115200);

  setupNetwork();

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Fehler");
    while (true);
  }

  esp_now_register_recv_cb(onDataRecv);

  setupServer();

  sendTelegram("ESP32 Empfaenger gestartet.\nBefehl: /status");
}

void loop() {
  processPendingPacket();
  server.handleClient();
  handleTelegram();
}
