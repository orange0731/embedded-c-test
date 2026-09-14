#include "unity.h"
#include "pid.h"
#include <math.h>  /* NAN 宏 */

static pid_t pid;

void setUp(void)
{
    /* 默认增益全 1、限幅 ±100；各用例按需重新 init，互不污染 */
    (void)pid_init(&pid, 1.0f, 1.0f, 1.0f, -100.0f, 100.0f);
}

void tearDown(void) { }

/* ---------------- 初始化（3） ---------------- */

void test_init_with_valid_params_succeeds(void)
{
    pid_t fresh;
    TEST_ASSERT_TRUE(pid_init(&fresh, 2.0f, 0.5f, 0.1f, -10.0f, 10.0f));
}

void test_init_rejects_null_handle(void)
{
    TEST_ASSERT_FALSE(pid_init(NULL, 1.0f, 1.0f, 1.0f, -10.0f, 10.0f));
}

void test_init_rejects_inverted_limits(void)
{
    pid_t fresh;
    TEST_ASSERT_FALSE(pid_init(&fresh, 1.0f, 1.0f, 1.0f, 10.0f, -10.0f));
    /* 等号也是非法：min == max 时输出被钳成常数，一定是配置错误 */
    TEST_ASSERT_FALSE(pid_init(&fresh, 1.0f, 1.0f, 1.0f, 5.0f, 5.0f));
}

/* ---------------- P / I / D 分量独立验证（4） ---------------- */

void test_proportional_term_only(void)
{
    /* ki=kd=0：out = Kp*e，选整数值做精确断言 */
    (void)pid_init(&pid, 2.0f, 0.0f, 0.0f, -100.0f, 100.0f);
    float out = 0.0f;
    TEST_ASSERT_TRUE(pid_compute(&pid, 3.0f, 0.0f, 0.1f, &out));
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 6.0f, out);   /* 2 * 3 */
}

void test_integral_term_accumulates_over_cycles(void)
{
    /* kp=kd=0，ki=1，e=2，dt=0.1：输出逐周期 0.2 / 0.4 / 0.6 */
    (void)pid_init(&pid, 0.0f, 1.0f, 0.0f, -100.0f, 100.0f);
    float out = 0.0f;
    (void)pid_compute(&pid, 2.0f, 0.0f, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.2f, out);
    (void)pid_compute(&pid, 2.0f, 0.0f, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.4f, out);
    (void)pid_compute(&pid, 2.0f, 0.0f, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN(0.001f, 0.6f, out);
}

void test_derivative_term_only_first_cycle(void)
{
    /* kp=ki=0，kd=1，e=1，dt=0.1：首周期 de/dt = (1-0)/0.1 = 10 */
    (void)pid_init(&pid, 0.0f, 0.0f, 1.0f, -100.0f, 100.0f);
    float out = 0.0f;
    (void)pid_compute(&pid, 1.0f, 0.0f, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN(0.01f, 10.0f, out);
}

void test_derivative_is_zero_when_error_constant(void)
{
    /* 误差不变 → 微分为 0：第二周期输出必须归零 */
    (void)pid_init(&pid, 0.0f, 0.0f, 1.0f, -100.0f, 100.0f);
    float out = 0.0f;
    (void)pid_compute(&pid, 1.0f, 0.0f, 0.1f, &out);
    (void)pid_compute(&pid, 1.0f, 0.0f, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, out);
}

/* ---------------- 输出限幅（2） ---------------- */

void test_output_saturates_at_max(void)
{
    (void)pid_init(&pid, 10.0f, 0.0f, 0.0f, -50.0f, 50.0f);
    float out = 0.0f;
    (void)pid_compute(&pid, 10.0f, 0.0f, 0.1f, &out);  /* 裸值 100 > 50 */
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 50.0f, out);
}

void test_output_saturates_at_min(void)
{
    (void)pid_init(&pid, 10.0f, 0.0f, 0.0f, -50.0f, 50.0f);
    float out = 0.0f;
    (void)pid_compute(&pid, -10.0f, 0.0f, 0.1f, &out); /* 裸值 -100 < -50 */
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -50.0f, out);
}

/* ---------------- 抗积分饱和（2，本模块灵魂用例） ---------------- */

void test_integral_anti_windup_clamps_at_max(void)
{
    /* ki=1，e=1，dt=1：积分 10 周期后到达限幅 10。
       再跑 40 周期输出必须钉死在 10（不发散），
       然后误差反号为 -1：积分立刻从 10 降到 9，输出 9.0——
       若没有限幅，积分已冲到 50，反向恢复需要 41 个周期而不是 1 个 */
    (void)pid_init(&pid, 0.0f, 1.0f, 0.0f, -10.0f, 10.0f);
    float out = 0.0f;
    for (unsigned i = 0u; i < 50u; i++) {
        (void)pid_compute(&pid, 1.0f, 0.0f, 1.0f, &out);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 10.0f, out);
    (void)pid_compute(&pid, -1.0f, 0.0f, 1.0f, &out);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 9.0f, out);
}

