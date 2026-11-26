Compiling
=========

1. You need a C/C++ compiler, Flex and Bison, and GNU make.
   The GNU Make needs to be version 3.81 or higher.
   On Debian-like distributions, including Ubuntu, do as root:
   ```
   apt-get install g++ gcc flex bison make git curl patch
   ```
   On Red Hat/Fedora or derivates, do as root:
   ```
   dnf install gcc gcc-c++ flex bison curl patch
   ```
   Note that you need g++ version 7.0 or newer.

   On Amazon Linux and similar distributions, do as root:
   ```
   yum install gcc72-c++ flex bison curl patch tar
   ```

2. Initialize and update the CBMC submodule:
   ```
   git submodule init; git submodule update
   ```

3. Download minisat:
   ```
   make -C lib/cbmc/src minisat2-download
   ```

4. Build EBMC:
   ```
   make -C src
   ```
   This also builds the CBMC submodule. The binary will be in src/ebmc/ebmc.

### Optional goto-vv compiler

Build slang v11.0 and the goto-vv executable with a C++20 compiler and CMake:

    make -C src goto-vv SLANG_SOURCE=/path/to/slang-v11.0 CMAKE_ARGS=-DCMAKE_BUILD_TYPE=Release

With an installed slang 11.0.0 CMake package, omit SLANG_SOURCE. Build EBMC
with the normal target. For an empty contract and a single-clock design:

    src/goto-vv/build/goto-vv compile design.vv --single-unit --top top design.sv
    src/goto-vv/build/goto-vv constrain design.vv empty-contract.txt run.vv
    src/ebmc/ebmc --undriven seq --bound 20 run.vv

An omitted or explicitly empty child input uses its undriven formal signal.
The contract can record `undriven const` to hold floating bits constant across
steps. An empty contract selects `seq`, which allows them to change each step.
If EBMC receives an explicit `--undriven` option, it must match the constrained
object. Use `ebmc --undriven-report run.vv` to list floating reads.

By default each input file is a separate compilation unit. Use `-mfcu`
(`--single-unit`) to put the primary sources in one compilation unit, in file
order. Use `-sfcu` (`--single-unit=false`) to explicitly select separate units.
The two spellings of each setting use the same slang option. A second setting
on the command line is an error, including when the first setting came from a
file list. Slang ignores a second setting it encounters inside a file list. Put
the setting on the command line before the file list to override a value in the
list.

Use `-f FILE` for paths relative to the working directory or `-F FILE` for
paths relative to that list. These options can nest and accept compiler options
including `+define+`, `+incdir+`, `-y`, and `-v`. This example reads a nested
list and compiles its two source files in the listed order as one unit.

    src/goto-vv/build/goto-vv compile design.vv --top top -F rtl/design.f

Put the following line in `rtl/design.f`.

    -F units/decoder.f

Put the unit setting and sources in `rtl/units/decoder.f`.

    -mfcu
    +define+SYNTHESIS
    +incdir+../include
    ../decoder.sv
    ../top.sv

Slang's `-C FILE` lists an explicitly separate unit, and library input is
separate from primary source units.

The compile stage is strict by default. Slang's
`--allow-use-before-declare` permits forward references to declared names.
For the two additional PLENA compatibility flags, start with a separate
checkout of slang 11.0.0 at `7ddf4059f79eff508dd486eb42fd650cdf320d52`.
From the EBMC repository root, apply the local patch and build goto-vv using
that checkout.

    slang_plena=/path/to/slang-v11-plena
    git -C "$slang_plena" apply --check --index "$PWD/src/goto-vv/slang-v11-plena.patch"
    git -C "$slang_plena" apply "$PWD/src/goto-vv/slang-v11-plena.patch"
    make -C src goto-vv SLANG_SOURCE="$slang_plena" CMAKE_ARGS=-DCMAKE_BUILD_TYPE=Release

`--allow-enum-literal-width-mismatch` permits explicit sized enum literals
whose values fit the enum base type. `--allow-initial-always-ff-drivers`
permits a variable to be written by an `initial` block and an `always_ff`
block. Each option is independent. The stock slang 11.0.0 build command
above remains valid for strict builds.

Elaboration may use constant `string` parameters for generate branches and
value choices. goto-vv preserves each elaborated parameter as a constant
symbol, including empty values, instance overrides and forwarded values.
Use `ebmc --show-symbol-table run.vv` to inspect these parameters and their
declaration locations. Reachable runtime string operations and file tasks
remain unsupported.

An empty contract is a zero-length file. Contract lines can specify
`reset top.rst` or `assume label posedge top.clk top.input_bus == '0`.
Run the cross-process interface fixtures with
`bash src/goto-vv/tests/run.sh /path/to/test-output`.

