# lab2 官方用户测试集（课程组出品 · 判分参照）

构建:与 sh/hi/spin 一样走内嵌方案 —— 在 Makefile `UPROGS` 后追加
`badecall bufstorm`(或按《能力目标与接口约定》§5 的合并方式)。

| 测试 | 覆盖 | 通过判据 | 驱动方式 |
|---|---|---|---|
| `badecall` | 未知调用号返回 -1、不 panic | 打出 `TEST-1 PASS`;sh 存活 | 直接执行 |
| `bufstorm` | 输入缓冲边界与语义 | 读满 4 行并完整回显,`BUFSTORM lines=4`;配合 `inject_uart.py --script` 灌入超长行(> 你的 LAB2_BUF_SIZE)验证不丢计数、不 panic | inject_uart storm |

验收口径:两测全过 = 本项功能演示通过;`bufstorm` 的"超长行丢字"属
缓冲区满的**文档化行为**,不计失败,但执行后内核必须完好。
