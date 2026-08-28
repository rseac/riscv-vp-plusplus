with open("vp/src/core/rv64/iss_ctemplate.h", "r") as f:
    content = f.read()

content = content.replace("dbbcache.cycle_counter_raw += n * prop_clock_cycle_period.value();", "dbbcache.inject_cycles(n * prop_clock_cycle_period.value());")

with open("vp/src/core/rv64/iss_ctemplate.h", "w") as f:
    f.write(content)
