#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
front=$(cd -- "$(dirname -- "$0")/.." && pwd)
vv=${VV_BIN:-$front/build/goto-vv}
ebmc=${EBMC:-$front/../ebmc/ebmc}
source_file="$front/tests/constant_power.sv"
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
: >empty

"$vv" compile constant.vv --single-unit --top constant_power "$source_file" >compile.log 2>&1
"$vv" constrain constant.vv empty checked.vv >constrain.log 2>&1
"$ebmc" --undriven seq --show-properties checked.vv >properties.log 2>&1

status=0
"$ebmc" --undriven seq --bound 2 --trace checked.vv >check.log 2>&1 || status=$?
[[ $status == 10 ]]
for goal in p_default p_override p_sign p_width p_exponent; do
    grep -E "^\\[constant_power\\.$goal\\].*: PROVED up to bound 2$" check.log
done
grep -E '^\[constant_power\.p_wrong_child\].*: REFUTED$' check.log
grep -Eq '^constant_power\.p_wrong_child: .*bad frame 0( |$)' check.log

for top in power_runtime_exponent power_runtime_base power_procedural_local; do
    status=0
    "$vv" compile "$top.vv" --single-unit --top "$top" "$source_file" >"$top.log" 2>&1 || status=$?
    [[ $status == 2 && ! -e "$top.vv" ]]
    grep -E 'constant_power\.sv:[0-9]+:[0-9]+: unsupported binary Power$' "$top.log"
done

echo 'Constant power PASS'
