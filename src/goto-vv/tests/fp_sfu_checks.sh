#!/usr/bin/env bash
# Spec 6. Supply the shared adapted input root, ordered file list, and a fresh output directory.
set -euo pipefail
ulimit -c 0
ulimit -v 8388608
tests=$(cd "$(dirname "$0")" && pwd)
native_only=false
if [[ ${1:-} == --native-only ]]; then native_only=true; shift; fi
if (( $# != 3 )); then
  echo "usage: $0 [--native-only] ADAPTED_ROOT ORDERED_FILES FRESH_OUTPUT" >&2
  exit 2
fi
root=$(realpath "$1")
mapfile -t sources <"$2"
if [[ ${#sources[@]} != 23 || ${sources[-1]} != spec/fp_sfu_spec.sv ]]; then
  echo 'expected 22 RTL sources followed by spec/fp_sfu_spec.sv' >&2
  exit 2
fi
work=$(realpath -m "$3")
eb=$(realpath "${EBMC:-$tests/../../ebmc/ebmc}")
if ! "$native_only"; then vv=$(realpath "${VV_BIN:-$tests/../build/goto-vv}"); fi
mkdir "$work"
cd "$work"
top=fp_sfu_spec
inc="$root/rtl/src/definitions"

run() {
  local label=$1 expected_rc=$2 rc=0
  shift 2
  printf '%q ' "$@" >"$label.cmd"
  printf '\n' >>"$label.cmd"
  /usr/bin/time -f '%e %M %x' -o "$label.time" timeout 600 "$@" >"$label.log" 2>&1 || rc=$?
  echo "$rc" >"$label.rc"
  if (( rc != expected_rc )); then
    echo "$label: exit $rc, expected $expected_rc; see $work/$label.log" >&2
    return 1
  fi
}

for mode in antecedents exp; do
  awk -v extra="$tests/fp_sfu_$mode.svh" '
    /^endmodule([[:space:]]|$)/ {
      count++
      while ((getline line < extra) > 0) print line
      close(extra)
      print ""
    }
    {print}
    END {if (count != 1) exit 1}
  ' "$root/${sources[-1]}" >"$mode.sv"
  for file in "${sources[@]:0:22}"; do
    printf '`include "%s/%s"\n' "$root" "$file"
  done >"$mode-unit.sv"
  printf '`include "%s/%s.sv"\n' "$work" "$mode" >>"$mode-unit.sv"

  direct=(--systemverilog --top "$top" -D SYNTHESIS -I "$inc")
  : >"$mode.txt"
  if [[ $mode == exp ]]; then
    direct+=(--reset "$top.rst")
    echo "reset $top.rst" >"$mode.txt"
    props=(m2_live m2_latency m2_value m2_opposite)
  else
    props=(m2_reset_one m2_reset_two m2_isolated_reset)
  fi
  direct+=("$work/$mode-unit.sv")
  routes=(direct)
  if ! "$native_only"; then
    run "$mode-compile" 0 "$vv" compile "$work/$mode.vv" --single-unit --top "$top" -DSYNTHESIS -I "$inc" "$work/$mode-unit.sv"
    run "$mode-bind" 0 "$vv" constrain "$work/$mode.vv" "$work/$mode.txt" "$work/$mode-constrained.vv"
    routes+=(typed)
  fi
  for prop in "${props[@]}"; do
    rc=0
    case "$prop" in
      m2_latency|m2_value) verdict='PROVED up to bound 20' ;;
      m2_opposite) verdict=REFUTED; rc=10 ;;
      *) verdict=PROVED ;;
    esac
    for route in "${routes[@]}"; do
      label="$prop-$route"
      if [[ $route == direct ]]; then inputs=("${direct[@]}"); else inputs=("$work/$mode-constrained.vv"); fi
      run "$label" "$rc" "$eb" --undriven seq --bound 20 --property "$top.$prop" --trace "${inputs[@]}"
      trace=$(awk -v prop="$prop" -v verdict="$verdict" -v mode="$mode" -f "$tests/fp_sfu_trace.awk" "$label.log")
      printf '%s\t%s\t%s\t%s\n' "$label" "$verdict" "$rc" "$trace" >>summary.tsv
    done
  done
done
cat summary.tsv
