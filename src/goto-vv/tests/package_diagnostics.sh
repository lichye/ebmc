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
top=package_diagnostics
fixture="$base/$top.sv"

run() {
  local name=$1 rc=0
  shift
  printf '%q ' "$@" > "$name.command"
  printf '\n' >> "$name.command"
  "$@" > "$name.log" 2>&1 || rc=$?
  printf '%s\n' "$rc" > "$name.rc"
}
status() { [[ $(cat "$1.rc") == "$2" ]]; }
build() {
  local name=$1 module=$2
  shift 2
  run "$name-compile" "$vv" compile "$name.vv" -mfcu --threads 1 --top "$module" "$@" "$fixture"
  status "$name-compile" 0
  run "$name-bind" "$vv" constrain "$name.vv" empty "$name-run.vv"
  status "$name-bind" 0
  run "$name-properties" "$eb" --show-properties --xml-ui "$name-run.vv"
  status "$name-properties" 0
}
build default "$top"
build explicit "$top" -DEXPLICIT_REFERENCE
build fatal "$top" --assert-diagnostic-tasks=fatal
build error "$top" --assert-diagnostic-tasks=error
build both "$top" --assert-diagnostic-tasks=fatal,warning,error
for variant in default explicit; do
  if grep -Fq ': diagnostic assertion ' "$variant-compile.log"; then exit 1; fi
done
test "$(grep -c ': diagnostic assertion ' fatal-compile.log)" = 7
test "$(grep -c ': diagnostic assertion ' error-compile.log)" = 7
test "$(grep -c ': diagnostic assertion ' both-compile.log)" = 14
! grep -Fq '($error)' fatal-compile.log
! grep -Fq '($fatal)' error-compile.log
grep -Fq '"function_location"' both-run.vv
grep -Fq '"function_definition"' both-run.vv
grep -Fq '"inner_call_location"' both-run.vv
grep -Fq '"function-diagnostic-task"' both-run.vv
run both-transition "$eb" --show-trans both-run.vv
status both-transition 0
states=$(awk '/^State variables:/{inside=1; next} /^Initial state constraints:/{inside=0} inside && NF {print $1}' both-transition.log)
[[ $states == package_diagnostics.phase ]]

