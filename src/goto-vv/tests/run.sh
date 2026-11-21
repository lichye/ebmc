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
compile bridge live.vv "$front/tests/bridge-cover.sv"
bind live.vv reset live-constrained.vv
run live.direct "$eb" --top bridge --reset bridge.rst --undriven seq --bound 2 --trace --property bridge.u.live "$front/tests/bridge-cover.sv"
run live.typed "$eb" --undriven seq --bound 2 --trace --property bridge.u.live live-constrained.vv
compare live.direct live.typed bridge.u.live PROVED
for top in temporal sampled_sequence wires generate_nested decoder_features immediate generate_scopes fixed_arithmetic counted_break wildcard_case scalar_concat; do
  src="$front/tests/$top.sv"
  compile "$top" "$top.vv" "$src"
  bind "$top.vv" empty "$top-check.vv"
  run "$top.direct" "$eb" --top "$top" --undriven seq --bound 3 --trace "$src"
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
echo PASS
