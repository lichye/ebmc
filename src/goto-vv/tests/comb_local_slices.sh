#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
top=comb_local_slices
fixture="$base/$top.sv"

run() {
  local label=$1 status=0
  shift
  printf '%q ' "$@" > "$label.command"
  printf '\n' >> "$label.command"
  "$@" > "$label.log" 2>&1 || status=$?
  printf '%s\n' "$status" > "$label.rc"
}
status() {
  local actual
  actual=$(cat "$1.rc")
  if [[ $actual != "$2" ]]; then
    cat "$1.log"
    printf '%s exit %s, expected %s\n' "$1" "$actual" "$2" >&2
    return 1
  fi
}

: > empty
for mode in typed reference; do
  flags=()
  if [[ $mode == reference ]]; then flags=(-DEXPLICIT_REFERENCE); fi
  run "$mode-compile" "$vv" compile "$mode.vv" -mfcu --threads 1 --top "$top" "${flags[@]}" "$fixture"
  status "$mode-compile" 0
  run "$mode-constrain" "$vv" constrain "$mode.vv" empty "$mode-run.vv"
  status "$mode-constrain" 0
  run "$mode-readback" "$eb" --show-symbol-table "$mode-run.vv"
  status "$mode-readback" 0
  run "$mode-trans" "$eb" --show-trans "$mode-run.vv"
  status "$mode-trans" 0
  grep -q "^State variables:" "$mode-trans.log"
  if sed -n '/^State variables:/,$p' "$mode-trans.log" | grep -F '.$local_'; then
    exit 1
  fi
done
grep -Eq '"#vv_cycle_local" [0-9]+' typed.vv
grep -Fq "$top.sv line " typed-readback.log

for backend in minisat yices; do
  flags=()
  if [[ $backend == yices ]]; then
    if [[ ${VV_TEST_YICES:-0} != 1 ]]; then continue; fi
    flags=(--yices)
  fi
  for route in typed reference native native-reference; do
    case "$route" in
      native) input=(--systemverilog --top "$top" "$fixture") ;;
      native-reference) input=(--systemverilog --top "$top" -D EXPLICIT_REFERENCE "$fixture") ;;
      *) input=("$route-run.vv") ;;
    esac
    for property in p_sign p_order p_paths p_wrong; do
      label="$backend-$route-$property"
      id="$top.$property"
      run "$label" "$eb" "${flags[@]}" --trace --vcd "$label.vcd" --bound 5 --property "$id" "${input[@]}"
      if [[ $property == p_wrong ]]; then
        status "$label" 10
        grep -F "[$id]" "$label.log" | grep -Fq ': REFUTED'
        grep -E 'finite bad frame 2([^0-9]|$)' "$label.log"
      else
        status "$label" 0
        grep -F "[$id]" "$label.log" | grep -Fq ': PROVED up to bound 5'
      fi
    done
  done
done

reject() {
  local label=$1 module=$2 diagnostic=$3
  shift 3
  run "$label" "$vv" compile "$label.vv" -mfcu --threads 1 --top "$module" "$@" "$base/${top}_invalid.sv"
  status "$label" 2
  test ! -e "$label.vv"
  grep -E "${top}_invalid.sv:[0-9]+:[0-9]+: ($diagnostic)" "$label.log"
}
reject unassigned_word unassigned_word 'combinational local read before assignment: bits$'
for module in unassigned_slice missing_path overlapping_read; do
  reject "$module" "$module" 'combinational local slice read before assignment: bits$'
done
reject case_write bad_target 'case branch writes an active procedural local: bits$' -DCASE_WRITE
reject outside_strict bad_target 'error: cannot refer to element' -DOUTSIDE
reject outside_relaxed bad_target 'unsupported assignment to a combinational local$' -DOUTSIDE -Wno-error=index-oob
reject nba bad_target 'error: nonblocking assignment to automatic variable' -DNBA
for mode in DYNAMIC_INDEX DYNAMIC_RANGE STATIC_LOCAL; do
  reject "$mode" bad_target 'unsupported assignment to a combinational local$' "-D$mode"
done
printf '%s\n' 'Combinational local slices PASS'
