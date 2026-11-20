# ESP32 Flight Controller

Reads accelerometer, gyroscope, and temperature data from an MPU6050 and
prints the measurements to the serial monitor every 500 ms.

## Wiring

| MPU6050 | ESP32 |
| ------- | ----- |
| SCL     | GPIO22 |
| SDA     | GPIO21 |
| VCC     | 3.3V |
| GND     | GND |

The program probes the common 7-bit I2C addresses `0x68` and `0x69` (selected
by the AD0 pin). Accelerometer readings are printed in mg, gyro readings in
0.001 degrees/second, and temperature in 0.01 degrees Celsius.

## Build and flash

Build and flash with the ESP-IDF extension, then open the serial monitor at
115200 baud.
