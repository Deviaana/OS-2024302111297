# 赠送件 —— lab2 Makefile 升级件(不可改本文件的规则本身;
# 合并方式:把下面全部规则并入你的 Makefile,并把 kernel/userimg.o
# 加进你的 OBJS。三固定项(make kernel / make qemu / course_sid.h
# 依赖)保持不变。
# ⚠ 历史合并陷阱(已在本文件内修复,合并时整段照抄即可):
#   1) OBJCOPY 变量:基线 Makefile 未定义,本文件已自带定义;
#   2) UFLAGS 需含 -I. :用户程序 #include "kernel/types.h" 从树根解析。)
#
# 设计说明:lab2 阶段没有磁盘、没有文件系统(那是 lab6 的债)。
# 用户程序由本规则链编译为平铺二进制,经 userimg.S 内嵌进内核镜像;
# 你的 sys_exec 从内嵌程序表按名字查找并加载(表格式见下方注释)。

UPROGS = sh hi spin

OBJCOPY ?= riscv64-unknown-elf-objcopy

UFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc \
         -fno-stack-protector -fno-pie -I.

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
	@if [ -f user/common.c ]; then \
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
