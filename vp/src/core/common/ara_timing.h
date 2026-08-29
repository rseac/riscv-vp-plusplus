#pragma once

#include <stdint.h>
#include <algorithm>
#include <cmath>
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
    uint32_t c_base_stride;
    double k_stride;
    uint32_t c_startup_gather;
    double k_gather;
    uint32_t c_drain;

public:
    AraTimingModel(uint32_t nr_lanes = 4, uint32_t vlen = 2048, uint32_t tau_mem = 10, uint32_t c_sync = 2)
        : nr_lanes(nr_lanes), vlen(vlen), tau_mem(tau_mem), c_sync(c_sync) {
        if (this->nr_lanes == 0) this->nr_lanes = 4;
        c_base_stride = (this->nr_lanes >= 4) ? 38 : 33;
        k_stride = 5.0 / 3.0;
        c_startup_gather = 24 + this->nr_lanes / 2;
        k_gather = 1.0 + 1.0 / (double)this->nr_lanes;
        c_drain = 0;
    }

    uint32_t getNrLanes() const { return nr_lanes; }
    uint32_t getVlen() const { return vlen; }
    uint32_t getTauMem() const { return tau_mem; }
    uint32_t getCSync() const { return c_sync; }

    uint64_t computeCycles(const AraVecInsn& desc);
    uint64_t computePipelineOverhead(const AraVecInsn& desc);
    uint64_t computeIssueLatency(const AraVecInsn& desc);
};
