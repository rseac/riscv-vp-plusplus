/*
 * XSTop (XiangShan Kunminghu) Vector Timing Model — implementation unit.
 *
 * PROFILE B (unified_ooo). See xs_timing.h for the full model documentation and
 * the equation-to-math-model mapping (build/XSTop/models/math_model_approved.md §6).
 *
 * The timing engine is implemented header-inline in xs_timing.h so the hot
 * finishInstr() hook can inline computeCycles(). This translation unit exists to
 * satisfy the build system (vp/src/core/common/CMakeLists.txt) and to host any
 * out-of-line diagnostics.
 */

#include "xs_timing.h"

namespace xs_timing {

// Model identification string, useful for provenance logging / smoke tests.
const char* kXSTopTimingModelVersion =
    "XSTop-unified_ooo Profile-B v2 (math_model_approved.md v2, 2_lanes_128_vlen)";

}  // namespace xs_timing
