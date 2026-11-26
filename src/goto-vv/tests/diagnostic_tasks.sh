#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
front=$(cd "$(dirname "$0")/.." && pwd)
eb=${EBMC:-$front/../ebmc/ebmc}
vv=${VV_BIN:-$front/build/goto-vv}
source_file="$front/tests/diagnostic_tasks.sv"
work=$(realpath -m "${1:?give a directory for test outputs}")
mkdir -p "$work"
cd "$work"
: >empty

compile() {
  local label=$1 top=$2
  shift 2
  "$vv" compile "$label.vv" --single-unit --top "$top" "$@" "$source_file" >"$label.compile" 2>&1
  "$vv" constrain "$label.vv" empty "$label-check.vv" >"$label.constrain" 2>&1
  "$eb" --show-properties --xml-ui "$label-check.vv" >"$label.properties" 2>&1
}

reject() {
  local label=$1 top=$2 pattern=$3 status=0
  shift 3
  "$vv" compile "$label.vv" --single-unit --top "$top" "$@" "$source_file" >"$label.compile" 2>&1 || status=$?
  [[ $status == 2 && ! -e $label.vv ]]
  grep -Eq -- "$pattern" "$label.compile"
}

property() {
  local label=$1 prefix=$2 id
  id=$(awk -v prefix="$prefix.$" '$0 ~ /: diagnostic assertion / && index($4, prefix) == 1 {print $4}' "$label.compile")
  [[ -n $id && $id != *$'\n'* ]]
  grep -Fq "<property name=\"$id\">" "$label.properties"
  printf '%s\n' "$id"
}

check() {
  local label=$1 id=$2 depth=$3 expected=$4 tag=$5 status=0
  "$eb" --undriven seq --bound "$depth" --property "$id" --trace "$label-check.vv" \
    >"$label.$tag.log" 2>&1 || status=$?
  grep -F "[$id]" "$label.$tag.log" | grep -Fq ": $expected"
  if [[ $expected == REFUTED ]]; then
    [[ $status == 10 ]]
    grep -Fq "finite bad frame $depth " "$label.$tag.log"
  else
    [[ $status == 0 ]]
  fi
}

compile default diagnostic_tasks
compile reference diagnostic_tasks -D DIAGNOSTICS_REFERENCE
for variant in default reference; do
  ! grep -Fq 'diagnostic assertion' "$variant.compile"
  ! grep -Fq '.$warning_' "$variant.properties"
  ! grep -Fq '.$info_' "$variant.properties"
  check "$variant" diagnostic_tasks.p_early 3 PROVED early
  check "$variant" diagnostic_tasks.p_effects 3 PROVED effects
  "$eb" --show-trans --undriven seq "$variant-check.vv" >"$variant.trans" 2>&1
  grep -q '^State variables:' "$variant.trans"
  sed -n '/^State variables:/,$p' "$variant.trans" >"$variant.model"
done
cmp default.model reference.model

compile warning diagnostic_tasks --assert-diagnostic-tasks=warning
[[ $(grep -c ': diagnostic assertion ' warning.compile) == 7 ]]
! grep -Fq '.$info_' warning.properties
! grep -Fq '.$error_' warning.properties
for instance in route_bad id_bad no_mismatch not_ready not_valid not_locked; do
  id=$(property warning "diagnostic_tasks.$instance")
  [[ $id == "diagnostic_tasks.$instance."\$warning_8_9 ]]
  awk -v id="$id" '
    /<property name=/ { check = index($0, "name=\"" id "\"") }
    check && /<location/ && /file="[^"]*diagnostic_tasks\.sv"/ &&
      /line="8"/ && /column="9"/ { found=1 }
    END { exit !found }
  ' warning.properties
  case "$instance" in
    route_bad)
      check warning "$id" 0 PROVED route-before
      check warning "$id" 1 REFUTED route-bad
      ;;
    id_bad)
      check warning "$id" 1 PROVED id-before
      check warning "$id" 2 REFUTED id-bad
      ;;
    *)
      check warning "$id" 3 PROVED "$instance"
      ;;
  esac
done
grep -E 'diagnostic_tasks\.sv:8:9: diagnostic assertion diagnostic_tasks\.route_bad\.' warning.compile
body_id=$(property warning diagnostic_tasks)
check warning "$body_id" 0 PROVED before-blocking-write
check warning "$body_id" 1 REFUTED after-blocking-write
check warning diagnostic_tasks.p_effects 3 PROVED effects

