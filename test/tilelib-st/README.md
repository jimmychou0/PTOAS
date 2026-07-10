# TileLib ST

`test/tilelib-st` contains PTODSL TileLib system tests that compile and run
operator-level TileLib kernels against a torch_npu runtime or the msprof
simulator wrapper.

## Layout

- Put A5 cases under `test/tilelib-st/a5/`.
- Put shared test helpers in `test/tilelib-st/common.py`.
- Keep one operator family per directory, for example `a5/tadd/` or
  `a5/tmatmul/`.
- Put the executable case module in that directory's `case.py`.
- Each `case.py` file must define a non-empty `CASES` list.

## Writing Cases

Use `golden_output_case(...)` for the common "inputs + output + golden compare"
shape. Prefer this auto-mode style for simple vector TileLib cases:

```python
from pathlib import Path
import sys

import numpy as np

if __package__ in {None, ""}:
    sys.path.insert(0, str(Path(__file__).resolve().parents[2]))

from common import auto_main, golden_output_case
from ptodsl import pto


ROWS = 16
COLS = 64


@pto.jit(name="my_tadd_f32_16x64", target="a5")
def _kernel(
    a_ptr: pto.ptr(pto.f32, "gm"),
    b_ptr: pto.ptr(pto.f32, "gm"),
    out_ptr: pto.ptr(pto.f32, "gm"),
):
    a_view = pto.make_tensor_view(a_ptr, shape=[ROWS, COLS], strides=[COLS, 1])
    b_view = pto.make_tensor_view(b_ptr, shape=[ROWS, COLS], strides=[COLS, 1])
    out_view = pto.make_tensor_view(out_ptr, shape=[ROWS, COLS], strides=[COLS, 1])

    a_tile = pto.alloc_tile(shape=[ROWS, COLS], dtype=pto.f32)
    b_tile = pto.alloc_tile(shape=[ROWS, COLS], dtype=pto.f32)
    out_tile = pto.alloc_tile(shape=[ROWS, COLS], dtype=pto.f32)

    pto.tile.load(a_view, a_tile)
    pto.tile.load(b_view, b_tile)
    pto.tile.add(a_tile, b_tile, out_tile)
    pto.tile.store(out_tile, out_view)


def _make_inputs():
    rng = np.random.default_rng(0xA5)
    a = rng.uniform(-1.0, 1.0, size=(ROWS, COLS)).astype(np.float32)
    b = rng.uniform(-1.0, 1.0, size=(ROWS, COLS)).astype(np.float32)
    return [a, b]


def _make_expected(a, b):
    return (a + b).astype(np.float32)


CASES = [
    golden_output_case(
        "my_tadd_f32_16x64",
        _kernel,
        inputs=_make_inputs,
        expected=_make_expected,
        rtol=1e-6,
        atol=1e-6,
    ),
]


auto_main(globals())
```

Key conventions:

- Use `@pto.jit(target="a5")` for auto-mode vector tile-op cases.
- Build `TensorView` objects with `pto.make_tensor_view(...)`.
- Pass `TensorView` directly to `pto.tile.load/store`; it will infer the
  full-tile partition from tile metadata.
- Allocate tiles with `pto.alloc_tile(shape=..., dtype=...)` and omit `addr=`.
- Put host-side inputs in `inputs=...` and NumPy golden logic in `expected=...`.
- Keep case names unique across the whole discovered tree.

For multiple cases in one suite, create one `@pto.jit` kernel per static shape
or layout variant in that suite's `case.py`, then append one
`golden_output_case(...)` entry for each case:

```python
CASE_SPECS = [
    ("f32_16x64", 16, 64),
    ("f32_32x32", 32, 32),
]

_kernels = {}
for _name, _rows, _cols in CASE_SPECS:

    def _make(rows=_rows, cols=_cols, kernel_name=f"my_op_{_name}"):
        @pto.jit(name=kernel_name, target="a5")
        def _kernel(src_ptr: pto.ptr(pto.f32, "gm"), out_ptr: pto.ptr(pto.f32, "gm")):
            src_view = pto.make_tensor_view(src_ptr, shape=[rows, cols], strides=[cols, 1])
            out_view = pto.make_tensor_view(out_ptr, shape=[rows, cols], strides=[cols, 1])
            src_tile = pto.alloc_tile(shape=[rows, cols], dtype=pto.f32)
            out_tile = pto.alloc_tile(shape=[rows, cols], dtype=pto.f32)
            pto.tile.load(src_view, src_tile)
            pto.tile.abs(src_tile, out_tile)
            pto.tile.store(out_tile, out_view)

        return _kernel

    _kernels[_name] = _make()


CASES = [
    golden_output_case(
        "my_op_" + name,
        _kernels[name],
        inputs=lambda rows=rows, cols=cols: [np.ones((rows, cols), dtype=np.float32)],
        expected=lambda src: np.abs(src).astype(np.float32),
    )
    for name, rows, cols in CASE_SPECS
]
```

