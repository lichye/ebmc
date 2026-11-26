#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")/clock-gate" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
fixture="$base/static-slices/pipeline.sv"
contract="$base/static-slices/pipeline.contract"
flags=(-mfcu --threads 1 --top sliced "$base/coral-reset.sv" "$base/cascade/cascade.sv")
run() {
  local label=$1 status=0
  shift
  printf '%q ' "$@" > "$label.command"
  printf '\n' >> "$label.command"
  "$@" > "$label.log" 2>&1 || status=$?
  printf '%s\n' "$status" > "$label.rc"
}
reject() {
  local label=$1 diagnostic=$2
  run "$label" "$vv" compile "$label.vv" "${flags[@]}" --clock-gate-contract "$contract" "$label.sv"
  test "$(cat "$label.rc")" = 2
  grep -Eq "$label.sv:[0-9]+:[0-9]+: .*($diagnostic)" "$label.log"
  test ! -e "$label.vv"
}

sed -e 's/always_ff/always/' -e '/assign valid\[0\]/a\  always @(posedge clock) valid[1] <= in_valid;' "$fixture" > overlap.sv
reject overlap 'requires a uniquely driven reset target'
sed '0,/__q <= (__reset_value);/s//__q <= (__reset_value); __q <= (__reset_value);/' "$fixture" > duplicate.sv
reject duplicate 'overlapping reset assignments'
sed '/assign valid\[0\]/a\  logic [1:0] spare; always @(posedge clock or negedge arst_n) if (!arst_n) spare[load[0]] <= 0;' "$fixture" > dynamic.sv
reject dynamic 'requires a static in-range packed target'
sed '/assign valid\[0\]/a\  logic [1:0] spare; always @(posedge clock or negedge arst_n) if (!arst_n) spare[2] <= 0;' "$fixture" > range.sv
reject range 'requires a static in-range packed target|cannot refer to element .*index-oob'
sed '/assign valid\[0\]/a\  logic [1:0] spare; always @(posedge clock or negedge arst_n) if (!arst_n) spare[0] <= 0; else spare[1] <= 1;' "$fixture" > mismatched.sv
reject mismatched 'requires a matching constant reset|lacks a matching constant reset'

run compile "$vv" compile compiled.vv "${flags[@]}" --clock-gate-contract "$contract" "$fixture"
test "$(cat compile.rc)" = 0
run constrain "$vv" constrain compiled.vv "$base/static-slices/start.txt" run.vv
test "$(cat constrain.rc)" = 0
run readback "$eb" --show-symbol-table run.vv
test "$(cat readback.rc)" = 0
for slice in 'valid$clock_gate_raw$0$0' 'valid$clock_gate_raw$1$1' 'lane$clock_gate_raw$0$2' 'lane$clock_gate_raw$3$5'; do
  grep -Fq "sliced.stages.pipe.$slice" readback.log
done
if grep -Fq 'sliced.stages.pipe.valid$clock_gate_raw$2' readback.log ||
   grep -Fq 'sliced.stages.pipe.lane$clock_gate_raw$6' readback.log; then
  exit 1
fi

for goal in p_input p_reset p_loaded p_bad; do
  run "$goal" "$eb" --bound 12 --trace --vcd "$goal.vcd" --property "sliced.$goal" run.vv
  case "$goal" in
    p_bad) status=10; expected=REFUTED;;
    p_loaded) status=0; expected=PROVED;;
    *) status=0; expected='PROVED up to bound 12';;
  esac
  test "$(cat "$goal.rc")" = "$status"
  grep -E "^\[sliced\.$goal\].*: $expected$" "$goal.log"
  if [[ $goal == p_loaded || $goal == p_bad ]]; then
    grep -E 'witness loop [0-9]+ -> [0-9]+ \(equal endpoints\), bad frame [0-9]+' "$goal.log"
  fi
done
printf '%s\n' 'Gated static slices PASS'
