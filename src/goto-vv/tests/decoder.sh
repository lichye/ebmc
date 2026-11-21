#!/usr/bin/env bash
# Run with an external PLENA checkout, the 60-property wrapper, and a fresh output directory.
set -euo pipefail
ulimit -c 0
ulimit -v 8388608
front=$(cd "$(dirname "$0")/.." && pwd)
vv=$(realpath "${VV_BIN:-$front/build/goto-vv}")
eb=$(realpath "${EBMC:-$front/../ebmc/ebmc}")
plena=$(realpath "${1:?PLENA checkout required}")
wrapper=$(realpath "${2:?decode_spec.sv required}")
dst=$(realpath -m "${3:?fresh output directory required}")
mkdir "$dst"
cd "$dst"
mkdir inputs out log
revision=bab29dba89a05fb50dbafe5724cda53d19071a47
rtl=(src/frontend/rtl/decoder.sv src/frontend/rtl/loop_controller.sv)
for mode in raw adapted contract cover typed-cover; do
  dir="$PWD/inputs/$mode"
  mkdir "$dir"
  git -C "$plena" archive "$revision" src/definitions "${rtl[@]}" | tar xf - -C "$dir"
  cp "$wrapper" "$dir/decode_spec.sv"
  if [[ $mode != raw ]]; then
    sed -i "100,106s/4'h/3'h/" "$dir/src/definitions/operation.svh"
  fi
  if [[ $mode = contract || $mode = cover ]]; then
    sed -i "/^endmodule/i\  m1_env_addr_zero: assume property (@(posedge clk) instruction_addr_from_imem == '0);" "$dir/decode_spec.sv"
  fi
  if [[ $mode = cover || $mode = typed-cover ]]; then
    sed -i '/^endmodule/i\  m1_live: cover property (@(posedge clk) !rst && instruction_ready);' "$dir/decode_spec.sv"
  fi
  printf '\x60include "%s"\n' "$dir/${rtl[0]}" "$dir/${rtl[1]}" "$dir/decode_spec.sv" >"$dir/unit.sv"
done
: >inputs/original.txt
printf '%s\n' 'reset decode_spec.rst' "assume m1_env_addr_zero posedge decode_spec.clk decode_spec.instruction_addr_from_imem == '0" >inputs/external.txt
flags=(--single-unit --top decode_spec -DSYNTHESIS)
if "$vv" compile out/strict.vv "${flags[@]}" -I inputs/raw/src/definitions inputs/raw/unit.sv >log/strict.log 2>&1; then
  echo 'Original RTL unexpectedly passed strict elaboration' >&2; exit 1
fi
grep -q 'load_instr_valid.*used before its declaration' log/strict.log
compile() {
  /usr/bin/time -f '%e %M %x' -o "log/$2.time" "$vv" compile "out/$2.vv" "${flags[@]}" --allow-use-before-declare -I "inputs/$1/src/definitions" "inputs/$1/unit.sv" >"log/$2.log" 2>&1
}
compile adapted decode
compile typed-cover cover-compiled
"$vv" constrain out/decode.vv inputs/original.txt out/decode-orig.vv
"$vv" constrain out/decode.vv inputs/external.txt out/decode-env.vv
"$vv" constrain out/cover-compiled.vv inputs/external.txt out/cover.vv
run() {
  local label=$1 route=$2 mode=$3; shift 3
  local -a args=(--undriven seq --bound 20)
  if [[ $route = direct ]]; then
    args+=(--top decode_spec --systemverilog -D SYNTHESIS -I "inputs/$mode/src/definitions")
    if [[ $mode = contract || $mode = cover ]]; then args+=(--reset decode_spec.rst); fi
    args+=("inputs/$mode/unit.sv")
  else
    args+=("out/$mode.vv")
  fi
  printf '%q ' "$eb" "${args[@]}" "$@" >"log/$label.cmd"
  local rc=0
  /usr/bin/time -f '%e %M %x' -o "log/$label.time" timeout 600 "$eb" "${args[@]}" "$@" >"log/$label.log" 2>&1 || rc=$?
  echo "$rc" >"log/$label.rc"
  if [[ $rc != 0 && $rc != 10 ]]; then echo "$label failed ($rc)" >&2; return 1; fi
  sed -nE 's/^\[decode_spec\.([^]]+)\].*: (REFUTED|ASSUMED|PROVED( up to bound 20)?)$/\1\t\2/p' "log/$label.log" | LC_ALL=C sort >"log/$label.verdicts"
}
run direct-raw direct raw
run direct-original direct adapted
run typed-original typed decode-orig
run direct-env direct contract
run typed-env typed decode-env
run direct-cover direct cover --property decode_spec.m1_live --trace
run typed-cover typed cover --property decode_spec.m1_live --trace
for group in original env cover; do
  cmp "log/direct-$group.verdicts" "log/typed-$group.verdicts"
  cmp "log/direct-$group.rc" "log/typed-$group.rc"
done
cmp log/direct-raw.verdicts log/direct-original.verdicts
cmp log/direct-raw.rc log/direct-original.rc
grep -q $'^m1_live\tPROVED$' log/typed-cover.verdicts
grep -q $'^m1_env_addr_zero\tASSUMED$' log/typed-env.verdicts
sed -nE 's/^[[:space:]]*([[:alnum:]_]+):[[:space:]]*(assert|assume) property.*/\1\t\2/p' "$wrapper" | LC_ALL=C sort >log/source.tsv
[[ $(grep -c $'\tassert$' log/source.tsv) = 57 && $(grep -c $'\tassume$' log/source.tsv) = 3 ]]
cmp <(cut -f1 log/source.tsv) <(cut -f1 log/typed-original.verdicts)
cmp <(cut -f1 log/source.tsv) <(grep -v $'^m1_env_addr_zero\t' log/typed-env.verdicts | cut -f1)
for group in original env; do
  awk -F '\t' 'NR==FNR {kind[$1]=$2;next} $1 in kind {if((kind[$1]=="assume")!=($2=="ASSUMED"))exit 1}' log/source.tsv "log/typed-$group.verdicts"
done
for id in isa_map_m_mm isa_map_m_mv isa_map_s_addi_int; do
  run "trace-$id-direct" direct adapted --property "decode_spec.$id" --trace
  run "trace-$id-typed" typed decode-orig --property "decode_spec.$id" --trace
done
echo PASS
