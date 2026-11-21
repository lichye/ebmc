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

An empty contract is a zero-length file. Contract lines can specify
`reset top.rst` or `assume label posedge top.clk top.input_bus == '0`.
Run the cross-process interface fixtures with
`bash src/goto-vv/tests/run.sh /path/to/test-output`.

The decoder comparison takes an external PLENA checkout, its decode_spec.sv
wrapper and a fresh output path. It builds the adapted closure, retains the
strict diagnostic and compares both routes at bound 20:

    bash src/goto-vv/tests/decoder.sh /path/to/plena /path/to/decode_spec.sv /path/to/results
