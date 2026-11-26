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
top=case_locals
source="$fixtures/$top.sv"

run() {
  local name=$1 rc=0
  shift
  printf '%q ' "$@" > "$name.command"
  printf '\n' >> "$name.command"
  "$@" > "$name.log" 2>&1 || rc=$?
  printf '%s\n' "$rc" > "$name.rc"
}
status() { [[ $(cat "$1.rc") == "$2" ]]; }

for route in unique ordinary checked explicit; do
  flags=()
  case "$route" in
    ordinary) flags=(-DORDINARY_CASE);;
    checked) flags=(--assert-case-qualifiers);;
    explicit) flags=(-DEXPLICIT_REFERENCE);;
  esac
  run "$route-compile" "$vv" compile "$route.vv" -mfcu --threads 1 --top "$top" "${flags[@]}" "$source"
  status "$route-compile" 0
  run "$route-bind" "$vv" constrain "$route.vv" empty "$route-run.vv"
  status "$route-bind" 0
done
run readback "$eb" --show-symbol-table unique-run.vv
status readback 0
grep -Eq 'Verilog::\$root\.case_locals\.\$local_[0-9]+\.sign_a' readback.log
grep -Eq 'Verilog::\$root\.case_locals\.\$local_[0-9]+\.sign_b' readback.log
grep -Fq '"#vv_cycle_local"' unique.vv
run transition "$eb" --show-trans unique-run.vv
status transition 0
grep -Fq 'State variables:' transition.log
states=$(awk '/^State variables:/{inside=1; next} /^Initial state constraints:/{inside=0} inside && NF {print $1}' transition.log)
[[ $states == case_locals.phase ]]

test "$(grep -c 'case qualifier assertion' checked-compile.log)" = 3
for route in unique ordinary explicit; do
  if grep -Fq 'case qualifier assertion' "$route-compile.log"; then exit 1; fi
done

check() {
  local label=$1 id=$2 rc=$3 verdict=$4 frame=$5
  shift 5
  run "$label" "$eb" "${solver_flags[@]}" --bound 7 --trace --vcd "$label.vcd" --property "$id" "$@"
  status "$label" "$rc"
  grep -F "[$id]" "$label.log" | grep -Fq ": $verdict"
  if [[ $frame != - ]]; then
    grep -Eq "(finite bad frame $frame has an infinite extension by totality|witness loop .*equal endpoints.*bad frame $frame$)" "$label.log"
  fi
}
qualified() {
  local label=$1 marker=$2 stem=$3 rc=$4 verdict=$5 frame=$6
  local id line
  line=$(awk -v tag=" // $marker" 'index($0, tag) && /unique case/ {print NR}' "$source")
  id=$(awk -F: -v line="$line" -v stem="$stem" \
    '$2 == line && index($0, stem) && /case qualifier assertion/ {n=split($0, fields, " "); print fields[n]}' checked-compile.log)
  [[ $id == case_locals.* && $id != *$'\n'* ]]
  check "$label" "$id" "$rc" "$verdict" "$frame" checked-run.vv
}
solvers=(minisat)
if [[ ${E31_TEST_YICES:-0} == 1 ]]; then solvers+=(yices); fi
for solver in "${solvers[@]}"; do
  solver_flags=()
  if [[ $solver == yices ]]; then solver_flags=(--yices); fi
  for route in unique ordinary checked explicit native native-reference; do
    model=("$route-run.vv")
    if [[ $route == native* ]]; then
      definitions=()
      if [[ $route == native-reference ]]; then definitions=(-D EXPLICIT_REFERENCE); fi
      model=(--systemverilog --top "$top" "${definitions[@]}" "$source")
    fi
    for prop in p_rne p_rtz p_rdn p_rup p_default p_first_match p_after; do
      check "$solver-$route-$prop" "$top.$prop" 0 'PROVED up to bound 7' - "${model[@]}"
    done
    check "$solver-$route-p_wrong" "$top.p_wrong" 10 REFUTED 2 "${model[@]}"
  done
  qualified "$solver-sign-overlap" sign_case '$case_unique_overlap' 0 'PROVED up to bound 7' -
  qualified "$solver-order-overlap" order_case '$case_unique_overlap' 10 REFUTED 2
  qualified "$solver-order-coverage" order_case '$case_unique_coverage' 10 REFUTED 3
done

for kind in unassigned_selector unassigned_label unassigned_branch outside_scope branch_write default_write wildcard priority level_always; do
  top="case_local_$kind"
  run "$kind" "$vv" compile "$kind.vv" -mfcu --threads 1 --top "$top" "$fixtures/case_locals_invalid.sv"
  status "$kind" 2
  test ! -e "$kind.vv"
  case "$kind" in
    unassigned_*) diagnostic='combinational local read before assignment' ;;
    outside_scope) diagnostic='hierarchical reference to a procedural local outside its lexical scope' ;;
    branch_write|default_write) diagnostic='case branch writes an active procedural local' ;;
    wildcard|priority|level_always) diagnostic='unsupported case with active procedural locals' ;;
  esac
  grep -E "case_locals_invalid\.sv:[0-9]+:[0-9]+: $diagnostic" "$kind.log"
done
printf '%s\n' 'Case reads of combinational locals PASS'