When a case needs a non-default tile layout, keep the same auto-mode structure
and pass layout metadata to `alloc_tile(...)`, for example:

```python
tile = pto.alloc_tile(shape=[16, 64], dtype=pto.f32, blayout="ColMajor")
view = pto.make_tensor_view(ptr, shape=[16, 64], strides=[1, 16])
```

Prefer automatic tile address allocation by omitting `addr=` from
`pto.alloc_tile(...)`. Use explicit tile addresses only when a case is
intentionally validating address-sensitive behavior or needs to mirror an
existing hand-authored ST exactly. Prefer auto-mode TileLib authoring for
simple vector tile-op cases; use explicit mode only when the case needs manual
sync or explicit low-level DMA/cube movement surfaces. For example, cube cases
that call `pto.mte_gm_l1_frac`, `pto.mte_l1_l0a`, `pto.mte_l1_l0b`, or
`pto.mte_l0c_gm` must use explicit mode because those APIs are explicit-only.

## Running

List cases:

```bash
python3 test/tilelib-st/run_tilelib_st.py test/tilelib-st/a5 --list
```

Run every A5 suite:

```bash
python3 test/tilelib-st/run_tilelib_st.py test/tilelib-st/a5
```

Run a single suite directly:

```bash
python3 test/tilelib-st/a5/tadd/case.py
```

Run the A5 directory through the simulator wrapper:

```bash
ASCEND_HOME_PATH=/path/to/cann \
PTOAS_BIN=/path/to/ptoas \
PYTHON_BIN=/path/to/python-with-torch-npu \
scripts/sim_dsl.sh test/tilelib-st/run_tilelib_st.py -- test/tilelib-st/a5
```

The runtime Python must provide `torch`, `torch_npu`, `numpy`, PTODSL, and the
matching MLIR PTO Python bindings.

## Simulator Runbook

Use this flow when bringing up `test/tilelib-st` on a simulator host from a
fresh checkout. The commands assume Linux, a VPTO-enabled LLVM build, CANN with
`msprof op simulator`, and a Python environment that can import `torch` and
`torch_npu`.

Prerequisites:

- CANN is installed and provides `msprof op simulator`.
- VPTO LLVM/MLIR has already been built by following the repository root
  `README.md` build guide. Generic upstream LLVM is not enough.
- The runtime Python can import `torch`, `torch_npu`, `numpy`, `pybind11`,
  `nanobind`, and `yaml`.
- The Python ABI used to build PTOAS Python bindings matches the runtime
  Python used to launch the ST cases.

Use the LLVM/MLIR version documented in the repository root `README.md`. At the
time of writing that is LLVM 21 from the VPTO branch
`vpto-dev/llvm-project:feature-vpto-llvm21`. If unsure, check
`.github/workflows/ci_sim.yml` and use its current `LLVM_REPO` / `LLVM_REF`.
PTOAS CMake also verifies the LLVM major version and will reject an
incompatible LLVM build.

### CANN 9.0.0 AArch64 Setup

For A5 simulator validation, install both the CANN 9.0.0 toolkit and the
Ascend 950 operator package. The 950 ops package provides the
`Ascend950PR_9599` simulator assets used by TileLib ST.

```bash
mkdir -p "$HOME/downloads/cann-9.0.0"
cd "$HOME/downloads/cann-9.0.0"

wget -O Ascend-cann-toolkit_9.0.0_linux-aarch64.run \
  'https://ascend-repo.obs.cn-east-2.myhuaweicloud.com/CANN/CANN%209.0.0/Ascend-cann-toolkit_9.0.0_linux-aarch64.run'
bash ./Ascend-cann-toolkit_9.0.0_linux-aarch64.run --install

wget -O Ascend-cann-950-ops_9.0.0_linux-aarch64.run \
  'https://ascend-repo.obs.cn-east-2.myhuaweicloud.com/CANN/CANN%209.0.0/Ascend-cann-950-ops_9.0.0_linux-aarch64.run'
bash ./Ascend-cann-950-ops_9.0.0_linux-aarch64.run --install
```

Point `ASCEND_HOME_PATH` at the installed CANN root. For a user-local install,
this is commonly under `$HOME/Ascend/cann-9.0.0`; if your installer chose a
different prefix, use the directory that contains `set_env.sh`.

```bash
export ASCEND_HOME_PATH="$HOME/Ascend/cann-9.0.0"
source "$ASCEND_HOME_PATH/set_env.sh"

test -f "$ASCEND_HOME_PATH/set_env.sh"
test -d "$ASCEND_HOME_PATH/tools/simulator/Ascend950PR_9599/lib"
command -v msprof
```

