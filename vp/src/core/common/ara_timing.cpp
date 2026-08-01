/*
 * AraXL Vector Timing Model Implementation
 *
 * Implements the calibrated timing equations from math_model_approved.md (v3).
 * See the Summary of Key Equations (§6) for the formulas implemented here.
 *
 * IMPORTANT:
 * - Fixed calibrated constants (T_FLOOR, C_PE_SYNC, etc.) are hardcoded — they are structural.
 * - Tunable constants (tau_mem, c_harness, c_per_elem_gather, etc.) are exposed via AraConfig.
 */

#include "ara_timing.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ara_timing {

static uint32_t ilog2_ceil(uint32_t x) {
	if (x <= 1) return 0;
	uint32_t r = 0;
	x--;
	while (x > 0) {
		x >>= 1;
		r++;
	}
	return r;
}

static uint32_t ilog2_floor(uint32_t x) {
	if (x == 0) return 0;
	uint32_t r = 0;
	while (x > 1) {
		x >>= 1;
		r++;
	}
	return r;
}

AraTimingModel::AraTimingModel(const AraConfig& cfg) : cfg_(cfg) {
	log2_nr_clusters_ = ilog2_ceil(cfg_.nr_clusters);
}

uint32_t AraTimingModel::computeNBeats(uint32_t vl, uint32_t sew) const {
	// N_beats = ceil(VL * SEW / (64 * NrLanes))
	uint64_t numerator = (uint64_t)vl * sew;
	uint64_t denominator = 64ULL * cfg_.nr_lanes;
	return (uint32_t)((numerator + denominator - 1) / denominator);
}

uint32_t AraTimingModel::getLatMul(uint32_t sew) const {
	switch (sew) {
		case 8: return LAT_MUL_EW8;
		case 16: return LAT_MUL_EW16;
		case 32: return LAT_MUL_EW32;
		case 64: return LAT_MUL_EW64;
		default: return LAT_MUL_EW64;
	}
}

uint32_t AraTimingModel::getLatFP(uint32_t sew) const {
	switch (sew) {
		case 8: return LAT_FP_EW8;
		case 16: return LAT_FP_EW16;
		case 32: return LAT_FP_EW32;
		case 64: return LAT_FP_EW64;
		default: return LAT_FP_EW64;
	}
}

uint32_t AraTimingModel::getALUFloor() const {
	return (cfg_.nr_lanes >= 4) ? T_FLOOR_GE4L : T_FLOOR_2L;
}

uint32_t AraTimingModel::getALUFrontEnd() const {
	return (cfg_.nr_lanes >= 4) ? L_FE_ALU_GE4L : L_FE_ALU_2L;
}

double AraTimingModel::getGatherPerElem() const {
	return cfg_.c_per_elem_gather;
}

uint32_t AraTimingModel::getGatherStartupFloor() const {
	return cfg_.c_startup_floor_gather;
}

// ============================================================================
// Equation #1/#2: Integer ALU (VADD, VSUB, VAND, VOR, VSLL, etc.)
// HIGH confidence (2L), MEDIUM confidence (>=4L)
//
// 2L: T = 8 + N_beats
// >=4L: T = max(10 + N_beats, 18)
// ============================================================================
uint64_t AraTimingModel::computeALU(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	uint32_t l_fe = getALUFrontEnd();
	uint32_t t_floor = getALUFloor();
	return std::max(l_fe + n_beats, t_floor);
}

// ============================================================================
// Equation #3/#4: Integer Multiply (VMUL, VMULH, VSMUL)
// HIGH confidence
//
// T = ceil(log2(NrClusters)) + 8 + lambda_MUL(SEW) + N_beats
// ============================================================================
uint64_t AraTimingModel::computeMUL(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	uint32_t lat_mul = getLatMul(desc.sew);
	// Constant = ceil(log2(N_C)) + 8 + lambda_MUL
	return log2_nr_clusters_ + 8 + lat_mul + n_beats;
}

