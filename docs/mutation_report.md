# 变异测试报告（Mutation Testing Report）

> 目的：回答"我们的测试套件真的能抓 bug 吗？"
> 方法：手工向源码植入典型缺陷（变异体），观察测试是否变红。
> 指标：**Mutation Score = 被杀死的变异体数 / 总变异体数 = 6/6 = 100%**
> 操作：git 基线 → 植入一行变异 → `ceedling test:all` → 记录 → `git checkout -- src/` 恢复。

## 变异体清单

| ID | 文件/位置 | 变异内容 | 缺陷类型 | 杀死它的用例 | 结果 |
|----|-----------|----------|----------|--------------|------|
| M1 | ring_buffer.c `push` | `count >= capacity` → `count > capacity` | 差一（off-by-one） | `test_push_fails_when_full`（第 9 次 push 应失败却成功） | ☠️ KILLED |
| M2 | ring_buffer.c `push` | `% capacity` → `% (capacity - 1)` | 取模运算错误 | `test_wrap_around_preserves_fifo_order` + `test_push_pop_maintains_fifo_order` | ☠️ KILLED |
| M3 | ring_buffer.c `push` | 删除 `count++` | 状态更新丢失 | `test_push_increments_count_and_decreases_free` + `test_push_fails_when_full` | ☠️ KILLED |
| M4 | modbus_parser.c `validate_read_quantity` | `quantity < 1` → `quantity <= 1` | 边界 `<` 误作 `<=` | `test_parse_known_frame_read_one_register`（quantity=1 合法帧被误判） | ☠️ KILLED |
| M5 | pid.c `compute` 上限支路 | `integral = int_max` → `integral = int_min` | 限幅方向写反 | `test_integral_anti_windup_clamps_at_max`（输出从 10 跳变到 -10） | ☠️ KILLED |
| M6 | sht30.c `map_hal_status` | `default: return SHT30_ERR_PARAM` → `return SHT30_ERR_BUS` | 兜底逻辑错误 | `test_trigger_maps_out_of_range_hal_status_to_param_error`（注入越界枚举 99） | ☠️ KILLED |

**Mutation Score：6/6 = 100%**

## M6 的故事：一个"曾经存活"的变异体如何驱动用例补强

初版 sht30 套件（19 条用例）中，M6 **存活**了。原因：`map_hal_status` 对所有
**合法**枚举值都有显式 case，`default` 只有"越界/未知枚举"才能到达——
当时的用例只注入合法枚举，default 成了覆盖率与断言的双重盲区。

处置（二选一，本项目选 A）：

- **A. 补强用例，注入越界枚举逼出 default 分支**（已采用，即套件第 20 条用例）：
  ```c
  hal_i2c_write_ExpectAnyArgsAndReturn((hal_i2c_status_t)99);
  TEST_ASSERT_EQUAL_INT(SHT30_ERR_PARAM,
                        sht30_trigger_measurement(SHT30_I2C_ADDR));
  ```
  工程理由：HAL 由其他团队维护，未来新增枚举值而驱动未跟进时，
  default 兜底就是最后一道防线——它必须被测试钉住。
- **B. 删除 default，靠 `-Wswitch` 编译告警兜底**：防线前移到编译期，
  也是合法取舍，前提是团队接受 `-Werror`。

## 结论与教训

1. 覆盖率 99% 的套件仍可能放过 M6 —— **覆盖率度量"执行过"，
   变异测试度量"断言有效"**，两者必须搭配。
2. 每个被杀死的变异体都精确对应至少一个用例，说明用例是"按分支设计"的。
3. 等效变异体（如 `0xFFFFu` → `0xFFFF`）永远杀不死——不计入分母，靠评审识别。
