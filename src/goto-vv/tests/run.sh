#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
front=$(cd "$(dirname "$0")/.." && pwd)
eb=${EBMC:-$front/../ebmc/ebmc}
vv=${VV_BIN:-$front/build/goto-vv}
work=$(realpath -m "${1:?give a directory for test outputs}")
mkdir -p "$work"
cd "$work"
: >empty
echo 'reset bridge.rst' >reset
echo "assume env_zero posedge bridge.clk bridge.a == '0" >env
cat reset env >both
compile() { "$vv" compile "$2" --single-unit --top "$1" "$3"; }
bind() { "$vv" constrain "$1" "$2" "$3"; }
run() {
  local log=$1
  shift
  local rc=0
  "$@" >"$log" 2>&1 || rc=$?
  echo "$rc" >"$log.rc"
}
compare() {
  local a=$1 b=$2 property=$3 expected=$4
  local result="^\[$property\].*: $expected$"
  grep -E "$result" "$a" > /dev/null
  grep -E "$result" "$b" > /dev/null
  cmp "$a.rc" "$b.rc"
  printf '%s\t%s\t%s\t%s\n' "$a" "$property" "$expected" "$(cat "$a.rc")"
}
compile bridge bridge-1.vv "$front/tests/bridge.sv"
compile bridge bridge-2.vv "$front/tests/bridge.sv"
cmp bridge-{1,2}.vv
for config in empty reset env both; do
  bind bridge-1.vv "$config" "$config.vv"
  src="$front/tests/bridge.sv"; flags=()
  if [[ $config = env || $config = both ]]; then src="$front/tests/bridge-env.sv"; fi
  if [[ $config = reset || $config = both ]]; then flags+=(--reset bridge.rst); fi
  common=(--bound 2 --undriven seq --property bridge.u.p_clean --trace)
  run "$config.direct" "$eb" "${common[@]}" --top bridge "${flags[@]}" "$src"
  run "$config.typed" "$eb" "${common[@]}" "$config.vv"
  expected=REFUTED; if [[ $config = both ]]; then expected='PROVED up to bound 2'; fi
  compare "$config.direct" "$config.typed" bridge.u.p_clean "$expected"
done
run typed-symbols "$eb" --show-symbol-table both.vv
test "$(cat typed-symbols.rc)" = 0
grep -Fq 'Verilog::$root.bridge.u.q' typed-symbols
compile bridge live.vv "$front/tests/bridge-cover.sv"
bind live.vv reset live-constrained.vv
run live.direct "$eb" --top bridge --reset bridge.rst --undriven seq --bound 2 --trace --property bridge.u.live "$front/tests/bridge-cover.sv"
run live.typed "$eb" --undriven seq --bound 2 --trace --property bridge.u.live live-constrained.vv
compare live.direct live.typed bridge.u.live PROVED
EBMC="$eb" VV_BIN="$vv" bash "$front/tests/clock_gate_contract.sh" "$work/clock-gate"
EBMC="$eb" VV_BIN="$vv" bash "$front/tests/clock_gate_cascade.sh" "$work/clock-gate-cascade"
for top in always_clock_reset type_parameter temporal sampled_sequence sampled_rose wires generate_nested open_outputs local_enum clock_reset clock_reset_low packed_fields decoder_features immediate generate_scopes fixed_arithmetic counted_break wildcard_case scalar_concat string_parameter compound_field packed_pattern empty_member zero_replication packed_select automatic_locals constant_guard packed_range packed_write counted_packed combinational_locals packed_read counted_guard empty_final reduction_xor assignment_step module_loop_index packed_bank_write modulo_bank_read past_depth packed_runtime_read packed_runtime_read_states; do
  src="$front/tests/$top.sv"
  compile "$top" "$top.vv" "$src"
  bind "$top.vv" empty "$top-check.vv"
  direct_src="$src"
  direct_flags=()
  if [[ $top == packed_select ]]; then
    direct_src="$front/tests/packed_select_flat.sv"
  elif [[ $top == packed_write ]]; then
    direct_flags=(-D NATIVE_STATIC_WRITE_REFERENCE)
  elif [[ $top == counted_packed ]]; then
    direct_flags=(-D NATIVE_FLAT_LOOP_REFERENCE)
  fi
  depth=3
  if [[ $top == packed_read ]]; then depth=15; fi
  if [[ $top == past_depth || $top == sampled_rose ]]; then depth=6; fi
  run "$top.direct" "$eb" --top "$top" --undriven seq --bound "$depth" --trace "${direct_flags[@]}" "$direct_src"
  run "$top.typed" "$eb" --undriven seq --bound "$depth" --trace "$top-check.vv"
  while read -r name expected; do
    compare "$top.direct" "$top.typed" "$top.$name" "$expected"
  done < "$front/tests/$top.expected"
done
run type-parameter-unsupported compile type_parameter_unsupported type-parameter-unsupported.vv "$front/tests/type_parameter_unsupported.sv"
grep -qE 'type_parameter_unsupported\.sv:2:[0-9]+: unsupported type string' type-parameter-unsupported
test "$(cat type-parameter-unsupported.rc)" = 2
test ! -e type-parameter-unsupported.vv
rose_source="$front/tests/sampled_rose.sv"
sed '/^  assume_next:/d' "$rose_source" >rose-no-assume.sv
compile sampled_rose rose-no-assume.vv rose-no-assume.sv
bind rose-no-assume.vv empty rose-no-assume-check.vv
rose_opts=(--undriven seq --bound 2 --property sampled_rose.next_blocked --trace)
run rose-assumed.direct "$eb" "${rose_opts[@]}" --top sampled_rose "$rose_source"
run rose-assumed.typed "$eb" "${rose_opts[@]}" sampled_rose-check.vv
run rose-unassumed.direct "$eb" "${rose_opts[@]}" --top sampled_rose rose-no-assume.sv
run rose-unassumed.typed "$eb" "${rose_opts[@]}" rose-no-assume-check.vv
compare rose-assumed.direct rose-assumed.typed sampled_rose.next_blocked 'REFUTED up to bound 2'
compare rose-unassumed.direct rose-unassumed.typed sampled_rose.next_blocked PROVED

