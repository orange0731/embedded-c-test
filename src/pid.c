#include "pid.h"
#include <stddef.h>  /* NULL 定义于此，IWYU */

bool pid_init(pid_t *pid, float kp, float ki, float kd, float out_min, float out_max)
{
    if (pid == NULL) {
        return false;
    }
    /* 限幅倒挂是配置错误的头号来源，必须在 init 拦截 */
    if (out_min >= out_max) {
        return false;
    }
    pid->kp = kp;
    pid->ki = ki;
    pid->kd = kd;
    pid->out_min = out_min;
    pid->out_max = out_max;
    /* 教学简化：积分限幅 = 输出限幅。量产项目会独立整定或引入条件积分 */
    pid->int_min = out_min;
    pid->int_max = out_max;
    pid->integral   = 0.0f;
    pid->prev_error = 0.0f;
    return true;
}

void pid_reset(pid_t *pid)
{
    if (pid == NULL) {
        return;
    }
    pid->integral   = 0.0f;
    pid->prev_error = 0.0f;
}

bool pid_compute(pid_t *pid, float setpoint, float measurement, float dt, float *output)
{
    if ((pid == NULL) || (output == NULL)) {
        return false;
    }
    /* dt 非法时提前返回：既不除零，也保证调用者输出变量不被污染 */
    if (dt <= 0.0f) {
        return false;
    }

    float error = setpoint - measurement;

    /* 先积分后限幅：抗饱和的核心，方向错一点都会被执行序列测试抓住 */
    pid->integral += error * dt;
    if (pid->integral > pid->int_max) {
        pid->integral = pid->int_max;
    }
    if (pid->integral < pid->int_min) {
        pid->integral = pid->int_min;
    }

    /* 注意：首周期 prev_error=0 会有微分冲击（derivative kick），
       量产做法是"对测量值微分"——本项目的取舍见 README trade-off 章节 */
    float derivative = (error - pid->prev_error) / dt;

    float out = pid->kp * error
              + pid->ki * pid->integral
              + pid->kd * derivative;

    if (out > pid->out_max) {
        out = pid->out_max;
    }
    if (out < pid->out_min) {
        out = pid->out_min;
    }

    pid->prev_error = error;
    *output = out;
    return true;
}
