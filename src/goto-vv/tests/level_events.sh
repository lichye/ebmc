#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
base=$(cd -- "$(dirname -- "$0")" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
src="$base/level_events.sv"
invalid="$base/level_events_invalid.sv"

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
  local label=$1 top=$2
  shift 2
  run "$label" "$vv" compile "$label.vv" -mfcu --threads 1 --top "$top" "$@"
}

: > empty
for mode in levels split implicit; do
  flags=()
  case "$mode" in
    split) flags=(-DSPLIT_VECTOR) ;;
    implicit) flags=(-DIMPLICIT_REFERENCE -DFLAT_REFERENCE) ;;
  esac
  compile "$mode" level_events "${flags[@]}" "$src"
  status "$mode" 0
  run "$mode-bind" "$vv" constrain "$mode.vv" empty "$mode-run.vv"
  status "$mode-bind" 0
  run "$mode-trans" "$eb" --show-trans "$mode-run.vv"
  status "$mode-trans" 0
  grep -Fq 'State variables:' "$mode-trans.log"
  if sed -n '/^State variables:/,$p' "$mode-trans.log" | grep -Eq '\$local_|\.tmp\b'; then
    exit 1
  fi
done
for backend in minisat yices; do
  solver=()
  if [[ $backend == yices ]]; then solver=(--yices); fi
  for mode in levels split implicit native; do
    input=("$mode-run.vv")
    if [[ $mode == native ]]; then
      input=(--systemverilog --top level_events -D IMPLICIT_REFERENCE -D FLAT_REFERENCE "$src")
    fi
    for property in p_q p_address p_single p_wrong; do
      label="$backend-$mode-$property"
      id="level_events.$property"
      run "$label" "$eb" "${solver[@]}" --bound 2 --trace --vcd "$label.vcd" --property "$id" "${input[@]}"
      if [[ $property == p_wrong ]]; then
        status "$label" 10
        grep -F "[$id]" "$label.log" | grep -Fq ': REFUTED'
        grep -Eq 'finite bad frame 0([^0-9]|$)' "$label.log"
      else
        status "$label" 0
        grep -F "[$id]" "$label.log" | grep -Fq ': PROVED up to bound 2'
      fi
    done
  done
done

reject() {
  local label=$1 top=$2 where=$3 diagnostic=$4
  shift 4
  compile "$label" "$top" "$@"
  status "$label" 2
  test ! -e "$label.vv"
  grep -E "$where.sv:[0-9]+:[0-9]+: $diagnostic" "$label.log"
}
reject missing level_events level_events 'level sensitivity is missing level_events\.addr level_events\.b level_events\.sel$' \
  -DMISSING_B -DMISSING_ADDR -DMISSING_GUARD "$src"
reject partial level_events level_events 'level sensitivity is missing level_events\.vec \(bits 3:2\)$' -DPARTIAL_VECTOR "$src"
reject mixed level_events level_events 'mixed level and edge sensitivity is unsupported$' -DMIXED_EDGES "$src"
reject local level_events level_events 'combinational local read before assignment: tmp$' -DUNASSIGNED_TEMP "$src"
reject latch level_events level_events 'level-sensitive process has an incomplete assignment to level_events\.mono_q$' -DLATCH "$src"
reject hold level_events level_events 'dynamic write in a level-sensitive process needs an unconditional whole assignment$' -DDYNAMIC_HOLD "$src"
for top in derived_event dynamic_event; do
  reject "$top" "$top" level_events_invalid 'cannot determine level sensitivity for this event expression$' "$invalid"
done
reject partial_array partial_array level_events_invalid 'level sensitivity is missing partial_array\.mem' "$invalid"
reject hidden_read hidden_read level_events_invalid 'level sensitivity is missing hidden_read\.extra$' "$invalid"
reject timed_write timed_write level_events_invalid 'level-sensitive process requires untimed blocking writes$' "$invalid"
reject self_read self_read level_events_invalid 'level-sensitive process reads a value it drives: self_read\.q$' "$invalid"
reject constant_body constant_body level_events_invalid 'level-sensitive process has no external reads$' "$invalid"
printf '%s\n' 'Explicit level sensitivities PASS'
