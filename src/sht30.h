#ifndef SHT30_H
#define SHT30_H

#include <stdint.h>
#include <stddef.h>   /* size_t（crc8 原型）+ NULL，IWYU */

#define SHT30_I2C_ADDR        (0x44u)   /* ADDR 引脚接地；接 VCC 为 0x45 */
#define SHT30_CMD_MEAS_HI_MSB (0x24u)   /* 单次测量、高重复性、禁时钟延展 */
#define SHT30_CMD_MEAS_HI_LSB (0x00u)

typedef enum {
    SHT30_OK = 0,
    SHT30_ERR_PARAM,
    SHT30_ERR_NACK,
    SHT30_ERR_TIMEOUT,
    SHT30_ERR_BUS,
    SHT30_ERR_CRC          /* 读回数据 CRC8 校验失败（总线受干扰的典型症状） */
} sht30_status_t;

/* 发送单次测量命令（0x2400，先发 MSB） */
sht30_status_t sht30_trigger_measurement(uint8_t dev_addr);

/*
 * 读取 6 字节测量结果（温度2+CRC1，湿度2+CRC1），校验 CRC 并换算：
 * T  = -45 + 175 * raw / 65535   (°C)
 * RH = 100 * raw / 65535         (%)
 * 契约：仅返回 SHT30_OK 时 *temperature/*humidity 才被写入。
 */
sht30_status_t sht30_read_measurement(uint8_t dev_addr, float *temperature, float *humidity);

/*
 * CRC-8/Sensirion：多项式 0x31，初值 0xFF，无反射，无最终异或。
 * 数据手册已知向量：CRC(0xBEEF) = 0x92。空输入返回初值 0xFF。
 */
uint8_t sht30_crc8(const uint8_t *data, size_t len);

#endif /* SHT30_H */
