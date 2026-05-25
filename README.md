# ESP32 IoT-Station mit ESP-NOW

**SYT ITP Projekt – ESP32 IoT Station mit Webserver, WiFi-Manager und Sleep-Mode**
Gruppenmitglieder: Lena Zangerl, Damjan Panevski
Datum: 25.05.2026

---

## Einführung
Im Rahmen dieses Projekts wurde ein IoT-System mit zwei ESP32-Mikrocontrollern umgesetzt. Ziel war es, Sensordaten drahtlos über ESP-NOW zu übertragen und auf einem zweiten ESP32 übersichtlich darzustellen.

Der erste ESP32 dient als Sender und erfasst verschiedene Umgebungsdaten wie Distanz, Temperatur, Luftfeuchtigkeit, Bewegung, Neigung und Helligkeit. Die Messwerte werden direkt auf einem OLED-Display angezeigt und zusätzlich per ESP-NOW an den zweiten ESP32 übertragen.

Der zweite ESP32 arbeitet als Empfänger. Er nimmt die Sensordaten entgegen, stellt ein Webinterface 

---

## Projektbeschreibung

Das System besteht aus einem Sender-ESP32 und einem Empfänger-ESP32. Der Sender liest die angeschlossenen Sensoren aus und sendet die Messdaten regelmäßig über ESP-NOW an den Empfänger. Während der Messphase werden aktuelle Werte laufend übertragen und am OLED-Display dargestellt.

Nach einer Messdauer von 30 Sekunden bildet der Sender Durchschnittswerte aus den aufgenommenen Messungen. Anschließend wechselt er für 30 Sekunden in einen Sleep-/Pausenmodus. In dieser Zeit werden keine neuen Sensordaten aufgenommen, stattdessen werden die zuletzt berechneten Durchschnittswerte angezeigt und übertragen.

Der Empfänger bleibt dauerhaft aktiv. Er empfängt die Datenpakete des Senders und stellt sie über einen eigenen Webserver zur Verfügung. Über die IP-Adresse des Empfängers kann ein Webinterface geöffnet werden, auf dem die aktuellen Messwerte angezeigt werden. Zusätzlich werden historische Durchschnittswerte gespeichert, ohne jede einzelne Messung dauerhaft abzulegen.

Zur besseren Auswertung werden die historischen Daten im Webinterface als einfacher Graph dargestellt. Außerdem stellt der Empfänger eine JSON-API bereit, über die die aktuellen Sensordaten maschinenlesbar abgerufen werden können. Als Zusatzfunktion wurde ein Telegram-Bot eingebunden, der auf Anfrage den aktuellen Status des Systems zurückmeldet.

---

## Projektziel

Ziel dieses Projekts war es, zwei ESP32-Mikrocontroller zu einer IoT-Station zu verbinden.

Ein ESP32 (Sender) misst verschiedene Umweltdaten und sendet diese über ESP-NOW an einen zweiten ESP32 (Receiver).  
Der Receiver verarbeitet die Daten und stellt sie über ein Webinterface im Browser dar.

---

## Umgesetzte Funktionen

### GK (Grundkompetenzen)

- **WiFi-Manager:** Automatische Verbindung mit einem WLAN ohne festen Code  
- **Sleep-Mode:** Energiesparmodus zwischen den Messungen  
- **LDR (Helligkeitssensor):** Erkennt die Lichtstärke (hell/dunkel)  
- **Tilt B15 Sensor:** Erkennt Neigung oder Bewegung des Geräts  

---

### EK (Erweiterte Kompetenzen)

- **PIR Bewegungssensor:** Erkennt Bewegungen im Raum und löst einen Alarm aus  
- **Ultraschallsensor:** Misst die Entfernung zu Objekten  
- **DHT11 Sensor:** Misst Temperatur und Luftfeuchtigkeit  
- **RGB LED:** Zeigt Zustände durch Farben (z. B. grün, orange, rot)  
- **Buzzer:** Gibt akustische Signale bei Ereignissen aus  
- **OLED Display:** Zeigt alle Messwerte übersichtlich an  

---

## Systemarchitektur

Die Sensoren erfassen verschiedene Umweltdaten (Bewegung, Temperatur, Luftfeuchtigkeit, Licht, Abstand und Neigung).

