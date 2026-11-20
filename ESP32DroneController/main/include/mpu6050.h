#define I2C_PORT              I2C_NUM_0
#define I2C_SCL_GPIO          22
#define I2C_SDA_GPIO          21 // I2C总线的SCL和SDA引脚/
#define I2C_INTERRUPT_GPIO      5 // MPU6050中断引脚连接到ESP32的 GPIO 5 引脚
#define I2C_FREQUENCY_HZ_FM      400000 // 400 kHz 该模式下，完全读取一次数据需要578us，频率大于1.7khz。留给数据解算的时间很充裕。
#define I2C_TIMEOUT_MS        1000 // 超时时间1s

#define MPU6050_ADDR_AD0_LOW  0x68 // 7-bit I2C address when AD0 pin is low
#define MPU6050_ADDR_AD0_HIGH 0x69 // 7-bit I2C address when AD0 pin is high


#define MPU6050_REG_SMPLRT_DIV   0x19 //采样率分频器寄存器，典型值 0x07（采样率为1kHz）
#define MPU6050_REG_CONFIG       0x1A // 设置低通滤波器截至频率，典型值 0x03（低通滤波器截止频率为44Hz） 0x06（低通滤波器截止频率为5Hz）
#define MPU6050_REG_GYRO_CONFIG  0x1B // 陀螺仪配置寄存器，典型值 0x00（±250°/s） 0x08（±500°/s） 0x10（±1000°/s） 0x18（±2000°/s）
#define MPU6050_REG_ACCEL_CONFIG 0x1C // 加速度计配置寄存器，典型值 0x00（±2g） 0x08（±4g） 0x10（±8g） 0x18（±16g）
#define MPU6050_REG_ACCEL_XOUT_H  0x3B //加速度计X轴高字节寄存器
#define MPU6050_REG_ACCEL_XOUT_L  0x3C //加速度计X轴低字节寄存器
#define MPU6050_REG_ACCEL_YOUT_H  0x3D //加速度计Y轴高字节寄存器
#define MPU6050_REG_ACCEL_YOUT_L  0x3E //加速度计Y轴低字节寄存器
#define MPU6050_REG_ACCEL_ZOUT_H  0x3F //加速度计Z轴高字节寄存器
#define MPU6050_REG_ACCEL_ZOUT_L  0x40 //加速度计Z轴低字节寄存器
#define MPU6050_REG_TEMP_OUT_H    0x41 //温度计高字节寄存器
#define MPU6050_REG_TEMP_OUT_L    0x42 //温度计低字节寄存器
#define MPU6050_REG_GYRO_XOUT_H   0x43 //陀螺仪X轴高字节寄存器
#define MPU6050_REG_GYRO_XOUT_L   0x44 //陀螺仪X轴低字节寄存器
#define MPU6050_REG_GYRO_YOUT_H   0x45 //陀螺仪Y轴高字节寄存器
#define MPU6050_REG_GYRO_YOUT_L   0x46 //陀螺仪Y轴低字节寄存器
#define MPU6050_REG_GYRO_ZOUT_H   0x47 //陀螺仪Z轴高字节寄存器
#define MPU6050_REG_GYRO_ZOUT_L   0x48 //陀螺仪Z轴低字节寄存器
#define MPU6050_REG_PWR_MGMT_1   0x6B //电源管理寄存器，典型值 0x0（正常启动）
#define MPU6050_REG_WHO_AM_I     0x75 //自检寄存器，典型值 0x68（表示设备ID为0x68）
#define MPU6050_INTERRUPT_ENABLE 0x38 //中断使能寄存器，典型值 0x01（数据准备好中断使能）
#define MPU6050_INTERRUPT_STATUS 0x37 //中断状态寄存器，典型值 0x00（无中断）0x37的LATCH_INT_EN位设为1，INT引脚就进入锁存模式 数据就绪后，INT引脚会持续保持高电平，而不是短暂脉冲 
/*INT_RD_CLEAR位的配置：

如果 INT_RD_CLEAR = 0（默认）：你必须显式读取中断状态寄存器 INT_STATUS (0x3A)，中断才会被清除，INT引脚才会被拉低。单纯读取传感器数据（如0x3B开始的14字节）并不会清除中断。

如果 INT_RD_CLEAR = 1：任何I²C读操作（包括读传感器数据或读状态寄存器）都会自动清除中断，并将INT引脚拉低。
Bit:    7          6          5            4            3                2            1            0
Name: INT_LEVEL  INT_OPEN  LATCH_INT_EN  INT_RD_CLEAR  FSYNC_INT_LEVEL  FSYNC_INT_EN  I2C_BYPASS_EN  —
=0:   高有效     推挽       脉冲模式      仅0x3A清除    高有效           禁用         禁用           —
=1:   低有效     开漏       锁存模式      任何读清除     低有效           使能         使能           —
但在脉冲模式下，INT 引脚已经在 50µs 后自动拉低，所以这个清除位实际影响不大，你的 ISR 只需要捕捉那个上升沿即可
*/