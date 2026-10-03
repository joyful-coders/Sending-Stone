#include <Arduino.h>
#include "MPU-6050.h"
#include "sound-sensor.h"

// XIAO ESP32-C6 user LED (yellow) is on GPIO15 and is active-low.
#define LED_PIN 15
#define ADC_PIN A0

void led();
void sound();
void accelerometer();

void setup() {
    Serial.begin(115200);
    while (!Serial) delay(10);
    Serial.println("Serial Started!");
    
    // led pin
    pinMode(LED_PIN, OUTPUT);


    

    Serial.println("Setup Finished!");
}

void loop() {
    // led();
    // sound();
    accelerometer();

    delay(20);
}

void led(){
    digitalWrite(LED_PIN, LOW);   // on
    Serial.println("LED on");
    delay(1500);

    digitalWrite(LED_PIN, HIGH);  // off
    Serial.println("LED off");
}

void sound() {
    long sum = 0;
    for(int i=0; i<32; i++)
    {
        sum += analogRead(ADC_PIN);
    }

    sum >>= 5;

    Serial.println(sum);
}
