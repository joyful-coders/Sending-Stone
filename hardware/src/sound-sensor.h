

void SoundSensor(uint8_t ADC) {
    ADC_PIN = ADC;

    pinMode(ADC_PIN, INPUT);
    Serial.println("Pins Set!");
}

void loop() {
    long sum = 0;
    for(int i=0; i<32; i++)
    {
        sum += analogRead(ADC_PIN);
    }

    sum >>= 5;

    Serial.println(sum);
}