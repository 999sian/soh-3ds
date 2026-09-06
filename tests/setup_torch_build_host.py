#!/usr/bin/env python3
"""Build the real setup adapter against an existing host Torch build."""
from pathlib import Path
import subprocess
root=Path(__file__).resolve().parents[1]
b=root/'third_party/shipwright/build-host'
t=root/'third_party/shipwright/torch'
cmd=['c++','-std=c++20','-O2','-DOOT_SUPPORT','-DPORT_VERSION_ENDIANNESS','-DSPDLOG_FMT_EXTERNAL','-DSPDLOG_COMPILED_LIB','-DYAML_CPP_STATIC_DEFINE']
for p in [root/'src',t,t/'src',t/'lib',b/'_deps/yaml-cpp-src/include']:
 cmd+=['-I',str(p)]
cmd += [str(root/'tests/setup_torch_extract.cpp'),str(root/'src/setup/torch_adapter.cpp'),str(b/'torch/libtorch.a'),str(b/'_deps/yaml-cpp-build/libyaml-cpp.a'),'-lspdlog','-lfmt',str(b/'_deps/tinyxml2-build/libtinyxml2.a'),str(b/'_deps/zlib-build/libz.a'),str(b/'torch/lib/n64graphics/libN64Graphics.a'),str(b/'torch/lib/binarytools/libBinaryTools.a'),'-o','/var/tmp/setup-torch-extract']
subprocess.run(cmd,check=True)
