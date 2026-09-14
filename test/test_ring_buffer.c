#include "unity.h"
#include "ring_buffer.h"
#include <string.h>

/* 每个测试都从"已初始化的 8 字节空缓冲区"出发——setUp 保证用例间完全隔离 */
static ring_buffer_t rb;
static uint8_t       storage[8];

void setUp(void)
{
    memset(storage, 0, sizeof(storage));
    (void)ring_buffer_init(&rb, storage, sizeof(storage));
}

void tearDown(void) { }

/* ---------------- 初始化与防御（5） ---------------- */

void test_init_with_valid_storage_succeeds(void)
{
    ring_buffer_t local;
    uint8_t buf[4];
    TEST_ASSERT_TRUE(ring_buffer_init(&local, buf, sizeof(buf)));
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)ring_buffer_count(&local));
}

void test_init_rejects_null_handle(void)
{
    uint8_t buf[4];
    TEST_ASSERT_FALSE(ring_buffer_init(NULL, buf, sizeof(buf)));
}

void test_init_rejects_null_storage(void)
{
    ring_buffer_t local;
    TEST_ASSERT_FALSE(ring_buffer_init(&local, NULL, 4u));
}

void test_init_rejects_zero_capacity(void)
{
    /* 容量 0 是典型配置错误，必须在 init 阶段就拦下而不是等到 push 时除零 */
    ring_buffer_t local;
    uint8_t buf[4];
    TEST_ASSERT_FALSE(ring_buffer_init(&local, buf, 0u));
}

void test_new_buffer_is_empty_not_full_with_free_equals_capacity(void)
{
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
    TEST_ASSERT_FALSE(ring_buffer_is_full(&rb));
    TEST_ASSERT_EQUAL_UINT(8u, (unsigned)ring_buffer_free(&rb));
}

/* ---------------- 基本读写与 FIFO（4） ---------------- */

void test_push_increments_count_and_decreases_free(void)
{
    TEST_ASSERT_TRUE(ring_buffer_push(&rb, 0x11u));
    TEST_ASSERT_EQUAL_UINT(1u, (unsigned)ring_buffer_count(&rb));
    TEST_ASSERT_EQUAL_UINT(7u, (unsigned)ring_buffer_free(&rb));
}

void test_pop_returns_pushed_byte(void)
{
    uint8_t out = 0u;
    (void)ring_buffer_push(&rb, 0xABu);
    TEST_ASSERT_TRUE(ring_buffer_pop(&rb, &out));
    TEST_ASSERT_EQUAL_HEX8(0xABu, out);
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
}

void test_push_pop_maintains_fifo_order(void)
{
    /* 用满容量做 FIFO 验证，顺便把 push 路径的取模回绕覆盖到 */
    const uint8_t seq[8] = {1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u};
    for (unsigned i = 0u; i < 8u; i++) {
        TEST_ASSERT_TRUE(ring_buffer_push(&rb, seq[i]));
    }
    for (unsigned i = 0u; i < 8u; i++) {
        uint8_t out = 0u;
        TEST_ASSERT_TRUE(ring_buffer_pop(&rb, &out));
        TEST_ASSERT_EQUAL_UINT8(seq[i], out);
    }
}

void test_peek_returns_oldest_without_removing(void)
{
    uint8_t out = 0u;
    (void)ring_buffer_push(&rb, 0x55u);
    (void)ring_buffer_push(&rb, 0x66u);
    /* 连续两次 peek 必须拿到同一个值，且 count 不变——证明"只看不取" */
    TEST_ASSERT_TRUE(ring_buffer_peek(&rb, &out));
    TEST_ASSERT_EQUAL_HEX8(0x55u, out);
    TEST_ASSERT_TRUE(ring_buffer_peek(&rb, &out));
    TEST_ASSERT_EQUAL_HEX8(0x55u, out);
    TEST_ASSERT_EQUAL_UINT(2u, (unsigned)ring_buffer_count(&rb));
}

/* ---------------- 满/空边界（3） ---------------- */

