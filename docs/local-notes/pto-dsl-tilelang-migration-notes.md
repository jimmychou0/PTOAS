# PTO-DSL / TileLang DSL 迁移说明

本文基于当前 `D:\work\ptoas\PTOAS-DSL` 代码状态整理，目标是说明：

- TileLang DSL 和 PTO-DSL 现在各自处在什么位置；
- 已经迁移或已经在 PTO-DSL 中原生提供的 API surface 有哪些；
- 之前的迁移大致是怎么做的；
- 后续如果要让 PTO-DSL 兼容并最终替代 TileLang DSL，应该优先补哪些能力。

## 1. 当前判断

现在更准确的描述不是“TileLang DSL 后端还没迁到 PTO IR”，而是：

```text
TileLang DSL frontend
  @pto.vkernel / @pto.ckernel
  Python AST capture
  semantic analysis
  lowering / pybind renderer
  PTO / VPTO IR

PTO-DSL frontend
  @pto.jit
  @pto.simd / @pto.cube / @pto.simt
  tracing + MLIR Python binding wrappers
  PTO IR
```

也就是说，TileLang DSL 的后端已经是 PTO/VPTO IR 方向了。现在真正要迁的是前端 API 和 authoring surface：以后希望用户写 PTO-DSL，而不是继续写 TileLang DSL 的 `@vkernel/@ckernel + strict_vecscope` 这套表层语言。

当前状态是双线并存：

- TileLang DSL 还在：仍然有 `tilelang-dsl/python/tilelang_dsl/kernel.py` 里的 `vkernel`、`ckernel`、`VKernelDescriptor`，以及 `frontend_ast.py`、`semantic.py`、`lowering.py` 这一整套前端链路。
- PTO-DSL 已经做了一部分替代表层：`ptodsl/ptodsl/_ops.py` 里已经有 `vlds`、`vsts`、`make_tensor_view`、`partition_view`、`alloc_tile`、`make_mask`、`mte_l1_l0a`、`mad` 等 native API。
- TileLang DSL 还没完全退出：PTO-DSL 目前覆盖了核心路径，但还没有完整吸收 TileLang DSL 的模板选择、descriptor specialization、advanced vecscope、部分高级 vector op 和 daemon/ExpandTileOp 相关能力。

## 2. 两套 DSL 的分工差异

| 维度 | TileLang DSL | PTO-DSL |
|---|---|---|
| 当前角色 | 旧的内核 authoring frontend | 新的 PTO native Python frontend |
| 用户入口 | `@pto.vkernel` / `@pto.ckernel` | `@pto.jit` / `@pto.simd` / `@pto.cube` / `@pto.simt` |
| 构建方式 | Python 源码 AST + semantic IR + lowering | Python tracing + MLIR pybinding wrapper |
| 内核组织 | vector/cube kernel descriptor | 一个 host-visible `@pto.jit` entry + 子 kernel |
| 数据描述 | `TensorView` / `Tile` 可作为较高层 descriptor 进入 | public entry 走 explicit GM pointer + runtime scalar，body 内 `make_tensor_view` |
| vecscope | `pto.vecscope` / `pto.strict_vecscope` | `@pto.simd` 或 `with pto.simd():` |
| cube | `@pto.ckernel` | `@pto.cube` |
| 后端目标 | PTO/VPTO IR | PTO IR |

一个关键变化是：PTO-DSL 更明确地把 host-visible entry 和 compute-unit sub-kernel 分开。`@pto.jit` 是唯一可 launch 的入口，`@pto.simd/@pto.cube/@pto.simt` 是从 `@pto.jit` 里调用的子 kernel。

## 3. 已经迁移出来的 API surface

下面是当前代码里已经能看到 PTO-DSL native surface 的部分。这里的“已迁移”不是指 TileLang 文件被删了，而是指 PTO-DSL 已经有对应的直接写法。

