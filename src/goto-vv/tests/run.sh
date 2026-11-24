#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
front=$(cd "$(dirname "$0")/.." && pwd)
eb=${EBMC:-$front/../ebmc/ebmc}
vv=${VV_BIN:-$front/build/goto-vv}
work=$(realpath -m "${1:?give a directory for test outputs}")
mkdir -p "$work"
cd "$work"
: >empty
echo 'reset bridge.rst' >reset
echo "assume env_zero posedge bridge.clk bridge.a == '0" >env
cat reset env >both
compile() { "$vv" compile "$2" --single-unit --top "$1" "$3"; }
bind() { "$vv" constrain "$1" "$2" "$3"; }
run() {
  local log=$1
  shift
  local rc=0
  "$@" >"$log" 2>&1 || rc=$?
  echo "$rc" >"$log.rc"
}
compare() {
  local a=$1 b=$2 property=$3 expected=$4
  local result="^\[$property\].*: $expected$"
  grep -E "$result" "$a" > /dev/null
  grep -E "$result" "$b" > /dev/null
  cmp "$a.rc" "$b.rc"
  printf '%s\t%s\t%s\t%s\n' "$a" "$property" "$expected" "$(cat "$a.rc")"
}
compile bridge bridge-1.vv "$front/tests/bridge.sv"
compile bridge bridge-2.vv "$front/tests/bridge.sv"
cmp bridge-{1,2}.vv
for config in empty reset env both; do
  bind bridge-1.vv "$config" "$config.vv"
  src="$front/tests/bridge.sv"; flags=()
  if [[ $config = env || $config = both ]]; then src="$front/tests/bridge-env.sv"; fi
  if [[ $config = reset || $config = both ]]; then flags+=(--reset bridge.rst); fi
  common=(--bound 2 --undriven seq --property bridge.u.p_clean --trace)
  run "$config.direct" "$eb" "${common[@]}" --top bridge "${flags[@]}" "$src"
  run "$config.typed" "$eb" "${common[@]}" "$config.vv"
  expected=REFUTED; if [[ $config = both ]]; then expected='PROVED up to bound 2'; fi
  compare "$config.direct" "$config.typed" bridge.u.p_clean "$expected"
done
run typed-symbols "$eb" --show-symbol-table both.vv
test "$(cat typed-symbols.rc)" = 0
grep -Fq 'Verilog::$root.bridge.u.q' typed-symbols
compile bridge live.vv "$front/tests/bridge-cover.sv"
bind live.vv reset live-constrained.vv
run live.direct "$eb" --top bridge --reset bridge.rst --undriven seq --bound 2 --trace --property bridge.u.live "$front/tests/bridge-cover.sv"
run live.typed "$eb" --undriven seq --bound 2 --trace --property bridge.u.live live-constrained.vv
compare live.direct live.typed bridge.u.live PROVED
for top in temporal sampled_sequence wires generate_nested open_outputs local_enum clock_reset packed_fields decoder_features immediate generate_scopes fixed_arithmetic counted_break wildcard_case scalar_concat string_parameter compound_field packed_pattern empty_member zero_replication packed_select automatic_locals constant_guard packed_range; do
  src="$front/tests/$top.sv"
  compile "$top" "$top.vv" "$src"
  bind "$top.vv" empty "$top-check.vv"
  direct_src="$src"
  if [[ $top == packed_select ]]; then
    direct_src="$front/tests/packed_select_flat.sv"
  fi
  run "$top.direct" "$eb" --top "$top" --undriven seq --bound 3 --trace "$direct_src"
  run "$top.typed" "$eb" --undriven seq --bound 3 --trace "$top-check.vv"
  while read -r name expected; do
    compare "$top.direct" "$top.typed" "$top.$name" "$expected"
  done < "$front/tests/$top.expected"
done
grep -Fq 'generate_nested.outer.lanes[0].even_lane.u' generate_nested.vv
grep -Fq 'generate_nested.outer.lanes[1].odd_lane.u' generate_nested.vv
if grep -Fq 'unused_lane' generate_nested.vv; then
  echo 'Uninstantiated generate scope was emitted' >&2
  exit 1
fi
if grep -Fq 'warning: ignoring' packed_fields.direct packed_fields.typed; then
  echo 'Packed field extraction did not reach the solver' >&2
  exit 1
