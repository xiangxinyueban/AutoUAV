#include "oled.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_check.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "lvgl.h"

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_I2C_ADDRESS 0x3C
#define OLED_FLUSH_ROWS 16
#define OLED_FLUSH_TIMEOUT_MS 1000

static const char *TAG = "oled";
static i2c_master_dev_handle_t s_oled;
static lv_display_t *s_display;
static lv_obj_t *s_axis_labels[3];
static SemaphoreHandle_t s_lvgl_mutex;
static uint8_t s_framebuffer[OLED_WIDTH * OLED_HEIGHT / 8];
static uint16_t s_draw_buffer[OLED_WIDTH * OLED_FLUSH_ROWS];

static uint32_t lvgl_get_tick(void)
{
    return (uint32_t)(esp_timer_get_time() / 1000);
}

static esp_err_t oled_write_command(const uint8_t *commands, size_t length)
{
    uint8_t packet[32];
    if (length + 1 > sizeof(packet)) {
        return ESP_ERR_INVALID_SIZE;
    }

    packet[0] = 0x00;
    for (size_t i = 0; i < length; ++i) {
        packet[i + 1] = commands[i];
    }
    return i2c_master_transmit(s_oled, packet, length + 1, OLED_FLUSH_TIMEOUT_MS);
}

static uint8_t pixel_luminance(uint16_t color)
{
    const uint8_t red = (uint8_t)(((color >> 11) & 0x1F) * 255 / 31);
    const uint8_t green = (uint8_t)(((color >> 5) & 0x3F) * 255 / 63);
    const uint8_t blue = (uint8_t)((color & 0x1F) * 255 / 31);
    return (uint8_t)((red * 77 + green * 150 + blue * 29) >> 8);
}

static void oled_flush(lv_display_t *display, const lv_area_t *area, uint8_t *px_map)
{
    const int32_t x1 = area->x1 < 0 ? 0 : area->x1;
    const int32_t y1 = area->y1 < 0 ? 0 : area->y1;
    const int32_t x2 = area->x2 >= OLED_WIDTH ? OLED_WIDTH - 1 : area->x2;
    const int32_t y2 = area->y2 >= OLED_HEIGHT ? OLED_HEIGHT - 1 : area->y2;
    const int32_t area_width = area->x2 - area->x1 + 1;
    const uint16_t *pixels = (const uint16_t *)px_map;

    for (int32_t y = y1; y <= y2; ++y) {
        for (int32_t x = x1; x <= x2; ++x) {
            const size_t pixel_index = (size_t)(y - area->y1) * area_width +
                                       (size_t)(x - area->x1);
            const size_t byte_index = (size_t)(y / 8) * OLED_WIDTH + (size_t)x;
            const uint8_t bit = (uint8_t)(1U << (y & 7));
            if (pixel_luminance(pixels[pixel_index]) >= 128) {
                s_framebuffer[byte_index] |= bit;
            } else {
                s_framebuffer[byte_index] &= (uint8_t)~bit;
            }
        }
    }

    const uint8_t first_page = (uint8_t)(y1 / 8);
    const uint8_t last_page = (uint8_t)(y2 / 8);
    uint8_t data[OLED_WIDTH + 1];
    data[0] = 0x40;

    for (uint8_t page = first_page; page <= last_page; ++page) {
        const uint8_t commands[] = {
            0x21, (uint8_t)x1, (uint8_t)x2,
            0x22, page, page,
        };
        esp_err_t err = oled_write_command(commands, sizeof(commands));
        if (err == ESP_OK) {
            const size_t offset = (size_t)page * OLED_WIDTH + (size_t)x1;
            const size_t length = (size_t)(x2 - x1 + 1);
            for (size_t i = 0; i < length; ++i) {
                data[i + 1] = s_framebuffer[offset + i];
            }
            err = i2c_master_transmit(s_oled, data, length + 1, OLED_FLUSH_TIMEOUT_MS);
        }
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Display flush failed: %s", esp_err_to_name(err));
            break;
        }
    }
    lv_display_flush_ready(display);
}

