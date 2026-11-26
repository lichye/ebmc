#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
fixtures=$(cd -- "$(dirname -- "$0")" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
: > empty

run() {
  local name=$1 rc=0
  shift
  printf '%q ' "$@" > "$name.command"
  printf '\n' >> "$name.command"
  "$@" > "$name.log" 2>&1 || rc=$?
  printf '%s\n' "$rc" > "$name.rc"
}
status() { [[ $(cat "$1.rc") == "$2" ]]; }

pipeline="$fixtures/clock_alias.sv"
gated_files=("$fixtures/clock-gate/coral-reset.sv" "$pipeline" "$fixtures/clock_alias_gated.sv")
gate_contract="$fixtures/clock_alias_gated.contract"
gate_start="$fixtures/clock_alias_gated.start.txt"

build() {
  local name=$1 top=$2 bind=$3
  shift 3
  run "$name-compile" "$vv" compile "$name.vv" -mfcu --threads 1 --top "$top" "$@"
  status "$name-compile" 0
  run "$name-bind" "$vv" constrain "$name.vv" "$bind" "$name-run.vv"
  status "$name-bind" 0
}
build alias clock_alias empty "$pipeline"
build direct clock_alias empty -DDIRECT_CLOCK "$pipeline"
build gated-alias clock_alias_gated "$gate_start" --clock-gate-contract "$gate_contract" "${gated_files[@]}"
build gated-direct clock_alias_gated "$gate_start" --clock-gate-contract "$gate_contract" -DDIRECT_GATE_CLOCK "${gated_files[@]}"

run alias-readback "$eb" --show-symbol-table alias-run.vv
status alias-readback 0
grep -Fq 'Verilog::$root.clock_alias.alias_clk' alias-readback.log
grep -Fq '"source_clock"' alias.vv
run gated-readback "$eb" --show-symbol-table gated-alias-run.vv
status gated-readback 0
grep -Fq 'effective Verilog::$root.clock_alias_gated.child.__clock_gate_enable' gated-readback.log
grep -Fq '"Verilog::$root.clock_alias_gated.sampled"' gated-alias.vv
grep -Fq '"gate_enable"' gated-alias.vv
grep -Fq '"gate_output"' gated-alias.vv

solvers=(minisat)
if [[ ${E30_TEST_YICES:-0} == 1 ]]; then solvers+=(yices); fi
check() {
  local name=$1 property=$2 rc=$3 verdict=$4 frame=$5
  shift 5
  run "$name" "$eb" "${flags[@]}" --undriven seq --bound 9 --property "$property" \
    --trace --vcd "$name.vcd" "$@"
  status "$name" "$rc"
  grep -F "[$property]" "$name.log" | grep -Fq ": $verdict"
  if [[ $frame != - ]]; then
    grep -Eq "(finite bad frame $frame has an infinite extension by totality|witness loop .*equal endpoints.*bad frame $frame$)" "$name.log"
  fi
}
for solver in "${solvers[@]}"; do
  flags=()
  if [[ $solver == yices ]]; then flags=(--yices); fi
  for route in alias direct native-alias native-direct; do
    model=("$route-run.vv")
    if [[ $route == native-* ]]; then
      definitions=()
      if [[ $route == native-direct ]]; then definitions=(-D DIRECT_CLOCK); fi
      model=(--top clock_alias "${definitions[@]}" "$pipeline")
    fi
    for prop in p_pipeline p_request_closed; do
      check "$solver-$route-$prop" "clock_alias.$prop" 0 'PROVED up to bound 9' - "${model[@]}"
    done
    check "$solver-$route-p_wrong" clock_alias.p_wrong 10 REFUTED 2 "${model[@]}"
  done
  for route in gated-alias gated-direct; do
    for prop in p_updates p_child_closed; do
      check "$solver-$route-$prop" "clock_alias_gated.$prop" 0 'PROVED up to bound 9' - "$route-run.vv"
    done
    check "$solver-$route-p_wrong" clock_alias_gated.p_wrong 10 REFUTED 8 "$route-run.vv"
  done
done

for kind in inverted logic_gate other_root multi multi_port cycle source_not_bound delayed; do
  top="clock_alias_$kind"
  run "$kind" "$vv" compile "$kind.vv" -mfcu --threads 1 --top "$top" "$pipeline" "$fixtures/clock_alias_invalid.sv"
  status "$kind" 2
  test ! -e "$kind.vv"
  case "$kind" in
    inverted|logic_gate|source_not_bound) diagnostic='unsupported clock source' ;;
    other_root) diagnostic='multiple clock domains' ;;
    multi|multi_port) diagnostic='multiple drivers for clock signal' ;;
    cycle) diagnostic='cycle in clock connections' ;;
    delayed) diagnostic='delayed continuous assign' ;;
  esac
  grep -Eq "clock_alias_invalid\.sv:[0-9]+:[0-9]+: $diagnostic" "$kind.log"
done
run gated-data "$vv" compile gated-data.vv -mfcu --threads 1 --top clock_alias_gated \
  --clock-gate-contract "$gate_contract" -DGATED_ALIAS_DATA "${gated_files[@]}"
status gated-data 2
test ! -e gated-data.vv
grep -Eq 'clock_alias_gated\.sv:[0-9]+:[0-9]+: clock gate contract does not support gated clock data use' gated-data.log

printf '%s\n' 'Clock pass-through aliases PASS'
