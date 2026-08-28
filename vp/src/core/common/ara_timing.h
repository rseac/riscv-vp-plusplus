#pragma once

#include <stdint.h>
#include <algorithm>
#include "instr.h"

enum class AraFU {
    VALU,
    VMFPU_MUL,
    VMFPU_FMA,
    VDVU,
    VLSU_UNIT,
    VLSU_STRIDED,
    VLSU_GATHER,
    VREDU_INT,
    VREDU_FP,
    UNKNOWN
};

struct AraVecInsn {
    AraFU fu;
    uint64_t vl;
    uint32_t ew;
    uint32_t lmul;
    uint64_t stride;
};

class AraTimingModel {
private:
    uint32_t nr_lanes;
    uint32_t vlen;
    uint32_t tau_mem;
    uint32_t c_sync;

public:
    AraTimingModel(uint32_t nr_lanes, uint32_t vlen, uint32_t tau_mem, uint32_t c_sync)
        : nr_lanes(nr_lanes), vlen(vlen), tau_mem(tau_mem), c_sync(c_sync) {}

    uint64_t computeCycles(const AraVecInsn& desc);
    uint64_t computePipelineOverhead(const AraVecInsn& desc);
    uint64_t computeIssueLatency(const AraVecInsn& desc);
};
