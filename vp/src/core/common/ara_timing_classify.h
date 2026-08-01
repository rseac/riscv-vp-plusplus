#pragma once

/*
 * AraXL FU Classification for VP++ Operation::OpId
 *
 * Maps every RVV instruction's OpId to the appropriate ARA functional unit.
 * Used by the timing model hook in VExtension::finishInstr().
 */

#include "ara_timing.h"
#include "instr.h"

namespace ara_timing {

/*
 * Classify a VP++ Operation::OpId into an ARA functional unit type.
 * Returns AraFU::UNKNOWN for non-vector or unrecognized instructions.
 */
inline AraFU classifyFU(Operation::OpId opId) {
	switch (opId) {
		// --- VSETVL family ---
		case Operation::OpId::VSETVLI:
		case Operation::OpId::VSETIVLI:
		case Operation::OpId::VSETVL:
			return AraFU::VSETVL;

		// --- Unit-stride loads ---
		case Operation::OpId::VLE8_V:
		case Operation::OpId::VLE16_V:
		case Operation::OpId::VLE32_V:
		case Operation::OpId::VLE64_V:
		case Operation::OpId::VLM_V:
		case Operation::OpId::VLE8FF_V:
		case Operation::OpId::VLE16FF_V:
		case Operation::OpId::VLE32FF_V:
		case Operation::OpId::VLE64FF_V:
			return AraFU::VLSU_UNIT_LD;

		// --- Unit-stride stores ---
		case Operation::OpId::VSE8_V:
		case Operation::OpId::VSE16_V:
		case Operation::OpId::VSE32_V:
		case Operation::OpId::VSE64_V:
		case Operation::OpId::VSM_V:
			return AraFU::VLSU_UNIT_ST;

		// --- Strided loads ---
		case Operation::OpId::VLSE8_V:
		case Operation::OpId::VLSE16_V:
		case Operation::OpId::VLSE32_V:
		case Operation::OpId::VLSE64_V:
			return AraFU::VLSU_STRIDED_LD;

		// --- Strided stores ---
		case Operation::OpId::VSSE8_V:
		case Operation::OpId::VSSE16_V:
		case Operation::OpId::VSSE32_V:
		case Operation::OpId::VSSE64_V:
			return AraFU::VLSU_STRIDED_ST;

		// --- Indexed / Gather loads ---
		case Operation::OpId::VLUXEI8_V:
		case Operation::OpId::VLUXEI16_V:
		case Operation::OpId::VLUXEI32_V:
		case Operation::OpId::VLUXEI64_V:
		case Operation::OpId::VLOXEI8_V:
		case Operation::OpId::VLOXEI16_V:
		case Operation::OpId::VLOXEI32_V:
		case Operation::OpId::VLOXEI64_V:
			return AraFU::VLSU_GATHER;

		// --- Indexed / Scatter stores ---
		case Operation::OpId::VSUXEI8_V:
		case Operation::OpId::VSUXEI16_V:
		case Operation::OpId::VSUXEI32_V:
		case Operation::OpId::VSUXEI64_V:
		case Operation::OpId::VSOXEI8_V:
		case Operation::OpId::VSOXEI16_V:
		case Operation::OpId::VSOXEI32_V:
		case Operation::OpId::VSOXEI64_V:
			return AraFU::VLSU_SCATTER;

		// --- Whole register load/store ---
		case Operation::OpId::VL1RE8_V:
		case Operation::OpId::VL1RE16_V:
		case Operation::OpId::VL1RE32_V:
		case Operation::OpId::VL1RE64_V:
		case Operation::OpId::VS1R_V:
		case Operation::OpId::VL2RE8_V:
		case Operation::OpId::VL2RE16_V:
		case Operation::OpId::VL2RE32_V:
		case Operation::OpId::VL2RE64_V:
		case Operation::OpId::VS2R_V:
		case Operation::OpId::VL4RE8_V:
		case Operation::OpId::VL4RE16_V:
		case Operation::OpId::VL4RE32_V:
		case Operation::OpId::VL4RE64_V:
		case Operation::OpId::VS4R_V:
		case Operation::OpId::VL8RE8_V:
		case Operation::OpId::VL8RE16_V:
		case Operation::OpId::VL8RE32_V:
		case Operation::OpId::VL8RE64_V:
		case Operation::OpId::VS8R_V:
			return AraFU::VWHOLE_REG;

		// --- Integer ALU ---
		case Operation::OpId::VADD_VV:
		case Operation::OpId::VADD_VI:
		case Operation::OpId::VADD_VX:
		case Operation::OpId::VSUB_VV:
		case Operation::OpId::VSUB_VX:
		case Operation::OpId::VRSUB_VX:
		case Operation::OpId::VRSUB_VI:
		case Operation::OpId::VAND_VI:
		case Operation::OpId::VAND_VV:
		case Operation::OpId::VAND_VX:
		case Operation::OpId::VOR_VV:
		case Operation::OpId::VOR_VI:
		case Operation::OpId::VOR_VX:
		case Operation::OpId::VXOR_VV:
		case Operation::OpId::VXOR_VI:
		case Operation::OpId::VXOR_VX:
		case Operation::OpId::VSLL_VI:
		case Operation::OpId::VSLL_VV:
		case Operation::OpId::VSLL_VX:
		case Operation::OpId::VSRL_VV:
		case Operation::OpId::VSRL_VI:
		case Operation::OpId::VSRL_VX:
		case Operation::OpId::VSRA_VV:
		case Operation::OpId::VSRA_VI:
		case Operation::OpId::VSRA_VX:
		case Operation::OpId::VMINU_VV:
		case Operation::OpId::VMINU_VX:
		case Operation::OpId::VMIN_VV:
		case Operation::OpId::VMIN_VX:
		case Operation::OpId::VMAXU_VV:
		case Operation::OpId::VMAXU_VX:
		case Operation::OpId::VMAX_VV:
		case Operation::OpId::VMAX_VX:
		case Operation::OpId::VMERGE_VVM:
		case Operation::OpId::VMERGE_VXM:
		case Operation::OpId::VMERGE_VIM:
		case Operation::OpId::VMV_V_V:
		case Operation::OpId::VMV_V_X:
		case Operation::OpId::VMV_V_I:
		case Operation::OpId::VMSEQ_VV:
		case Operation::OpId::VMSEQ_VX:
		case Operation::OpId::VMSEQ_VI:
		case Operation::OpId::VMSNE_VV:
		case Operation::OpId::VMSNE_VX:
		case Operation::OpId::VMSNE_VI:
		case Operation::OpId::VMSLTU_VV:
		case Operation::OpId::VMSLTU_VX:
		case Operation::OpId::VMSLT_VV:
		case Operation::OpId::VMSLT_VX:
		case Operation::OpId::VMSLEU_VV:
		case Operation::OpId::VMSLEU_VX:
		case Operation::OpId::VMSLEU_VI:
		case Operation::OpId::VMSLE_VV:
		case Operation::OpId::VMSLE_VX:
		case Operation::OpId::VMSLE_VI:
		case Operation::OpId::VMSGTU_VX:
		case Operation::OpId::VMSGTU_VI:
		case Operation::OpId::VMSGT_VX:
		case Operation::OpId::VMSGT_VI:
		case Operation::OpId::VADC_VVM:
		case Operation::OpId::VADC_VXM:
		case Operation::OpId::VADC_VIM:
		case Operation::OpId::VMADC_VVM:
		case Operation::OpId::VMADC_VXM:
		case Operation::OpId::VMADC_VIM:
		case Operation::OpId::VMADC_VV:
		case Operation::OpId::VMADC_VX:
		case Operation::OpId::VMADC_VI:
		case Operation::OpId::VSBC_VVM:
		case Operation::OpId::VSBC_VXM:
		case Operation::OpId::VMSBC_VVM:
		case Operation::OpId::VMSBC_VXM:
		case Operation::OpId::VMSBC_VV:
		case Operation::OpId::VMSBC_VX:
		// Widening add/sub (still integer ALU path)
		case Operation::OpId::VWADD_VV:
		case Operation::OpId::VWADD_VX:
		case Operation::OpId::VWSUB_VV:
		case Operation::OpId::VWSUB_VX:
		case Operation::OpId::VWADDU_VV:
		case Operation::OpId::VWADDU_VX:
		case Operation::OpId::VWSUBU_VV:
		case Operation::OpId::VWSUBU_VX:
		case Operation::OpId::VWADD_WV:
		case Operation::OpId::VWADD_WX:
		case Operation::OpId::VWSUB_WV:
		case Operation::OpId::VWSUB_WX:
		case Operation::OpId::VWADDU_WV:
		case Operation::OpId::VWADDU_WX:
		case Operation::OpId::VWSUBU_WV:
		case Operation::OpId::VWSUBU_WX:
		// Zero/sign extension
		case Operation::OpId::VZEXT_VF2:
		case Operation::OpId::VSEXT_VF2:
		case Operation::OpId::VZEXT_VF4:
		case Operation::OpId::VSEXT_VF4:
		case Operation::OpId::VZEXT_VF8:
		case Operation::OpId::VSEXT_VF8:
		// Saturating add/sub (integer ALU)
		case Operation::OpId::VSADDU_VV:
		case Operation::OpId::VSADDU_VX:
		case Operation::OpId::VSADDU_VI:
		case Operation::OpId::VSADD_VV:
		case Operation::OpId::VSADD_VX:
		case Operation::OpId::VSADD_VI:
		case Operation::OpId::VSSUBU_VV:
		case Operation::OpId::VSSUBU_VX:
		case Operation::OpId::VSSUB_VV:
		case Operation::OpId::VSSUB_VX:
		// Averaging add/sub
		case Operation::OpId::VAADDU_VV:
		case Operation::OpId::VAADDU_VX:
		case Operation::OpId::VAADD_VV:
		case Operation::OpId::VAADD_VX:
		case Operation::OpId::VASUBU_VV:
		case Operation::OpId::VASUBU_VX:
		case Operation::OpId::VASUB_VV:
		case Operation::OpId::VASUB_VX:
		// Scaling shift right
		case Operation::OpId::VSSRL_VV:
		case Operation::OpId::VSSRL_VX:
		case Operation::OpId::VSSRL_VI:
		case Operation::OpId::VSSRA_VV:
		case Operation::OpId::VSSRA_VX:
		case Operation::OpId::VSSRA_VI:
			return AraFU::VALU;

		// --- Narrowing operations ---
		case Operation::OpId::VNSRL_WV:
		case Operation::OpId::VNSRL_WI:
		case Operation::OpId::VNSRL_WX:
		case Operation::OpId::VNSRA_WV:
		case Operation::OpId::VNSRA_WI:
		case Operation::OpId::VNSRA_WX:
		case Operation::OpId::VNCLIPU_WV:
		case Operation::OpId::VNCLIPU_WX:
		case Operation::OpId::VNCLIPU_WI:
		case Operation::OpId::VNCLIP_WV:
		case Operation::OpId::VNCLIP_WX:
		case Operation::OpId::VNCLIP_WI:
			return AraFU::VNARROW;

		// --- Integer Multiply ---
		case Operation::OpId::VMUL_VV:
		case Operation::OpId::VMUL_VX:
		case Operation::OpId::VMULH_VV:
		case Operation::OpId::VMULH_VX:
		case Operation::OpId::VMULHU_VV:
		case Operation::OpId::VMULHU_VX:
		case Operation::OpId::VMULHSU_VV:
		case Operation::OpId::VMULHSU_VX:
		case Operation::OpId::VSMUL_VV:
		case Operation::OpId::VSMUL_VX:
		// Widening multiply
		case Operation::OpId::VWMUL_VV:
		case Operation::OpId::VWMUL_VX:
		case Operation::OpId::VWMULU_VV:
		case Operation::OpId::VWMULU_VX:
		case Operation::OpId::VWMULSU_VV:
		case Operation::OpId::VWMULSU_VX:
		// Multiply-accumulate (goes through MUL pipe in VMFPU)
		case Operation::OpId::VMACC_VV:
		case Operation::OpId::VMACC_VX:
		case Operation::OpId::VNMSAC_VV:
		case Operation::OpId::VNMSAC_VX:
		case Operation::OpId::VMADD_VV:
		case Operation::OpId::VMADD_VX:
		case Operation::OpId::VNMSUB_VV:
		case Operation::OpId::VNMSUB_VX:
		case Operation::OpId::VWMACCU_VV:
		case Operation::OpId::VWMACCU_VX:
		case Operation::OpId::VWMACC_VV:
		case Operation::OpId::VWMACC_VX:
		case Operation::OpId::VWMACCSU_VV:
		case Operation::OpId::VWMACCSU_VX:
		case Operation::OpId::VWMACCUS_VX:
			return AraFU::VMFPU_MUL;

		// --- Integer Division ---
		case Operation::OpId::VDIVU_VV:
		case Operation::OpId::VDIVU_VX:
		case Operation::OpId::VDIV_VV:
		case Operation::OpId::VDIV_VX:
		case Operation::OpId::VREMU_VV:
		case Operation::OpId::VREMU_VX:
		case Operation::OpId::VREM_VV:
		case Operation::OpId::VREM_VX:
			return AraFU::VMFPU_IDIV;

		// --- FP Arithmetic (FMA path) ---
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
		case Operation::OpId::VFMSUB_VV:
		case Operation::OpId::VFMSUB_VF:
		case Operation::OpId::VFNMSUB_VV:
		case Operation::OpId::VFNMSUB_VF:
		// Widening FP
		case Operation::OpId::VFWADD_VV:
		case Operation::OpId::VFWADD_VF:
		case Operation::OpId::VFWSUB_VV:
		case Operation::OpId::VFWSUB_VF:
		case Operation::OpId::VFWADD_WV:
		case Operation::OpId::VFWADD_WF:
		case Operation::OpId::VFWSUB_WV:
		case Operation::OpId::VFWSUB_WF:
		case Operation::OpId::VFWMUL_VV:
		case Operation::OpId::VFWMUL_VF:
		case Operation::OpId::VFWMACC_VV:
		case Operation::OpId::VFWMACC_VF:
		case Operation::OpId::VFWNMACC_VV:
		case Operation::OpId::VFWNMACC_VF:
		case Operation::OpId::VFWMSAC_VV:
		case Operation::OpId::VFWMSAC_VF:
		case Operation::OpId::VFWNMSAC_VV:
		case Operation::OpId::VFWNMSAC_VF:
			return AraFU::VMFPU_FMA;

		// --- FP Div/Sqrt ---
		case Operation::OpId::VFDIV_VV:
		case Operation::OpId::VFDIV_VF:
		case Operation::OpId::VFRDIV_VF:
		case Operation::OpId::VFSQRT_V:
		case Operation::OpId::VFRSQRT7_V:
		case Operation::OpId::VFREC7_V:
			return AraFU::VMFPU_FDIV;

		// --- FP Non-computational ---
		case Operation::OpId::VFMIN_VV:
		case Operation::OpId::VFMIN_VF:
		case Operation::OpId::VFMAX_VV:
		case Operation::OpId::VFMAX_VF:
		case Operation::OpId::VFSGNJ_VV:
		case Operation::OpId::VFSGNJ_VF:
		case Operation::OpId::VFSGNJN_VV:
		case Operation::OpId::VFSGNJN_VF:
		case Operation::OpId::VFSGNJX_VV:
		case Operation::OpId::VFSGNJX_VF:
		case Operation::OpId::VMFEQ_VV:
		case Operation::OpId::VMFEQ_VF:
		case Operation::OpId::VMFNE_VV:
		case Operation::OpId::VMFNE_VF:
		case Operation::OpId::VMFLT_VV:
		case Operation::OpId::VMFLT_VF:
		case Operation::OpId::VMFLE_VV:
		case Operation::OpId::VMFLE_VF:
		case Operation::OpId::VMFGT_VF:
		case Operation::OpId::VMFGE_VF:
		case Operation::OpId::VFCLASS_V:
		case Operation::OpId::VFMERGE_VFM:
		case Operation::OpId::VFMV_V_F:
			return AraFU::VMFPU_FNONCOMP;

		// --- FP Conversion ---
		case Operation::OpId::VFCVT_XU_F_V:
		case Operation::OpId::VFCVT_X_F_V:
		case Operation::OpId::VFCVT_RTZ_XU_F_V:
		case Operation::OpId::VFCVT_RTZ_X_F_V:
		case Operation::OpId::VFCVT_F_XU_V:
		case Operation::OpId::VFCVT_F_X_V:
		case Operation::OpId::VFWCVT_XU_F_V:
		case Operation::OpId::VFWCVT_X_F_V:
		case Operation::OpId::VFWCVT_RTZ_XU_F_V:
		case Operation::OpId::VFWCVT_RTZ_X_F_V:
		case Operation::OpId::VFWCVT_F_XU_V:
		case Operation::OpId::VFWCVT_F_X_V:
		case Operation::OpId::VFWCVT_F_F_V:
		case Operation::OpId::VFNCVT_XU_F_W:
		case Operation::OpId::VFNCVT_X_F_W:
		case Operation::OpId::VFNCVT_RTZ_XU_F_W:
		case Operation::OpId::VFNCVT_RTZ_X_F_W:
		case Operation::OpId::VFNCVT_F_XU_W:
		case Operation::OpId::VFNCVT_F_X_W:
		case Operation::OpId::VFNCVT_F_F_W:
		case Operation::OpId::VFNCVT_ROD_F_F_W:
			return AraFU::VMFPU_FCONV;

		// --- Integer Reductions ---
		case Operation::OpId::VREDSUM_VS:
		case Operation::OpId::VREDMAXU_VS:
		case Operation::OpId::VREDMAX_VS:
		case Operation::OpId::VREDMINU_VS:
		case Operation::OpId::VREDMIN_VS:
		case Operation::OpId::VREDAND_VS:
		case Operation::OpId::VREDOR_VS:
		case Operation::OpId::VREDXOR_VS:
		case Operation::OpId::VWREDSUMU_VS:
		case Operation::OpId::VWREDSUM_VS:
			return AraFU::VREDU_INT;

		// --- FP Reductions ---
		case Operation::OpId::VFREDUSUM_VS:
		case Operation::OpId::VFREDOSUM_VS:
		case Operation::OpId::VFREDMAX_VS:
		case Operation::OpId::VFREDMIN_VS:
		case Operation::OpId::VFWREDUSUM_VS:
		case Operation::OpId::VFWREDOSUM_VS:
			return AraFU::VREDU_FP;

		// --- Mask operations ---
		case Operation::OpId::VMAND_MM:
		case Operation::OpId::VMNAND_MM:
		case Operation::OpId::VMANDN_MM:
		case Operation::OpId::VMXOR_MM:
		case Operation::OpId::VMOR_MM:
		case Operation::OpId::VMNOR_MM:
		case Operation::OpId::VMORN_MM:
		case Operation::OpId::VMXNOR_MM:
		case Operation::OpId::VCPOP_M:
		case Operation::OpId::VFIRST_M:
		case Operation::OpId::VMSBF_M:
		case Operation::OpId::VMSIF_M:
		case Operation::OpId::VMSOF_M:
		case Operation::OpId::VIOTA_M:
		case Operation::OpId::VID_V:
			return AraFU::VMASK;

		// --- Scalar moves ---
		case Operation::OpId::VMV_X_S:
		case Operation::OpId::VMV_S_X:
		case Operation::OpId::VFMV_F_S:
		case Operation::OpId::VFMV_S_F:
			return AraFU::VMV;

		// --- Slide ---
		case Operation::OpId::VSLIDEUP_VX:
		case Operation::OpId::VSLIDEUP_VI:
		case Operation::OpId::VSLIDEDOWN_VX:
		case Operation::OpId::VSLIDEDOWN_VI:
		case Operation::OpId::VSLIDE1UP_VX:
		case Operation::OpId::VFSLIDE1UP_VF:
		case Operation::OpId::VSLIDE1DOWN_VX:
		case Operation::OpId::VFSLIDE1DOWN_VF:
		case Operation::OpId::VRGATHER_VV:
		case Operation::OpId::VRGATHEREI16_VV:
		case Operation::OpId::VRGATHER_VX:
		case Operation::OpId::VRGATHER_VI:
		case Operation::OpId::VCOMPRESS_VM:
			return AraFU::VSLIDE;

		// --- Whole register move ---
		case Operation::OpId::VMV_NR_R_V:
			return AraFU::VWHOLE_REG;

		default:
			return AraFU::UNKNOWN;
	}
}

/*
 * Check if an OpId is a vector operation that should use the timing model.
 * Returns true for all RVV ops (from VSETVLI onwards).
 */
inline bool isVectorOp(Operation::OpId opId) {
	return opId >= Operation::OpId::VSETVLI && opId <= Operation::OpId::VMV_NR_R_V;
}

/*
 * Get the effective SEW for memory operations from the OpId suffix.
 * For example, VLE32_V uses 32-bit elements regardless of the current SEW.
 */
inline uint32_t getMemEEW(Operation::OpId opId) {
	switch (opId) {
		case Operation::OpId::VLE8_V:
		case Operation::OpId::VSE8_V:
		case Operation::OpId::VLSE8_V:
		case Operation::OpId::VSSE8_V:
		case Operation::OpId::VLE8FF_V:
			return 8;
		case Operation::OpId::VLE16_V:
		case Operation::OpId::VSE16_V:
		case Operation::OpId::VLSE16_V:
		case Operation::OpId::VSSE16_V:
		case Operation::OpId::VLE16FF_V:
			return 16;
		case Operation::OpId::VLE32_V:
		case Operation::OpId::VSE32_V:
		case Operation::OpId::VLSE32_V:
		case Operation::OpId::VSSE32_V:
		case Operation::OpId::VLE32FF_V:
			return 32;
		case Operation::OpId::VLE64_V:
		case Operation::OpId::VSE64_V:
		case Operation::OpId::VLSE64_V:
		case Operation::OpId::VSSE64_V:
		case Operation::OpId::VLE64FF_V:
			return 64;
		// Indexed ops use current SEW for data, index EEW from suffix
		case Operation::OpId::VLUXEI8_V:
		case Operation::OpId::VLOXEI8_V:
		case Operation::OpId::VSUXEI8_V:
		case Operation::OpId::VSOXEI8_V:
			return 8;
		case Operation::OpId::VLUXEI16_V:
		case Operation::OpId::VLOXEI16_V:
		case Operation::OpId::VSUXEI16_V:
		case Operation::OpId::VSOXEI16_V:
			return 16;
		case Operation::OpId::VLUXEI32_V:
		case Operation::OpId::VLOXEI32_V:
		case Operation::OpId::VSUXEI32_V:
		case Operation::OpId::VSOXEI32_V:
			return 32;
		case Operation::OpId::VLUXEI64_V:
		case Operation::OpId::VLOXEI64_V:
		case Operation::OpId::VSUXEI64_V:
		case Operation::OpId::VSOXEI64_V:
			return 64;
		default:
			return 0;  // Use current SEW
	}
}

}  // namespace ara_timing
