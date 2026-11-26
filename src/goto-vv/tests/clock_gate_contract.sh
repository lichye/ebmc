#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
front=$(cd -- "$(dirname -- "$0")/.." && pwd)
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
cases="$front/tests/clock-gate"
vv=${VV_BIN:-$front/build/goto-vv}
ebmc=${EBMC:-$front/../ebmc/ebmc}
run() {
    local step=$1 rc=0
    shift
    printf '%q ' "$@" > "$step.command"
    printf '\n' >> "$step.command"
    "$@" > "$step.log" 2>&1 || rc=$?
    printf '%s\n' "$rc" > "$step.exit"
}
src=("$cases/coral-reset.sv" "$cases/gated.sv")
flags=(-mfcu --threads 1 --top gated -DSYNTHESIS -DUSE_GENERIC)
run baseline "$vv" compile baseline.vv "${flags[@]}" "${src[@]}"
test "$(cat baseline.exit)" = 2
grep -Fq 'multiple clock domains' baseline.log
test ! -e baseline.vv
run compile "$vv" compile compiled.vv --clock-gate-contract "$cases/gated.contract" "${flags[@]}" "${src[@]}"
test "$(cat compile.exit)" = 0
grep -Fq 'coral-w4-env1-env2.v1 (temporary) ENV1' compile.log
run constrain "$vv" constrain compiled.vv "$cases/start.txt" run.vv
test "$(cat constrain.exit)" = 0
run readback "$ebmc" --show-symbol-table run.vv
test "$(cat readback.exit)" = 0
grep -Fq 'initial low required' readback.log
grep -Fq 'coral-w4-env1-env2.v1 (temporary) ENV1' readback.log

for goal in p_env1 p_env2 p_initial_two p_start p_released p_bad; do
    run "$goal" "$ebmc" --bound 8 --trace --vcd "$goal.vcd" --property "gated.$goal" run.vv
    case "$goal" in
        p_bad) expected=10; pattern=': REFUTED$';;
        p_released) expected=0; pattern=': PROVED$';;
        *) expected=0; pattern=': PROVED up to bound 8$';;
    esac
    test "$(cat "$goal.exit")" = "$expected"
    grep -E "^\[gated\.$goal\].*$pattern" "$goal.log"
done

printf 'reset gated.rst_ni\n' > single-reset.txt
run timing-conflict "$vv" constrain compiled.vv single-reset.txt broken.vv
test "$(cat timing-conflict.exit)" = 2
grep -Fq 'single-cycle reset conflicts with ENV2' timing-conflict.log
test ! -e broken.vv

sed 's/^ENV1 gated.test_enable_i$/ENV1 gated.data/' "$cases/gated.contract" > wrong-te.txt
run wrong-te "$vv" compile wrong-te.vv --clock-gate-contract wrong-te.txt "${flags[@]}" "${src[@]}"
test "$(cat wrong-te.exit)" = 2
grep -Fq 'ENV1/ENV2 port binding mismatch' wrong-te.log
test ! -e wrong-te.vv

sed 's/if (!clk_i) begin/if (clk_i) begin/' "$cases/coral-reset.sv" > extra-edges.sv
run extra-edges "$vv" compile extra-edges.vv --clock-gate-contract "$cases/gated.contract" \
    "${flags[@]}" extra-edges.sv "$cases/gated.sv"
test "$(cat extra-edges.exit)" = 2
grep -Fq 'requires the Coral v1 low-phase ClockGate body' extra-edges.log
test ! -e extra-edges.vv

sed -e 's/, data);/, data, independent_clk);/' \
    -e '/  reg q;/a\  reg separate; always @(posedge independent_clk) separate <= data;' \
    "$cases/gated.sv" > independent.sv
run independent "$vv" compile independent.vv --clock-gate-contract "$cases/gated.contract" \
    "${flags[@]}" "$cases/coral-reset.sv" independent.sv
test "$(cat independent.exit)" = 2
grep -Fq 'multiple clock domains' independent.log
test ! -e independent.vv

sed '/  reg q;/a\  wire illegal_data = gclk;' "$cases/gated.sv" > gated-data.sv
run gated-data "$vv" compile gated-data.vv --clock-gate-contract "$cases/gated.contract"     "${flags[@]}" "$cases/coral-reset.sv" gated-data.sv
test "$(cat gated-data.exit)" = 2
grep -Fq 'clock gate contract does not support gated clock data use' gated-data.log
test ! -e gated-data.vv
