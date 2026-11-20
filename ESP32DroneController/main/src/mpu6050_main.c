#include <stdbool.h>
#include <inttypes.h>
#include <stdint.h>

#include "driver/i2c_master.h"
#include "esp_check.h"
#include "esp_err.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "driver/gpio.h"
#include "mpu6050.h"
#include "LED.h"
#include "button.h"




static const char *TAG = "mpu6050";
static i2c_master_dev_handle_t s_mpu6050;
static SemaphoreHandle_t mpu_data_ready_sem = NULL; //创建信号量

typedef struct {
    int16_t accel_x;
    int16_t accel_y;
    int16_t accel_z;
    int16_t temperature;
    int16_t gyro_x;
    int16_t gyro_y;
    int16_t gyro_z;
} mpu6050_sample_t;

static esp_err_t mpu6050_write_register(uint8_t reg, uint8_t value)
{   // 
    const uint8_t packet[] = {reg, value};
    return i2c_master_transmit(s_mpu6050, packet, sizeof(packet), I2C_TIMEOUT_MS);
}

static esp_err_t mpu6050_read_register(uint8_t reg, uint8_t *value)
{   // 先写寄存器地址，再读数据
    printf("Write Register: 0x%02X, size: %lu\n", reg, (unsigned long)sizeof(reg));
    return i2c_master_transmit_receive(s_mpu6050, &reg, sizeof(reg),
                                       value, sizeof(*value), I2C_TIMEOUT_MS);
}

static int16_t read_int16_be(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[0] << 8) | data[1]);
}

static esp_err_t mpu6050_read_sample(mpu6050_sample_t *sample, uint64_t *read_time_ns)
{
    const uint8_t first_register = MPU6050_REG_ACCEL_XOUT_H;
    // 读取加速度计、陀螺仪和温度计的原始数据，一次性读取14个字节
    uint8_t data[14];

    const int64_t start_time_us = esp_timer_get_time();
    esp_err_t err = i2c_master_transmit_receive(s_mpu6050, &first_register,
                                                sizeof(first_register), data,
                                                sizeof(data), I2C_TIMEOUT_MS);
    *read_time_ns = (uint64_t)(esp_timer_get_time() - start_time_us) * 1000;
    ESP_RETURN_ON_ERROR(err, TAG, "Read sensor sample failed");

    sample->accel_x = read_int16_be(&data[0]);
    sample->accel_y = read_int16_be(&data[2]);
    sample->accel_z = read_int16_be(&data[4]);
    sample->temperature = read_int16_be(&data[6]);
    sample->gyro_x = read_int16_be(&data[8]);
    sample->gyro_y = read_int16_be(&data[10]);
    sample->gyro_z = read_int16_be(&data[12]);
    return ESP_OK;
}