Run a minimal simulator smoke before building PTOAS. Use an output directory
owned by the current user; `msprof` rejects world-writable locations such as
plain `/tmp`.

```bash
mkdir -p "$HOME/.cache/ptoas/msprof-smoke"
chmod 700 "$HOME/.cache" "$HOME/.cache/ptoas" "$HOME/.cache/ptoas/msprof-smoke" 2>/dev/null || true

msprof op simulator --soc-version=Ascend950PR_9599 \
  --output="$HOME/.cache/ptoas/msprof-smoke/cann900-a5" \
  python3 -c 'print("msprof_Ascend950PR_9599_ok")'
```

Prepare the main paths:

```bash
cd /path/to/PTOAS

export PTOAS_REPO="$PWD"
export PTOAS_BUILD="$PTOAS_REPO/build-sim"
export ASCEND_HOME_PATH=/path/to/cann
export LLVM_BUILD_DIR=/path/to/vpto-llvm/build
export MLIR_PYTHON_ROOT="$LLVM_BUILD_DIR/tools/mlir/python_packages/mlir_core"
export PYTHON_BIN=/path/to/python-with-torch-npu
export PTO_PYTHON_BIN="$PYTHON_BIN"
export PTOAS_ENV_SKIP_SMOKE_TEST=1
export TORCH_DEVICE_BACKEND_AUTOLOAD=0
```

Source CANN before importing `torch_npu` or launching the simulator:

```bash
source "$ASCEND_HOME_PATH/set_env.sh" >/dev/null 2>&1 || \
source "$ASCEND_HOME_PATH/bin/setenv.bash"

# Simulator execution does not require the system NPU driver. Set this after
# sourcing CANN so PTODSL does not probe /usr/local/Ascend/driver/kernel/inc.
export ASCEND_DRIVER_PATH="$ASCEND_HOME_PATH/driver"
```

The directory selected by `ASCEND_DRIVER_PATH` does not need to exist for
simulator execution. PTODSL treats the driver headers as optional and skips
the include path when it is absent. Do not change permissions under
`/usr/local/Ascend/driver` just to run the simulator.

Check the simulator, LLVM, and Python prerequisites:

```bash
command -v msprof
test -d "$LLVM_BUILD_DIR/lib/cmake/llvm"
test -d "$LLVM_BUILD_DIR/lib/cmake/mlir"
test -d "$MLIR_PYTHON_ROOT"
test -d "$ASCEND_HOME_PATH/tools/simulator/Ascend950PR_9599/lib"

"$PYTHON_BIN" - <<'PY'
import nanobind
import numpy
import pybind11
import torch
import torch_npu  # noqa: F401
import yaml

print("nanobind", getattr(nanobind, "__version__", "unknown"))
print("numpy", numpy.__version__)
print("pybind11", pybind11.__version__)
print("torch", torch.__version__)
print("torch_npu", getattr(torch_npu, "__version__", "unknown"))
PY
```

If `MLIR_PYTHON_ROOT` belongs to another user or is otherwise read-only, copy
the MLIR Python package to a writable overlay and use the overlay for
`MLIR_PYTHON_PACKAGE_DIR`. PTOAS builds `_pto.so` into this package as part of
`PTOPythonModules`, so CMake needs write permission there.

```bash
if [[ ! -w "$MLIR_PYTHON_ROOT/mlir/_mlir_libs" ]]; then
  export MLIR_PYTHON_ROOT_ORIG="$MLIR_PYTHON_ROOT"
  export MLIR_PYTHON_ROOT="$PTOAS_BUILD/mlir_core"
  rm -rf "$MLIR_PYTHON_ROOT"
  mkdir -p "$MLIR_PYTHON_ROOT"
  cp -a "$MLIR_PYTHON_ROOT_ORIG"/. "$MLIR_PYTHON_ROOT"/
fi
```

If the lightweight build dependencies are missing, install them into the same
runtime Python. Do not install `torch` or `torch_npu` blindly; use a known
compatible prebuilt environment for those packages.

```bash
"$PYTHON_BIN" -m pip install 'pybind11<3' nanobind numpy ml-dtypes PyYAML
```

Configure PTOAS if `build-sim` does not exist yet. The Python used to build
PTOAS Python bindings must have the same Python ABI as the runtime Python that
will run the ST cases:

