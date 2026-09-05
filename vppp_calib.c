// vppp_calib.c — XSTop timing-model correlation benchmark for RISC-V VP++.
//
// Single-op methodology (matches ara test_vector_timing.c): vppp's computeCycles()
// is DETERMINISTIC, so one instruction between two rdcycle reads gives the exact
// modeled per-op latency — no averaging loops (which would be pathologically slow
// through the per-instruction timing hook). Warm up once, then time ONE op.
//
// Output: [CALIB] <name> cycles=<n>  (directly comparable to the RTL per-op numbers
// in build/XSTop/reports/rtl_calibration.md).
//
// Build (Profile-B / vppp): riscv64-unknown-elf-gcc -O2 -march=rv64gcv -mabi=lp64d
//   -nostartfiles -Wl,--no-relax vppp_bootstrap.S vppp_calib.c
// NOTE: no Zbb `rol` bench — vppp's decoder does not implement Zbb (it traps).

#include <stdint.h>
#include <stdio.h>

static inline uint64_t rdc(void) {
    uint64_t c;
    __asm__ volatile("fence; csrr %0, cycle" : "=r"(c));
    return c;
}

static uint64_t A[512] __attribute__((aligned(256)));

// ---- Scalar ops (single-op) ----
static void scalar_ops(void) {
    uint64_t a = 3, b = 7, t0, t1, r;
    t0 = rdc(); __asm__ volatile("add %0,%0,%1":"+r"(a):"r"(b)); t1 = rdc();
    printf("[CALIB] alu_add cycles=%lu\n", (unsigned long)(t1 - t0));
    t0 = rdc(); __asm__ volatile("mul %0,%0,%1":"+r"(a):"r"(b)); t1 = rdc();
    printf("[CALIB] mul cycles=%lu\n", (unsigned long)(t1 - t0));
    a = 0xF0F0F0F0F0F0F0F0ULL;
    t0 = rdc(); __asm__ volatile("divu %0,%0,%1":"+r"(a):"r"((uint64_t)3)); t1 = rdc();
    printf("[CALIB] div cycles=%lu\n", (unsigned long)(t1 - t0));
    double x = 1.5, y = 2.5;
    t0 = rdc(); __asm__ volatile("fadd.d %0,%0,%1":"+f"(x):"f"(y)); t1 = rdc();
    printf("[CALIB] fadd_d cycles=%lu\n", (unsigned long)(t1 - t0));
    t0 = rdc(); __asm__ volatile("fmul.d %0,%0,%1":"+f"(x):"f"(y)); t1 = rdc();
    printf("[CALIB] fmul_d cycles=%lu\n", (unsigned long)(t1 - t0));
    t0 = rdc(); __asm__ volatile("fmadd.d %0,%0,%1,%2":"+f"(x):"f"(y),"f"(x)); t1 = rdc();
    printf("[CALIB] fmadd_d cycles=%lu\n", (unsigned long)(t1 - t0));
    t0 = rdc(); __asm__ volatile("fdiv.d %0,%0,%1":"+f"(x):"f"(y)); t1 = rdc();
    printf("[CALIB] fdiv_d cycles=%lu\n", (unsigned long)(t1 - t0));
    t0 = rdc(); __asm__ volatile("feq.d %0,%1,%2":"=r"(r):"f"(x),"f"(y)); t1 = rdc();
    printf("[CALIB] fcmp_d cycles=%lu\n", (unsigned long)(t1 - t0));
    t0 = rdc(); __asm__ volatile("fcvt.w.d %0,%1":"=r"(r):"f"(x)); t1 = rdc();
    printf("[CALIB] fcvt_wd cycles=%lu\n", (unsigned long)(t1 - t0));
    A[0] = (uint64_t)&A[1];
    volatile uint64_t *p = &A[0];
    t0 = rdc(); __asm__ volatile("ld %0,0(%1)":"=r"(r):"r"(p)); t1 = rdc();
    printf("[CALIB] load_l1 cycles=%lu\n", (unsigned long)(t1 - t0));
}

// Single-op vector measurement: warm up (prime vtype) then time one op.
#define VMEAS(name, sew, lmul, vl, insn)                                       \
    do {                                                                       \
        __asm__ volatile("vsetvli zero,%0," sew "," lmul ",ta,ma\n\t" insn     \
                         : : "r"((uint64_t)(vl)) : "memory");                  \
        uint64_t _t0 = rdc();                                                  \
        __asm__ volatile(insn : : : "memory");                                 \
        uint64_t _t1 = rdc();                                                  \
        printf("[CALIB] " name " cycles=%lu\n", (unsigned long)(_t1 - _t0));   \
    } while (0)

static void vector_ops(void) {
    VMEAS("vadd_m1", "e64", "m1", 16, "vadd.vv v8,v16,v24");
    VMEAS("vadd_m2", "e64", "m2", 16, "vadd.vv v8,v16,v24");
    VMEAS("vadd_m4", "e64", "m4", 16, "vadd.vv v8,v16,v24");
    VMEAS("vadd_m8", "e64", "m8", 16, "vadd.vv v8,v16,v24");
    VMEAS("vfadd_m1",  "e64", "m1", 16, "vfadd.vv v8,v16,v24");
    VMEAS("vfadd_m8",  "e64", "m8", 16, "vfadd.vv v8,v16,v24");
    VMEAS("vfmacc_m1", "e64", "m1", 16, "vfmacc.vv v8,v16,v24");
    VMEAS("vfmacc_m8", "e64", "m8", 16, "vfmacc.vv v8,v16,v24");
    VMEAS("vmul_m1", "e64", "m1", 16, "vmul.vv v8,v16,v24");
    VMEAS("vmul_m8", "e64", "m8", 16, "vmul.vv v8,v16,v24");
    VMEAS("vdiv_m1",  "e64", "m1", 16, "vdiv.vv v8,v16,v24");
    VMEAS("vfdiv_m1", "e64", "m1", 16, "vfdiv.vv v8,v16,v24");
    VMEAS("vredsum_m1", "e64", "m1", 16, "vredsum.vs v8,v16,v24");
    VMEAS("vredsum_m8", "e64", "m8", 16, "vredsum.vs v8,v16,v24");
    VMEAS("vfredusum_m1", "e64", "m1", 16, "vfredusum.vs v8,v16,v24");
    VMEAS("vrgather_m1", "e64", "m1", 16, "vrgather.vv v8,v16,v24");
    VMEAS("vrgather_m4", "e64", "m4", 16, "vrgather.vv v8,v16,v24");
    VMEAS("vslideup_m1", "e64", "m1", 16, "vslideup.vi v8,v16,1");
    VMEAS("vwadd_m1", "e32", "m1", 16, "vwadd.vv v8,v16,v24");
    VMEAS("vsetvli_m1", "e64", "m1", 16, "vadd.vv v8,v16,v24"); /* placeholder timed w/ vset */
}

int main(void) {
    for (int k = 0; k < 512; k++) A[k] = (uint64_t)(k * 2654435761u);
    printf("[CALIB] === XSTop vppp single-op correlation start ===\n");
    scalar_ops();
    vector_ops();
    printf("[CALIB] === done ===\n");
    return 0;
}
