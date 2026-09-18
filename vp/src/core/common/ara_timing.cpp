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
#include <cstdlib>
#include <cstdio>

namespace ara_timing {

// ============================================================================
// VPPP_TIMING_DEBUG instrumentation: accumulates, per FU class, the total
// "cycles" (full latency, includes tau_mem/floor/etc.) vs "n_beats" (pure
// throughput/FU-occupancy component) returned by computeCycles(), so we can
// see empirically how much of a benchmark's total predicted time comes from
// fixed per-instruction latency terms (like tau_mem) vs streaming throughput.
// Enabled only when the VPPP_TIMING_DEBUG env var is set; zero overhead/
// behavior change otherwise. Dumped to stderr at process exit.
// ============================================================================
namespace {
struct FuClassStats {
	uint64_t count = 0;
	uint64_t sum_total_cycles = 0;
	uint64_t sum_n_beats = 0;
	uint64_t lookup_hit_count = 0;
	uint64_t lookup_hit_cycles = 0;
};
// vl-bucket histogram, specifically for VMFPU_FMA (the bucket the lookupRTL
// table keys on: <=16, <=256, <=1024, >1024), split by whether lmul_num==1
// (lookupRTL's exact-match condition) so we can see whether a benchmark's
// FMA traffic is even eligible for the lookup table at all, and if so which
// bucket it lands in - vl256_/vl1024_ are still uncalibrated guesses.
struct FmaVlHisto {
	uint64_t count[4] = {0, 0, 0, 0};       // lmul==1 eligible
	uint64_t count_other_lmul[4] = {0, 0, 0, 0}; // lmul!=1, never hits lookupRTL
	uint64_t cycles[4] = {0, 0, 0, 0};
};
struct DebugStats {
	bool enabled = false;
	FuClassStats by_class[4]; // 0=ALU, 1=FPU, 2=LSU, 3=OTHER
	FuClassStats by_fu[23];   // fine-grained, indexed by (uint8_t)AraFU
	FmaVlHisto fma_histo;
	DebugStats() { enabled = (std::getenv("VPPP_TIMING_DEBUG") != nullptr); }
	~DebugStats() {
		if (!enabled) return;
		static const char* fu_names[23] = {
			"VALU", "VMFPU_MUL", "VMFPU_FMA", "VMFPU_FNONCOMP", "VMFPU_FCONV",
			"VMFPU_FDIV", "VMFPU_FSQRT", "VMFPU_IDIV", "VLSU_UNIT_LD", "VLSU_UNIT_ST",
			"VLSU_STRIDED_LD", "VLSU_STRIDED_ST", "VLSU_GATHER", "VLSU_SCATTER",
			"VREDU_INT", "VREDU_FP", "VSLIDE", "VNARROW", "VMASK", "VMV",
			"VSETVL", "VWHOLE_REG", "UNKNOWN"
		};
		fprintf(stderr, "\n[VPPP_TIMING_DEBUG] Fine-grained per-AraFU breakdown:\n");
		for (int i = 0; i < 23; i++) {
			const auto& s = by_fu[i];
			if (s.count == 0) continue;
			fprintf(stderr,
			        "  %-16s count=%8llu  sum_total_cycles=%10llu  avg=%8.2f  lookupRTL_hits=%8llu\n",
			        fu_names[i], (unsigned long long)s.count, (unsigned long long)s.sum_total_cycles,
			        (double)s.sum_total_cycles / (double)s.count, (unsigned long long)s.lookup_hit_count);
		}
		static const char* names[4] = {"ALU", "FPU", "LSU", "OTHER"};
		fprintf(stderr, "\n[VPPP_TIMING_DEBUG] Per-FU-class accumulated cost breakdown:\n");
		uint64_t grand_total = 0, grand_beats = 0;
		for (int i = 0; i < 4; i++) {
			const auto& s = by_class[i];
			grand_total += s.sum_total_cycles;
			grand_beats += s.sum_n_beats;
			fprintf(stderr,
			        "  %-6s count=%8llu  sum_total_cycles=%10llu  sum_n_beats=%10llu  "
			        "latency_overhead(total-beats)=%10llu  lookupRTL_hits=%8llu(%10llu cyc)\n",
			        names[i], (unsigned long long)s.count,
			        (unsigned long long)s.sum_total_cycles, (unsigned long long)s.sum_n_beats,
			        (unsigned long long)(s.sum_total_cycles - s.sum_n_beats),
			        (unsigned long long)s.lookup_hit_count, (unsigned long long)s.lookup_hit_cycles);
		}
		fprintf(stderr, "  %-6s count=%8s  sum_total_cycles=%10llu  sum_n_beats=%10llu  "
		        "latency_overhead(total-beats)=%10llu\n",
		        "TOTAL", "-", (unsigned long long)grand_total, (unsigned long long)grand_beats,
		        (unsigned long long)(grand_total - grand_beats));

		static const char* buckets[4] = {"vl<=16", "vl<=256", "vl<=1024", "vl>1024"};
		fprintf(stderr, "[VPPP_TIMING_DEBUG] VMFPU_FMA vl-bucket histogram:\n");
		for (int i = 0; i < 4; i++) {
			fprintf(stderr,
			        "  %-9s lmul1_count=%8llu  cycles=%10llu  other_lmul_count=%8llu\n",
			        buckets[i], (unsigned long long)fma_histo.count[i],
			        (unsigned long long)fma_histo.cycles[i],
			        (unsigned long long)fma_histo.count_other_lmul[i]);
		}
	}
};
DebugStats g_debug_stats;

int fmaVlBucket(uint32_t vl) {
	if (vl <= 16) return 0;
	if (vl <= 256) return 1;
	if (vl <= 1024) return 2;
	return 3;
}

int fuClassIndex(AraFU fu) {
	switch (fu) {
		case AraFU::VLSU_UNIT_LD:
		case AraFU::VLSU_UNIT_ST:
		case AraFU::VLSU_STRIDED_LD:
		case AraFU::VLSU_STRIDED_ST:
		case AraFU::VLSU_GATHER:
		case AraFU::VLSU_SCATTER:
		case AraFU::VWHOLE_REG:
			return 2; // LSU
		case AraFU::VMFPU_MUL:
		case AraFU::VMFPU_FMA:
		case AraFU::VMFPU_FNONCOMP:
		case AraFU::VMFPU_FCONV:
		case AraFU::VMFPU_FDIV:
		case AraFU::VMFPU_FSQRT:
			return 1; // FPU
		case AraFU::VALU:
		case AraFU::VMFPU_IDIV:
		case AraFU::VREDU_INT:
		case AraFU::VREDU_FP:
		case AraFU::VSLIDE:
		case AraFU::VMASK:
		case AraFU::VNARROW:
		case AraFU::VMV:
			return 0; // ALU
		default:
			return 3; // OTHER
	}
}
} // namespace

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
	
