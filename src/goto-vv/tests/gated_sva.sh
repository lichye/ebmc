#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")/clock-gate" && pwd)
fixture="$base/gated-sva/arbiter.sv"
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"

run() {
  local label=$1 status=0
  shift
  printf '%q ' "$@" > "$label.command"
  printf '\n' >> "$label.command"
  "$@" > "$label.log" 2>&1 || status=$?
  printf '%s\n' "$status" > "$label.rc"
}
status() { test "$(cat "$1.rc")" = "$2"; }
flags=(-mfcu --threads 1 --top gated_sva)
contract=(--clock-gate-contract "$base/gated-sva/arbiter.contract")
files=("$base/coral-reset.sv" "$fixture")
build() {
  local label=$1
  shift
  run "$label-compile" "$vv" compile "$label.vv" "${flags[@]}" "${contract[@]}" "$@" "${files[@]}"
  status "$label-compile" 0
  run "$label-bind" "$vv" constrain "$label.vv" "$base/gated-sva/start.txt" "$label-run.vv"
  status "$label-bind" 0
}
check() {
  local label=$1 model=$2 property=$3 code=$4 verdict=$5 frame=$6
  shift 6
  run "$label" "$eb" --trace --vcd "$label.vcd" --bound 12 --property "$property" "$@" "$model-run.vv"
  status "$label" "$code"
  grep -F "[$property]" "$label.log" | grep -Fq ": $verdict"
  if [[ $frame != - ]]; then
    grep -E "bad frame $frame([^0-9]|$)" "$label.log"
  fi
}

for fault in 0 1 2 3 4; do
  build "gated-$fault" "-GFAULT=$fault"
  build "reference-$fault" "-GFAULT=$fault" -DEXPLICIT_REFERENCE
done
build unguarded -DUNGUARDED_CONTROL

run readback "$eb" --show-symbol-table gated-0-run.vv
status readback 0
for property in hot_one gnt0 gnt1 gnt_idx; do
  grep -Fq "Verilog::\$root.gated_sva.client.$property" readback.log
done
grep -Eq 'arbiter\.sv line [0-9]+ column [0-9]+' readback.log
for field in source_clock gate_output gate_enable failure_action; do
  grep -Fq "\"$field\"" gated-0-run.vv
done
grep -Fq 'Verilog::$root.gated_sva.child.__clock_gate_enable' readback.log
run trans "$eb" --show-properties gated-0-run.vv
status trans 0
test "$(grep -Ec '^gated_sva\.clock_gate_ENV[12]:' trans.log)" = 2
test "$(grep -Ec '^gated_sva\.client\.(hot_one|gnt0|gnt1|gnt_idx):' trans.log)" = 4

top=gated_sva
fault_properties=(unused hot_one gnt0 gnt1 gnt_idx)
for backend in minisat yices; do
  backend_flags=()
  if [[ $backend == yices ]]; then backend_flags=(--yices); fi
  for mode in gated reference; do
    for property in hot_one gnt0 gnt1 gnt_idx; do
      check "$backend-$mode-safe-$property" "$mode-0" "$top.client.$property" 0 'PROVED up to bound 12' - "${backend_flags[@]}"
    done
    for fault in 1 2 3 4; do
      property=${fault_properties[$fault]}
      check "$backend-$mode-fault-$property" "$mode-$fault" "$top.client.$property" 10 REFUTED 8 "${backend_flags[@]}"
    done
  done
  for goal in c_parent_closed c_child_closed c_zero c_first_one; do
    case "$goal" in
      c_parent_closed) frame=4 ;;
      c_child_closed) frame=7 ;;
      c_zero) frame=6 ;;
      c_first_one) frame=8 ;;
    esac
    check "$backend-$goal" gated-0 "$top.$goal" 0 PROVED "$frame" "${backend_flags[@]}"
  done
  check "$backend-root-control" unguarded "$top.root_control" 10 REFUTED 4 "${backend_flags[@]}"
  check "$backend-onehot0-scalar" gated-0 "$top.native_onehot0_scalar" 0 'PROVED up to bound 12' - "${backend_flags[@]}"
  check "$backend-onehot0-constant" gated-0 "$top.native_onehot0_constant" 0 'PROVED up to bound 12' - "${backend_flags[@]}"
done

reject() {
  local label=$1 diagnostic=$2
  shift 2
  run "$label" "$vv" compile "$label.vv" "${flags[@]}" "$@" "${files[@]}"
  status "$label" 2
  test ! -e "$label.vv"
  grep -E "arbiter.sv:[0-9]+:[0-9]+: ($diagnostic)" "$label.log"
}
run no-contract "$vv" compile no-contract.vv "${flags[@]}" "${files[@]}"
status no-contract 2
test ! -e no-contract.vv
grep -E '(coral-reset|arbiter)\.sv:[0-9]+:[0-9]+: (expected a reset edge matching the outer if|multiple clock domains|unsupported assertion action)$' no-contract.log
test "$(grep -c ': ASSUMED$' minisat-gated-safe-hot_one.log)" = 2
for kind in NEXT DELAY PAST REPEAT; do
  reject "$kind" 'clock gate contract requires a single-sample assertion$' "${contract[@]}" "-DCROSS_CLOCK_$kind"
done
for kind in ASSUME NEGEDGE; do
  reject "$kind" 'clock gate contract requires an unguarded posedge assert$' "${contract[@]}" "-DGATED_$kind"
done
reject EFFECT 'unsupported assertion action$' "${contract[@]}" -DGATED_EFFECT
printf '%s\n' 'Gated single-sample assertions PASS'
