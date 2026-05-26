# ESP32 IoT-Station mit ESP-NOW

**SYT ITP Projekt – ESP32 IoT Station mit Webserver, WiFi-Manager und Sleep-Mode**

Gruppenmitglieder: Lena Zangerl, Damjan Panevski

Datum: 25.05.2026

---

## 1. Einführung
Im Rahmen dieses Projekts wurde ein IoT-System mit zwei ESP32-Mikrocontrollern umgesetzt. Ziel war es, Sensordaten drahtlos über ESP-NOW zu übertragen und auf einem zweiten ESP32 übersichtlich darzustellen.

Der erste ESP32 dient als Sender und erfasst verschiedene Umgebungsdaten wie Distanz, Temperatur, Luftfeuchtigkeit, Bewegung, Neigung und Helligkeit. Die Messwerte werden direkt auf einem OLED-Display angezeigt und zusätzlich per ESP-NOW an den zweiten ESP32 übertragen.

Der zweite ESP32 arbeitet als Empfänger. Er nimmt die Sensordaten entgegen, stellt ein Webinterface 

---

## 2. Projektbeschreibung

Das System besteht aus einem Sender-ESP32 und einem Empfänger-ESP32. Der Sender liest die angeschlossenen Sensoren aus und sendet die Messdaten regelmäßig über ESP-NOW an den Empfänger. Während der Messphase werden aktuelle Werte laufend übertragen und am OLED-Display dargestellt.

Nach einer Messdauer von 30 Sekunden bildet der Sender Durchschnittswerte aus den aufgenommenen Messungen. Anschließend wechselt er für 30 Sekunden in einen Sleep-/Pausenmodus. In dieser Zeit werden keine neuen Sensordaten aufgenommen, stattdessen werden die zuletzt berechneten Durchschnittswerte angezeigt und übertragen.

Der Empfänger bleibt dauerhaft aktiv. Er empfängt die Datenpakete des Senders und stellt sie über einen eigenen Webserver zur Verfügung. Über die IP-Adresse des Empfängers kann ein Webinterface geöffnet werden, auf dem die aktuellen Messwerte angezeigt werden. Zusätzlich werden historische Durchschnittswerte gespeichert, ohne jede einzelne Messung dauerhaft abzulegen.

Zur besseren Auswertung werden die historischen Daten im Webinterface als einfacher Graph dargestellt. Außerdem stellt der Empfänger eine JSON-API bereit, über die die aktuellen Sensordaten maschinenlesbar abgerufen werden können. Als Zusatzfunktion wurde ein Telegram-Bot eingebunden, der auf Anfrage den aktuellen Status des Systems zurückmeldet.

---

## 3. Projektziel

Ziel dieses Projekts ist die Umsetzung eines IoT-Systems mit zwei ESP32-Mikrocontrollern. Sensordaten sollen auf einem ESP32 erfasst und anschließend drahtlos über ESP-NOW an einen zweiten ESP32 übertragen werden.

Der zweite ESP32 soll die empfangenen Daten über ein Webinterface visualisieren. Zusätzlich sollen historische Durchschnittswerte in einem einfachen Graphen dargestellt werden. Der Sender soll außerdem einen Mess- und Pausenmodus verwenden, um nicht dauerhaft Messungen durchzuführen.

---

## 4. Arbeitsschritte

1. Auswahl und Anschluss der Sensoren am Sender-ESP32
2. Einrichten des OLED-Displays zur Anzeige der aktuellen Messwerte
3. Implementierung der Sensordatenerfassung am Sender
4. Bildung von Durchschnittswerten über eine Messphase von 30 Sekunden
5. Umsetzung eines Sleep-/Pausenmodus nach jeder Messphase
6. Einrichtung der ESP-NOW-Kommunikation zwischen Sender und Empfänger
7. Implementierung des Empfängers zur Verarbeitung der Datenpakete
8. Erstellung eines Webservers auf dem Empfänger-ESP32
9. Gestaltung eines einfachen Webinterfaces zur Anzeige der aktuellen Messwerte
10. Speicherung historischer Durchschnittswerte im Arbeitsspeicher
11. Darstellung der historischen Daten in einem einfachen Graphen
12. Bereitstellung einer JSON-API für die aktuellen Sensordaten
13. Integration eines Telegram-Bots zur Statusabfrage
14. Testen der Kommunikation, Anzeige und Bot-Funktion