| 能力 | TileLang DSL 写法 | PTO-DSL 写法 | 当前状态 |
|---|---|---|---|
| kernel entry | `@pto.vkernel(...)` / `@pto.ckernel(...)` | `@pto.jit(target="a5", mode=...)` | 已有 |
| vector 子 kernel | `with pto.strict_vecscope(...)` | `@pto.simd` / `with pto.simd():` | 已有核心路径 |
| cube 子 kernel | `@pto.ckernel` | `@pto.cube` | 已有核心路径 |
| tensor descriptor | `pto.TensorView` 参数 / descriptor | `pto.make_tensor_view(ptr, shape=..., strides=...)` | 已有 |
| tile allocation | `pto.Tile` descriptor / specialization | `pto.alloc_tile(shape=..., dtype=..., memory_space=...)` | 已有 |
| GM view slicing | descriptor 内部 specialization | `pto.partition_view(...)` | 已有 |
| vector load/store | `pto.vlds(tile, lane)` / `pto.vsts(...)` | `pto.vlds(tile[row, 0:])` / `pto.vsts(vec, tile[row, 0:], mask)` | 已有 |
| mask/predicate | `pto.make_mask(pto.f32, pto.PAT.ALL)` 等 | `pto.make_mask(pto.f32, remained)`、`pto.plt_b32(...)` | 已有，但语义更 runtime 化 |
| scalar/pointer | `pto.ptr`、`pto.addptr` 等 advanced surface | `pto.ptr`、`pto.castptr`、`pto.addptr`、`scalar.load/store` | 已有 |
| MTE DMA | `pto.mte_gm_ub` / `pto.mte_ub_gm` 等 | `pto.mte_load` / `pto.mte_store` / grouped MTE wrappers | 部分已有 |
| cube MTE | `pto.mte_l1_l0a` / `pto.mte_l1_l0b` / `pto.mte_l0c_ub` | 同名 PTO-DSL wrapper | 已有 |
| matrix compute | `pto.mad` / `pto.mad_acc` / `pto.mad_bias` | 同名 PTO-DSL wrapper | 已有 |

代码依据：

- PTO-DSL operation wrappers：`ptodsl/ptodsl/_ops.py`
- PTO-DSL 编译探针：`ptodsl/tests/test_jit_compile.py`
- TileLang DSL surface matrix：`tilelang-dsl/python/tilelang_dsl/support_matrix.py`
- TileLang DSL 旧入口：`tilelang-dsl/python/tilelang_dsl/kernel.py`
- TileLang DSL lowering：`tilelang-dsl/python/tilelang_dsl/lowering.py`

## 4. 迁移方式一：同名底层 op 直接保留

最容易迁的是底层 PTO op，本来就接近 PTO IR 指令。比如：

- `pto.vlds`
- `pto.vsts`
- `pto.vadd`
- `pto.vexp`
- `pto.vcgmax`
- `pto.mte_l1_l0a`
- `pto.mte_l1_l0b`
- `pto.mte_l0c_ub`
- `pto.mad`

这类 API 在 PTO-DSL 里基本不需要改名，主要变化是调用参数从 TileLang DSL 的 descriptor/lane 风格，变成 PTO-DSL 的 tile slice / pointer / mask 风格。

TileLang DSL 旧风格大致是：

```python
@pto.vkernel(op="template_slot_add_unique", dtypes=[(pto.f32, pto.f32, pto.f32)])
def kernel(dst: pto.Tile, src0: pto.Tile, src1: pto.Tile):
    with pto.strict_vecscope(dst, src0, src1, 0, 64, 64) as (
        out_tile,
        lhs_tile,
        rhs_tile,
        lb,
        ub,
        step,
    ):
        for lane in range(lb, ub, step):
            mask = pto.make_mask(pto.f32, pto.PAT.ALL)
            lhs = pto.vlds(lhs_tile, lane)
            rhs = pto.vlds(rhs_tile, lane)
            out = pto.vadd(lhs, rhs, mask)
            pto.vsts(out, out_tile, lane, mask)
```

PTO-DSL 新风格更像：

```python
@pto.simd
def vector_add(lhs_tile: pto.Tile, rhs_tile: pto.Tile, out_tile: pto.Tile, row: pto.index):
    mask, _ = pto.plt_b32(pto.const(64, dtype=pto.i32))
    lhs = pto.vlds(lhs_tile[row, 0:])
    rhs = pto.vlds(rhs_tile[row, 0:])
    out = pto.vadd(lhs, rhs, mask)
    pto.vsts(out, out_tile[row, 0:], mask)


@pto.jit(target="a5")
def add_kernel(*, BLOCK: pto.constexpr = 128):
    lhs_tile = pto.alloc_tile(shape=[2, BLOCK], dtype=pto.f32)
    rhs_tile = pto.alloc_tile(shape=[2, BLOCK], dtype=pto.f32)
    out_tile = pto.alloc_tile(shape=[2, BLOCK], dtype=pto.f32)
    with pto.for_(0, 1, step=1) as row:
        vector_add(lhs_tile, rhs_tile, out_tile, row)
```

这里的本质迁移是：