rose_invalid="$front/tests/sampled_rose_invalid.sv"
for kind in explicit_clock procedural function direct_x direct_z folded_x folded_z; do
  log="rose-invalid-$kind"
  run "$log" compile "rose_$kind" "$log.vv" "$rose_invalid"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
for kind in explicit_clock procedural; do
  grep -Eq 'sampled_rose_invalid.sv:[0-9]+:[0-9]+: \$rose requires the enclosing property' "rose-invalid-$kind"
done
grep -Eq 'sampled_rose_invalid.sv:[0-9]+:[0-9]+: error: could not determine clocking event' rose-invalid-function
for kind in direct_x direct_z folded_x folded_z; do
  grep -Eq 'sampled_rose_invalid.sv:[0-9]+:[0-9]+: unsupported X/Z constant argument to \$rose' "rose-invalid-$kind"
done
grep -Fq 'generate_nested.outer.lanes[0].even_lane.u' generate_nested.vv
grep -Fq 'generate_nested.outer.lanes[1].odd_lane.u' generate_nested.vv
if grep -Fq 'unused_lane' generate_nested.vv; then
  echo 'Uninstantiated generate scope was emitted' >&2
  exit 1
fi
if grep -Fq 'warning: ignoring' packed_fields.direct packed_fields.typed; then
  echo 'Packed field extraction did not reach the solver' >&2
  exit 1
fi
for name in first_zero first_nonzero; do
  flags=(--bound 0 --undriven seq --property "sampled_sequence.$name" --trace)
  run "$name.direct" "$eb" "${flags[@]}" --top sampled_sequence "$front/tests/sampled_sequence.sv"
  run "$name.typed" "$eb" "${flags[@]}" sampled_sequence-check.vv
  if [[ $name = first_zero ]]; then
    compare "$name.direct" "$name.typed" "sampled_sequence.$name" 'PROVED up to bound 0'
    test "$(cat "$name.direct.rc")" = 0
  else
    compare "$name.direct" "$name.typed" "sampled_sequence.$name" REFUTED
    test "$(cat "$name.direct.rc")" = 10
  fi
done
sed 's/if (rst) q/if (data) q/' "$front/tests/clock_reset.sv" >wrong-reset.sv
run wrong-reset compile clock_reset rejected.vv wrong-reset.sv
grep -q 'wrong-reset.sv:[0-9].*reset edge matching the outer if' wrong-reset
test "$(cat wrong-reset.rc)" != 0
# Check the active-low edge, signal identity, and the serialized guard.
for kind in condition signal; do
  if [[ $kind = condition ]]; then
    sed 's/if (!rst_n)/if (rst_n)/' "$front/tests/clock_reset_low.sv" >"wrong-reset-$kind.sv"
  else
    sed 's/if (!rst_n)/if (!data)/' "$front/tests/clock_reset_low.sv" >"wrong-reset-$kind.sv"
  fi
  run "wrong-reset-$kind" compile clock_reset_low "rejected-$kind.vv" "wrong-reset-$kind.sv"
  grep -qE "wrong-reset-$kind.sv:3:[0-9]+: expected a reset edge matching the outer if" "wrong-reset-$kind"
  test "$(cat "wrong-reset-$kind.rc")" = 2
  test ! -e "rejected-$kind.vv"
done
for route in direct typed; do
  grep -Fq 'finite bad frame 1 has an infinite extension by totality' "clock_reset_low.$route"
done
grep -q '^"negedge" ' clock_reset_low-check.vv
sed 's/^"negedge" /"posedge" /' clock_reset_low-check.vv >wrong-reset-object.vv
run wrong-reset-object "$eb" --show-symbol-table wrong-reset-object.vv
grep -qE 'clock_reset_low\.sv:3:.*invalid clock/reset guard in always_ff' wrong-reset-object
test "$(cat wrong-reset-object.rc)" = 6
for polarity in high low; do
  if [[ $polarity = high ]]; then
    line=3
    sed 's/if (rst)/if (!rst)/' "$front/tests/always_clock_reset.sv" >"always-wrong-$polarity.sv"
  else
    line=9
    sed 's/if (!rst_n)/if (rst_n)/' "$front/tests/always_clock_reset.sv" >"always-wrong-$polarity.sv"
  fi
  run "always-wrong-$polarity" compile always_clock_reset "always-wrong-$polarity.vv" "always-wrong-$polarity.sv"
  grep -qE "always-wrong-$polarity.sv:$line:[0-9]+: expected a reset edge matching the outer if" "always-wrong-$polarity"
  test "$(cat "always-wrong-$polarity.rc")" = 2
  test ! -e "always-wrong-$polarity.vv"
  for route in direct typed; do
    grep -Fq "always_clock_reset.p_${polarity}_bad: finite bad frame 1 has an infinite extension by totality" "always_clock_reset.$route"
  done