---

## 4.1 Komponenten

Software

Libraries:
**WiFi.h**  
  Wird verwendet, um den ESP32 mit einem WLAN zu verbinden und den Access Point für das Webinterface bereitzustellen.

- **WiFiClientSecure.h**  
  Ermöglicht verschlüsselte HTTPS-Verbindungen. Diese Library wird für die Kommunikation mit der Telegram-API benötigt.

- **WiFiManager.h**  
  Stellt eine Konfigurationsseite bereit, über die WLAN-Zugangsdaten eingegeben werden können, ohne sie fest im Code einzutragen.

- **esp_now.h**  
  Wird für die ESP-NOW-Kommunikation zwischen Sender-ESP32 und Empfänger-ESP32 verwendet.

- **esp_wifi.h**  
  Ermöglicht erweiterte WLAN-Einstellungen wie das Setzen bzw. Auslesen des WLAN-Kanals für ESP-NOW.

- **WebServer.h**  
  Wird genutzt, um auf dem Empfänger-ESP32 einen Webserver zu starten und das Webinterface sowie die JSON-API bereitzustellen.

- **UniversalTelegramBot.h**  
  Ermöglicht die Kommunikation mit dem Telegram-Bot, z. B. für den Befehl `/status`.

- **ArduinoJson.h**  
  Wird von der Telegram-Bot-Library benötigt und kann außerdem zur Verarbeitung von JSON-Daten verwendet werden.

---

## Umgesetzte Funktionen

### GK (Grundkompetenzen)

- **ESP-NOW-Kommunikation:** Drahtlose Übertragung der Sensordaten zwischen zwei ESP32-Mikrocontrollern
- **Webserver:** Der Empfänger stellt ein Webinterface über seine IP-Adresse bereit
- **Webinterface:** Anzeige der aktuellen Messwerte in einer übersichtlichen Webseite
- **Sleep-/Pausenmodus:** Der Sender misst 30 Sekunden lang und pausiert anschließend 30 Sekunden
- **Helligkeitssensor (LDR):** Erfassung der Umgebungshelligkeit und Einteilung in hell oder dunkel
- **Tilt-Sensor:** Erkennung einer Neigung bzw. Lageänderung des Geräts
- **OLED-Display:** Anzeige der aktuellen Werte und des aktuellen Zustands direkt am Sender

### EK (Erweiterte Kompetenzen)

- **PIR-Bewegungssensor:** Erkennt Bewegungen im Raum und löst bei Bewegung den Buzzer aus
- **Ultraschallsensor HC-SR04:** Misst die Entfernung zu Objekten in Zentimetern
- **DHT11-Sensor:** Misst Temperatur und Luftfeuchtigkeit
- **RGB-LED:** Visualisiert die Distanz durch verschiedene Farben, z. B. grün, orange und rot
- **Buzzer:** Gibt ein akustisches Signal aus, sobald Bewegung erkannt wird
- **JSON-API:** Bereitstellung der aktuellen Sensordaten in maschinenlesbarer Form
- **Telegram-Bot:** Abfrage des aktuellen Sensorstatus über Telegram mit `/status`
- **einfacher Graph / Mittelwertbildung:** Berechnung von Durchschnittswerten aus den Messdaten einer Messphase und darstellung historischer Durchschnittswerte im Webinterface

---

## Systemarchitektur

Das System besteht aus zwei getrennten ESP32-Mikrocontrollern: einem Sender und einem Empfänger.

