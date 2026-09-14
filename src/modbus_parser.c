#include "modbus_parser.h"
#include <string.h>

uint16_t modbus_crc16(const uint8_t *data, size_t len)
{
    /* 位运算实现：牺牲 ROM 换可读性。已知向量测试保证未来换查表法的重构安全 */
    if ((data == NULL) || (len == 0u)) {
        return 0xFFFFu;
    }
    uint16_t crc = 0xFFFFu;
    for (size_t i = 0u; i < len; i++) {
        crc ^= data[i];
        for (unsigned bit = 0u; bit < 8u; bit++) {
            if ((crc & 0x0001u) != 0u) {
                crc = (uint16_t)((crc >> 1) ^ 0xA001u);
            } else {
                crc = (uint16_t)(crc >> 1);
            }
        }
    }
    return crc;
}

/* 各功能码的数据区语义校验，独立成静态函数让 parse 主流程只表达"顺序" */
static modbus_status_t validate_read_quantity(const uint8_t *data, size_t data_len)
{
    if (data_len != 4u) {
        return MODBUS_ERR_DATA;
    }
    /* 协议规定读寄存器数量合法区间 1..125 */
    uint16_t quantity = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
    if ((quantity < 1u) || (quantity > 125u)) {
        return MODBUS_ERR_DATA;
    }
    return MODBUS_OK;
}

static modbus_status_t validate_write_multiple(const uint8_t *data, size_t data_len)
{
    /* 请求格式：起始地址(2) + 数量(2) + 字节计数(1) + N 字节值 */
    if (data_len < 5u) {
        return MODBUS_ERR_DATA;
    }
    uint16_t quantity   = (uint16_t)(((uint16_t)data[2] << 8) | data[3]);
    uint8_t  byte_count = data[4];
    if ((quantity < 1u) || (quantity > 123u)) {
        return MODBUS_ERR_DATA;
    }
    /* 双重一致性：字节计数既要匹配数量，也要匹配实际帧长——防截断帧 */
    if (byte_count != (uint8_t)(quantity * 2u)) {
        return MODBUS_ERR_DATA;
    }
    if (byte_count != (uint8_t)(data_len - 5u)) {
        return MODBUS_ERR_DATA;
    }
    return MODBUS_OK;
}

modbus_status_t modbus_parse_frame(const uint8_t *frame,
                                   size_t len,
                                   uint8_t expected_addr,
                                   modbus_frame_t *out)
{
    if ((frame == NULL) || (out == NULL)) {
        return MODBUS_ERR_NULL;
    }
    /* 长度检查必须在一切解引用之前，否则短帧会越界读 */
    if ((len < MODBUS_MIN_FRAME_LEN) || (len > MODBUS_MAX_FRAME_LEN)) {
        return MODBUS_ERR_LENGTH;
    }

    /* CRC 在帧尾，低字节在前（小端线路序） */
    uint16_t crc_calc = modbus_crc16(frame, len - 2u);
    uint16_t crc_recv = (uint16_t)((uint16_t)frame[len - 2u] |
                                  ((uint16_t)frame[len - 1u] << 8));
    if (crc_calc != crc_recv) {
        return MODBUS_ERR_CRC;
    }

    uint8_t address  = frame[0];
    uint8_t function = frame[1];
    const uint8_t *data = &frame[2];
    size_t data_len = len - 4u;

    if ((address != expected_addr) && (address != MODBUS_BROADCAST_ADDR)) {
        return MODBUS_ERR_ADDRESS;
    }

    modbus_status_t status;
    if ((function & MODBUS_EXCEPTION_FLAG) != 0u) {
        /* 异常响应帧数据区必须恰好 1 个字节（异常码） */
        status = (data_len == 1u) ? MODBUS_OK : MODBUS_ERR_DATA;
    } else {
        switch (function) {
        case MODBUS_FUNC_READ_HOLDING:
        case MODBUS_FUNC_READ_INPUT:
            status = validate_read_quantity(data, data_len);
            break;
        case MODBUS_FUNC_WRITE_SINGLE:
            status = (data_len == 4u) ? MODBUS_OK : MODBUS_ERR_DATA;
            break;
        case MODBUS_FUNC_WRITE_MULTIPLE:
            status = validate_write_multiple(data, data_len);
            break;
        default:
            status = MODBUS_ERR_FUNCTION;
            break;
        }
    }

    /* populate-on-success：只有完全合法的帧才允许写调用者的结构体 */
    if (status == MODBUS_OK) {
        out->address  = address;
        out->function = function;
        out->data_len = data_len;
        if (data_len > 0u) {
            memcpy(out->data, data, data_len);
        }
    }
    return status;
}
