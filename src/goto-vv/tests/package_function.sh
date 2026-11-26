#!/usr/bin/env bash
set -euo pipefail
ulimit -c 0
tests=$(cd -- "$(dirname -- "$0")" && pwd)
eb=${EBMC:?}
vv=${VV_BIN:?}
work=$(realpath -m "${1:?output directory}")
mkdir -p "$work"
cd "$work"
source_file="$tests/package_function.sv"
negative_file="$tests/package_function_invalid.sv"
: > empty

run() {
    local label=$1 status=0
    shift
    printf '%q ' "$@" >"$label.command"
    printf '\n' >>"$label.command"
    "$@" >"$label.log" 2>&1 || status=$?
    printf '%s\n' "$status" >"$label.rc"
}
expect_rc() {
    test "$(cat "$1.rc")" = "$2"
}

run compile "$vv" compile package.vv --single-unit --top package_function "$source_file"
expect_rc compile 0
run constrain "$vv" constrain package.vv empty run.vv
expect_rc constrain 0
run readback "$eb" --show-symbol-table run.vv
expect_rc readback 0
for pkg in dispatch_pkg secondary_pkg; do
    grep -Eq "${pkg}[.:]+get_opgroup" readback.log
done

for engine in minisat yices; do
    flags=(--undriven seq --bound 16 --trace)
    if [[ $engine == yices ]]; then flags+=(--yices); fi
    run "$engine.reference" "$eb" "${flags[@]}" --top package_function -D NATIVE_REFERENCE "$source_file"
    run "$engine.typed" "$eb" "${flags[@]}" run.vv
    for route in reference typed; do
        label="$engine.$route"
        expect_rc "$label" 10
        for property in index generate isolation width four_state; do
            grep -E "^\[package_function\.p_$property\].*: PROVED up to bound 16$" "$label.log"
        done
        grep -E '^\[package_function\.p_wrong\].*: REFUTED$' "$label.log"
        grep -E '^package_function\.p_wrong: (finite bad frame 4 has an infinite extension by totality|witness loop .*\(equal endpoints\), bad frame 4)$' "$label.log"
    done
done

for probe in WRITE READ RECURSION STATIC_LOCAL STATIC_FUNCTION OUTPUT REF DIAGNOSTIC STATE_LABEL; do
    case "$probe" in
        WRITE) diagnostic='requires a direct blocking write to a runtime function local';;
        READ) diagnostic='runtime function reads a nonlocal variable';;
        RECURSION) diagnostic='recursive runtime function';;
        STATIC_LOCAL) diagnostic='unsupported runtime function local declaration';;
        STATIC_FUNCTION) diagnostic='requires an automatic package function with an integral return';;
        OUTPUT|REF) diagnostic='runtime function requires input formals';;
        DIAGNOSTIC) diagnostic='unsupported call statement in a pure runtime package function';;
        STATE_LABEL) diagnostic='runtime package case cannot preserve an X/Z label with a variable four-state selector';;
    esac
    run "bad-$probe" "$vv" compile "bad-$probe.vv" --single-unit --top package_function_invalid -D "BAD_$probe" "$negative_file"
    expect_rc "bad-$probe" 2
    grep -E "package_function_invalid\.sv:[0-9]+:[0-9]+: $diagnostic$" "bad-$probe.log"
    test ! -e "bad-$probe.vv"
done

printf '%s\n' 'Runtime package functions PASS'
