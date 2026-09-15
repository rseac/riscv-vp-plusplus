#pragma once

/*
 * AraXL Vector Timing Model for RISC-V VP++
 *
 * Implements RTL-derived timing equations from the AraXL mathematical model (v3).
 * Computes dynamic cycle counts for RVV instructions based on runtime VTYPE/VL state.
 *
 * Reference: build/AraXL/models/math_model_approved.md
 */

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace ara_timing {

/*
 * Functional Unit classification for ARA vector accelerator
 */
enum class AraFU : uint8_t {
	VALU,          // Integer ALU (vadd, vsub, vand, vor, etc.)
	VMFPU_MUL,    // Integer multiply (vmul, vmulh, vsmul)
	VMFPU_FMA,    // FP fused multiply-add/sub (vfmacc, vfmadd, vfmsac, vfnmacc,
	              // etc.) - reads its own destination as a third operand for
	              // accumulation, unlike VMFPU_FADD below.
	VMFPU_FADD,   // FP simple 2-operand arithmetic (vfadd, vfsub, vfrsub,
	              // vfmul) - split out from VMFPU_FMA 2026-09: jacobi2d's
	              // real dependent chain (vfadd.vv x4 -> vfmul.vf, a 5-point
	              // stencil sum+scale, confirmed via objdump) measured
	              // ~9.2 cycles/instr (EW32) / ~10.1 (EW64) on real AraXL
	              // RTL via a matching dependent chain -- roughly 1/3 of
	              // VMFPU_FMA's ~28-31 cycles/instr, which was calibrated
	              // from a vfmul->vfmacc->vfsub chain. Lumping these under
	              // one constant overestimated every simple-add-dominated
	              // benchmark (jacobi2d, somier's vfsub/vfadd portions) by
	              // ~3x on their dependent-chain instructions. The 3-operand
	              // fused ops (reading their own destination) appear to have
	              // genuinely higher real latency than plain 2-operand ones,
	              // consistent with an extra register-file read port/cycle
	              // for the accumulator operand (not yet independently
	              // confirmed in the RTL structurally -- flagged as
	              // curve-fit-derived, not structurally-derived, per the
	              // rtl_calibrator opcode-uniformity discipline).
	VMFPU_FNONCOMP, // FP non-computational (vfmin, vfmax, vfsgnj, etc.)
	VMFPU_FCONV,  // FP conversion (vfcvt*, vfwcvt*, vfncvt*)
	VMFPU_FDIV,   // FP divide (vfdiv, vfrdiv) - real division, NOT sqrt
	VMFPU_FSQRT,  // FP sqrt (vfsqrt) - split out 2026-09: on AraXL RTL,
	              // measured cost is actually similar to FDIV (~6-7
	              // cycles/instr for both), unlike Ara where sqrt was ~4x
	              // more expensive than divide - kept as its own category
	              // for consistency with Ara's model and because the two
	              // costs are independently calibrated, not assumed equal.
	VMFPU_IDIV,   // Integer division (vdiv, vdivu, vrem, vremu)
	VLSU_UNIT_LD, // Unit-stride load
	VLSU_UNIT_ST, // Unit-stride store
	VLSU_STRIDED_LD,  // Strided load
	VLSU_STRIDED_ST,  // Strided store
	VLSU_GATHER,  // Indexed/gather load (vluxei, vloxei)
	VLSU_SCATTER, // Indexed/scatter store (vsuxei, vsoxei)
	VREDU_INT,    // Integer reduction
	VREDU_FP,     // FP reduction
	VSLIDE,       // Slide operations
	VNARROW,      // Narrowing operations (vnsrl, vnsra, vnclip)
	VMASK,        // Mask operations (vmand, vmor, vcpop, etc.)
	VMV,          // Scalar move (vmv.x.s, vmv.s.x)
	VSETVL,       // vsetvli/vsetivli/vsetvl (config, not vector compute)
	VWHOLE_REG,   // Whole register load/store
	UNKNOWN       // Fallback
};

/*
 * Descriptor for a vector instruction's runtime parameters
 */
struct AraVecInsn {
	AraFU fu;
	uint32_t vl;       // Current vector length
	uint32_t sew;      // Selected element width in bits (8, 16, 32, 64)
	uint32_t lmul_num; // LMUL numerator (1, 2, 4, 8 for LMUL>=1; 1 for fractional)
	uint32_t lmul_den; // LMUL denominator (1 for LMUL>=1; 2, 4, 8 for fractional)
	uint64_t stride;   // For strided ops: stride value from rs2
	uint32_t sew_idx;  // For gather/scatter: index EEW (8, 16, 32, 64)
	bool is_widening;  // Widening operation
};

/*
 * Hardware configuration parameters
 * Tunable constants are exposed as command-line arguments for calibration.
 */
struct AraConfig {
	// Hardware structural parameters (from project_config.json / math model)
	uint32_t nr_lanes;    // Number of execution lanes (default: 2)
	uint32_t nr_clusters; // Number of clusters (default: 2)
	uint32_t vlen;        // Vector register length in bits (default: 2048)
	uint32_t elen;        // Max element width (default: 64)

	// Tunable constants (exposed as CLI/property args for calibrator)
	uint32_t tau_mem;     // Memory latency in cycles (AXI R/B response)
	uint32_t c_harness;   // Measurement harness overhead (rdcycle pair)

	// Gather tunable: per-element cost and startup floor
	// These are per-config and may be tuned by the calibrator
	double c_per_elem_gather;    // Gather per-element cost (default: 3.0 for 2L)
	uint32_t c_startup_floor_gather; // Gather startup floor (default: 67 for 2L)

	// FPU front-end constants (tunable for calibration)
	uint32_t c_fe_fpu_ew32;  // FPU front-end overhead for EW32 (default: 8)
	uint32_t c_fe_fpu_ew64;  // FPU front-end overhead for EW64 (default: 5)

	AraConfig()
	    : nr_lanes(2),
	      nr_clusters(2),
	      vlen(2048),
	      elen(64),
	      tau_mem(10),
	      c_harness(6),
	      c_per_elem_gather(3.0),
	      c_startup_floor_gather(67),
	      c_fe_fpu_ew32(8),
	      c_fe_fpu_ew64(5) {}
};

struct AraInstLatency {
	uint64_t total_cycles;
	uint64_t n_beats; // fu_busy_time
};

/*
 * Main timing engine class
 */
class AraTimingModel {
   public:
	uint32_t getLatFP(uint32_t sew) const;
	explicit AraTimingModel(const AraConfig& cfg);

	/*
	 * Compute the cycle count for a given vector instruction descriptor.
	 * This is the primary entry point called from VExtension::finishInstr().
	 */
	AraInstLatency computeCycles(const AraVecInsn& desc) const;

	/*
	 * Compute only the pipeline overhead for memory operations
	 * (used when TLM memory provides its own latency).
	 */
	uint64_t computePipelineOverhead(const AraVecInsn& desc) const;

	const AraConfig& getConfig() const { return cfg_; }

   private:
	AraConfig cfg_;

	// Precomputed derived constants
	uint32_t log2_nr_clusters_;

	// --- Fixed structural constants (from math model v3) ---

	// Pipeline latencies from ara_pkg.sv
	static constexpr uint32_t LAT_MUL_EW64 = 1;
	static constexpr uint32_t LAT_MUL_EW32 = 1;
	static constexpr uint32_t LAT_MUL_EW16 = 1;
	static constexpr uint32_t LAT_MUL_EW8 = 0;

	static constexpr uint32_t LAT_FP_EW64 = 5;
	static constexpr uint32_t LAT_FP_EW32 = 4;
	static constexpr uint32_t LAT_FP_EW16 = 3;
	static constexpr uint32_t LAT_FP_EW8 = 2;

	static constexpr uint32_t LAT_FNONCOMP = 1;
	static constexpr uint32_t LAT_FCONV = 2;
	static constexpr uint32_t LAT_FDIVSQRT = 3;

	// ALU floor values (from structural_fix_v2, §3.3)
	static constexpr uint32_t T_FLOOR_2L = 16;
	static constexpr uint32_t T_FLOOR_GE4L = 18;

	// ALU front-end latencies
	static constexpr uint32_t L_FE_ALU_2L = 8;
	static constexpr uint32_t L_FE_ALU_GE4L = 10;

	// Chaining constants (structural from desynch mechanism)
	static constexpr uint32_t C_DESYNCH_PROP = 2;
	static constexpr uint32_t C_DESYNCH_ISSUE = 3;
	static constexpr uint32_t C_DRAIN_FLOOR = 8;

	// Strided interconnect floor
	static constexpr uint32_t C_STRIDED_INTERCONNECT_FLOOR = 4;

	// Gather fixed pipeline constant
	static constexpr uint32_t C_GATHER_FIXED = 15;

	// VLSU sync FF chain
	static constexpr uint32_t C_SYNC_VLSU = 3;

	// --- Helper methods ---
	uint32_t computeNBeats(uint32_t vl, uint32_t sew) const;
	uint32_t getLatMul(uint32_t sew) const;
	uint32_t getALUFloor() const;
	uint32_t getALUFrontEnd() const;
	double getGatherPerElem() const;
	uint32_t getGatherStartupFloor() const;

	uint64_t computeALU(const AraVecInsn& desc) const;
	uint64_t computeMUL(const AraVecInsn& desc) const;
	uint64_t computeFPFMA(const AraVecInsn& desc) const;
	uint64_t computeFPAdd(const AraVecInsn& desc) const;
	uint64_t computeFPNonComp(const AraVecInsn& desc) const;
	uint64_t computeFPConv(const AraVecInsn& desc) const;
	uint64_t computeIDIV(const AraVecInsn& desc) const;
	uint64_t computeFPDiv(const AraVecInsn& desc) const;
	uint64_t computeFPSqrt(const AraVecInsn& desc) const;
	uint64_t computeUnitLoad(const AraVecInsn& desc) const;
	uint64_t computeUnitStore(const AraVecInsn& desc) const;
	uint64_t computeStridedLoad(const AraVecInsn& desc) const;
	uint64_t computeStridedStore(const AraVecInsn& desc) const;
	uint64_t computeGather(const AraVecInsn& desc) const;
	uint64_t computeScatter(const AraVecInsn& desc) const;
	uint64_t computeReduction(const AraVecInsn& desc, bool is_fp) const;
	uint64_t computeSlide(const AraVecInsn& desc) const;
	uint64_t computeNarrow(const AraVecInsn& desc) const;
	uint64_t computeMask(const AraVecInsn& desc) const;
	uint64_t lookupRTL(const AraVecInsn& desc) const;

	// RTL calibration values per (NrLanes, VLEN) configuration
	uint32_t rtl_fpu_ew32_vl16_, rtl_fpu_ew32_vl256_, rtl_fpu_ew32_vl1024_;
	uint32_t rtl_fpu_ew64_vl16_, rtl_fpu_ew64_vl256_, rtl_fpu_ew64_vl1024_;
	// VMFPU_FADD dependent-chain constants (see AraFU::VMFPU_FADD comment).
	// Measured via ubench_fpu_addchain_ew32/64 on real 4L/2C RTL: flat
	// ~9.1-9.3 cycles/instr (EW32) and ~10.1 cycles/instr (EW64) at
	// vl<=VLMAX across VLEN 2048 (only config measured so far -- not yet
	// swept across vl buckets or other VLEN/lane configs the way the FMA
	// constants above were, so the same value is used for all three
	// buckets pending further calibration).
	uint32_t rtl_fadd_ew32_vl16_, rtl_fadd_ew32_vl256_, rtl_fadd_ew32_vl1024_;
	uint32_t rtl_fadd_ew64_vl16_, rtl_fadd_ew64_vl256_, rtl_fadd_ew64_vl1024_;
	// Independent-register FMA-class throughput (FU-occupancy side of the
	// latency/occupancy split -- see computeCycles). Measured via
	// ubench_fpu_indep_ew32/64 on real 4L/2C RTL: flat ~5.2 cycles/instr
	// across VLEN 1024/2048/4096 at realistic (<=VLMAX) vl, vs. the
	// ~28-31 cycles/instr the dependent-chain constants above measure.
	uint32_t rtl_fma_indep_beats_;
	uint32_t rtl_valu_add_m1_vl16_, rtl_valu_add_m1_vl256_, rtl_valu_add_m1_vl1024_;
	uint32_t rtl_valu_add_m2_vl16_, rtl_valu_add_m2_vl256_, rtl_valu_add_m2_vl1024_;
	uint32_t rtl_valu_add_m8_vl16_, rtl_valu_add_m8_vl256_;
	uint32_t rtl_valu_mul_vl16_, rtl_valu_mul_vl256_, rtl_valu_mul_vl1024_;
	uint32_t rtl_valu_div_vl16_, rtl_valu_div_vl256_;
	uint32_t rtl_valu_redsum_vl16_, rtl_valu_redsum_vl256_;
	uint32_t rtl_vlsu_stride_vl16_, rtl_vlsu_stride_vl256_, rtl_vlsu_stride_vl1024_;
};

}  // namespace ara_timing
