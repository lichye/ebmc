# Check property results, all source/drive assumptions, and the boundary trace for one check.
function fail(message) {
  print FILENAME ": " message > "/dev/stderr"
  failed = 1
  exit 1
}
function check_value(state, name, expected) {
  if (!((state, name) in sample) || sample[state, name] != expected)
    fail("unexpected " name " in state " state ", expected " expected)
}
BEGIN {
  expected["sfu_in_finite"] = expected["sfu_in_normal"] = expected["sfu_in_nonzero_for_reci"] = "ASSUMED"
  if (mode == "exp")
    expected["m2_drive_op"] = expected["m2_drive_data"] = expected["m2_drive_addr"] = "ASSUMED"
  expected[prop] = verdict
  state = last = -1
}
/warning: ignoring concatenation/ { fail($0) }
/^\[fp_sfu_spec\.[[:alnum:]_]+\]/ {
  name = $1
  sub(/^\[fp_sfu_spec\./, "", name)
  sub(/\]$/, "", name)
  result = $0
  sub(/^.*: /, "", result)
  if (!(name in expected) || result != expected[name] || seen[name]++)
    fail("unexpected or repeated result: " $0)
}
/^Transition system state [0-9]+$/ { state = last = $4 + 0 }
/^  fp_sfu_spec\.[[:alnum:]_]+ = / && state >= 0 {
  name = $1
  sub(/^fp_sfu_spec\./, "", name)
  if ($NF ~ /^\([01]+\)$/) {
    bits = substr($NF, 2, length($NF) - 2)
    value = 0
    for (i = 1; i <= length(bits); ++i)
      value = value * 2 + substr(bits, i, 1)
  } else if ($NF ~ /^[01]$/) {
    value = $NF + 0
  } else {
    next
  }
  sample[state, name] = value
}
END {
  if (failed) exit 1
  for (name in expected)
    if (!(name in seen)) fail("missing result: " name)
  if (verdict == "PROVED up to bound 20") exit 0
  if (mode == "antecedents") {
    if (prop == "m2_reset_one") pattern = "1"
    else if (prop == "m2_reset_two") pattern = "11"
    else pattern = "010"
    for (start = 0; start <= last; ++start) {
      for (i = 0; i < length(pattern); ++i) {
        if (!((start + i, "rst") in sample) || sample[start + i, "rst"] != substr(pattern, i + 1, 1))
          break
      }
      if (i == length(pattern)) {
        printf "rst=%s, states %d..%d\n", pattern, start, start + i - 1
        exit 0
      }
    }
    fail("reset sample sequence " pattern " absent from witness")
  }
  for (i = 0; i <= 19; ++i) {
    check_value(i, "rst", i == 0)
    check_value(i, "m2_step", i)
    check_value(i, "data_in", 1008) # 0x3f0
    check_value(i, "reg_waddr", 5)
    check_value(i, "operation", i == 1 ? 5 : 0)
    if (i > 0) check_value(i, "data_out_valid", i == 19)
  }
  check_value(19, "data_out", 1059) # 0x423
  print "request=1, first post-reset valid=19, data_out=0x423"
}
