#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include "DHT.h"

// ================================================================
// ESP-NOW KONFIGURATION
// ================================================================

// Dieser Kanal muss gleich sein wie der "WLAN Kanal fuer Sender",
// der beim Empfaenger im seriellen Monitor angezeigt wird.
#define ESPNOW_CHANNEL 11

// MAC-Adresse des Empfaenger-ESP32.
// Wichtig: Hier muss die STA MAC des Empfaengers stehen, nicht die AP MAC.
uint8_t receiverAddress[] = {0x00, 0x70, 0x07, 0x25, 0x77, 0x4C};

// Diese Werte dienen dazu, gueltige Datenpakete zu erkennen.
#define PACKET_MAGIC 0xAABBCCDD
// welche Version deiner Paketstruktur verwendet wird. Wichtig, falls du später das Datenpaket ändergeändert wird
// Dadurch könnten alte und neue Pakete unterschiedlich aufgebaut sein. Sender und Empfänger müssen dieselbe Version verwenden.
#define PACKET_VERSION 2

// Datenpaket, das per ESP-NOW an den Empfaenger gesendet wird.
// Diese Struktur muss beim Sender und Empfaenger exakt gleich sein.
// struct bedeutet: Mehrere Werte werden zu einem gemeinsamen Paket zusammengefasst.
// __attribute__((packed)) bedeutet: Die Daten werden ohne zusätzliche Lücken im Speicher gespeichert. 
// Dadurch wird das Paket genauso gesendet, wie es aufgebaut ist.
typedef struct __attribute__((packed)) {

  // Erkennungswert prüfung ob Paket zu meinem Projekt gehört
  uint32_t magic;
  // Version des Pakets. Sender und Empfänger müssen dieselbe Version verwenden.
  uint8_t version;
  // Paketnummer. Jedes gesendete Paket bekommt eine fortlaufende Nummer. 
  //Damit sieht man, ob Pakete ankommen und in welcher Reihenfolge.
  uint32_t seq;
  // Zeit seit Start des Senders in Millisekunden.
  uint32_t uptimeMs;

  // Messwerte
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

// SensorPacket ist dann der Name dieses Pakets.
} SensorPacket;

// Zähler für die Paketnummer, Startewert wird erhöht bei jedem Senden
uint32_t packetSeq = 0;

// Callback: Wird nach jedem Sendeversuch automatisch aufgerufen.
// Dadurch sieht man im seriellen Monitor, ob ESP-NOW erfolgreich war.
void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Sendung: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FEHLER");
}

// ================================================================
// OLED DISPLAY
// ================================================================

#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ================================================================
// PINBELEGUNG
// ================================================================

// Ultraschallsensor HC-SR04
#define TRIG 5
#define ECHO 18

// RGB LED
int redPin = 25;
int greenPin = 26;
int bluePin = 27;

// PIR Bewegungssensor und Buzzer
int pirPin = 2;
int buzzer = 13;

// DHT11 Temperatur- und Luftfeuchtigkeitssensor
#define DHTPIN 33
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// DHT11 wird nur alle 2 Sekunden gelesen,
// weil der Sensor keine sehr schnellen Messungen erlaubt.
const unsigned long DHT_INTERVAL = 2000;
unsigned long lastDhtReadMs = 0;
// Wenn false keine gültigen Messwerze
bool hasDhtValue = false;
// Not a Number wird überschrieben wenn Messung klappt
float lastTemp = NAN;
// Not a Number wird überschrieben wenn Messung klappt
float lastHum = NAN;

// Tilt-Sensor und Helligkeitssensor
int tiltPin = 32;
int lightPin = 34;

// ================================================================
// MESS- UND PAUSENMODUS
// ================================================================

// Der Sender misst 30 Sekunden und pausiert danach 30 Sekunden.
// unsigned ist für positive Werte
const unsigned long MEASURE_TIME = 30000;
const unsigned long SLEEP_TIME = 30000;

// Live-Daten werden ungefaehr einmal pro Sekunde gesendet.
const unsigned long SEND_INTERVAL = 1000;

bool sleepMode = false;
unsigned long phaseStart = 0;
unsigned long lastSendMs = 0;