fi
for name in first_zero first_nonzero; do
  flags=(--bound 0 --undriven seq --property "sampled_sequence.$name" --trace)
  run "$name.direct" "$eb" "${flags[@]}" --top sampled_sequence "$front/tests/sampled_sequence.sv"
  run "$name.typed" "$eb" "${flags[@]}" sampled_sequence-check.vv
  if [[ $name = first_zero ]]; then
    compare "$name.direct" "$name.typed" "sampled_sequence.$name" 'PROVED up to bound 0'
    test "$(cat "$name.direct.rc")" = 0
  else
    compare "$name.direct" "$name.typed" "sampled_sequence.$name" REFUTED
    test "$(cat "$name.direct.rc")" = 10
  fi
done
sed 's/if (rst) q/if (data) q/' "$front/tests/clock_reset.sv" >wrong-reset.sv
run wrong-reset compile clock_reset rejected.vv wrong-reset.sv
grep -q 'wrong-reset.sv:[0-9].*posedge reset tested by the outer if' wrong-reset
test "$(cat wrong-reset.rc)" != 0
# A semantic analysis error must stop compilation before an object is written.
sed '/always_ff/i\  initial q = 0;' "$front/tests/bridge.sv" >driver-conflict.sv
run driver-conflict compile bridge driver-rejected.vv driver-conflict.sv
grep -qE 'driver-conflict.sv:[0-9]+:[0-9]+:.*always_ff.*(written|driv)' driver-conflict
test "$(cat driver-conflict.rc)" = 2
test ! -e driver-rejected.vv
sed 's/"base.hex"/"image.hex"/' "$front/tests/string_parameter.sv" >string-file.sv
run string-file compile string_parameter string-file.vv string-file.sv
grep -qE 'string-file.sv:[0-9]+:[0-9]+: unsupported procedural call \$readmemh$' string-file
test "$(cat string-file.rc)" = 2
test ! -e string-file.vv
run compound-target compile compound_target compound-rejected.vv "$front/tests/compound_target.sv"
grep -qE 'compound_target.sv:4:[0-9]+: unsupported compound assignment target' compound-target
test "$(cat compound-target.rc)" = 2
test ! -e compound-rejected.vv
run packed-pattern-target compile packed_pattern_target packed-pattern-rejected.vv "$front/tests/packed_pattern_target.sv"
grep -qE 'packed_pattern_target.sv:3:[0-9]+: unsupported assignment pattern' packed-pattern-target
test "$(cat packed-pattern-target.rc)" = 2
test ! -e packed-pattern-rejected.vv
grep -Fqx '"1xz" 0 0' packed_pattern.vv
run empty-member-strict "$vv" compile empty-strict.vv --top empty_member -Werror=empty-member "$front/tests/empty_member.sv"
grep -qE 'empty_member.sv:7:[0-9]+: error: .*\[-Wempty-member\]' empty-member-strict
test "$(cat empty-member-strict.rc)" = 2
test ! -e empty-strict.vv
run empty-member-clocking compile empty_member_clocking empty-clocking.vv "$front/tests/empty_member_clocking.sv"
grep -qE 'empty_member_clocking.sv:2:[0-9]+: unsupported module member ClockingBlock' empty-member-clocking
test "$(cat empty-member-clocking.rc)" = 2
test ! -e empty-clocking.vv
zero_replication_invalid="$front/tests/zero_replication_invalid.sv"
for kind in empty standalone; do
  log="zero-replication-$kind"
  run "$log" compile "zero_replication_$kind" "$log.vv" "$zero_replication_invalid"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'zero_replication_invalid.sv:2:[0-9]+: error: .*concatenation' zero-replication-empty
grep -qE 'zero_replication_invalid.sv:7:[0-9]+: error: replication constant can only be zero inside' zero-replication-standalone
run packed-select-dynamic compile packed_select_dynamic packed-dynamic.vv "$front/tests/packed_select_dynamic.sv"
grep -qE 'packed_select_dynamic.sv:2:[0-9]+: packed element select requires a known constant index' packed-select-dynamic
test "$(cat packed-select-dynamic.rc)" = 2
test ! -e packed-dynamic.vv
run packed-select-oob "$vv" compile packed-oob.vv --top packed_select_oob -Wno-error=index-oob "$front/tests/packed_select_oob.sv"
grep -qE 'packed_select_oob.sv:2:[0-9]+: packed element select index is out of range' packed-select-oob
test "$(cat packed-select-oob.rc)" = 2
test ! -e packed-oob.vv
grep -Fqx '"1z" 0 0' packed_select.vv
grep -Fqx '"x1" 0 0' packed_select.vv
automatic_invalid="$front/tests/automatic_locals_invalid.sv"
for kind in unassigned partial dynamic oob; do
  log="automatic-$kind"
  run "$log" compile "automatic_$kind" "$log.vv" "$automatic_invalid"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'automatic_locals_invalid.sv:8:[0-9]+: automatic local read before assignment: tmp' automatic-unassigned
