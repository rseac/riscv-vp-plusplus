#pragma once

#include <cstdint>
#include <iostream>

#include "core/common/instr.h"

template <typename Core>
void configure_vector_latencies_p600(Core& core, uint64_t clock_period) {
	auto& opMap = core.opMap;

	std::cout << "[ISS] Using SiFive P600 Vector Latencies" << std::endl;
	for (int i = Operation::OpId::VSETVLI; i <= Operation::OpId::VMV_NR_R_V; ++i) {
		// Default Base Latency for unknown or mask ops (1 cycle)
		opMap[i].instr_time = 1 * clock_period;

		switch (i) {
			// Configuration
			case Operation::OpId::VSETVLI:
			case Operation::OpId::VSETIVLI:
			case Operation::OpId::VSETVL:
				opMap[i].instr_time = 1 * clock_period;
				break;

			// Memory Operations (Loads/Stores) - ~8 cycles
			case Operation::OpId::VLE8_V:
			case Operation::OpId::VLE16_V:
			case Operation::OpId::VLE32_V:
			case Operation::OpId::VLE64_V:
			case Operation::OpId::VSE8_V:
			case Operation::OpId::VSE16_V:
			case Operation::OpId::VSE32_V:
			case Operation::OpId::VSE64_V:
			case Operation::OpId::VLM_V:
			case Operation::OpId::VSM_V:
			case Operation::OpId::VLSE8_V:
			case Operation::OpId::VLSE16_V:
			case Operation::OpId::VLSE32_V:
			case Operation::OpId::VLSE64_V:
			case Operation::OpId::VSSE8_V:
			case Operation::OpId::VSSE16_V:
			case Operation::OpId::VSSE32_V:
			case Operation::OpId::VSSE64_V:
			case Operation::OpId::VLUXEI8_V:
			case Operation::OpId::VLUXEI16_V:
			case Operation::OpId::VLUXEI32_V:
			case Operation::OpId::VLUXEI64_V:
			case Operation::OpId::VLOXEI8_V:
			case Operation::OpId::VLOXEI16_V:
			case Operation::OpId::VLOXEI32_V:
			case Operation::OpId::VLOXEI64_V:
			case Operation::OpId::VSUXEI8_V:
			case Operation::OpId::VSUXEI16_V:
			case Operation::OpId::VSUXEI32_V:
			case Operation::OpId::VSUXEI64_V:
			case Operation::OpId::VSOXEI8_V:
			case Operation::OpId::VSOXEI16_V:
			case Operation::OpId::VSOXEI32_V:
			case Operation::OpId::VSOXEI64_V:
				opMap[i].instr_time = 8 * clock_period;
				break;

			// Segmented Load/Stores - Higher latency (~12 cycles)
			case Operation::OpId::VLSEG2E8_V:
			case Operation::OpId::VLSEG2E16_V:
			case Operation::OpId::VLSEG2E32_V:
			case Operation::OpId::VLSEG2E64_V:
			case Operation::OpId::VSSEG2E8_V:
			case Operation::OpId::VSSEG2E16_V:
			case Operation::OpId::VSSEG2E32_V:
			case Operation::OpId::VSSEG2E64_V:
				opMap[i].instr_time = 12 * clock_period;
				break;

			// Integer ALU (Default 2)
			case Operation::OpId::VADD_VV:
			case Operation::OpId::VADD_VI:
			case Operation::OpId::VADD_VX:
			case Operation::OpId::VSUB_VV:
			case Operation::OpId::VSUB_VX:
			case Operation::OpId::VRSUB_VX:
			case Operation::OpId::VRSUB_VI:
			case Operation::OpId::VAND_VV:
			case Operation::OpId::VAND_VI:
			case Operation::OpId::VAND_VX:
			case Operation::OpId::VOR_VV:
			case Operation::OpId::VOR_VI:
			case Operation::OpId::VOR_VX:
			case Operation::OpId::VXOR_VV:
			case Operation::OpId::VXOR_VI:
			case Operation::OpId::VXOR_VX:
			case Operation::OpId::VSLL_VV:
			case Operation::OpId::VSLL_VI:
			case Operation::OpId::VSLL_VX:
			case Operation::OpId::VSRL_VV:
			case Operation::OpId::VSRL_VI:
			case Operation::OpId::VSRL_VX:
			case Operation::OpId::VSRA_VV:
			case Operation::OpId::VSRA_VI:
			case Operation::OpId::VSRA_VX:
			case Operation::OpId::VMIN_VV:
			case Operation::OpId::VMIN_VX:
			case Operation::OpId::VMAX_VV:
			case Operation::OpId::VMAX_VX:
			case Operation::OpId::VMINU_VV:
			case Operation::OpId::VMINU_VX:
			case Operation::OpId::VMAXU_VV:
			case Operation::OpId::VMAXU_VX:
			case Operation::OpId::VMERGE_VVM:
			case Operation::OpId::VMERGE_VXM:
			case Operation::OpId::VMERGE_VIM:
			case Operation::OpId::VMV_V_V:
			case Operation::OpId::VMV_V_X:
			case Operation::OpId::VMV_V_I:
				opMap[i].instr_time = 2 * clock_period;
				break;

			// Widening Integer ALU - 6 cycles
			case Operation::OpId::VWADD_VV:
			case Operation::OpId::VWADD_VX:
			case Operation::OpId::VWADD_WV:
			case Operation::OpId::VWADD_WX:
			case Operation::OpId::VWSUB_VV:
			case Operation::OpId::VWSUB_VX:
			case Operation::OpId::VWSUB_WV:
			case Operation::OpId::VWSUB_WX:
			case Operation::OpId::VWADDU_VV:
			case Operation::OpId::VWADDU_VX:
			case Operation::OpId::VWADDU_WV:
			case Operation::OpId::VWADDU_WX:
			case Operation::OpId::VWSUBU_VV:
			case Operation::OpId::VWSUBU_VX:
			case Operation::OpId::VWSUBU_WV:
			case Operation::OpId::VWSUBU_WX:
				opMap[i].instr_time = 6 * clock_period;
				break;

			// Integer Multiply - 6 cycles
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
				opMap[i].instr_time = 6 * clock_period;
				break;

			// Widening Multiply - 6 cycles
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
				opMap[i].instr_time = 6 * clock_period;
				break;

			// Integer Divide - High latency (assume 40)
			case Operation::OpId::VDIV_VV:
			case Operation::OpId::VDIV_VX:
			case Operation::OpId::VDIVU_VV:
			case Operation::OpId::VDIVU_VX:
			case Operation::OpId::VREM_VV:
			case Operation::OpId::VREM_VX:
			case Operation::OpId::VREMU_VV:
			case Operation::OpId::VREMU_VX:
				opMap[i].instr_time = 40 * clock_period;
				break;

			// Floating Point ALU - 6 cycles
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
				opMap[i].instr_time = 6 * clock_period;
				break;

			// Widening Float ALU - 6 cycles
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

			// Floating Point Div/Sqrt - ~25 cycles
			case Operation::OpId::VFDIV_VV:
			case Operation::OpId::VFDIV_VF:
			case Operation::OpId::VFRDIV_VF:
			case Operation::OpId::VFSQRT_V:
				opMap[i].instr_time = 25 * clock_period;
				break;
		}
	}
}
