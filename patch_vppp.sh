#!/bin/bash
set -e

# 1. Patch v.h
sed -i '/bool first_vec_in_window_ = true;/a \
\
\t// --- ARI Queue Scalar-Hiding: CVA6/Ara decoupling model ---\n\tuint64_t vector_time_ps_ = 0;\n\tbool scalar_hiding_enabled_ = true;\n\tvoid syncVector() {\n\t\tif (!scalar_hiding_enabled_) return;\n\t\tuint64_t now_ps = iss.dbbcache.get_cycle_counter_raw();\n\t\tif (now_ps < vector_time_ps_) {\n\t\t\tuint64_t stall_ps = vector_time_ps_ - now_ps;\n\t\t\tiss.dbbcache.add_cycle_counter_raw(stall_ps);\n\t\t}\n\t}' vp/src/core/common/v.h

sed -i 's/void initTimingModel(const ara_timing::AraConfig& cfg) {/void initTimingModel(const ara_timing::AraConfig\& cfg, bool enable_scalar_hiding = true) {\n\t\tscalar_hiding_enabled_ = enable_scalar_hiding;/' vp/src/core/common/v.h

sed -i '/uint64_t overhead = first_vec_in_window_ ? C_ISS_OVERHEAD : 0;/c \
\t\t\t\tif (scalar_hiding_enabled_) {\n\t\t\t\t\tuint64_t now_ps = iss.dbbcache.get_cycle_counter_raw();\n\t\t\t\t\tuint64_t period_ps = iss.get_clock_cycle_period_ps();\n\t\t\t\t\tuint64_t V_ps = cycles * period_ps;\n\t\t\t\t\tif (now_ps < vector_time_ps_) {\n\t\t\t\t\t\tuint64_t stall_ps = vector_time_ps_ - now_ps;\n\t\t\t\t\t\tiss.dbbcache.add_cycle_counter_raw(stall_ps);\n\t\t\t\t\t\tnow_ps = vector_time_ps_;\n\t\t\t\t\t}\n\t\t\t\t\tvector_time_ps_ = now_ps + V_ps;\n\t\t\t\t} else {\n\t\t\t\t\tuint64_t overhead = first_vec_in_window_ ? C_ISS_OVERHEAD : 0;' vp/src/core/common/v.h

sed -i '/first_vec_in_window_ = false;/c \
\t\t\t\t\tfirst_vec_in_window_ = false;\n\t\t\t\t\tif (cycles > overhead) {\n\t\t\t\t\t\tiss.ara_inject_cycles(cycles - overhead);\n\t\t\t\t\t}\n\t\t\t\t}' vp/src/core/common/v.h

# 2. Patch iss_ctemplate.h
# Add get_clock_cycle_period_ps() to ISS_CT
sed -i '/uint64_t _compute_and_get_current_cycles();/i \
\t__always_inline uint64_t get_clock_cycle_period_ps() {\n\t\treturn prop_clock_cycle_period.value();\n\t}' vp/src/core/rv64/iss_ctemplate.h

# Add ara_sync_vector() to ISS_CT
sed -i '/dbbcache.add_cycle_counter_raw(ps);/a \
\n\t__always_inline void ara_sync_vector(Operation::OpId opId) {\n\t\tswitch (opId) {\n\t\t\tcase Operation::OpId::CSRRS:\n\t\t\tcase Operation::OpId::CSRRW:\n\t\t\tcase Operation::OpId::CSRRC:\n\t\t\tcase Operation::OpId::CSRRSI:\n\t\t\tcase Operation::OpId::CSRRWI:\n\t\t\tcase Operation::OpId::CSRRCI:\n\t\t\tcase Operation::OpId::FENCE:\n\t\t\tcase Operation::OpId::FENCE_I:\n\t\t\tcase Operation::OpId::ECALL:\n\t\t\tcase Operation::OpId::EBREAK:\n\t\t\t\tv_ext.syncVector();\n\t\t\t\treturn;\n\t\t\tdefault:\n\t\t\t\tbreak;\n\t\t}\n\t}' vp/src/core/rv64/iss_ctemplate.h

# 3. Patch iss_ctemplate.cpp
sed -i 's/void \*opLabelPtr = dbbcache.fetch_decode(pc, instr);/{\n\t\tvoid *opLabelPtr = dbbcache.fetch_decode(pc, instr);\n\t\tOperation::OpId _decoded_opId = instr.decode_normal(ARCH, *isa_config);\n\t\tara_sync_vector(_decoded_opId);/g' vp/src/core/rv64/iss_ctemplate.cpp

sed -i 's/goto \*opLabelPtr;/goto *opLabelPtr;\n\t}/g' vp/src/core/rv64/iss_ctemplate.cpp

