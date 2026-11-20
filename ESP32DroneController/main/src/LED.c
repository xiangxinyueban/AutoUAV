#include "LED.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_err.h"
#include <stdint.h>
static const char *TAG = "LED";
static QueueHandle_t mode_queue;



// 模式表：
static const led_frame_t s_breath[] = {
    { .duty = LED_MAX_DUTY, .fade_ms = 2046, .hold_ms = 0 },
    { .duty = 0,            .fade_ms = 2046, .hold_ms = 0 },
};

static const led_frame_t s_blink_slow[] = {
    { .duty = LED_MAX_DUTY, .fade_ms = 0, .hold_ms = 300 },
    { .duty = 0,            .fade_ms = 0, .hold_ms = 700 },
};

static const led_frame_t s_blink_fast[] = {
    { .duty = LED_MAX_DUTY, .fade_ms = 0, .hold_ms = 80 },
    { .duty = 0,            .fade_ms = 0, .hold_ms = 80 },
};

static const led_frame_t s_double_flash[] = {
    { .duty = LED_MAX_DUTY, .fade_ms = 0, .hold_ms = 100 },
    { .duty = 0,            .fade_ms = 0, .hold_ms = 100 },
    { .duty = LED_MAX_DUTY, .fade_ms = 0, .hold_ms = 100 },
    { .duty = 0,            .fade_ms = 0, .hold_ms = 600 },
};

static const led_frame_t s_solid[] = {
    { .duty = LED_MAX_DUTY, .fade_ms = 500, .hold_ms = 0 },
};

static const led_frame_t s_off[] = {
    { .duty = 0,            .fade_ms = 500, .hold_ms = 0 },
};

static const led_pattern_t s_patterns[LED_MODE_COUNT] = {
    [LED_MODE_BREATH]       = { "breath",       s_breath,        2, true },
    [LED_MODE_BLINK_SLOW]   = { "blink_slow",   s_blink_slow,    2, true },
    [LED_MODE_BLINK_FAST]   = { "blink_fast",   s_blink_fast,    2, true },
    [LED_MODE_DOUBLE_FLASH] = { "double_flash", s_double_flash,  4, true },
    [LED_MODE_SOLID_ON]     = { "solid_on",     s_solid,         1, false },
    [LED_MODE_OFF]          = { "off",          s_off,           1, false },
};

static const led_pattern_t *s_pat;   // 当前模式
static uint8_t              s_idx;   // 当前帧索引

/*
LED 工作在不同模式，不同工作状态指定不同闪烁频率，比如初始化阶段闪烁频率高，运行阶段闪烁频率低，错误状态开双闪。
*/



esp_err_t led_hw_init(void)
{
    ledc_timer_config_t t = {
        .speed_mode      = LEDC_MODE,
        .timer_num       = LEDC_TIMER,
        .duty_resolution = LEDC_RES,
        .freq_hz         = LEDC_FREQ,
        .clk_cfg         = LEDC_CLK,    // 你选的时钟源
    };
    esp_err_t err = ledc_timer_config(&t);
    if (err != ESP_OK) {
        return err;
    }
    ledc_channel_config_t c = {
        .gpio_num   = LEDC_PIN,
        .speed_mode = LEDC_MODE,
        .channel    = LEDC_CH,
        .timer_sel  = LEDC_TIMER,
        .duty       = 0,                    // 初值给 0，上电先灭
        .intr_type  = LEDC_INTR_DISABLE,
    };
    err = ledc_channel_config(&c);
    if (err != ESP_OK) {
        return err;
    }
    err = ledc_fade_func_install(0);
    if (err != ESP_OK) {
        return err;
    }
    return ESP_OK;

}

void led_hw_set_duty(uint8_t duty)
{
    ledc_set_duty(LEDC_MODE, LEDC_CH, duty);
    ledc_update_duty(LEDC_MODE, LEDC_CH);             // 必须成对
}

void apply_mode(led_mode_id_t id) {
    s_pat = &s_patterns[id];
    s_idx = 0;
}

// FreeRTOS task entry; declare this signature in LED.h before using xTaskCreate().
void led_breath_fade_task(void *pvParameters)
{
    (void)pvParameters; // 忽略未用参数，消除编译警告
    uint32_t target = LED_MAX_DUTY/2;
    led_mode_id_t mode_id = (led_mode_id_t)(uintptr_t)pvParameters;
    (void)mode_id;
    apply_mode(mode_id);
    while (1) {
        if (xQueueReceive(mode_queue, &mode_id, 0) == pdTRUE) { 
            //如果读不到，xQueueReceive 会返回 pdFALSE，变量 id 会保持原来的值不变。
            ESP_LOGI(TAG, "LED mode switched", "");
            apply_mode(mode_id);
        }

        if (!s_pat || s_pat->frame_cnt == 0) {
            vTaskDelay(pdMS_TO_TICKS(100));
            continue;
        }
        // ESP_LOGI(TAG, "mode_id_t: %d s_idx: %d frame_cnt:%d", mode_id, s_idx, s_pat->frame_cnt);
        const led_frame_t *f = &s_pat->frames[s_idx];

        if (f->fade_ms > 0) {
            ledc_set_fade_with_time(LEDC_MODE, LEDC_CH, f->duty, f->fade_ms);
            ledc_fade_start(LEDC_MODE, LEDC_CH, LEDC_FADE_WAIT_DONE);
        } else {
            ledc_set_duty(LEDC_MODE, LEDC_CH, f->duty);
            ledc_update_duty(LEDC_MODE, LEDC_CH);
        }

          if (f->hold_ms) vTaskDelay(pdMS_TO_TICKS(f->hold_ms));

          if (++s_idx >= s_pat->frame_cnt) {
            if (s_pat->loop) {
                s_idx = 0;
            } else {
                /* 不循环的模式，播完停在最后一帧，等切换命令 */
                while (xQueueReceive(mode_queue, &mode_id, portMAX_DELAY) != pdTRUE) {
                    /* 阻塞等命令，醒来后再走整个 while 循环 */
                }
                apply_mode(mode_id);
            }
        }

        // 2046 ms 见下文解释；先用 WAIT_DONE，代码最简单
        ESP_ERROR_CHECK(ledc_set_fade_with_time(LEDC_MODE, LEDC_CH, target, 200)); // 2046ms
        ESP_ERROR_CHECK(ledc_fade_start(LEDC_MODE, LEDC_CH, LEDC_FADE_NO_WAIT));

        target = (target == 0) ? LED_MAX_DUTY : 0;   // 反向
    }
}

esp_err_t led_init(led_mode_id_t mode){
    esp_err_t err = led_hw_init();
    if (err != ESP_OK){
        ESP_LOGI(TAG, "LED hardware init failed: %s", esp_err_to_name(err));
    }

    // 创建队列：
    mode_queue = xQueueCreate(4, sizeof(led_mode_id_t));
    BaseType_t ret = xTaskCreatePinnedToCore(
        led_breath_fade_task,          // 任务函数
        "LED blink",             // 名称
        4096,   // 栈大小（字节）
        (void *)2,                  // 参数2
        10,     // 优先级
        NULL,                  // 不需要句柄
        1          // 绑定到 Core 1
    );
    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create LED blink task");
    }
    
    return err;
}

void IRAM_ATTR led_mode_switch(led_mode_id_t mode){
    if (mode_queue) xQueueSend(mode_queue, &mode, 0);   // 非阻塞投递
}