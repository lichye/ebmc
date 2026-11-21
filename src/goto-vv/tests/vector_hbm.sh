#!/usr/bin/env bash
# Replay PLENA vhbm-l3 from an external copy of the RTL and original wrapper.
set -euo pipefail
ulimit -c 0
tests=$(cd "$(dirname "$0")" && pwd)
root=$(realpath "${1:?input root containing rtl/ and spec/}")
mode=${2:?original, high or low}
dest=$(realpath -m "${3:?fresh output directory}")
eb=$(realpath "${EBMC:-$tests/../../ebmc/ebmc}")
vv=$(realpath "${VV_BIN:-$tests/../build/goto-vv}")
top=plena_kinds_vhbm_l3_spec
inc="$root/rtl/src/memory/HBM/TileLink_Lib"
wrapper="$root/spec/$top.sv"
case "$mode" in original|high|low) ;; *) echo "unknown mode $mode" >&2; exit 2 ;; esac
mkdir "$dest"
cd "$dest"
mkdir native typed
sha256sum "$eb" "$vv" >tools.sha256
"$eb" --version >version
native=(--systemverilog --top "$top" -D SYNTHESIS -I "$inc")
front=(--single-unit --top "$top" -DSYNTHESIS -I "$inc")
: >env
: >expected
expect() {
  local status=$1
  shift
  for label in "$@"; do printf '%s.%s\t%s\n' "$top" "$label" "$status"; done >>expected
}
expect ASSUMED env_scale_d_ack_data env_consumer_ready
if [[ $mode == original ]]; then
  check=(--undriven seq --bound 20)
  expect 'PROVED up to bound 20' vjoin_accepts_both vjoin_scale_idle_ready vscale_capture_env \
    vscale_slice_hold vscale_slice_load velem_valid_sel vwrite_ready_scale vlow_mask_tie
  expected_rc=0
else
  awk -v extra="$tests/vhbm_transaction.svh" '/^[[:space:]]*endmodule[[:space:]]*$/ {
    n++; while((getline line < extra)>0) print line; close(extra)
  } {print} END{if(n!=1)exit 1}' "$wrapper" >"$top.sv"
  wrapper="$PWD/$top.sv"
  if [[ $mode == low ]]; then front+=(-DLOW_PRECISION); native+=(-D LOW_PRECISION); fi
  printf 'reset %s.rst\n' "$top" >env
  native+=(--reset "$top.rst")
  # The schedule saturates at 13. Sixteen transitions cover the last transfer
  # and leave room for the stable suffix without expanding the B12 search.
  check=(--undriven seq --bound 12 --witness-lasso "${WITNESS_LASSO:-16}" --trace --vcd wave)
  expect ASSUMED schedule_cmd schedule_a schedule_e schedule_s quiet_b
  expect 'PROVED up to bound 12' selected low_mask scale_accept element_accept scale_capture \
    scale_slice element_capture element_slice no_early_join hold_first join_value joined_once
  expect REFUTED scale_inverse join_inverse
  expect PROVED scale_handshake transaction
  while IFS=$'\t' read -r id status; do
    if [[ $status != ASSUMED ]]; then check+=(--property "$id"); fi
  done <expected
  expected_rc=10
fi
LC_ALL=C sort expected >expected.tsv
rtl="$root/rtl/src"
printf '\x60include "%s"\n' "$inc/tl_pkg.svh" "$rtl/basic_components/common/rtl/join2.sv" \
  "$rtl/basic_components/common/rtl/register_slice.sv" "$inc/tl_adapter.sv" "$inc/tl_selector.sv" \
  "$rtl/memory/HBM/rtl/double_precision_v_hbm_controller.sv" "$rtl/memory/HBM/rtl/tl_master.sv" \
  "$wrapper" >unit.sv
run() {
  local tag=$1 want=$2 rc=0
  shift 2
  printf '%q ' "$@" >"$tag.cmd"
  /usr/bin/time -f '%e %M %x' -o "$tag.time" timeout 600 "$@" >"$tag.log" 2>&1 || rc=$?
  echo "$rc" >"$tag.rc"
  test "$rc" = "$want"
}
run compile 0 "$vv" compile vector.vv "${front[@]}" unit.sv
run constrain 0 "$vv" constrain vector.vv env checked.vv
(cd native; run check "$expected_rc" "$eb" "${check[@]}" "${native[@]}" "$dest/unit.sv")
(cd typed; run check "$expected_rc" "$eb" "${check[@]}" "$dest/checked.vv")
for route in native typed; do
  log="$route/check.log"
  if grep -F 'warning: ignoring' "$log"; then exit 1; fi
  sed -nE 's/^\[([^]]+)\].*: (.*)$/\1\t\2/p' "$log" | LC_ALL=C sort >"$route/all.tsv"
  awk -F '\t' -v p="$top." 'index($1,p)==1 && substr($1,length(p)+1)!~/\./' "$route/all.tsv" >"$route/wrapper.tsv"
  diff -u expected.tsv "$route/wrapper.tsv"
  if [[ $mode == original ]]; then
    test "$(wc -l <"$route/all.tsv")" = 16
    for child in high_precision_element_master low_precision_element_master scale_master; do
      for i in 1 2; do
        if [[ $route == native ]]; then
          param=LOAD_AMOUNT; if [[ $i == 2 ]]; then param=WRITE_AMOUNT; fi
          grep -F "$top.u_dut.$child." "$log" | grep -F ".assert.$i]" | grep -F "$param > 0: PROVED up to bound 20" >/dev/null
        else
          label='$assert_'$((54+i))'_5'
          grep -Fx "$top.u_dut.$child.$label"$'\t''PROVED up to bound 20' "$route/all.tsv" >/dev/null
        fi
      done
    done
  else
    test "$(wc -l <"$route/all.tsv")" = 23
    gawk -v mode="$mode" -v top="$top" -f "$tests/vhbm_wave.awk" \
      "$log" "$route/wave.$top.transaction_witness.vcd" >"$route/samples.tsv"
  fi
done
if [[ $mode != original ]]; then cmp native/samples.tsv typed/samples.tsv; fi
echo PASS
