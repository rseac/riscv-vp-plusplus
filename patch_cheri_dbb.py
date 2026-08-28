with open("vp/src/core/common_cheriv9/dbbcache.h", "r") as f:
    content = f.read()

dummy_idx = content.find("uint64_t get_cycle_counter_raw() {")
if dummy_idx != -1:
    dummy_code = "__always_inline void inject_cycles(uint64_t n) { cycle_counter_raw += n; }\\n\\t"
    content = content[:dummy_idx] + dummy_code.replace('\\n', '\n').replace('\\t', '\t') + content[dummy_idx:]

dbb_idx = content.find("uint64_t get_cycle_counter_raw() {", dummy_idx + 100)
if dbb_idx != -1:
    dbb_code = "__always_inline void inject_cycles(uint64_t n) { cycle_counter_raw += n; }\\n\\t"
    content = content[:dbb_idx] + dbb_code.replace('\\n', '\n').replace('\\t', '\t') + content[dbb_idx:]

with open("vp/src/core/common_cheriv9/dbbcache.h", "w") as f:
    f.write(content)
