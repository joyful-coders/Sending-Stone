#include <cstdint>

class SoundSensor {
    private:
        uint8_t ADC_PIN;
    
    public:
        SoundSensor(uint8_t ADC);
        
        void loop();
};