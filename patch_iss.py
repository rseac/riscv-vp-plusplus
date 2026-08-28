import re

# PATCH iss_ctemplate.h
with open("vp/src/core/rv64/iss_ctemplate.h", "r") as f:
    content = f.read()

# find public section to insert our methods
public_idx = content.find("public:")
insert_code = """
    void inject_cycles(uint64_t n) {
        dbbcache.cycle_counter_raw += n * prop_clock_cycle_period.value();
    }
    uint64_t get_cycle_count() {
        return dbbcache.get_cycle_counter_raw() / prop_clock_cycle_period.value();
    }
"""

content = content[:public_idx + 7] + insert_code + content[public_idx + 7:]

with open("vp/src/core/rv64/iss_ctemplate.h", "w") as f:
    f.write(content)


# PATCH iss_ctemplate.cpp
with open("vp/src/core/rv64/iss_ctemplate.cpp", "r") as f:
    content = f.read()

# Disable vector static timing
vector_disable = """		opMap[opId].instr_time = instr_clock_cycles * prop_clock_cycle_period.value(); /* ps */
	}

	for (unsigned int opId = Operation::OpId::VSETVLI; opId < Operation::OpId::NUMBER_OF_OPERATIONS; ++opId) {
		opMap[opId].instr_time = 0;  // Vector timing handled dynamically
	}"""
content = content.replace("		opMap[opId].instr_time = instr_clock_cycles * prop_clock_cycle_period.value(); /* ps */\n	}", vector_disable)

# Update prepInstr calls for vector ops
content = content.replace("v_ext.prepInstr(true, true, false);", "v_ext.prepInstr(true, true, false, opId);")
content = content.replace("v_ext.prepInstr(true, false, false);", "v_ext.prepInstr(true, false, false, opId);")
content = content.replace("v_ext.prepInstr(true, true, true);", "v_ext.prepInstr(true, true, true, opId);")
content = content.replace("v_ext.prepInstr(true, false, true);", "v_ext.prepInstr(true, false, true, opId);")

with open("vp/src/core/rv64/iss_ctemplate.cpp", "w") as f:
    f.write(content)
