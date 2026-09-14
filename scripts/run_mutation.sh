#!/usr/bin/env bash
# ============================================================
# run_mutation.sh — 半自动变异测试（加固版）
#  1. 基线检查：套件不绿则中止（防环境故障被误判为 KILLED）
#  2. 植入验证：sed 后 grep 确认变异写入（防 pattern 漂移假结果）
#  3. trap 恢复：Ctrl+C / 异常退出也会恢复源码
# ============================================================
set -u
KILLED=0
TOTAL=0
LOG=/tmp/mutation_run.log
declare -a BACKED_UP=()

restore_all() {
  for f in "${BACKED_UP[@]}"; do
    [ -f "${f}.mutbak" ] && mv -f "${f}.mutbak" "${f}"
  done
}
trap restore_all EXIT INT TERM

echo "[0/6] 基线检查：未植入变异时套件必须全绿"
if ! ceedling test:all > "$LOG" 2>&1; then
  echo "  ❌ 基线为红，中止（先修复套件）。日志：$LOG"
  exit 2
fi
echo "  ✅ 基线全绿，开始植入"

mutate_and_run() {
    local id="$1" file="$2" pattern="$3" replacement="$4" desc="$5"
    TOTAL=$((TOTAL+1))
    cp "$file" "$file.mutbak"
    BACKED_UP+=("$file")
    sed -i "s#${pattern}#${replacement}#" "$file"
    if ! grep -qF "$replacement" "$file"; then
        echo "  ⚠️  $id 变异植入失败（pattern 未命中：$pattern），已恢复并中止"
        exit 3
    fi
    if ceedling test:all > "$LOG" 2>&1; then
        echo "  ❌ $id SURVIVED —— $desc"
    else
        echo "  ☠️  $id KILLED   —— $desc"
        KILLED=$((KILLED+1))
    fi
    mv -f "$file.mutbak" "$file"
}

mutate_and_run M1 src/ring_buffer.c \
  'if (rb->count >= rb->capacity)' 'if (rb->count > rb->capacity)' \
  '差一错误：满判定 off-by-one'
mutate_and_run M2 src/ring_buffer.c \
  'rb->head = (rb->head + 1u) % rb->capacity;' \
  'rb->head = (rb->head + 1u) % (rb->capacity - 1u);' \
  '取模运算写错：环绕周期错乱'
mutate_and_run M3 src/ring_buffer.c \
  'rb->count++;' '/* rb->count++; MUTANT */' \
  '状态丢失：push 不再更新 count'
mutate_and_run M4 src/modbus_parser.c \
  'if ((quantity < 1u) || (quantity > 125u))' \
  'if ((quantity <= 1u) || (quantity > 125u))' \
  '边界运算符：< 误作 <='
mutate_and_run M5 src/pid.c \
  'pid->integral = pid->int_max;' 'pid->integral = pid->int_min;' \
  '积分限幅方向写反'
mutate_and_run M6 src/sht30.c \
  'default: *return SHT30_ERR_PARAM' 'default: return SHT30_ERR_BUS' \
  '兜底逻辑错误：default 返回值写反'

echo "===== Mutation Score: ${KILLED}/${TOTAL} ====="
[ "${KILLED}" -eq "${TOTAL}" ]