void test_push_fails_when_full(void)
{
    for (unsigned i = 0u; i < 8u; i++) {
        (void)ring_buffer_push(&rb, (uint8_t)i);
    }
    TEST_ASSERT_TRUE(ring_buffer_is_full(&rb));
    /* 第 9 次 push 必须失败且 count 不变——这是差一错误变异体的死刑测试 */
    TEST_ASSERT_FALSE(ring_buffer_push(&rb, 0xFFu));
    TEST_ASSERT_EQUAL_UINT(8u, (unsigned)ring_buffer_count(&rb));
}

void test_pop_fails_when_empty(void)
{
    uint8_t out = 0xEEu;
    TEST_ASSERT_FALSE(ring_buffer_pop(&rb, &out));
    TEST_ASSERT_EQUAL_HEX8(0xEEu, out);  /* 失败时不得污染出参 */
}

void test_peek_fails_when_empty(void)
{
    uint8_t out = 0xEEu;
    TEST_ASSERT_FALSE(ring_buffer_peek(&rb, &out));
}

/* ---------------- NULL 与未初始化句柄防御（5） ---------------- */

void test_push_rejects_null_handle(void)
{
    TEST_ASSERT_FALSE(ring_buffer_push(NULL, 0x01u));
}

void test_pop_rejects_null_handle(void)
{
    uint8_t out;
    TEST_ASSERT_FALSE(ring_buffer_pop(NULL, &out));
}

void test_pop_rejects_null_output(void)
{
    (void)ring_buffer_push(&rb, 0x01u);
    TEST_ASSERT_FALSE(ring_buffer_pop(&rb, NULL));
}

void test_peek_rejects_null_output(void)
{
    (void)ring_buffer_push(&rb, 0x01u);
    TEST_ASSERT_FALSE(ring_buffer_peek(&rb, NULL));
}

void test_uninitialized_handle_is_rejected_by_all_ops(void)
{
    /* 现场真实故障：句柄是 BSS 段零初始化变量，init 没被调用。
       全零句柄 buffer==NULL，所有写操作必须安全失败而不是写飞指针 */
    ring_buffer_t zombie = {0};
    uint8_t out;
    TEST_ASSERT_FALSE(ring_buffer_push(&zombie, 1u));
    TEST_ASSERT_FALSE(ring_buffer_pop(&zombie, &out));
    TEST_ASSERT_FALSE(ring_buffer_peek(&zombie, &out));
    /* clear(NULL) 必须是无害的——void 接口用"调用后不崩"作为断言 */
    ring_buffer_clear(NULL);
    ring_buffer_clear(&zombie);
}

/* ---------------- 环绕（wrap-around）——本模块最难的边界（2） ---------------- */

void test_wrap_around_preserves_fifo_order(void)
{
    /* 容量 4：写满→读出 2 个→再写 2 个，此时 head 已回绕到存储区头部，
       数据在物理上不连续但逻辑上必须 FIFO：3,4,5,6 */
    ring_buffer_t w;
    uint8_t buf[4];
    (void)ring_buffer_init(&w, buf, sizeof(buf));

    (void)ring_buffer_push(&w, 1u);
    (void)ring_buffer_push(&w, 2u);
    (void)ring_buffer_push(&w, 3u);
    (void)ring_buffer_push(&w, 4u);      /* 满，head 回绕到 0 */

    uint8_t out = 0u;
    (void)ring_buffer_pop(&w, &out);  TEST_ASSERT_EQUAL_UINT8(1u, out);
    (void)ring_buffer_pop(&w, &out);  TEST_ASSERT_EQUAL_UINT8(2u, out);

    (void)ring_buffer_push(&w, 5u);      /* 写入物理位置 buf[0] */
    (void)ring_buffer_push(&w, 6u);      /* 写入物理位置 buf[1]，再次写满 */

    const uint8_t expect[4] = {3u, 4u, 5u, 6u};
    for (unsigned i = 0u; i < 4u; i++) {
        TEST_ASSERT_TRUE(ring_buffer_pop(&w, &out));
        TEST_ASSERT_EQUAL_UINT8(expect[i], out);
    }
}