- `strict_vecscope` region 变成 `@pto.simd` 子 kernel；
- `lane` 参数变成 tile slice 的 index；
- `vlds(tile, lane)` 变成 `vlds(tile[row, 0:])`；
- `vsts(vec, tile, lane, mask)` 变成 `vsts(vec, tile[row, 0:], mask)`。

## 5. 迁移方式二：descriptor specialization 下沉到 `@pto.jit`

TileLang DSL 里很多信息通过 descriptor 或模板参数隐式携带。PTO-DSL 当前 public entry 刻意收窄为：

- GM pointer：`pto.ptr(dtype, "gm")`
- runtime scalar：`pto.i32`、`pto.f32`、`pto.i1` 等
- compile-time constant：keyword-only `pto.constexpr`

所以迁移时要把旧的 TensorView/Tile descriptor entry 改成 pointer-first entry，然后在 kernel body 里显式重建 view。

PTO-DSL 写法：

```python
@pto.jit(target="a5")
def copy_kernel(
    A_ptr: pto.ptr(pto.f32, "gm"),
    O_ptr: pto.ptr(pto.f32, "gm"),
    rows: pto.i32,
    cols: pto.i32,
    *,
    BLOCK: pto.constexpr = 128,
):
    a_view = pto.make_tensor_view(A_ptr, shape=[rows, cols], strides=[cols, 1])
    o_view = pto.make_tensor_view(O_ptr, shape=[rows, cols], strides=[cols, 1])

    a_part = pto.partition_view(a_view, offsets=[0, 0], sizes=[rows, cols])
    o_part = pto.partition_view(o_view, offsets=[0, 0], sizes=[rows, cols])

    a_tile = pto.alloc_tile(shape=[1, BLOCK], dtype=pto.f32, valid_shape=[1, cols])
    o_tile = pto.alloc_tile(shape=[1, BLOCK], dtype=pto.f32, valid_shape=[1, cols])

    pto.tile.load(a_part, a_tile)
    pto.tile.store(o_tile, o_part)
```

这说明 PTO-DSL 的迁移方向不是“把旧 descriptor 原样搬过来”，而是把 entry ABI 标准化。host wrapper 负责把 shape/stride 拆成 runtime scalar 传入，kernel body 再组装 `TensorView`。

## 6. 迁移方式三：cube 从 `@ckernel` 变成 `@pto.cube`

Cube 侧迁移和 SIMD 类似：旧 DSL 的 `@pto.ckernel` 入口要变成 PTO-DSL 的 `@pto.cube` 子 kernel，再由 `@pto.jit(mode="explicit")` 调用。

PTO-DSL 当前可写成：

```python
@pto.cube
def matmul_tile(
    lhs_tile: pto.Tile,
    rhs_tile: pto.Tile,
    lhs_l0a: pto.Tile,
    rhs_l0b: pto.Tile,
    acc_tile: pto.Tile,
    out_tile: pto.Tile,
):
    m = pto.const(16)
    k = pto.const(16)
    n = pto.const(16)
    pto.mte_l1_l0a(lhs_tile.as_ptr(), lhs_l0a.as_ptr(), m, k)
    pto.mte_l1_l0b(rhs_tile.as_ptr(), rhs_l0b.as_ptr(), k, n, transpose=True)
    pto.mad(lhs_l0a.as_ptr(), rhs_l0b.as_ptr(), acc_tile.as_ptr(), m, n, k)
    pto.mte_l0c_ub(acc_tile.as_ptr(), out_tile.as_ptr(), m, n, n, n, 0)


@pto.jit(target="a5", mode="explicit")
def matmul_kernel():
    lhs_tile = pto.alloc_tile(shape=[16, 16], dtype=pto.f16, memory_space=pto.MemorySpace.MAT)
    rhs_tile = pto.alloc_tile(shape=[16, 16], dtype=pto.f16, memory_space=pto.MemorySpace.MAT)
    lhs_l0a = pto.alloc_tile(shape=[16, 16], dtype=pto.f16, memory_space=pto.MemorySpace.LEFT)
    rhs_l0b = pto.alloc_tile(shape=[16, 16], dtype=pto.f16, memory_space=pto.MemorySpace.RIGHT)
    acc_tile = pto.alloc_tile(shape=[16, 16], dtype=pto.f32, memory_space=pto.MemorySpace.ACC)
    out_tile = pto.alloc_tile(shape=[16, 16], dtype=pto.f32)
    matmul_tile(lhs_tile, rhs_tile, lhs_l0a, rhs_l0b, acc_tile, out_tile)
```

这里的迁移重点是把 cube-local memory space 显式化：

- `MAT`
- `LEFT`
- `RIGHT`
- `ACC`
- `BIAS`