void test_integral_anti_windup_clamps_at_min(void)
{
    /* 与上一用例镜像：验证负方向限幅，防止"只测了一半"的覆盖率假象 */
    (void)pid_init(&pid, 0.0f, 1.0f, 0.0f, -10.0f, 10.0f);
    float out = 0.0f;
    for (unsigned i = 0u; i < 50u; i++) {
        (void)pid_compute(&pid, -1.0f, 0.0f, 1.0f, &out);
    }
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -10.0f, out);
    (void)pid_compute(&pid, 1.0f, 0.0f, 1.0f, &out);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, -9.0f, out);
}

/* ---------------- 复位（1） ---------------- */

void test_reset_clears_integral_and_previous_error(void)
{
    /* 先制造内部状态：integral=2、prev_error=2 */
    float out = 0.0f;
    (void)pid_compute(&pid, 2.0f, 0.0f, 1.0f, &out);   /* out = 2+2+2 = 6 */
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 6.0f, out);

    pid_reset(&pid);

    /* 若复位失败：积分会是 3、微分会是 (1-2)/1=-1，输出不会是 3.0 */
    (void)pid_compute(&pid, 1.0f, 0.0f, 1.0f, &out);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.0f, out);      /* 1 + 1 + 1 */
}

/* ---------------- 非法输入（4） ---------------- */

void test_dt_zero_returns_false_and_output_untouched(void)
{
    float out = -123.0f;   /* 哨兵值 */
    TEST_ASSERT_FALSE(pid_compute(&pid, 1.0f, 0.0f, 0.0f, &out));
    TEST_ASSERT_EQUAL_FLOAT(-123.0f, out);   /* 精确相等：保证一个字节都没被碰 */
}

void test_dt_negative_returns_false(void)
{
    float out = -123.0f;
    TEST_ASSERT_FALSE(pid_compute(&pid, 1.0f, 0.0f, -0.1f, &out));
    TEST_ASSERT_EQUAL_FLOAT(-123.0f, out);
}

void test_compute_rejects_null_pid(void)
{
    float out;
    TEST_ASSERT_FALSE(pid_compute(NULL, 1.0f, 0.0f, 1.0f, &out));
}

void test_compute_rejects_null_output(void)
{
    TEST_ASSERT_FALSE(pid_compute(&pid, 1.0f, 0.0f, 1.0f, NULL));
}

/* ---------------- 稳态与多周期行为（2） ---------------- */

void test_zero_error_produces_zero_output_on_fresh_controller(void)
{
    /* 注意前提"fresh"：积分有记忆，非首周期 e=0 输出未必为 0——
       这个注释本身就是面试加分点 */
    (void)pid_init(&pid, 1.0f, 1.0f, 1.0f, -100.0f, 100.0f);
    float out = 1.0f;
    (void)pid_compute(&pid, 5.0f, 5.0f, 0.1f, &out);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 0.0f, out);
}

void test_multi_cycle_convergence_with_plant_model(void)
{
    /* 闭环仿真：被控对象 meas += out*0.5（一阶积分环节），纯 P 控制 kp=0.5。
       误差理论上每周期衰减为 0.75 倍，15 周期后 |e| ≈ 10*0.75^15 ≈ 0.13 */
    (void)pid_init(&pid, 0.5f, 0.0f, 0.0f, -100.0f, 100.0f);
    float measurement = 0.0f;
    float out = 0.0f;
    const float setpoint = 10.0f;
    for (unsigned i = 0u; i < 15u; i++) {
        TEST_ASSERT_TRUE(pid_compute(&pid, setpoint, measurement, 0.1f, &out));
        measurement += out * 0.5f;
    }
    TEST_ASSERT_FLOAT_WITHIN(0.5f, setpoint, measurement);
}

void test_reset_null_handle_is_harmless(void)
{
    pid_reset(NULL);   /* void 接口的 NULL 防御：调用后不崩即通过 */
    float out = 0.0f;
    TEST_ASSERT_TRUE(pid_compute(&pid, 1.0f, 0.0f, 1.0f, &out));  /* 既有状态未被影响 */
}



void test_dt_nan_returns_false_and_output_untouched(void)
{
    /* NaN 参与比较恒为 false，"dt <= 0" 拦不住它——必须 isnan 显式拦截 */
    float out = -123.0f;
    TEST_ASSERT_FALSE(pid_compute(&pid, 1.0f, 0.0f, NAN, &out));
    TEST_ASSERT_EQUAL_FLOAT(-123.0f, out);
}

void test_nan_measurement_returns_false_and_integral_stays_clean(void)
{
    /* 测量值 NaN → error 为 NaN：拦截且积分器零污染（NaN 有传染性）。
       验证：随后一个合法周期输出精确等于"从未发生 NaN"时的 3.0 */
    float out = -123.0f;
    TEST_ASSERT_FALSE(pid_compute(&pid, 1.0f, NAN, 1.0f, &out));
    TEST_ASSERT_EQUAL_FLOAT(-123.0f, out);
    (void)pid_compute(&pid, 1.0f, 0.0f, 1.0f, &out);
    TEST_ASSERT_FLOAT_WITHIN(0.0001f, 3.0f, out);   /* 1 + 1 + 1 */
}