static esp_err_t mpu6050_init(bool enable_interrupt)
{
    i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_PORT,
        .sda_io_num = I2C_SDA_GPIO, // GPIO_21
        .scl_io_num = I2C_SCL_GPIO, // GPIO_22
        .clk_source = I2C_CLK_SRC_DEFAULT, //默认是APB_CLK
        .glitch_ignore_cnt = 7,
        .flags.enable_internal_pullup = true, // 因为没有外接上拉电阻，只能开启内部上拉。
    };
    i2c_master_bus_handle_t bus;
    // bus里面有互斥信号量 bus_lock_mux，保证了对总线的访问是线程安全的。这是一个二值信号量。通过：xSemaphoreCreateBinaryWithCaps 创建。
    ESP_RETURN_ON_ERROR(i2c_new_master_bus(&bus_config, &bus),
                        TAG, "I2C bus init failed");

    uint16_t address = MPU6050_ADDR_AD0_LOW;
    esp_err_t probe_result = i2c_master_probe(bus, address, 100); // 代表100ms
    if (probe_result == ESP_ERR_NOT_FOUND) {
        address = MPU6050_ADDR_AD0_HIGH;
        probe_result = i2c_master_probe(bus, address, 100);
    }
    if (probe_result != ESP_OK) {
        ESP_LOGE(TAG, "MPU6050 not found at 0x68 or 0x69: %s",
                 esp_err_to_name(probe_result));
        return probe_result;
    }
    ESP_LOGI(TAG, "MPU6050 responded at I2C address 0x%02X", address);

    i2c_device_config_t device_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = address,
        .scl_speed_hz = I2C_FREQUENCY_HZ_FM,
    };
    ESP_RETURN_ON_ERROR(i2c_master_bus_add_device(bus, &device_config, &s_mpu6050),
                        TAG, "Add MPU6050 to I2C bus failed");

    uint8_t who_am_i = 0;
    ESP_RETURN_ON_ERROR(mpu6050_read_register(MPU6050_REG_WHO_AM_I, &who_am_i),
                        TAG, "Read WHO_AM_I failed");
    if (who_am_i != 0x68) {
        ESP_LOGE(TAG, "Unexpected WHO_AM_I value: 0x%02X (expected 0x68)", who_am_i);
        return ESP_ERR_NOT_SUPPORTED;
    }
  

    ESP_RETURN_ON_ERROR(mpu6050_write_register(MPU6050_REG_PWR_MGMT_1, 0x80),
                        TAG, "Reset MPU6050 failed");
    vTaskDelay(pdMS_TO_TICKS(100));
    ESP_RETURN_ON_ERROR(mpu6050_write_register(MPU6050_REG_PWR_MGMT_1, 0x00),
                        TAG, "Wake MPU6050 failed");
    ESP_RETURN_ON_ERROR(mpu6050_write_register(MPU6050_REG_CONFIG, 0x03),
                        TAG, "Configure digital low-pass filter failed");
    ESP_RETURN_ON_ERROR(mpu6050_write_register(MPU6050_REG_SMPLRT_DIV, 0x07),
                        TAG, "Configure sample rate failed");
    ESP_RETURN_ON_ERROR(mpu6050_write_register(MPU6050_REG_GYRO_CONFIG, 0x00), // 250°/s
                        TAG, "Configure gyro range failed");
    ESP_RETURN_ON_ERROR(mpu6050_write_register(MPU6050_REG_ACCEL_CONFIG, 0x00), // ±2g
                        TAG, "Configure accelerometer range failed");
    if (enable_interrupt) {
        // enable interrupts on MPU6050, so that we can use interrupt-driven data acquisition in the future.
        ESP_RETURN_ON_ERROR(mpu6050_write_register(MPU6050_INTERRUPT_ENABLE, 0x01),
                            TAG, "Enable MPU6050 interrupts failed");
        ESP_LOGI(TAG, "INTERRUPT enabled: 0x%02X", who_am_i);
    }
    return ESP_OK;
}

// 该函数打印传感器的原始数据和换算后的数据，并检查传入参数的有效性。
static esp_err_t log_sample(const mpu6050_sample_t *sample)
{
    // 原始数据到测量范围的换算：比如加速度计的测量范围是±2g，对应的原始数据范围是-32768到32767，所以换算公式是：mg = 原始数据 * 1000 / 16384
    // 所以换算其实也会带来误差是吗？
    // 比如-250°/s 对应的原始数据是-32768，换算后是 -250.0152587890625°/s，误差是0.0152587890625°/s
    // 但是原始数据不能到-32768吗？因为原始数据是16位有符号整数，范围是-32768到32767，所以换算后最大值是 32767 * 1000 / 16384 = 1999.93896484375 mg，误差是0.06103515625 mg
    // 如何在真实场景中把这个误差消除掉:
    // 1. 通过校准，获取传感器的偏置值，然后在软件中进行补偿。
    // 2. 使用更高精度的传感器，或者使用多传感器融合算法来提高精度。
    // 3. 使用滤波算法，比如卡尔曼滤波，来减少噪声和误差。
    // 4. 在软件中进行数据平滑处理，比如移动平均滤波，来减少误差。
    const int32_t ax_mg = (int32_t)sample->accel_x * 1000 / 16384; //16384是65536的四分之一。
    const int32_t ay_mg = (int32_t)sample->accel_y * 1000 / 16384;
    const int32_t az_mg = (int32_t)sample->accel_z * 1000 / 16384;
    const int32_t gx_mdps = (int32_t)sample->gyro_x * 1000 / 131; //  131 = 65536/500 所以 每一度/秒对应的原始数据是 131，换算成 mdps（毫度每秒）就是 原始数据 * 1000 / 131
    const int32_t gy_mdps = (int32_t)sample->gyro_y * 1000 / 131;
    const int32_t gz_mdps = (int32_t)sample->gyro_z * 1000 / 131;
    const int32_t temperature_centi_c = (int32_t)sample->temperature * 100 / 340 + 3653; // 温度范围是-40°C到+85°C，而是按固定的 340 LSB/°C 线性变化，0对应室温偏移：36.53°C（对应原始数据 0）

    const int32_t temperature_c = temperature_centi_c / 100;
    const int32_t ax = ax_mg / 1000;
    const int32_t ay = ay_mg / 1000;
    const int32_t az = az_mg / 1000;
    const int32_t gx = gx_mdps / 1000;
    const int32_t gy = gy_mdps / 1000;
    const int32_t gz = gz_mdps / 1000;


    ESP_LOGI(TAG,
             "Accel[g] X=%" PRId32 " Y=%" PRId32 " Z=%" PRId32
             " | Gyro[dps] X=%" PRId32 " Y=%" PRId32 " Z=%" PRId32
             " | Temp[1C]=%" PRId32,
             ax, ay, az, gx, gy, gz, temperature_c);
    return ESP_OK;
}

