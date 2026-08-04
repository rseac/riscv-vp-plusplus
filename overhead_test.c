#include <stdint.h>
#include <stdio.h>
static inline uint64_t rdcycle() {
    uint64_t c; asm volatile("fence; rdcycle %0" : "=r"(c)); return c;
}
int main() {
    uint64_t t0, t1;
    // Measure empty window
    asm volatile("fence"); t0 = rdcycle();
    asm volatile("fence"); t1 = rdcycle();
    printf("EMPTY: %ld\n", t1 - t0);
    
    // Measure with vsetvli only (in timing enabled, vsetvli static=0)
    t0 = rdcycle();
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma" : : "r"(16));
    asm volatile("fence");
    t1 = rdcycle();
    printf("VSETVLI_ONLY: %ld\n", t1 - t0);
    
    // Measure just vadd (vsetvli already done)
    asm volatile("vsetvli zero, %0, e32, m1, ta, ma" : : "r"(16));
    t0 = rdcycle();
    asm volatile("vadd.vv v1, v2, v3" ::: "v1");
    asm volatile("fence");
    t1 = rdcycle();
    printf("VADD_ONLY_CLEAN: %ld\n", t1 - t0);
    
    return 0;
}
