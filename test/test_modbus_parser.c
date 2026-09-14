#include "unity.h"
#include "modbus_parser.h"
#include <string.h>

#define SLAVE_ADDR  (0x01u)

static modbus_frame_t parsed;

void setUp(void)   { memset(&parsed, 0, sizeof(parsed)); }
void tearDown(void) { }

/* 造帧辅助：用被测 CRC 构造函数补 CRC。
 * 注意：这类用例只验证"解析逻辑"自洽性，CRC 正确性由已知向量用例独立保证 */
static size_t build_frame(uint8_t *buf, uint8_t addr, uint8_t func,
                          const uint8_t *data, size_t data_len)
{
    buf[0] = addr;
    buf[1] = func;
    memcpy(&buf[2], data, data_len);
    uint16_t crc = modbus_crc16(buf, 2u + data_len);
    buf[2u + data_len] = (uint8_t)(crc & 0xFFu);   /* 低字节在前 */
    buf[3u + data_len] = (uint8_t)(crc >> 8);
    return data_len + 4u;
}

/* ---------------- CRC16：已知答案测试（3） ---------------- */

void test_crc16_known_vector_123456789(void)
{
    /* 国际标准校验值：CRC-16/MODBUS("123456789") = 0x4B37 */
    const uint8_t s[9] = {'1','2','3','4','5','6','7','8','9'};
    TEST_ASSERT_EQUAL_HEX16(0x4B37u, modbus_crc16(s, 9u));
}

void test_crc16_empty_input_returns_init_value(void)
{
    uint8_t dummy = 0u;
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, modbus_crc16(&dummy, 0u));
}

void test_crc16_null_input_returns_init_value(void)
{
    TEST_ASSERT_EQUAL_HEX16(0xFFFFu, modbus_crc16(NULL, 5u));
}

/* ---------------- 合法帧：硬编码标准样例（1） ---------------- */

void test_parse_known_frame_read_one_register(void)
{
    /* Modbus 标准文档样例帧：从站1 读保持寄存器 起始0 数量1，CRC=0x0A84 低字节在前 */
    const uint8_t frame[8] = {0x01u, 0x03u, 0x00u, 0x00u, 0x00u, 0x01u, 0x84u, 0x0Au};
    TEST_ASSERT_EQUAL_INT(MODBUS_OK, modbus_parse_frame(frame, sizeof(frame), SLAVE_ADDR, &parsed));
    TEST_ASSERT_EQUAL_HEX8(0x01u, parsed.address);
    TEST_ASSERT_EQUAL_HEX8(MODBUS_FUNC_READ_HOLDING, parsed.function);
    TEST_ASSERT_EQUAL_UINT(4u, (unsigned)parsed.data_len);
    const uint8_t expect_data[4] = {0x00u, 0x00u, 0x00u, 0x01u};
    TEST_ASSERT_EQUAL_MEMORY(expect_data, parsed.data, 4u);
}

/* ---------------- NULL 与长度（4） ---------------- */

void test_parse_rejects_null_frame_pointer(void)
{
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_NULL, modbus_parse_frame(NULL, 8u, SLAVE_ADDR, &parsed));
}

void test_parse_rejects_null_output_pointer(void)
{
    const uint8_t frame[8] = {0x01u, 0x03u, 0, 0, 0, 1u, 0x84u, 0x0Au};
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_NULL, modbus_parse_frame(frame, 8u, SLAVE_ADDR, NULL));
}

void test_parse_rejects_frame_too_short(void)
{
    const uint8_t tiny[3] = {0x01u, 0x03u, 0x00u};
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_LENGTH, modbus_parse_frame(tiny, sizeof(tiny), SLAVE_ADDR, &parsed));
}

void test_parse_rejects_frame_too_long(void)
{
    /* 长度检查先于 CRC，所以内容无需合法——这个用例同时证明校验顺序 */
    uint8_t huge[MODBUS_MAX_FRAME_LEN + 1u];
    memset(huge, 0, sizeof(huge));
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_LENGTH, modbus_parse_frame(huge, sizeof(huge), SLAVE_ADDR, &parsed));
}

/* ---------------- CRC 错误与字节序（2） ---------------- */