void mpu_data_task(void *pvParameters) {
    mpu6050_sample_t sample;
    while (1) {
        // 阻塞等待，直到 ISR 释放信号量（表示数据就绪）
        if (xSemaphoreTake(mpu_data_ready_sem, portMAX_DELAY) == pdTRUE) {
            // 此时再执行 I²C 读取，完全避开 ISR 的严格限制
            uint8_t raw_data[14];
            uint64_t read_time_ns;
            esp_err_t err = mpu6050_read_sample(&sample, &read_time_ns);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Sensor read failed: %s", esp_err_to_name(err));
                continue;
            }
            // ESP_LOGI(TAG, "Data ready, read sensor data successfully.");
            log_sample(&sample);
        }
    }
}

static volatile uint32_t s_isr_count = 0; // static 定义变量只在本文件中可用，不能共享。
static void IRAM_ATTR mpu_int_isr_handler(void *arg) { // 这段代码必须放在IRAM中，而不是默认的flash中。
    // 仅释放信号量，通知任务“数据来了”
    s_isr_count++;
    xSemaphoreGiveFromISR(mpu_data_ready_sem, NULL);
}


void app_main(void)
{
    esp_err_t err = led_init(1);
    button_init();
    if (err != ESP_OK){
        ESP_LOGE(TAG, "led init failed %s", esp_err_to_name(err));
    }
    vTaskDelay(pdMS_TO_TICKS(2000));
    led_mode_switch(1);

    char TAG[]="12345";
    err = mpu6050_init(true);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Initialization stopped: %s", esp_err_to_name(err));
        return;
    }

    // ① 先创建信号量，并检查是否成功
    mpu_data_ready_sem = xSemaphoreCreateBinary();
    if (mpu_data_ready_sem == NULL) {
        ESP_LOGE(TAG, "failed to create semaphore");
        return;   // 或 abort，但不能继续
    }

    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << GPIO_NUM_23),
        .mode         = GPIO_MODE_INPUT, // 设置为输入模式
        .pull_down_en = GPIO_PULLDOWN_ENABLE, // 下拉防止悬空。
        .intr_type    = GPIO_INTR_POSEDGE, //上升沿中断。
    };

    err = gpio_config(&io_conf);
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
    gpio_isr_handler_add(GPIO_NUM_23, mpu_int_isr_handler, NULL);

     BaseType_t ret = xTaskCreatePinnedToCore(
        mpu_data_task,          // 任务函数
        "mpu6050",             // 名称
        4096,   // 栈大小（字节）
        (void *)1,                  // 无参数
        10,     // 优先级
        NULL,                  // 不需要句柄
        1          // 绑定到 Core 1
    );

    if (ret != pdPASS) {
        ESP_LOGE(TAG, "failed to create mpu6050 task");
    }

    mpu6050_sample_t sample;
    while (true) {
        ESP_LOGI(TAG, "Count of interrupts: %u", s_isr_count);
        vTaskDelay(pdMS_TO_TICKS(500));
        
    }

}
