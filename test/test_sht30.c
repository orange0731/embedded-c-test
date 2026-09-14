#include "unity.h"
#include "sht30.h"
#include "mock_hal_i2c.h"   /* CMock 依据 src/hal_i2c.h 自动生成——Mock 硬件的入口 */
#include <string.h>

void setUp(void)   { }
void tearDown(void) { }

/* 25.0°C / 40.0%RH 的标准帧：raw=0x6666 → 26214/65535 = 0.4，
 * T = -45+175*0.4 = 25.0，RH = 100*0.4 = 40.0，CRC(0x66 0x66)=0x93（手工推导验证）。
 * 注意：传给 ReturnArrayThruPtr 的数组必须是非 const（CMock 签名要求） */
static uint8_t FRAME_25C_40RH[6] = {0x66u, 0x66u, 0x93u, 0x66u, 0x66u, 0x93u};

/* ---------------- CRC8 已知答案测试（3） ---------------- */

void test_crc8_datasheet_known_vector_beef(void)
{
    /* 手册官方样例：CRC(0xBE 0xEF) = 0x92——驱动可信度的锚点 */
    const uint8_t data[2] = {0xBEu, 0xEFu};
    TEST_ASSERT_EQUAL_HEX8(0x92u, sht30_crc8(data, 2u));
}

void test_crc8_zero_bytes_vector(void)
{
    /* 手工推导向量：CRC(0x00 0x00) = 0x81，同时是"最小值帧"的组成部分 */
    const uint8_t data[2] = {0x00u, 0x00u};
    TEST_ASSERT_EQUAL_HEX8(0x81u, sht30_crc8(data, 2u));
}

void test_crc8_empty_input_returns_init_value(void)
{
    TEST_ASSERT_EQUAL_HEX8(0xFFu, sht30_crc8(NULL, 0u));
}

/* ---------------- 触发命令：字节序 + 错误映射（5） ---------------- */

void test_trigger_sends_command_msb_first(void)
{
    /* ExpectWithArray：按内容比较指针指向的 2 个字节。
       若驱动把 0x24/0x00 写反成 0x00/0x24，CMock 在此报错——字节序守门员 */
    uint8_t expected_cmd[2] = {0x24u, 0x00u};
    hal_i2c_write_ExpectWithArrayAndReturn(SHT30_I2C_ADDR, expected_cmd, 2, 2u, HAL_I2C_OK);
    TEST_ASSERT_EQUAL_INT(SHT30_OK, sht30_trigger_measurement(SHT30_I2C_ADDR));
}

void test_trigger_maps_nack_to_driver_error(void)
{
    /* ExpectAnyArgs：错误注入场景只关心返回值，不关心参数 */
    hal_i2c_write_ExpectAnyArgsAndReturn(HAL_I2C_ERR_NACK);
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_NACK, sht30_trigger_measurement(SHT30_I2C_ADDR));
}

void test_trigger_maps_timeout_to_driver_error(void)
{
    hal_i2c_write_ExpectAnyArgsAndReturn(HAL_I2C_ERR_TIMEOUT);
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_TIMEOUT, sht30_trigger_measurement(SHT30_I2C_ADDR));
}

void test_trigger_maps_hal_param_to_driver_param(void)
{
    /* 显式 case 分支：HAL 返回 PARAM 时驱动必须正确映射 */
    hal_i2c_write_ExpectAnyArgsAndReturn(HAL_I2C_ERR_PARAM);
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_PARAM, sht30_trigger_measurement(SHT30_I2C_ADDR));
}

void test_trigger_maps_out_of_range_hal_status_to_param_error(void)
{
    /* default 兜底分支：注入"越界枚举"（模拟 HAL 未来新增状态而驱动未跟进）。
       这条用例是变异测试 M6 的补强成果——没有它，default 分支的变异体可以存活 */
    hal_i2c_write_ExpectAnyArgsAndReturn((hal_i2c_status_t)99);
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_PARAM, sht30_trigger_measurement(SHT30_I2C_ADDR));
}