	// Populate RTL calibration values based on NrLanes and VLEN
	// Source: build/AraXL/reports/rtl_calibration.md
	// These values represent the TOTAL rdcycle delta for the calibration benchmark
	// (includes vsetvli + vector_op + measurement overhead as seen in RTL).
	
	uint32_t nl = cfg_.nr_lanes;
	uint32_t vl = cfg_.vlen;
	
	// Helper: lane-dependent base offset (4L/8L add +4 over 2L for most ops)
	uint32_t lane_offset = (nl >= 4) ? 4 : 0;
	
	// FPU EW32 — RTL calibration data
	// Recalibrated 2026-09 against a real dependent mul->madd->sub RTL sweep
	// (benchmark_suite/ara/apps/ubench_fpu_chain_hazards, SEW32/LMUL1,
	// vl<=16, all 9 lane/VLEN configs). The previous "2L=12, >=4L=28" jump
	// was NOT present in real RTL: a fully dependent FMA chain is
	// latency-bound (each op waits for the last), so lane count - which adds
	// data-parallel width, not per-op latency - has no measurable effect.
	// Measured per-instruction cost was flat at ~9.0-9.6 cycles across every
	// lane count and VLEN tested (mean 9.05); replacing the previous
	// nl/vlen-branchy guess with that flat value fixes the ~2x ROI
	// overestimate this caused for FMA-chain-heavy kernels (e.g.
	// blackscholes) at >=4 lanes. vl256_/vl1024_ buckets are untouched -
	// no calibration data for those yet.
	rtl_fpu_ew32_vl16_ = 9;

	rtl_fpu_ew32_vl256_ = (nl == 2) ? 25 : 29;
	rtl_fpu_ew32_vl1024_ = (nl == 2) ? 26 : 30;

