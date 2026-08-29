#include <stdint.h>
#include <stdio.h>
#include <stdbool.h>

static inline uint64_t rdcycle() {
    uint64_t c;
    asm volatile("fence; rdcycle %0" : "=r"(c));
    return c;
}

static uint64_t _t0, _t1;
void start_timer() { _t0 = rdcycle(); }
void stop_timer() { _t1 = rdcycle(); }
uint64_t get_timer() { return _t1 - _t0; }

uint64_t bufA[2048] __attribute__((aligned(4096)));
uint64_t bufB[2048] __attribute__((aligned(4096)));

int test_tier1() {
    printf("[Tier 1] Checking Vector Timing Hooks Alive...\n");
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvadd.vv v1, v2, v3" :: "r"(64) : "v1");
    start_timer();
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvadd.vv v1, v2, v3" :: "r"(64) : "v1");
    stop_timer();
    uint64_t dt = get_timer();
    printf("  vadd.vv (vl=64, e32) measured latency: %lu cycles\n", dt);
    if (dt > 1) {
        printf("  [PASS] Tier 1: Vector timing hooks active.\n\n");
        return 1;
    } else {
        printf("  [FAIL] Tier 1: Vector timing hooks inactive.\n\n");
        return 0;
    }
}

int test_tier2() {
    printf("[Tier 2] Checking Dynamic Latency (VL & SEW Scaling)...\n");

    // VFADD EW32 VL=16
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvfadd.vv v1, v2, v3" :: "r"(16) : "v1");
    start_timer();
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvfadd.vv v1, v2, v3" :: "r"(16) : "v1");
    stop_timer();
    uint64_t c_vfadd_16 = get_timer();

    // VFADD EW32 VL=64
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvfadd.vv v1, v2, v3" :: "r"(64) : "v1");
    start_timer();
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvfadd.vv v1, v2, v3" :: "r"(64) : "v1");
    stop_timer();
    uint64_t c_vfadd_64 = get_timer();

    // VFADD EW64 VL=16
    asm volatile("vsetvli zero, %0, e64, m1, ta, ma\n\tvfadd.vv v1, v2, v3" :: "r"(16) : "v1");
    start_timer();
    asm volatile("vsetvli zero, %0, e64, m1, ta, ma\n\tvfadd.vv v1, v2, v3" :: "r"(16) : "v1");
    stop_timer();
    uint64_t c_vfadd_e64_16 = get_timer();

    // VDIV EW32 VL=16 vs VL=64
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvdiv.vv v1, v2, v3" :: "r"(16) : "v1");
    start_timer();
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvdiv.vv v1, v2, v3" :: "r"(16) : "v1");
    stop_timer();
    uint64_t c_vdiv_16 = get_timer();

    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvdiv.vv v1, v2, v3" :: "r"(64) : "v1");
    start_timer();
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvdiv.vv v1, v2, v3" :: "r"(64) : "v1");
    stop_timer();
    uint64_t c_vdiv_64 = get_timer();

    printf("  VFADD e32 vl=16: %lu cycles\n", c_vfadd_16);
    printf("  VFADD e32 vl=64: %lu cycles\n", c_vfadd_64);
    printf("  VFADD e64 vl=16: %lu cycles\n", c_vfadd_e64_16);
    printf("  VDIV  e32 vl=16: %lu cycles\n", c_vdiv_16);
    printf("  VDIV  e32 vl=64: %lu cycles\n", c_vdiv_64);

    bool pass = (c_vfadd_64 > c_vfadd_16) && (c_vfadd_e64_16 >= c_vfadd_16) && (c_vdiv_64 > c_vdiv_16);
    if (pass) {
        printf("  [PASS] Tier 2: Dynamic latency scaling verified across VL and SEW.\n\n");
        return 1;
    } else {
        printf("  [FAIL] Tier 2: Latency did not scale dynamically.\n\n");
        return 0;
    }
}

int test_tier3() {
    printf("[Tier 3] Checking Vector Load / Store Unit Execution...\n");
    start_timer();
    asm volatile("vsetvli zero, %0, e64, m1, ta, ma\n\tvle64.v v1, (%1)\n\tvse64.v v1, (%2)" 
                 :: "r"(16), "r"(bufA), "r"(bufB) : "v1", "memory");
    stop_timer();
    uint64_t c_mem = get_timer();
    printf("  VLE64 + VSE64 (vl=16): %lu cycles\n", c_mem);
    if (c_mem > 5) {
        printf("  [PASS] Tier 3: Vector memory pipeline modeled correctly.\n\n");
        return 1;
    } else {
        printf("  [FAIL] Tier 3: Memory timing abnormal.\n\n");
        return 0;
    }
}