done
grep -q '^"verilog_always" ' always_clock_reset-check.vv
if grep -q '^"verilog_always_ff" ' always_clock_reset-check.vv; then exit 1; fi
sed 's/^"negedge" /"posedge" /' always_clock_reset-check.vv >always-wrong-object.vv
run always-wrong-object "$eb" --show-symbol-table always-wrong-object.vv
grep -qE 'always_clock_reset\.sv:9:.*invalid clock/reset guard in always$' always-wrong-object
test "$(cat always-wrong-object.rc)" = 6
# A semantic analysis error must stop compilation before an object is written.
sed '/always_ff/i\  initial q = 0;' "$front/tests/bridge.sv" >driver-conflict.sv
run driver-conflict compile bridge driver-rejected.vv driver-conflict.sv
grep -qE 'driver-conflict.sv:[0-9]+:[0-9]+:.*always_ff.*(written|driv)' driver-conflict
test "$(cat driver-conflict.rc)" = 2
test ! -e driver-rejected.vv
sed 's/"base.hex"/"image.hex"/' "$front/tests/string_parameter.sv" >string-file.sv
run string-file compile string_parameter string-file.vv string-file.sv
grep -qE 'string-file.sv:[0-9]+:[0-9]+: unsupported procedural call \$readmemh$' string-file
test "$(cat string-file.rc)" = 2
test ! -e string-file.vv
run compound-target compile compound_target compound-rejected.vv "$front/tests/compound_target.sv"
grep -qE 'compound_target.sv:4:[0-9]+: unsupported compound assignment target' compound-target
test "$(cat compound-target.rc)" = 2
test ! -e compound-rejected.vv
run packed-pattern-target compile packed_pattern_target packed-pattern-rejected.vv "$front/tests/packed_pattern_target.sv"
grep -qE 'packed_pattern_target.sv:3:[0-9]+: unsupported assignment pattern' packed-pattern-target
test "$(cat packed-pattern-target.rc)" = 2
test ! -e packed-pattern-rejected.vv
grep -Fqx '"1xz" 0 0' packed_pattern.vv
run empty-member-strict "$vv" compile empty-strict.vv --top empty_member -Werror=empty-member "$front/tests/empty_member.sv"
grep -qE 'empty_member.sv:7:[0-9]+: error: .*\[-Wempty-member\]' empty-member-strict
test "$(cat empty-member-strict.rc)" = 2
test ! -e empty-strict.vv
run empty-member-clocking compile empty_member_clocking empty-clocking.vv "$front/tests/empty_member_clocking.sv"
grep -qE 'empty_member_clocking.sv:2:[0-9]+: unsupported module member ClockingBlock' empty-member-clocking
test "$(cat empty-member-clocking.rc)" = 2
test ! -e empty-clocking.vv
zero_replication_invalid="$front/tests/zero_replication_invalid.sv"
for kind in empty standalone; do
  log="zero-replication-$kind"
  run "$log" compile "zero_replication_$kind" "$log.vv" "$zero_replication_invalid"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'zero_replication_invalid.sv:2:[0-9]+: error: .*concatenation' zero-replication-empty
grep -qE 'zero_replication_invalid.sv:7:[0-9]+: error: replication constant can only be zero inside' zero-replication-standalone
# A runtime index wider than the packed dimension is a guarded read (Spec 31 E9).
run packed-select-dynamic compile packed_select_dynamic packed-dynamic.vv "$front/tests/packed_select_dynamic.sv"
test "$(cat packed-select-dynamic.rc)" = 0
test -e packed-dynamic.vv
# An out-of-range runtime read of a two-state element is 0 (1800-2017 7.4.6),
# as for the explicit flat slice; the native element select leaves it free.
grep -E '^\[packed_runtime_read_states\.p_b_oob\].*: PROVED up to bound 3$' packed_runtime_read_states.typed >/dev/null
grep -E '^\[packed_runtime_read_states\.p_b_prop\].*: PROVED up to bound 3$' packed_runtime_read_states.typed >/dev/null
grep -E '^\[packed_runtime_read_states\.p_b_oob\].*: REFUTED$' packed_runtime_read_states.direct >/dev/null
run packed-select-oob "$vv" compile packed-oob.vv --top packed_select_oob -Wno-error=index-oob "$front/tests/packed_select_oob.sv"
grep -qE 'packed_select_oob.sv:2:[0-9]+: packed element select index is out of range' packed-select-oob
test "$(cat packed-select-oob.rc)" = 2
test ! -e packed-oob.vv
grep -Fqx '"1z" 0 0' packed_select.vv
grep -Fqx '"x1" 0 0' packed_select.vv
automatic_invalid="$front/tests/automatic_locals_invalid.sv"
run automatic-dynamic compile automatic_dynamic automatic-dynamic.vv "$front/tests/automatic_locals_invalid.sv"
test "$(cat automatic-dynamic.rc)" = 0
for kind in unassigned partial oob; do
  log="automatic-$kind"
  run "$log" compile "automatic_$kind" "$log.vv" "$automatic_invalid"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'automatic_locals_invalid.sv:8:[0-9]+: automatic local read before assignment: tmp' automatic-unassigned
grep -qE 'automatic_locals_invalid.sv:19:[0-9]+: unsupported assignment to an automatic local' automatic-partial
grep -qE 'automatic_locals_invalid.sv:42:[0-9]+: packed element select index is out of range' automatic-oob
run constant-guard-indivisible "$vv" compile indivisible.vv --top constant_guard \
  -G MLEN=7 "$front/tests/constant_guard.sv"
grep -qE 'constant_guard.sv:6:[0-9]+: unsupported procedural call \$error' constant-guard-indivisible
test "$(cat constant-guard-indivisible.rc)" = 2
test ! -e indivisible.vv
for kind in dynamic_error dynamic_finish unknown; do
  log="constant-guard-$kind"
  run "$log" compile "constant_guard_$kind" "$log.vv" "$front/tests/constant_guard_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'constant_guard_invalid.sv:4:[0-9]+: unsupported procedural call \$error' constant-guard-dynamic_error
