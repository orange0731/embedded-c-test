#ifndef RING_BUFFER_H
#define RING_BUFFER_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * 环形缓冲区（字节流）
 * 设计要点：
 *  - 存储空间由调用者提供，模块内部不做任何动态内存分配（嵌入式无堆约束）
 *  - 使用 count 计数器区分"满"与"空"，capacity 全部可用
 *  - 所有公开 API 均做 NULL 防御，失败返回 false 而不是崩溃
 */
typedef struct {
    uint8_t *buffer;    /* 外部提供的存储区                       */
    size_t   capacity;  /* 存储区容量（可用槽位数 = capacity）     */
    size_t   head;      /* 写指针：下一次 push 写入的位置          */
    size_t   tail;      /* 读指针：下一次 pop 读取的位置           */
    size_t   count;     /* 当前元素个数，满/空判定依据             */
} ring_buffer_t;

/* 初始化。任一参数非法返回 false，且不解引用任何指针 */
bool   ring_buffer_init(ring_buffer_t *rb, uint8_t *storage, size_t capacity);

/* 压入一个字节；缓冲区满或句柄非法返回 false */
bool   ring_buffer_push(ring_buffer_t *rb, uint8_t data);

/* 弹出一个字节到 *data；空或参数非法返回 false */
bool   ring_buffer_pop(ring_buffer_t *rb, uint8_t *data);

/* 窥探最旧字节但不移除；空或参数非法返回 false */
bool   ring_buffer_peek(const ring_buffer_t *rb, uint8_t *data);

/* 当前元素个数；NULL 句柄返回 0（查询类接口用安全默认值而不是 bool） */
size_t ring_buffer_count(const ring_buffer_t *rb);

/* 剩余可写空间 = capacity - count */
size_t ring_buffer_free(const ring_buffer_t *rb);

bool   ring_buffer_is_empty(const ring_buffer_t *rb);
bool   ring_buffer_is_full(const ring_buffer_t *rb);

/* 复位为空态；对 NULL 句柄安全（直接返回） */
void   ring_buffer_clear(ring_buffer_t *rb);

#endif /* RING_BUFFER_H */
