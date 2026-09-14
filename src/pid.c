#include "pid.h"
#include <math.h>   /* isnan：NaN 无法被 <= 比较捕获，必须显式检查 */
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
    /* dt 非法（含 NaN）时提前返回：既不除零，也保证输出不被污染。
       注意：NaN 参与任何比较结果都是 false，"dt <= 0" 拦不住它 */
    if (isnan(dt) || (dt <= 0.0f)) {
        return false;
    }

    float error = setpoint - measurement;
    /* error 为 NaN 直接拒绝：NaN 有传染性，一旦进入积分器将永久污染 */
    if (isnan(error)) {
        return false;
    }

    /* 先积分后限幅：抗饱和的核心 */
    pid->integral += error * dt;
    if (pid->integral > pid->int_max) {
        pid->integral = pid->int_max;
    }
    if (pid->integral < pid->int_min) {
        pid->integral = pid->int_min;
    }

    /* 首周期 prev_error=0 存在微分冲击（derivative kick），
       量产做法是"对测量值微分"——见 README 改进路线 */
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
