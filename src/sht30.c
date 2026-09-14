#include "sht30.h"
#include "hal_i2c.h"   /* 只依赖接口——链接时才决定是 BSP 实现还是 CMock */

/* HAL 错误到驱动错误的显式映射：
 * 显式 switch 而不是强转枚举值，是为了让 gcov 分支覆盖把每条错误路径都逼出来；
 * default 与显式 case 分开写——这个兜底分支是变异测试 M6 的"案发现场" */
static sht30_status_t map_hal_status(hal_i2c_status_t status)
{
    switch (status) {
    case HAL_I2C_OK:          return SHT30_OK;
    case HAL_I2C_ERR_NACK:    return SHT30_ERR_NACK;
    case HAL_I2C_ERR_TIMEOUT: return SHT30_ERR_TIMEOUT;
    case HAL_I2C_ERR_BUS:     return SHT30_ERR_BUS;
    case HAL_I2C_ERR_PARAM:   return SHT30_ERR_PARAM;
    default:                  return SHT30_ERR_PARAM;  /* 未知/越界枚举兜底 */
    }
}

uint8_t sht30_crc8(const uint8_t *data, size_t len)
{
    if ((data == NULL) || (len == 0u)) {
        return 0xFFu;
    }
    uint8_t crc = 0xFFu;
    for (size_t i = 0u; i < len; i++) {
        crc ^= data[i];
        for (unsigned bit = 0u; bit < 8u; bit++) {
            /* MSB 优先（无反射），与手册一致 */
            crc = ((crc & 0x80u) != 0u) ? (uint8_t)((uint8_t)(crc << 1) ^ 0x31u)
                                        : (uint8_t)(crc << 1);
        }
    }
    return crc;
}

sht30_status_t sht30_trigger_measurement(uint8_t dev_addr)
{
    /* 命令字 0x2400 按 MSB 先发——字节序错误有专门的 ExpectWithArray 用例守候 */
    const uint8_t cmd[2] = {SHT30_CMD_MEAS_HI_MSB, SHT30_CMD_MEAS_HI_LSB};
    return map_hal_status(hal_i2c_write(dev_addr, cmd, 2u));
}

sht30_status_t sht30_read_measurement(uint8_t dev_addr, float *temperature, float *humidity)
{
    /* 参数检查必须发生在任何总线动作之前——"零交互"有专门用例验证 */
    if ((temperature == NULL) || (humidity == NULL)) {
        return SHT30_ERR_PARAM;
    }

    uint8_t buf[6];
    hal_i2c_status_t hal_status = hal_i2c_read(dev_addr, buf, 6u);
    if (hal_status != HAL_I2C_OK) {
        return map_hal_status(hal_status);
    }

    /* 温度与湿度各自独立校验 CRC：任一组失败都不得换算 */
    if (sht30_crc8(&buf[0], 2u) != buf[2]) {
        return SHT30_ERR_CRC;
    }
    if (sht30_crc8(&buf[3], 2u) != buf[5]) {
        return SHT30_ERR_CRC;
    }

    uint16_t raw_t = (uint16_t)(((uint16_t)buf[0] << 8) | buf[1]);
    uint16_t raw_h = (uint16_t)(((uint16_t)buf[3] << 8) | buf[4]);

    *temperature = -45.0f + 175.0f * ((float)raw_t / 65535.0f);
    *humidity    = 100.0f * ((float)raw_h / 65535.0f);
    return SHT30_OK;
}