// ================================================================
// VARIABLEN FUER MITTELWERTE
// ================================================================

float sumDistance = 0;
float sumTemp = 0;
float sumHum = 0;
long sumLight = 0;

long distanceSamples = 0;
long tempSamples = 0;
long humSamples = 0;
long lightSamples = 0;
long totalSamples = 0;

long pirHighSamples = 0;
long tiltHighSamples = 0;

float avgDistance = 0;
float avgTemp = 0;
float avgHum = 0;
int avgLight = 0;
int avgPir = LOW;
int avgTilt = LOW;

// ================================================================
// ULTRASCHALLMESSUNG
// ================================================================

float measureDistance() {
  float sum = 0;

  // Mehrere Messungen werden gemittelt, damit Ausreisser reduziert werden.
  for (int i = 0; i < 5; i++) {
    digitalWrite(TRIG, LOW);
    delayMicroseconds(5);

    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);

    long duration = pulseIn(ECHO, HIGH, 30000);
    
    // Umrechnung in cm
    float d = duration * 0.034 / 2;

    sum += d;
    delay(10);
  }

  return sum / 5;
}

// ================================================================
// DHT11 AUSLESEN
// ================================================================

void readDhtCached(float &t, float &h) {
  unsigned long now = millis();

  // DHT11 wird nur in einem sinnvollen Intervall neu gelesen.
  if (!hasDhtValue || now - lastDhtReadMs >= DHT_INTERVAL) {
    float newH = dht.readHumidity();
    float newT = dht.readTemperature();

    // Nur gueltige Werte uebernehmen.
    if (!isnan(newT)) lastTemp = newT;
    if (!isnan(newH)) lastHum = newH;

    if (!isnan(lastTemp) && !isnan(lastHum)) hasDhtValue = true;

    lastDhtReadMs = now;
  }

  t = lastTemp;
  h = lastHum;
}

// ================================================================
// RGB LED
// ================================================================

void setColor(int r, int g, int b) {
  ledcWrite(redPin, r);
  ledcWrite(greenPin, g);
  ledcWrite(bluePin, b);
}

// ================================================================
// ESP-NOW EINRICHTEN UND SENDEN
// ================================================================

void setupEspNow() {
  // arbeitet als Client
  WiFi.mode(WIFI_STA);
  // Kein Wlan schlafmodus
  WiFi.setSleep(false);
  // entfernt alte Verbindungen wie Router 
  WiFi.disconnect();

  // Stromsparmodus vom WLAN deaktivieren, damit ESP-NOW stabiler laeuft.
  esp_wifi_set_ps(WIFI_PS_NONE);

  // ESP-NOW muss auf demselben Kanal laufen wie der Empfaenger.
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  // Kanal geändert also wieder schließen
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Fehler");
    while (true);
  }

  // Ruft die Funktion auf wenn das Senden fertig ist
  esp_now_register_send_cb(onDataSent);

  // Empfaenger als Peer eintragen.
  esp_now_peer_info_t peerInfo = {};
  memcpy(peerInfo.peer_addr, receiverAddress, 6);
  peerInfo.channel = ESPNOW_CHANNEL;
  peerInfo.encrypt = false;

  if (esp_now_add_peer(&peerInfo) != ESP_OK) {
    Serial.println("ESP-NOW Peer Fehler");
    while (true);
  }

  Serial.print("Sender MAC: ");
  Serial.println(WiFi.macAddress());

  Serial.print("ESP-NOW Kanal: ");
  Serial.println(ESPNOW_CHANNEL);
}

