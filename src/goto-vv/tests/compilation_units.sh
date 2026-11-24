#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
front=$(cd "$(dirname "$0")/.." && pwd)
vv=$(realpath "${VV_BIN:-$front/build/goto-vv}")
eb=$(realpath "${EBMC:-$front/../ebmc/ebmc}")
work=$(realpath -m "${1:?give a fresh directory for test outputs}")
mkdir "$work"
cp -R "$front/tests/units/." "$work"
cd "$work"
: >empty

compile() {
  local label=$1 expected=$2 rc=0
  shift 2
  "$vv" compile "$label.vv" --threads 1 "$@" >"$label.log" 2>&1 || rc=$?
  if [[ $rc != "$expected" ]]; then
    printf '%s: exit %s, expected %s\n' "$label" "$rc" "$expected" >&2
    cat "$label.log" >&2
    return 1
  fi
  if [[ $rc != 0 ]]; then test ! -e "$label.vv"; fi
}
check() {
  local label=$1 property=$2
  "$vv" constrain "$label.vv" empty "$label-check.vv"
  "$eb" --undriven seq --bound 2 "$label-check.vv" >"$label.check.log" 2>&1
  grep -F "[$property]" "$label.check.log" | grep -Fq ': PROVED up to bound 2'
}

merge=(--top merge_top -F lists/merge.f)
split=(--top split_top -F lists/split.f)
compile native-merge 0 --single-unit "${merge[@]}"
compile alias-merge 0 -mfcu "${merge[@]}"
compile cwd-merge 0 -mfcu --top merge_top -f lists/cwd.f
compile default-split 0 "${split[@]}"
compile alias-split 0 -sfcu "${split[@]}"
compile explicit-groups 0 -mfcu --top split_top -C lists/leaf-unit.f -C lists/top-unit.f
check alias-merge merge_top.value_ok
grep -F '[merge_top.libraries_ok]' alias-merge.check.log | grep -Fq ': PROVED up to bound 2'
check alias-split split_top.isolation_ok
check explicit-groups split_top.isolation_ok

# Put aliases in a nested list to exercise driver parsing at that depth.
printf '%s\n' '-F nested/mfcu.f' >lists/mfcu.f
printf '%s\n' '-mfcu' '-F merge.f' >lists/nested/mfcu.f
printf '%s\n' '-F nested/sfcu.f' >lists/sfcu.f
printf '%s\n' '-sfcu' '-F ../split.f' >lists/nested/sfcu.f
compile nested-merge 0 --top merge_top -F lists/mfcu.f
compile nested-split 0 --top split_top -F lists/sfcu.f
check nested-merge merge_top.value_ok
check nested-split split_top.isolation_ok

# At top level a second assignment fails. In command files the first wins.
compile repeated 2 -mfcu -mfcu "${merge[@]}"
compile repeated-split 2 -sfcu -sfcu "${split[@]}"
compile mixed 2 -mfcu --single-unit "${merge[@]}"
compile conflicting 2 --single-unit -sfcu "${merge[@]}"
compile conflicting-aliases 2 -sfcu -mfcu "${split[@]}"
compile after-list 2 --top merge_top -F lists/mfcu.f --single-unit
for label in repeated repeated-split mixed conflicting conflicting-aliases after-list; do
  grep -Eq "more than one value provided for argument '--single-unit(=false)?'" "$label.log"
done
printf '%s\n' '-sfcu' '--single-unit' '-sfcu' '-F ../split.f' >lists/nested/sfcu.f
compile list-first 0 --top split_top -F lists/sfcu.f
printf '%s\n' '--single-unit' '-F ../split.f' >lists/nested/sfcu.f
compile cli-first 0 -sfcu --top split_top -F lists/sfcu.f
check list-first split_top.isolation_ok
check cli-first split_top.isolation_ok

compile missing-macro-and-type 2 "${merge[@]}"
grep -Eq "merge/top\.sv:3:[0-9]+: error: .*undeclared identifier 'word_t'" missing-macro-and-type.log
grep -Eq "merge/top\.sv:8:[0-9]+: error: .*unknown macro.*UNIT_VALUE" missing-macro-and-type.log
compile reverse-order 2 -mfcu --top merge_top -F lists/nested/reverse.f
grep -Eq "merge/top\.sv:8:[0-9]+: error: .*unknown macro.*UNIT_VALUE" reverse-order.log
compile duplicate-type 2 -mfcu "${split[@]}"
grep -Eq "split/top\.sv:2:[0-9]+: error: redefinition of 'local_t'" duplicate-type.log
grep -Eq "split/leaf\.sv:2:[0-9]+: note: previous definition" duplicate-type.log
compile missing-list 2 -mfcu --top merge_top -F lists/missing.f
grep -Fq "command file 'lists/missing.f'" missing-list.log
echo 'Compilation units PASS'