grep -qE 'constant_guard_invalid.sv:15:[0-9]+: unsupported procedural call \$finish' constant-guard-dynamic_finish
grep -qE 'constant_guard_invalid.sv:23:[0-9]+: unsupported procedural call \$error' constant-guard-unknown
run packed-range-dynamic compile packed_range_dynamic packed-range-dynamic.vv "$front/tests/packed_range_dynamic.sv"
grep -qE 'packed_range_dynamic.sv:2:[0-9]+: packed range select requires known constant bounds' packed-range-dynamic
test "$(cat packed-range-dynamic.rc)" = 2
test ! -e packed-range-dynamic.vv
run packed-range-oob "$vv" compile packed-range-oob.vv --top packed_range_oob -Wno-error=range-oob "$front/tests/packed_range_oob.sv"
grep -qE 'packed_range_oob.sv:2:[0-9]+: packed range select is out of range' packed-range-oob
test "$(cat packed-range-oob.rc)" = 2
test ! -e packed-range-oob.vv
run counted-packed-dynamic compile counted_packed_dynamic counted-packed-dynamic.vv "$front/tests/counted_packed_invalid.sv"
test "$(cat counted-packed-dynamic.rc)" = 0
for kind in oob; do
  log="counted-packed-$kind"
  run "$log" compile "counted_packed_$kind" "$log.vv" "$front/tests/counted_packed_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'counted_packed_invalid.sv:16:[0-9]+: packed element select index is out of range' counted-packed-oob
for kind in live dynamic write; do
  log="counted-guard-$kind"
  run "$log" compile "counted_guard_$kind" "$log.vv" "$front/tests/counted_guard_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'counted_guard_invalid.sv:7:[0-9]+: packed element select index is out of range' counted-guard-live
grep -qE 'counted_guard_invalid.sv:18:[0-9]+: packed element select index is out of range' counted-guard-dynamic
grep -qE 'counted_guard_invalid.sv:29:[0-9]+: unsupported write in a condition' counted-guard-write
for kind in other_target other_source nonunit nonterminating; do
  log="assignment-step-$kind"
  run "$log" compile "assignment_step_$kind" "$log.vv" "$front/tests/assignment_step_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