grep -qE 'automatic_locals_invalid.sv:19:[0-9]+: unsupported assignment to an automatic local' automatic-partial
grep -qE 'automatic_locals_invalid.sv:31:[0-9]+: packed element select requires a known constant index' automatic-dynamic
grep -qE 'automatic_locals_invalid.sv:42:[0-9]+: packed element select index is out of range' automatic-oob
run constant-guard-indivisible "$vv" compile indivisible.vv --top constant_guard \
  -G MLEN=7 "$front/tests/constant_guard.sv"
grep -qE 'constant_guard.sv:6:[0-9]+: unsupported procedural call \$error' constant-guard-indivisible
test "$(cat constant-guard-indivisible.rc)" = 2
test ! -e indivisible.vv
for kind in dynamic_error dynamic_finish unknown; do
  log="constant-guard-$kind"
  run "$log" compile "constant_guard_$kind" "$log.vv" "$front/tests/constant_guard_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'constant_guard_invalid.sv:4:[0-9]+: unsupported procedural call \$error' constant-guard-dynamic_error
grep -qE 'constant_guard_invalid.sv:15:[0-9]+: unsupported procedural call \$finish' constant-guard-dynamic_finish
grep -qE 'constant_guard_invalid.sv:23:[0-9]+: unsupported procedural call \$error' constant-guard-unknown
run packed-range-dynamic compile packed_range_dynamic packed-range-dynamic.vv "$front/tests/packed_range_dynamic.sv"
grep -qE 'packed_range_dynamic.sv:2:[0-9]+: packed range select requires known constant bounds' packed-range-dynamic
test "$(cat packed-range-dynamic.rc)" = 2
test ! -e packed-range-dynamic.vv
run packed-range-oob "$vv" compile packed-range-oob.vv --top packed_range_oob -Wno-error=range-oob "$front/tests/packed_range_oob.sv"
grep -qE 'packed_range_oob.sv:2:[0-9]+: packed range select is out of range' packed-range-oob
test "$(cat packed-range-oob.rc)" = 2
test ! -e packed-range-oob.vv
# An explicitly empty input still needs its own supported undriven semantics.
sed 's/\.a(a), \.forwarded(open_q)/.a(), .forwarded(open_q)/' "$front/tests/open_outputs.sv" >open_input.sv
run open-input compile open_outputs rejected.vv open_input.sv
grep -q 'unbound child port a' open-input
test "$(cat open-input.rc)" != 0
echo "assume typo posedge bridge.clk bridge.missing == '0" >bad-contract
run bad-binding "$vv" constrain bridge-1.vv bad-contract rejected.vv
grep -q 'bad-contract:1: unbound input bridge.missing' bad-binding
sed '1s/goto-vv 1/goto-vv 9/' both.vv >bad-version.vv
run bad-version "$eb" bad-version.vv
grep -q 'bad-version.vv:1: unsupported goto-vv schema' bad-version
sed 's/posedge clk/negedge clk/' "$front/tests/bridge.sv" >bad-clock.sv
run bad-clock compile bridge rejected.vv bad-clock.sv
grep -q 'bad-clock.sv:[0-9].*posedge' bad-clock
sed 's/\$stable(zero)/$stable(zero, @(negedge clk))/' "$front/tests/sampled_sequence.sv" >stable-clock.sv
run stable-clock compile sampled_sequence rejected.vv stable-clock.sv
grep -q 'stable-clock.sv:[0-9].*\$stable.*posedge' stable-clock
sed 's/single-posedge\/preponed\/disable-current/single-posedge\/unsampled\/disable-current/' both.vv >bad-sampling.vv
run bad-sampling "$eb" bad-sampling.vv
grep -q 'bridge.sv:[0-9].*unsupported sampling model' bad-sampling
grep -q '^"0110" 0 0$' wires.vv
awk '/^Transition system state 0/{in0=1;next} /^Transition system state 1/{in0=0} in0 && /temporal.started = 0/{seen=1} END{exit !seen}' temporal.typed
for k in bad-binding bad-version bad-clock stable-clock bad-sampling; do test "$(cat "$k.rc")" != 0; done
EBMC="$eb" VV_BIN="$vv" bash "$front/tests/compilation_units.sh" "$work/compilation-units"
echo PASS
