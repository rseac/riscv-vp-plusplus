.globl _start
.globl main
_start:
    la sp, _sp
    li t0, 0x00006600      /* mstatus.FS=1, VS=1 (enable FP + Vector) */
    csrs mstatus, t0
    jal main
    mv a0, a0
    li a7, 93              /* exit via ecall */
    ecall
.section .bss
.align 4
.space 65536
_sp:

.globl hw_cnt_en_reg
.align 3
hw_cnt_en_reg:
    .dword 0
