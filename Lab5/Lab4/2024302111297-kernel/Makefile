# lab3 Makefile: lab1 skeleton + lab2 process model + SV39 virtual memory
CC   = riscv64-unknown-elf-gcc
LD   = riscv64-unknown-elf-ld
NM   ?= riscv64-unknown-elf-nm
QEMU = qemu-system-riscv64
CFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc -fno-stack-protector -fno-pie

# Keep the lab1 artifact path and qemu line unchanged for course acceptance.
OBJS = kernel/entry.o kernel/start.o kernel/console.o kernel/printf.o \
       kernel/main.o kernel/trap.o kernel/kernelvec.o kernel/syscall.o \
       kernel/sysproc.o kernel/proc.o kernel/string.o kernel/plic.o \
       kernel/kalloc.o kernel/vm.o kernel/trampoline.o \
       kernel/userimg.o kernel/userentry.o

kernel/kernel: $(OBJS) kernel/kernel.ld
	$(LD) -T kernel/kernel.ld -o $@ $(OBJS)

%.o: %.c kernel/riscv.h kernel/course_sid.h
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

%.o: %.S
	$(CC) $(CFLAGS) -Ikernel -c -o $@ $<

UPROGS = sh hi spin badecall bufstorm bufcap badaccess memcap copytest guardtest

OBJCOPY ?= riscv64-unknown-elf-objcopy

UFLAGS = -Wall -Werror -O -std=gnu99 -mcmodel=medany -ffreestanding \
         -nostdlib -fno-common -ggdb -march=rv64gc \
         -fno-stack-protector -fno-pie -mno-relax -I.

user/usys.S: user/usys.pl
	perl user/usys.pl > user/usys.S

# The course-provided user.ld is not edited in place. Derive a build copy
# that page-aligns .data and exposes the text/data/image boundaries needed to
# give code and data different PTE permissions.
user-flat/user.ld: user/user.ld
	mkdir -p user-flat
	awk '\
	  /\.data : \{/ { \
	    print "  _text_end = .;"; \
	    print "  . = ALIGN(4096);"; \
	    print "  _data_start = .;"; \
	    print; next; \
	  } \
	  /end = \.;/ { \
	    print "  end = .;"; \
	    print "  _image_end = .;"; \
	    next; \
	  } \
	  { print }' $< > $@

user-flat:
	mkdir -p user-flat

USER_COMMON =
ifneq ($(wildcard user/common.c),)
USER_COMMON = user-flat/common.o
endif

# Build the shared user objects once.  Keeping these as real make targets
# avoids two -j recipes writing the same .o file concurrently.
user-flat/ulib.o: user/ulib.c user/user.h | user-flat
	$(CC) $(UFLAGS) -Iuser -c $< -o $@

user-flat/printf.o: user/printf.c user/user.h | user-flat
	$(CC) $(UFLAGS) -Iuser -c $< -o $@

user-flat/usys.o: user/usys.S | user-flat
	$(CC) $(UFLAGS) -Iuser -c $< -o $@

user-flat/common.o: user/common.c user/user.h | user-flat
	$(CC) $(UFLAGS) -Iuser -c $< -o $@

user-flat/%.o: user/%.c user/user.h | user-flat
	$(CC) $(UFLAGS) -Iuser -c $< -o $@

user-flat/%.bin: user-flat/%.o $(USER_COMMON) user-flat/ulib.o \
                 user-flat/printf.o user-flat/usys.o user-flat/user.ld
	$(LD) -T user-flat/user.ld -o user-flat/$*.elf \
	    user-flat/$*.o $(USER_COMMON) user-flat/ulib.o user-flat/printf.o user-flat/usys.o
	$(OBJCOPY) -O binary user-flat/$*.elf $@

# Program table ABI:
#   _uprog_table entry = start, image_end, text_end, data_start, file_size, name
kernel/userimg.S: Makefile $(addprefix user-flat/,$(UPROGS:=.bin))
	printf '.section .rodata\n .global _uprog_table\n_uprog_table:\n' > $@.tmp
	for p in $(UPROGS); do \
	    fs=`wc -c < user-flat/$$p.bin`; \
	    te=`$(NM) user-flat/$$p.elf | awk '$$3=="_text_end"{print $$1}'`; \
	    ds=`$(NM) user-flat/$$p.elf | awk '$$3=="_data_start"{print $$1}'`; \
	    en=`$(NM) user-flat/$$p.elf | awk '$$3=="end"{print $$1}'`; \
	    if [ -z "$$te" ] || [ -z "$$ds" ] || [ -z "$$en" ]; then \
	      echo "missing section boundary symbols in $$p" >&2; exit 1; \
	    fi; \
	    printf ' .quad _uprog_%s_start\n .quad 0x%s\n .quad 0x%s\n .quad 0x%s\n .quad %s\n .asciz "%s"\n .balign 8\n' \
	      $$p $$en $$te $$ds $$fs $$p >> $@.tmp; \
	done
	printf ' .quad 0\n .quad 0\n .quad 0\n .quad 0\n .quad 0\n' >> $@.tmp
	for p in $(UPROGS); do \
	    printf ' .global _uprog_%s_start\n_uprog_%s_start:\n .incbin "user-flat/%s.bin"\n .global _uprog_%s_end\n_uprog_%s_end:\n .balign 8\n' \
	      $$p $$p $$p $$p $$p >> $@.tmp; \
	done
	mv $@.tmp $@

kernel/userentry.S: $(addprefix user-flat/,$(UPROGS:=.bin))
	printf '.section .rodata\n .global _uprog_entry\n_uprog_entry:\n' > $@.tmp
	for p in $(UPROGS); do \
	    a=`$(NM) user-flat/$$p.elf | awk '$$3=="main"{print $$1}'`; \
	    if [ -z "$$a" ]; then echo "no main in user-flat/$$p.elf" >&2; exit 1; fi; \
	    printf ' .quad 0x%s\n' "$$a" >> $@.tmp; \
	done
	mv $@.tmp $@

qemu: kernel/kernel
	$(QEMU) -machine virt -bios none -kernel kernel/kernel -nographic

clean:
	rm -f kernel/*.o kernel/kernel
	rm -rf user-flat kernel/userimg.S kernel/userentry.S user/usys.S