Der Sender-ESP32 ist mit den Sensoren und einem OLED-Display verbunden. Er misst Distanz, Temperatur, Luftfeuchtigkeit, Bewegung, Neigung und Helligkeit. Während der Messphase werden die aktuellen Werte direkt am OLED-Display angezeigt und per ESP-NOW an den Empfänger gesendet.

Der Empfänger-ESP32 bleibt dauerhaft aktiv. Er empfängt die Sensordaten über ESP-NOW und stellt sie über einen Webserver bereit. Über die IP-Adresse des Empfängers kann ein Webinterface geöffnet werden, auf dem aktuelle Messwerte und historische Durchschnittswerte angezeigt werden.

Zusätzlich ist auf dem Empfänger ein Telegram-Bot integriert. Über diesen kann der aktuelle Sensorstatus per Telegram abgefragt werden.

---

## Verwendete Komponenten

### Sender-ESP32

| Komponente                 | Anzahl | Funktion |
|---------------------------|:------:|----------|
| ESP32 Dev Board           | 1      | Steuerung, Messung und ESP-NOW-Senden |
| DHT11 Sensor              | 1      | Temperatur und Luftfeuchtigkeit |
| HC-SR04 Ultraschallsensor | 1      | Abstandsmessung |
| PIR Bewegungssensor       | 1      | Bewegungserkennung |
| LDR                       | 1      | Helligkeitsmessung |
| Tilt B15 Sensor           | 1      | Neigungserkennung |
| OLED Display 128x64       | 1      | Anzeige der Messwerte |
| RGB LED                   | 1      | Optische Statusanzeige |
| Buzzer                    | 1      | Akustisches Signal |
| Breadboard                | 1      | Schaltungsaufbau |
| Jumper Kabel              | mehrere| Verbindungen |
| USB Kabel                 | 1      | Stromversorgung und Programmierung |

### Empfänger-ESP32

| Komponente        | Anzahl | Funktion |
|------------------|:------:|----------|
| ESP32 Dev Board  | 1      | ESP-NOW-Empfang, Webserver und Telegram-Bot |
| USB Kabel        | 1      | Stromversorgung und Programmierung |

---

## Funktionshinweise

Der Sender-ESP32 erfasst die Sensordaten und zeigt sie direkt am OLED-Display an. Zusätzlich sendet er die Werte per ESP-NOW an den Empfänger-ESP32.

Der Buzzer wird aktiviert, sobald der PIR-Sensor eine Bewegung erkennt. Dadurch wird eine erkannte Bewegung nicht nur im Display und Webinterface angezeigt, sondern auch akustisch signalisiert.

Die RGB-LED visualisiert die gemessene Distanz des Ultraschallsensors:

- **Grün:** Objekt ist weit entfernt
- **Orange/Gelb:** Objekt befindet sich in mittlerer Entfernung
- **Rot:** Objekt ist sehr nah

Der Sender arbeitet in einem Mess- und Pausenzyklus. Während der Messphase werden aktuelle Werte aufgenommen und angezeigt. Nach 30 Sekunden werden Durchschnittswerte berechnet. In der anschließenden Pausenphase werden keine neuen Messwerte aufgenommen, sondern die zuletzt berechneten Durchschnittswerte angezeigt und übertragen.

Der Empfänger-ESP32 bleibt dauerhaft aktiv. Er empfängt die Daten, stellt sie im Webinterface dar und beantwortet Telegram-Anfragen wie `/status`.

---

## Schaltungsplan

Der Schaltungsplan zeigt den Aufbau des Projekts mit dem Sender-ESP32 und den angeschlossenen Sensoren bzw. Aktoren. Dargestellt sind die Verbindungen zum DHT11-Sensor, Ultraschallsensor, PIR-Sensor, LDR, Tilt-Sensor, OLED-Display, RGB-LED und Buzzer.

![Schaltungsplan](img/Schaltung.png)

---
## Sender Code
```cpp
// Code 
