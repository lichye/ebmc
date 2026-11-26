#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")/case-qualifiers" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
fixture="$base/qualifiers.sv"
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
sed -E 's/(unique|priority) case/case/' "$fixture" > ordinary.sv
for mode in default checked; do
  opts=()
  if [[ $mode == checked ]]; then opts+=(--assert-case-qualifiers); fi
  run "$mode-compile" "$vv" compile "$mode.vv" -mfcu --threads 1 --top qualifiers "${opts[@]}" "$fixture"
  status "$mode-compile" 0
  run "$mode-constrain" "$vv" constrain "$mode.vv" empty "$mode-run.vv"
  status "$mode-constrain" 0
  run "$mode-readback" "$eb" --show-symbol-table "$mode-run.vv"
  status "$mode-readback" 0
done
! grep -Fq 'case qualifier assertion' default-compile.log
! grep -Fq '$case_' default-readback.log
test "$(grep -c 'case qualifier assertion' checked-compile.log)" = 6

check() {
  local label=$1 property=$2 result=$3 rc=$4 bad_frame=$5
  shift 5
  run "$label" "$eb" --bound 8 --trace --vcd "$label.vcd" --property "$property" "$@"
  status "$label" "$rc"
  grep -F "[$property]" "$label.log" | grep -E ": $result\$"
  if [[ $bad_frame != - ]]; then
    grep -E "bad frame $bad_frame([^0-9]|\$)" "$label.log"
  fi
}
for route in native default checked; do
  input=("$route-run.vv")
  if [[ $route == native ]]; then input=(--systemverilog --top qualifiers ordinary.sv); fi
  check "$route-values" qualifiers.p_values 'PROVED up to bound 8' 0 - "${input[@]}"
  check "$route-overlap-reachable" qualifiers.c_overlap PROVED 0 1 "${input[@]}"
  check "$route-hole-reachable" qualifiers.c_hole PROVED 0 2 "${input[@]}"
  check "$route-nested-reachable" qualifiers.c_nested PROVED 0 5 "${input[@]}"
done

check_generated() {
  local label=$1 tag=$2 stem=$3 result=$4 rc=$5 frame=$6
  local line id
  line=$(awk -v marker="$tag" '$0 ~ "// " marker "$" {print NR}' "$fixture")
  id=$(awk -F: -v line="$line" -v stem="$stem" \
     '$2 == line && index($0, stem) && /case qualifier assertion/ {n=split($0, fields, " "); print fields[n]}' checked-compile.log)
  [[ $id == qualifiers.* && $id != *$'\n'* ]]
  grep -Fq "$id" checked-readback.log
  check "$label" "$id" "$result" "$rc" "$frame" checked-run.vv
}
check_generated overlap-with-default overlap_with_default '$case_unique_overlap' REFUTED 10 1
check_generated overlap-without-default unique_without_default '$case_unique_overlap' REFUTED 10 1
check_generated unique-coverage unique_without_default '$case_unique_coverage' REFUTED 10 2
check_generated priority-coverage priority_without_default '$case_priority_coverage' REFUTED 10 2
check_generated nested-coverage nested_missing '$case_priority_coverage' REFUTED 10 5
check_generated guarded-coverage guarded_safe '$case_priority_coverage' 'PROVED up to bound 8' 0 -

run repeated-default "$vv" compile repeated.vv -mfcu --top repeated "$base/repeated.sv"
status repeated-default 0
run repeated-checked "$vv" compile repeated-checked.vv -mfcu --top repeated --assert-case-qualifiers "$base/repeated.sv"
status repeated-checked 2
grep -Eq 'repeated.sv:[0-9]+:[0-9]+: unsupported case qualifier assertion in this procedural context' repeated-checked.log
run effect "$vv" compile effect.vv -mfcu --top effect --assert-case-qualifiers "$base/effect.sv"
status effect 2
grep -Eq 'effect.sv:[0-9]+:[0-9]+: unsupported expression effect in a checked case' effect.log
sed 's/unique case (/unique0 case (/' "$fixture" > unique0.sv
run unique0 "$vv" compile unique0.vv -mfcu --top qualifiers --assert-case-qualifiers unique0.sv
status unique0 2
grep -Eq 'unique0.sv:[0-9]+:[0-9]+: unsupported case matching mode' unique0.log
sed 's/unique case (/unique casez (/' "$fixture" > wildcard.sv
run wildcard "$vv" compile wildcard.vv -mfcu --top qualifiers --assert-case-qualifiers wildcard.sv
status wildcard 2
grep -Eq 'wildcard.sv:[0-9]+:[0-9]+: unsupported case matching mode' wildcard.log
printf '%s\n' 'Procedural case qualifiers PASS'
