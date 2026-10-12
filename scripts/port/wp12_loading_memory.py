"""Keep the complete host loading arena descriptor after the long64 transformation."""
from pathlib import Path
import sys
p = Path(sys.argv[1])
s = p.read_text(encoding="utf-8")
old = "    *(LoadingMemoryWords *) memory = *(LoadingMemoryWords *) &info->memory;"
if s.count(old) != 1:
    raise SystemExit("nowload.cpp: expected one loading arena copy anchor")
p.write_text(s.replace(old, "    *memory = info->memory;"), encoding="utf-8")