```bash
PYBIND11_CMAKE_DIR="$("$PYTHON_BIN" -m pybind11 --cmakedir)"
NANOBIND_CMAKE_DIR="$("$PYTHON_BIN" -m nanobind --cmake_dir)"

cmake -S "$PTOAS_REPO" -B "$PTOAS_BUILD" -G Ninja \
  -DLLVM_DIR="$LLVM_BUILD_DIR/lib/cmake/llvm" \
  -DMLIR_DIR="$LLVM_BUILD_DIR/lib/cmake/mlir" \
  -DPython3_EXECUTABLE="$PYTHON_BIN" \
  -DPython3_FIND_STRATEGY=LOCATION \
  -Dpybind11_DIR="$PYBIND11_CMAKE_DIR" \
  -Dnanobind_DIR="$NANOBIND_CMAKE_DIR" \
  -DMLIR_ENABLE_BINDINGS_PYTHON=ON \
  -DMLIR_PYTHON_PACKAGE_DIR="$MLIR_PYTHON_ROOT" \
  -DPTO_ENABLE_PYTHON_BINDING=ON \
  -DPTOAS_ENABLE_WERROR=OFF \
  -DBUILD_TESTING=ON
```

Build the compiler and PTO Python bindings:

```bash
ninja -C "$PTOAS_BUILD" ptoas PTOPythonModules
```

Export the runtime lookup paths. `PTOAS_BIN` is useful, but PTODSL native build
also resolves `ptoas` through `PATH`, so keep the freshly built binary first:

```bash
export PTOAS_BIN="$PTOAS_BUILD/tools/ptoas/ptoas"
export PATH="$PTOAS_BUILD/tools/ptoas:$PATH"
export PYTHONPATH="$PTOAS_BUILD/python:$PTOAS_REPO/ptodsl:$MLIR_PYTHON_ROOT:${PYTHONPATH:-}"
export LD_LIBRARY_PATH="$LLVM_BUILD_DIR/lib:$PTOAS_BUILD/lib:${LD_LIBRARY_PATH:-}"
```

Check that PTODSL and the PTO MLIR Python dialect import from the intended
build:

```bash
"$PYTHON_BIN" - <<'PY'
from ptodsl import pto  # noqa: F401
from mlir.dialects import pto as _pto  # noqa: F401

print("ptodsl imports ok")
PY
```

List the cases before running the simulator:

```bash
"$PYTHON_BIN" test/tilelib-st/run_tilelib_st.py test/tilelib-st/a5 --list
```

Run all A5 TileLib ST cases through `msprof op simulator`:

```bash
scripts/sim_dsl.sh --soc-version Ascend950PR_9599 \
  test/tilelib-st/run_tilelib_st.py -- test/tilelib-st/a5 \
  2>&1 | tee /tmp/tilelib-st-a5-sim.log
```

Expected A5 TileLib ST coverage currently includes:

```text
tadd_f32_16x64
tadd_f32_32x32
tcolexpand_f32_1x8x128
tcolsum_f32_16x128
tload_store_nd_f32_16x64
tload_store_dn_f32_16x64
tmatmul_f32_16x32x64
```

Run one suite when debugging:

```bash
scripts/sim_dsl.sh --soc-version Ascend950PR_9599 \
  test/tilelib-st/a5/tadd/case.py \
  2>&1 | tee /tmp/tilelib-st-tadd-sim.log
```

The expected successful ending is:

```text
PASS ...
All cases passed.
```

Example path layout:

```bash
export ASCEND_HOME_PATH=/opt/ascend/cann
source "$ASCEND_HOME_PATH/set_env.sh"
export ASCEND_DRIVER_PATH="$ASCEND_HOME_PATH/driver"
export LLVM_BUILD_DIR=/opt/vpto-llvm/build
export MLIR_PYTHON_ROOT="$LLVM_BUILD_DIR/tools/mlir/python_packages/mlir_core"
export PYTHON_BIN=/opt/ptodsl-runtime/bin/python
```

Common failures:

- `libhccl.so: cannot open shared object file`: source the CANN environment
  before importing `torch_npu`.
- `simulator library directory not found`: check
  `$ASCEND_HOME_PATH/tools/simulator/<soc>/lib` and pass the matching
  `--soc-version`.
- `unsupported soc version: Ascend950PR_9599`: verify that the CANN 9.0.0
  toolkit and `Ascend-cann-950-ops` package were both installed, then rerun the
  `msprof` smoke above.
- `Permission denied` under `/usr/local/Ascend/driver/kernel/inc`: the simulator
  does not require the system NPU driver. After sourcing CANN, set
  `ASCEND_DRIVER_PATH="$ASCEND_HOME_PATH/driver"`; do not use `sudo` or relax
  permissions on the system driver directory.
- `_pto...so` copy failure under another user's LLVM tree: copy
  `MLIR_PYTHON_ROOT` to a writable overlay and point
  `MLIR_PYTHON_PACKAGE_DIR` at that overlay before configuring PTOAS.
- `No TileLib ST cases discovered`: pass the case root through
  `scripts/sim_dsl.sh ... run_tilelib_st.py -- test/tilelib-st/a5`, keeping
  the `--` separator.
- Stale compiler behavior: rebuild `ptoas PTOPythonModules`, put
  `$PTOAS_BUILD/tools/ptoas` first in `PATH`, and verify `$PTOAS_BIN --version`.