check() {
  local label=$1 id=$2 rc=$3 verdict=$4 frame=$5
  shift 5
  run "$label" "$eb" "${solver_flags[@]}" --bound 6 --property "$id" --trace --vcd "$label.vcd" "$@"
  status "$label" "$rc"
  grep -F "[$id]" "$label.log" | grep -Fq ": $verdict"
  if [[ $frame != - ]]; then
    grep -Eq "(finite bad frame $frame has an infinite extension by totality|witness loop .*equal endpoints.*bad frame $frame$)" "$label.log"
  fi
}
candidate() {
  local mode=$1 owner=$2 marker=$3 task=$4 frame=$5 id line result rc task_marker task_line
  line=$(awk -v marker="// $marker" 'index($0, marker) && substr($0, length($0)-length(marker)+1) == marker {print NR}' "$fixture")
  id=$(awk -F: -v line="$line" -v prefix="$owner.\$$task" '
    $2 == line && /: diagnostic assertion / {
      split($0, fields, " ")
      if(index(fields[4], prefix) == 1) print fields[4]
    }' "$mode-compile.log")
  [[ $id == "$owner."* && $id != *$'\n'* ]]
  task_marker="${task}_site"
  if [[ $marker == reported_call ]]; then task_marker=retained_site; fi
  task_line=$(awk -v tag="// $task_marker" 'index($0, tag) {print NR}' "$fixture")
  grep -F "$id " "$mode-compile.log" | grep -Eq " at .*package_diagnostics\.sv:$task_line:[0-9]+$"
  awk -v id="$id" -v line="$line" '
    /<property name=/ { selected=index($0, "name=\"" id "\"") }
    selected && /<location/ && index($0, "line=\"" line "\"") { found=1 }
    END {exit !found}
  ' "$mode-properties.log"
  if [[ $frame == - ]]; then
    rc=0; result='PROVED up to bound 6'
  else
    rc=10; result=REFUTED
  fi
  local owner_base=${owner##*.}
  check "$solver-$mode-$owner_base-$marker-$task" "$id" "$rc" "$result" "$frame" "$mode-run.vv"
}

solvers=(minisat)
if [[ ${E32_TEST_YICES:-0} == 1 ]]; then solvers+=(yices); fi
for solver in "${solvers[@]}"; do
  solver_flags=()
  if [[ $solver == yices ]]; then solver_flags=(--yices); fi
  for mode in default explicit fatal error both native; do
    model=("$mode-run.vv")
    if [[ $mode == native ]]; then
      model=(--systemverilog --top "$top" -D EXPLICIT_REFERENCE "$fixture")
    fi
    for prop in p_value p_disabled p_nested p_retained; do
      check "$solver-$mode-$prop" "$top.$prop" 0 'PROVED up to bound 6' - "${model[@]}"
    done
    check "$solver-$mode-p_wrong" "$top.p_wrong" 10 REFUTED 3 "${model[@]}"
  done
  for mode in fatal both; do
    candidate "$mode" "$top.fires" caller_fatal fatal 3
    candidate "$mode" "$top.fires" caller_error fatal -
    candidate "$mode" "$top.idle" caller_fatal fatal -
    candidate "$mode" "$top.idle" caller_error fatal -
    candidate "$mode" "$top" nested_idle fatal -
    candidate "$mode" "$top" nested_fire fatal 4
    candidate "$mode" "$top" dead_call fatal -
  done
  for mode in error both; do
    candidate "$mode" "$top.fires" caller_error error 2
    candidate "$mode" "$top.fires" caller_fatal error -
    candidate "$mode" "$top.idle" caller_fatal error -
    candidate "$mode" "$top.idle" caller_error error -
    candidate "$mode" "$top" nested_idle error -
    candidate "$mode" "$top" nested_fire error -
    candidate "$mode" "$top" reported_call error 3
  done
done

build standalone standalone_fatal --assert-diagnostic-tasks=fatal
standalone_id=$(awk '/: diagnostic assertion / {print $4}' standalone-compile.log)
solver=minisat
solver_flags=()
check standalone-failure "$standalone_id" 10 REFUTED 1 standalone-run.vv

invalid="$base/package_diagnostics_invalid.sv"
for kind in ARGUMENT_WRITE ARGUMENT_CALL MISSING_RETURN CONDITIONAL_CALL SHORT_CIRCUIT CONSTANT_POWER SAMPLED_CALL REPETITION_CALL; do
  case "$kind" in
    ARGUMENT_WRITE) diagnostic='unsupported write in diagnostic argument'; flags=();;
    ARGUMENT_CALL) diagnostic='unsupported call in diagnostic argument'; flags=(--assert-diagnostic-tasks=error);;
    MISSING_RETURN) diagnostic='runtime diagnostic function requires a defined return'; flags=();;
    *) diagnostic='cannot preserve evaluation path for a runtime diagnostic call'; flags=(--assert-diagnostic-tasks=fatal);;
  esac
  run "$kind" "$vv" compile "$kind.vv" -mfcu --threads 1 --top package_diagnostics_invalid "-D$kind" "${flags[@]}" "$invalid"
  status "$kind" 2
  test ! -e "$kind.vv"
  grep -E "package_diagnostics_invalid\.sv:[0-9]+:[0-9]+: $diagnostic" "$kind.log"
done
run conditional-default "$vv" compile conditional-default.vv -mfcu --threads 1 --top package_diagnostics_invalid -DCONDITIONAL_CALL "$invalid"
status conditional-default 0
printf '%s\n' 'Runtime package diagnostics PASS'