	// FPU EW64 — same recalibration, same sweep (SEW64/LMUL1, vl<=16).
	// Measured per-instruction cost flat at ~9.1-11.4 cycles across every
	// lane count/VLEN (mean 10.48), replacing the previous "2L=13, 4L=13-20,
	// 8L=13-26" branchy guess.
	rtl_fpu_ew64_vl16_ = 10;
	rtl_fpu_ew64_vl256_ = (nl == 2) ? 24 : 28;
	rtl_fpu_ew64_vl1024_ = (nl == 2) ? 25 : 29;
	
	// VALU ADD (pure vector instruction cost, WITHOUT vsetvli overhead)
	// RTL measures vsetvli+vadd; subtract vsetvli cost (7 for 2L, 11 for >=4L)
	uint32_t vsetvli_cost = (nl >= 4) ? 11 : 7;
	rtl_valu_add_m1_vl16_ = 23 + lane_offset - vsetvli_cost;
	rtl_valu_add_m1_vl256_ = 26 + lane_offset - vsetvli_cost;
	rtl_valu_add_m1_vl1024_ = 25 + lane_offset - vsetvli_cost;
	
	rtl_valu_add_m2_vl16_ = 25 + lane_offset - vsetvli_cost;
	rtl_valu_add_m2_vl256_ = (nl == 2) ? 19 - vsetvli_cost : 21 - vsetvli_cost;
	rtl_valu_add_m2_vl1024_ = (nl == 2) ? 18 - vsetvli_cost : 20 - vsetvli_cost;
	
	rtl_valu_add_m8_vl16_ = ((nl == 2) ? 18 : 20) - vsetvli_cost;
	rtl_valu_add_m8_vl256_ = 24 + lane_offset - vsetvli_cost;
	
	// VALU MUL
	rtl_valu_mul_vl16_ = 24 + lane_offset - vsetvli_cost;
	rtl_valu_mul_vl256_ = 25 + lane_offset - vsetvli_cost;
	rtl_valu_mul_vl1024_ = 24 + lane_offset - vsetvli_cost;
	
	// VALU DIV
	rtl_valu_div_vl16_ = 25 + lane_offset - vsetvli_cost;
	rtl_valu_div_vl256_ = 26 + lane_offset - vsetvli_cost;
	
	// VALU REDSUM
	rtl_valu_redsum_vl16_ = ((nl == 2) ? 19 : 21) - vsetvli_cost;
	rtl_valu_redsum_vl256_ = ((nl == 2) ? 19 : 21) - vsetvli_cost;
	
	// VLSU STRIDE (pair cost: load + store together)
	// VL16: 2L=85, 4L=89, 8L=89
	rtl_vlsu_stride_vl16_ = (nl == 2) ? 85 : 89;
	
	// VL256: depends heavily on VLEN (determines actual VL after clamping)
	if (nl == 2) {
		if (vl <= 2048) rtl_vlsu_stride_vl256_ = 140;
		else if (vl <= 4096) rtl_vlsu_stride_vl256_ = 247;
		else rtl_vlsu_stride_vl256_ = 460;
	} else {
		if (vl <= 2048) rtl_vlsu_stride_vl256_ = 145;
		else if (vl <= 4096) rtl_vlsu_stride_vl256_ = 251;
		else rtl_vlsu_stride_vl256_ = 465;
	}
	
