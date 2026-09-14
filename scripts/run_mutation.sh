#!/usr/bin/env bash
# ============================================================
# run_mutation.sh — 半自动变异测试
# 每个变异体：备份 → sed 植入 → ceedling test:all → 期望失败 → 恢复
# 全部 KILLED 才退出 0；任何 SURVIVED 说明套件存在断言盲区
# 幂等：每个变异体跑完都恢复原文件，可重复执行
# ============================================================
set -u
KILLED=0
TOTAL=0
LOG=/tmp/mutation_run.log

mutate_and_run() {
    local id="$1" file="$2" pattern="$3" replacement="$4" desc="$5"
    TOTAL=$((TOTAL+1))
    cp "$file" "$file.mutbak"
    sed -i "s#${pattern}#${replacement}#" "$file"
    if ceedling test:all > "$LOG" 2>&1; then
        echo "  ❌ $id SURVIVED —— $desc"
        echo "     （套件全绿，没有 use case 抓住它！日志：$LOG）"
    else
        echo "  ☠️  $id KILLED   —— $desc"
        KILLED=$((KILLED+1))
    fi
    mv "$file.mutbak" "$file"
}

echo "===== 变异测试开始 ====="

mutate_and_run M1 src/ring_buffer.c \
  'if (rb->count >= rb->capacity)' \
  'if (rb->count > rb->capacity)' \
  '差一错误：满判定 off-by-one'

mutate_and_run M2 src/ring_buffer.c \
  'rb->head = (rb->head + 1u) % rb->capacity;' \
  'rb->head = (rb->head + 1u) % (rb->capacity - 1u);' \
  '取模运算写错：环绕周期错乱'

mutate_and_run M3 src/ring_buffer.c \
  'rb->count++;' \
  '/* rb->count++; MUTANT */' \
  '状态丢失：push 不再更新 count'

mutate_and_run M4 src/modbus_parser.c \
  'if ((quantity < 1u) || (quantity > 125u))' \
  'if ((quantity <= 1u) || (quantity > 125u))' \
  '边界运算符：< 误作 <=（quantity=1 合法帧被误杀）'

mutate_and_run M5 src/pid.c \
  'pid->integral = pid->int_max;' \
  'pid->integral = pid->int_min;' \
  '积分限幅方向写反'

mutate_and_run M6 src/sht30.c \
  'default: *return SHT30_ERR_PARAM' \
  'default: return SHT30_ERR_BUS' \
  '兜底逻辑错误：default 返回值写反'

echo "===== Mutation Score: ${KILLED}/${TOTAL} ====="
[ "${KILLED}" -eq "${TOTAL}" ]
