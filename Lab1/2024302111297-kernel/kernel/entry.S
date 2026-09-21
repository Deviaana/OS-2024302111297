#include "course_sid.h"

        .section .text
        .globl _entry
_entry:
        csrw mie, zero

        csrr a0, mhartid
        bnez a0, park

        la sp, boot_stack
        li t0, LAB1_STACK_KB
        slli t0, t0, 10
        add sp, sp, t0

        call start

park:
        wfi
        j park

        .balign 4
        .globl machine_trap_vector
machine_trap_vector:
        j park

        .section .bss
        .balign 16
        .globl boot_stack
boot_stack:
        .space LAB1_STACK_KB * 1024
