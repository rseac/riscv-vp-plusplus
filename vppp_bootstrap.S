.globl _start
.globl main
_start:
    la sp, _sp
    li t0, 0x00006600
    csrs mstatus, t0
    jal main
    mv a0, a0
    li a7, 93
    ecall
.section .bss
.align 4
.space 65536
_sp:
