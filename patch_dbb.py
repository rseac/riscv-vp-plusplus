with open("vp/src/core/common/dbbcache.h", "r") as f:
    content = f.read()

# Add inject_cycles to DBBCacheDummy_T
dummy_idx = content.find("uint64_t get_cycle_counter_raw() {")
if dummy_idx != -1:
    dummy_code = "void inject_cycles(uint64_t n) { cycle_counter_raw += n; }\n\t"
    content = content[:dummy_idx] + dummy_code + content[dummy_idx:]

# Add inject_cycles to DBBCache_T
dbb_idx = content.find("uint64_t get_cycle_counter_raw() {", dummy_idx + 100)
if dbb_idx != -1:
    dbb_code = "void inject_cycles(uint64_t n) { cycle_counter_raw += n; }\n\t"
    content = content[:dbb_idx] + dbb_code + content[dbb_idx:]

# Also, update dbbcache.h for Step 3: Zero scalar instr_time while vector is busy in dbbcache.h
# In fetch_decode:
fetch_decode_find = "cycle_counter_raw += this->opMap[opId].instr_time;"
fetch_decode_replace = "cycle_counter_raw += this->opMap[opId].instr_time;"
# Wait, this is DBBCacheDummy_T. Let's patch decode_update_entry instead!

with open("vp/src/core/common/dbbcache.h", "w") as f:
    f.write(content)
