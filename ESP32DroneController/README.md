# ESP32 Flight Controller

Reads accelerometer, gyroscope, and temperature data from an MPU6050 and
prints measurements to the serial monitor. It also uses LVGL to show live
accelerometer and gyroscope readings on a 128x64 SSD1306-compatible OLED.

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

The OLED shares the I2C bus with the MPU6050:

| OLED | ESP32 |
| ---- | ----- |
| SCL  | GPIO22 |
| SDA  | GPIO21 |
| VCC  | 3.3V |
| GND  | GND |

## Build and flash

Build and flash with the ESP-IDF extension, then open the serial monitor at
115200 baud.