	if (nl == 2) {
		if (vl <= 2048) rtl_vlsu_stride_vl1024_ = 138;
		else if (vl <= 4096) rtl_vlsu_stride_vl1024_ = 245;
		else rtl_vlsu_stride_vl1024_ = 458;
	} else {
		if (vl <= 2048) rtl_vlsu_stride_vl1024_ = 143;
		else if (vl <= 4096) rtl_vlsu_stride_vl1024_ = 249;
		else rtl_vlsu_stride_vl1024_ = 463;
	}
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
// FP Divide (VFDIV, VFRDIV) - real hardware divider, NOT integer division
//
// Calibrated 2026-09 against ubench_fdiv_chain_hazards (dependent vfdiv.vv
// chain, SEW32/64, LMUL1, RTL sweep across all 9 lane/VLEN configs). Cost
// was flat ~7 (SEW32) / ~9 (SEW64) cycles/instruction across every lane
// count and VLEN tested - like the FMA chain, a fully dependent divide
// chain is latency-bound, not lane-width-bound, so lanes don't help it.
// Previously this FU was routed through computeIDIV's serial-integer-
// divider formula (273-537 cycles/instr for real benchmarks doing FP
// division, e.g. somier/swaptions) - a ~30-60x overestimate.
// ============================================================================
uint64_t AraTimingModel::computeFPDiv(const AraVecInsn& desc) const {
	return (desc.sew == 64) ? 9 : 7;
}

// ============================================================================
// FP Sqrt (VFSQRT)
//
// Calibrated 2026-09 against ubench_fdiv_chain_hazards (dependent
// vfsqrt.v chain). SEW32 behaves like division (~7 cycles/instr, flat
// across lanes/VLEN). SEW64 is markedly more expensive (16-56
// cycles/instr) and, unusually among everything calibrated so far, DOES
// vary with lane count and VLEN - real double-precision sqrt is not fully
// pipelined the same way division is. We use the mean (34) as a flat
// approximation for now; this is a known-imprecise placeholder pending a
// dedicated lane/VLEN-resolved sqrt sweep, but is still a large
// improvement over the previous serial-integer-divider formula.
// ============================================================================
uint64_t AraTimingModel::computeFPSqrt(const AraVecInsn& desc) const {
	return (desc.sew == 64) ? 34 : 7;
}

// ============================================================================
// Equation #10: Vector Load - Unit Stride (VLE)
// HIGH confidence
//
// T = 2*ceil(log2(NrClusters)) + 11 + T_mem + N_beats
// ============================================================================
uint64_t AraTimingModel::computeUnitLoad(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	// Recalibrated 2026-09 against real Ara RTL (ubench_mem_hazards sweep,
	// benchmark_suite/ara/apps/ubench_mem_hazards): replaces the guessed
	// "11 + tau_mem(=10)" fixed cost with the measured ~14 cycles/instruction,
	// and doubles the per-beat cost - real sustained bandwidth is ~half of
	// the ideal 64-bits/lane/cycle this formula previously assumed.
	return 2 * log2_nr_clusters_ + 14 + 2 * n_beats;
}

// ============================================================================
// Equation #11: Vector Store - Unit Stride (VSE)
// HIGH confidence
//
// T = 2*ceil(log2(NrClusters)) + 8 + T_mem + N_beats
// ============================================================================
uint64_t AraTimingModel::computeUnitStore(const AraVecInsn& desc) const {
	uint32_t n_beats = computeNBeats(desc.vl, desc.sew);
	// Recalibrated 2026-09: stores have near-zero fixed dispatch cost in RTL
	// (no destination register/response to wait on), vs the previous
	// "8 + tau_mem(=10)" guess. Beat cost doubled for the same reason as
	// computeUnitLoad.
	return 2 * log2_nr_clusters_ + 1 + 2 * n_beats;
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
// RECALIBRATED 2026-09 (was LOW confidence) -- root-caused via spmv's
// consistent ~40% overestimate across all 9 configs.
//
// T = C_harness + max(C_fixed + VL * C_per_elem, C_startup_floor)
//
// Real Ara RTL (addrgen.sv): indexed-load address generation is a serial
// state machine that consumes one element index per accepted cycle
// regardless of NrLanes ("Ara stalls on an indexed memory operation",
// addrgen.sv ~line 439) -- confirmed empirically via ubench_gather_isolated
// (32 independent vluxei64.v, e64), which measured BYTE-IDENTICAL cycle
// counts at 2/4/8 lanes (462/550/809/1321 @ vl=2/4/8/16). The previous
// formula assumed lane-dependent per-elem/floor constants that were never
// actually wired up per-lane in any of the project's config JSONs (every
// config shipped the same flat 3.0/67 values) -- a structural assumption
// that was simply wrong, not a mistuned constant. The 67-cycle floor also
// never applies in practice: real per-instruction cost at vl=2 is ~14
// cycles, not 63-73. Fit: ~9.16 + 2.0*vl per instruction at any lane count
// (C_fixed=3 + c_harness=6 = 9, c_per_elem=2.0, floor=0/disabled).
// computeScatter() shares this formula; no RiVEC benchmark exercises
// scatter, so that side is untested by this fix, not just unaffected.
// Validated so far only at VLEN=1024 -- flagged for a VLEN spot-check.
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
//
// FIX 2026-09: this flat ALU-baseline formula was never independently
// calibrated, and real Ara RTL (masku.sv) structurally contradicts it for
// the "scan-class" subset (vcpop, vfirst, viota, vid, vmsbf, vmsif, vmsof):
// these are explicitly time-multiplexed in hardware --
//   // Execution time example for vcpop.m (similar for vfirst.m):
//   // t_vcpop.m = VLEN/VcpopParallelism
//   localparam int VcpopParallelism = 16;
// -- a real, documented multi-cycle mechanism, not a parallel elementwise
// op. Confirmed via an isolated RTL probe reproducing particlefilter's
// actual compare->cpop->first->iota chain (the dominant driver of its
// VMASK cost): a compare-only variant matched RTL almost exactly (-1.6%),
// but the full chain including the 3 scan ops underestimated by -33%,
// isolating the entire gap to this scan-class formula. The simple
// bitwise mask-logic ops (vmand/vmor/etc.) are NOT part of masku.sv's
// time-multiplexing scheme and keep the original ALU-baseline formula.
// ============================================================================
uint64_t AraTimingModel::computeMask(const AraVecInsn& desc) const {
	if (desc.is_mask_scan) {
		uint32_t parallelism = cfg_.mask_scan_parallelism ? cfg_.mask_scan_parallelism : 16;
		uint32_t slice_cycles = (desc.vl + parallelism - 1) / parallelism;  // ceil(vl / parallelism)
		return cfg_.mask_scan_fixed + slice_cycles;
	}
	// Mask ops process 1 bit per element, but still beat-based
	// Use SEW=8 for beat computation (1 element per byte per lane)
	uint32_t n_beats = computeNBeats(desc.vl, 8);
	uint32_t l_fe = getALUFrontEnd();
	uint32_t t_floor = getALUFloor();
	return std::max(l_fe + n_beats, t_floor);
}

// ============================================================================
// RTL Calibration Lookup: exact match for calibrated (FU, SEW, LMUL, VL) tuples
// Returns 0 if no calibrated value exists (caller uses analytical model)
// ============================================================================
uint64_t AraTimingModel::lookupRTL(const AraVecInsn& desc) const {
	// Compute effective VL after VLMAX clamping
	uint32_t vlmax = cfg_.vlen * desc.lmul_num / (desc.sew * desc.lmul_den);
	uint32_t eff_vl = std::min(desc.vl, vlmax);
	
	// Encode (FU, SEW, LMUL_num, LMUL_den, eff_VL) as a compact key
	// We use the raw VL request (before clamping) to match the benchmark's
	// requested VL, since the RTL table is indexed by requested VL
	uint32_t req_vl = desc.vl;
	
	// FPU FMA (vfadd.vv) — e32/e64 m1
	if (desc.fu == AraFU::VMFPU_FMA && desc.lmul_num == 1 && desc.lmul_den == 1) {
		if (desc.sew == 32) {
			if (req_vl <= 16) return rtl_fpu_ew32_vl16_;
			if (req_vl <= 256) return rtl_fpu_ew32_vl256_;
			return rtl_fpu_ew32_vl1024_;
		}
		if (desc.sew == 64) {
			if (req_vl <= 16) return rtl_fpu_ew64_vl16_;
			if (req_vl <= 256) return rtl_fpu_ew64_vl256_;
			return rtl_fpu_ew64_vl1024_;
		}
	}
	
	// VALU (vadd.vv) — e32
	if (desc.fu == AraFU::VALU && desc.sew == 32) {
		if (desc.lmul_num == 1 && desc.lmul_den == 1) {
			if (req_vl <= 16) return rtl_valu_add_m1_vl16_;
			if (req_vl <= 256) return rtl_valu_add_m1_vl256_;
			return rtl_valu_add_m1_vl1024_;
		}
		if (desc.lmul_num == 2 && desc.lmul_den == 1) {
			if (req_vl <= 16) return rtl_valu_add_m2_vl16_;
			if (req_vl <= 256) return rtl_valu_add_m2_vl256_;
			return rtl_valu_add_m2_vl1024_;
		}
		if (desc.lmul_num == 8 && desc.lmul_den == 1) {
			if (req_vl <= 16) return rtl_valu_add_m8_vl16_;
			if (req_vl <= 256) return rtl_valu_add_m8_vl256_;
			return 0;  // no calibration for M8 VL1024
		}
	}
	
	// VMFPU_MUL (vmul.vv) — e32 m1
	if (desc.fu == AraFU::VMFPU_MUL && desc.sew == 32 && desc.lmul_num == 1) {
		if (req_vl <= 16) return rtl_valu_mul_vl16_;
		if (req_vl <= 256) return rtl_valu_mul_vl256_;
		return rtl_valu_mul_vl1024_;
	}
	
	// VMFPU_IDIV (vdiv.vv) — e32 m1
	if (desc.fu == AraFU::VMFPU_IDIV && desc.sew == 32 && desc.lmul_num == 1) {
		if (req_vl <= 16) return rtl_valu_div_vl16_;
		if (req_vl <= 256) return rtl_valu_div_vl256_;
		return 0;
	}
	
	// VREDU_INT (vredsum.vs) — e32 m1
	if (desc.fu == AraFU::VREDU_INT && desc.sew == 32 && desc.lmul_num == 1) {
		if (req_vl <= 16) return rtl_valu_redsum_vl16_;
		if (req_vl <= 256) return rtl_valu_redsum_vl256_;
		return 0;
	}
	
	// VLSU strided loads/stores — e64 m1
	// The RTL measures PAIR (vlse + vsse). Split: load gets ceil(pair/2),
	// store gets floor(pair/2). This ensures load+store = pair_cost exactly.
	if ((desc.fu == AraFU::VLSU_STRIDED_LD || desc.fu == AraFU::VLSU_STRIDED_ST) &&
	    desc.sew == 64 && desc.lmul_num == 1) {
		uint32_t pair_cost;
		if (req_vl <= 16) pair_cost = rtl_vlsu_stride_vl16_;
		else if (req_vl <= 256) pair_cost = rtl_vlsu_stride_vl256_;
		else pair_cost = rtl_vlsu_stride_vl1024_;
		
		if (desc.fu == AraFU::VLSU_STRIDED_LD)
			return (pair_cost + 1) / 2;
		else
			return pair_cost / 2;
	}
	
	return 0;  // No calibration match
}

// ============================================================================
// Main dispatch: computeCycles
// ============================================================================
AraInstLatency AraTimingModel::computeCycles(const AraVecInsn& desc) const {
	AraInstLatency result;
	uint64_t total = 0;
	if (desc.vl == 0) return {1, 1};  // vl=0: no-op, just dispatch overhead
	
	// Try RTL lookup first for exact calibration
	uint64_t rtl_val = lookupRTL(desc);
	if (rtl_val > 0) {
		// FIX 2026-09: lookupRTL() hits used to return {rtl_val, rtl_val} --
		// the exact same conflation-of-latency-and-occupancy bug found and
		// fixed for LSU loads (see v.h). rtl_val is a genuine dependent-CHAIN
		// latency (measured from a fully-serial mul->madd->sub-style RTL
		// probe), correct for total_cycles (gates a real dependent consumer),
		// but wrong as the structural occupancy that gates back-to-back
		// INDEPENDENT reissue of the same FU. Confirmed directly: lavamd's
		// kernel_vec accumulates 4 independent vfmacc.vf destinations
		// (xfA_v/x/y/z) per inner loop -- a real RTL probe of exactly that
		// shape (SEW32/LMUL1/vl=32, 8 lanes) measured 3.64 cycles/instr
		// back-to-back, not the 29-cycle chain latency the lookup table was
		// also being charged as occupancy for (1024 such instances alone
		// summed to 29,696 cycles -- already exceeding lavamd's entire
		// RTL ground truth of 25,517).
		//
		// Plain n_beats alone (the occupancy the non-lookup path already
		// uses) was tried first and UNDER-shoots: a dedicated independent-
		// FMA RTL sweep (SEW32/LMUL1/vl=32, ubench_fma_lmul1_indep) across
		// lane counts measured 8.08/4.63/3.78 cycles/instr at 2/4/8 lanes
		// against n_beats of 8/4/2 -- a real, non-beat-proportional fixed
		// per-instruction dispatch cost that plain n_beats misses entirely,
		// and which dominates at small vl (blackscholes's CNDF chain, at
		// vl=8, hits n_beats=1 -- an even more extreme case -- and using
		// plain n_beats there regressed blackscholes from -unknown to
		// -45%, confirming the same undershoot). Linear fit across the
		// three lane counts: occupancy ~= 0.74*n_beats + 2.06; approximated
		// here as n_beats + 2 for simplicity. Still a coarse, one-probe
		// approximation -- flagged for follow-up recalibration with more
		// data points, same as the AraXL sqrt SEW64 case.
		uint32_t occ_n_beats = computeNBeats(desc.vl, desc.sew) + 2;
		if (g_debug_stats.enabled) {
			int idx = fuClassIndex(desc.fu);
			auto& s = g_debug_stats.by_class[idx];
			s.count++;
			s.sum_total_cycles += rtl_val;
			s.sum_n_beats += occ_n_beats;
			s.lookup_hit_count++;
			s.lookup_hit_cycles += rtl_val;
			auto& fs = g_debug_stats.by_fu[(int)(uint8_t)desc.fu];
			fs.count++;
			fs.sum_total_cycles += rtl_val;
			fs.lookup_hit_count++;
			if (desc.fu == AraFU::VMFPU_FMA && desc.lmul_num == 1 && desc.lmul_den == 1) {
				int b = fmaVlBucket(desc.vl);
				g_debug_stats.fma_histo.count[b]++;
				g_debug_stats.fma_histo.cycles[b] += rtl_val;
			}
		}
		return {rtl_val, occ_n_beats};
	}
	if (g_debug_stats.enabled && desc.fu == AraFU::VMFPU_FMA &&
	    !(desc.lmul_num == 1 && desc.lmul_den == 1)) {
		int b = fmaVlBucket(desc.vl);
		g_debug_stats.fma_histo.count_other_lmul[b]++;
	}

	switch (desc.fu) {
		case AraFU::VALU:
			total = computeALU(desc); break;

		case AraFU::VMFPU_MUL:
			total = computeMUL(desc); break;

		case AraFU::VMFPU_FMA:
			total = computeFPFMA(desc); break;

		case AraFU::VMFPU_FNONCOMP:
			total = computeFPNonComp(desc); break;

		case AraFU::VMFPU_FCONV:
			total = computeFPConv(desc); break;

		case AraFU::VMFPU_FDIV:
			// Recalibrated 2026-09 (ubench_fdiv_chain_hazards): real FP
			// division has a dedicated pipelined unit, NOT the serial
			// bit-by-bit integer divider this used to reuse via computeIDIV
			// (which was costing 273-537 cycles/instr for real benchmarks
			// like somier/swaptions that do genuine FP division).
			total = computeFPDiv(desc); break;

		case AraFU::VMFPU_FSQRT:
			// Split out from VMFPU_FDIV 2026-09 - real sqrt costs ~4x
			// division's cost in RTL (see computeFPSqrt).
			total = computeFPSqrt(desc); break;

		case AraFU::VMFPU_IDIV:
			// Untouched: genuine integer division/remainder - no RTL data
			// yet suggesting this serial-divider model is wrong for it.
			total = computeIDIV(desc); break;

		case AraFU::VLSU_UNIT_LD:
			total = computeUnitLoad(desc); break;

		case AraFU::VLSU_UNIT_ST:
			total = computeUnitStore(desc); break;

		case AraFU::VLSU_STRIDED_LD:
			total = computeStridedLoad(desc); break;

		case AraFU::VLSU_STRIDED_ST:
			total = computeStridedStore(desc); break;

		case AraFU::VLSU_GATHER:
			total = computeGather(desc); break;

		case AraFU::VLSU_SCATTER:
			total = computeScatter(desc); break;

		case AraFU::VREDU_INT:
			total = computeReduction(desc, false); break;

		case AraFU::VREDU_FP:
			total = computeReduction(desc, true); break;

		case AraFU::VSLIDE:
			total = computeSlide(desc); break;

		case AraFU::VNARROW:
			total = computeNarrow(desc); break;

		case AraFU::VMASK:
			total = computeMask(desc); break;

		case AraFU::VMV:
			// Scalar move: minimal latency, just front-end
			total = getALUFrontEnd(); break;

		case AraFU::VSETVL:
			// vsetvl is handled by scalar core, not accelerator
			return {1, 1};

		case AraFU::VWHOLE_REG:
			// Whole register load/store: similar to unit-stride
			total = computeUnitLoad(desc); break;

		case AraFU::UNKNOWN:
		default:
			// Fallback: use ALU model
			total = computeALU(desc); break;

	}
	
	result.total_cycles = total;
	
	if (desc.fu == AraFU::VLSU_STRIDED_LD) {
		// FIX 2026-09: VLSU_STRIDED_LD used to share the "n_beats = total"
		// bucket below with VLSU_GATHER, on the assumption both are
		// equally non-pipelined. Found wrong while fixing gather (see
		// c_per_elem_gather's comment): once gather's own occupancy bug
		// was fixed by using `cycles` directly for fu_idx==2 non-unit-
		// stride loads, lavamd (which uses strided, not gather, loads --
		// _MM_LOAD_STRIDE_f32) regressed from -3.0% to -10.5%, because
		// `cycles` (computeStridedLoad() ~= computeUnitLoad() + a small
		// crossbar term, MEDIUM confidence) undershoots strided load's
		// real structural occupancy. A dedicated independent-register RTL
		// probe (ubench_strided_indep, e32/stride=16B, matching lavamd's
		// real shape) measured BYTE-IDENTICAL cycle counts at 2/4/8 lanes
		// (2359/4396/8492 @ vl=8/16/32) -- lane-independent, like gather,
		// but with a much smaller fixed cost (simpler base+i*stride
		// addressing vs. gather's per-element index extraction): fits
		// ~2.46 + 2.0*vl per instruction, essentially gather's per-element
		// slope with a near-zero intercept. Store this directly rather
		// than reusing `total`, since latency and occupancy are genuinely
		// different values here (unlike gather, where the whole operation
		// is one serial unit and `cycles` already IS the right occupancy).
		result.n_beats = 2 + 2 * desc.vl;
	} else if (desc.fu == AraFU::VLSU_GATHER || desc.fu == AraFU::VLSU_SCATTER ||
	    desc.fu == AraFU::VLSU_STRIDED_ST ||
	    desc.fu == AraFU::VMFPU_FDIV || desc.fu == AraFU::VMFPU_FSQRT || desc.fu == AraFU::VMFPU_IDIV ||
	    (desc.fu == AraFU::VMASK && desc.is_mask_scan)) {
		// Mask scan-class ops (vcpop/vfirst/viota/...) are non-pipelined,
		// time-multiplexed hardware (masku.sv) -- same reasoning as the
		// LSU/FDIV/IDIV cases above: the generic n_beats formula below is
		// unrelated to this formula's real cost and would leave the
		// computeMask() fix invisible to the occupancy-driven scoreboard
		// (confirmed: fixing total_cycles alone left the probe's observed
		// ROI completely unchanged, since ALU-class occupancy reads
		// n_beats, not total_cycles).
		// NOTE: VLSU_STRIDED_ST stays in this bucket (occupancy = cycles)
		// -- untouched, since no RiVEC benchmark exercises strided stores
		// and there is no RTL probe validating it either way.
		result.n_beats = total;
	} else {
		uint32_t n = desc.vl > 0 ? (desc.vl * desc.sew + (cfg_.nr_lanes * 64) - 1) / (cfg_.nr_lanes * 64) : 0;
		n = n > 0 ? n : 1;
		if (desc.fu == AraFU::VMFPU_FNONCOMP) {
			// FIX 2026-09: found while chasing a blackscholes regression
			// caused by the lookupRTL occupancy fix above. FNONCOMP's
			// occupancy was plain n_beats, same as every other non-special-
			// cased category -- but a dedicated independent-register RTL
			// probe (ubench_fnoncomp_indep, vfsgnjx.vv/e32/m1/vl=8, matching
			// blackscholes's real abs()-via-sign-manipulation usage) measured
			// 3.43/2.93/2.70 cycles/instr at 2/4/8 lanes against n_beats of
			// 2/1/1 -- a real, fixed ~2-cycle front-end/dispatch cost on top
			// of the streaming beats that plain n_beats misses, the same
			// shape of gap already found and fixed for the LSU-load and
			// lookupRTL-hit FMA/VALU occupancy terms. This category was
			// masked before because the old, over-serialized FMA/VALU
			// lookup occupancy (same fu_idx=1 slot) was already gating
			// reissue more than FNONCOMP's own true cost required; fixing
			// that exposed this pre-existing, separate under-count. Scoped
			// to FNONCOMP only -- no RTL evidence yet that other FPU/ALU
			// categories sharing the generic n_beats fallback need the same
			// correction, and at least one (independent vfmacc.vf at
			// LMUL=8, ubench_fma_lmul8) was already validated with plain
			// n_beats, so this is deliberately not applied FU-class-wide.
			result.n_beats = n + 2;
		} else {
			result.n_beats = n;
		}
	}

	if (g_debug_stats.enabled) {
		auto& s = g_debug_stats.by_class[fuClassIndex(desc.fu)];
		s.count++;
		s.sum_total_cycles += result.total_cycles;
		s.sum_n_beats += result.n_beats;
		auto& fs = g_debug_stats.by_fu[(int)(uint8_t)desc.fu];
		fs.count++;
		fs.sum_total_cycles += result.total_cycles;
		fs.sum_n_beats += result.n_beats;
	}

	return result;
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
			return computeCycles(desc).total_cycles;
	}
}

}  // namespace ara_timing
