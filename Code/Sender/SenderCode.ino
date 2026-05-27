#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <esp_sleep.h>
#include "DHT.h"

// ================================================================
// ESP-NOW KONFIGURATION
// ================================================================

#define ESPNOW_CHANNEL 11

uint8_t receiverAddress[] = {0x00, 0x70, 0x07, 0x25, 0x77, 0x4C};

#define PACKET_MAGIC 0xAABBCCDD
#define PACKET_VERSION 2

typedef struct __attribute__((packed)) {
  uint32_t magic;
  uint8_t version;
  uint32_t seq;
  uint32_t uptimeMs;

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
} SensorPacket;

uint32_t packetSeq = 0;

void onDataSent(const wifi_tx_info_t *tx_info, esp_now_send_status_t status) {
  Serial.print("ESP-NOW Sendung: ");
  Serial.println(status == ESP_NOW_SEND_SUCCESS ? "OK" : "FEHLER");
}

void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  if (len == 1) {
    extern bool ledEnabled;
    if (data[0] == 1) {
      ledEnabled = true;
      Serial.println("LED via Webinterface eingeschaltet.");
    } else if (data[0] == 0) {
      ledEnabled = false;
      ledcWrite(25, 0);
      ledcWrite(26, 0);
      ledcWrite(27, 0);
      Serial.println("LED via Webinterface ausgeschaltet.");
    }
  }
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

#define TRIG 5
#define ECHO 18

int redPin = 25;
int greenPin = 26;
int bluePin = 27;

int pirPin = 2;
int buzzer = 13;

#define DHTPIN 33
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

const unsigned long DHT_INTERVAL = 2000;
unsigned long lastDhtReadMs = 0;
bool hasDhtValue = false;
float lastTemp = NAN;
float lastHum = NAN;

int tiltPin = 32;
int lightPin = 34;

// ================================================================
// MESS- UND DEEP-SLEEP-MODUS
// ================================================================

const unsigned long MEASURE_TIME = 30000;
const unsigned long SLEEP_TIME = 30000;

#define uS_TO_S_FACTOR 1000000ULL
#define TIME_TO_SLEEP_SECONDS (SLEEP_TIME / 1000)

const unsigned long SEND_INTERVAL = 1000;

unsigned long phaseStart = 0;
unsigned long lastSendMs = 0;

bool ledEnabled = true;

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
  int validSamples = 0;

  for (int i = 0; i < 5; i++) {
    digitalWrite(TRIG, LOW);
    delayMicroseconds(5);

    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);

    long duration = pulseIn(ECHO, HIGH, 30000);
    float d = duration * 0.034 / 2;

    if (d > 0) {
      sum += d;
      validSamples++;
    }

    delay(10);
  }

  if (validSamples == 0) {
    return 0;
  }

  return sum / validSamples;
}

// ================================================================
// DHT11 AUSLESEN
// ================================================================

void readDhtCached(float &t, float &h) {
  unsigned long now = millis();

  if (!hasDhtValue || now - lastDhtReadMs >= DHT_INTERVAL) {
    float newH = dht.readHumidity();
    float newT = dht.readTemperature();

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
  if (!ledEnabled) {
    ledcWrite(redPin, 0);
    ledcWrite(greenPin, 0);
    ledcWrite(bluePin, 0);
    return;
  }

  ledcWrite(redPin, r);
  ledcWrite(greenPin, g);
  ledcWrite(bluePin, b);
}

// ================================================================
// ESP-NOW EINRICHTEN UND SENDEN
// ================================================================

void setupEspNow() {
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);
  WiFi.disconnect();

  esp_wifi_set_ps(WIFI_PS_NONE);

  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(ESPNOW_CHANNEL, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous(false);

  if (esp_now_init() != ESP_OK) {
    Serial.println("ESP-NOW Fehler");
    while (true);
  }

  esp_now_register_send_cb(onDataSent);
  esp_now_register_recv_cb(onDataRecv);

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

  packet.motion = pir ? 1 : 0;
  packet.tilt = tilt ? 1 : 0;
  packet.dark = light > 4050 ? 1 : 0;

  packet.sleeping = sleeping ? 1 : 0;
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
  avgDistance = distanceSamples > 0 ? sumDistance / distanceSamples : 0;
  avgTemp = tempSamples > 0 ? sumTemp / tempSamples : 0;
  avgHum = humSamples > 0 ? sumHum / humSamples : 0;
  avgLight = lightSamples > 0 ? sumLight / lightSamples : 0;

  avgPir = totalSamples > 0 && pirHighSamples > totalSamples / 2 ? HIGH : LOW;
  avgTilt = totalSamples > 0 && tiltHighSamples > totalSamples / 2 ? HIGH : LOW;
}

// ================================================================
// ZEIT UND DEEP SLEEP
// ================================================================

int remainingSeconds(unsigned long phaseTime) {
  unsigned long elapsed = millis() - phaseStart;

  if (elapsed >= phaseTime) return 1;

  return (phaseTime - elapsed + 999) / 1000;
}

void sensorsOffForSleep() {
  noTone(buzzer);
  digitalWrite(TRIG, LOW);
  setColor(0, 0, 0);
}

void goToDeepSleep() {
  sensorsOffForSleep();

  display.clearDisplay();
  display.display();
  display.ssd1306_command(SSD1306_DISPLAYOFF);

  Serial.println("Gehe jetzt in Deep Sleep...");
  Serial.print("Wache wieder auf in ");
  Serial.print(TIME_TO_SLEEP_SECONDS);
  Serial.println(" Sekunden");

  esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP_SECONDS * uS_TO_S_FACTOR);

  delay(200);
  Serial.flush();

  esp_deep_sleep_start();
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

  Wire.begin(21, 22);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("OLED Fehler");
    while (true);
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(WHITE);

  ledcAttach(redPin, 5000, 8);
  ledcAttach(greenPin, 5000, 8);
  ledcAttach(bluePin, 5000, 8);

  dht.begin();
  setupEspNow();

  resetAverages();

  phaseStart = millis();
  lastSendMs = millis() - SEND_INTERVAL;
}

// ================================================================
// LOOP
// ================================================================

void loop() {
  unsigned long now = millis();

  if (now - phaseStart >= MEASURE_TIME) {
    finalizeAverages();

    sendSensorPacket(true, true, SLEEP_TIME / 1000,
                     avgPir, avgDistance, avgTilt, avgTemp, avgHum, avgLight);

    delay(200);

    goToDeepSleep();
  }

  int pir = digitalRead(pirPin);

  if (pir == HIGH) tone(buzzer, 1200);
  else noTone(buzzer);

  float distance = measureDistance();

  float h;
  float t;
  readDhtCached(t, h);

  int tilt = !digitalRead(tiltPin);

  int light = analogRead(lightPin);

  bool isDark = (light > 4050);

  addAverages(pir, distance, tilt, t, h, light);

  if (distance <= 0) {
    setColor(0, 0, 255);
  } else if (distance < 10) {
    setColor(255, 0, 0);
  } else if (distance < 25) {
    setColor(255, 120, 0);
  } else {
    setColor(0, 255, 0);
  }

  int secondsLeft = remainingSeconds(MEASURE_TIME);

  showOLED(pir, distance, tilt, t, h, isDark, "Awake", secondsLeft);

  if (now - lastSendMs >= SEND_INTERVAL) {
    sendSensorPacket(false, false, secondsLeft,
                     pir, distance, tilt, t, h, light);

    lastSendMs = now;
  }

  delay(50);
}