reject default-error diagnostic_errors 'diagnostic_tasks\.sv:[0-9]+:[0-9]+: unsupported procedural call \$error'
reject warning-only-error diagnostic_errors 'diagnostic_tasks\.sv:[0-9]+:[0-9]+: unsupported procedural call \$error' --assert-diagnostic-tasks=warning
compile error diagnostic_errors --assert-diagnostic-tasks=error
[[ $(grep -c ': diagnostic assertion ' error.compile) == 1 ]]
! grep -Fq '.$warning_' error.properties
error_id=$(property error diagnostic_errors)
[[ $error_id == *'.$error_'* ]]
check error "$error_id" 1 PROVED before-error
check error "$error_id" 2 REFUTED error
compile both diagnostic_errors --assert-diagnostic-tasks=warning,error
[[ $(grep -c ': diagnostic assertion ' both.compile) == 2 ]]
grep -Fq "<property name=\"$error_id\">" both.properties
warning_id=$(awk '$0 ~ /: diagnostic assertion / && /\(\$warning\)$/ { print $4 }' both.compile)
check both "$warning_id" 1 REFUTED warning

compile action-default diagnostic_action
! grep -Fq 'diagnostic assertion' action-default.compile
check action-default diagnostic_action.p_count 3 PROVED count
check action-default diagnostic_action.p_legacy 2 REFUTED legacy
compile action-both diagnostic_action --assert-diagnostic-tasks=warning,error
[[ $(grep -c ': diagnostic assertion ' action-both.compile) == 2 ]]
for task in warning error; do
  id=$(awk -v task="(\$$task)" '$0 ~ /: diagnostic assertion / && $5 == task { print $4 }' action-both.compile)
  depth=1
  if [[ $task == error ]]; then depth=2; fi
  check action-both "$id" "$((depth - 1))" PROVED "before-$task"
  check action-both "$id" "$depth" REFUTED "$task"
done
check action-both diagnostic_action.p_count 3 PROVED count

reject nested diagnostic_nested_effect 'diagnostic_tasks\.sv:[0-9]+:[0-9]+: unsupported write in diagnostic argument'
reject multiple diagnostic_multiple_effects 'diagnostic_tasks\.sv:[0-9]+:[0-9]+: multiple side effects in diagnostic arguments' --assert-diagnostic-tasks=warning
reject unknown-call diagnostic_unknown_call 'diagnostic_tasks\.sv:[0-9]+:[0-9]+: unsupported call in diagnostic argument \$urandom'
for task in fatal finish; do
  reject "$task" "diagnostic_$task" "diagnostic_tasks\\.sv:[0-9]+:[0-9]+: unsupported procedural call .*$task" --assert-diagnostic-tasks=warning,error
done
reject invalid-option diagnostic_tasks '--assert-diagnostic-tasks expects a comma-separated selection' --assert-diagnostic-tasks=info
reject elab-error diagnostic_elaboration_error 'elaboration error'
reject elab-error-option diagnostic_elaboration_error 'elaboration error' --assert-diagnostic-tasks=warning,error
cmp elab-error.compile elab-error-option.compile
for mode in default selected; do
  flags=()
  if [[ $mode == selected ]]; then flags=(--assert-diagnostic-tasks=warning,error); fi
  status=0
  "$vv" compile "elab-warning-$mode.vv" --single-unit --top diagnostic_elaboration_warning     "${flags[@]}" "$source_file" >"elab-warning-$mode.compile" 2>&1 || status=$?
  printf '%s\n' "$status" >"elab-warning-$mode.rc"
  grep -Fq 'elaboration warning' "elab-warning-$mode.compile"
  ! grep -Fq 'diagnostic assertion' "elab-warning-$mode.compile"
done
cmp elab-warning-{default,selected}.rc
cmp elab-warning-{default,selected}.compile
compile final diagnostic_final
! grep -Fq 'diagnostic assertion' final.compile
reject final-warning diagnostic_final 'diagnostic_tasks\.sv:[0-9]+:[0-9]+: unsupported reachable final call \$warning' --assert-diagnostic-tasks=warning
reject ref-effect diagnostic_ref_effect 'diagnostic_tasks\.sv:[0-9]+:[0-9]+: runtime function requires input formals'
compile function-argument diagnostic_function_argument
check function-argument diagnostic_function_argument.p_unchanged 3 PROVED unchanged
echo 'Diagnostic tasks PASS'
