# -ITP-Projekt-IoT-ESP-NOW
// LED & Ultraschallsensor code
#define TRIG 5
#define ECHO 18

int redPin = 25;
int greenPin = 26;
int bluePin = 27;

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

void setup() {

  Serial.begin(115200);

  pinMode(TRIG, OUTPUT);
  pinMode(ECHO, INPUT);

  // NEUE ESP32 PWM API
  ledcAttach(redPin, 5000, 8);
  ledcAttach(greenPin, 5000, 8);
  ledcAttach(bluePin, 5000, 8);
}

void setColor(int r, int g, int b) {
  ledcWrite(redPin, r);
  ledcWrite(greenPin, g);
  ledcWrite(bluePin, b);
}

void loop() {

  float distance = measureDistance();

  Serial.println(distance);

  if (distance > 0 && distance < 10) {

    setColor(255, 0, 0); // rot

  } 
  else if (distance >= 10 && distance <= 25) {

    setColor(255, 120, 0); // gelb

  } 
  else if (distance > 25) {

    setColor(0, 255, 0); // grün
  }

  delay(150);
}