// ============================================================================
// Equation #6: FP FMA effective model (MEDIUM confidence)
//
// T_effective = max(N_beats, lambda_FP(SEW)) + C_FE_FPU(SEW)
//
// We use the MEDIUM-confidence effective model as it matches RTL behavior.
// The HIGH-confidence structural formula (T = log2(NC)+7+lambda+Nbeats)
// overpredicts due to pipeline parallelism in the VMFPU.
// ============================================================================
uint64_t AraTimingModel::computeFPFMA(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	uint32_t lat_fp = getLatFP(desc.sew);
	uint32_t c_fe_fpu;

	switch (desc.sew) {
		case 32: c_fe_fpu = cfg_.c_fe_fpu_ew32; break;
		case 64: c_fe_fpu = cfg_.c_fe_fpu_ew64; break;
		// For EW16/EW8, extrapolate from the pattern:
		// EW32=8, EW64=5. EW16 and EW8 have fewer SIMD conversion overhead.
		// Use structural formula as fallback for non-calibrated widths.
		case 16: c_fe_fpu = cfg_.c_fe_fpu_ew32; break;  // conservative
		case 8:  c_fe_fpu = cfg_.c_fe_fpu_ew32; break;  // conservative
		default: c_fe_fpu = cfg_.c_fe_fpu_ew64; break;
	}

	return std::max(n_beats, lat_fp) + c_fe_fpu;
}

// ============================================================================
// Equation #7: FP Non-Computational (VFMIN, VFMAX, VFSGNJ, VFCLASS, etc.)
// HIGH confidence
//
// T = ceil(log2(NrClusters)) + 8 + N_beats
// (lambda_NC=1 absorbed in constant)
// ============================================================================
uint64_t AraTimingModel::computeFPNonComp(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	return log2_nr_clusters_ + 8 + n_beats;
}

// ============================================================================
// Equation #8: FP Conversion (VFCVT*, VFWCVT*, VFNCVT*)
// HIGH confidence
//
// T = ceil(log2(NrClusters)) + 9 + N_beats
// (lambda_Conv=2 absorbed in constant)
// ============================================================================
uint64_t AraTimingModel::computeFPConv(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	return log2_nr_clusters_ + 9 + n_beats;
}

// ============================================================================
// Equation #9: Integer Division (VDIV, VDIVU, VREM, VREMU)
// HIGH confidence
//
// T = ceil(log2(NrClusters)) + 8 + N_beats * (64/SEW) * (SEW+2)
// Serial divider: iterates SEW+2 cycles per element
// ============================================================================
uint64_t AraTimingModel::computeIDIV(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	uint32_t elems_per_beat = 64 / desc.sew;
	uint32_t cycles_per_elem = desc.sew + 2;
	uint64_t div_cycles = (uint64_t)n_beats * elems_per_beat * cycles_per_elem;
	return log2_nr_clusters_ + 8 + div_cycles;
}

// ============================================================================
// Equation #10: Vector Load - Unit Stride (VLE)
// HIGH confidence
//
// T = 2*ceil(log2(NrClusters)) + 11 + T_mem + N_beats
// ============================================================================
uint64_t AraTimingModel::computeUnitLoad(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	return 2 * log2_nr_clusters_ + 11 + cfg_.tau_mem + n_beats;
}

// ============================================================================
// Equation #11: Vector Store - Unit Stride (VSE)
// HIGH confidence
//
// T = 2*ceil(log2(NrClusters)) + 8 + T_mem + N_beats
// ============================================================================
uint64_t AraTimingModel::computeUnitStore(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	return 2 * log2_nr_clusters_ + 8 + cfg_.tau_mem + n_beats;
}

// ============================================================================
// Equation #17: Strided Load (VLSE)
// MEDIUM confidence
//
// T = T_base_load + C_PE_strided(NL, VL)
// C_PE_strided(NL>=4, VL) = min(5, 4 + floor(VL_lane/32))
// C_PE_strided(NL=2) = 0
// ============================================================================
uint64_t AraTimingModel::computeStridedLoad(const AraVecInsn& desc) const {
	uint64_t t_base = computeUnitLoad(desc);

	if (cfg_.nr_lanes >= 4) {
		uint32_t vl_lane = (desc.vl + cfg_.nr_lanes - 1) / cfg_.nr_lanes;
		uint32_t c_pe = std::min(5u, 4 + vl_lane / 32);
		t_base += c_pe;
	}
	return t_base;
}

