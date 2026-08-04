#include <stdint.h>
#include <stdio.h>

static inline uint64_t rdcycle() {
    uint64_t c; asm volatile("fence; rdcycle %0" : "=r"(c)); return c;
}

static uint64_t _t0, _t1;
void start_timer() { _t0 = rdcycle(); }
void stop_timer() { _t1 = rdcycle(); }
uint64_t get_timer() { return _t1 - _t0; }

uint64_t A[2048] __attribute__((aligned(4096)));
uint64_t B[2048] __attribute__((aligned(4096)));
uint64_t IDX[2048] __attribute__((aligned(4096)));

/* === FPU === */
void fpu_ew32_vl16() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(16):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(16):"v1");
    stop_timer(); printf("FPU EW32 VL16: %ld\n", get_timer());
}
void fpu_ew32_vl256() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(256):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(256):"v1");
    stop_timer(); printf("FPU EW32 VL256: %ld\n", get_timer());
}
void fpu_ew32_vl1024() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(1024):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(1024):"v1");
    stop_timer(); printf("FPU EW32 VL1024: %ld\n", get_timer());
}
void fpu_ew64_vl16() {
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(16):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(16):"v1");
    stop_timer(); printf("FPU EW64 VL16: %ld\n", get_timer());
}
void fpu_ew64_vl256() {
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(256):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(256):"v1");
    stop_timer(); printf("FPU EW64 VL256: %ld\n", get_timer());
}
void fpu_ew64_vl1024() {
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(1024):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvfadd.vv v1,v2,v3"::"r"(1024):"v1");
    stop_timer(); printf("FPU EW64 VL1024: %ld\n", get_timer());
}

/* === VLSU Strided === */
void vlsu_stride8_vl16() {
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(16),"r"(A),"r"((uint64_t)8),"r"(B):"v1","memory");
    start_timer();
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(16),"r"(A),"r"((uint64_t)8),"r"(B):"v1","memory");
    stop_timer(); printf("VLSU STRIDE8 VL16: %ld\n", get_timer());
}
void vlsu_stride8_vl256() {
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(256),"r"(A),"r"((uint64_t)8),"r"(B):"v1","memory");
    start_timer();
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(256),"r"(A),"r"((uint64_t)8),"r"(B):"v1","memory");
    stop_timer(); printf("VLSU STRIDE8 VL256: %ld\n", get_timer());
}
void vlsu_stride8_vl1024() {
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(1024),"r"(A),"r"((uint64_t)8),"r"(B):"v1","memory");
    start_timer();
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(1024),"r"(A),"r"((uint64_t)8),"r"(B):"v1","memory");
    stop_timer(); printf("VLSU STRIDE8 VL1024: %ld\n", get_timer());
}
void vlsu_stride64_vl16() {
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(16),"r"(A),"r"((uint64_t)64),"r"(B):"v1","memory");
    start_timer();
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(16),"r"(A),"r"((uint64_t)64),"r"(B):"v1","memory");
    stop_timer(); printf("VLSU STRIDE64 VL16: %ld\n", get_timer());
}
void vlsu_stride64_vl256() {
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(256),"r"(A),"r"((uint64_t)64),"r"(B):"v1","memory");
    start_timer();
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(256),"r"(A),"r"((uint64_t)64),"r"(B):"v1","memory");
    stop_timer(); printf("VLSU STRIDE64 VL256: %ld\n", get_timer());
}
void vlsu_stride64_vl1024() {
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(1024),"r"(A),"r"((uint64_t)64),"r"(B):"v1","memory");
    start_timer();
    asm volatile("vsetvli zero,%0,e64,m1,ta,ma\n\tvlse64.v v1,(%1),%2\n\tvsse64.v v1,(%3),%2"::"r"(1024),"r"(A),"r"((uint64_t)64),"r"(B):"v1","memory");
    stop_timer(); printf("VLSU STRIDE64 VL1024: %ld\n", get_timer());
}

/* === VALU VL16 === */
void valu_add_m1_vl16() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3"::"r"(16):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3"::"r"(16):"v1");
    stop_timer(); printf("VALU ADD M1 VL16: %ld\n", get_timer());
}
void valu_add_m2_vl16() {
    asm volatile("vsetvli zero,%0,e32,m2,ta,ma\n\tvadd.vv v2,v4,v6"::"r"(16):"v2","v3");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m2,ta,ma\n\tvadd.vv v2,v4,v6"::"r"(16):"v2","v3");
    stop_timer(); printf("VALU ADD M2 VL16: %ld\n", get_timer());
}
void valu_add_m8_vl16() {
    asm volatile("vsetvli zero,%0,e32,m8,ta,ma\n\tvadd.vv v8,v16,v24"::"r"(16):"v8","v9","v10","v11","v12","v13","v14","v15");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m8,ta,ma\n\tvadd.vv v8,v16,v24"::"r"(16):"v8","v9","v10","v11","v12","v13","v14","v15");
    stop_timer(); printf("VALU ADD M8 VL16: %ld\n", get_timer());
}
void valu_mul_vl16() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvmul.vv v1,v2,v3"::"r"(16):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvmul.vv v1,v2,v3"::"r"(16):"v1");
    stop_timer(); printf("VALU MUL VL16: %ld\n", get_timer());
}
void valu_div_vl16() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvdiv.vv v1,v2,v3"::"r"(16):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvdiv.vv v1,v2,v3"::"r"(16):"v1");
    stop_timer(); printf("VALU DIV VL16: %ld\n", get_timer());
}
void valu_redsum_vl16() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvredsum.vs v1,v2,v3"::"r"(16):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvredsum.vs v1,v2,v3"::"r"(16):"v1");
    stop_timer(); printf("VALU REDSUM VL16: %ld\n", get_timer());
}

