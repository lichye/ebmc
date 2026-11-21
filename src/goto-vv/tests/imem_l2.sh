#!/usr/bin/env bash
# Run one imem-l2 case against an external RTL + wrapper tree.
set -euo pipefail
ulimit -c 0
tests=$(cd "$(dirname "$0")" && pwd)
root=$(realpath "${1:?supply the imem-l2 input tree with rtl/ and spec/}")
mode=${2:?original, single, alt, opcode-free, opcode-locked, or full}
dest=$(realpath -m "${3:?supply a fresh output directory}")
eb=$(realpath "${EBMC:-$tests/../../ebmc/ebmc}")
vv=$(realpath "${VV_BIN:-$tests/../build/goto-vv}")
top=plena_kinds_imem_l2_spec
inc="$root/rtl/src/memory/HBM/TileLink_Lib"
wrapper="$root/spec/$top.sv"
bound=6
defines=(SYNTHESIS)
case "$mode" in
  original) bound=20 ;;
  single) ;;
  alt) defines+=(ALT_DATA) ;;
  opcode-free|opcode-locked) defines+=(OPCODE_CONTROL) ;;
  full) bound=70; defines+=(REFILL32) ;;
  *) echo "unknown case $mode" >&2; exit 2 ;;
esac
mkdir "$dest"
cd "$dest"
mkdir native typed
sha256sum "$eb" "$vv" >tools.sha256
"$eb" --version >version
: >env
if [[ $mode != original ]]; then
  printf 'reset %s.rst\n' "$top" >env
  awk -v mode="$mode" -v extra="$tests/imem_l2_directed.svh" '
    /env_instr_d_ack_data: assume property/ { env++; if(mode=="opcode-free") next }
    /^[[:space:]]*endmodule[[:space:]]*$/ {
      modules++; while((getline line < extra)>0) print line; close(extra)
    }
    {print}
    END {if(modules!=1 || env!=1) exit 1}
  ' "$wrapper" >"$top.sv"
  wrapper="$dest/$top.sv"
fi
printf '\x60include "%s"\n' "$inc/tl_pkg.svh" \
  "$root/rtl/src/frontend/rtl/instr_mem.sv" "$root/rtl/src/memory/HBM/rtl/tl_master.sv" \
  "$wrapper" >unit.sv
awk -v mode="$mode" -v bound="$bound" -v top="$top" '
  $1==mode || ($1=="source" && mode!="opcode-free") ||
  ($1=="drive" && mode!="original") ||
  ($1=="packet" && (mode=="single" || mode=="alt" || mode=="full")) {
    verdict=$3
    if(verdict=="bounded") verdict="PROVED up to bound " bound
    if(verdict=="uncovered") verdict="REFUTED up to bound " bound
    print top "." $2 "\t" verdict
  }' "$tests/imem_l2.expected" | LC_ALL=C sort >expected.tsv
native=(--systemverilog --top "$top" -I "$inc")
front=(--single-unit --top "$top" -I "$inc")
for define in "${defines[@]}"; do native+=(-D "$define"); front+=("-D$define"); done
check=(--undriven seq --bound "$bound")
if [[ $mode != original ]]; then
  native+=(--reset "$top.rst")
  check+=(--trace --vcd wave)
  while IFS=$'\t' read -r id verdict; do
    if [[ $verdict != ASSUMED ]]; then check+=(--property "$id"); fi
  done <expected.tsv
fi
if [[ -n ${WITNESS_LASSO:-} ]]; then check+=(--witness-lasso "$WITNESS_LASSO"); fi
run() {
  local stage=$1 rc=0
  shift
  printf '%q ' "$@" >"$stage.cmd"
  /usr/bin/time -f '%e %M %x' -o "$stage.time" timeout 600 "$@" >"$stage.log" 2>&1 || rc=$?
  echo "$rc" >"$stage.rc"
}
run compile "$vv" compile imem.vv "${front[@]}" unit.sv
test "$(<compile.rc)" = 0
run constrain "$vv" constrain imem.vv env checked.vv
test "$(<constrain.rc)" = 0
(cd native; run check "$eb" "${check[@]}" "${native[@]}" "$dest/unit.sv")
(cd typed; run check "$eb" "${check[@]}" "$dest/checked.vv")
for route in native typed; do
  expect_rc=10
  if [[ $mode == original ]]; then expect_rc=0; fi
  test "$(<"$route/check.rc")" = "$expect_rc"
  # The two unnamed RTL assertions have reader-specific generated labels.
  # Keep the complete logs and check all source-labelled properties by ID.
  sed -nE 's/^\[([^]]+)\].*: (.*)$/\1\t\2/p' "$route/check.log" |
    awk -F '\t' -v prefix="$top." 'index($1,prefix)==1 && substr($1,length(prefix)+1)!~/\./' |
    LC_ALL=C sort >"$route/verdicts.tsv"
  diff -u expected.tsv "$route/verdicts.tsv"
  if [[ $mode == original ]]; then
    test "$(grep -cE '^\[[^]]+\.tl_master_inst\..*\].*: PROVED up to bound 20$' "$route/check.log")" = 2
  fi
done
echo PASS
