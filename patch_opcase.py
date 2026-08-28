with open("vp/src/core/rv64/iss_ctemplate.cpp", "r") as f:
    content = f.read()

opcase_find = """#define OP_CASE(_op)                                                                                                 \\
	OP_LABEL_OP(_op)                                                                                                 \\
	    : static struct op_label_entry OP_LABEL_ENTRY_OP(_op)                                                        \\
	          __attribute__((used, section(OP_LABLE_ENTRIES_SEC_STR))) = {Operation::OpId::_op, &&OP_LABEL_OP(_op)}; \\
	stats.inc_op(Operation::OpId::_op);"""

opcase_replace = """static inline bool is_vector_op(Operation::OpId opId) {
    return opId >= Operation::OpId::VSETVLI && opId < Operation::OpId::NUMBER_OF_OPERATIONS;
}

#define OP_CASE(_op)                                                                                                 \\
	OP_LABEL_OP(_op)                                                                                                 \\
	    : static struct op_label_entry OP_LABEL_ENTRY_OP(_op)                                                        \\
	          __attribute__((used, section(OP_LABLE_ENTRIES_SEC_STR))) = {Operation::OpId::_op, &&OP_LABEL_OP(_op)}; \\
	stats.inc_op(Operation::OpId::_op); \\
	if (v_ext.timingEnabled() && !is_vector_op(Operation::OpId::_op)) { \\
		uint64_t now = dbbcache.get_cycle_counter_raw() / prop_clock_cycle_period.value(); \\
		if (v_ext.isVectorBusy(now)) { \\
			dbbcache.inject_cycles(- (int64_t)opMap[Operation::OpId::_op].instr_time); \\
		} \\
	}"""

content = content.replace(opcase_find, opcase_replace)

with open("vp/src/core/rv64/iss_ctemplate.cpp", "w") as f:
    f.write(content)
