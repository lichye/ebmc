#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")/clock-gate" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=${1:?output directory}
mkdir -p "$work"
cd "$work"
fixture="$base/cascade/cascade.sv"
contract="$base/cascade/cascade.contract"
flags=(--single-unit --top cascade "$base/coral-reset.sv")
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

reject no-contract '[0-9]+:[0-9]+: (multiple clock domains|expected a reset edge matching the outer if|unsupported procedure AlwaysLatch)' "$fixture"
sed '/^ClockGate /d' "$contract" > original.contract
reject rstsync-only '[0-9]+:[0-9]+: unsupported procedure AlwaysLatch' --clock-gate-contract original.contract "$fixture"
sed 's/^ClockGate cascade.child$/ClockGate cascade.missing/' "$contract" > wrong.contract
reject wrong-instance 'unbound ClockGate instance cascade.missing' --clock-gate-contract wrong.contract "$fixture"

sed 's/^  ClockGate child/  BadGate child/' "$fixture" > wrong-body.sv
cat >> wrong-body.sv <<'SV'
module BadGate(input clk_i, enable, te, output clk_o);
  logic en_latch;
  always_latch if (clk_i) en_latch = enable | te;
  assign clk_o = en_latch & clk_i;
endmodule
SV
reject wrong-body 'wrong-body.sv:[0-9]+:[0-9]+: clock gate contract requires an independent Coral v1 low-phase ClockGate body' --clock-gate-contract "$contract" wrong-body.sv
sed 's/child(.clk_i(parent_clock)/child(.clk_i(data)/' "$fixture" > outside.sv
reject outside 'outside.sv:[0-9]+:[0-9]+: .*outside the root domain' --clock-gate-contract "$contract" outside.sv
sed '/ClockGate child/s/.te(test_enable_i)/.te(data)/' "$fixture" > wrong-te.sv
reject wrong-te 'wrong-te.sv:[0-9]+:[0-9]+: clock gate contract ClockGate.te must bind ENV1' --clock-gate-contract "$contract" wrong-te.sv
sed '/^  wire parent_clock/a\  wire clock_data = child_clock;' "$fixture" > waveform-data.sv
reject waveform-data 'waveform-data.sv:[0-9]+:[0-9]+: .*does not support gated clock data use' --clock-gate-contract "$contract" waveform-data.sv
sed 's/csr_reset | debug_reset/csr_reset | data/' "$fixture" > unproved-reset.sv
reject unproved-reset 'unproved-reset.sv:[0-9]+:[0-9]+: .*cannot prove the independent asynchronous reset timing' --clock-gate-contract "$contract" unproved-reset.sv
sed "s/csr <= 2'b01;/csr <= 2'b00;/" "$fixture" > reset-can-pulse.sv
reject reset-can-pulse 'reset-can-pulse.sv:[0-9]+:[0-9]+: .*cannot prove the independent asynchronous reset timing' --clock-gate-contract "$contract" reset-can-pulse.sv
sed -e '/^  wire parent_clock/a\  reg racy_enable; always @(posedge clk_i) racy_enable = enable_i;' \
    -e '/ClockGate child/s/.enable(child_request)/.enable(racy_enable)/' "$fixture" > racy-enable.sv
reject racy-enable 'racy-enable.sv:[0-9]+:[0-9]+: .*cannot prove a sampled enable driver' --clock-gate-contract "$contract" racy-enable.sv

sed '/ClockGate child/s/.enable(child_request)/.enable(enable_i)/' "$fixture" > unbound-enable.sv
reject unbound-enable 'unbound-enable.sv:[0-9]+:[0-9]+: .*cannot prove a sampled enable driver' --clock-gate-contract "$contract" unbound-enable.sv

run compile "$vv" compile compiled.vv "${flags[@]}" --clock-gate-contract "$contract" "$fixture"
test "$(cat compile.rc)" = 0
grep -Fq 'ClockGate cascade.child input Verilog::$root.cascade.parent_clock output Verilog::$root.cascade.child_clock' compile.log
grep -Fq 'independent reset Verilog::$root.cascade.core.reset parent-edge-NBA/async-priority/no-ENV2' compile.log
run constrain "$vv" constrain compiled.vv "$base/cascade/start.txt" run.vv
test "$(cat constrain.rc)" = 0
run readback "$eb" --show-symbol-table run.vv
test "$(cat readback.rc)" = 0
grep -Fq 'ClockGate cascade.independent input Verilog::$root.cascade.clk_i' readback.log
grep -Fq 'independent reset Verilog::$root.cascade.core.reset parent-edge-NBA/async-priority/no-ENV2' readback.log

for goal in p_assertion p_independent p_child_hold p_release p_pulse p_bad; do
  run "$goal" "$eb" --bound 10 --trace --vcd "$goal.vcd" --property "cascade.$goal" run.vv
  case "$goal" in
    p_bad) status=10; expected='REFUTED';;
    p_release|p_pulse) status=0; expected='PROVED';;
    *) status=0; expected='PROVED up to bound 10';;
  esac
  test "$(cat "$goal.rc")" = "$status"
  grep -E "^\[cascade\.$goal\].*: $expected$" "$goal.log"
  if [[ $goal == p_release || $goal == p_pulse || $goal == p_bad ]]; then
    grep -E 'witness loop [0-9]+ -> [0-9]+ \(equal endpoints\), bad frame [0-9]+' "$goal.log"
  fi
done
