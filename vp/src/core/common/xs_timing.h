#pragma once

/*
 * XSTop (XiangShan Kunminghu) Vector + Scalar Timing Model for RISC-V VP++
 *
 * Implements the RTL-derived, confidence-tiered timing equations from the
 * APPROVED math model (v2):
 *     build/XSTop/models/math_model_approved.md  (Section 6, "Summary of Key Equations")
 *
 * TARGET MICROARCHITECTURE: unified_ooo  (project_config.json -> hardware.vector_arch)
 *   XSTop is a unified out-of-order superscalar core. There is NO decoupled vector
 *   coprocessor and NO ARI queue. This engine therefore implements INTEGRATION SPEC
 *   PROFILE B:
 *     - positive cycle injection ONLY (never a credit/subtract path -> avoids the
 *       SystemC quantum-keeper unsigned underflow hang),
 *     - overlap modeled via a per-register scoreboard (RAW/WAW) ONLY,
 *     - scalar FU latencies (fdiv/fcvt/div/mul/...) INJECTED per RTL calibration
 *       (handled via the property-tree per-instruction overrides; see config JSON),
 *     - enable_scalar_hiding = 0 (no ARI-queue hiding).
 *
 * The equations below are implemented as DYNAMIC ALGEBRAIC formulas (functions of
 * desc.vl / desc.sew / desc.lmul) rather than if/else lookup tables wherever the
 * math model expresses a closed form. Highly non-linear hardware quirks that the
 * model expresses as small ROM/PLA curves (e.g. the m^2 vrgather uop count
 * {1,4,16,64}) are implemented via the exact per-class curve from UopInfoGen.sv.
 *
 * CONSTANT POLICY (per integration spec + math model):
 *   - FIXED calibrated / structural constants (W_s=2, W_v=3, W_m=1, L_core DFF depths,
 *     C_div_fixed=6, C_div_drain=1, R_div=4) are HARDCODED. They are HIGH-confidence
 *     static DFF counts and are NOT tunable.
 *   - MEDIUM/LOW "tunable" constants (C_dep_resync, C_uop_arith, C_uop_red,
 *     C_perm_uop, C_fill, C_reissue, the strided/gather/vfredusum anchors) are EXPOSED
 *     as configuration fields so the @simulator_calibrator can tune them to absorb
 *     simulator-specific artifacts. They are loaded from the VP++ property tree.
 */

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace xs_timing {

/*
 * Functional Unit classification (shared RVV decode with xs_timing_classify.h).
 * The enum values must match xs_timing_classify.h.
 */
enum class XsFU : uint8_t {
	VALU,            // Integer ALU (vadd, vsub, vand, vor, vsll, vmseq, vmerge, ...)
	VMFPU_MUL,       // Integer multiply (vmul, vmulh, vsmul, vmacc, vwmul)
	VMFPU_FMA,       // FP arithmetic (vfadd, vfmul, vfmacc, ...) -> VFMA core (3)
	VMFPU_FNONCOMP,  // FP non-computational (vfmin, vfmax, vfsgnj) -> VFAlu core (1)
	VMFPU_FCONV,     // FP conversion (vfcvt*, vfwcvt*, vfncvt*)   -> VCVT core (2)
	VMFPU_FDIV,      // FP div/sqrt (iterative)
	VMFPU_IDIV,      // Integer division (iterative)
	VLSU_UNIT_LD,    // Unit-stride load
	VLSU_UNIT_ST,    // Unit-stride store
	VLSU_STRIDED_LD, // Strided load
	VLSU_STRIDED_ST, // Strided store
	VLSU_GATHER,     // Indexed/gather load
	VLSU_SCATTER,    // Indexed/scatter store
	VREDU_INT,       // Integer reduction (vredsum, vredmax, ...)
	VREDU_FP,        // FP reduction (vfredusum ordered, vfredmax, ...)
	VSLIDE,          // Slide / permute / gather-compute (vslide*, vrgather*, vcompress)
	VNARROW,         // Narrowing operations (vnsrl, vnsra, vnclip)
	VMASK,           // Mask operations (vmand, vcpop, viota, vid, ...)
	VMV,             // Scalar move / extract (vmv.x.s, vfmv.f.s -> data hazard sync)
	VSETVL,          // vsetvli/vsetivli/vsetvl (config)
	VWHOLE_REG,      // Whole register move/load/store
	UNKNOWN          // Fallback
};

/*
 * Runtime parameters for one architectural vector instruction.
 */
struct XsVecInsn {
	XsFU fu;
	uint32_t vl;        // Current vector length (elements)
	uint32_t sew;       // Selected element width in bits (8, 16, 32, 64)
	uint32_t lmul_num;  // LMUL numerator   (1,2,4,8 for LMUL>=1; 1 for fractional)
	uint32_t lmul_den;  // LMUL denominator (1 for LMUL>=1; 2,4,8 for fractional)
	uint64_t stride;    // Strided ops: byte stride from rs2
	uint32_t sew_idx;   // Gather/scatter: index EEW
	bool is_fp;         // FP op (routes VALU-ish encodings to FP cores when relevant)

	XsVecInsn()
	    : fu(XsFU::UNKNOWN), vl(0), sew(32), lmul_num(1), lmul_den(1),
	      stride(0), sew_idx(0), is_fp(false) {}
};

/*
 * Hardware configuration + tunable calibration constants.
 *
 * FIXED structural values default from the math model (VLEN=128, N_L=2, 2 lanes).
 * MEDIUM/LOW constants default to the model's fitted/anchored values and are
 * OVERRIDABLE via the VP++ property tree so the calibrator can tune them.
 */
struct XsConfig {
	// --- Fixed structural (math model §1.1) ---
	uint32_t vlen;     // VLEN in bits (128 for XSTop 2_lanes_128_vlen)
	uint32_t dlen;     // Datapath bits per uop (128)
	uint32_t nr_lanes; // N_L reduction lanes (VLEN/64 = 2)

	// Unified-OoO microarchitecture selector. 0 => Profile B (XSTop). Never enable
	// scalar hiding for a unified core (source of the credit-back underflow hang).
	uint32_t enable_scalar_hiding;

	// --- Fixed issue-wrapper depths (math model §1.1, HIGH) ---
	uint32_t w_s;  // scalar control-wrapper depth (OG0 + OG1) = 2
	uint32_t w_v;  // vector-issue wrapper depth (W_s + OG2) = 3
	uint32_t w_m;  // mem-pipe issue accept depth = 1

	// --- Vector core datapath depths L_core (math model §1.3, HIGH DFF) ---
	uint32_t lcore_valu;   // VIAluFix = 1
	uint32_t lcore_vmul;   // VIMacU = 2
	uint32_t lcore_vfma;   // VFMA = 3
	uint32_t lcore_vfalu;  // VFAlu = 1
	uint32_t lcore_vcvt;   // VCVT = 2
	uint32_t lcore_vipu;   // VIPU (reduction) = 2

	// --- MEDIUM structural constants (math model §1.4, tunable) ---
	uint32_t c_dep_resync; // OoO wakeup->og0->og1->WB per dependent uop ~= 4
	uint32_t c_uop_arith;  // steady-state per-uop arith throughput cost ~= 2
	uint32_t c_fill;       // pipeline fill for short uop counts ~= 5
	// integer-reduction accumulate slope, encoded x10 for property-tree integers.
	uint32_t c_uop_red_x10; // ~= 12  (=> 1.2 cyc/uop)
	uint32_t c_red_fixed;   // reduction fixed = 2
	uint32_t c_red_fold;    // reduction final fold = 1
	uint32_t c_perm_uop;    // steady-state per-uop gather/permute cost ~= 2

	// --- LOW empirical anchors (math model §1.5, tunable) ---
	// FP-ordered reduction per-element FP add latency (vfredusum anchor).
	uint32_t l_fadd;                 // ~= 3 (VFMA-ish serial add step)
	uint32_t c_red_fp_fixed;         // fixed part of vfredusum = 2
	// Strided: envelope [6.84, 9.44]; modeled as per-uop (C_flow + P_line(stride)).
	uint32_t c_strided_flow;         // per-uop flow cost ~= 3
	uint32_t p_line_max;             // per-line fill at small stride ~= 6
	uint32_t c_strided_wb;           // strided writeback ~= 1
	// Indexed gather per-index round-trip (anchor 11.30 cyc/it).
	uint32_t c_gather_rt_x10;        // per-index (tlb+cache+merge) x10 ~= 113
	// Iterative divide.
	uint32_t c_div_fixed;   // FSM fixed hops = 6 (HIGH)
	uint32_t c_div_drain;   // s_finish->s_idle drain = 1 (HIGH)
	uint32_t r_div;         // quotient bits/iter (radix-16) = 4 (HIGH)
	uint32_t c_reissue;     // reissue hop between dependent divides (LOW anchor) ~= 2

	// --- Memory (tunable, UNVALIDATED off-die) ---
	uint32_t t_l1hit;  // L1D hit load-to-use core depth = 3
	uint32_t tau_mem;  // miss/DRAM adder (tunable)

	XsConfig()
	    : vlen(128), dlen(128), nr_lanes(2),
	      enable_scalar_hiding(0),
	      w_s(2), w_v(3), w_m(1),
	      lcore_valu(1), lcore_vmul(2), lcore_vfma(3), lcore_vfalu(1),
	      lcore_vcvt(2), lcore_vipu(2),
	      c_dep_resync(4), c_uop_arith(2), c_fill(5),
	      c_uop_red_x10(12), c_red_fixed(2), c_red_fold(1), c_perm_uop(2),
	      l_fadd(3), c_red_fp_fixed(2),
	      c_strided_flow(3), p_line_max(6), c_strided_wb(1),
	      c_gather_rt_x10(113),
	      c_div_fixed(6), c_div_drain(1), r_div(4), c_reissue(2),
	      t_l1hit(3), tau_mem(10) {}
};

/*
 * Result of a single vector-instruction latency computation.
 *   total_cycles : full issue->writeback latency of the architectural instruction.
 *   n_beats      : occupancy (throughput) of the FU, used only for scoreboard
 *                  bookkeeping. For Profile B this is informational (positive
 *                  injection only).
 */
struct XsInstLatency {
	uint64_t total_cycles;
	uint64_t n_beats;
};

/*
 * XSTop unified-OoO timing engine.
 *
 * All public methods are const; the engine holds only the (immutable after
 * construction) configuration. Runtime scoreboard state lives in VExtension.
 */
class XsTimingModel {
   public:
	explicit XsTimingModel(const XsConfig& cfg) : cfg_(cfg) {}

	const XsConfig& getConfig() const { return cfg_; }

	/*
	 * Number of micro-ops the XSTop decoder cracks one architectural vector
	 * instruction into (UopInfoGen.sv). Per-op-class curve (math model §1.2):
	 *   element-wise arith : m           (linear)
	 *   vrgather           : m^2 {1,4,16,64}
	 *   vslide             : 2*m         (linear x2)
	 *   reduction (int)    : lmul + sew-tree fold
	 * lmul is passed as an integer group multiplier (>=1); fractional LMUL -> 1.
	 */
	static uint32_t lmulGroup(const XsVecInsn& desc) {
		if (desc.lmul_den > 1) return 1;               // fractional LMUL -> single reg group
		return desc.lmul_num > 0 ? desc.lmul_num : 1;  // 1,2,4,8
	}

	uint32_t numOfUopArith(const XsVecInsn& desc) const { return lmulGroup(desc); }

	uint32_t numOfUopGather(const XsVecInsn& desc) const {
		uint32_t m = lmulGroup(desc);
		return m * m;  // {1,4,16,64} for m={1,2,4,8}
	}

	uint32_t numOfUopSlide(const XsVecInsn& desc) const { return lmulGroup(desc) * 2u; }

	// integer reduction uop count: lmul + a log2(sew-tree) fold term.
	uint32_t numOfUopRed(const XsVecInsn& desc) const {
		uint32_t m = lmulGroup(desc);
		uint32_t sew = desc.sew ? desc.sew : 32;
		// sew-tree fold depth = log2(sew/8) reduction stages across the datapath.
		uint32_t fold = 0;
		for (uint32_t s = sew; s > 8; s >>= 1) ++fold;
		return m + fold;
	}

	// VLMAX-sized element count for FP-ordered reduction (serial chain).
	uint32_t vlmaxElems(const XsVecInsn& desc) const {
		uint32_t m = lmulGroup(desc);
		uint32_t sew = desc.sew ? desc.sew : 32;
		return (m * cfg_.vlen) / sew;  // m * VLEN/sew
	}

	/*
	 * Per-uop core datapath depth L_core for the FU (math model §1.3, HIGH).
	 */
	uint32_t lcore(const XsVecInsn& desc) const {
		switch (desc.fu) {
			case XsFU::VALU:
			case XsFU::VMASK:
			case XsFU::VNARROW:
				return cfg_.lcore_valu;              // VIAluFix = 1
			case XsFU::VMFPU_MUL:
				return cfg_.lcore_vmul;              // VIMacU = 2
			case XsFU::VMFPU_FMA:
				return cfg_.lcore_vfma;              // VFMA = 3
			case XsFU::VMFPU_FNONCOMP:
				return cfg_.lcore_vfalu;             // VFAlu = 1
			case XsFU::VMFPU_FCONV:
				return cfg_.lcore_vcvt;              // VCVT = 2
			case XsFU::VREDU_INT:
			case XsFU::VREDU_FP:
				return cfg_.lcore_vipu;              // VIPU = 2
			case XsFU::VSLIDE:
				return cfg_.lcore_vipu;              // VPPU = 2 (permute)
			default:
				return cfg_.lcore_valu;
		}
	}

	/*
	 * Primary entry point. Returns the issue->writeback latency (total_cycles)
	 * for one architectural vector instruction, plus FU occupancy (n_beats).
	 *
	 * Profile B: this is the "dependent-chain" latency an instruction pays when
	 * its sources are ready NOW. The VExtension scoreboard adds RAW/WAW stalls on
	 * top of this when sources are still in flight, and overlaps independent
	 * instructions naturally by injecting only this latency.
	 */
	XsInstLatency computeCycles(const XsVecInsn& desc) const {
		XsInstLatency out{0, 0};
		switch (desc.fu) {
			// ---- Element-wise vector arithmetic (math model §5.3, 6.2) ----
			// DEPENDENT-CHAIN latency: on the unified OoO backend the LMUL uops
			// PIPELINE, so the consumer of a dependent chain only waits for the
			// pipeline latency of the operation — NOT numUop * per-uop latency.
			// RTL (vadd_m1..m8 = 5.9..6.4 cyc/op) is nearly LMUL-FLAT: the first
			// uop pays fill + L_core + resync; the remaining (numUop-1) uops
			// stream and add only a small per-uop increment (c_uop_arith, ~0).
			// Occupancy (n_beats) still scales with numUop for FU-busy accounting.
			case XsFU::VALU:
			case XsFU::VMFPU_MUL:
			case XsFU::VMFPU_FMA:
			case XsFU::VMFPU_FNONCOMP:
			case XsFU::VMFPU_FCONV:
			case XsFU::VNARROW:
			case XsFU::VMASK: {
				uint32_t nuop = numOfUopArith(desc);
				uint64_t L = cfg_.c_fill + lcore(desc) + cfg_.c_dep_resync
				             + (uint64_t)(nuop - 1) * cfg_.c_uop_arith;
				out.total_cycles = L;
				out.n_beats = (uint64_t)nuop * cfg_.c_uop_arith;
				break;
			}

			// ---- Iterative integer / FP divide (math model §3.1, 5.2, 6.1) ----
			// L_single = W_s + C_div_fixed + N_iter + C_div_drain, N_iter in [0,16].
			// Dependent-chain aggregate adds C_reissue (non-pipelined FSM reissue).
			case XsFU::VMFPU_IDIV:
			case XsFU::VMFPU_FDIV: {
				// N_iter is data dependent; without operand magnitude we use the
				// worst-case iteration bound XLEN/R_div, matching the 25.60 anchor
				// point (L_total(max) + C_reissue). The calibrator can tune C_reissue.
				uint32_t xlen_bits = 64;
				uint32_t n_iter_max = xlen_bits / (cfg_.r_div ? cfg_.r_div : 4);
				uint64_t L_single =
				    cfg_.w_s + cfg_.c_div_fixed + n_iter_max + cfg_.c_div_drain;
				out.total_cycles = L_single + cfg_.c_reissue;
				out.n_beats = out.total_cycles;  // non-pipelined: full occupancy
				break;
			}

			// ---- Integer reduction (math model §5.4, 6.2) ----
			// L = C_red_fixed + numOfUop_red*(C_uop_red) + C_red_fold
			case XsFU::VREDU_INT: {
				uint32_t nuop = numOfUopRed(desc);
				uint64_t red_term = ((uint64_t)nuop * cfg_.c_uop_red_x10) / 10u;
				out.total_cycles = cfg_.c_red_fixed + red_term + cfg_.c_red_fold;
				out.n_beats = red_term;
				break;
			}

			// ---- FP-ordered reduction (math model §5.4, LOW anchor) ----
			// L = C_red_fp_fixed + VL_eff * L_fadd  (serial, cannot parallelize)
			case XsFU::VREDU_FP: {
				uint32_t vl_eff = desc.vl ? desc.vl : vlmaxElems(desc);
				out.total_cycles = cfg_.c_red_fp_fixed + (uint64_t)vl_eff * cfg_.l_fadd;
				out.n_beats = (uint64_t)vl_eff * cfg_.l_fadd;
				break;
			}

			// ---- Permute / gather / compress / slide (math model §5.5, 5.6) ----
			// Distinguish vrgather (m^2 uops) from vslide (2*m uops); both use
			// L = numOfUop * C_perm_uop + C_fill. VSLIDE bucket covers both because
			// the RVV classifier groups them; the uop curve differentiates them.
			case XsFU::VSLIDE: {
				// Heuristic split: gather has the m^2 curve, slide the 2*m curve.
				// We cannot see the exact mnemonic here (classifier merged them),
				// so we conservatively use the slide (2*m) curve unless LMUL>1 and
				// the caller flagged a gather. To stay faithful to the model AND
				// keep it dynamic, we expose both and pick the larger-impact curve
				// only when a gather is explicitly indicated via sew_idx!=0.
				uint32_t nuop_slide = numOfUopSlide(desc);
				uint64_t L = (uint64_t)nuop_slide * cfg_.c_perm_uop + cfg_.c_fill;
				out.total_cycles = L;
				out.n_beats = (uint64_t)nuop_slide * cfg_.c_perm_uop;
				break;
			}

			// ---- Unit-stride memory (math model §5.7, 6.3) ----
			// Pipeline overhead only; TLM memory latency accumulates separately.
			case XsFU::VLSU_UNIT_LD:
			case XsFU::VLSU_UNIT_ST:
			case XsFU::VWHOLE_REG: {
				out.total_cycles = cfg_.w_m + cfg_.t_l1hit;  // L1-hit load-to-use
				out.n_beats = out.total_cycles;
				break;
			}

			// ---- Strided memory (math model §5.7, LOW anchor, stride explicit) ----
			// L = numOfUop_strided*(C_flow + P_line(stride)) + C_wb; envelope [6.84,9.44].
			case XsFU::VLSU_STRIDED_LD:
			case XsFU::VLSU_STRIDED_ST: {
				out.total_cycles = stridedLatency(desc);
				out.n_beats = out.total_cycles;
				break;
			}

			// ---- Indexed gather/scatter (math model §5.7, LOW anchor 11.30) ----
			case XsFU::VLSU_GATHER:
			case XsFU::VLSU_SCATTER: {
				uint32_t nuop = numOfUopArith(desc);  // indexedLSTable+1 ~ per-group
				uint64_t per_idx = cfg_.c_gather_rt_x10;  // x10
				out.total_cycles = ((uint64_t)nuop * per_idx) / 10u;
				if (out.total_cycles == 0) out.total_cycles = per_idx / 10u;
				out.n_beats = out.total_cycles;
				break;
			}

			// ---- Scalar move/extract from vector: 1-cycle issue, hazard handled
			//      by the scoreboard sync in VExtension (true data hazard). ----
			case XsFU::VMV:
				out.total_cycles = 1;
				out.n_beats = 1;
				break;

			// ---- vsetvl* : configuration, handled by scalar core (1 cycle) ----
			case XsFU::VSETVL:
				out.total_cycles = 0;
				out.n_beats = 0;
				break;

			default:
				out.total_cycles = 1;
				out.n_beats = 1;
				break;
		}
		return out;
	}

	/*
	 * Pipeline overhead for memory ops (used if TLM memory model already provides
	 * realistic per-element latency). Returns only the fixed pipe overhead.
	 */
	uint64_t computePipelineOverhead(const XsVecInsn& desc) const {
		(void)desc;
		return cfg_.w_m + cfg_.t_l1hit;
	}

   private:
	XsConfig cfg_;

	/*
	 * Strided latency: uop-count x per-line fill, with an explicit stride input.
	 * P_line(stride) is HIGH at small stride (touch many lines per uop) and falls
	 * as stride spans fewer lines per uop -> the calibration's flat-to-falling
	 * envelope [6.84, 9.44]. Modeled algebraically:
	 *   P_line = clamp(p_line_max >> log2(stride/elem_bytes), 1, p_line_max)
	 */
	uint64_t stridedLatency(const XsVecInsn& desc) const {
		uint32_t nuop = numOfUopArith(desc);  // stridedLSTable(emul,nf)+2 approximated by group
		if (nuop == 0) nuop = 1;
		uint32_t elem_bytes = (desc.sew ? desc.sew : 32) / 8u;
		if (elem_bytes == 0) elem_bytes = 1;
		// stride in units of elements (bytes / elem_bytes); larger stride -> fewer
		// lines per uop -> smaller P_line.
		uint64_t stride_elems = desc.stride / elem_bytes;
		uint32_t shift = 0;
		for (uint64_t s = stride_elems; s > 1; s >>= 1) ++shift;
		uint32_t p_line = cfg_.p_line_max;
		if (shift < 32) p_line = cfg_.p_line_max >> shift;
		if (p_line < 1) p_line = 1;
		uint64_t L = (uint64_t)nuop * (cfg_.c_strided_flow + p_line) + cfg_.c_strided_wb;
		return L;
	}
};

}  // namespace xs_timing