// ============================================================================
// Strided Store
// Same structure as strided load but with store base
// ============================================================================
uint64_t AraTimingModel::computeStridedStore(const AraVecInsn& desc) const {
	uint64_t t_base = computeUnitStore(desc);

	if (cfg_.nr_lanes >= 4) {
		uint32_t vl_lane = (desc.vl + cfg_.nr_lanes - 1) / cfg_.nr_lanes;
		uint32_t c_pe = std::min(5u, 4 + vl_lane / 32);
		t_base += c_pe;
	}
	return t_base;
}

// ============================================================================
// Equation #18: Gather / Indexed Load (VLUXEI, VLOXEI)
// LOW confidence
//
// T = C_harness + max(C_fixed + VL * C_per_elem(NL), C_startup_floor(NL))
//
// C_fixed = 15 (dispatch + operand delivery + sync + completion)
// C_per_elem: 3.0 (2L), 2.7 (4L), 2.5 (8L) — tunable
// C_startup_floor: 67 (2L), 61 (4L), 57 (8L) — tunable
// ============================================================================
uint64_t AraTimingModel::computeGather(const AraVecInsn& desc) const {
	double c_per_elem = getGatherPerElem();
	uint32_t c_floor = getGatherStartupFloor();

	double t_linear = C_GATHER_FIXED + desc.vl * c_per_elem;
	double t = std::max(t_linear, (double)c_floor);
	return (uint64_t)(t + cfg_.c_harness);
}

// ============================================================================
// Scatter (VSUXEI, VSOXEI)
// Use same model as gather (similar AXI serialization path)
// ============================================================================
uint64_t AraTimingModel::computeScatter(const AraVecInsn& desc) const {
	return computeGather(desc);
}

// ============================================================================
// Equation #13: Reduction Operations
// HIGH confidence
//
// T = ceil(log2(NC)) + 9 + N_beats + 3*ceil(log2(NL)) + (NC-1)*5 + 2 + log2(64/SEW)
// ============================================================================
uint64_t AraTimingModel::computeReduction(const AraVecInsn& desc, bool is_fp) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	uint32_t log2_nl = ilog2_ceil(cfg_.nr_lanes);
	uint32_t simd_fold = ilog2_floor(64 / desc.sew);

	uint64_t t = log2_nr_clusters_ + 9 + n_beats + 3 * log2_nl +
	             (cfg_.nr_clusters - 1) * 5 + 2 + simd_fold;
	return t;
}

// ============================================================================
// Equation #14: Slide Operations
// HIGH confidence
//
// T = ceil(log2(NC)) + 9 + N_beats + hops * 5 + 2
// hops <= ceil(NC/2) — for simplicity, use 1 hop (most common case)
// ============================================================================
uint64_t AraTimingModel::computeSlide(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	// Default: 1 hop (slideup/slidedown by small amount within cluster)
	uint32_t hops = 1;
	return log2_nr_clusters_ + 9 + n_beats + hops * 5 + 2;
}

// ============================================================================
// Equation #12: Narrowing Operations (VNSRL, VNSRA, VNCLIP)
// HIGH confidence
//
// T = ceil(log2(NC)) + 7 + 2 * N_beats_dst
// Narrowing: 2 input beats per output beat
// ============================================================================
uint64_t AraTimingModel::computeNarrow(const AraVecInsn& desc) const {
	// For narrowing, the destination SEW is half the source SEW
	// N_beats_dst uses the destination element width
	uint32_t dst_sew = desc.sew;  // desc.sew should already be dest SEW
	uint32_t n_beats_dst = computeNBeats(desc.vl, dst_sew);
	return log2_nr_clusters_ + 7 + 2 * n_beats_dst;
}

