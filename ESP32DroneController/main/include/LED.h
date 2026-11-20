#include <stdint.h>
#include "driver/ledc.h"
#include "esp_err.h"
#include "esp_check.h"
#define LEDC_PIN        2      // 板载 LED，按你的板子改
#define LED_ON 1      // 输出高电平点亮
#define LEDC_CH        LEDC_CHANNEL_0
#define LEDC_TIMER     LEDC_TIMER_0
#define LEDC_FREQ      5000   // 5kHz，避免可闻噪声
#define LEDC_RES       LEDC_TIMER_8_BIT // 5000*2^8 = 1280000Hz所以只要大于1.28Mhz的始终都可以
#define LEDC_CLK       LEDC_SLOW_CLK_RC_FAST
#define LEDC_MODE      LEDC_LOW_SPEED_MODE
#define LED_MAX_DUTY   (uint8_t)((1 << LEDC_RES) - 1)
// 一段：持续 dur_ms 毫秒，占空比 duty(0~255)，fade=1 表示从上一段平滑渐变过来

typedef enum {
    LED_MODE_BREATH = 0,
    LED_MODE_BLINK_SLOW,
    LED_MODE_BLINK_FAST,
    LED_MODE_DOUBLE_FLASH,
    LED_MODE_SOLID_ON,
    LED_MODE_OFF,
    LED_MODE_COUNT
} led_mode_id_t;

typedef struct {
    uint16_t duty;        // 目标占空比
    uint16_t fade_ms;     // 渐变到该 duty 的时长；0 = 立即跳变
    uint16_t hold_ms;     // 到达后停留时间
} led_frame_t;

typedef struct {
    const char      *name;
    const led_frame_t *frames;
    uint8_t          frame_cnt;
    bool             loop;       // false = 播完停住
} led_pattern_t;


esp_err_t led_init(led_mode_id_t mode); //led被激活后就使用的模式
void led_mode_switch(led_mode_id_t mode);


esp_err_t led_hw_init(void);
void led_breath_fade_task(void*);
