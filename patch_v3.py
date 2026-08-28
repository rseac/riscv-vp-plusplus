with open("vp/src/core/common/v.h", "r") as f:
    content = f.read()

# find void prepInstr(bool require_not_off, bool require_vill, bool is_fp, Operation::OpId opId) {
idx = content.find("void prepInstr(bool require_not_off, bool require_vill, bool is_fp, Operation::OpId opId) {")

if idx != -1:
    overload = "void prepInstr(bool require_not_off, bool require_vill, bool is_fp) {\\n\\t\\thas_current_opId_ = false;\\n\\t\\tprepInstr(require_not_off, require_vill, is_fp, (Operation::OpId)0);\\n\\t}\\n\\n\\t"
    content = content[:idx] + overload.replace('\\n', '\n').replace('\\t', '\t') + content[idx:]
    
    content = content.replace("current_opId_ = opId;", "current_opId_ = opId;\\n\\t\\thas_current_opId_ = true;".replace('\\n', '\n').replace('\\t', '\t'))

with open("vp/src/core/common/v.h", "w") as f:
    f.write(content)
