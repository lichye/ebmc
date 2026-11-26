#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
top=initial_parameter_assertions
source="$base/$top.sv"
run() {
  local label=$1 status=0
  shift
  printf '%q ' "$@" > "$label.command"
  printf '\n' >> "$label.command"
  "$@" > "$label.log" 2>&1 || status=$?
  printf '%s\n' "$status" > "$label.rc"
}
status() { test "$(cat "$1.rc")" = "$2"; }
compile() {
  local label=$1 module=$2
  shift 2
  run "$label" "$vv" compile "$label.vv" -mfcu --threads 1 --top "$module" "$@"
}
reject() {
  status "$1" 2
  test ! -e "$1.vv"
  grep -Eq "$2" "$1.log"
}

: > empty
compile accepted "$top" "$source"
status accepted 0
run constrain "$vv" constrain accepted.vv empty run.vv
status constrain 0
run properties "$eb" --show-properties run.vv
status properties 0
if grep -Eq '\.p_(width|truth)([^[:alnum:]_]|$)' properties.log; then
  exit 1
fi
for property in defaults.gen_lzc.p_stamp overrides.gen_lzc.p_stamp p_wiring p_skipped; do
  id="$top.$property"
  run "$property" "$eb" --bound 1 --property "$id" run.vv
  status "$property" 0
  grep -F "[$id]" "$property.log" | grep -Fq ': PROVED up to bound 1'
done
run data-refutes "$eb" --bound 1 --trace --property "$top.overrides.gen_lzc.p_live" run.vv
status data-refutes 10
grep -Eq 'finite bad frame 0([^0-9]|$)' data-refutes.log

for kind in WIDTH X Z; do
  compile "bad-$kind" "$top" "-DBAD_$kind" "$source"
  if [[ $kind == WIDTH ]]; then
    line=$(awk '/p_width: assert/ {print NR}' "$source")
    message='expected positive WIDTH'
  else
    line=$(awk '/p_truth: assert/ {print NR}' "$source")
    message='expected true TRUTH'
  fi
  reject "bad-$kind" "$top.sv:$line:[0-9]+: initial parameter assertion failed in $top\.overrides\.gen_lzc: $message$"
done
for module in runtime_value runtime_path within_event after_event after_branch_event \
              after_assignment_delay success_action wrong_task always_runtime deferred; do
  compile "$module" "$module" "$base/${top}_invalid.sv"
  diagnostic='unsupported procedural assertion action'
  if [[ $module == deferred ]]; then
    diagnostic='unsupported procedural assertion kind or timing'
  fi
  reject "$module" "${top}_invalid.sv:[0-9]+:[0-9]+: $diagnostic$"
done
printf '%s\n' 'Initial parameter assertions PASS'