for probe in other_target:5 other_source:14 nonunit:22; do
  kind=${probe%:*}
  line=${probe#*:}
  grep -qE "assignment_step_invalid.sv:$line:[0-9]+: expected a blocking unit increment" "assignment-step-$kind"
done
grep -qE 'assignment_step_invalid.sv:30:[0-9]+: static loop exceeds the expansion limit' assignment-step-nonterminating
for kind in write break runtime oob guard_write nested_init; do
  log="module-loop-$kind"
  run "$log" compile "module_loop_index_$kind" "$log.vv" "$front/tests/module_loop_index_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'module_loop_index_invalid.sv:7:[0-9]+: write to a statically expanded loop index' module-loop-write
grep -qE 'module_loop_index_invalid.sv:19:[0-9]+: unsupported break in a statically expanded loop' module-loop-break
grep -qE 'module_loop_index_invalid.sv:30:[0-9]+: nonconstant loop initializer' module-loop-runtime
grep -qE 'module_loop_index_invalid.sv:41:[0-9]+: packed element select index is out of range' module-loop-oob
grep -qE 'module_loop_index_invalid.sv:50:[0-9]+: unsupported write in a loop guard' module-loop-guard_write
grep -qE 'module_loop_index_invalid.sv:61:[0-9]+: unsupported write in a loop initializer' module-loop-nested_init
for kind in wide dynamic_inner oob_inner; do
  log="packed-bank-write-$kind"
  run "$log" "$vv" compile "$log.vv" --single-unit --top "packed_bank_write_$kind" \
    -Wno-error=range-oob "$front/tests/packed_bank_write_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'packed_bank_write_invalid.sv:5:[0-9]+: runtime packed bank write requires an index whose value domain matches the declared range' packed-bank-write-wide
grep -qE 'packed_bank_write_invalid.sv:15:[0-9]+: packed range select requires known constant bounds' packed-bank-write-dynamic_inner
grep -qE 'packed_bank_write_invalid.sv:24:[0-9]+: packed range select is out of range' packed-bank-write-oob_inner
run modulo-flat.direct "$eb" --top modulo_bank_read --undriven seq --bound 3 \
  -D NATIVE_FLAT "$front/tests/modulo_bank_read.sv"
while read -r name expected; do
  compare modulo-flat.direct modulo_bank_read.typed "modulo_bank_read.$name" "$expected"
done <"$front/tests/modulo_bank_read.expected"
# Remainders that can leave the declared range are guarded runtime reads
# (Spec 31 E9); the result matches the native front end.
for kind in signed zero dynamic wrong_count nonzero_base; do
  log="modulo-read-$kind"
  top="modulo_bank_read_$kind"
  run "$log" compile "$top" "$log.vv" "$front/tests/modulo_bank_read_invalid.sv"
  test "$(cat "$log.rc")" = 0
  bind "$log.vv" empty "$log-check.vv"
  run "$log.direct" "$eb" --top "$top" --undriven seq --bound 3 "$front/tests/modulo_bank_read_invalid.sv"
  run "$log.typed" "$eb" --undriven seq --bound 3 "$log-check.vv"
  compare "$log.direct" "$log.typed" "$top.p" REFUTED
done
run final-control-compile "$vv" compile final-control.vv --single-unit --top empty_final \
  -D OMIT_FINAL "$front/tests/empty_final.sv"
test "$(cat final-control-compile.rc)" = 0
bind final-control.vv empty final-control-check.vv
for variant in empty_final final-control; do
  log="$variant.trans"
  run "$log" "$eb" --undriven seq --show-trans "$variant-check.vv"
  test "$(cat "$log.rc")" = 0
  grep -q '^State variables:' "$log"
  sed -n '/^State variables:/,$p' "$log" >"$log.model"
done
cmp empty_final.trans.model final-control.trans.model
if grep -Fq -e verilog_final -e '$writememh' empty_final-check.vv; then exit 1; fi
run final-nonempty compile final_nonempty final-nonempty.vv "$front/tests/empty_final.sv"
test "$(cat final-nonempty.rc)" = 2
test ! -e final-nonempty.vv
grep -qE 'empty_final.sv:17:[0-9]+: unsupported reachable final call \$writememh' final-nonempty
for kind in dynamic unknown assignment; do
  log="final-$kind"
  run "$log" compile "final_$kind" "$log.vv" "$front/tests/empty_final_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'empty_final_invalid.sv:4:[0-9]+: final condition must be a known constant' final-dynamic
grep -qE 'empty_final_invalid.sv:12:[0-9]+: final condition must be a known constant' final-unknown
grep -qE 'empty_final_invalid.sv:22:[0-9]+: unsupported reachable final statement ExpressionStatement' final-assignment
test ! -e final-guard-dump.hex
for kind in static_init automatic_init unassigned partial missing; do
  log="comb-local-$kind"
  run "$log" compile "comb_local_$kind" "$log.vv" "$front/tests/combinational_locals_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'combinational_locals_invalid.sv:4:[0-9]+: unsupported initializer for a combinational local' comb-local-static_init
grep -qE 'combinational_locals_invalid.sv:14:[0-9]+: unsupported initializer for a combinational local' comb-local-automatic_init
grep -qE 'combinational_locals_invalid.sv:24:[0-9]+: combinational local read before assignment' comb-local-unassigned
grep -qE 'combinational_locals_invalid.sv:36:[0-9]+: combinational local read before assignment' comb-local-partial
grep -qE 'combinational_locals_invalid.sv:44:[0-9]+: combinational local is not assigned on every path' comb-local-missing
for probe in wide:3 gap:8 negative:13 lvalue:20; do
  kind=${probe%:*}
  line=${probe#*:}
  log="packed-read-$kind"
  run "$log" compile "packed_read_$kind" "$log.vv" "$front/tests/packed_read_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
  if [[ $kind == lvalue ]]; then
    grep -qE "packed_read_invalid.sv:$line:[0-9]+: packed element select requires a known constant index" "$log"
  else
    # Wide, gapped and signed runtime reads are guarded reads (Spec 31 E9),
    # see packed_runtime_read; these modules only lack a clock.
    ! grep -qE "packed element select requires" "$log"
    grep -qE "packed_read_invalid.sv:[0-9]+: unbound primary clock" "$log"
  fi
done
open_inputs="$front/tests/open_inputs.sv"
compile open_inputs open-inputs.vv "$open_inputs"
for mode in seq const; do
  printf 'undriven %s\n' "$mode" >"open-inputs-$mode.contract"
  bind open-inputs.vv "open-inputs-$mode.contract" "open-inputs-$mode.vv"
  flags=(--undriven "$mode" --finite-prefix --bound 1)
  direct="open-inputs-$mode.direct"
  typed="open-inputs-$mode.typed"
  run "$direct" "$eb" "${flags[@]}" --top open_inputs "$open_inputs"
  run "$typed" "$eb" "${flags[@]}" "open-inputs-$mode.vv"
  for prop in omitted_shared empty_shared bound default override; do
    compare "$direct" "$typed" "open_inputs.p_$prop" 'PROVED up to bound 1'
  done
  for prop in omitted_zero empty_ones open_default distinct; do
    compare "$direct" "$typed" "open_inputs.p_$prop" REFUTED
  done
  expected=REFUTED
  if [[ $mode == const ]]; then expected='PROVED up to bound 1'; fi
  for prop in omitted_hold empty_hold; do
    compare "$direct" "$typed" "open_inputs.p_$prop" "$expected"
  done
done
run open-inputs-const-metadata "$eb" --finite-prefix --bound 1 open-inputs-const.vv
compare open-inputs-const.direct open-inputs-const-metadata open_inputs.p_omitted_hold 'PROVED up to bound 1'
run open-inputs-policy-mismatch "$eb" --undriven const open-inputs-seq.vv
test "$(cat open-inputs-policy-mismatch.rc)" != 0
grep -Fq 'floating policy must match the constrained object' open-inputs-policy-mismatch
run open-inputs-report.direct "$eb" --undriven-report --top open_inputs "$open_inputs"
run open-inputs-report.typed "$eb" --undriven-report open-inputs-seq.vv
for route in direct typed; do
  log="open-inputs-report.$route"
  test "$(cat "$log.rc")" = 0
  awk -F '\t' '$1 == "UNDRIVEN" { sub(/^.*open_inputs.sv:/, "", $3); print $2, $3 }' "$log" |
    sort >"$log.normalized"
done
cmp open-inputs-report.{direct,typed}.normalized
printf '%s\n' 'open_inputs.empty_default.data 6' 'open_inputs.explicit_empty.data 1' 'open_inputs.omitted.data 1' |
  cmp - open-inputs-report.typed.normalized
printf 'undriven const\nundriven seq\n' >duplicate-undriven
run open-inputs-duplicate-policy "$vv" constrain open-inputs.vv duplicate-undriven duplicate-undriven.vv
test "$(cat open-inputs-duplicate-policy.rc)" = 2
test ! -e duplicate-undriven.vv
grep -Fq 'duplicate-undriven:2: expected a single undriven' open-inputs-duplicate-policy
for kind in ref inout implicit name; do
  log="open-inputs-bad-$kind"
  run "$log" "$vv" compile "$log.vv" --single-unit --top "open_input_bad_$kind" \
    -D "BAD_${kind^^}" "$front/tests/open_inputs_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -qE 'open_inputs_invalid.sv:1:[0-9]+: unsupported port direction' open-inputs-bad-ref
grep -qE 'open_inputs_invalid.sv:5:[0-9]+: unsupported port direction' open-inputs-bad-inout
grep -qE "open_inputs_invalid.sv:29:[0-9]+: error:.*implicit named port 'data'" open-inputs-bad-implicit
grep -qE "open_inputs_invalid.sv:37:[0-9]+: error:.*port 'typo'" open-inputs-bad-name
for kind in dynamic gate clock zero negative unknown empty; do
  log="past-depth-invalid-$kind"
  run "$log" "$vv" compile "$log.vv" --single-unit --top "past_depth_$kind" \
    -D "BAD_${kind^^}" "$front/tests/past_depth_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -Fq "past_depth_invalid.sv:3:53: error: reference to non-constant variable 'depth'" past-depth-invalid-dynamic
grep -Fq 'past_depth_invalid.sv:8:56: unsupported gate or clock argument to $past' past-depth-invalid-gate
grep -Fq 'past_depth_invalid.sv:13:58: unsupported gate or clock argument to $past' past-depth-invalid-clock
grep -Fq "past_depth_invalid.sv:18:53: error: 'number_of_ticks' argument must be greater than or equal to 1" past-depth-invalid-zero
grep -Fq "past_depth_invalid.sv:23:53: error: 'number_of_ticks' argument must be greater than or equal to 1" past-depth-invalid-negative
grep -Fq 'past_depth_invalid.sv:28:53: error: value must not have any unknown bits' past-depth-invalid-unknown
grep -Fq 'past_depth_invalid.sv:33:53: $past depth must be a static positive integer' past-depth-invalid-empty
echo "assume typo posedge bridge.clk bridge.missing == '0" >bad-contract
run bad-binding "$vv" constrain bridge-1.vv bad-contract rejected.vv
grep -q 'bad-contract:1: unbound input bridge.missing' bad-binding
sed '1s/goto-vv 1/goto-vv 9/' both.vv >bad-version.vv
run bad-version "$eb" bad-version.vv
grep -q 'bad-version.vv:1: unsupported goto-vv schema' bad-version
sed 's/posedge clk/negedge clk/' "$front/tests/bridge.sv" >bad-clock.sv
run bad-clock compile bridge rejected.vv bad-clock.sv
grep -q 'bad-clock.sv:[0-9].*posedge' bad-clock
named_property="$front/tests/named_property.sv"
compile named_property named-property.vv "$named_property"
bind named-property.vv empty named-property-check.vv
named_flags=(--undriven seq --finite-prefix --bound 3)
run named-property.direct "$eb" "${named_flags[@]}" --top named_property -D NATIVE_INLINE_REMOTE "$named_property"
run named-property.typed "$eb" "${named_flags[@]}" named-property-check.vv
while read -r name expected; do
  compare named-property.direct named-property.typed "named_property.$name" "$expected"
done <"$front/tests/named_property.expected"
test "$(cat named-property.typed.rc)" = 10
run named-property-list "$eb" --show-properties named-property-check.vv
test "$(cat named-property-list.rc)" = 0
sed -n 's/^\(named_property\.[^:]*\): .*/\1/p' named-property-list |
  sort >named-property-labels
awk '{ print "named_property." $1 }' "$front/tests/named_property.expected" |
  sort | cmp - named-property-labels
for kind in formal local negedge second_clock body; do
  log="named-property-$kind"
  run "$log" compile "named_property_$kind" "$log.vv" "$front/tests/named_property_invalid.sv"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
done
grep -Fq 'named_property_invalid.sv:5:29: named property with formal arguments' named-property-formal
grep -Fq 'named_property_invalid.sv:13:29: named property with assertion-local variables' named-property-local
grep -Fq 'named_property_invalid.sv:18:7: expected an unguarded single posedge' named-property-negedge
grep -Fq 'named_property_invalid.sv:25:7: multiple clock domains' named-property-second_clock
grep -qE 'named_property_invalid.sv:33:[0-9]+: unsupported SVA Unary' named-property-body
for repeat_case in star:7 fixed:13; do
  repeat_kind=${repeat_case%:*}
  horizon=${repeat_case#*:}
  top="sequence_$repeat_kind"
  prefix="sequence-$repeat_kind"
  src="$front/tests/$top.sv"
  compile "$top" "$prefix.vv" "$src"
  bind "$prefix.vv" empty "$prefix-check.vv"
  repeat_flags=(--undriven seq --finite-prefix)
  run "$prefix.direct" "$eb" "${repeat_flags[@]}" --bound "$horizon" --top "$top" "$src"
  run "$prefix.typed" "$eb" "${repeat_flags[@]}" --bound "$horizon" "$prefix-check.vv"
  while read -r name bad_frame; do
    property="$top.$name"
    if [[ $bad_frame = pass ]]; then
      compare "$prefix.direct" "$prefix.typed" "$property" "PROVED up to bound $horizon"
      continue
    fi
    compare "$prefix.direct" "$prefix.typed" "$property" REFUTED
    for depth in "$((bad_frame - 1))" "$bad_frame"; do
      log="$prefix-$name-d$depth"
      query=("${repeat_flags[@]}" --bound "$depth" --property "$property" --numbered-trace)
      run "$log.direct" "$eb" "${query[@]}" --top "$top" "$src"
      run "$log.typed" "$eb" "${query[@]}" "$prefix-check.vv"
      if [[ $depth = "$bad_frame" ]]; then
        compare "$log.direct" "$log.typed" "$property" REFUTED
        test "$(cat "$log.typed.rc")" = 10
        grep -Fq "Counterexample with $((bad_frame + 1)) states:" "$log.direct"
        grep -Fq "Counterexample with $((bad_frame + 1)) states:" "$log.typed"
      else
        compare "$log.direct" "$log.typed" "$property" "PROVED up to bound $depth"
        test "$(cat "$log.typed.rc")" = 0
      fi
    done
  done <"$front/tests/$top.expected"
done
star_invalid="$front/tests/sequence_star_invalid.sv"
for test_case in nonconsecutive:2 goto:6 bounded:10 positive_min:14 temporal:18 delay:22 ranged_delay:26 equal_range:30; do
  kind=${test_case%:*}
  line=${test_case#*:}
  log="sequence-star-$kind"
  run "$log" compile "sequence_star_$kind" "$log.vv" "$star_invalid"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
  case "$kind" in
    temporal) message='unsupported SVA SequenceWithMatch';;
    delay) message='unbounded sequence';;
    ranged_delay) message='ranged sequence delay next to a sequence admitting an empty match';;
    *) message='unsupported sequence repetition';;
  esac
  grep -qE "sequence_star_invalid.sv:$line:[0-9]+: $message$" "$log"
