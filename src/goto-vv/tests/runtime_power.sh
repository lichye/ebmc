#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
: > empty
source="$base/runtime_power.sv"

run() {
  local label=$1 rc=0
  shift
  printf '%q ' "$@" > "$label.command"
  printf '\n' >> "$label.command"
  "$@" > "$label.log" 2>&1 || rc=$?
  printf '%s\n' "$rc" > "$label.rc"
}
status() { [[ $(cat "$1.rc") == "$2" ]]; }
for top in runtime_power runtime_power_x; do
  for route in typed explicit; do
    label="$top-$route"
    flags=()
    if [[ $route == explicit ]]; then flags=(-DEXPLICIT_REFERENCE); fi
    run "$label-compile" "$vv" compile "$label.vv" -mfcu --threads 1 --top "$top" "${flags[@]}" "$source"
    status "$label-compile" 0
    run "$label-bind" "$vv" constrain "$label.vv" empty "$label-run.vv"
    status "$label-bind" 0
  done
done
grep -Eq '^"xxxxxxxx" [0-9]+ [0-9]+' runtime_power_x-typed.vv
run readback "$eb" --show-symbol-table runtime_power-typed-run.vv
status readback 0
grep -Fq 'Verilog::$root.runtime_power.bias_value' readback.log

check() {
  local label=$1 id=$2 depth=$3 rc=$4 verdict=$5 frame=$6
  shift 6
  run "$label" "$eb" "${solver_flags[@]}" --bound "$depth" --property "$id" --trace --vcd "$label.vcd" "$@"
  status "$label" "$rc"
  grep -F "[$id]" "$label.log" | grep -Fq ": $verdict"
  if [[ $frame != - ]]; then
    grep -Eq "(finite bad frame $frame has an infinite extension by totality|witness loop .*equal endpoints.*bad frame $frame$)" "$label.log"
  fi
}
solvers=(minisat)
if [[ ${E33_TEST_YICES:-0} == 1 ]]; then solvers+=(yices); fi
for solver in "${solvers[@]}"; do
  solver_flags=()
  if [[ $solver == yices ]]; then solver_flags=(--yices); fi
  for route in typed explicit native-reference; do
    top=runtime_power
    input=("$top-$route-run.vv")
    if [[ $route == native-reference ]]; then
      input=(--systemverilog --top "$top" -D EXPLICIT_REFERENCE "$source")
    fi
    for prop in p_bias p_unsigned p_signed p_short p_signed_result p_context p_high; do
      check "$solver-$route-$prop" "$top.$prop" 4 0 'PROVED up to bound 4' - "${input[@]}"
    done
    check "$solver-$route-p_wrong" "$top.p_wrong" 4 10 REFUTED 3 "${input[@]}"

    top=runtime_power_x
    input=("$top-$route-run.vv")
    if [[ $route == native-reference ]]; then
      input=(--systemverilog --top "$top" -D EXPLICIT_REFERENCE "$source")
    fi
    for prop in p_known p_known_overflow p_known_extension; do
      check "$solver-$route-$prop" "$top.$prop" 1 0 'PROVED up to bound 1' - "${input[@]}"
    done
    for prop in x_not_zero x_not_ones z_not_zero z_not_ones constant_not_zero constant_not_ones extension_not_zero extension_not_ones; do
      check "$solver-$route-$prop" "$top.$prop" 0 10 REFUTED 0 "${input[@]}"
    done
  done
done

invalid="$base/runtime_power_invalid.sv"
for probe in variable_base other_base signed_negative_base truncated_base; do
  run "$probe" "$vv" compile "$probe.vv" -mfcu --threads 1 --top "power_$probe" "$invalid"
  status "$probe" 2
  test ! -e "$probe.vv"
  grep -Eq 'runtime_power_invalid\.sv:[0-9]+:[0-9]+: unsupported binary Power' "$probe.log"
done
printf '%s\n' 'Runtime powers of two PASS'
