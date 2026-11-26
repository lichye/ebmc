#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
top=reduction_inverted
source="$base/$top.sv"
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
: > empty

for shape in operators explicit; do
  definitions=()
  if [[ $shape == explicit ]]; then definitions=(-DEXPLICIT_REFERENCE); fi
  "$vv" compile "$shape.vv" -mfcu --threads 1 --top "$top" "${definitions[@]}" "$source"
  "$vv" constrain "$shape.vv" empty "$shape-run.vv"
done

for route in native-operators native-explicit vv-operators vv-explicit; do
  input=("${route#*-}-run.vv")
  if [[ $route == native-* ]]; then
    definitions=()
    if [[ $route == native-explicit ]]; then definitions=(-D EXPLICIT_REFERENCE); fi
    input=(--top "$top" "${definitions[@]}" "$source")
  fi
  for property in p_constants p_runtime p_patterns p_scalar p_wide p_masked p_wrong \
    p_unknown_nor_zero p_unknown_nor_one p_unknown_nand_zero p_unknown_nand_one \
    p_unknown_xnor_zero p_unknown_xnor_one p_unknown_scalar_zero p_unknown_scalar_one p_unknown_driven; do
    id="$top.$property"
    log="$route-$property.log"
    rc=0
    "$eb" --bound 3 --property "$id" "${input[@]}" > "$log" 2>&1 || rc=$?
    case "$property" in
      p_wrong|p_unknown_*)
        [[ $rc == 10 ]]
        grep -F "[$id]" "$log" | grep -Fq ': REFUTED'
        if [[ $property == p_wrong ]]; then
          grep -Eq 'bad frame 2$' "$log"
        fi
        ;;
      *)
        [[ $rc == 0 ]]
        grep -F "[$id]" "$log" | grep -Fq ': PROVED up to bound 3'
        ;;
    esac
  done
done
printf '%s\n' 'Inverted reductions PASS'
