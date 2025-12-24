#pragma once

#include <cstdint>
#include <iostream>

#include "core/common/instr.h"

template <typename Core>
void configure_vector_latencies_ara(Core& core, uint64_t clock_period) {
	auto& opMap = core.opMap;

	std::cout << "[ISS] Using ARA Vector Latencies" << std::endl;
	for (int i = Operation::OpId::VSETVLI; i <= Operation::OpId::VMV_NR_R_V; ++i) {
		// Default Base Latency for unknown or mask ops (2 cycles: 1 pipeline + 1 throughput)
		opMap[i].instr_time = 2 * clock_period;

		switch (i) {
			// Configuration
			case Operation::OpId::VSETVLI:
			case Operation::OpId::VSETIVLI:
			case Operation::OpId::VSETVL:
				opMap[i].instr_time = 1 * clock_period;
				break;

			// Memory Operations (Loads) - 4 cycles (3 pipeline + 1 cache)
			case Operation::OpId::VLE8_V:
			case Operation::OpId::VLE16_V:
			case Operation::OpId::VLE32_V:
			case Operation::OpId::VLE64_V:
			case Operation::OpId::VLM_V:
			case Operation::OpId::VLSE8_V:
			case Operation::OpId::VLSE16_V:
			case Operation::OpId::VLSE32_V:
			case Operation::OpId::VLSE64_V:
			case Operation::OpId::VLUXEI8_V:
			case Operation::OpId::VLUXEI16_V:
			case Operation::OpId::VLUXEI32_V:
			case Operation::OpId::VLUXEI64_V:
			case Operation::OpId::VLOXEI8_V:
			case Operation::OpId::VLOXEI16_V:
			case Operation::OpId::VLOXEI32_V:
			case Operation::OpId::VLOXEI64_V:
			case Operation::OpId::VLSEG2E8_V:
			case Operation::OpId::VLSEG2E16_V:
			case Operation::OpId::VLSEG2E32_V:
			case Operation::OpId::VLSEG2E64_V:
				opMap[i].instr_time = 4 * clock_period;
				break;

			// Memory Operations (Stores) - 3 cycles (2 pipeline + 1 cache)
			case Operation::OpId::VSE8_V:
			case Operation::OpId::VSE16_V:
			case Operation::OpId::VSE32_V:
			case Operation::OpId::VSE64_V:
			case Operation::OpId::VSM_V:
			case Operation::OpId::VSSE8_V:
			case Operation::OpId::VSSE16_V:
			case Operation::OpId::VSSE32_V:
			case Operation::OpId::VSSE64_V:
			case Operation::OpId::VSUXEI8_V:
			case Operation::OpId::VSUXEI16_V:
			case Operation::OpId::VSUXEI32_V:
			case Operation::OpId::VSUXEI64_V:
			case Operation::OpId::VSOXEI8_V:
			case Operation::OpId::VSOXEI16_V:
			case Operation::OpId::VSOXEI32_V:
			case Operation::OpId::VSOXEI64_V:
			case Operation::OpId::VSSEG2E8_V:
			case Operation::OpId::VSSEG2E16_V:
			case Operation::OpId::VSSEG2E32_V:
			case Operation::OpId::VSSEG2E64_V:
				opMap[i].instr_time = 3 * clock_period;
				break;

			// Integer ALU (2 cycles) - Covered by default, but listing for completeness/clarity if needed.
			// Default handles VADD, VMIN, etc.

			// Integer Multiply - 2 cycles (Conservative, EW8 is 1)
			case Operation::OpId::VMUL_VV:
			case Operation::OpId::VMUL_VX:
			case Operation::OpId::VMULH_VV:
			case Operation::OpId::VMULH_VX:
			case Operation::OpId::VMULHU_VV:
			case Operation::OpId::VMULHU_VX:
			case Operation::OpId::VMULHSU_VV:
			case Operation::OpId::VMULHSU_VX:
			case Operation::OpId::VMACC_VV:
			case Operation::OpId::VMACC_VX:
			case Operation::OpId::VNMSAC_VV:
			case Operation::OpId::VNMSAC_VX:
			case Operation::OpId::VMADD_VV:
			case Operation::OpId::VMADD_VX:
			case Operation::OpId::VNMSUB_VV:
			case Operation::OpId::VNMSUB_VX:
			case Operation::OpId::VWMUL_VV:
			case Operation::OpId::VWMUL_VX:
			case Operation::OpId::VWMULU_VV:
			case Operation::OpId::VWMULU_VX:
			case Operation::OpId::VWMULSU_VV:
			case Operation::OpId::VWMULSU_VX:
			case Operation::OpId::VWMACC_VV:
			case Operation::OpId::VWMACC_VX:
			case Operation::OpId::VWMACCU_VV:
			case Operation::OpId::VWMACCU_VX:
			case Operation::OpId::VWMACCSU_VV:
			case Operation::OpId::VWMACCSU_VX:
				opMap[i].instr_time = 2 * clock_period;
				break;

			// Integer Divide - Variable (2-65 cycles), avg 32
			case Operation::OpId::VDIV_VV:
			case Operation::OpId::VDIV_VX:
			case Operation::OpId::VDIVU_VV:
			case Operation::OpId::VDIVU_VX:
			case Operation::OpId::VREM_VV:
			case Operation::OpId::VREM_VX:
			case Operation::OpId::VREMU_VV:
			case Operation::OpId::VREMU_VX:
				opMap[i].instr_time = 32 * clock_period;
				break;

			// Floating Point ALU - 6 cycles (Conservative EW64 max)
			case Operation::OpId::VFADD_VV:
			case Operation::OpId::VFADD_VF:
			case Operation::OpId::VFSUB_VV:
			case Operation::OpId::VFSUB_VF:
			case Operation::OpId::VFRSUB_VF:
			case Operation::OpId::VFMUL_VV:
			case Operation::OpId::VFMUL_VF:
			case Operation::OpId::VFMACC_VV:
			case Operation::OpId::VFMACC_VF:
			case Operation::OpId::VFNMACC_VV:
			case Operation::OpId::VFNMACC_VF:
			case Operation::OpId::VFMSAC_VV:
			case Operation::OpId::VFMSAC_VF:
			case Operation::OpId::VFNMSAC_VV:
			case Operation::OpId::VFNMSAC_VF:
			case Operation::OpId::VFMADD_VV:
			case Operation::OpId::VFMADD_VF:
			case Operation::OpId::VFNMADD_VV:
			case Operation::OpId::VFNMADD_VF:
			case Operation::OpId::VFWADD_VV:
			case Operation::OpId::VFWADD_VF:
			case Operation::OpId::VFWADD_WV:
			case Operation::OpId::VFWADD_WF:
			case Operation::OpId::VFWSUB_VV:
			case Operation::OpId::VFWSUB_VF:
			case Operation::OpId::VFWSUB_WV:
			case Operation::OpId::VFWSUB_WF:
			case Operation::OpId::VFWMUL_VV:
			case Operation::OpId::VFWMUL_VF:
				opMap[i].instr_time = 6 * clock_period;
				break;

			// Floating Point Div/Sqrt - 4 cycles
			case Operation::OpId::VFDIV_VV:
			case Operation::OpId::VFDIV_VF:
			case Operation::OpId::VFRDIV_VF:
			case Operation::OpId::VFSQRT_V:
				opMap[i].instr_time = 4 * clock_period;
				break;
		}
	}
}