/* ---------------- 读数换算：已知答案（3） ---------------- */

void test_read_converts_known_frame_25c_40rh(void)
{
    /* 经典三连：先声明期望（出参用 NULL 占位）→ 忽略出参 → 回填数组。
       顺序不能反：IgnoreArg/ReturnThruPtr 修饰的是"最近一次 Expect" */
    hal_i2c_read_ExpectAndReturn(SHT30_I2C_ADDR, NULL, 6u, HAL_I2C_OK);
    hal_i2c_read_IgnoreArg_data();
    hal_i2c_read_ReturnArrayThruPtr_data(FRAME_25C_40RH, 6);

    float t = 0.0f, h = 0.0f;
    TEST_ASSERT_EQUAL_INT(SHT30_OK, sht30_read_measurement(SHT30_I2C_ADDR, &t, &h));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, t);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, h);
}

void test_read_converts_min_raw_values(void)
{
    /* raw=0x0000：T=-45.0°C，RH=0.0%，CRC(00 00)=0x81 */
    uint8_t frame[6] = {0x00u, 0x00u, 0x81u, 0x00u, 0x00u, 0x81u};
    hal_i2c_read_ExpectAndReturn(SHT30_I2C_ADDR, NULL, 6u, HAL_I2C_OK);
    hal_i2c_read_IgnoreArg_data();
    hal_i2c_read_ReturnArrayThruPtr_data(frame, 6);

    float t = 99.0f, h = 99.0f;
    TEST_ASSERT_EQUAL_INT(SHT30_OK, sht30_read_measurement(SHT30_I2C_ADDR, &t, &h));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, -45.0f, t);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 0.0f, h);
}

void test_read_converts_max_raw_values(void)
{
    /* raw=0xFFFF：T=130.0°C，RH=100.0%，CRC(FF FF)=0xAC */
    uint8_t frame[6] = {0xFFu, 0xFFu, 0xACu, 0xFFu, 0xFFu, 0xACu};
    hal_i2c_read_ExpectAndReturn(SHT30_I2C_ADDR, NULL, 6u, HAL_I2C_OK);
    hal_i2c_read_IgnoreArg_data();
    hal_i2c_read_ReturnArrayThruPtr_data(frame, 6);

    float t = 0.0f, h = 0.0f;
    TEST_ASSERT_EQUAL_INT(SHT30_OK, sht30_read_measurement(SHT30_I2C_ADDR, &t, &h));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 130.0f, t);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 100.0f, h);
}

/* ---------------- 读回 CRC 校验（2） ---------------- */

void test_read_rejects_corrupted_temperature_crc(void)
{
    uint8_t frame[6] = {0x66u, 0x66u, 0x00u, 0x66u, 0x66u, 0x93u};  /* 温度 CRC 坏 */
    hal_i2c_read_ExpectAndReturn(SHT30_I2C_ADDR, NULL, 6u, HAL_I2C_OK);
    hal_i2c_read_IgnoreArg_data();
    hal_i2c_read_ReturnArrayThruPtr_data(frame, 6);

    float t, h;
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_CRC, sht30_read_measurement(SHT30_I2C_ADDR, &t, &h));
}

void test_read_rejects_corrupted_humidity_crc(void)
{
    /* 温度组合法、湿度组 CRC 坏——证明两组校验相互独立 */
    uint8_t frame[6] = {0x66u, 0x66u, 0x93u, 0x66u, 0x66u, 0x00u};
    hal_i2c_read_ExpectAndReturn(SHT30_I2C_ADDR, NULL, 6u, HAL_I2C_OK);
    hal_i2c_read_IgnoreArg_data();
    hal_i2c_read_ReturnArrayThruPtr_data(frame, 6);

    float t, h;
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_CRC, sht30_read_measurement(SHT30_I2C_ADDR, &t, &h));
}

/* ---------------- HAL 错误传播（3） ---------------- */

