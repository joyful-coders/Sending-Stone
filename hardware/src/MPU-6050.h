#include <Adafruit_MPU6050.h>
#include <Adafruit_Sensor.h>
#include <Wire.h>

Adafruit_MPU6050 mpu;

class MPU6050 {
    private:
    
    public:
        MPU6050();
        
        void loop();
};
