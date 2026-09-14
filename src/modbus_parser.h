#ifndef MODBUS_PARSER_H
#define MODBUS_PARSER_H

#include <stdint.h>
#include <stddef.h>

/* Modbus RTU 帧：[地址][功能码][数据...][CRC-lo][CRC-hi] */
#define MODBUS_MAX_FRAME_LEN   (256u)
#define MODBUS_MIN_FRAME_LEN   (4u)     /* 地址+功能码+CRC，数据可为 0 字节 */
#define MODBUS_MAX_DATA_LEN    (252u)
#define MODBUS_BROADCAST_ADDR  (0u)     /* 广播地址：任何从站都应接收 */

#define MODBUS_FUNC_READ_HOLDING   (0x03u)
#define MODBUS_FUNC_READ_INPUT     (0x04u)
#define MODBUS_FUNC_WRITE_SINGLE   (0x06u)
#define MODBUS_FUNC_WRITE_MULTIPLE (0x10u)
#define MODBUS_EXCEPTION_FLAG      (0x80u)  /* 功能码最高位置位 = 异常响应帧 */

typedef enum {
    MODBUS_OK = 0,
    MODBUS_ERR_NULL,       /* 入参指针为 NULL              */
    MODBUS_ERR_LENGTH,     /* 帧长越界                     */
    MODBUS_ERR_CRC,        /* CRC 校验失败                 */
    MODBUS_ERR_ADDRESS,    /* 不是发给我（也非广播）的帧   */
    MODBUS_ERR_FUNCTION,   /* 不支持的功能码               */
    MODBUS_ERR_DATA        /* 数据区语义不合法             */
} modbus_status_t;

typedef struct {
    uint8_t address;
    uint8_t function;
    uint8_t data[MODBUS_MAX_DATA_LEN];
    size_t  data_len;
} modbus_frame_t;

/*
 * CRC-16/MODBUS：多项式 0xA001（0x8005 的反射），初值 0xFFFF
 * 空输入返回初值 0xFFFF——行为确定、可测试，且与"0 字节数据的 CRC"语义一致
 */
uint16_t modbus_crc16(const uint8_t *data, size_t len);

/*
 * 解析从站请求帧。
 * 校验顺序（安全等级从高到低）：NULL → 长度 → CRC → 地址 → 功能码 → 数据
 * 契约：仅当返回 MODBUS_OK 时 out 才被写入；错误路径保证 out 不被修改。
 * 地址匹配规则：帧地址 == expected_addr 或 == 广播地址 0。
 */
modbus_status_t modbus_parse_frame(const uint8_t *frame,
                                   size_t len,
                                   uint8_t expected_addr,
                                   modbus_frame_t *out);

#endif /* MODBUS_PARSER_H */
