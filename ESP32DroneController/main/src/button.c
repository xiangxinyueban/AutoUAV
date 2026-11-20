#include "button.h"
#include "LED.h"
static const char* TAG = "BUTTON";
static led_mode_id_t i = 0;
static void IRAM_ATTR led_switch(void *arg) { // 这段代码必须放在IRAM中，而不是默认的flash中。
    // 仅释放信号量，通知任务“数据来了”
    if (++i>5) {
        i = 0;
    }
    led_mode_switch(i);
}

// boot按键中断。
void button_init(void){
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_0), 
        .mode         = GPIO_MODE_INPUT, // 设置为输入模式
        .pull_up_en = GPIO_PULLUP_ENABLE, 
        .pull_down_en = GPIO_PULLDOWN_DISABLE, // 下拉防止悬空。
        .intr_type    = GPIO_INTR_NEGEDGE, //下降沿中断。
    };
    esp_err_t err = gpio_config(&io_conf);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO config failed: %s", esp_err_to_name(err));
        return;
    }
        // 安装 GPIO 中断服务，并检查是否成功
    err = gpio_install_isr_service(ESP_INTR_FLAG_IRAM); // ESP_INTR_FLAG_IRAM
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO ISR service installation failed: %s", esp_err_to_name(err));
        return;
    }
    err = gpio_isr_handler_add(GPIO_NUM_0, led_switch, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "GPIO ISR handler registration failed: %s", esp_err_to_name(err));
        return;
    }
}