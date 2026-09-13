#!/usr/bin/env python3
# SPDX-License-Identifier: MIT
"""Apply one explicit AVR-to-ARM integer-promotion adaptation to a generated TU."""
from pathlib import Path
import sys
source,output=map(Path,sys.argv[1:]);data=source.read_bytes()
old=b'if (currentMilli - lastMilli >= 1000)'
new=b'if (static_cast<uint16_t>(currentMilli - lastMilli) >= 1000)'
if data.count(old)!=1:raise SystemExit('ArdDrivin timer patch no longer matches pinned upstream')
result=('#line 1 "'+str(source.resolve())+'"\n').encode()+data.replace(old,new)
if not output.exists() or output.read_bytes()!=result:output.write_bytes(result)