void test_full_after_wrap_around_rejects_push(void)
{
    /* 环绕后 head==tail，但此时是"满"不是"空"——
       这正是 count 判满设计存在的意义，也是取模错误变异体的坟场 */
    ring_buffer_t w;
    uint8_t buf[4];
    uint8_t out;
    (void)ring_buffer_init(&w, buf, sizeof(buf));
    for (unsigned i = 0u; i < 4u; i++) { (void)ring_buffer_push(&w, (uint8_t)i); }
    (void)ring_buffer_pop(&w, &out);
    (void)ring_buffer_push(&w, 9u);      /* head 追上 tail，重新满 */
    TEST_ASSERT_TRUE(ring_buffer_is_full(&w));
    TEST_ASSERT_FALSE(ring_buffer_push(&w, 0xAAu));
}

/* ---------------- clear / 容量1 / 长序列计数（3） ---------------- */

void test_clear_makes_buffer_fully_reusable(void)
{
    for (unsigned i = 0u; i < 8u; i++) { (void)ring_buffer_push(&rb, (uint8_t)i); }
    ring_buffer_clear(&rb);
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&rb));
    TEST_ASSERT_EQUAL_UINT(0u, (unsigned)ring_buffer_count(&rb));
    TEST_ASSERT_EQUAL_UINT(8u, (unsigned)ring_buffer_free(&rb));
    /* 清空后必须能再次写满——证明索引真正复位而不是只改了 count */
    for (unsigned i = 0u; i < 8u; i++) {
        TEST_ASSERT_TRUE(ring_buffer_push(&rb, (uint8_t)(0xF0u + i)));
    }
    uint8_t out;
    (void)ring_buffer_pop(&rb, &out);
    TEST_ASSERT_EQUAL_HEX8(0xF0u, out);
}

void test_capacity_one_boundary(void)
{
    /* 最小合法容量：任何"边界用错 < 还是 <="的 bug 在这里无处遁形 */
    ring_buffer_t one;
    uint8_t buf[1];
    uint8_t out = 0u;
    (void)ring_buffer_init(&one, buf, sizeof(buf));

    TEST_ASSERT_TRUE(ring_buffer_push(&one, 0xAAu));
    TEST_ASSERT_TRUE(ring_buffer_is_full(&one));
    TEST_ASSERT_FALSE(ring_buffer_push(&one, 0xBBu));
    TEST_ASSERT_TRUE(ring_buffer_pop(&one, &out));
    TEST_ASSERT_EQUAL_HEX8(0xAAu, out);
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&one));
    TEST_ASSERT_FALSE(ring_buffer_pop(&one, &out));
}

void test_count_stays_consistent_over_alternating_long_sequence(void)
{
    /* 容量 16，每轮 push 2 个 pop 1 个（净 +1），跑 10 轮：
       验证长时间交错操作下 count 不漂移、顺序不错乱 */
    ring_buffer_t w;
    uint8_t buf[16];
    (void)ring_buffer_init(&w, buf, sizeof(buf));

    uint8_t pushed = 0u;
    for (uint8_t round = 0u; round < 10u; round++) {
        TEST_ASSERT_TRUE(ring_buffer_push(&w, pushed++));
        TEST_ASSERT_TRUE(ring_buffer_push(&w, pushed++));
        uint8_t out = 0xFFu;
        TEST_ASSERT_TRUE(ring_buffer_pop(&w, &out));
        TEST_ASSERT_EQUAL_UINT8(round, out);   /* 第 round 轮弹出的应是 round */
        TEST_ASSERT_EQUAL_UINT((unsigned)(round + 1u), (unsigned)ring_buffer_count(&w));
    }
    /* 剩余 10 个元素按 10..19 顺序排出 */
    for (uint8_t expect = 10u; expect < 20u; expect++) {
        uint8_t out;
        TEST_ASSERT_TRUE(ring_buffer_pop(&w, &out));
        TEST_ASSERT_EQUAL_UINT8(expect, out);
    }
    TEST_ASSERT_TRUE(ring_buffer_is_empty(&w));
}
