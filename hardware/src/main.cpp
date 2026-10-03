#include <Arduino.h>

// XIAO ESP32-C6 user LED (yellow) is on GPIO15 and is active-low.
#define LED_PIN 15

void setup() {
    Serial.begin(115200);
    pinMode(LED_PIN, OUTPUT);
}

void loop() {
    digitalWrite(LED_PIN, LOW);   // on
    Serial.println("LED on");
    delay(1500);

    digitalWrite(LED_PIN, HIGH);  // off
    Serial.println("LED off");
    delay(500);
}