void test_parse_detects_crc_error_and_leaves_output_untouched(void)
{
    uint8_t frame[8] = {0x01u, 0x03u, 0, 0, 0, 1u, 0x84u, 0x0Au};
    frame[7] ^= 0xFFu;  /* 翻转 CRC 高字节 */
    parsed.address = 0xAAu;  /* 哨兵值：验证错误路径不污染 out（populate-on-success 契约） */
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_CRC, modbus_parse_frame(frame, sizeof(frame), SLAVE_ADDR, &parsed));
    TEST_ASSERT_EQUAL_HEX8(0xAAu, parsed.address);
}

void test_parse_rejects_swapped_crc_byte_order(void)
{
    /* 把 CRC 按高字节在前发送——验证解析器严格按"低字节在前"的小端线路序 */
    const uint8_t frame[8] = {0x01u, 0x03u, 0, 0, 0, 1u, 0x0Au, 0x84u};
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_CRC, modbus_parse_frame(frame, sizeof(frame), SLAVE_ADDR, &parsed));
}

/* ---------------- 地址匹配 / 广播（2） ---------------- */

void test_parse_rejects_frame_for_other_slave(void)
{
    const uint8_t frame[8] = {0x01u, 0x03u, 0, 0, 0, 1u, 0x84u, 0x0Au};
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_ADDRESS, modbus_parse_frame(frame, sizeof(frame), 0x02u, &parsed));
}