void sendSensorPacket(bool sleeping, bool historySample, int secondsLeft,
                      int pir, float distance, int tilt, float t, float h, int light) {
  SensorPacket packet;

  packet.magic = PACKET_MAGIC;
  packet.version = PACKET_VERSION;
  packet.seq = packetSeq++;
  packet.uptimeMs = millis();

  packet.distanceCm = distance;
  packet.temperatureC = t;
  packet.humidityPct = h;
  packet.lightRaw = light;

  // 1 zB Bewegung 0 keine
  packet.motion = pir ? 1 : 0;
  packet.tilt = tilt ? 1 : 0;
  packet.dark = light > 3000 ? 1 : 0;

  // 1 Pause 0 Messen
  packet.sleeping = sleeping ? 1 : 0;

  // historySample ist nur bei den 30-Sekunden-Durchschnittswerten 1.
  // Der Empfaenger nutzt nur diese Pakete fuer den Graphen.
  packet.historySample = historySample ? 1 : 0;

  packet.secondsLeft = secondsLeft;

  esp_err_t result = esp_now_send(receiverAddress, (uint8_t *)&packet, sizeof(packet));

  Serial.print("esp_now_send Paket #");
  Serial.print(packet.seq);
  Serial.print(": ");
  Serial.println(result == ESP_OK ? "gestartet" : "FEHLER");
}

// ================================================================
// MITTELWERTE BILDEN
// ================================================================

void resetAverages() {
  sumDistance = 0;
  sumTemp = 0;
  sumHum = 0;
  sumLight = 0;

  distanceSamples = 0;
  tempSamples = 0;
  humSamples = 0;
  lightSamples = 0;
  totalSamples = 0;

  pirHighSamples = 0;
  tiltHighSamples = 0;
}

void addAverages(int pir, float distance, int tilt, float t, float h, int light) {
  totalSamples++;

  if (pir == HIGH) pirHighSamples++;
  if (tilt == HIGH) tiltHighSamples++;

  if (distance > 0) {
    sumDistance += distance;
    distanceSamples++;
  }

  if (!isnan(t)) {
    sumTemp += t;
    tempSamples++;
  }

  if (!isnan(h)) {
    sumHum += h;
    humSamples++;
  }

  sumLight += light;
  lightSamples++;
}

void finalizeAverages() {
  // Schutz gegen Divison durch 0 
  avgDistance = distanceSamples > 0 ? sumDistance / distanceSamples : 0;
  avgTemp = tempSamples > 0 ? sumTemp / tempSamples : 0;
  avgHum = humSamples > 0 ? sumHum / humSamples : 0;
  avgLight = lightSamples > 0 ? sumLight / lightSamples : 0;

  // Bei digitalen Sensoren wird geprueft, ob der Zustand in der Mehrheit
  // der Messungen aktiv war.
  avgPir = totalSamples > 0 && pirHighSamples > totalSamples / 2 ? HIGH : LOW;
  avgTilt = totalSamples > 0 && tiltHighSamples > totalSamples / 2 ? HIGH : LOW;
}

// ================================================================
// ZEIT UND PAUSENMODUS
// ================================================================

int remainingSeconds(unsigned long phaseTime) {

  // Start des Programs - Phase
  unsigned long elapsed = millis() - phaseStart;

  if (elapsed >= phaseTime) return 1;

  return (phaseTime - elapsed + 999) / 1000;
}

void sensorsOffForSleep() {
  noTone(buzzer);
  digitalWrite(TRIG, LOW);
  setColor(0, 0, 0);
}

// ================================================================
// OLED ANZEIGE
// ================================================================

void showOLED(int pir, float distance, int tilt, float t, float h,
              bool isDark, const char* modeText, int secondsLeft) {
  display.clearDisplay();

  display.setCursor(0, 0);
  display.print("Move:");
  display.print(pir ? "Yes " : "No ");

  display.print("Dist:");
  display.print(distance);
  display.println("cm");

  display.setCursor(0, 12);
  display.print("Incl:");
  display.print(tilt ? "Yes " : "No ");

  display.print("Temp:");
  display.print(t);
  display.println("C");

  display.setCursor(0, 24);
  display.print("Hum:");
  display.print(h);
  display.println("%");

  display.setCursor(0, 36);
  display.print("Light:");

  if (isDark) display.println("Dark");
  else display.println("Bright");

  display.setCursor(0, 50);
  display.print(modeText);
  display.print(": ");
  display.print(secondsLeft);
  display.println("sek");

  display.display();
}

// ================================================================
// SETUP
// ================================================================

