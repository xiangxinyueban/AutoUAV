#include "driver/i2c_master.h"
#include "esp_err.h"
#include <stdint.h>

esp_err_t oled_lvgl_init(i2c_master_bus_handle_t i2c_bus);
void oled_lvgl_update(int16_t accel_x, int16_t accel_y, int16_t accel_z,
                      int16_t gyro_x, int16_t gyro_y, int16_t gyro_z);