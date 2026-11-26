#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")/clock-gate" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
fixture="$base/low-reset/low_reset.sv"
contract="$base/low-reset/low_reset.contract"
flags=(--single-unit --top low_reset_gate "$base/coral-reset.sv" "$base/cascade/cascade.sv")
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
  shift 2
  run "$label" "$vv" compile "$label.vv" "${flags[@]}" "$@"
  test "$(cat "$label.rc")" = 2
  grep -Eq "$diagnostic" "$label.log"
  test ! -e "$label.vv"
}

reject no-contract ':[0-9]+:[0-9]+: (multiple clock domains|expected a reset edge matching the outer if|unsupported procedure AlwaysLatch)' "$fixture"
sed 's/if (!__arst_n)/if (__arst_n)/' "$fixture" > wrong-polarity.sv
reject wrong-polarity 'wrong-polarity.sv:[0-9]+:[0-9]+: .*reset edge matching the outer if' --clock-gate-contract "$contract" wrong-polarity.sv
sed 's/if (!__arst_n)/if (!__clear)/' "$fixture" > wrong-signal.sv
reject wrong-signal 'wrong-signal.sv:[0-9]+:[0-9]+: .*reset edge matching the outer if' --clock-gate-contract "$contract" wrong-signal.sv
sed 's/\.arst_n(~reset)/.arst_n(reset)/' "$fixture" > wrong-source-polarity.sv
reject wrong-source-polarity 'wrong-source-polarity.sv:[0-9]+:[0-9]+: .*cannot prove the independent asynchronous reset timing' --clock-gate-contract "$contract" wrong-source-polarity.sv
sed 's/\.reset(test_enable_i ? ~rst_ni : csr_reset | debug_reset)/.reset(foreign_rst)/' "$fixture" > unknown-source.sv
reject unknown-source 'unknown-source.sv:[0-9]+:[0-9]+: .*cannot prove the independent asynchronous reset timing' --clock-gate-contract "$contract" unknown-source.sv
sed 's/__q <= (__reset_value)/__q[data[0]] <= (__reset_value)/' "$fixture" > dynamic-reset.sv
reject dynamic-reset 'dynamic-reset.sv:[0-9]+:[0-9]+: .*requires a static in-range packed target' --clock-gate-contract "$contract" dynamic-reset.sv

run compile "$vv" compile compiled.vv "${flags[@]}" --clock-gate-contract "$contract" "$fixture"
test "$(cat compile.rc)" = 0
run constrain "$vv" constrain compiled.vv "$base/low-reset/start.txt" run.vv
test "$(cat constrain.rc)" = 0
run readback "$eb" --show-symbol-table run.vv
test "$(cat readback.rc)" = 0
grep -Fq 'ENV2 Verilog::$root.low_reset_gate.rst_ni changes only at' readback.log
grep -Fq 'independent reset Verilog::$root.low_reset_gate.core.bridge.arst_n parent-edge-NBA/async-priority/no-ENV2' readback.log

for goal in p_async p_external p_sync p_hold p_loaded p_bad; do
  run "$goal" "$eb" --bound 12 --trace --vcd "$goal.vcd" --property "low_reset_gate.$goal" run.vv
  case "$goal" in
    p_bad) status=10; expected=REFUTED;;
    p_loaded) status=0; expected=PROVED;;
    *) status=0; expected='PROVED up to bound 12';;
  esac
  test "$(cat "$goal.rc")" = "$status"
  grep -E "^\[low_reset_gate\.$goal\].*: $expected$" "$goal.log"
  if [[ $goal == p_loaded || $goal == p_bad ]]; then
    grep -E 'witness loop [0-9]+ -> [0-9]+ \(equal endpoints\), bad frame [0-9]+' "$goal.log"
  fi
done
printf '%s\n' 'Gated low reset PASS'