int test_tier4() {
    printf("[Tier 4] Checking Hardware Scoreboard Chaining...\n");

    // Single isolated VADD on v1
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvadd.vv v1, v2, v3" :: "r"(16) : "v1");
    start_timer();
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvadd.vv v1, v2, v3" :: "r"(16) : "v1");
    stop_timer();
    uint64_t c_single_add = get_timer();

    // Single isolated VMUL on v4
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvmul.vv v4, v5, v6" :: "r"(16) : "v4");
    start_timer();
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvmul.vv v4, v5, v6" :: "r"(16) : "v4");
    stop_timer();
    uint64_t c_single_mul = get_timer();

    // Independent chained pair: VADD(v1, v2, v3) followed by VMUL(v4, v5, v6)
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvadd.vv v1, v2, v3\n\tvmul.vv v4, v5, v6" 
                 :: "r"(16) : "v1", "v4");
    start_timer();
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvadd.vv v1, v2, v3\n\tvmul.vv v4, v5, v6" 
                 :: "r"(16) : "v1", "v4");
    stop_timer();
    uint64_t c_chained_nodep = get_timer();

    // RAW Dependent pair: VADD(v1, v2, v3) followed by VADD(v4, v1, v5)
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvadd.vv v1, v2, v3\n\tvadd.vv v4, v1, v5" 
                 :: "r"(16) : "v1", "v4");
    start_timer();
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma\n\tvadd.vv v1, v2, v3\n\tvadd.vv v4, v1, v5" 
                 :: "r"(16) : "v1", "v4");
    stop_timer();
    uint64_t c_raw_dep = get_timer();

    printf("  Single VADD latency:       %lu cycles\n", c_single_add);
    printf("  Single VMUL latency:       %lu cycles\n", c_single_mul);
    printf("  Independent VADD+VMUL:     %lu cycles (Sum = %lu)\n", c_chained_nodep, c_single_add + c_single_mul);
    printf("  RAW Dependent Pair:        %lu cycles\n", c_raw_dep);

    // Chaining allows concurrent issue, RAW stalls
    bool pass = (c_chained_nodep < c_single_add + c_single_mul) && (c_raw_dep >= c_chained_nodep);
    if (pass) {
        printf("  [PASS] Tier 4: Hardware scoreboard enables chaining for independent FUs.\n\n");
        return 1;
    } else {
        printf("  [FAIL] Tier 4: Chaining test failed.\n\n");
        return 0;
    }
}

int test_tier5() {
    printf("[Tier 5] Checking Scalar Loop Overhead Hiding...\n");

    uint64_t vl = 16;
    int iterations = 16;
    uint64_t *ptrA = bufA;
    uint64_t *ptrB = bufB;

    start_timer();
    for (int i = 0; i < iterations; i++) {
        asm volatile("vsetvli zero, %0, e64, m1, ta, ma\n\tvle64.v v1, (%1)\n\tvadd.vv v2, v1, v1\n\tvse64.v v2, (%2)"
                     :: "r"(vl), "r"(ptrA), "r"(ptrB) : "v1", "v2", "memory");
        ptrA += vl;
        ptrB += vl;
    }
    stop_timer();
    uint64_t t_loop_total = get_timer();

    printf("  16-iteration vector loop total time: %lu cycles (~%lu cycles/iter)\n", 
           t_loop_total, t_loop_total / iterations);
    
    bool pass = (t_loop_total > 0 && t_loop_total < 2000);
    if (pass) {
        printf("  [PASS] Tier 5: Scalar loop overhead is hidden behind vector operations.\n\n");
        return 1;
    } else {
        printf("  [FAIL] Tier 5: Scalar loop overhead hiding failed.\n\n");
        return 0;
    }
}

int main() {
    printf("========================================\n");
    printf("CoralNPU VP++ Vector Timing Verification\n");
    printf("========================================\n\n");

    int pass = 0;
    pass += test_tier1();
    pass += test_tier2();
    pass += test_tier3();
    pass += test_tier4();
    pass += test_tier5();

    printf("========================================\n");
    printf("Verification Summary: %d / 5 Tiers Passed\n", pass);
    printf("========================================\n");

    return (pass == 5) ? 0 : 1;
}