/* === VALU VL256 === */
void valu_add_m1_vl256() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3"::"r"(256):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3"::"r"(256):"v1");
    stop_timer(); printf("VALU ADD M1 VL256: %ld\n", get_timer());
}
void valu_add_m2_vl256() {
    asm volatile("vsetvli zero,%0,e32,m2,ta,ma\n\tvadd.vv v2,v4,v6"::"r"(256):"v2","v3");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m2,ta,ma\n\tvadd.vv v2,v4,v6"::"r"(256):"v2","v3");
    stop_timer(); printf("VALU ADD M2 VL256: %ld\n", get_timer());
}
void valu_add_m8_vl256() {
    asm volatile("vsetvli zero,%0,e32,m8,ta,ma\n\tvadd.vv v8,v16,v24"::"r"(256):"v8","v9","v10","v11","v12","v13","v14","v15");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m8,ta,ma\n\tvadd.vv v8,v16,v24"::"r"(256):"v8","v9","v10","v11","v12","v13","v14","v15");
    stop_timer(); printf("VALU ADD M8 VL256: %ld\n", get_timer());
}
void valu_mul_vl256() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvmul.vv v1,v2,v3"::"r"(256):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvmul.vv v1,v2,v3"::"r"(256):"v1");
    stop_timer(); printf("VALU MUL VL256: %ld\n", get_timer());
}
void valu_div_vl256() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvdiv.vv v1,v2,v3"::"r"(256):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvdiv.vv v1,v2,v3"::"r"(256):"v1");
    stop_timer(); printf("VALU DIV VL256: %ld\n", get_timer());
}
void valu_redsum_vl256() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvredsum.vs v1,v2,v3"::"r"(256):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvredsum.vs v1,v2,v3"::"r"(256):"v1");
    stop_timer(); printf("VALU REDSUM VL256: %ld\n", get_timer());
}

/* === VALU VL1024 === */
void valu_add_m1_vl1024() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3"::"r"(1024):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3"::"r"(1024):"v1");
    stop_timer(); printf("VALU ADD M1 VL1024: %ld\n", get_timer());
}
void valu_add_m2_vl1024() {
    asm volatile("vsetvli zero,%0,e32,m2,ta,ma\n\tvadd.vv v2,v4,v6"::"r"(1024):"v2","v3");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m2,ta,ma\n\tvadd.vv v2,v4,v6"::"r"(1024):"v2","v3");
    stop_timer(); printf("VALU ADD M2 VL1024: %ld\n", get_timer());
}
void valu_mul_vl1024() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvmul.vv v1,v2,v3"::"r"(1024):"v1");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvmul.vv v1,v2,v3"::"r"(1024):"v1");
    stop_timer(); printf("VALU MUL VL1024: %ld\n", get_timer());
}

/* === Chaining & RAW VL16 === */
void chain_vadd_vmul_vl16() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3\n\tvmul.vv v4,v1,v5"::"r"(16):"v1","v4");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3\n\tvmul.vv v4,v1,v5"::"r"(16):"v1","v4");
    stop_timer(); printf("CHAINING VADD_VMUL VL16: %ld\n", get_timer());
}
void raw_alu_alu_vl16() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3\n\tvadd.vv v4,v1,v5"::"r"(16):"v1","v4");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3\n\tvadd.vv v4,v1,v5"::"r"(16):"v1","v4");
    stop_timer(); printf("RAW ALU_ALU VL16: %ld\n", get_timer());
}
void nodep_alu_alu_vl16() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3\n\tvadd.vv v4,v5,v6"::"r"(16):"v1","v4");
    start_timer();
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma\n\tvadd.vv v1,v2,v3\n\tvadd.vv v4,v5,v6"::"r"(16):"v1","v4");
    stop_timer(); printf("NODEP ALU_ALU VL16: %ld\n", get_timer());
}

/* === VADD Only (isolated) === */
void vadd_only_m1_vl16() {
    asm volatile("vsetvli zero,%0,e32,m1,ta,ma"::"r"(16));
    start_timer();
    asm volatile("vadd.vv v1,v2,v3":::"v1");
    stop_timer(); printf("VADD_ONLY M1 VL16: %ld\n", get_timer());
}

int main() {
    printf("=== AraXL Calibration Benchmark ===\n");
    for(int i=0;i<2048;i++) IDX[i]=(i*8)%2048;

    fpu_ew32_vl16();
    fpu_ew32_vl256();
    fpu_ew32_vl1024();
    fpu_ew64_vl16();
    fpu_ew64_vl256();
    fpu_ew64_vl1024();
    vlsu_stride8_vl16();
    vlsu_stride8_vl256();
    vlsu_stride8_vl1024();
    vlsu_stride64_vl16();
    vlsu_stride64_vl256();
    vlsu_stride64_vl1024();
    valu_add_m1_vl16();
    valu_add_m2_vl16();
    valu_add_m8_vl16();
    valu_mul_vl16();
    valu_div_vl16();
    valu_redsum_vl16();
    valu_add_m1_vl256();
    valu_add_m2_vl256();
    valu_add_m8_vl256();
    valu_mul_vl256();
    valu_div_vl256();
    valu_redsum_vl256();
    valu_add_m1_vl1024();
    valu_add_m2_vl1024();
    valu_mul_vl1024();
    chain_vadd_vmul_vl16();
    raw_alu_alu_vl16();
    nodep_alu_alu_vl16();
    vadd_only_m1_vl16();
    printf("=== Calibration Complete ===\n");
    return 0;
}