// ============================================================================
// Mask operations (VMAND, VMOR, VCPOP, VFIRST, etc.)
// Use ALU model as baseline — mask ops go through similar pipeline
// ============================================================================
uint64_t AraTimingModel::computeMask(const AraVecInsn& desc) const {
	// Mask ops process 1 bit per element, but still beat-based
	// Use SEW=8 for beat computation (1 element per byte per lane)
	uint32_t n_beats = computeNBeats(desc.vl, 8);
	uint32_t l_fe = getALUFrontEnd();
	uint32_t t_floor = getALUFloor();
	return std::max(l_fe + n_beats, t_floor);
}

// ============================================================================
// Main dispatch: computeCycles
// ============================================================================
uint64_t AraTimingModel::computeCycles(const AraVecInsn& desc) const {
	if (desc.vl == 0) return 1;  // vl=0: no-op, just dispatch overhead

	switch (desc.fu) {
		case AraFU::VALU:
			return computeALU(desc);

		case AraFU::VMFPU_MUL:
			return computeMUL(desc);

		case AraFU::VMFPU_FMA:
			return computeFPFMA(desc);

		case AraFU::VMFPU_FNONCOMP:
			return computeFPNonComp(desc);

		case AraFU::VMFPU_FCONV:
			return computeFPConv(desc);

		case AraFU::VMFPU_FDIV:
			// Use FP pipeline + serial divider approximation
			return computeIDIV(desc);

		case AraFU::VMFPU_IDIV:
			return computeIDIV(desc);

		case AraFU::VLSU_UNIT_LD:
			return computeUnitLoad(desc);

		case AraFU::VLSU_UNIT_ST:
			return computeUnitStore(desc);

		case AraFU::VLSU_STRIDED_LD:
			return computeStridedLoad(desc);

		case AraFU::VLSU_STRIDED_ST:
			return computeStridedStore(desc);

		case AraFU::VLSU_GATHER:
			return computeGather(desc);

		case AraFU::VLSU_SCATTER:
			return computeScatter(desc);

		case AraFU::VREDU_INT:
			return computeReduction(desc, false);

		case AraFU::VREDU_FP:
			return computeReduction(desc, true);

		case AraFU::VSLIDE:
			return computeSlide(desc);

		case AraFU::VNARROW:
			return computeNarrow(desc);

		case AraFU::VMASK:
			return computeMask(desc);

		case AraFU::VMV:
			// Scalar move: minimal latency, just front-end
			return getALUFrontEnd();

		case AraFU::VSETVL:
			// vsetvl is handled by scalar core, not accelerator
			return 1;

		case AraFU::VWHOLE_REG:
			// Whole register load/store: similar to unit-stride
			return computeUnitLoad(desc);

		case AraFU::UNKNOWN:
		default:
			// Fallback: use ALU model
			return computeALU(desc);
	}
}

// ============================================================================
// computePipelineOverhead: for memory ops when TLM provides its own latency
// Returns the pipeline overhead WITHOUT T_mem
// ============================================================================
uint64_t AraTimingModel::computePipelineOverhead(const AraVecInsn& desc) const {
	if (desc.vl == 0) return 1;

	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);

	switch (desc.fu) {
		case AraFU::VLSU_UNIT_LD:
			// Pipeline overhead = total - T_mem
			return 2 * log2_nr_clusters_ + 11 + n_beats;

		case AraFU::VLSU_UNIT_ST:
			return 2 * log2_nr_clusters_ + 8 + n_beats;

		case AraFU::VLSU_STRIDED_LD:
		case AraFU::VLSU_STRIDED_ST: {
			uint64_t base = (desc.fu == AraFU::VLSU_STRIDED_LD)
			                    ? (2 * log2_nr_clusters_ + 11 + n_beats)
			                    : (2 * log2_nr_clusters_ + 8 + n_beats);
			if (cfg_.nr_lanes >= 4) {
				uint32_t vl_lane = (desc.vl + cfg_.nr_lanes - 1) / cfg_.nr_lanes;
				base += std::min(5u, 4 + vl_lane / 32);
			}
			return base;
		}

		case AraFU::VLSU_GATHER:
		case AraFU::VLSU_SCATTER:
			// Gather/scatter overhead is dominated by per-element serialization
			// T_mem is already amortized into c_per_elem
			return computeGather(desc);

		default:
			return computeCycles(desc);
	}
}

}  // namespace ara_timing