static void oled_lvgl_task(void *arg)
{
    while (true) {
        if (xSemaphoreTake(s_lvgl_mutex, portMAX_DELAY) == pdTRUE) {
            lv_timer_handler();
            xSemaphoreGive(s_lvgl_mutex);
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
}

static void format_centi(char *buffer, size_t length, int32_t value)
{
    const uint32_t magnitude = (uint32_t)(value < 0 ? -value : value);
    snprintf(buffer, length, "%c%u.%02u", value < 0 ? '-' : '+',
             (unsigned)(magnitude / 100), (unsigned)(magnitude % 100));
}

esp_err_t oled_lvgl_init(i2c_master_bus_handle_t i2c_bus)
{
    if (s_oled == NULL) {
        const i2c_device_config_t config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = OLED_I2C_ADDRESS,
            .scl_speed_hz = 400000,
        };
        ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(i2c_bus, &config, &s_oled),
                            TAG, "Add OLED to I2C bus failed");
    }

    ESP_RETURN_ON_ERROR(i2c_master_probe(i2c_bus, OLED_I2C_ADDRESS, OLED_FLUSH_TIMEOUT_MS),
                        TAG, "OLED not found on I2C bus");
    
    const uint8_t init[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40,
        0x8D, 0x14, 0x20, 0x00, 0xA1, 0xC8, 0xDA, 0x12,
        0x81, 0xCF, 0xD9, 0xF1, 0xDB, 0x40, 0xA4, 0xA6, 0xAF,
    };
    ESP_RETURN_ON_ERROR(oled_write_command(init, sizeof(init)), TAG, "OLED init failed");

    uint8_t clear_data[OLED_WIDTH * OLED_HEIGHT / 8 + 1] = {0x40};
    ESP_RETURN_ON_ERROR(i2c_master_transmit(s_oled, clear_data, sizeof(clear_data),
                                            OLED_FLUSH_TIMEOUT_MS),
                        TAG, "Clear OLED failed");

    lv_init();
    lv_tick_set_cb(lvgl_get_tick);
    s_lvgl_mutex = xSemaphoreCreateMutex();
    if (s_lvgl_mutex == NULL) {
        return ESP_ERR_NO_MEM;
    }

    s_display = lv_display_create(OLED_WIDTH, OLED_HEIGHT);
    if (s_display == NULL) {
        return ESP_ERR_NO_MEM;
    }
    lv_display_set_color_format(s_display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(s_display, s_draw_buffer, NULL, sizeof(s_draw_buffer),
                           LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(s_display, oled_flush);

    lv_obj_t *screen = lv_screen_active();
    lv_obj_set_style_bg_color(screen, lv_color_black(), LV_PART_MAIN);
    lv_obj_set_style_bg_opa(screen, LV_OPA_COVER, LV_PART_MAIN);

    lv_obj_t *title = lv_label_create(screen);
    lv_label_set_text(title, "AutoUAV MPU6050");
    lv_obj_set_style_text_color(title, lv_color_white(), LV_PART_MAIN);
    lv_obj_align(title, LV_ALIGN_TOP_LEFT, 0, 0);

    static const char *axis_names[] = {"X", "Y", "Z"};
    for (size_t i = 0; i < 3; ++i) {
        s_axis_labels[i] = lv_label_create(screen);
        lv_label_set_text_fmt(s_axis_labels[i], "%s +0.00g +0dps", axis_names[i]);
        lv_obj_set_style_text_color(s_axis_labels[i], lv_color_white(), LV_PART_MAIN);
        lv_obj_align(s_axis_labels[i], LV_ALIGN_TOP_LEFT, 0, 16 + (int32_t)i * 15);
    }

    const BaseType_t task_result = xTaskCreate(oled_lvgl_task, "oled_lvgl", 4096, NULL, 4,
                                                NULL);
    if (task_result != pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

void oled_lvgl_update(int16_t accel_x, int16_t accel_y, int16_t accel_z,
                      int16_t gyro_x, int16_t gyro_y, int16_t gyro_z)
{
    const int16_t accel_raw[] = {accel_x, accel_y, accel_z};
    const int16_t gyro_raw[] = {gyro_x, gyro_y, gyro_z};

    if (s_display == NULL || s_lvgl_mutex == NULL ||
        xSemaphoreTake(s_lvgl_mutex, portMAX_DELAY) != pdTRUE) {
        return;
    }

    static const char *axis_names[] = {"X", "Y", "Z"};
    for (size_t i = 0; i < 3; ++i) {
        char accel[12];
        char gyro[12];
        format_centi(accel, sizeof(accel), (int32_t)accel_raw[i] * 100 / 16384);
        format_centi(gyro, sizeof(gyro), (int32_t)gyro_raw[i] * 100 / 13100);
        lv_label_set_text_fmt(s_axis_labels[i], "%s %sg %sdps",
                              axis_names[i], accel, gyro);
    }
    xSemaphoreGive(s_lvgl_mutex);
}