#!/usr/bin/env python3
"""Run Spec 6's FP-SFU boundary checks on the common adapted RTL and ordered file list."""

import argparse
import os
from pathlib import Path
import re
import resource
import shlex
import subprocess


TESTS = Path(__file__).resolve().parent
TOP = "fp_sfu_spec"
BOUND = "PROVED up to bound 20"
SOURCE_ASSUMPTIONS = {"sfu_in_finite", "sfu_in_normal", "sfu_in_nonzero_for_reci"}
DRIVE = {"m2_drive_op", "m2_drive_data", "m2_drive_addr"}
CHECKS = {
    "antecedents": {"m2_reset_one": "PROVED", "m2_reset_two": "PROVED",
                    "m2_isolated_reset": "PROVED"},
    "exp": {"m2_live": "PROVED", "m2_latency": BOUND, "m2_value": BOUND,
            "m2_opposite": "REFUTED"},
}


def run(work, label, command, expected_rc=0):
    args = list(map(str, command))
    log = work / (label + ".log")
    log.with_suffix(".cmd").write_text(shlex.join(args) + "\n")
    with log.open("w") as stream:
        rc = subprocess.run(
            ["/usr/bin/time", "-f", "%e %M %x", "-o", log.with_suffix(".time"),
             "timeout", "600", *args], cwd=work, stdout=stream,
            stderr=subprocess.STDOUT, check=False).returncode
    log.with_suffix(".rc").write_text(str(rc) + "\n")
    if rc != expected_rc:
        raise ValueError(f"{label}: exit {rc}, expected {expected_rc}; see {log}")
    return log.read_text()


def witness(log, prop):
    # EBMC prints scalar bits directly and vector/enum values with their bits in parentheses.
    states = {}
    for line in log.splitlines():
        if match := re.fullmatch(r"Transition system state (\d+)", line):
            index = int(match[1])
            states[index] = {}
        elif match := re.fullmatch(r"  fp_sfu_spec\.(\w+) = (.+)", line):
            value = match[2]
            bits = re.search(r"\(([01]+)\)$", value)
            if bits or value in ("0", "1"):
                states[index][match[1]] = int(bits[1] if bits else value, 2)

    if prop.startswith("m2_reset") or prop == "m2_isolated_reset":
        pattern = {"m2_reset_one": [1], "m2_reset_two": [1, 1],
                   "m2_isolated_reset": [0, 1, 0]}[prop]
        for start in sorted(states):
            if all(states.get(start + i, {}).get("rst") == bit for i, bit in enumerate(pattern)):
                return f"rst={''.join(map(str, pattern))}, states {start}..{start + len(pattern) - 1}"
        raise ValueError(f"{prop}: reset samples absent from witness")

    # Check the issued request and each sampled top-level input through the response.
    for index in range(20):
        sample = states[index]
        expected = {"rst": int(index == 0), "m2_step": index, "data_in": 0x3F0,
                    "reg_waddr": 5, "operation": 5 if index == 1 else 0}
        if index > 0:
            expected["data_out_valid"] = int(index == 19)
        if index == 19:
            expected["data_out"] = 0x423
        for name, value in expected.items():
            if sample.get(name) != value:
                raise ValueError(f"{prop}: state {index} {name}={sample.get(name)}, expected {value}")
    return "request=1, first post-reset valid=19, data_out=0x423"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("root", type=Path, help="adapted input tree containing rtl/ and spec/")
    parser.add_argument("files", type=Path, help="one relative path per line, original wrapper last")
    parser.add_argument("out", type=Path, help="fresh output directory")
    parser.add_argument("--native-only", action="store_true", help="measure only the native EBMC baseline")
    args = parser.parse_args()
    root = args.root.resolve()
    ordered = args.files.read_text().splitlines()
    if len(ordered) != 23 or ordered[-1] != "spec/fp_sfu_spec.sv":
        parser.error("expected the Spec 6 ordered file list (22 RTL sources, then spec/fp_sfu_spec.sv)")
    sources = [root / f for f in ordered[:-1]]
    original = (root / ordered[-1]).read_text()
    inc = root / "rtl/src/definitions"
    eb = Path(os.getenv("EBMC", TESTS / "../../ebmc/ebmc")).resolve()
    vv = Path(os.getenv("VV_BIN", TESTS / "../build/goto-vv")).resolve()
    work = args.out.resolve()
    work.mkdir(parents=True)
    resource.setrlimit(resource.RLIMIT_CORE, (0, 0))
    summary = []
    for mode, properties in CHECKS.items():
        extra = (TESTS / f"fp_sfu_{mode}.svh").read_text()
        wrapper = work / f"{mode}.sv"
        edited, count = re.subn(r"^endmodule\b", lambda m: extra + "\n" + m[0], original, flags=re.M)
        if count != 1:
            raise ValueError("expected one top module in the FP-SFU wrapper")
        wrapper.write_text(edited)
        unit = work / f"{mode}-unit.sv"
        unit.write_text("".join(f'`include "{path}"\n' for path in [*sources, wrapper]))
        reset = ["--reset", TOP + ".rst"] if mode == "exp" else []
        direct = ["--systemverilog", "--top", TOP, "-D", "SYNTHESIS", "-I", inc, *reset, unit]
        routes = {"direct": direct}
        if not args.native_only:
            obj = work / f"{mode}.vv"
            bound_obj = work / f"{mode}-constrained.vv"
            contract = work / f"{mode}.txt"
            contract.write_text("reset fp_sfu_spec.rst\n" if mode == "exp" else "")
            run(work, mode + "-compile", [vv, "compile", obj, "--single-unit", "--top", TOP,
                                          "-DSYNTHESIS", "-I", inc, unit])
            run(work, mode + "-bind", [vv, "constrain", obj, contract, bound_obj])
            routes["typed"] = [bound_obj]
        for prop, verdict in properties.items():
            for route, inputs in routes.items():
                label = f"{prop}-{route}"
                rc = 10 if verdict == "REFUTED" else 0
                log = run(work, label, [eb, "--undriven", "seq", "--bound", "20", "--property",
                                        f"{TOP}.{prop}", "--trace", *inputs], rc)
                results = re.findall(r"^\[fp_sfu_spec\.(\w+)\].*: (.+)$", log, re.M)
                assumes = SOURCE_ASSUMPTIONS | (DRIVE if mode == "exp" else set())
                expected = {name: "ASSUMED" for name in assumes}
                expected[prop] = verdict
                if sorted(results) != sorted(expected.items()):
                    raise ValueError(f"{label}: unexpected property results; see {label}.log")
                trace = "" if verdict == BOUND else witness(log, prop)
                summary.append(f"{label}\t{verdict}\t{rc}\t{trace}")
    (work / "summary.tsv").write_text("\n".join(summary) + "\n")
    print("\n".join(summary))


if __name__ == "__main__":
    main()
