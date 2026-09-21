/* ============ 预置支撑代码 · 请勿修改 ============
 * 说明: 加载地址与段排布规则(环境事实,注释讲清了为什么)
 * 修改该支撑代码可能破坏统一接口规范；若遇到问题建议通过 git diff 核对本段代码。
 * ======================================== */
/* lab1 链接脚本(课程版,带注释)
 * `.` 是位置计数器(VMA)。QEMU -kernel 按 program header 加载,
 * 故加载地址与这里一致 —— 这就是"内核的家在 0x80000000"的来源
 * (QEMU virt 的 RAM 起点)。
 * 你要核对的参数:BASE_ADDRESS(想想改成 0x0 会发生什么 —— 排雷见)。
 */
OUTPUT_ARCH(riscv)
ENTRY(_entry)

BASE_ADDRESS = 0x80000000;

SECTIONS
{
  . = BASE_ADDRESS;

  .text : {          /* 代码段:entry.S 的第一段指令就在这里开头 */
    *(.text .text.*)
  }

  .rodata : {        /* 只读数据:字符串常量等 */
    *(.rodata .rodata.*)
  }

  .data : {          /* 已初始化数据 */
    *(.data .data.*)
  }

  .bss : {           /* 未初始化数据:启动时由内核自行清零 */
    *(.bss .bss.*)
    *(.sbss*)
  }

  /* 内核结束符号:lab3 的 kalloc 将从这里开始分配物理页 */
  kernel_end = .;
}
