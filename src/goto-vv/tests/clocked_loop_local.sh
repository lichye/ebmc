#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")/clocked-loop-local" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
fixture="$base/masked.sv"
run() {
  local label=$1 status=0
  shift
  printf '%q ' "$@" > "$label.command"
  printf '\n' >> "$label.command"
  "$@" > "$label.log" 2>&1 || status=$?
  printf '%s\n' "$status" > "$label.rc"
}
status() { test "$(cat "$1.rc")" = "$2"; }
reject() {
  local label=$1 diagnostic=$2
  run "$label" "$vv" compile "$label.vv" -mfcu --top clocked_loop_local "$label.sv"
  status "$label" 2
  grep -Eq "$label.sv:[0-9]+(:[0-9]+)?: .*($diagnostic)" "$label.log"
  test ! -e "$label.vv"
}

: > empty
run compile "$vv" compile compiled.vv -mfcu --threads 1 --top clocked_loop_local "$fixture"
status compile 0
run constrain "$vv" constrain compiled.vv empty run.vv
status constrain 0
run readback "$eb" --show-symbol-table run.vv
status readback 0
! grep -Fq '.mem_write.i' readback.log
! grep -Fq '.signed_index.i' readback.log
for backend in minisat yices; do
  options=()
  if [[ $backend == yices ]]; then options+=(--yices); fi
  for route in native typed; do
    input=(run.vv)
    if [[ $route == native ]]; then input=(--systemverilog --top clocked_loop_local "$fixture"); fi
    for property in p_reference p_endpoints p_full p_sparse p_disabled p_address p_wrong_old; do
      label="$backend-$route-$property"
      run "$label" "$eb" "${options[@]}" --trace --bound 7 --vcd "$label.vcd" \
        --property "clocked_loop_local.$property" "${input[@]}"
      if [[ $property == p_wrong_old ]]; then
        status "$label" 10
        grep -E '^\[clocked_loop_local\.p_wrong_old\].*: REFUTED$' "$label.log"
        grep -E 'bad frame 3([^0-9]|$)' "$label.log"
      else
        status "$label" 0
        grep -E "^\[clocked_loop_local\.$property\].*: PROVED up to bound 7\$" "$label.log"
      fi
    done
  done
done

sed 's/i < 4/i < variable_count/' "$fixture" > runtime-bound.sv
reject runtime-bound 'nonconstant loop guard'
sed 's/i = i + 1/i = i + 2/' "$fixture" > nonunit.sv
reject nonunit 'expected a blocking unit increment'
sed 's/integer i;/integer i = 0;/' "$fixture" > lifetime-initializer.sv
reject lifetime-initializer 'unsupported procedural local declaration'
sed -e '/  bit \[31:0\] old_word;/a\  bit [31:0] before_init;' \
    -e '/    begin : mem_write/,+1 { /integer i;/a\      before_init <= i;
    }' "$fixture" > early-read.sv
reject early-read 'unsupported procedural local declaration'
sed '/^  p_reference:/i\  wire outside_i = mem_write.i[0];' "$fixture" > hierarchical.sv
reject hierarchical 'bad signal binding .*mem_write.i'
sed '/if (enable \& write_word \& mask\[i\])/a\        i = 0;' "$fixture" > index-write.sv
reject index-write 'write to a statically expanded loop index'
printf '%s\n' 'Clocked named loop locals PASS'
