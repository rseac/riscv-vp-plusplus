#include <stdint.h>
#include <stdio.h>

static inline uint64_t read_rdcycle() {
    uint64_t cycles;
    asm volatile ("csrr %0, mcycle" : "=r"(cycles));
    return cycles;
}

void test_vadd_vl(int vl_val) {
    uint64_t start, end;
    uint64_t vl_set;

    asm volatile ("vsetvli %0, %1, e32, m8" : "=r"(vl_set) : "r"(vl_val));

    start = read_rdcycle();
    asm volatile ("vadd.vv v8, v0, v16");
    end = read_rdcycle();

    printf("VADD.VV (e32, m8): requested_vl=%d, actual_vl=%lu, cycles=%lu\n",
           vl_val, (unsigned long)vl_set, (unsigned long)(end - start));
}

void test_sew(int sew_code) {
    uint64_t start, end;
    uint64_t vl_set;

    if (sew_code == 8) {
        asm volatile ("vsetvli %0, %1, e8, m1" : "=r"(vl_set) : "r"(16));
        start = read_rdcycle();
        asm volatile ("vadd.vv v2, v0, v1");
        end = read_rdcycle();
        printf("VADD.VV (e8, m1): actual_vl=%lu, cycles=%lu\n", (unsigned long)vl_set, (unsigned long)(end - start));
    } else if (sew_code == 64) {
        asm volatile ("vsetvli %0, %1, e64, m1" : "=r"(vl_set) : "r"(16));
        start = read_rdcycle();
        asm volatile ("vadd.vv v2, v0, v1");
        end = read_rdcycle();
        printf("VADD.VV (e64, m1): actual_vl=%lu, cycles=%lu\n", (unsigned long)vl_set, (unsigned long)(end - start));
    }
}

void test_vdiv(int vl_val) {
    uint64_t start, end;
    uint64_t vl_set;

    asm volatile ("vsetvli %0, %1, e32, m1" : "=r"(vl_set) : "r"(vl_val));

    start = read_rdcycle();
    asm volatile ("vdiv.vv v2, v0, v1");
    end = read_rdcycle();

    printf("VDIV.VV (e32, m1): requested_vl=%d, actual_vl=%lu, cycles=%lu\n",
           vl_val, (unsigned long)vl_set, (unsigned long)(end - start));
}

void test_vred(int vl_val) {
    uint64_t start, end;
    uint64_t vl_set;

    asm volatile ("vsetvli %0, %1, e32, m1" : "=r"(vl_set) : "r"(vl_val));

    start = read_rdcycle();
    asm volatile ("vredsum.vs v2, v0, v1");
    end = read_rdcycle();

    printf("VREDSUM.VS (e32, m1): requested_vl=%d, actual_vl=%lu, cycles=%lu\n",
           vl_val, (unsigned long)vl_set, (unsigned long)(end - start));
}

int main() {
    printf("====================================================\n");
    printf("  RISC-V VP++ Ara Timing Model Comprehensive Verification\n");
    printf("====================================================\n");

    printf("\n--- Test 1: Dynamic Cycle Scaling with Vector Length (VL) ---\n");
    test_vadd_vl(4);
    test_vadd_vl(16);
    test_vadd_vl(64);
    test_vadd_vl(128);

    printf("\n--- Test 2: Element Width (SEW) Sensitivity ---\n");
    test_sew(8);
    test_sew(64);

    printf("\n--- Test 3: Complex Functional Units (VDIV & VRED) ---\n");
    test_vdiv(4);
    test_vdiv(16);
    test_vred(4);
    test_vred(16);

    printf("\n====================================================\n");
    printf("  All Tier 1 and Tier 2 Tests Executed Successfully!\n");
    printf("====================================================\n");

    return 0;
}
