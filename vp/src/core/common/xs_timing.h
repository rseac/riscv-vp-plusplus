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
	VMFPU_FMA,       // FP fused multiply-add/sub (vfmacc, vfmadd, vfmsac, ...) ->
	                 // VFMA core (3). Reads its own destination as a third
	                 // operand for accumulation, unlike VMFPU_FADD below.
	VMFPU_FADD,      // FP simple 2-operand arithmetic (vfadd, vfsub, vfrsub,
	                 // vfmul) -> VFAlu-class core (1), split out from VMFPU_FMA
	                 // 2026-09. math_model_approved.md §6.2 already documents
	                 // vfadd_m1≈6.21 (L_core=1) vs vfmacc_m1≈8.13 (L_core=3) as
	                 // separately measured RTL values, but the classifier had
	                 // collapsed both into one XsFU::VMFPU_FMA category using
	                 // only the more expensive depth -- confirmed real,
	                 // ~33%/instr overestimate on any vfadd/vfmul-dominated
	                 // dependent chain (e.g. jacobi2d's 5-point stencil).
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
	// Structural FU-occupancy for a strided load/store (throughput, not
	// latency) -- feeds n_beats, enforced via v.h's fu_busy_until_. Distinct
	// from c_strided_flow (which feeds the single-shot LATENCY anchor) since
	// RTL-calibrated burst throughput and isolated-access latency differ.
	// RTL probe: real-RTL default (~1 elem = 1 numOfUopArith at LMUL=1).
	uint32_t c_strided_occupancy;
	// Indexed gather per-index round-trip (anchor 11.30 cyc/it).
	uint32_t c_gather_rt_x10;        // per-index (tlb+cache+merge) x10 ~= 113
	// Iterative divide.
	uint32_t c_div_fixed;   // FSM fixed hops = 6 (HIGH)
	uint32_t c_div_drain;   // s_finish->s_idle drain = 1 (HIGH)
	uint32_t r_div;         // quotient bits/iter (radix-16) = 4 (HIGH)
	uint32_t c_reissue;     // reissue hop between dependent divides (LOW anchor) ~= 2
	// FP divide/sqrt (VMFPU_FDIV) uses a typical, not worst-case, iteration
	// count -- see the VMFPU_FDIV case in computeCycles() for derivation.
	// Backed out from math_model_approved.md Sec 6.1's "fdiv_d~=14.69"
	// anchor (LOW confidence, empirical): 14.69 - c_reissue - w_s -
	// c_div_fixed - c_div_drain ~= 3.7 -> 4.
	uint32_t fdiv_n_iter_typical;

	// --- Memory (tunable, UNVALIDATED off-die) ---
	uint32_t t_l1hit;  // L1D hit load-to-use core depth = 3
	uint32_t tau_mem;  // miss/DRAM adder (tunable)
	// Sustained unit-stride vector-memory throughput cap (elements/cycle).
	// RTL-calibrated: real bandwidth sweep (see VLSU_UNIT_LD/ST case) measured
	// 1.00 cyc/elem steady-state for e32 unit-stride loads, independent of
	// working-set size (a real prefetcher hides capacity-driven misses for
	// this access pattern) and independent of LMUL/vl grouping.
	uint32_t peak_elems_per_cycle;

	XsConfig()
	    : vlen(128), dlen(128), nr_lanes(2),
	      enable_scalar_hiding(0),
	      w_s(2), w_v(3), w_m(1),
	      lcore_valu(1), lcore_vmul(2), lcore_vfma(3), lcore_vfalu(1),
	      lcore_vcvt(2), lcore_vipu(2),
	      c_dep_resync(4), c_uop_arith(2), c_fill(5),
	      c_uop_red_x10(12), c_red_fixed(2), c_red_fold(1), c_perm_uop(2),
	      l_fadd(3), c_red_fp_fixed(2),
	      c_strided_flow(3), p_line_max(6), c_strided_wb(1), c_strided_occupancy(4),
	      c_gather_rt_x10(113),
	      c_div_fixed(6), c_div_drain(1), r_div(4), c_reissue(2),
	      fdiv_n_iter_typical(4),
	      t_l1hit(3), tau_mem(10), peak_elems_per_cycle(1) {}
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
			case XsFU::VMFPU_FADD:
				return cfg_.lcore_vfalu;             // VFAlu-class = 1 (see VMFPU_FADD comment)
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
			case XsFU::VMFPU_FADD:
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

			// ---- Iterative integer divide (math model §3.1, 6.1) ----
			// L_single = W_s + C_div_fixed + N_iter + C_div_drain, N_iter in [0,16].
			// Dependent-chain aggregate adds C_reissue (non-pipelined FSM reissue).
			// N_iter is data dependent; without operand magnitude we use the
			// worst-case iteration bound XLEN/R_div, matching the SCALAR INTEGER
			// divide's own documented 25.60 anchor point (L_total(max) +
			// C_reissue). This worst-case treatment is only validated for
			// integer divide -- see VMFPU_FDIV below for why FP divide/sqrt
			// must NOT share this formula.
			case XsFU::VMFPU_IDIV: {
				uint32_t xlen_bits = 64;
				uint32_t n_iter_max = xlen_bits / (cfg_.r_div ? cfg_.r_div : 4);
				uint64_t L_single =
				    cfg_.w_s + cfg_.c_div_fixed + n_iter_max + cfg_.c_div_drain;
				out.total_cycles = L_single + cfg_.c_reissue;
				out.n_beats = out.total_cycles;  // non-pipelined: full occupancy
				break;
			}

			// ---- Iterative FP divide/sqrt (math model §6.1) ----
			// FIX 2026-09: this used to share VMFPU_IDIV's worst-case-N_iter
			// formula (n_iter_max=16, total~27), matching the SCALAR INTEGER
			// divide's 25.60 anchor -- but math_model_approved.md Sec 6.1's own
			// table documents a SEPARATE, much lower empirical anchor for FP
			// divide/sqrt specifically: "Scalar FP div/sqrt ... fdiv_d≈14.69",
			// not 25.60. Confirmed via real XiangShan RTL: a probe reproducing
			// somier's exact vfsqrt/vfdiv dependency chain (distance -> sum of
			// squares -> sqrt -> spring force -> divide, with real memory
			// loads/stores) measured the full chain at 858 cycles on RTL vs.
			// 1593 on this model (+85.7%, vs. somier's own +74.1% -- the two
			// track closely, confirming this chain IS the dominant driver).
			// Backing out the model's own 14.69 anchor: 14.69 - c_reissue(2) -
			// w_s(2) - c_div_fixed(6) - c_div_drain(1) ~= 3.7 -> use a typical
			// iteration count of 4 instead of the worst-case 16.
			case XsFU::VMFPU_FDIV: {
				uint32_t n_iter_typical = cfg_.fdiv_n_iter_typical;
				uint64_t L_single =
				    cfg_.w_s + cfg_.c_div_fixed + n_iter_typical + cfg_.c_div_drain;
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
			case XsFU::VWHOLE_REG: {
				out.total_cycles = cfg_.w_m + cfg_.t_l1hit;  // L1-hit load-to-use
				out.n_beats = out.total_cycles;
				break;
			}

			// FIX 2026-09: VLSU_UNIT_LD/ST used to share VWHOLE_REG's flat
			// "latency-only" formula (w_m + t_l1hit = 4 cycles), independent of
			// vl/LMUL. Confirmed via a real-RTL bandwidth sweep (unit-stride
			// vle32.v, e32/m4/vl=16, working set swept 4KB->256KB, well past
			// XiangShan's real 32KB L1D): measured cycles/element converged to
			// EXACTLY 1.00 (1081/1071/16441/65625 cycles for 64/64/1024/4096
			// vector loads of 16 elements each -> 1.06, 1.05, 1.00, 1.00
			// cyc/elem), REGARDLESS of working-set size -- ruling out a
			// cache-capacity/miss explanation (a real sequential prefetcher
			// hides that) in favor of a flat ~1 element/cycle sustained LSU
			// throughput cap that the old formula never charged for
			// high-LMUL / high-vl unit-stride ops.
			//
			// CORRECTION 2026-09: the sweep measures sustained ISSUE
			// THROUGHPUT (how often the LSU can accept a NEW request), not
			// per-request LATENCY (how long until ONE request's result is
			// ready for a dependent consumer) -- a deep, overlapped pipeline
			// can sustain 1 elem/cycle issue rate while any SINGLE result is
			// still ready in just the flat L1-hit depth. The mem-sweep probe
			// only measured the combined quantity because it reused the same
			// destination register every iteration (WAW-forced reissue), which
			// happens to make sustained throughput visible through the
			// latency channel. Originally this file set BOTH total_cycles
			// (latency, gates RAW consumers via vreg_ready_cycle_) AND
			// n_beats (occupancy, should gate same-FU reissue only) to the
			// inflated throughput value -- this incorrectly inflated the
			// LATENCY seen by genuinely-independent downstream consumers too.
			// Confirmed via pathfinder_agnostic (vle32.v -> vslide -> vmin ->
			// vadd -> vse32.v, a tight RAW chain): conflating the two flipped
			// its error from -49.6% (underestimate) to +49.6% (overestimate,
			// identical magnitude) -- the signature of charging one real cost
			// twice. Fix: keep total_cycles as the true flat latency; route
			// the throughput cap through n_beats only (enforced by v.h's
			// fu_busy_until_ occupancy tracker, which gates same-category
			// reissue without touching RAW-consumer latency).
			case XsFU::VLSU_UNIT_LD:
			case XsFU::VLSU_UNIT_ST: {
				uint64_t latency_term = cfg_.w_m + cfg_.t_l1hit;
				uint64_t elems = desc.vl ? desc.vl : 1;
				uint32_t peak = cfg_.peak_elems_per_cycle ? cfg_.peak_elems_per_cycle : 1;
				uint64_t throughput_term = (elems + peak - 1) / peak;  // ceil
				out.total_cycles = latency_term;       // latency: unchanged, real RAW cost
				out.n_beats = std::max(latency_term, throughput_term);  // occupancy only
				break;
			}

			// ---- Strided memory (decoupled VLSU; latency-band, not numUop-scaled) ----
			case XsFU::VLSU_STRIDED_LD:
			case XsFU::VLSU_STRIDED_ST: {
				out.total_cycles = stridedLatency(desc);          // use latency (flat-ish)
				out.n_beats = (uint64_t)numOfUopArith(desc) * cfg_.c_strided_occupancy; // occupancy
				break;
			}

			// ---- Indexed gather/scatter (decoupled VLSU; latency-band) ----
			case XsFU::VLSU_GATHER:
			case XsFU::VLSU_SCATTER: {
				// Consumer-visible latency is a use-latency band (~c_gather_rt), NOT
				// numUop * per-idx (that is occupancy). Decoupled gather engine overlaps
				// the per-index walk; RTL vgather ~11 cyc regardless of small LMUL.
				out.total_cycles = cfg_.c_gather_rt_x10 / 10u;
				if (out.total_cycles == 0) out.total_cycles = 1;
				out.n_beats = ((uint64_t)numOfUopArith(desc) * cfg_.c_gather_rt_x10) / 10u; // occupancy
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
	// Strided VLSU dependent-chain LATENCY (decoupled memory pipeline).
	// XSTop's VLSplit/VSSplit + merge buffers hide most per-uop/per-line cost, so the
	// consumer-visible latency is a use-latency band, only WEAKLY stride-dependent
	// (RTL vlse_stride8..1024 ~= 6.8..9.4 cyc, near-flat) — NOT numUop-scaled and NOT
	// steeply stride-scaled. Model as: base use latency + small stride adder; occupancy
	// (n_beats) still carries the uop count for FU-busy accounting.
	uint64_t stridedLatency(const XsVecInsn& desc) const {
		uint32_t elem_bytes = (desc.sew ? desc.sew : 32) / 8u;
		if (elem_bytes == 0) elem_bytes = 1;
		uint64_t stride_elems = desc.stride / elem_bytes;
		// base use latency = unit-stride L1-hit path (W_m + T_L1hit); add a small,
		// saturating stride term (0..p_line_max) — larger stride -> at most +p_line_max,
		// matching the RTL's shallow, bounded strided band (decoupled units cap it).
		uint64_t base = (uint64_t)cfg_.w_m + cfg_.t_l1hit + cfg_.c_strided_flow;
		uint32_t stride_add = 0;
		if (stride_elems > 1) {
			// grows slowly (log) and saturates at p_line_max; NOT linear in stride
			for (uint64_t s = stride_elems; s > 1 && stride_add < cfg_.p_line_max; s >>= 1)
				++stride_add;
		}
		return base + stride_add + cfg_.c_strided_wb;
	}
};

}  // namespace xs_timing
