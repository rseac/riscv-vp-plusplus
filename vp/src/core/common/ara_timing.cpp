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
// VPPP_TIMING_DEBUG instrumentation (ported from the Ara timing model,
// 2026-09): accumulates, per FU class and per fine-grained AraFU, the total
// "cycles" vs "n_beats" returned by computeCycles(), plus a vl-bucket
// histogram for VMFPU_FMA, so a specific benchmark's real instruction mix
// can be inspected instead of guessed at. Enabled only when the
// VPPP_TIMING_DEBUG env var is set; zero overhead otherwise. Dumped to
// stderr at process exit.
// ============================================================================
namespace {
struct FuClassStats {
	uint64_t count = 0;
	uint64_t sum_total_cycles = 0;
	uint64_t sum_n_beats = 0;
	uint64_t lookup_hit_count = 0;
	uint64_t lookup_hit_cycles = 0;
};
struct FmaVlHisto {
	uint64_t count[4] = {0, 0, 0, 0};
	uint64_t count_other_lmul[4] = {0, 0, 0, 0};
	uint64_t cycles[4] = {0, 0, 0, 0};
};
struct DebugStats {
	bool enabled = false;
	FuClassStats by_class[4]; // 0=ALU, 1=FPU, 2=LSU, 3=OTHER
	FuClassStats by_fu[24];   // fine-grained, indexed by (uint8_t)AraFU
	FmaVlHisto fma_histo;
	DebugStats() { enabled = (std::getenv("VPPP_TIMING_DEBUG") != nullptr); }
	~DebugStats() {
		if (!enabled) return;
		static const char* fu_names[24] = {
			"VALU", "VMFPU_MUL", "VMFPU_FMA", "VMFPU_FADD", "VMFPU_FNONCOMP", "VMFPU_FCONV",
			"VMFPU_FDIV", "VMFPU_FSQRT", "VMFPU_IDIV", "VLSU_UNIT_LD", "VLSU_UNIT_ST",
			"VLSU_STRIDED_LD", "VLSU_STRIDED_ST", "VLSU_GATHER", "VLSU_SCATTER",
			"VREDU_INT", "VREDU_FP", "VSLIDE", "VNARROW", "VMASK", "VMV",
			"VSETVL", "VWHOLE_REG", "UNKNOWN"
		};
		fprintf(stderr, "\n[VPPP_TIMING_DEBUG] Fine-grained per-AraFU breakdown:\n");
		for (int i = 0; i < 24; i++) {
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
		case AraFU::VMFPU_FADD:
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
	
	// FPU EW32 — recalibrated 2026-09 against real AraXL RTL
	// (ubench_fpu_chain_ew32/ew64, benchmark_suite/araxl/apps/), a
	// dependent mul->macc->sub chain, SEW32/64, LMUL1, vl<=16, run on
	// every working AraXL config (4L_2C, 8L_2C - AraXL has no working 2L
	// sim_model, so the old "nl==2" branch below was dead code anyway).
	// Measured per-instruction cost was flat at ~25.8-29.0 cycles across
	// every lane/VLEN tested (mean ~27.5) - like Ara's equivalent fix,
	// a fully dependent chain is latency-bound, not lane-count-bound.
	// Replaces the previous nl/vlen-branchy guess (which also had the
	// SAME fabricated values as Ara's pre-fix model, just copy-pasted).
	// Getting this data required first root-causing and working around a
	// genuine AraXL RTL hang: switching SEW via vsetvli between FP ops
	// deadlocks AraXL's RTL (confirmed AraXL-specific vs single-cluster
	// Ara) - these calibration binaries are single-SEW to avoid it.
	rtl_fpu_ew32_vl16_ = 28;
	rtl_fpu_ew32_vl256_ = (nl == 2) ? 25 : 29;
	rtl_fpu_ew32_vl1024_ = (nl == 2) ? 26 : 30;

	// FPU EW64 -- RECALIBRATED 2026-09 (Phase 11b, chasing a somier
	// overestimate). The old "~27.4-32.3 cycles (mean ~30.6)" claim was
	// never actually validated against a chain matching real application
	// usage. Root-caused via somier's real dependent chain
	// (vfmul->vfmacc->vfmacc->vfsqrt->vfmul.vf->vfdiv->vfmacc x3):
	// isolating just the vfmacc.vv sub-chain on real RTL (4L/2C/1024V)
	// measured a FLAT 11.61-11.66 cycles/instr at vl=4/16/32 (1045/1055/
	// 1048 cycles for 90 total instructions) -- ~2.65x lower than the old
	// "31" constant, which VP++ was reproducing almost exactly (30.74
	// measured on the model). vl=32 falls in the vl256_ bucket per
	// lookupRTL()'s own boundary and showed the identical flat value,
	// directly implicating that bucket too, not just vl16_. Confirmed
	// against RTL/Chisel source, not just curve-fit: ara_pkg.sv defines
	// LatFCompEW64 = 5 (the real FPU pipeline's fixed latency for fused
	// ops at EW64) -- a hardware constant with NO vl dependence, which
	// both explains why the true value is flat across vl (5-cycle pipe +
	// front-end dispatch/VRF-access overhead) and why "31" has no
	// structural basis (it would imply a pipeline ~5x deeper than what
	// actually exists). All three vl buckets share the same "flat ~30.6"
	// origin story in the removed comment above, so all three are set to
	// the same corrected value; vl1024_ has no direct RTL measurement
	// (only vl<=32 was tested) but is set the same way on the strength of
	// LatFCompEW64's vl-independence, consistent with how every other
	// flat dependent-chain latency in this project has behaved.
	rtl_fpu_ew64_vl16_ = 12;
	rtl_fpu_ew64_vl256_ = 12;
	rtl_fpu_ew64_vl1024_ = 12;

	// VMFPU_FADD dependent-chain constants (see AraFU::VMFPU_FADD comment
	// in ara_timing.h). Measured via ubench_fpu_addchain_ew32/64 on real
	// 4L/2C/2048V RTL: a dependent vfadd.vv x4 -> vfmul.vf chain (jacobi2d's
	// actual pattern). Flat ~9.1-9.3 (EW32) / ~10.1 (EW64) cycles/instr at
	// vl<=VLMAX -- roughly 1/3 of VMFPU_FMA's dependent-chain cost above.
	// Only measured at one lane/cluster/VLEN config so far (unlike the FMA
	// constants, which were swept across nl and vl buckets) -- same value
	// used for all three vl buckets pending further calibration.
	rtl_fadd_ew32_vl16_ = 9;
	rtl_fadd_ew32_vl256_ = 9;
	rtl_fadd_ew32_vl1024_ = 9;
	rtl_fadd_ew64_vl16_ = 10;
	rtl_fadd_ew64_vl256_ = 10;
	rtl_fadd_ew64_vl1024_ = 10;

	// P1 fix: independent-register throughput, measured via
	// ubench_fpu_indep_ew32/64.c on real 4L/2C RTL (8 independent vfmul.vv,
	// disjoint vd/vs1/vs2, no RAW/WAW/WAR hazard between any two). Flat
	// ~5.2-5.3 cycles/instr at vl<=16 and at vl==VLMAX across all three
	// VLEN configs tested (1024/2048/4096) -- dominated by front-end issue
	// rate, not by per-beat FU throughput, in this regime. (A request for
	// vl beyond VLMAX measures a much higher, VLEN-dependent cost on this
	// RTL, but that never happens in compiled code -- vsetvli's own AVL
	// clamping means real applications never request vl > VLMAX -- so it
	// is deliberately not modeled here.) This is the n_beats (FU-occupancy)
	// side of the latency/occupancy split for lookupRTL hits; the
	// dependent-chain constants above remain the total_cycles (full result
	// latency) side.
	// NOTE 2026-09 (VMFPU_FADD split): the probe used here is vfmul.vv,
	// which the VMFPU_FADD/VMFPU_FMA split above now classifies as
	// VMFPU_FADD, not VMFPU_FMA -- so this constant is applied to BOTH
	// categories below in computeCycles as a conservative reuse, but it
	// was only actually measured for the simple (non-fused) op class.
	// True fused-FMA (vfmacc-class) independent throughput has not been
	// separately measured; flagged as an open gap rather than assumed
	// identical.
	rtl_fma_indep_beats_ = 5;

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
	// P2 fix 2026-09: was "N_beats = ceil(VL * SEW / (64 * NrLanes))",
	// missing the same nr_clusters factor the memory path
	// (computeUnitLoad/Store) already applies locally. Confirmed
	// structurally: valu.sv/vmfpu.sv (element_cnt = 1 <<
	// (EW64-vsew), i.e. 1 beat/cycle *per lane*) and ara_cluster.sv
	// (NrLanes is replicated per-cluster, NrClusters clusters total) --
	// real total parallel lane count is nr_lanes*nr_clusters, not
	// nr_lanes alone. Confirmed empirically too: independent vmul.vx
	// throughput measured on real 4L/2C RTL (ubench_mulacc_indep_ew64_lmul2)
	// came in at ~2x faster than the old (nr_lanes-only) formula predicted
	// at scale (n_beats=32 old -> ~15.1 cycles measured, not 32) -- a ~2x
	// speedup matching nr_clusters=2 almost exactly. This helper is shared
	// by computeALU/MUL/FPNonComp/FPConv/IDIV/Reduction/Slide/Narrow/Mask
	// and the generic per-instruction n_beats below, so this one fix
	// applies everywhere memory's fix could not (memory calibrates its own
	// n_beats inline, it never called this shared helper).
	uint64_t numerator = (uint64_t)vl * sew;
	uint64_t denominator = 64ULL * cfg_.nr_lanes * cfg_.nr_clusters;
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
	// NOTE 2026-09: VALU has no cluster-crossing term, unlike every other
	// FU category (MUL, FPNonComp, FPConv, IDIV, Reduction, Slide, Narrow),
	// which already carry "log2_nr_clusters_ + ...". Tried restoring it as
	// a flat "+log2_nr_clusters_" (matching that convention, +1 for the
	// only working NR_CLUSTERS=2 configs) and measured NO improvement on
	// the full AraXL RiVEC suite (57.0% vs 56.8% MAPE, within noise) -
	// reverted. A flat few-cycle addition is too small in magnitude to be
	// the real missing mechanism; whatever multi-cluster effect is missing
	// here is more likely multiplicative (as the memory-bandwidth fix
	// turned out to be: real cost scaled with nr_lanes*nr_clusters, not a
	// flat offset) than additive. Confirming that needs real RTL data for
	// ALU/FMA under multiple cluster counts, which is currently blocked by
	// the FMA/FDIV/FSQRT dependent-chain hang (see computeFPFMA below).
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
	// P2 fix 2026-09: this formula only ever gets exercised for patterns
	// lookupRTL doesn't cover (SEW32/LMUL1 always hits the lookupRTL table
	// above instead) -- e.g. imatmul's real usage, vmacc.vx at SEW64/LMUL2,
	// a genuine dependent accumulation chain (C[i][j] += A[i][k]*B[k][j]).
	// The old "+ n_beats" term assumed a dependent chain's latency grows
	// 1:1 with beat count -- no pipelining at all within a single
	// instruction's own element-processing. Measured against real 4L/2C
	// RTL (ubench_mulacc_chain_ew64_lmul2.c, a dependent vmacc.vx chain
	// matching imatmul's actual pattern), using n_beats AFTER the
	// computeNBeats() nr_clusters fix above: total ~= 5.5 + 0.28*n_beats
	// across n_beats(corrected) 1..16, not 1:1 -- the accumulator
	// dependency resolves at a fixed pipeline depth long before all beats
	// finish draining. Only validated at 4L/2C (the only config with real
	// AraXL RTL ground truth); log2_nr_clusters_ and lat_mul are kept
	// symbolically for other configs but were not separately re-validated.
	return log2_nr_clusters_ + 4 + lat_mul + n_beats / 4;
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
	// NOTE 2026-09: the comment above says the log2_nr_clusters_ term was
	// deliberately dropped from the "HIGH-confidence structural formula"
	// because it "overpredicted" against whatever calibration point
	// motivated this "effective" model - that conclusion was never
	// re-verified against a real application suite. Tried restoring
	// "+log2_nr_clusters_" (matching the convention used elsewhere, +1 for
	// the only working NR_CLUSTERS=2 configs) and measured NO improvement
	// on the full AraXL RiVEC suite (57.0% vs 56.8% MAPE, within noise) -
	// reverted. Same conclusion as computeALU above: a flat few-cycle
	// addition isn't the real missing mechanism. The FMA/FDIV/FSQRT
	// dependent-chain RTL calibration needed to find the real one hangs
	// AraXL's RTL reproducibly (100% CPU, zero forward progress,
	// deterministic across every working config - see
	// ubench_fpu_chain_hazards) - unresolved pending that hang's root cause.
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
// FP simple 2-operand arithmetic (VMFPU_FADD) analytical fallback
// Only reached when lookupRTL doesn't match (LMUL != 1) -- the LMUL=1 case
// always hits the lookupRTL table above, calibrated separately from FMA
// (see AraFU::VMFPU_FADD). This fallback reuses computeFPFMA's structural
// formula rather than a separately-derived one: real AraXL RTL fpu_latency()
// (vmfpu.sv) gives ADD/MUL/FMA the same fixed pipeline latency for a given
// SEW in the default (non-NONCOMP/CONV/DIVSQRT) case -- the empirically
// measured ~3x gap between VMFPU_FADD and VMFPU_FMA's *dependent-chain*
// lookupRTL constants reflects real chaining/desynchronization overhead this
// simpler analytical model doesn't attempt to capture for either category,
// so there's no evidence yet that a different analytical formula is needed
// here specifically.
// ============================================================================
uint64_t AraTimingModel::computeFPAdd(const AraVecInsn& desc) const {
	return computeFPFMA(desc);
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
// Calibrated 2026-09 against ubench_fdiv_chain_ew32/ew64 on real AraXL RTL
// (single-SEW binaries - see the SEW-switch hang note above). Cost was
// flat ~6.6-7.3 cycles/instruction across every lane count, VLEN, and SEW
// tested - same shape as Ara's equivalent fix, and coincidentally almost
// the same magnitude (Ara: ~7-9). Previously this FU was routed through
// computeIDIV's serial-integer-divider formula, the same ~30-60x
// overestimate bug Ara had before its fix.
// ============================================================================
uint64_t AraTimingModel::computeFPDiv(const AraVecInsn& desc) const {
	(void)desc;
	return 7;
}

// ============================================================================
// FP Sqrt (VFSQRT)
//
// Calibrated 2026-09, same sweep. Measured flat ~5.6-6.9 cycles/instruction
// - notably, on AraXL sqrt costs about the SAME as divide (unlike Ara,
// where sqrt was ~4x more expensive than divide and showed real
// lane/VLEN-dependent variability). Different real hardware, different
// characteristic - not assumed equal to Ara's, independently measured.
// ============================================================================
uint64_t AraTimingModel::computeFPSqrt(const AraVecInsn& desc) const {
	(void)desc;
	return 6;
}

// ============================================================================
// Equation #10: Vector Load - Unit Stride (VLE)
// HIGH confidence
//
// T = 2*ceil(log2(NrClusters)) + 11 + T_mem + N_beats
// ============================================================================
uint64_t AraTimingModel::computeUnitLoad(const AraVecInsn& desc) const {
	// AraXL-specific recalibration, 2026-09, against a real RTL sweep
	// (ubench_mem_hazards, benchmark_suite/araxl/apps/ubench_mem_hazards)
	// run on every working AraXL sim_model config (all NR_CLUSTERS=2 - no
	// other cluster count has a working Verilator binary to calibrate
	// against). Two findings vs. the flat single-cluster Ara model:
	//   1. Fixed per-instruction cost is ~30 cycles (not Ara's ~14) - real
	//      AraXL pays extra round-trip latency crossing the inter-cluster
	//      interconnect, consistent with the separately-confirmed _canneal
	//      hang being a genuine cross-cluster memory transaction issue.
	//   2. Aggregate bandwidth scales with nr_lanes * nr_clusters, not
	//      nr_lanes alone (each cluster has its own AXI port) - measured
	//      per-lane rate matched Ara's ~1.94-2.0 almost exactly once
	//      divided by (lanes * clusters) instead of lanes alone. The shared
	//      computeNBeats() only divides by nr_lanes, so the nr_clusters
	//      factor is applied locally here rather than in computeNBeats()
	//      (used by every FU category) - we only have RTL evidence for this
	//      scaling on the memory path. The FMA/FDIV/FSQRT calibration
	//      chains reproducibly HANG on AraXL RTL (100% CPU, zero forward
	//      progress for 3+ minutes, deterministic across all 6 working
	//      configs) - compute-side scaling is NOT verified and NOT changed.
	// CAVEAT: calibrated only for NR_CLUSTERS=2. Extrapolating the
	// nr_clusters factor to other cluster counts is unverified.
	uint64_t denom = 64ULL * cfg_.nr_lanes * cfg_.nr_clusters;
	uint64_t n_beats_aggregate = ((uint64_t)desc.vl * desc.sew + denom - 1) / denom;
	return 30 + 2 * (uint32_t)n_beats_aggregate;
}

// ============================================================================
// Equation #11: Vector Store - Unit Stride (VSE)
//
// AraXL-specific recalibration, 2026-09 - same methodology and caveats as
// computeUnitLoad above. Fixed cost measured much smaller for stores (~6
// cycles vs loads' ~30), consistent with Ara's finding that stores have no
// destination register to expose a full round-trip wait on.
// ============================================================================
uint64_t AraTimingModel::computeUnitStore(const AraVecInsn& desc) const {
	uint64_t denom = 64ULL * cfg_.nr_lanes * cfg_.nr_clusters;
	uint64_t n_beats_aggregate = ((uint64_t)desc.vl * desc.sew + denom - 1) / denom;
	return 6 + 2 * (uint32_t)n_beats_aggregate;
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
	
	// FPU fused multiply-add (vfmacc.vv etc) — e32/e64 m1
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

	// FPU simple 2-operand arithmetic (vfadd.vv, vfmul.vf, etc) — e32/e64 m1.
	// Split from VMFPU_FMA above 2026-09 (see AraFU::VMFPU_FADD comment).
	if (desc.fu == AraFU::VMFPU_FADD && desc.lmul_num == 1 && desc.lmul_den == 1) {
		if (desc.sew == 32) {
			if (req_vl <= 16) return rtl_fadd_ew32_vl16_;
			if (req_vl <= 256) return rtl_fadd_ew32_vl256_;
			return rtl_fadd_ew32_vl1024_;
		}
		if (desc.sew == 64) {
			if (req_vl <= 16) return rtl_fadd_ew64_vl16_;
			if (req_vl <= 256) return rtl_fadd_ew64_vl256_;
			return rtl_fadd_ew64_vl1024_;
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
		// P1 fix: lookupRTL's FMA/FADD constants are calibrated from fully
		// dependent chains (see ubench_fpu_chain_ew32/64 and
		// ubench_fpu_addchain_ew32/64), so rtl_val is a full-result-latency
		// number. Independent instructions of these classes (no data
		// dependency on their predecessor) can pipeline and should only
		// occupy the FU for ~rtl_fma_indep_beats_ cycles, not the full
		// dependent-chain latency -- otherwise every instance serializes
		// against every other one regardless of real dependencies
		// (confirmed via RTL debug tracing to be the dominant driver of
		// jacobi2d/somier/swaptions' overestimates). Other lookupRTL
		// categories are unaffected: their calibration wasn't shown to
		// have this gap.
		bool is_fma_class_lookup = (desc.fu == AraFU::VMFPU_FMA || desc.fu == AraFU::VMFPU_FADD) &&
		                           desc.lmul_num == 1 && desc.lmul_den == 1;
		uint64_t n_beats_val = is_fma_class_lookup ? rtl_fma_indep_beats_ : rtl_val;

		if (g_debug_stats.enabled) {
			int idx = fuClassIndex(desc.fu);
			auto& s = g_debug_stats.by_class[idx];
			s.count++;
			s.sum_total_cycles += rtl_val;
			s.sum_n_beats += n_beats_val;
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
		return {rtl_val, n_beats_val};
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

		case AraFU::VMFPU_FADD:
			total = computeFPAdd(desc); break;

		case AraFU::VMFPU_FNONCOMP:
			total = computeFPNonComp(desc); break;

		case AraFU::VMFPU_FCONV:
			total = computeFPConv(desc); break;

		case AraFU::VMFPU_FDIV:
			// Recalibrated 2026-09: real FP division has a dedicated
			// pipelined unit, not the serial integer divider this used to
			// reuse via computeIDIV.
			total = computeFPDiv(desc); break;

		case AraFU::VMFPU_FSQRT:
			// Split out from VMFPU_FDIV 2026-09.
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
	
	if (desc.fu == AraFU::VLSU_GATHER || desc.fu == AraFU::VLSU_SCATTER ||
	    desc.fu == AraFU::VLSU_STRIDED_LD || desc.fu == AraFU::VLSU_STRIDED_ST ||
	    desc.fu == AraFU::VMFPU_FDIV || desc.fu == AraFU::VMFPU_FSQRT || desc.fu == AraFU::VMFPU_IDIV) {
		result.n_beats = total;
	} else {
		// P2 fix 2026-09: same missing nr_clusters factor as computeNBeats()
		// above (this is a separate, duplicated copy of that formula) --
		// see the comment there for the RTL/empirical justification.
		uint64_t denom = 64ULL * cfg_.nr_lanes * cfg_.nr_clusters;
		uint64_t n = desc.vl > 0 ? ((uint64_t)desc.vl * desc.sew + denom - 1) / denom : 0;
		result.n_beats = n > 0 ? (uint32_t)n : 1;
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
