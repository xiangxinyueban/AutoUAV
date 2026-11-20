idf_component_register(SRCS "src/mpu6050_main.c" "src/LED.c" "src/button.c" "src/oled.c"
                    INCLUDE_DIRS "include"
                    PRIV_REQUIRES esp_driver_i2c esp_driver_gpio esp_timer esp_driver_ledc lvgl)
