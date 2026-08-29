#include "ara_timing.h"
#include <cmath>
#include <algorithm>

uint64_t AraTimingModel::computeCycles(const AraVecInsn& desc) {
    if (desc.fu == AraFU::UNKNOWN) return 0;
    if (desc.vl == 0) return 0;

    uint32_t ew = desc.ew ? desc.ew : 32;
    uint64_t vl = desc.vl;
    uint32_t nl = nr_lanes ? nr_lanes : 4;
    
    // Execution beats: B = ceil(VL * SEW / (64 * N_L))
    uint64_t beats = (vl * ew + (64 * nl - 1)) / (64 * nl);
    if (beats == 0) beats = 1;

    switch (desc.fu) {
        case AraFU::VALU:
            // Int ALU & BRU: L_1st = 2 cycles, T = 1 + B
            return 1 + beats;

        case AraFU::VMFPU_MUL:
            // Int MLU: L_1st = 3 cycles, T = 2 + B
            return 2 + beats;

        case AraFU::VMFPU_FMA:
            // FP FMA: L_1st = 3 cycles, T = 2 + B
            return 2 + beats;

        case AraFU::VDVU: {
            // Int DIV: T ~ 2 + (SEW + 2) * ceil(VL / N_L)
            uint64_t vl_per_lane = (vl + nl - 1) / nl;
            return 2 + (ew + 2) * vl_per_lane;
        }

        case AraFU::VLSU_UNIT:
            // Unit-stride load/store total cycles: T = 1 + tau_mem + c_sync + B
            return 1 + tau_mem + c_sync + beats;

        case AraFU::VLSU_STRIDED: {
            // Strided load/store: T = C_base_stride + round(VL * K_stride)
            uint64_t stride_cycles = (uint64_t)(vl * k_stride + 0.5);
            return c_base_stride + stride_cycles;
        }

        case AraFU::VLSU_GATHER: {
            // Gather (indexed): T = C_startup + round(VL * K_gather) + C_sync + C_drain
            uint64_t gather_cycles = (uint64_t)(vl * k_gather + 0.5);
            return c_startup_gather + gather_cycles + c_sync + c_drain;
        }

        case AraFU::VREDU_INT: {
            // Int Reduction: T = 1 + ceil(VL / (N_L * SIMD_w)) + 2 * ceil(log2(N_L))
            uint64_t simd_w = 64 / ew;
            if (simd_w == 0) simd_w = 1;
            uint64_t elem_beats = (vl + (nl * simd_w) - 1) / (nl * simd_w);
            uint64_t log_lanes = 0;
            while ((1U << log_lanes) < nl) log_lanes++;
            return 1 + elem_beats + 2 * log_lanes;
        }

        case AraFU::VREDU_FP: {
            // FP Reduction: T = 2 + 2 * ceil(VL / (N_L * SIMD_w)) + 3 * ceil(log2(N_L))
            uint64_t simd_w = 64 / ew;
            if (simd_w == 0) simd_w = 1;
            uint64_t elem_beats = (vl + (nl * simd_w) - 1) / (nl * simd_w);
            uint64_t log_lanes = 0;
            while ((1U << log_lanes) < nl) log_lanes++;
            return 2 + 2 * elem_beats + 3 * log_lanes;
        }

        default:
            return 1 + beats;
    }
}

uint64_t AraTimingModel::computePipelineOverhead(const AraVecInsn& desc) {
    if (desc.vl == 0) return 0;

    uint32_t ew = desc.ew ? desc.ew : 32;
    uint64_t vl = desc.vl;
    uint32_t nl = nr_lanes ? nr_lanes : 4;
    uint64_t beats = (vl * ew + (64 * nl - 1)) / (64 * nl);
    if (beats == 0) beats = 1;

    if (desc.fu == AraFU::VLSU_UNIT) {
        // Only fixed pipeline overhead for unit-stride load/store: 1 + c_sync + beats
        // (Memory latency tau_mem is inherently counted by the VP++ TLM transactions)
        return 1 + c_sync + beats;
    }

    return computeCycles(desc);
}

uint64_t AraTimingModel::computeIssueLatency(const AraVecInsn& desc) {
    // Dispatch issue latency synchronous to the scalar core
    return 1;
}
