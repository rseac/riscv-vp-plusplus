import re

with open("vp/src/core/rv64/iss_ctemplate.cpp", "r") as f:
    lines = f.readlines()

current_op = None
for i, line in enumerate(lines):
    m = re.search(r'OP_CASE\(([A-Z0-9_]+)\)', line)
    if m:
        current_op = m.group(1)
    
    if 'v_ext.prepInstr' in line:
        if 'opId' in line:
            line = line.replace('opId', f'Operation::OpId::{current_op}')
            lines[i] = line

with open("vp/src/core/rv64/iss_ctemplate.cpp", "w") as f:
    f.writelines(lines)