Runtime calls can use package-level automatic functions with input formals and
fixed-width integer or enum results. A plain `unique case` with a default uses
ordinary case matching. For a selector and labels known to Slang, X and Z
compare exactly even when another argument is evaluated at run time.
An X/Z label with a variable four-state selector requires a representation
the runtime package path does not provide and is diagnosed. Package state,
nonlocal writes, recursion and effectful statements are also diagnosed.

### Clocked block-local loop counters

A sequential named block in a single positive-edge procedure can contain an
uninitialized `integer` declaration followed by a counted `for` loop.
The loop must initialize that counter with a blocking constant assignment
and have a statically determined unit-increment bound. goto-vv substitutes
the typed counter values within that loop. The loop body retains its original
assignments and their nonblocking scheduling. Reading the eliminated counter
from outside the loop is diagnosed.

### Procedural case qualifiers

At the untimed entry of an `initial` sequence, goto-vv evaluates an immediate
parameter assertion with an empty success action and a single `$fatal`
failure action. A true elaborated condition produces no assertion in the
model. A false condition reports the instance and source location and aborts
compilation. The check requires a statically reached statement in an
instantiated scope. Existing runtime assertions and translation directives
keep their previous behavior.

For ordinary matching, `unique case` and `priority case` select the first
matching item in source order, or the default if present. To create
procedural assertions at the case statement, pass `--assert-case-qualifiers`
to `goto-vv compile`. The assertions use the same grouped matches as branch
selection. `unique` checks that at most one item matches. Both qualifiers
check that some item matches if the default is omitted. An empty default
still counts as a default. The assertions are guarded by the path that
reaches the case and add no assumptions.

Checked case statements in runtime functions, loops or asynchronously reset
gated processes are diagnosed when they would require repeated or unsupported
sampling. Ordinary matching and existing package function lowering remain
available without the assertion option.

### Temporary Coral clock gate contract

`goto-vv compile --clock-gate-contract FILE` opts into the temporary Coral W4
root-clock model. The file gives the contract version and elaborated bindings,
in this order.

    coral-w4-env1-env2.v1
    ENV1 top.test_enable_i
    ENV2 top.rst_ni top.clk_i
    RstSync top.core.rst_sync
    reset-mux top.core.internal_reset

After those bindings, add one line per independent ClockGate to project. Use
the elaborated instance path. For example:

    ClockGate top.core.cg

An omitted list keeps the original RstSync-only contract. Listed gates must
use the checked low-phase latch implementation with `te` bound to ENV1. Their
inputs must resolve to the root clock, the named RstSync output, or an
explicitly listed parent. All derived clocks reach low during the root low
phase. The next root rising edge passes a branch when its own preponed enable
and the enables of all its ancestors are true. A branch can be used for
positive-edge processes. Waveform reads and unproved enable drivers are errors.

A supported independent active-high asynchronous reset may come from the
Coral functional reset mux whose active branch is the OR of two fixed bits of
parent-clocked registers. Their global asynchronous reset must leave that OR
asserted, including when the global reset follows a parent edge. The model
honors their internal reset after the parent NBA even when the consumer's
gate is closed. ENV2 applies to the original bound external reset only. The
object and readback record the additional gate bindings and independent reset
sampling.

A falling asynchronous edge with a matching outer logical negation is also
supported. The scalar reset signal may resolve through aliases and inversions
to a proved reset source with the same assertion polarity. Synchronous clear
and load in the remaining branch still require the gated clock edge.

A gated asynchronous process can drive a statically resolved packed interval.
The reset and each nonblocking update must target the same interval. Other
drivers can own disjoint intervals, including continuous inputs to adjacent
pipeline stages. Overlapping, dynamic or unproved writes are diagnosed.

ENV1 fixes the bound test input to zero. ENV2 treats external reset as a
boundary input driven after the root rising-edge preponed sample. The model
requires at least two low samples for each assertion. Internal asynchronous
reset takes priority before the next sample. The gate uses the preceding
low-phase enable. The version checks the active Coral RstSync and ClockGate
definitions, their port bindings and reset mux. The supported use of the gated
output is a process clock.

The object records these bindings and assumptions. Inspect them with
`ebmc --show-symbol-table RUN.vv`. Select a compiled property by ID for
verification. To require a reset at startup, use a binding file containing
`clock-gate-start-reset` with `goto-vv constrain`. This activates initial low
and the existing two-low-sample rule then applies. The ordinary `reset`
directive requires a single cycle and conflicts with this model.


The decoder comparison takes an external PLENA checkout, its decode_spec.sv
wrapper and a fresh output path. It builds the adapted closure, retains the
strict diagnostic and compares both routes at bound 20:

    bash src/goto-vv/tests/decoder.sh /path/to/plena /path/to/decode_spec.sv /path/to/results