PIR Bewegungssensor + Ultraschallsensor + DHT11 + LDR + Tilt B15 + OLED Display + RGB LED  
→ ESP32 Sender (Sensordaten erfassen & verarbeiten)  
→ ESP-NOW (drahtlose Kommunikation ohne Router)  
→ ESP32 Receiver (Daten empfangen & weiterverarbeiten)  
→ Webserver (Anzeige der Daten im Browser in Echtzeit)

---

## Verwendete Komponenten

| Komponente                 | Anzahl | Beschreibung |
|---------------------------|:------:|--------------|
| ESP32 Dev Board           | 2      | Hauptcontroller |
| DHT11 Sensor              | 1      | Temperatur- & Luftfeuchtigkeitsmessung |
| HC-SR04 Ultraschallsensor | 1      | Abstandsmessung |
| PIR Bewegungssensor       | 1      | Bewegungserkennung |
| LDR (Helligkeitssensor)   | 1      | Lichtmessung (analog) |
| Tilt B15 Sensor           | 1      | Neigungserkennung (digital) |
| OLED Display 128x64       | 1      | Anzeige der Messwerte |
| RGB LED                   | 1      | Statusanzeige (Farben) |
| Buzzer                    | 1      | Akustisches Signal |
| Breadboard                | 1      | Steckbrett für Aufbau |
| Jumper Kabel              | mehrere| Verbindungen zwischen Bauteilen |
| USB Kabel                 | 2      | Stromversorgung |

---

```cpp
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "DHT.h"

// ---------------- OLED ----------------
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// ---------------- Ultraschall ----------------
#define TRIG 5
#define ECHO 18

// ---------------- RGB ----------------
int redPin = 25;
int greenPin = 26;
int bluePin = 27;

// ---------------- PIR + Buzzer ----------------
int pirPin = 2;
int buzzer = 13;

// ---------------- DHT11 ----------------
#define DHTPIN 33
#define DHTTYPE DHT11
DHT dht(DHTPIN, DHTTYPE);

// ---------------- NEW SENSORS ----------------
int tiltPin = 32;
int lightPin = 34;

// ---------------- DISTANZ ----------------
float measureDistance() {

  float sum = 0;

  for (int i = 0; i < 5; i++) {

    digitalWrite(TRIG, LOW);
    delayMicroseconds(5);
    digitalWrite(TRIG, HIGH);
    delayMicroseconds(10);
    digitalWrite(TRIG, LOW);

    long duration = pulseIn(ECHO, HIGH, 30000);
    float d = duration * 0.034 / 2;

    sum += d;
    delay(10);
  }

  return sum / 5;
}

// ---------------- RGB ----------------
void setColor(int r, int g, int b) {
  ledcWrite(redPin, r);
  ledcWrite(greenPin, g);
  ledcWrite(bluePin, b);
}

// ---------------- SETUP ----------------
void setup() {

  Serial.begin(115200);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  pinMode(pirPin, INPUT);
  pinMode(buzzer, OUTPUT);

  pinMode(tiltPin, INPUT);

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
}

// ---------------- LOOP ----------------
void loop() {

  // -------- PIR --------
  int pir = digitalRead(pirPin);

  if (pir == HIGH) tone(buzzer, 1200);
  else noTone(buzzer);

  // -------- DISTANZ --------
  float distance = measureDistance();

  // -------- DHT11 --------
  float h = dht.readHumidity();
  float t = dht.readTemperature();
  t = round(t * 10) / 10.0;

  // -------- TILT (UMGEDREHT) --------
  int tilt = digitalRead(tiltPin);

  // -------- LIGHT (4095 = dunkel) --------
  int light = analogRead(lightPin);

  bool isDark = (light > 3000);

  // -------- RGB --------
  if (distance > 0 && distance < 10) setColor(255, 0, 0);
  else if (distance < 25) setColor(255, 120, 0);
  else setColor(0, 255, 0);


// -------- OLED --------
display.clearDisplay();

// Line 1: Bewegung + Tilt
display.setCursor(0, 0);
display.print("Move:");
display.print(pir ? "Yes " : "No ");

display.print("   Incl:");
display.println(tilt ? "Yes" : "No");

// Line 2: Distanz + Temperatur
display.setCursor(0, 12);
display.print("Dist:");
display.print(distance);
display.print(" Temp:");
display.print(t);
display.println("C");

// Line 3: Feuchte + Licht
display.setCursor(0, 24);
display.print("Hum:");
display.print(h);
display.print("% Light:");

if (isDark) display.println("Dark");
else display.println("Bright");

display.display();

  delay(50);
}