这比旧 DSL 的模板/descriptor 隐式推导更直接，也更接近 PTO IR。

## 7. 之前的实现路径

从代码结构看，已经做过的迁移不是一刀切，而是按 surface 分层推进：

1. 先把 PTO-DSL 的 kernel 编译框架搭起来：`@pto.jit` 负责 tracing、specialization、MLIR module 输出和 runtime launch binding。
2. 再把 compute unit boundary 建出来：`@pto.simd`、`@pto.cube`、`@pto.simt` 作为子 kernel，只能从 `@pto.jit` 内调用。
3. 然后补底层 op wrapper：在 `ptodsl/ptodsl/_ops.py` 里用 MLIR Python binding 直接创建 PTO op。
4. 对 TileLang DSL 常用 surface 做同名或近似同名兼容：例如 `vlds`、`vsts`、`make_mask`、`mte_l1_l0a`、`mad`。
5. 把 descriptor 风格改成 explicit construction：`make_tensor_view`、`partition_view`、`alloc_tile`。
6. 用 tests 固化 public surface：`ptodsl/tests/test_jit_compile.py` 里有 vector、cube、pointer、mask、runtime scalar、tile slice 等 probe。

这条路线说明 PTO-DSL 不是在复用 TileLang DSL 的 AST frontend，而是在重建一个 PTO native frontend。TileLang DSL 里的 semantic/lowering 经验会被吸收，但实现方式已经不同。

## 8. 还没完全迁完的地方

下面这些是后续兼容 TileLang DSL 时要重点看的：

| 缺口 | 为什么重要 |
|---|---|
| `@vkernel/@ckernel` 兼容层 | 如果想让旧代码低成本迁移，需要考虑是否提供 shim 或迁移工具 |
| `strict_vecscope` 语义 | 旧代码大量依赖显式 capture、lane 范围和 block argument |
| TileLang template / `tpl` / op selection | 旧 DSL 的模板选择能力还没有完全等价到 PTO-DSL |
| advanced vector ops 全量覆盖 | `support_matrix.py` 里的 vector surface 很大，PTO-DSL 需要逐项对齐 |
| descriptor specialization | 旧 DSL 可从 descriptor 推导很多信息，PTO-DSL 当前要求显式 pointer/scalar |
| daemon / ExpandTileOp 相关路径 | 如果旧 TileLang DSL 用它做自动展开或模板实例化，需要单独迁移 |
| diagnostics 兼容 | 旧写法迁移时，错误信息要能指向替代 API，而不是只报 unsupported |

## 9. 建议的下一步迁移顺序

如果现在要继续推进“TileLang DSL 往 PTO-DSL 迁移”，建议按这个顺序：

1. 做一张 TileLang surface 到 PTO-DSL surface 的完整矩阵，以 `tilelang-dsl/python/tilelang_dsl/support_matrix.py` 为输入，以 `ptodsl/ptodsl/_ops.py` 和 tests 为 PTO-DSL 侧依据。
2. 先迁代表性 kernel，而不是先追求 API 全量覆盖。优先选 vector add/softmax、tile load/store、cube matmul、GM-UB DMA 这几类。
3. 给每个迁移 kernel 保留 old/new 对照测试，验证输出 PTO IR 的关键 op、kernel kind、function attr 和 verifier 行为。
4. 对高频旧写法提供明确替代模式，比如 `strict_vecscope -> @pto.simd`、`ckernel -> @pto.cube`、`TensorView entry -> ptr + make_tensor_view`。
5. 最后再决定是否做兼容 shim。如果一开始就做 shim，容易把旧 DSL 的复杂语义搬进 PTO-DSL，导致新 DSL 变得不干净。

## 10. 总结

当前更准确的结论是：

```text
TileLang DSL 还在维护，后端已经面向 PTO/VPTO IR。
PTO-DSL 正在建设新的 PTO native authoring surface。
迁移重点不是后端，而是前端 API、语义边界和用户写法。
已经迁出的核心 surface 包括 @pto.jit、@pto.simd、@pto.cube、TensorView/Tile 构造、vector load/store、mask、MTE 和 mad。
还没完全迁完的是 TileLang DSL 的模板、descriptor specialization、strict_vecscope 兼容、高级 vector surface 和部分自动展开能力。
```

所以你前面的判断是对的：现在处于 PTO-DSL 和 TileLang DSL 同时开发的阶段。等 PTO-DSL 的 API surface 足够覆盖 TileLang DSL，并且旧 kernel 能用 PTO-DSL 方式稳定表达后，才能说 TileLang DSL 的 authoring 角色可以退出。