void test_parse_accepts_broadcast_address(void)
{
    uint8_t frame[16];
    const uint8_t data[4] = {0x00u, 0x01u, 0x00u, 0xFFu};
    size_t len = build_frame(frame, MODBUS_BROADCAST_ADDR, MODBUS_FUNC_WRITE_SINGLE, data, 4u);
    TEST_ASSERT_EQUAL_INT(MODBUS_OK, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
    TEST_ASSERT_EQUAL_HEX8(0x00u, parsed.address);
}

/* ---------------- 功能码与异常帧（3） ---------------- */

void test_parse_rejects_unsupported_function_code(void)
{
    uint8_t frame[8];
    const uint8_t data[1] = {0x00u};
    size_t len = build_frame(frame, SLAVE_ADDR, 0x2Bu, data, 1u);  /* 0x2B 不在支持集 */
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_FUNCTION, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

void test_parse_accepts_exception_response_frame(void)
{
    /* 功能码 |0x80 = 异常响应，数据区恰好 1 字节异常码 */
    uint8_t frame[8];
    const uint8_t data[1] = {0x02u};  /* 02 = 非法数据地址 */
    size_t len = build_frame(frame, SLAVE_ADDR, 0x83u, data, 1u);
    TEST_ASSERT_EQUAL_INT(MODBUS_OK, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
    TEST_ASSERT_EQUAL_HEX8(0x83u, parsed.function);
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)parsed.data_len);
    TEST_ASSERT_EQUAL_HEX8(0x02u, parsed.data[0]);
}

void test_parse_rejects_exception_frame_with_wrong_data_length(void)
{
    uint8_t frame[8];
    const uint8_t data[2] = {0x02u, 0x03u};
    size_t len = build_frame(frame, SLAVE_ADDR, 0x83u, data, 2u);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_DATA, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

/* ---------------- 数据内容校验：读（3） ---------------- */

void test_parse_rejects_read_request_with_short_data(void)
{
    uint8_t frame[8];
    const uint8_t data[2] = {0x00u, 0x00u};
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_READ_HOLDING, data, 2u);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_DATA, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

void test_parse_rejects_read_quantity_zero(void)
{
    uint8_t frame[12];
    const uint8_t data[4] = {0x00u, 0x00u, 0x00u, 0x00u};  /* 数量=0，协议下限是 1 */
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_READ_HOLDING, data, 4u);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_DATA, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

void test_parse_rejects_read_quantity_above_125(void)
{
    uint8_t frame[12];
    const uint8_t data[4] = {0x00u, 0x00u, 0x00u, 0x7Eu};  /* 126 > 125，协议上限 */
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_READ_HOLDING, data, 4u);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_DATA, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

/* ---------------- 写单寄存器（1，合法/非法边界对） ---------------- */

void test_parse_write_single_register_valid_and_bad_length_pair(void)
{
    /* 合法/非法边界对放同一用例：验证的是同一条 "data_len == 4" 判断的两侧 */
    uint8_t frame[16];
    const uint8_t good[4] = {0x00u, 0x01u, 0x00u, 0xFFu};
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_WRITE_SINGLE, good, 4u);
    TEST_ASSERT_EQUAL_INT(MODBUS_OK, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
    TEST_ASSERT_EQUAL_MEMORY(good, parsed.data, 4u);

    const uint8_t bad[2] = {0x00u, 0x01u};
    len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_WRITE_SINGLE, bad, 2u);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_DATA, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

/* ---------------- 写多寄存器：四重一致性（4） ---------------- */

void test_parse_write_multiple_valid_frame(void)
{
    /* 起始0，数量2，字节计数4，值 0x000A / 0x0102 */
    uint8_t frame[20];
    const uint8_t data[9] = {0x00u, 0x00u, 0x00u, 0x02u, 0x04u, 0x00u, 0x0Au, 0x01u, 0x02u};
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_WRITE_MULTIPLE, data, 9u);
    TEST_ASSERT_EQUAL_INT(MODBUS_OK, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
    TEST_ASSERT_EQUAL_UINT(9u, (unsigned)parsed.data_len);
    TEST_ASSERT_EQUAL_MEMORY(data, parsed.data, 9u);
}

void test_parse_rejects_write_multiple_byte_count_vs_quantity(void)
{
    /* 数量=2 要求字节计数=4，帧里写 3 → 第一层一致性拦截 */
    uint8_t frame[20];
    const uint8_t data[9] = {0x00u, 0x00u, 0x00u, 0x02u, 0x03u, 0x00u, 0x0Au, 0x01u, 0x02u};
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_WRITE_MULTIPLE, data, 9u);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_DATA, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

void test_parse_rejects_write_multiple_byte_count_vs_actual_length(void)
{
    /* 字节计数=4 与数量一致，但实际只跟了 3 个值字节（截断帧）→ 第二层一致性拦截 */
    uint8_t frame[20];
    const uint8_t data[8] = {0x00u, 0x00u, 0x00u, 0x02u, 0x04u, 0x00u, 0x0Au, 0x01u};
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_WRITE_MULTIPLE, data, 8u);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_DATA, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

void test_parse_rejects_write_multiple_quantity_above_123(void)
{
    uint8_t frame[20];
    const uint8_t data[5] = {0x00u, 0x00u, 0x00u, 0x7Cu, 0x00u};  /* 数量 124 > 123 */
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_WRITE_MULTIPLE, data, 5u);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_DATA, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

void test_parse_rejects_write_multiple_with_data_len_below_5(void)
{
    /* 0x10 数据区最少 5 字节（地址2+数量2+字节计数1），只给 4 个 */
    uint8_t frame[16];
    const uint8_t data[4] = {0x00u, 0x00u, 0x00u, 0x02u};
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_WRITE_MULTIPLE, data, 4u);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_DATA, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

void test_parse_rejects_write_multiple_quantity_zero(void)
{
    uint8_t frame[16];
    const uint8_t data[5] = {0x00u, 0x00u, 0x00u, 0x00u, 0x00u};  /* 数量=0，协议下限 1 */
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_WRITE_MULTIPLE, data, 5u);
    TEST_ASSERT_EQUAL_INT(MODBUS_ERR_DATA, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
}

void test_parse_accepts_read_input_registers_function_0x04(void)
{
    /* 0x04 与 0x03 共用校验逻辑，但 case 标签行需要自己的帧来覆盖 */
    uint8_t frame[12];
    const uint8_t data[4] = {0x00u, 0x00u, 0x00u, 0x01u};
    size_t len = build_frame(frame, SLAVE_ADDR, MODBUS_FUNC_READ_INPUT, data, 4u);
    TEST_ASSERT_EQUAL_INT(MODBUS_OK, modbus_parse_frame(frame, len, SLAVE_ADDR, &parsed));
    TEST_ASSERT_EQUAL_HEX8(MODBUS_FUNC_READ_INPUT, parsed.function);
}






