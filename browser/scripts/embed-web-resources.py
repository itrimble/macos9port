#!/usr/bin/env python3
"""Embed our own static app resources; no host filesystem is exposed to pages."""
from pathlib import Path
import sys,hashlib
root,out=map(Path,sys.argv[1:])
files=sorted(p for p in root.rglob('*') if p.is_file())
version=hashlib.sha256(b''.join(p.relative_to(root).as_posix().encode()+p.read_bytes() for p in files)).hexdigest()[:16]
lines=['#include "web_app_resources.h"','bool WebAppResource(const std::string& path, std::string* body, std::string* mime) {']
types={'.html':'text/html','.js':'text/javascript','.json':'application/manifest+json','.svg':'image/svg+xml','.wasm':'application/wasm'}
for i,p in enumerate(files):
 name='/'+p.relative_to(root).as_posix()
 if name.endswith('/index.html'):name=name[:-10]
 data=p.read_bytes()
 if p.name == "sw.js":data=data.replace(b"os999-arena-v1", ("os999-arena-"+version).encode())
 lines += ['  if (path == "'+name+'") {', '    static const unsigned char data[] = {']
 lines += ['      '+','.join(str(b) for b in data[n:n+32])+',' for n in range(0,len(data),32)]
 lines += ['    };','    body->assign(reinterpret_cast<const char*>(data), sizeof data);','    *mime="'+types[p.suffix]+'"; return true;','  }']
lines+=['  return false;','}']
out.write_text('\n'.join(lines)+'\n')