done
sed 's/\$stable(zero)/$stable(zero, @(negedge clk))/' "$front/tests/sampled_sequence.sv" >stable-clock.sv
runtime_src="$front/tests/runtime_function.sv"
compile runtime_function runtime-function.vv "$runtime_src"
bind runtime-function.vv empty runtime-function-check.vv
for depth in 1 2 3; do
  log="runtime-function-d$depth"
  flags=(--undriven seq --finite-prefix --numbered-trace --bound "$depth")
  run "$log.direct" "$eb" "${flags[@]}" --top runtime_function "$runtime_src"
  run "$log.typed" "$eb" "${flags[@]}" runtime-function-check.vv
  while read -r name expected; do
    if [[ $depth == 1 || $expected != REFUTED ]]; then
      expected="PROVED up to bound $depth"
    fi
    compare "$log.direct" "$log.typed" "runtime_function.$name" "$expected"
  done <"$front/tests/runtime_function.expected"
  if [[ $depth = 1 ]]; then
    test "$(cat "$log.typed.rc")" = 0
  else
    test "$(cat "$log.typed.rc")" = 10
    grep -Fq "Counterexample with 3 states:" "$log.typed"
    grep -Fq "Counterexample with 3 states:" "$log.direct"
  fi
done
runtime_invalid="$front/tests/runtime_function_invalid.sv"
for probe in recursive:4 out:11 ref:22 nonlocal:34 static:54 dynamic_loop:61 unsupported:44; do
  kind=${probe%:*}
  line=${probe#*:}
  log="runtime-function-$kind"
  run "$log" compile "runtime_function_$kind" "$log.vv" "$runtime_invalid"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
  case "$kind" in
    recursive) message='recursive runtime function';;
    out|ref) message='runtime function requires input formals';;
    nonlocal) message='requires a direct blocking write to a runtime function local';;
    static) message='requires a module-local automatic function with an integral return';;
    dynamic_loop) message='nonconstant loop initializer';;
    unsupported) message='unsupported statement RepeatLoop';;
  esac
  grep -qE "runtime_function_invalid.sv:$line:[0-9]+: $message$" "$log"
