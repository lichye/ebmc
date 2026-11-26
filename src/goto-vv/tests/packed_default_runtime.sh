#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"

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
for top in packed_default_runtime packed_default_states; do
  source="$base/$top.sv"
  for shape in pattern reference; do
    label="$top-$shape"
    args=()
    if [[ $shape == reference ]]; then args=(-DEXPLICIT_REFERENCE); fi
    run "$label-compile" "$vv" compile "$label.vv" -mfcu --threads 1 --top "$top" "${args[@]}" "$source"
    status "$label-compile" 0
    run "$label-constrain" "$vv" constrain "$label.vv" empty "$label-run.vv"
    status "$label-constrain" 0
  done
done

top=packed_default_runtime
for backend in minisat yices; do
  flags=()
  if [[ $backend == yices ]]; then flags=(--yices); fi
  for route in pattern reference native; do
    input=("$top-$route-run.vv")
    if [[ $route == native ]]; then
      input=(--systemverilog --top "$top" -D EXPLICIT_REFERENCE "$base/$top.sv")
    fi
    for property in p_vectors p_branches p_payload p_order p_wrong_vector p_wrong_payload; do
      label="$backend-$route-$property"
      id="$top.$property"
      run "$label" "$eb" "${flags[@]}" --trace --vcd "$label.vcd" --bound 4 --property "$id" "${input[@]}"
      if [[ $property == p_wrong_* ]]; then
        status "$label" 10
        grep -F "[$id]" "$label.log" | grep -Fq ': REFUTED'
        grep -E 'finite bad frame 1([^0-9]|$)' "$label.log"
      else
        status "$label" 0
        grep -F "[$id]" "$label.log" | grep -Fq ': PROVED up to bound 4'
      fi
    done
  done
done

top=packed_default_states
for route in pattern reference native; do
  input=("$top-$route-run.vv")
  if [[ $route == native ]]; then
    input=(--systemverilog --top "$top" -D EXPLICIT_REFERENCE "$base/$top.sv")
  else
    grep -Fqx '"x" 0 0' "$top-$route.vv"
    grep -Fqx '"z" 0 0' "$top-$route.vv"
    grep -Fqx '"xz" 0 0' "$top-$route.vv"
  fi
  run "states-$route" "$eb" --bound 1 --property "$top.p_head" "${input[@]}"
  status "states-$route" 0
  grep -F "[$top.p_head]" "states-$route.log" | grep -Fq ': PROVED up to bound 1'
done

run effect "$vv" compile effect.vv -mfcu --top packed_default_effect "$base/packed_default_effect.sv"
status effect 2
test ! -e effect.vv
grep -E 'packed_default_effect.sv:[0-9]+:[0-9]+: unsupported write in a packed default$' effect.log
printf '%s\n' 'Runtime packed defaults PASS'
