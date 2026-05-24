# ESP32 IoT-Station mit ESP-NOW

**SYT ITP Projekt – ESP32 IoT Station mit Webserver, WiFi-Manager und Sleep-Mode**

---

## Fach
Systemtechnik (SYT)

## Gruppe
- Lena Zangerl
- Damjan Panevski

---

## Projektziel

Ziel dieses Projekts war es, zwei ESP32-Mikrocontroller zu einer IoT-Station zu verbinden.

Ein ESP32 (Sender) misst die Umgebungshelligkeit sowie die Neigung und sendet diese Daten über ESP-NOW an einen zweiten ESP32 (Receiver).  
Der Receiver stellt die Messwerte über ein Webinterface dar.

Umgesetzt wurden die Varianten:
GK:
- **Wi-Fi Manager** (automatische WLAN-Konfiguration)
- **Sleep-Mode** (Energieeinsparung zwischen Messungen)
- **LDR (Helligkeitssensor):** Erkennt, wie hell oder dunkel es ist.
- **Tilt B15 Sensor:** Erkennt, ob das Gerät gekippt oder bewegt wurde.  
Ek:
- **PIR Bewegungssensor:** Erkennt Bewegungen im Raum und löst z. B. einen Alarm aus.  
- **Ultraschallsensor:** Misst die Entfernung zu einem Objekt.  
- **DHT11 Sensor:** Misst Temperatur und Luftfeuchtigkeit.   
- **RGB LED:** Zeigt Zustände mit Farben (z. B. grün, orange, rot).  
- **Buzzer:** Gibt einen Ton aus bei Bewegung oder Alarm.  
- **OLED Display:** Zeigt alle Messwerte übersichtlich an.


## Systemarchitektur

Die Sensoren erfassen verschiedene Daten (Bewegung, Temperatur, Luftfeuchtigkeit, Licht, Abstand, Display, RGB und Neigung).

PIR Bewegungssensor + Ultraschallsensor + DHT11 + LDR + Tilt B15 + OLED Display + LDR + RGB LED  
→ ESP32 Sender (Sensordaten erfassen & verarbeiten)  
→ ESP-NOW (drahtlose Kommunikation ohne Router)  
→ ESP32 Receiver (Daten empfangen & weiterverarbeiten)  
→ Webserver (Anzeige der Daten im Browser in Echtzeit)

| Komponente              | Anzahl | Beschreibung |
|------------------------|:------:|--------------|
| ESP32 Dev Board        | 2      | Hauptcontroller |
| DHT11 Sensor           | 1      | Temperatur- & Luftfeuchtigkeitsmessung |
| HC-SR04 Ultraschallsensor | 1   | Abstandsmessung |
| PIR Bewegungssensor    | 1      | Bewegungserkennung |
| LDR (Helligkeitssensor)| 1      | Lichtmessung (analog) |
| Tilt B15 Sensor        | 1      | Neigungserkennung (digital) |
| OLED Display 128x64    | 1      | Anzeige der Messwerte |
| RGB LED                | 1      | Statusanzeige (Farben) |
| Buzzer                 | 1      | Akustisches Signal |
| Breadboard             | 1      | Steckbrett für Aufbau |
| Jumper Kabel           | mehrere| Verbindungen zwischen Bauteilen |
| USB Kabel              | 2      | Strom |

---

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

// -------- Bewegung --------
display.setCursor(0, 0);
display.print("Bewegung: ");
if (pir == 1) display.println(" Ja");
else display.println(" Nein");

// -------- Distanz --------
display.setCursor(0, 12);
display.print("Distanz: ");
display.print(distance);
display.println(" cm");

// -------- Temperatur --------
display.setCursor(0, 24);
display.print("Temperatur: ");
display.print(t);
display.println(" C");

// -------- Luftfeuchtigkeit --------
display.setCursor(0, 36);
display.print("Feuchte: ");
display.print(h);
display.println(" %");

// -------- Light --------
display.setCursor(0, 48);
display.print("Licht: ");

if (isDark) display.println("DUNKEL");
else display.println("HELL");

display.display();

  delay(50);
}
