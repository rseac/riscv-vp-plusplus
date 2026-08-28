import re

with open("vp/src/core/common/v.h", "r") as f:
    content = f.read()

# Add include
content = content.replace('#include <cstring>', '#include <cstring>\n#include "ara_timing.h"')

# Add private members to VExtension
ext_find = "class VExtension {"
ext_replace = """class VExtension {
   private:
    uint64_t vector_busy_until_cycle_ = 0;
    bool timing_enabled_ = false;
    AraTimingModel* timing_model_ = nullptr;
    Operation::OpId current_opId_ = Operation::OpId::INVALID;

   public:
    void initTiming(bool enabled, uint32_t lanes, uint32_t vlen, uint32_t tau_mem, uint32_t c_sync) {
        timing_enabled_ = enabled;
        if (enabled) {
            timing_model_ = new AraTimingModel(lanes, vlen, tau_mem, c_sync);
        }
    }
    
    bool timingEnabled() const { return timing_enabled_; }
    bool isVectorBusy(uint64_t current_cycle) const {
        return timing_enabled_ && current_cycle < vector_busy_until_cycle_;
    }
    
    AraFU classifyFU(Operation::OpId opId) const {
        switch (opId) {
            case Operation::OpId::VADD_VV: case Operation::OpId::VADD_VI: case Operation::OpId::VADD_VX:
            case Operation::OpId::VSUB_VV: case Operation::OpId::VSUB_VX:
            case Operation::OpId::VAND_VV: case Operation::OpId::VOR_VV: case Operation::OpId::VXOR_VV:
                return AraFU::VALU;
            case Operation::OpId::VMUL_VV: case Operation::OpId::VMUL_VX:
            case Operation::OpId::VMULH_VV: case Operation::OpId::VMULHU_VV:
                return AraFU::VMFPU_MUL;
            case Operation::OpId::VFADD_VV: case Operation::OpId::VFADD_VF:
            case Operation::OpId::VFMUL_VV: case Operation::OpId::VFMACC_VV:
                return AraFU::VMFPU_FMA;
            case Operation::OpId::VDIV_VV: case Operation::OpId::VDIVU_VV:
                return AraFU::VDVU;
            case Operation::OpId::VLE8_V: case Operation::OpId::VLE16_V: case Operation::OpId::VLE32_V: case Operation::OpId::VLE64_V:
            case Operation::OpId::VSE8_V: case Operation::OpId::VSE16_V: case Operation::OpId::VSE32_V: case Operation::OpId::VSE64_V:
                return AraFU::VLSU_UNIT;
            case Operation::OpId::VLSE8_V: case Operation::OpId::VLSE16_V: case Operation::OpId::VLSE32_V: case Operation::OpId::VLSE64_V:
            case Operation::OpId::VSSE8_V: case Operation::OpId::VSSE16_V: case Operation::OpId::VSSE32_V: case Operation::OpId::VSSE64_V:
                return AraFU::VLSU_STRIDED;
            case Operation::OpId::VLOXEI8_V: case Operation::OpId::VLOXEI16_V: case Operation::OpId::VLOXEI32_V: case Operation::OpId::VLOXEI64_V:
                return AraFU::VLSU_GATHER;
            case Operation::OpId::VREDSUM_VS: case Operation::OpId::VREDMAX_VS: case Operation::OpId::VREDMIN_VS:
                return AraFU::VREDU_INT;
            case Operation::OpId::VFREDUSUM_VS: case Operation::OpId::VFREDOSUM_VS:
                return AraFU::VREDU_FP;
            default:
                return AraFU::VALU;
        }
    }
"""
content = content.replace(ext_find, ext_replace)

# Modify prepInstr
prep_find = "void prepInstr(bool require_not_off, bool require_vill, bool is_fp) {"
prep_replace = "void prepInstr(bool require_not_off, bool require_vill, bool is_fp, Operation::OpId opId = Operation::OpId::INVALID) {\n\t\tcurrent_opId_ = opId;"
content = content.replace(prep_find, prep_replace)

# Modify finishInstr
finish_find = """	void finishInstr(bool is_fp) {
		iss.csrs.vstart.reg.val = 0;

		if (is_fp) {
			iss.fp_finish_instr();
		}
	}"""
finish_replace = """	void finishInstr(bool is_fp) {
		iss.csrs.vstart.reg.val = 0;

		if (is_fp) {
			iss.fp_finish_instr();
		}

		if (timing_enabled_ && timing_model_ && current_opId_ != Operation::OpId::INVALID) {
			AraVecInsn desc = {
				classifyFU(current_opId_),
				iss.csrs.vl.reg.val,
				(uint32_t)(1 << (iss.csrs.vtype.reg.fields.vsew + 3)),
				(uint32_t)iss.csrs.vtype.reg.fields.vlmul,
				0
			};
			
			uint64_t cycles = 0;
			if (desc.fu == AraFU::VLSU_UNIT || desc.fu == AraFU::VLSU_STRIDED || desc.fu == AraFU::VLSU_GATHER) {
			    cycles = timing_model_->computePipelineOverhead(desc);
			} else {
			    cycles = timing_model_->computeCycles(desc);
			}
			
			uint64_t now = iss.get_cycle_count();
			vector_busy_until_cycle_ = now + cycles;
			iss.inject_cycles(cycles);
		}
	}"""
content = content.replace(finish_find, finish_replace)

with open("vp/src/core/common/v.h", "w") as f:
    f.write(content)

