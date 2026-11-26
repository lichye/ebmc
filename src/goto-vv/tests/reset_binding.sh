#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
cases=$(cd -- "$(dirname -- "$0")" && pwd)
vv=${VV_BIN:?}
eb=${EBMC:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"

run() {
  local label=$1 rc=0
  shift
  printf '%q ' "$@" >"$label.command"
  printf '\n' >>"$label.command"
  "$@" >"$label.log" 2>&1 || rc=$?
  printf '%s\n' "$rc" >"$label.rc"
}
status() {
  local actual
  actual=$(cat "$1.rc")
  if [[ $actual != "$2" ]]; then
    cat "$1.log"
    printf '%s expected rc %s, got %s\n' "$1" "$2" "$actual" >&2
    return 1
  fi
}

src="$cases/reset_binding.sv"
gate="$cases/clock-gate"
printf 'reset top.rst_ni low\n' >low.bind
printf 'reset top.rst_ni\n' >high.bind
run compile "$vv" compile compiled.vv -mfcu --threads 1 --top top "$src"
status compile 0
for polarity in low high; do
  run "bind-$polarity" "$vv" constrain compiled.vv "$polarity.bind" "$polarity.vv"
  status "bind-$polarity" 0
  run "readback-$polarity" "$eb" --show-trans "$polarity.vv"
  status "readback-$polarity" 0
  run "override-$polarity" "$eb" --show-trans --reset '!top.rst_ni' "$polarity.vv"
  status "override-$polarity" 6
  grep -Fx 'error: goto-vv reset and floating policy must match the constrained object' "override-$polarity.log"
done
grep -E '^"design".* "reset_polarity" [0-9]+' low.vv
test "$(head -n 1 low.vv)" = "$(head -n 1 high.vv) reset-low"
if grep -qE '^"design".* "reset_polarity" [0-9]+' high.vv; then
  exit 1
fi

reject_binding() {
  local label=$1 binding=$2 pattern=$3 object=${4:-compiled.vv}
  printf '%s\n' "$binding" >"$label.bind"
  run "$label" "$vv" constrain "$object" "$label.bind" "$label.vv"
  status "$label" 2
  test ! -e "$label.vv"
  grep -E "^$label\.bind:$pattern$" "$label.log"
}
reject_binding unknown-polarity 'reset top.rst_ni middle' '1: expected reset NAME \[low\]'
reject_binding extra-token 'reset top.rst_ni low extra' '1: expected reset NAME \[low\]'
reject_binding duplicate $'reset top.rst_ni low\nreset top.rst_ni' '2: invalid initial reset'
reject_binding unknown-input 'reset top.missing low' '1: unbound input top\.missing'
reject_binding not-input 'reset top.carried low' '1: unbound input top\.carried'
reject_binding wide-input 'reset top.wide low' '1: invalid initial reset'
printf 'reset top.u.data low\n' >child-input.bind
run child-input "$vv" constrain compiled.vv child-input.bind child-input.vv
status child-input 2
test ! -e child-input.vv
grep -E 'reset_binding\.sv:[0-9]+: invalid reset binding$' child-input.log

# Keep all node indices intact and edit the named fields of the design node.
sed -E '/^"design" /s/ "reset" [0-9]+//' low.vv |
  awk '$1=="\"design\"" {--$3} {print}' >orphan.vv
awk '$1=="\"design\"" {
       for (i=4; i<NF; i+=2) {
         if ($i=="\"reset_polarity\"") polarity=i+1
         if ($i=="\"undriven\"") undriven=$(i+1)
       }
       if (!polarity || !undriven) exit 1
       $polarity=undriven
     }
     {print}' low.vv >stored-unknown.vv
for label in orphan stored-unknown; do
  run "$label" "$eb" --show-trans "$label.vv"
  status "$label" 6
done
grep -E 'reset_binding\.sv:[0-9]+: reset polarity requires a reset binding$' orphan.log
grep -E 'reset_binding\.sv:[0-9]+: invalid reset polarity$' stored-unknown.log
{
  head -n 1 high.vv
  tail -n +2 low.vv
} >legacy-with-low-field.vv
{
  head -n 1 low.vv
  tail -n +2 high.vv
} >low-marker-without-field.vv
for label in legacy-with-low-field low-marker-without-field; do
  run "$label" "$eb" --show-trans "$label.vv"
  status "$label" 6
  grep -F 'malformed typed object' "$label.log"
done

run gate-compile "$vv" compile gate-compiled.vv -mfcu --threads 1 --top gated \
  -DSYNTHESIS -DUSE_GENERIC --clock-gate-contract "$gate/gated.contract" \
  "$gate/coral-reset.sv" "$gate/gated.sv"
status gate-compile 0
reject_binding gate-single-low 'reset gated.rst_ni low' \
  '1: single-cycle reset conflicts with ENV2; use clock-gate-start-reset' gate-compiled.vv

engines=(bit)
if [[ ${VV_TEST_YICES:-0} == 1 ]]; then engines+=(word); fi
printf 'engine\tbinding\troute\tproperty\trc\tbad_frame\n' >matrix.tsv
for engine in "${engines[@]}"; do
  flags=(--aig)
  if [[ $engine == word ]]; then flags=(--yices); fi
  for polarity in low high; do
    active=top.rst_ni
    goals=(p_low0 p_low1 p_high0 p_high1 "c_$polarity")
    if [[ $polarity == low ]]; then
      active=!top.rst_ni
      goals+=(p_reset_effect)
    fi
    for route in native typed; do
      input=("$polarity.vv")
      if [[ $route == native ]]; then
        input=(--systemverilog --top top --reset "$active" "$src")
      fi
      for goal in "${goals[@]}"; do
        label="$engine-$polarity-$route-$goal"
        run "$label" "$eb" "${flags[@]}" --bound 1 --trace --vcd "$label.vcd" \
          --property "top.$goal" "${input[@]}"
        rc=0
        frame=-
        expected='PROVED up to bound 1'
        if [[ $goal == c_* ]]; then expected=PROVED; fi
        if [[ $goal == p_low* && $polarity == high || $goal == p_high* && $polarity == low ]]; then
          rc=10
          frame="${goal: -1}"
          expected=REFUTED
        fi
        status "$label" "$rc"
        grep -F "[top.$goal]" "$label.log" | grep -F ": $expected"
        if [[ $frame != - ]]; then
          grep -E "bad frame $frame([^0-9]|$)" "$label.log"
        fi
        printf '%s\t%s\t%s\t%s\t%s\t%s\n' "$engine" "$polarity" "$route" "$goal" "$rc" "$frame" >>matrix.tsv
      done
    done
  done
done
printf 'Single-cycle reset binding PASS\n'
