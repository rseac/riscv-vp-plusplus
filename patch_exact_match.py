import re

with open("vp/src/core/common/v.h", "r") as f:
    content = f.read()

content = content.replace("default:\\n                return AraFU::VALU;", "default:\\n                return AraFU::UNKNOWN;")

with open("vp/src/core/common/v.h", "w") as f:
    f.write(content)
