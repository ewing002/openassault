#!/bin/sh
# run.sh out.c <hexaddr | c<hexaddr> ...>  -  headless Ghidra decompile of re/xa3dfx.img
# GHIDRA = Ghidra install folder; the project (gh/proj) is created on first use with the image imported as raw x86:LE:32
out=$1; shift
here=$(cd "$(dirname "$0")" && pwd)
GHIDRA=${GHIDRA:-$HOME/tools/ghidra_11.1.2_PUBLIC}
[ -d "$here/proj.rep" ] || "$GHIDRA/support/analyzeHeadless" "$here" proj -import "$here/../xa3dfx.img" -processor x86:LE:32:default -noanalysis
timeout 590 "$GHIDRA/support/analyzeHeadless" "$here" proj -process xa3dfx.img -noanalysis -readOnly -scriptPath "$here" -postScript Decomp.java "$out" "$@" 2>&1 | grep -E "ERROR|Exception" | head -5
