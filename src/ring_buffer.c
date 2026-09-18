#include "ring_buffer.h"

bool ring_buffer_init(ring_buffer_t *rb, uint8_t *storage, size_t capacity)
{
    /* 防御优先于解引用：顺序不能反，否则 NULL 检查本身就没意义 */
    if ((rb == NULL) || (storage == NULL) || (capacity == 0u)) {
        return false;
    }
    rb->buffer   = storage;
    rb->capacity = capacity;
    rb->head     = 0u;
    rb->tail     = 0u;
    rb->count    = 0u;
    return true;
}

bool ring_buffer_push(ring_buffer_t *rb, uint8_t data)
{
    /* rb->buffer == NULL 防御的是"调用者传了未初始化的句柄"这种真实现场故障 */
    if ((rb == NULL) || (rb->buffer == NULL)) {
        return false;
    }
    /* 用 count 判满：这是本模块的核心不变式（invariant） */
    if (rb->count >= rb->capacity) {
        return false;
    }
    rb->buffer[rb->head] = data;
    /* 取模回绕是环绕行为的关键，变异测试会专门攻击这一行 */
    rb->head = (rb->head + 1u) % rb->capacity;
    rb->count++;
    return true;
}

bool ring_buffer_pop(ring_buffer_t *rb, uint8_t *data)
{
    if ((rb == NULL) || (data == NULL) || (rb->buffer == NULL)) {
        return false;
    }
    if (rb->count == 0u) {
        return false;
    }
    *data = rb->buffer[rb->tail];
    rb->tail = (rb->tail + 1u) % rb->capacity;
    rb->count--;
    return true;
}

bool ring_buffer_peek(const ring_buffer_t *rb, uint8_t *data)
{
    if ((rb == NULL) || (data == NULL) || (rb->buffer == NULL)) {
        return false;
    }
    if (rb->count == 0u) {
        return false;
    }
    /* peek 与 pop 的唯一区别：不移动 tail、不改 count——测试要证明这一点 */
    *data = rb->buffer[rb->tail];
    return true;
}

size_t ring_buffer_count(const ring_buffer_t *rb)
{
    /* 查询接口的防御策略：返回安全默认值 0，让调用者的循环自然退出 */
    return (rb == NULL) ? 0u : rb->count;
}

size_t ring_buffer_free(const ring_buffer_t *rb)
{
    return (rb == NULL) ? 0u : (rb->capacity - rb->count);
}

bool ring_buffer_is_empty(const ring_buffer_t *rb)
{
    /* NULL 视为"空"：调用者拿去 guarding 写流程时是安全方向 */
    return (rb == NULL) ? true : (rb->count == 0u);
}

bool ring_buffer_is_full(const ring_buffer_t *rb)
{
    /* NULL 视为"满"：guarding 读流程时是安全方向（宁可不写不可越界） */
    return (rb == NULL) ? true : (rb->count >= rb->capacity);
}

void ring_buffer_clear(ring_buffer_t *rb)
{
    if (rb == NULL) {
        return;
    }
    /* 只复位索引与计数，不清 storage：避免 O(n) 擦除，实时系统里时间要可预测 */
    rb->head  = 0u;
    rb->tail  = 0u;
    rb->count = 0u;
}