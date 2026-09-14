#ifndef HAL_I2C_H
#define HAL_I2C_H

/*
 * I2C 硬件抽象层——只有接口，没有实现。
 * 这是依赖倒置（DIP）的落地：
 *  - 量产固件里由 BSP 提供实现（寄存器/中断/DMA）
 *  - 单元测试里由 CMock 自动生成 mock_hal_i2c.c
 * 上层驱动（sht30）只面向这个接口编程，对"硬件是否存在"一无所知。
 */
#include <stdint.h>

typedef enum {
    HAL_I2C_OK = 0,
    HAL_I2C_ERR_NACK,      /* 从站不应答：掉线/地址错/忙 */
    HAL_I2C_ERR_TIMEOUT,   /* SCL 被拉死或从站拖太久     */
    HAL_I2C_ERR_BUS,       /* 仲裁丢失/总线错误          */
    HAL_I2C_ERR_PARAM      /* HAL 层入参非法             */
} hal_i2c_status_t;

hal_i2c_status_t hal_i2c_write(uint8_t dev_addr, const uint8_t *data, uint16_t len);
hal_i2c_status_t hal_i2c_read (uint8_t dev_addr, uint8_t *data, uint16_t len);

#endif /* HAL_I2C_H */