done
array_src="$front/tests/runtime_local_array.sv"
compile runtime_local_array runtime-local-array.vv "$array_src"
bind runtime-local-array.vv empty runtime-local-array-check.vv
array_flags=(--undriven seq --bound 2 --yices)
run runtime-array.direct "$eb" "${array_flags[@]}" --top runtime_local_array "$array_src"
run runtime-array.typed "$eb" "${array_flags[@]}" runtime-local-array-check.vv
for property in odd_tail signed_endpoints isolated_calls nested; do
  compare runtime-array.direct runtime-array.typed "runtime_local_array.$property" 'PROVED up to bound 2'
done
compare runtime-array.direct runtime-array.typed runtime_local_array.wrong_tail REFUTED
test "$(cat runtime-array.typed.rc)" = 10
for route in direct typed; do
  grep -Fq 'runtime_local_array.wrong_tail: finite bad frame 1 has an infinite extension by totality' "runtime-array.$route"
done
"$eb" --show-trans --undriven seq runtime-local-array-check.vv >runtime-array.trans
if grep -Fq '.$local_' runtime-array.trans; then
  echo 'A runtime function local escaped its call frame' >&2
  exit 1
fi
array_invalid="$front/tests/runtime_local_array_invalid.sv"
for probe in dynamic:3 static:11 nested:20 nonlocal:30; do
  kind=${probe%:*}
  line=${probe#*:}
  log="runtime-array-$kind"
  run "$log" compile "bad_array_$kind" "$log.vv" "$array_invalid"
  test "$(cat "$log.rc")" = 2
  test ! -e "$log.vv"
  message='unsupported runtime function local declaration'
  if [[ $kind = nonlocal ]]; then
    message='requires a direct blocking write to a runtime function local'
  fi
  grep -qE "runtime_local_array_invalid.sv:$line:[0-9]+: $message$" "$log"
done
scalar_reductions="$front/tests/scalar_reductions.sv"
compile scalar_reductions scalar-reductions.vv "$scalar_reductions"
bind scalar-reductions.vv empty scalar-reductions-check.vv
reduction_flags=(--undriven seq --yices --k-induction --buechi --bound 2)
run scalar-reductions.direct "$eb" "${reduction_flags[@]}" --top scalar_reductions "$scalar_reductions"
run scalar-reductions.typed "$eb" "${reduction_flags[@]}" scalar-reductions-check.vv
test "$(cat scalar-reductions.typed.rc)" = 0
for check in scalar_ok wide_ok; do
  compare scalar-reductions.direct scalar-reductions.typed "scalar_reductions.$check" PROVED
done
indexed_src="$front/tests/one_bit_indexed_read.sv"
compile one_bit_indexed_read one-bit-indexed-read.vv "$indexed_src"
bind one-bit-indexed-read.vv empty one-bit-indexed-read-check.vv
run one-bit-indexed-read.direct "$eb" "${reduction_flags[@]}" --top one_bit_indexed_read "$indexed_src"
run one-bit-indexed-read.typed "$eb" "${reduction_flags[@]}" one-bit-indexed-read-check.vv
test "$(cat one-bit-indexed-read.typed.rc)" = 0
for check in mask_ok slice_ok; do
  compare one-bit-indexed-read.direct one-bit-indexed-read.typed "one_bit_indexed_read.$check" PROVED
done
bit_select_src="$front/tests/scalar_packed_read.sv"
compile scalar_packed_read scalar-packed-read.vv "$bit_select_src"
bind scalar-packed-read.vv empty scalar-packed-read-check.vv
run scalar-packed-read.direct "$eb" "${reduction_flags[@]}" --top scalar_packed_read "$bit_select_src"
run scalar-packed-read.typed "$eb" "${reduction_flags[@]}" scalar-packed-read-check.vv
test "$(cat scalar-packed-read.typed.rc)" = 0
compare scalar-packed-read.direct scalar-packed-read.typed scalar_packed_read.p PROVED
strict_src="$front/tests/strict_packed_output.sv"
compile strict_packed_output strict-packed-output.vv "$strict_src"
bind strict-packed-output.vv empty strict-packed-output-check.vv
strict_flags=(--undriven seq --bound 1 --yices)
run strict-packed-output.direct "$eb" "${strict_flags[@]}" --top strict_packed_output "$strict_src"
run strict-packed-output.typed "$eb" "${strict_flags[@]}" strict-packed-output-check.vv
compare strict-packed-output.direct strict-packed-output.typed strict_packed_output.good 'PROVED up to bound 1'
compare strict-packed-output.direct strict-packed-output.typed strict_packed_output.wrong REFUTED
compare strict-packed-output.direct strict-packed-output.typed strict_packed_output.nonzero_good 'PROVED up to bound 1'
compare strict-packed-output.direct strict-packed-output.typed strict_packed_output.nonzero_wrong REFUTED
test "$(cat strict-packed-output.typed.rc)" = 10
for route in direct typed; do
  grep -Fq 'strict_packed_output.wrong: finite bad frame 0 has an infinite extension by totality' "strict-packed-output.$route"
  grep -Fq 'strict_packed_output.nonzero_wrong: finite bad frame 0 has an infinite extension by totality' "strict-packed-output.$route"
done
sed 's/ready_nz\[3\]/ready_nz[2]/' "$strict_src" >strict-packed-oob.sv
run strict-packed-oob "$vv" compile strict-packed-oob.vv --top strict_packed_output \
  -Wno-error=index-oob strict-packed-oob.sv
grep -Eq 'strict-packed-oob.sv:.*: packed element select index is out of range' strict-packed-oob
test "$(cat strict-packed-oob.rc)" = 2
test ! -e strict-packed-oob.vv
sed 's/ready_nz\[3\]/ready_nz[d]/' "$strict_src" >strict-packed-dynamic.sv
run strict-packed-dynamic compile strict_packed_output strict-packed-dynamic.vv strict-packed-dynamic.sv
grep -Eq 'strict-packed-dynamic.sv:.*: error: reference to non-constant variable' strict-packed-dynamic
test "$(cat strict-packed-dynamic.rc)" = 2
test ! -e strict-packed-dynamic.vv
indexed_src="$front/tests/strict_indexed_write.sv"
compile strict_indexed_write strict-indexed-write.vv "$indexed_src"
bind strict-indexed-write.vv empty strict-indexed-write-check.vv
run strict-indexed-write.direct "$eb" "${strict_flags[@]}" --top strict_indexed_write \
  -D EBMC_FIXED_REFERENCE "$indexed_src"
run strict-indexed-write.typed "$eb" "${strict_flags[@]}" strict-indexed-write-check.vv
test "$(cat strict-indexed-write.typed.rc)" = 10
for direction in down up; do
  compare strict-indexed-write.direct strict-indexed-write.typed \
    "strict_indexed_write.${direction}_good" 'PROVED up to bound 1'
  compare strict-indexed-write.direct strict-indexed-write.typed \
    "strict_indexed_write.${direction}_wrong" REFUTED
  for route in direct typed; do
    grep -Fq "strict_indexed_write.${direction}_wrong: finite bad frame 0 has an infinite extension by totality" \
      "strict-indexed-write.$route"
  done
done
indexed_controls="$front/tests/strict_indexed_write_controls.sv"
compile strict_indexed_write_controls strict-indexed-dynamic.vv "$indexed_controls"
"$vv" compile strict-indexed-oob.vv --top strict_indexed_write_controls \
  -D EBMC_STATIC_OOB -Wno-error=range-oob "$indexed_controls" >strict-indexed-oob 2>&1
grep -Eq 'strict_indexed_write_controls.sv:.*: warning: .*\[-Wrange-oob\]' strict-indexed-oob
for kind in dynamic oob; do
  grep -Fq '"verilog_indexed_part_select_plus" 3 ' "strict-indexed-$kind.vv"
done
run stable-clock compile sampled_sequence rejected.vv stable-clock.sv
grep -q 'stable-clock.sv:[0-9].*\$stable.*posedge' stable-clock
sed 's/single-posedge\/preponed\/disable-current/single-posedge\/unsampled\/disable-current/' both.vv >bad-sampling.vv
run bad-sampling "$eb" bad-sampling.vv
grep -q 'bridge.sv:[0-9].*unsupported sampling model' bad-sampling
grep -q '^"0110" 0 0$' wires.vv
awk '/^Transition system state 0/{in0=1;next} /^Transition system state 1/{in0=0} in0 && /temporal.started = 0/{seen=1} END{exit !seen}' temporal.typed
for k in bad-binding bad-version bad-clock stable-clock bad-sampling; do test "$(cat "$k.rc")" != 0; done
EBMC="$eb" VV_BIN="$vv" bash "$front/tests/compilation_units.sh" "$work/compilation-units"
EBMC="$eb" VV_BIN="$vv" bash "$front/tests/diagnostic_tasks.sh" "$work/diagnostics"
EBMC="$eb" VV_BIN="$vv" bash "$front/tests/constant_power.sh" "$work/constant-power"
EBMC="$eb" VV_BIN="$vv" bash "$front/tests/package_function.sh" "$work/package-function"
echo PASS
