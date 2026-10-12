# lab2 Makefile:lab1 骨架 + gifts/Makefile.upgrade 的规则(整段并入)
CC   = riscv64-unknown-elf-gcc
LD   = riscv64-unknown-elf-ld
NM   ?= riscv64-unknown-elf-nm
QEMU = qemu-system-riscv64
CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector -fno-pie

# 三固定项之一:make kernel 的产物路径与 OBJS 集合保持 lab1 形态
OBJS = kernel/entry.o kernel/start.o kernel/console.o kernel/printf.o \
       kernel/main.o kernel/trap.o kernel/syscall.o kernel/sysproc.o \
       kernel/proc.o kernel/string.o kernel/plic.o \
       kernel/trampoline.o kernel/userimg.o kernel/userentry.o

kernel/kernel: $(OBJS) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ $(OBJS)

%.o: %.c kernel/riscv.h kernel/course_sid.h
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

# =====================================================================
# 以下规则整段并入自 gifts/Makefile.upgrade(赠送件,规则本身不改)
# =====================================================================

UPROGS = sh hi spin badecall bufstorm bufcap

OBJCOPY ?= riscv64-unknown-elf-objcopy

UFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc \
         -fno-stack-protector -fno-pie -mno-relax -I.

# ---- lab2 附加:-mno-relax(必须)----
# 用户程序从 0 链接、却被内核搬到别处执行,因此必须保持"位置无关"。
# 不加 -mno-relax 时,RISC-V 链接器松弛会把 `auipc+addi`(PC 相对)折叠成
# `addi rd,zero,<link 地址>`(绝对寻址),实测表现为 sh 第一次写 static buf
# 就 store access fault(stval=0x740)。加上它,访问恢复为 auipc 相对。
# usys 桩由 perl 脚本生成(课程赠送,勿改)
user/usys.S: user/usys.pl
	perl user/usys.pl > user/usys.S

# 用户程序 → 链接(基址 0)→ 平铺二进制
user-flat/%.bin: user/%.c user/user.h user/ulib.c user/printf.c user/usys.S
	mkdir -p user-flat
	$(CC) $(UFLAGS) -Iuser -c user/ulib.c   -o user-flat/ulib.o
	$(CC) $(UFLAGS) -Iuser -c user/printf.c -o user-flat/printf.o
	$(CC) $(UFLAGS) -Iuser -c user/usys.S   -o user-flat/usys.o
	$(CC) $(UFLAGS) -Iuser -c $<            -o user-flat/$*.o
	  if [ -f user/common.c ]; then \
	    $(CC) $(UFLAGS) -Iuser -c user/common.c -o user-flat/common.o; \
	    EXTRA_OBJS=user-flat/common.o; \
	  else EXTRA_OBJS=; fi; \
	  $(LD) -T user/user.ld -o user-flat/$*.elf \
	      user-flat/$*.o $$EXTRA_OBJS user-flat/ulib.o user-flat/printf.o user-flat/usys.o
	$(OBJCOPY) -O binary user-flat/$*.elf $@

# 内嵌程序表(ABI,勿改):
#   _uprog_table: 每项 = .quad start; .quad end; .asciz "name";
#   以 .quad 0; .quad 0 结尾。内核侧按表遍历。
kernel/userimg.S: Makefile $(addprefix user-flat/,$(UPROGS:=.bin))
	printf '.section .rodata\n .global _uprog_table\n_uprog_table:\n' > $@.tmp
	for p in $(UPROGS); do \
	    printf ' .quad _uprog_%s_start\n .quad _uprog_%s_end\n .asciz "%s"\n .balign 8\n' \
	      $$p $$p $$p >> $@.tmp; \
	    printf ' .global _uprog_%s_start\n_uprog_%s_start:\n .incbin "user-flat/%s.bin"\n .global _uprog_%s_end\n_uprog_%s_end:\n .balign 8\n' \
	      $$p $$p $$p $$p $$p >> $@.tmp; \
	done
	printf ' .quad 0\n .quad 0\n' >> $@.tmp
	mv $@.tmp $@

clean-extra:
	rm -rf user-flat kernel/userimg.S user/usys.S

# =====================================================================
# lab2 附加件:入口偏移表(不修改上面的赠送规则)
#   内嵌镜像的入口 = main 在平铺二进制里的偏移。它未必为 0(badecall.c
#   的 main 前面有静态函数),所以构建期用 nm 从 ELF 取出来,生成一张与
#   _uprog_table 同序的表,内核按名查表时同步取用。
# =====================================================================
kernel/userentry.S: $(addprefix user-flat/,$(UPROGS:=.bin))
	printf '.section .rodata\n .global _uprog_entry\n_uprog_entry:\n' > $@.tmp
	for p in $(UPROGS); do \
	    a=`$(NM) user-flat/$$p.elf | awk '$$3=="main"{print $$1}'`; \
	    if [ -z "$$a" ]; then echo "no main in user-flat/$$p.elf" >&2; exit 1; fi; \
	    printf ' .quad 0x%s\n' "$$a" >> $@.tmp; \
	done
	mv $@.tmp $@

# 三固定项之二/之三:make qemu 的命令行与 course_sid.h 依赖链保持 lab1 形态
qemu: kernel/kernel
	$(QEMU) -machine virt -bios none -kernel kernel/kernel -nographic

clean:
	rm -f kernel/*.o kernel/kernel
	rm -rf user-flat kernel/userimg.S kernel/userentry.S user/usys.S