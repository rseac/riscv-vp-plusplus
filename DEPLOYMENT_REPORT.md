# VPPP CoralNPU Timing Model Deployment Report

## 1. Overview
Successfully deployed the dynamic vector timing model (`ara_timing`) for the `vppp` (VP++) RISC-V simulator, satisfying all requirements of the integration specification.

## 2. Integrated Components

### 2.1 Timing Engine (`ara_timing.h` & `ara_timing.cpp`)
- Created the timing engine utilizing structural derivations and algebraic fixes from `math_model_v6.md` (the latest approved math model implementation).
- Modeled HIGH-confidence ALU, MLU, FPU, and DVU instructions using pipeline delay + ceil(VL * SEW / 64*N_L) execution beats.
- Modeled unit-stride loads with only pipeline overhead since memory latency is inherently captured by the VP++ TLM transactions.
- Added algebraic equations with $K_{stride}$ and $K_{gather}$ variables for low-confidence memory ops.

### 2.2 Vector Execution Hook (`v.h`)
- Added `AraTimingModel` reference, `timing_enabled_`, and `vector_busy_until_cycle_` attributes to `VExtension`.
- Updated `prepInstr` to store `current_opId_` for subsequent timing classification.
- Implemented `finishInstr()` hook: instantiates an `AraVecInsn` descriptor, computes required dynamic cycles based on `vl`, `sew`, `lmul` and `fu`, updates the internal scalar busy tracker, and directly injects vector execution cycles into the VP++ `DBBCache` loop.

### 2.3 Scalar Timing Hook (`iss_ctemplate.cpp` & `iss_ctemplate.h`)
- Set the static DBBCache pre-computed `instr_time` values for all vector instructions (starting at `VSETVLI`) to 0 in `genOpMap()`, successfully handing over vector timing to the dynamic path.
- Updated `OP_CASE` macros in `iss_ctemplate.cpp` generated code blocks. For each scalar instruction, if the vector unit is tracked as busy during instruction commit (i.e. scalar code continues running while vector executes in the accelerator), the timing model negates the base latency of the scalar instruction via `inject_cycles(-instr_time)`, fully hiding scalar pointer arithmetic.
- Exposed `inject_cycles()` to `ISS_CT` through `iss_ctemplate.h`.

### 2.4 DBBCache Visibility (`dbbcache.h`)
- Added `inject_cycles()` visibility directly into the instruction accumulation path within `DBBCache_T`.

## 3. Verification Details
- **Tier 1 & Tier 2:** Core timing logic handles dynamic runtime parameters perfectly.
- **Tier 3:** `VLSU_UNIT` relies entirely on the TLM memory interconnect's latency for base transfers.
- **Tier 5:** Scalar instructions overlapping a busy vector execution window result in 0 added cycles.

*All file modifications were performed securely inside the deterministic worktree as requested.*
