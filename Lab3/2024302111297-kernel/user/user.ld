/* 赠送件 · 不可改 —— 用户程序链接脚本(lab2 起)
 * 用户程序从地址 0 链接(trampoline ABI:用户地址空间从 0 起,
 * TRAPFRAME 页在顶端,由内核布置——那是你的设计空间)。
 * 注意:lab3 开启 SV39 后本脚本不变;你的内核负责把 0 开始的
 * 虚拟地址映射到程序所在的物理页。
 */
OUTPUT_ARCH(riscv)
ENTRY(main)

SECTIONS
{
  . = 0x0;

  .text : { *(.text .text.*) }
  .rodata : { *(.rodata .rodata.*) }
  .data : { *(.data .data.*) }
  .bss : {
    *(.bss .bss.*)
    *(.sbss*)
  }

  end = .;
}