void setup() {
  Serial.begin(115200);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  pinMode(pirPin, INPUT);
  pinMode(buzzer, OUTPUT);

  pinMode(tiltPin, INPUT);
  pinMode(lightPin, INPUT);

  // I2C fuer OLED: SDA = 21, SCL = 22
  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Fehler");
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  // PWM fuer RGB LED einrichten.
  ledcAttach(redPin, 5000, 8);
  ledcAttach(greenPin, 5000, 8);
  ledcAttach(bluePin, 5000, 8);

  dht.begin();
  setupEspNow();

  resetAverages();

  // Start der ersten Messphase.
  phaseStart = millis();

  // Dadurch wird direkt beim Start ein erstes Paket gesendet.
  lastSendMs = millis() - SEND_INTERVAL;
}

// ================================================================
// LOOP
// ================================================================

void loop() {
  unsigned long now = millis();

  // ------------------------------------------------------------
  // Wechsel von Messphase zu Pausenphase
  // ------------------------------------------------------------
  if (!sleepMode && now - phaseStart >= MEASURE_TIME) {
    finalizeAverages();

    sleepMode = true;
    phaseStart = now;

    sensorsOffForSleep();

    // Einmal den fertigen 30-Sekunden-Durchschnitt senden.
    // Dieses Paket wird beim Empfaenger fuer den Graphen verwendet.
    sendSensorPacket(true, true, SLEEP_TIME / 1000,
                     avgPir, avgDistance, avgTilt, avgTemp, avgHum, avgLight);

    lastSendMs = now;
  }

  // ------------------------------------------------------------
  // Wechsel von Pausenphase zurueck zur Messphase
  // ------------------------------------------------------------
  if (sleepMode && now - phaseStart >= SLEEP_TIME) {
    sleepMode = false;
    phaseStart = now;

    resetAverages();

    // Damit nach dem Aufwachen direkt wieder gesendet werden kann.
    lastSendMs = now - SEND_INTERVAL;
  }

  // ------------------------------------------------------------
  // Verhalten waehrend der Pausenphase
  // ------------------------------------------------------------
  if (sleepMode) {
    bool avgIsDark = avgLight > 3000;
    int secondsLeft = remainingSeconds(SLEEP_TIME);

    // Im Sleep/Pausenmodus werden die Durchschnittswerte angezeigt.
    showOLED(avgPir, avgDistance, avgTilt, avgTemp, avgHum,
             avgIsDark, "Sleep", secondsLeft);

    // Status und Countdown weiter senden, aber nicht als Graph-Wert speichern.
    if (now - lastSendMs >= SEND_INTERVAL) {
      sendSensorPacket(true, false, secondsLeft,
                       avgPir, avgDistance, avgTilt, avgTemp, avgHum, avgLight);

      lastSendMs = now;
    }

    delay(100);
    return;
  }

  // ------------------------------------------------------------
  // Verhalten waehrend der Messphase
  // ------------------------------------------------------------

  int pir = digitalRead(pirPin);

  if (pir == HIGH) tone(buzzer, 1200);
  else noTone(buzzer);

  float distance = measureDistance();

  float h;
  float t;
  readDhtCached(t, h);

  // Tilt-Sensor ist invertiert angeschlossen, deshalb das Ausrufezeichen.
  int tilt = !digitalRead(tiltPin);

  int light = analogRead(lightPin);
  bool isDark = (light > 3000);

  // Werte fuer den 30-Sekunden-Durchschnitt sammeln.
  addAverages(pir, distance, tilt, t, h, light);

  // RGB LED zeigt die Distanz an.
  if (distance > 0 && distance < 10) setColor(255, 0, 0);
  else if (distance < 25) setColor(255, 120, 0);
  else setColor(0, 255, 0);

  int secondsLeft = remainingSeconds(MEASURE_TIME);

  // Aktuelle Live-Werte am OLED anzeigen.
  showOLED(pir, distance, tilt, t, h, isDark, "Awake", secondsLeft);

  // Live-Werte ungefaehr einmal pro Sekunde an den Empfaenger senden.
  if (now - lastSendMs >= SEND_INTERVAL) {
    sendSensorPacket(false, false, secondsLeft,
                     pir, distance, tilt, t, h, light);

    lastSendMs = now;
  }

  delay(50);
}