void test_read_propagates_nack(void)
{
    /* 传感器不应答（测量未完成/掉线）→ 错误必须原样映射上传，不得吞掉 */
    hal_i2c_read_ExpectAnyArgsAndReturn(HAL_I2C_ERR_NACK);
    float t, h;
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_NACK, sht30_read_measurement(SHT30_I2C_ADDR, &t, &h));
}

void test_read_propagates_timeout(void)
{
    hal_i2c_read_ExpectAnyArgsAndReturn(HAL_I2C_ERR_TIMEOUT);
    float t, h;
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_TIMEOUT, sht30_read_measurement(SHT30_I2C_ADDR, &t, &h));
}

void test_read_propagates_bus_error(void)
{
    hal_i2c_read_ExpectAnyArgsAndReturn(HAL_I2C_ERR_BUS);
    float t, h;
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_BUS, sht30_read_measurement(SHT30_I2C_ADDR, &t, &h));
}

/* ---------------- 参数防御：关键是"零总线交互"（2） ---------------- */

void test_read_null_temperature_never_touches_i2c(void)
{
    /* 故意不设置任何 Expect：若驱动先碰 I2C 再检查参数，
       CMock 会立刻报"调用了未期望的函数"——
       本用例真正断言的是"非法参数时总线上一个字节都不许发" */
    float h = 0.0f;
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_PARAM, sht30_read_measurement(SHT30_I2C_ADDR, NULL, &h));
}

void test_read_null_humidity_never_touches_i2c(void)
{
    float t = 0.0f;
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_PARAM, sht30_read_measurement(SHT30_I2C_ADDR, &t, NULL));
}

/* ---------------- Callback：用真函数假扮传感器（1） ---------------- */

static uint8_t g_fake_response[6];

/* 回调内部可以直接写 Unity 断言——失败同样让用例变红。
   这是 StubWithCallback 相对 Expect 的核心优势：行为逻辑可以任意复杂 */
static hal_i2c_status_t fake_sht30_on_bus(uint8_t dev_addr, uint8_t *data, uint16_t len, int cmock_num_calls)
{
    (void)cmock_num_calls;
    TEST_ASSERT_EQUAL_UINT8(SHT30_I2C_ADDR, dev_addr);   /* 驱动有没有找错地址？ */
    TEST_ASSERT_EQUAL_UINT16(6u, len);                   /* 驱动有没有读错长度？ */
    memcpy(data, g_fake_response, 6u);
    return HAL_I2C_OK;
}

void test_read_with_stub_callback_fake_sensor(void)
{
    memcpy(g_fake_response, FRAME_25C_40RH, 6u);
    hal_i2c_read_StubWithCallback(fake_sht30_on_bus);    /* 挂上假传感器 */

    float t = 0.0f, h = 0.0f;
    TEST_ASSERT_EQUAL_INT(SHT30_OK, sht30_read_measurement(SHT30_I2C_ADDR, &t, &h));
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 25.0f, t);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 40.0f, h);
}

/* ---------------- 综合场景："半路 NACK"（1） ---------------- */

void test_trigger_ok_but_read_nacks_midway_sequence(void)
{
    /* 现场高频故障：命令被应答了，但紧接着读数据时传感器 NACK
       （测量未完成/总线被干扰/传感器中途掉电）。
       因 project.yml 里 enforce_strict_ordering=TRUE，
       CMock 还会强制验证"必须先写命令、后读数据"的调用时序 */
    uint8_t expected_cmd[2] = {0x24u, 0x00u};
    hal_i2c_write_ExpectWithArrayAndReturn(SHT30_I2C_ADDR, expected_cmd, 2, 2u, HAL_I2C_OK);
    TEST_ASSERT_EQUAL_INT(SHT30_OK, sht30_trigger_measurement(SHT30_I2C_ADDR));

    hal_i2c_read_ExpectAnyArgsAndReturn(HAL_I2C_ERR_NACK);
    float t = 0.0f, h = 0.0f;
    TEST_ASSERT_EQUAL_INT(SHT30_ERR_NACK, sht30_read_measurement(SHT30_I2C_ADDR, &t, &h));
}
