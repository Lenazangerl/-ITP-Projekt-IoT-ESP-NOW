
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

// VORBEREITUNG FÜR WEBINTERFACE: Diese Funktion verarbeitet empfangene Daten vom Empfänger.
void onDataRecv(const esp_now_recv_info_t *recv_info, const uint8_t *data, int len) {
  // Wir erwarten 1 Byte (1 = LED an, 0 = LED aus)
  if (len == 1) {
    extern bool ledEnabled;
    if (data[0] == 1) {
      ledEnabled = true;
      Serial.println("LED via Webinterface eingeschaltet.");
    } else if (data[0] == 0) {
      ledEnabled = false;
      // Schaltet die Hardware-Pins sofort ab
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

// VORBEREITUNG FÜR WEBINTERFACE: Bestimmt, ob die RGB-LED leuchten darf
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
// ULTRASCHALLMESSUNG (GEÄNDERT: FILTERT EINZELNE FEHLMESSUNGEN)
// ================================================================

float measureDistance() {
  float sum = 0;
  int validSamples = 0;

  // Mehrere Messungen werden durchlaufen
  for (int i = 0; i < 5; i++) {
    digitalWrite(TRIG, LOW);
    delayMicroseconds(5);

    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);

    long duration = pulseIn(ECHO, HIGH, 30000);
    
    // Umrechnung in cm
    float d = duration * 0.034 / 2;

    // GEÄNDERT: Nur Werte über 0 sind gültig und fließen in den Durchschnitt ein
    if (d > 0) {
      sum += d;
      validSamples++;
    }
    delay(10);
  }

  // Wenn KEINE einzige Messung gültig war (Sensor abgesteckt), liefere 0 für BLAU
  if (validSamples == 0) {
    return 0;
  }

  // Ansonsten teile nur durch die Anzahl der wirklich erfolgreichen Messungen
  return sum / validSamples;
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
  // VORBEREITUNG FÜR WEBINTERFACE: Wenn deaktiviert, bleibt die LED aus
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
  
  // VORBEREITUNG FÜR WEBINTERFACE: Registriert die Empfangs-Funktion
  esp_now_register_recv_cb(onDataRecv);

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
  
  // NEU: Schwellenwert auf 4050 angepasst, damit dein helles Raumlicht (4095) korrekt filtert
  packet.dark = light > 4050 ? 1 : 0;

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
  // Verhalten waeharem der Pausenphase
  // ------------------------------------------------------------
  if (sleepMode) {
    // NEU: Schwellenwert auf 4050 angepasst
    bool avgIsDark = avgLight > 4050;
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
  
  // NEU: Schwellenwert auf 4050 angepasst
  bool isDark = (light > 4050);

  // Werte fuer den 30-Sekunden-Durchschnitt sammeln.
  addAverages(pir, distance, tilt, t, h, light);

  // RGB LED zeigt den Zustand an.
  // Wenn der Ultraschallsensor disconnected/0 ist, schaltet die LED auf BLAU
  if (distance <= 0) {
    setColor(0, 0, 255); 
  } 
  else if (distance < 10) {
    setColor(255, 0, 0); // Rot bei sehr naher Distanz
  } 
  else if (distance < 25) {
    setColor(255, 120, 0); // Orange bei mittlerer Distanz
  } 
  else {
    setColor(0, 255, 0); // Gruen bei freier Bahn
  }

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
erändere nur das es ist den deep sleep geht sprich bei awake soll coundown etc bleiben werte aktuliseirt werden und bei schlaf modus nichts mehr am oled display anzeigen etc soll nach dieser art integriert wrerdne :You can use the timer, waking up your ESP32 using predefined periods of time;

You can use the touch pins;

You can use two possibilities of external wake up: you can use either one external wake up, or several different external wake-up sources;

You can use the ULP co-processor to wake up – this won’t be covered in this guide.

Writing a Deep Sleep Sketch

To write a sketch to put your ESP32 into deep sleep mode, and then wake it up, you need to keep in mind that:

First, you need to configure the wake up sources. This means configure what will wake up the ESP32. You can use one or combine more than one wake up source.

You can decide what peripherals to shut down or keep on during deep sleep. However, by default, the ESP32 automatically powers down the peripherals that are not needed with the wake up source you define.

Finally, you use the esp_deep_sleep_start() function to put your ESP32 into deep sleep mode.

Timer Wake Up

The ESP32 can go into deep sleep mode, and then wake up at predefined periods of time. This feature is specially useful if you are running projects that require time stamping or daily tasks, while maintaining low power consumption.



The ESP32 RTC controller has a built-in timer you can use to wake up the ESP32 after a predefined amount of time.

Enable Timer Wake Up

Enabling the ESP32 to wake up after a predefined amount of time is very straightforward. In the Arduino IDE, you just have to specify the sleep time in microseconds in the following function:

esp_sleep_enable_timer_wakeup(time_in_us)

Code

To program the ESP32 we’ll use Arduino IDE. So, you need to make sure you have the ESP32 Arduino core installed. Follow the next tutorial to install the ESP32 add-on, if you haven’t already:

Installing ESP32 Board in Arduino IDE 2 (Windows, Mac OS X, Linux)

Let’s see how this works using an example from the library. Open your Arduino IDE, and go to File > Examples > ESP32 Deep Sleep, and open the TimerWakeUp sketch.

/* Simple Deep Sleep with Timer Wake Up

ESP32 offers a deep sleep mode for effective power saving as power is an important factor for IoT applications. In this mode CPUs, most of the RAM, and all the digital peripherals which are clocked

from APB_CLK are powered off. The only parts of the chip which can still be powered on are: RTC controller, RTC peripherals ,and RTC memories This code displays the most basic deep sleep with a timer to wake it up and how to store data in RTC memory to use it over reboots This code is under Public Domain License.

Author: Pranav Cherukupalli <cherukupallip@gmail.com> */#define uS_TO_S_FACTOR 1000000ULL // Conversion factor for micro seconds to seconds#define TIME_TO_SLEEP 5 // Time ESP32 will go to sleep (in seconds)



RTC_DATA_ATTR int bootCount = 0;// Method to print the reason by which ESP32 has been awaken from sleepvoid print_wakeup_reason(){

esp_sleep_wakeup_cause_t wakeup_reason;



wakeup_reason = esp_sleep_get_wakeup_cause();



switch(wakeup_reason)

{

case ESP_SLEEP_WAKEUP_EXT0 : Serial.println("Wakeup caused by external signal using RTC_IO"); break;

case ESP_SLEEP_WAKEUP_EXT1 : Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;

case ESP_SLEEP_WAKEUP_TIMER : Serial.println("Wakeup caused by timer"); break;

case ESP_SLEEP_WAKEUP_TOUCHPAD : Serial.println("Wakeup caused by touchpad"); break;

case ESP_SLEEP_WAKEUP_ULP : Serial.println("Wakeup caused by ULP program"); break;

default : Serial.printf("Wakeup was not caused by deep sleep: %d\n",wakeup_reason); break;

}}void setup(){

Serial.begin(115200);

delay(1000); // Take some time to open up the Serial Monitor



// Increment boot number and print it every reboot

++bootCount;

Serial.println("Boot number: " + String(bootCount));



// Print the wakeup reason for ESP32

print_wakeup_reason();



// First we configure the wake up source We set our ESP32 to wake up every 5 seconds

esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

Serial.println("Setup ESP32 to sleep for every " + String(TIME_TO_SLEEP) +

" Seconds");



/*

Next we decide what all peripherals to shut down/keep on

By default, ESP32 will automatically power down the peripherals

not needed by the wakeup source, but if you want to be a poweruser

this is for you. Read in detail at the API docs

http://esp-idf.readthedocs.io/en/latest/api-reference/system/deep_sleep.html

Left the line commented as an example of how to configure peripherals.

The line below turns off all RTC peripherals in deep sleep.

*/

//esp_deep_sleep_pd_config(ESP_PD_DOMAIN_RTC_PERIPH, ESP_PD_OPTION_OFF);

//Serial.println("Configured all RTC Peripherals to be powered down in sleep");



// Now that we have setup a wake cause and if needed setup the peripherals state in deep sleep, we can now start going to deep sleep.

// In the case that no wake up sources were provided but deep sleep was started, it will sleep forever unless hardware reset occurs.

Serial.println("Going to sleep now");

delay(1000);

Serial.flush();

esp_deep_sleep_start();

Serial.println("This will never be printed");}void loop(){

// This is not going to be called}

View raw code

Let’s take a look at this code. The first comment describes what is powered off during deep sleep with timer wake up.

In this mode CPUs, most of the RAM,

and all the digital peripherals which are clocked

from APB_CLK are powered off. The only parts of

the chip which can still be powered on are:

RTC controller, RTC peripherals ,and RTC memories

When you use timer wake-up, the parts that will be powered on are RTC controller, RTC peripherals, and RTC memories.

Define the Sleep Time

These first two lines of code define the period of time the ESP32 will be sleeping.

#define uS_TO_S_FACTOR 1000000ULL /* Conversion factor for micro seconds to seconds */#define TIME_TO_SLEEP 5 /* Time ESP32 will go to sleep (in seconds) */

This example uses a conversion factor from microseconds to seconds, so that you can set the sleep time in the TIME_TO_SLEEP variable in seconds. In this case, the example will put the ESP32 into deep sleep mode for 5 seconds.

Save Data on RTC Memories

With the ESP32, you can save data on the RTC memories. The ESP32 has 8kB SRAM on the RTC part, called RTC fast memory. The data saved here is not erased during deep sleep. However, it is erased when you press the reset button (the button labeled EN on the ESP32 board).

To save data on the RTC memory, you just have to add RTC_DATA_ATTR before a variable definition. The example saves the bootCount variable on the RTC memory. This variable will count how many times the ESP32 has woken up from deep sleep.

RTC_DATA_ATTR int bootCount = 0;

Wake Up Reason

Then, the code defines the print_wakeup_reason() function, that prints the source that caused the wake-up from deep sleep.

void print_wakeup_reason() {

esp_sleep_wakeup_cause_t wakeup_reason;



wakeup_reason = esp_sleep_get_wakeup_cause();



switch (wakeup_reason) {

case ESP_SLEEP_WAKEUP_EXT0: Serial.println("Wakeup caused by external signal using RTC_IO"); break;

case ESP_SLEEP_WAKEUP_EXT1: Serial.println("Wakeup caused by external signal using RTC_CNTL"); break;

case ESP_SLEEP_WAKEUP_TIMER: Serial.println("Wakeup caused by timer"); break;

case ESP_SLEEP_WAKEUP_TOUCHPAD: Serial.println("Wakeup caused by touchpad"); break;

case ESP_SLEEP_WAKEUP_ULP: Serial.println("Wakeup caused by ULP program"); break;

default: Serial.printf("Wakeup was not caused by deep sleep: %d\n", wakeup_reason); break;

}}

The setup()

In the setup() is where you should put your code. You need to write all the instructions before calling the esp_deep_sleep_start() function.

This example starts by initializing the serial communication at a baud rate of 115200.

Serial.begin(115200);

Then, the bootCount variable is increased by one in every reboot, and that number is printed in the serial monitor.

++bootCount;

Serial.println("Boot number: " + String(bootCount));

Then, the code calls the print_wakeup_reason() function, but you can call any function you want to perform a desired task. For example, you may want to wake up your ESP32 once a day to read a value from a sensor.

Next, the code defines the wake-up source by using the following function:

esp_sleep_enable_timer_wakeup(time_in_us)

This function accepts as argument the time to sleep in microseconds as we’ve seen previously. In our case, we have the following:

esp_sleep_enable_timer_wakeup(TIME_TO_SLEEP * uS_TO_S_FACTOR);

Then, after all the tasks are performed, the ESP32 goes to sleep by calling the following function:

esp_deep_sleep_start()

As soon as you call the esp_deep_sleep_start() function, the ESP32 will go to sleep and will not run any code written after this function. When it wakes up from deep sleep, it will run the code from the beginning.

loop()

The loop() section is empty because the ESP32 will sleep before reaching this part of the code. So, you need to write all your tasks in the setup() before calling the esp_deep_sleep_start() function.

Testing the Timer Wake Up

Upload the example sketch to your ESP32. Make sure you have the right board and COM port selected. Open the Serial Monitor at a baud rate of 115200.

define mit timer intergiere 


