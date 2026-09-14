#!/usr/bin/env bash
# ============================================================
# check_coverage.sh — 覆盖率质量门禁
#   语句覆盖率 >= 95%，分支覆盖率 >= 90%，不达标 exit 1
# 用法：bash scripts/check_coverage.sh
# 自检：把 LINE_MIN 临时改成 101 再跑，必须 FAIL——
#       证明门禁本身不是"永远绿灯"的摆设
# ============================================================
set -euo pipefail

LINE_MIN=95
BRANCH_MIN=90

echo "[1/3] 清理旧构建产物（防止陈旧 .gcda 污染统计）..."
ceedling clobber

echo "[2/3] 以 gcov 插桩编译并运行全部测试..."
ceedling gcov:all

echo "[3/3] gcovr 汇总 + 门禁判定..."
mkdir -p build/gcov

# --object-directory：指向 build/gcov 根，gcovr 递归找到 out/ 下的 .gcda/.gcno
# --filter 'src/'   ：只统计被测源码
# --exclude         ：unity.c / cmock.c 由 Ceedling vendor 到 build/ 下，
#                     路径里也含 "src/"，必须显式剔除，否则拖低分母
gcovr --root . \
      --object-directory build/gcov \
      --filter 'src/' \
      --exclude '.*unity.*' \
      --exclude '.*[Cc]mock.*' \
      --print-summary \
      --txt build/gcov/coverage.txt \
      --html --html-details --html-title "ECU Unit Test Coverage" \
      -o build/gcov/coverage.html \
      --xml build/gcov/coverage.xml \
      --fail-under-line ${LINE_MIN} \
      --fail-under-branch ${BRANCH_MIN}

echo "PASS: 覆盖率达标 (line >= ${LINE_MIN}%, branch >= ${BRANCH_MIN}%)"
echo "      HTML 报告: build/gcov/coverage.html"
