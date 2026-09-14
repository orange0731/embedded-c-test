#ifndef PID_H
#define PID_H

#include <stdbool.h>

/*
 * 位置式 PID 控制器
 * out = Kp*e + Ki*∫e·dt + Kd*de/dt
 * 特性：输出限幅 + 积分限幅（抗饱和）。教学简化：积分限幅取输出限幅。
 */
typedef struct {
    float kp, ki, kd;
    float out_min, out_max;
    float int_min, int_max;   /* 积分限幅，init 时默认等于输出限幅 */
    float integral;           /* 积分累加器 */
    float prev_error;         /* 上次误差，微分项用 */
} pid_t;

/* 初始化；pid 为 NULL 或 out_min >= out_max 返回 false */
bool pid_init(pid_t *pid, float kp, float ki, float kd, float out_min, float out_max);

/* 清零积分与上次误差（增益与限幅保留） */
void pid_reset(pid_t *pid);

/*
 * 计算一个控制周期。
 * dt <= 0 属于非法输入：返回 false 且保证不修改 *output（防除零/防污染）。
 */
bool pid_compute(pid_t *pid, float setpoint, float measurement, float dt, float *output);

#endif /* PID_H */
