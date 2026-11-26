#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
top=packed_comb_status
fixture="$base/$top.sv"

run() {
  local label=$1 status=0
  shift
  printf '%q ' "$@" > "$label.command"
  printf '\n' >> "$label.command"
  "$@" > "$label.log" 2>&1 || status=$?
  printf '%s\n' "$status" > "$label.rc"
}
status() { test "$(cat "$1.rc")" = "$2"; }

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
  if sed -n '/^State variables:/,$p' "$mode-trans.log" | grep -F 'temp_status'; then
    exit 1
  fi
done
test "$(grep -Ec '^  Verilog::\$root\.packed_comb_status\.\$local_[0-9]+\.temp_status$' typed-readback.log)" = 2
grep -Eq '"#vv_cycle_local" [0-9]+' typed.vv

for backend in minisat yices; do
  flags=()
  if [[ $backend == yices ]]; then flags=(--yices); fi
  for route in typed reference native native-reference; do
    case "$route" in
      native) input=(--systemverilog --top "$top" "$fixture") ;;
      native-reference) input=(--systemverilog --top "$top" -D EXPLICIT_REFERENCE "$fixture") ;;
      *) input=("$route-run.vv") ;;
    esac
    for property in p_full p_sparse p_clear p_middle p_steps p_members p_current_branch p_independent p_wrong; do
      label="$backend-$route-$property"
      id="$top.$property"
      run "$label" "$eb" "${flags[@]}" --trace --vcd "$label.vcd" --bound 5 --property "$id" "${input[@]}"
      if [[ $property == p_wrong ]]; then
        status "$label" 10
        grep -F "[$id]" "$label.log" | grep -Fq ': REFUTED'
        grep -E 'finite bad frame 1([^0-9]|$)' "$label.log"
      else
        status "$label" 0
        grep -F "[$id]" "$label.log" | grep -Fq ': PROVED up to bound 5'
      fi
    done
  done
done

reject() {
  local module=$1 diagnostic=$2
  shift 2
  run "$module" "$vv" compile "$module.vv" -mfcu --threads 1 --top "$module" "$@" "$base/${top}_invalid.sv"
  status "$module" 2
  test ! -e "$module.vv"
  grep -E "${top}_invalid.sv:[0-9]+:[0-9]+: ($diagnostic)" "$module.log"
}
for mode in read_before or_before partial_path; do
  reject "$mode" 'combinational local read before assignment: temp_status$'
done
reject external_read '.*(hierarchical|bad signal binding)' -DEXTERNAL_READ
reject static_value 'unsupported procedural local declaration$'
reject partial_write 'unsupported assignment target containing a procedural local$'
printf '%s\n' 'Packed combinational locals PASS'
