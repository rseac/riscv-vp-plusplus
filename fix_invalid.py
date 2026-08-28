with open("vp/src/core/common/v.h", "r") as f:
    content = f.read()

content = content.replace("Operation::OpId current_opId_ = Operation::OpId::INVALID;", "Operation::OpId current_opId_;\\n    bool has_current_opId_ = false;")
content = content.replace("void prepInstr(bool require_not_off, bool require_vill, bool is_fp, Operation::OpId opId = Operation::OpId::INVALID) {\\n\\t\\tcurrent_opId_ = opId;", "void prepInstr(bool require_not_off, bool require_vill, bool is_fp, Operation::OpId opId) {\\n\\t\\tcurrent_opId_ = opId;\\n\\t\\thas_current_opId_ = true;")
content = content.replace("void prepInstr(bool require_not_off, bool require_vill, bool is_fp) {", "void prepInstr(bool require_not_off, bool require_vill, bool is_fp) {\\n\\t\\thas_current_opId_ = false;")
content = content.replace("current_opId_ != Operation::OpId::INVALID", "has_current_opId_")

with open("vp/src/core/common/v.h", "w") as f:
    f.write(content)
