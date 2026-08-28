import re

insert_code = """
    void inject_cycles(uint64_t n) {
        dbbcache.inject_cycles(n * prop_clock_cycle_period.value());
    }
    uint64_t get_cycle_count() {
        return dbbcache.get_cycle_counter_raw() / prop_clock_cycle_period.value();
    }
"""

for fname in ["vp/src/core/rv32/iss_ctemplate.h", "vp/src/core/rv64_cheriv9/iss_ctemplate.h"]:
    with open(fname, "r") as f:
        content = f.read()

    public_idx = content.find("public:")
    if public_idx != -1 and "inject_cycles" not in content:
        content = content[:public_idx + 7] + insert_code + content[public_idx + 7:]

    with open(fname, "w") as f:
        f.write(content)
