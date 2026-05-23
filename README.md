#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>

// OLED
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64

Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, -1);

// Ultraschall
#define TRIG 5
#define ECHO 18

// RGB Pins
int redPin = 25;
int greenPin = 26;
int bluePin = 27;

// PIR + Buzzer
int pirPin = 2;
int buzzer = 13;

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
}

// ---------------- LOOP ----------------
void loop() {

  // -------- PIR --------
  int state = digitalRead(pirPin);
  Serial.println(state);

  if (state == HIGH) {
    tone(buzzer, 1200);
  } else {
    noTone(buzzer);
  }

  // -------- DISTANZ --------
  float distance = measureDistance();

  Serial.println(distance);

  // -------- OLED --------
  display.clearDisplay();
  display.setCursor(0, 0);
  display.print("Distanz:");

  display.setCursor(0, 20);
  display.print(distance);
  display.print(" cm");

  display.display();

  // -------- RGB --------
  if (distance > 0 && distance < 10) {
    setColor(255, 0, 0);
  } 
  else if (distance >= 10 && distance <= 25) {
    setColor(255, 120, 0);
  } 
  else {
    setColor(0, 255, 0);
  }

  delay(20); // wichtig: nur kleine Stabilisierung
}
