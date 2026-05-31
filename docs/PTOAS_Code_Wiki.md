# PTOAS Code Wiki

> **PTOAS** (PTO Assembler & Optimizer) v0.39 — 基于 LLVM/MLIR 19.1.7 的 PTO Bytecode 编译器工具链

---

## 目录

1. [项目概述](#1-项目概述)
2. [目录结构总览](#2-目录结构总览)
3. [系统架构](#3-系统架构)
4. [核心模块详解](#4-核心模块详解)
   - 4.1 [IR 层 — PTO Dialect](#41-ir-层--pto-dialect)
   - 4.2 [变换 Pass 层](#42-变换-pass-层)
   - 4.3 [同步分析子系统](#43-同步分析子系统)
   - 4.4 [代码生成层](#44-代码生成层)
   - 4.5 [内存规划](#45-内存规划)
5. [工具链](#5-工具链)
6. [关键数据结构与算法](#6-关键数据结构与算法)
7. [Python 绑定](#7-python-绑定)
8. [构建系统](#8-构建系统)
9. [测试体系](#9-测试体系)
10. [CI/CD](#10-cicd)
11. [依赖关系图](#11-依赖关系图)
12. [开发指南](#12-开发指南)
13. [架构演进：跳过 CCE 的新编译路径](#13-架构演进跳过-cce-的新编译路径设计中)

---

## 1. 项目概述

### 定位

PTOAS 是 PTO（Programming Tiling Operator）编译栈的核心组装器与优化器。它接收来自上层 AI 框架（PyPTO、TileLang、CuTile）生成的 **PTO IR**，经过一系列优化 Pass 后，生成面向昇腾 NPU 的 **C++ 代码**（通过 MLIR EmitC 方言）或 **PTO 二进制字节码**（PTOBC 格式）。

### 技术栈

| 组件 | 版本/技术 |
|------|----------|
| 编译器框架 | LLVM/MLIR 19.1.7 (commit `cd708029`) |
| 语言标准 | C++17 |
| 构建系统 | CMake >= 3.20 + Ninja |
| Python 绑定 | pybind11 2.12.0 |
| 目标硬件 | 昇腾 910B (A3 架构) / 昇腾 950 (A5 架构) |
| 许可证 | CANN Open Software License Agreement v2.0 |

### 核心能力

- **PTO IR 解析与验证** — 160+ 种 PTO 操作定义，完整的类型系统与语义检查
- **自动同步插入** — 基于流水线冲突分析，自动插入 set_flag/wait_flag 同步指令
- **内存规划** — 多缓冲槽位分配、生命周期分析、地址布局推断
- **代码生成** — PTO IR → EmitC → C++ 源码，供 bisheng 编译器编译为 NPU 可执行文件
- **字节码编解码** — PTOBC v0 二进制格式的编码/解码，支持调试信息

---

## 2. 目录结构总览

```
PTOAS/
├── include/                    # 头文件与 TableGen 定义
│   └── PTO/
│       ├── IR/                 #   PTO 方言定义（Ops, Types, Attrs, Interfaces）
│       └── Transforms/         #   Pass 声明 + 同步分析子系统头文件
│           ├── InsertSync/     #     传统同步插入分析
│           └── GraphSyncSolver/#     图同步求解器
│
├── lib/                        # 核心实现
│   └── PTO/
│       ├── IR/                 #   方言 C++ 实现（5 个源文件）
│       ├── Transforms/         #   变换 Pass 实现（52 个源文件）
│       │   ├── InsertSync/     #     同步插入 Pass 实现
│       │   └── GraphSyncSolver/#     图同步求解器实现
│       ├── CAPI/Dialect/       #   C 语言 API 绑定
│       └── Bindings/Python/    #   pybind11 Python 绑定
│
├── tools/                      # 命令行工具
│   ├── ptoas/                  #   主编译器 CLI（ptoas.cpp）
│   └── ptobc/                  #   字节码编解码器
│       ├── include/ptobc/      #     格式定义头文件
│       ├── src/                #     编解码实现
│       └── tests/              #     ptobc 单元测试
│
├── python/                     # Python 包构建脚本
│   └── pto/dialects/           #   Python 方言绑定
│
├── test/                       # 测试套件
│   ├── lit/                    #   LLVM LIT 回归测试（205 个 .pto 文件）
│   ├── samples/                #   算子样例（130 个子目录，449 个 .py 文件）
│   ├── python/                 #   Python 接口测试
│   ├── compile_cpp/            #   C++ 编译验证
│   └── npu_validation/         #   NPU 硬件验证框架
│
├── docs/                       # 文档
│   └── designs/                #   设计文档
│
├── cmake/                      # CMake 配置模板
├── docker/                     # Docker 构建与分发
├── .github/                    # GitHub CI/CD 工作流
│
├── CMakeLists.txt              # 顶层构建配置
├── README.md                   # 项目主文档
├── ReleaseNotes.md             # 版本发布说明
├── pyproject.toml              # Python 包元数据
└── install.sh                  # 安装辅助脚本
```

---

## 3. 系统架构

### 3.1 编译流水线全景

```
                         PTOAS 编译流水线
 ═══════════════════════════════════════════════════════════

  PyPTO / TileLang / CuTile          上层 AI 框架
           │
           ▼
  ┌─────────────────┐
  │   PTO IR 输入    │                MLIR 文本格式 (.pto)
  └────────┬────────┘
           │
  ═════════╪═══════════════════════════ ptoas 编译管线 ═══
           │
           ▼
  ┌─────────────────┐
  │ InferPTOMemScope │  ①  推断内存作用域 (GM/L1/L0/UB)
  └────────┬────────┘
           ▼
  ┌─────────────────┐
  │  InferPTOLayout  │  ②  推断 GlobalTensor 布局 (ND/DN/NZ)
  └────────┬────────┘
           ▼
  ┌─────────────────┐
  │   PlanMemory     │  ③  Tile 缓冲区分配 + 多缓冲规划
  └────────┬────────┘
           ▼
  ┌─────────────────┐
  │   InsertSync     │  ④  自动同步插入 (set_flag/wait_flag)
  │  或 GraphSync    │     支持传统模式 / 图求解器模式
  └────────┬────────┘
           ▼
  ┌─────────────────┐
  │ RemoveRedundant  │  ⑤  冗余 Barrier 消除
  │    Barrier       │
  └────────┬────────┘
           ▼
  ┌─────────────────┐
  │  LowerFrontend   │  ⑥  前端 TPUSH/TPOP → 内部 Pipe 操作
  │    PipeOps       │
  └────────┬────────┘
           ▼
  ┌─────────────────┐
  │  Materialize     │  ⑦  tile_buf handle 实体化
  │  TileHandles     │
  └────────┬────────┘
           ▼
  ┌─────────────────┐
  │ PTOViewToMemref  │  ⑧  View 描述符 → MemRef 降级
  └────────┬────────┘
           ▼
  ┌─────────────────┐
  │  PTOToEmitC      │  ⑨  PTO IR → EmitC 方言（核心代码生成）
  └────────┬────────┘
           │
  ═════════╪═══════════════════════════ ptoas 编译管线结束 ═
           │
           ▼
  ┌─────────────────┐
  │  C++ 源码输出    │  .cpp 文件，调用 pto-isa 模板库函数
  └────────┬────────┘  （TLOAD, Add, TSTORE, set_flag 等）
           │
  ═════════╪════════════════════════ 下游编译工具链 ════════
           │
           ▼
  ┌─────────────────┐
  │  pto-isa         │  C++ 模板头文件库（独立项目）
  │  (头文件库)      │  提供 TLOAD<T>/Add<T>/TSTORE<T> 等
  └────────┬────────┘  模板函数的硬件向量 intrinsic 实现
           │
           │  bisheng -xcce（C++ 编译 + 模板实例化）
           ▼
  ┌─────────────────┐
  │  LLVM IR         │  模板展开后的中间表示
  └────────┬────────┘
           │
           │  bisheng 后端（LLVM → 昇腾机器码）
           ▼
  ┌─────────────────┐
  │  NPU 可执行文件  │  昇腾 910B (A3) / 950 (A5)
  └─────────────────┘
```

**关键角色说明**：

- **PTOAS** — 负责 PTO IR 优化和代码生成，输出调用 pto-isa 函数的 C++ 源码
- **pto-isa** — 独立的 C++ 模板头文件库，提供 `TLOAD<T>`、`Add<T>`、`set_flag` 等函数的硬件实现。每个函数内部将 Tile 级操作展开为向量循环 + 硬件 intrinsic
- **bisheng** — 华为昇腾原生编译器（基于 LLVM），负责 C++ 编译（含模板实例化）和最终机器码生成
- **编译时间瓶颈**：C++ 模板实例化（pto-isa 的模板在 bisheng 编译时展开）是当前编译链中最慢的环节

### 3.2 字节码路径（PTOBC）

```
  PTO IR (.pto)
       │
       ├──► ptoas ──► C++ 代码     （主路径）
       │
       └──► ptobc encode ──► .ptobc 二进制
                                │
                                └──► ptobc decode ──► PTO IR 文本
```

### 3.3 Op 定义到 IR 生成链路（以 `pto.load_scalar` 为例）

上面两节描述了 PTO IR 被 ptoas 编译（3.1）或被 ptobc 编解码（3.2）的过程。本节回答上游问题：**PTO IR 本身是怎么产生的？**

完整链路为：`.td 定义` → `mlir-tblgen 自动生成 C++` → `手写验证器补充` → `Python 绑定暴露` → `用户 Python 代码构建 IR`。

#### 第 1 步：TableGen 声明式定义（.td — "设计图纸"）

**文件**: `include/PTO/IR/PTOOps.td:112-129`

```tablegen
def LoadScalarOp : PTO_Op<"load_scalar", [
    DeclareOpInterfaceMethods<MemoryEffectsOpInterface>
  ]> {
  let summary = "Load a single scalar element from a pointer at offset.";

  let arguments = (ins          // 输入操作数
    ScalarPtrOrMemRef:$ptr,     //   ptr: 标量指针或 MemRef
    Index:$offset               //   offset: 索引偏移
  );

  let results = (outs           // 输出结果
    AnyType:$value              //   value: 任意标量类型
  );

  let hasVerifier = 1;          // 需要手写验证器

  let assemblyFormat = [{       // IR 文本格式模板
    $ptr `[` $offset `]` attr-dict `:` type($ptr) `->` type($value)
  }];
}
```

这份声明定义了操作名、操作数、结果、Trait 和文本格式，是后续所有自动生成的源头。

#### 第 2 步：CMake 触发 mlir-tblgen（构建时 — "自动翻译"）

**文件**: `include/PTO/IR/CMakeLists.txt`

```cmake
set(LLVM_TARGET_DEFINITIONS PTOOps.td)

mlir_tablegen(PTOOps.h.inc   -gen-op-decls)     # → C++ 类声明
mlir_tablegen(PTOOps.cpp.inc -gen-op-defs)      # → C++ 类实现
mlir_tablegen(PTODialect.h.inc  -gen-dialect-decls -dialect=pto)
mlir_tablegen(PTODialect.cpp.inc -gen-dialect-defs -dialect=pto)
mlir_tablegen(PTOTypeDefs.h.inc  -gen-typedef-decls ...)
mlir_tablegen(PTOAttrs.h.inc     -gen-attrdef-decls ...)
mlir_tablegen(PTOEnums.h.inc     -gen-enum-decls)

add_public_tablegen_target(PTOOpsIncGen)
```

构建时等效执行 `mlir-tblgen -gen-op-decls PTOOps.td -o PTOOps.h.inc`，从一份 `.td` 生成多种 C++ 文件。

#### 第 3 步：自动生成的 C++ 代码（.inc — "样板产出"）

`mlir-tblgen` 自动生成到 `build/include/PTO/IR/`（简化示意）：

```cpp
// ===== PTOOps.h.inc (自动生成，不要手动修改) =====
namespace mlir::pto {

class LoadScalarOp
    : public Op<LoadScalarOp, OpTrait::OneResult,
                MemoryEffectsOpInterface::Trait> {
public:
  static StringRef getOperationName() { return "pto.load_scalar"; }

  // 从 .td 的 $ptr, $offset, $value 自动生成 getter
  Value getPtr()    { return getOperand(0); }
  Value getOffset() { return getOperand(1); }
  Value getValue()  { return getResult(0); }

  // 从 assemblyFormat 自动生成 IR 文本读写
  static ParseResult parse(OpAsmParser &parser, OperationState &result);
  void print(OpAsmPrinter &p);

  // hasVerifier = 1 → 只生成声明，实现要手写
  LogicalResult verify();

  // 自动生成构造方法
  static void build(OpBuilder &builder, OperationState &result,
                    Type value, Value ptr, Value offset);
};

} // namespace mlir::pto
```

**自动生成了什么**: getter/setter、parser/printer、build 构造方法。
**没有自动生成什么**: `verify()` 的实现体（因为 `hasVerifier = 1`）。

#### 第 4 步：手写验证器（C++ — "补充语义检查"）

**文件**: `lib/PTO/IR/PTO.cpp:6022-6039`

TableGen 无法表达的复杂约束，由开发者手写：

```cpp
LogicalResult LoadScalarOp::verify() {
  Type ptrTy = getPtr().getType();   // ← 使用自动生成的 getter
  Type elemTy;

  if (auto pty = dyn_cast<mlir::pto::PtrType>(ptrTy)) {
    elemTy = pty.getElementType();
  } else if (auto memTy = dyn_cast<MemRefType>(ptrTy)) {
    elemTy = memTy.getElementType();
    if (!isGmAddressSpaceAttr(memTy.getMemorySpace()))
      return emitOpError()
        << "scalar load only supports GM address space pointers";
  } else {
    return emitOpError("expects ptr to be !pto.ptr or memref type");
  }

  if (getValue().getType() != elemTy)
    return emitOpError("expects result type to match ptr element type");
  return success();
}
```

验证了两条规则：① ptr 必须是 `!pto.ptr<T>` 或 GM 地址空间的 memref；② 结果类型必须匹配指针元素类型。

#### 第 5 步：Python 绑定（暴露给上层框架）

**文件**: `python/pto/dialects/pto.py:538-550`

```python
def load_scalar(result_type, ptr, offset, *, loc=None, ip=None):
    operands = [
        get_op_result_or_value(ptr),
        get_op_result_or_value(offset),
    ]
    op = _ods_ir.Operation.create(
        "pto.load_scalar",             # 与 .td 中 PTO_Op<"load_scalar"> 对应
        results=[result_type],
        operands=operands,
        loc=loc, ip=ip,
    )
    return op.results[0]
```

调用 MLIR Python 绑定的 `Operation.create()`，在内存中的 IR Module 里创建一个 `pto.load_scalar` Op 节点。

#### 第 6 步：用户代码构建 IR

**文件**: `test/samples/ScalarPtr/scalar_ptr.py`

```python
from mlir.ir import Context, Location, Module, InsertionPoint
from mlir.dialects import func, arith, pto
from mlir.ir import F32Type, IndexType

def build():
    with Context() as ctx:
        pto.register_dialect(ctx, load=True)       # 注册 PTO 方言
        with Location.unknown(ctx):
            m = Module.create()
            f32 = F32Type.get(ctx)
            idx = IndexType.get(ctx)
            ptr_f32 = pto.PtrType.get(f32, ctx)    # !pto.ptr<f32>

            fn_ty = func.FunctionType.get([ptr_f32, ptr_f32], [])
            with InsertionPoint(m.body):
                fn = func.FuncOp("ptr_scalar_rw", fn_ty)
                entry = fn.add_entry_block()

            with InsertionPoint(entry):
                c4 = arith.ConstantOp(idx, 4).result
                c8 = arith.ConstantOp(idx, 8).result
                src, dst = entry.arguments
                src_off = pto.addptr(src, c8)

                val = pto.load_scalar(f32, src_off, c4)  # ★ 调用第5步的函数
                pto.store_scalar(dst, c4, val)
                func.ReturnOp([])

            m.operation.verify()  # 触发第4步的验证器
            return m

if __name__ == "__main__":
    print(build())               # 触发第3步自动生成的 print()
```

#### 第 7 步：输出 PTO IR 文本

`python scalar_ptr.py` 运行输出：

```mlir
module {
  func.func @ptr_scalar_rw(%arg0: !pto.ptr<f32>, %arg1: !pto.ptr<f32>) {
    %c4 = arith.constant 4 : index
    %c8 = arith.constant 8 : index
    %0 = pto.addptr %arg0, %c8 : !pto.ptr<f32> -> !pto.ptr<f32>
    %1 = pto.load_scalar %0[%c4] : !pto.ptr<f32> -> f32
    pto.store_scalar %1, %arg1[%c4] : !pto.ptr<f32>, f32
    return
  }
}
```

其中 `pto.load_scalar %0[%c4] : !pto.ptr<f32> -> f32` 的格式正是 `.td` 中 `assemblyFormat` 定义的模板。

#### 全链路总览图

```
  ┌─────────────────────────────────────────────────────────────┐
  │ ① PTOOps.td（声明式定义）                                    │
  │    def LoadScalarOp : PTO_Op<"load_scalar", [...]>          │
  │        arguments: (ptr, offset)  results: (value)           │
  │        assemblyFormat: $ptr `[` $offset `]` ...             │
  └──────────────────────┬──────────────────────────────────────┘
                         │  cmake 构建时 → mlir-tblgen (LLVM 自带)
                         ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ ② PTOOps.h.inc / PTOOps.cpp.inc（自动生成 C++）              │
  │    class LoadScalarOp {                                     │
  │      getPtr(), getOffset(), getValue()   // getter          │
  │      parse(), print()                    // IR 读写          │
  │      build()                             // 构造方法         │
  │      verify();                           // 仅声明           │
  │    }                                                        │
  └──────────────────────┬──────────────────────────────────────┘
                         │  开发者手写补充
                         ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ ③ PTO.cpp（手写验证器）                                      │
  │    LoadScalarOp::verify() { ... }                           │
  └──────────────────────┬──────────────────────────────────────┘
                         │  编译链接为 _pto.so
                         ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ ④ pto.py（Python 绑定）                                     │
  │    def load_scalar(result_type, ptr, offset):               │
  │        Operation.create("pto.load_scalar", ...)             │
  └──────────────────────┬──────────────────────────────────────┘
                         │  用户调用
                         ▼
  ┌─────────────────────────────────────────────────────────────┐
  │ ⑤ scalar_ptr.py（用户代码）                                  │
  │    val = pto.load_scalar(f32, src_off, c4)                  │
  │    print(module)  →  pto.load_scalar %0[%c4] : ... -> f32  │
  └─────────────────────────────────────────────────────────────┘
```

**核心理解**：`.td` 是"一次定义"的源头，`mlir-tblgen` 负责生成 C++ 样板代码（getter/parser/printer/builder），开发者只需手写 `verify()` 等复杂逻辑。Python 绑定层调用这些 C++ 类在内存中构建 IR 树，`print()` 将其序列化为 `.pto` 文本——也就是 3.1 节编译流水线的输入。

---

## 4. 核心模块详解

### 4.1 IR 层 — PTO Dialect

**源码位置**: `include/PTO/IR/` + `lib/PTO/IR/`

PTO Dialect 是整个编译器的 IR 基础，定义了面向 Tiling 操作的完整 MLIR 方言。

#### TableGen (.td) 与手写 C++ 的分工

PTO 方言由两部分共同构成，缺一不可：

```
.td 文件（TableGen 声明式定义）            手写 C++（补充逻辑）
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━          ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

 Op 有哪些、叫什么名字                     复杂语义验证 (verify)
 每个 Op 的输入/输出类型约束                 常量折叠 (fold)
 IR 文本格式 (assemblyFormat)               规范化 (canonicalize)
 类型定义 (tile_buf, ptr 等)                类型推断的复杂逻辑
 属性定义 (AddressSpace, PIPE 等)           工具函数 (地址空间判断等)
 枚举定义                                  接口实现 (MemoryEffects 等)
 getter/setter/builder 等样板               方言初始化钩子

 PTOOps.td: 6,083 行                      PTO.cpp: 12,229 行
```

`.td` 定义了方言的"骨架"——有什么 Op、什么类型、什么属性。构建时由 LLVM 自带的 `mlir-tblgen` 工具自动生成 C++ 样板代码（getter、parser、printer、builder）。手写 C++ 补充了"血肉"——TableGen 的声明式语法无法表达的复杂约束（如跨操作数的条件判断、地址空间校验等），需要在 `PTO.cpp` 中手动实现 `verify()`、`fold()` 等方法。

完整的 Op 定义到 IR 生成链路详见 [3.3 节](#33-op-定义到-ir-生成链路以-ptoload_scalar-为例)。

#### 4.1.1 操作定义（PTOOps.td — 6,083 行）

160+ 种操作，按功能分类：

| 类别 | 代表操作 | 说明 |
|------|---------|------|
| **指针/视图** | `AddPtrOp`, `MakeTensorViewOp`, `PartitionViewOp` | 地址运算与张量视图构建 |
| **标量读写** | `LoadScalarOp`, `StoreScalarOp` | 标量内存访问 |
| **Tensor 操作** | `TensorViewOp`, `PartitionTensorViewOp` | 张量分片与视图变换 |
| **数据搬运** | `TLoadOp`, `TStoreOp`, `TMovOp` | GM↔L1↔L0 数据搬运 |
| **向量计算** | `TVecAddOp`, `TVecMulOp`, `TVecExpOp` ... | L0 向量流水线计算 |
| **矩阵计算** | `TMatMulOp` (Cube 指令) | L0C 矩阵乘法 |
| **同步** | `BarrierOp`, `EventSetOp`, `EventWaitOp` | 流水线同步原语 |
| **流水线** | `TPushOp`, `TPopOp` | 前端流水线通信 |
| **布局** | `InferLayoutOp`, `MaterializeTileOp` | 布局推断与 Tile 实体化 |
| **通信** | Async DMA, collective ops | 多卡通信操作 |

#### 4.1.2 类型系统（PTOTypeDefs.td）

```
!pto.ptr<T>                         指针类型（带元素类型参数）
!pto.tensor_view<d0xd1x...xT>      全局张量视图（动态/静态形状）
!pto.partition_tensor_view<...>     逻辑分区视图（高维分片）
!pto.tile<d0xd1x...xT>             固定形状 Tile
!pto.tile_buf<shape, T, mem, ...>   带内存规划信息的 Tile 缓冲区
!pto.eventid_array<size>            动态事件 ID 数组
!pto.local_array<shape x T>         C++ 栈上静态数组
!pto.pipe                           TPUSH/TPOP 流水线句柄
!pto.async_session                  异步 DMA 会话句柄
!pto.async_event                    异步 DMA 事件句柄
!pto.hif8                           A5 HiFloat8 类型（1 字节/元素）
!pto.f4E1M2x2                       FP4 打包对（E1M2 格式）
!pto.f4E2M1x2                       FP4 打包对（E2M1 格式）
```

`tile_buf` 是最核心的类型，携带完整的内存规划元数据：

```
!pto.tile_buf<16x256xf16,          形状: 16x256, 元素类型: f16
              #pto.address_space<VEC>,  内存空间: UB
              validShape = [16, 128],   有效形状
              config = {                配置属性
                blayout = RowMajor,
                slayout = NoneBox,
                padValue = Zero,
                compactMode = Normal
              }>
```

#### 4.1.3 属性系统（PTOAttrs.td）

**地址空间 (AddressSpace)**:

| 枚举值 | ID | 对应硬件 |
|--------|-----|---------|
| `Zero` (Default) | 0 | 默认 |
| `GM` | 1 | Global Memory (DDR/HBM) |
| `MAT` | 2 | Matrix Buffer (CB) |
| `LEFT` | 3 | L0A (矩阵左操作数) |
| `RIGHT` | 4 | L0B (矩阵右操作数) |
| `ACC` | 5 | L0C (累加器) |
| `VEC` | 6 | Vector Buffer (UB) |
| `BIAS` | 7 | Bias Buffer |
| `SCALING` | 8 | Scaling Buffer |

**流水线 (PIPE)**:

| 枚举值 | ID | 含义 |
|--------|-----|------|
| `PIPE_S` | 0 | 标量流水线 |
| `PIPE_V` | 1 | 向量流水线 |
| `PIPE_M` | 2 | 矩阵流水线 (Cube) |
| `PIPE_MTE1` | 3 | L1 → L0 数据搬运 |
| `PIPE_MTE2` | 4 | GM → L1 加载 |
| `PIPE_MTE3` | 5 | L1 → GM 存储 |
| `PIPE_ALL` | 6 | 全流水线 Barrier |
| `PIPE_MTE4` | 7 | 扩展搬运 4 |
| `PIPE_MTE5` | 8 | 扩展搬运 5 |
| `PIPE_V2` | 9 | 向量流水线 2 |
| `PIPE_FIX` | 10 | 固定流水线 |
| `VIRTUAL_PIPE_MTE2_L1A` | 11 | 虚拟：GM→L1A |
| `VIRTUAL_PIPE_MTE2_L1B` | 12 | 虚拟：GM→L1B |

**其他属性**:
- `#pto.layout<ND|DN|NZ>` — GlobalTensor 布局（行优先/列优先/Fractal）
- `#pto.pipe_event_type<...>` — 高层同步端点类型
- `#pto.sync_op_type<...>` — 同步操作类型（TLOAD, TSTORE_ACC, TMOV_M2L 等）
- `RoundMode` — 舍入模式（NONE, RINT, ROUND, FLOOR, CEIL, TRUNC, ODD, CAST_RINT）
- `SaturationMode` — 饱和模式（ON, OFF）
- `CmpMode` — 比较模式（EQ, NE, LT, LE, GT, GE）
- `PadValue` — 填充值（Null, Zero, Max, Min）
- `CompactMode` — 紧凑模式（Null, Normal, RowPlusOne）

#### 4.1.4 关键 C++ API（PTO.h）

```cpp
// 目标架构
enum class PTOArch { A3, A5 };
PTOArch getTargetArch(ModuleOp module);
bool isTargetArchA3(Operation *op);
bool isTargetArchA5(Operation *op);

// 地址空间查询
AddressSpaceAttr getPTOAddressSpaceAttr(Type type);
bool isScalarPtrOrMemRef(Type type);

// 入口函数管理
bool isPTOEntryFunction(func::FuncOp func);
LogicalResult validatePTOEntryFunctions(ModuleOp module);
void annotatePTOEntryFunctions(ModuleOp module);

// RAII 目标架构切换
class ScopedPTOParserTargetArch {
  explicit ScopedPTOParserTargetArch(MLIRContext *ctx, PTOParserTargetArch arch);
  ~ScopedPTOParserTargetArch();  // 自动恢复
};
```

#### 4.1.5 源文件清单

| 文件 | 大小 | 职责 |
|------|------|------|
| `lib/PTO/IR/PTO.cpp` | 12,229 行 | 方言注册、Op 定义、验证器、语义检查 |
| `lib/PTO/IR/PTOAttrs.cpp` | — | 属性实现（解析、打印、验证） |
| `lib/PTO/IR/PTOTypeDefs.cpp` | — | 类型定义实现 |
| `lib/PTO/IR/PTOTypeUtils.cpp` | — | 类型工具函数 |
| `lib/PTO/IR/PTOSyncUtils.cpp` | — | 同步工具函数 |

---

### 4.2 变换 Pass 层

**源码位置**: `include/PTO/Transforms/Passes.td` + `lib/PTO/Transforms/`

共 **17 个已注册 Pass**，52 个源文件。按编译阶段分类：

#### 阶段 A：推断与分析

| Pass | 作用域 | 说明 |
|------|--------|------|
| `InferPTOMemScope` | `func::FuncOp` | 推断 PTO 操作的内存作用域（GM/L1/L0/UB） |
| `InferPTOLayout` | `func::FuncOp` | 推断 GlobalTensor 布局（ND/DN/NZ） |
| `ConvertToPTOOp` | `ModuleOp` | 将其他方言操作转换为 PTO 操作 |

#### 阶段 B：内存规划

| Pass | 作用域 | 说明 | 关键选项 |
|------|--------|------|---------|
| `PlanMemory` | `ModuleOp` | Tile 缓冲区分配、多缓冲、地址布局 | `mem-plan-mode`, `enable-global-workspace-reuse`, `restrict-inplace-as-isa` |

#### 阶段 C：同步插入

| Pass | 作用域 | 说明 | 关键选项 |
|------|--------|------|---------|
| `PTOInsertSync` | `func::FuncOp` | 传统同步插入（基于流水线冲突分析） | — |
| `PTOInjectBarrierAllSync` | `func::FuncOp` | 保守模式：在所有内存操作前插入 PIPE_ALL barrier | — |
| `PTOGraphSyncSolver` | `func::FuncOp` | 图同步求解器（实验性，Dijkstra + 事件ID着色） | `event-id-num-max` (默认 8) |
| `PTORemoveRedundantBarrier` | `func::FuncOp` | 消除冗余 Barrier | — |

#### 阶段 D：流水线降级

| Pass | 作用域 | 说明 |
|------|--------|------|
| `PTOAssignDefaultFrontendPipeId` | `func::FuncOp` | 为前端 TPUSH/TPOP 分配默认 id=0 |
| `PTOLowerFrontendPipeOps` | `func::FuncOp` | 前端 TPUSH/TPOP → 内部 pipe 操作 |
| `PTOInferValidatePipeInit` | `ModuleOp` | 推断并验证 pipe init nosplit 配置 |
| `PTOLoweringSyncToPipe` | `func::FuncOp` | 高层 record_event/wait_event → 底层 set_flag/wait_flag |
| `PTOWrapFunctionsInSections` | `func::FuncOp` | 用 section.cube/section.vector 包裹函数 |
| `PTOVerifyTFree` | `func::FuncOp` | 验证 pto.tpop 与 pto.tfree 的匹配 |

#### 阶段 E：降级与代码生成

| Pass | 作用域 | 说明 |
|------|--------|------|
| `PTOResolveReservedBuffers` | `ModuleOp` | 解析保留缓冲区地址 + peer pipe flag 基址 |
| `PTOMaterializeTileHandles` | `ModuleOp` | 从规划后的 memref 实体化 tile_buf handle |
| `PTOViewToMemref` | `ModuleOp` | PTO View → MemRef 降级（含元数据绑定） |
| `EmitPTOManual` (PTOToEmitC) | — | PTO IR → EmitC 方言（最终 C++ 代码生成） |

#### 阶段 F：架构特定优化

| Pass | 作用域 | 说明 |
|------|--------|------|
| `PTOA5NormalizeTMov` | `func::FuncOp` | A5 架构：规范化 vec→vec col_major TMOV |

---

### 4.3 同步分析子系统

**源码位置**: `lib/PTO/Transforms/InsertSync/` + `lib/PTO/Transforms/GraphSyncSolver/`

昇腾 NPU 采用多流水线并行架构（Scalar/Vector/Matrix/MTE1-5），操作之间存在 RAW/WAR/WAW 数据依赖。PTOAS 提供两种同步策略：

#### 4.3.1 传统同步插入（InsertSync）

**核心算法流程**:

```
  1. 遍历函数体，构建 InstanceElement 序列
     ├── CompoundInstanceElement  — 计算/搬运指令
     ├── LoopInstanceElement      — 循环边界 (LOOP_BEGIN/END)
     ├── BranchInstanceElement    — 分支边界 (IF_BEGIN/ELSE/END)
     └── PlaceHolderInstanceElement — 虚拟占位

  2. 对每对相邻操作，检查内存依赖
     ├── 提取 defVec (写缓冲区) 和 useVec (读缓冲区)
     ├── 比较 rootBuffer 和 AddressSpace
     └── 判断 RAW/WAR/WAW 冲突

  3. 若存在冲突，插入同步操作
     ├── SET_EVENT (src pipe)
     ├── WAIT_EVENT (dst pipe)
     └── 或 PIPE_BARRIER (全局屏障)

  4. 优化
     ├── 跟踪已同步 pipe 避免重复
     ├── 循环头尾特殊处理 (UNIT_FLAG)
     └── 分支合并同步状态
```

**关键文件**:

| 文件 | 职责 |
|------|------|
| `PTOInsertSync.cpp` | Pass 入口，驱动分析与代码生成 |
| `InsertSyncAnalysis.cpp` | 依赖分析核心（700+ 行） |
| `MemoryDependentAnalyzer.cpp` | 内存依赖求解器 |
| `SyncCodegen.cpp` | 同步操作代码生成 |
| `SyncEventIdAllocation.cpp` | 事件 ID 分配 |
| `RemoveRedundantSync.cpp` | 冗余同步消除 |
| `MoveSyncState.cpp` | 同步状态优化 |

#### 4.3.2 图同步求解器（GraphSyncSolver — 实验性）

基于图论的全局最优同步方案求解：

```
  1. 构建 SyncSolver IR
     ├── 将 MLIR FuncOp 翻译为层次化 IR
     │   Function → Scope → Operation
     └── 线性化为 Occurrence 序列

  2. 检测冲突对 (ConflictPair)
     └── 基于内存依赖分析

  3. 构建邻接图
     ├── 节点: CorePipeInfo (core type + pipe)
     ├── 边: ConflictPair (start → end)
     └── 权重: 事件 ID 开销

  4. Dijkstra 最短路径求解
     ├── 找到最优事件 ID 分配
     └── 支持 UnitFlag 优化

  5. 代码生成
     └── 从求解结果生成 set_flag/wait_flag
```

**GraphSolver 核心接口**:

```cpp
class GraphSolver {
  struct Edge {
    ConflictPair *conflictPair;
    CorePipeInfo corePipeSrc, corePipeDst;
    int startIndex, endIndex;
    bool isUnitFlag;
  };

  void addConflictPair(ConflictPair *pair);
  void optimizeAdjacencyList();
  std::optional<int> runDijkstra(CorePipeInfo src, CorePipeInfo dst,
                                 int startIdx, int endIdx);
};
```

**关键文件**:

| 文件 | 职责 |
|------|------|
| `SyncSolver.cpp` | 求解器主编排（420+ 行） |
| `GraphSolver.cpp` | Dijkstra 最短路径求解 |
| `EventIdSolver.cpp` | 事件 ID 着色分配 |
| `SyncSolverIR.cpp` | 内部 IR 表示 |
| `SyncSolverIRTranslator.cpp` | MLIR → SyncSolver IR 翻译 |
| `SyncSolverCodeGen.cpp` | 求解结果 → 同步代码 |
| `MemInfo.cpp` | 内存信息分析 |

---

### 4.4 代码生成层

代码生成分为两步：PTOAS 内部的**方言间转换**（PTO → EmitC）和 MLIR 自带的**文本翻译**（EmitC → C++）。

#### 什么是 EmitC 方言

**EmitC 是 MLIR/LLVM 19.1.7 自带的标准方言**，不是 PTOAS 开发的。它用 MLIR IR 来"描述" C/C++ 代码：

```mlir
emitc.include "header.hpp"                          → #include "header.hpp"
emitc.func @foo(%a: !emitc.opaque<"int">) { ... }  → void foo(int a) { ... }
emitc.call_opaque "TLOAD" (%dst, %src)              → TLOAD(dst, src);
emitc.variable "0" : !emitc.opaque<"int">           → int x = 0;
```

MLIR 同时提供 `emitc::translateToCpp()` 翻译器，将 EmitC IR 直接打印为 C++ 文本。

#### PTOToEmitC（504KB — 最大的单文件）

**路径**: `lib/PTO/Transforms/PTOToEmitC.cpp`

**输入**：经过前序 8 个 Pass 处理后的 IR，包含 PTO 方言 + MemRef + Arith + SCF 等多种方言混合。

**输出**：纯 EmitC 方言 IR。

**转换机制**：注册了 **100+ 个 `OpConversionPattern`**，每个 PTO Op 对应一个模式替换规则。核心步骤：

```
① 配置非法方言（PTO, MemRef, Arith, SCF → 全部标记为 illegal）
② 配置合法方言（EmitC → 标记为 legal）
③ 注册 100+ 个转换 Pattern，例如：
   pto.tload  → emitc.call_opaque "TLOAD"
   pto.tadd   → emitc.call_opaque "Add"
   pto.barrier → emitc.call_opaque "pipe_barrier"
   memref     → emitc.opaque<"LocalTensor<T>">
   func.func  → emitc.func
④ applyPartialConversion() 执行批量替换
⑤ 清理残留的 UnrealizedConversionCast
```

以 `pto.tload` 为例（`PTOToEmitC.cpp:4072-4108`）：

```cpp
struct PTOTLoadToTLOAD : public OpConversionPattern<pto::TLoadOp> {
  LogicalResult matchAndRewrite(pto::TLoadOp op, OpAdaptor adaptor,
                                ConversionPatternRewriter &rewriter) const override {
    Value src = peelUnrealized(adaptor.getSrc());
    Value dst = peelUnrealized(adaptor.getDst());
    // 如果 src 是 GM memref，包一层 GlobalTensor
    if (isGlobal)
      srcArg = buildGlobalTensorFromMemref(rewriter, op.getLoc(), src, ...);
    // pto.tload → emitc.call_opaque "TLOAD"
    rewriter.create<emitc::CallOpaqueOp>(
        op.getLoc(), TypeRange{}, "TLOAD",    // 直接映射为 C++ 函数名
        ArrayAttr{}, ArrayAttr{}, ValueRange{dst, srcArg});
    rewriter.eraseOp(op);
    return success();
  }
};
```

**最终 C++ 输出**：在 `ptoas.cpp:1235` 调用 MLIR 自带翻译器：

```cpp
emitc::translateToCpp(*module, cppOS);
```

生成的 C++ 代码调用 pto-isa 模板库的函数（`TLOAD`、`Add`、`set_flag` 等），后续由 bisheng 编译。

**PTOAS 做了什么 vs MLIR 提供了什么**：

```
MLIR/LLVM 自带                        PTOAS 开发
━━━━━━━━━━━━━━━━━━━━━━━━              ━━━━━━━━━━━━━━━━━━━━━━━━━━

EmitC 方言定义                         PTO 方言定义（160+ Op）
emitc.call_opaque 等 Op               100+ 个 ConversionPattern
translateToCpp() 翻译器                PTOToEmitCTypeConverter
类型转换框架                           类型映射（tile_buf → LocalTensor 等）
```

#### PTOViewToMemref（120KB）

**路径**: `lib/PTO/Transforms/PTOViewToMemref.cpp`

将 PTO 的高层 View 描述符降级为 MLIR MemRef：

- 张量视图实体化 — `!pto.tensor_view` → `memref<...>`
- 分区视图降级 — `!pto.partition_tensor_view` → 偏移 + 步长计算
- 元数据绑定 — 将布局、地址空间等信息编码到 MemRef 的 affine map

---

### 4.5 内存规划

**路径**: `lib/PTO/Transforms/PTOPlanMemory.cpp`（88KB）

**核心功能**:

1. **生命周期分析** — 确定每个 tile_buf 的活跃区间
2. **多缓冲槽位分配** — 支持 double buffering / multi-buffering
3. **地址布局推断** — 结合布局属性确定内存地址
4. **别名分析** — 检测重叠缓冲区
5. **工作空间复用** — 全局 workspace 内存复用优化

**Pass 选项**:

| 选项 | 说明 |
|------|------|
| `mem-plan-mode` | 内存规划策略 |
| `enable-global-workspace-reuse` | 启用全局 workspace 内存复用 |
| `enable-print-memory-allocated-size` | 打印分配的内存大小 |
| `restrict-inplace-as-isa` | 按 ISA 约束限制原地操作 |

**辅助 Pass**: `OptMemPlanForPipeline.cpp` — 面向流水线优化的内存规划

---

## 5. 工具链

### 5.1 ptoas — 主编译器

**路径**: `tools/ptoas/ptoas.cpp`（44KB）

基于 MLIR 的 `MlirOptMain` 框架构建的 CLI 工具。

**CLI 参数**:

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `<input>` | `-` (stdin) | 输入 PTO IR 文件 |
| `-o <file>` | `-` (stdout) | 输出文件 |
| `--pto-arch a3\|a5` | — | 目标架构 |
| `--pto-level level1\|level2\|level3` | — | 优化级别 |
| `--enable-insert-sync` | `false` | 启用传统同步插入 |
| `--enable-graph-sync-solver` | `false` | 启用图同步求解器（实验性） |
| `--enable-inject-barrier-all-sync` | `false` | 启用保守 barrier 插入 |
| `--disable-infer-layout` | `false` | 跳过布局推断 |
| `--emit-pto-ir` | `false` | 输出 PTO IR 而非 C++ |

**注意**: `--enable-insert-sync`、`--enable-graph-sync-solver`、`--enable-inject-barrier-all-sync` 三者互斥。

**典型用法**:

```bash
# 编译 PTO IR 为 C++，启用自动同步
ptoas input.pto --pto-arch a3 --enable-insert-sync -o output.cpp

# 仅做 IR 优化，不生成 C++
ptoas input.pto --pto-arch a5 --emit-pto-ir -o optimized.pto
```

**Pass 编排逻辑**:

`ptoas.cpp` 中的 `reorderEmitCFunctions()` 负责对生成的 EmitC 函数做拓扑排序，确保函数定义顺序满足前向引用约束。

### 5.2 ptobc — 字节码编解码器

**路径**: `tools/ptobc/`

PTO 二进制字节码（PTOBC v0）的编码器与解码器。

**CLI 用法**:

```bash
# 编码: PTO IR → 二进制字节码
ptobc encode input.pto -o output.ptobc

# 解码: 二进制字节码 → PTO IR 文本
ptobc decode input.ptobc -o output.pto
```

**PTOBC v0 文件格式**:

```
┌─────────────────────────────────┐
│  Magic + Version (kVersionV0)   │
│  Flags (kFlagsV0)               │
├─────────────────────────────────┤
│  Section: Strings      (0x01)   │  字符串池
├─────────────────────────────────┤
│  Section: Types        (0x02)   │  类型定义（ASM 文本）
├─────────────────────────────────┤
│  Section: Attrs        (0x03)   │  属性定义（ASM 文本）
├─────────────────────────────────┤
│  Section: ConstPool    (0x04)   │  常量池
├─────────────────────────────────┤
│  Section: OpcodeSchema (0x05)   │  扩展操作码
├─────────────────────────────────┤
│  Section: Module       (0x06)   │  MLIR 模块数据
├─────────────────────────────────┤
│  Section: DebugInfo    (0x07)   │  调试信息（文件/行号/片段）
├─────────────────────────────────┤
│  Section: Extra        (0x7F)   │  扩展元数据
└─────────────────────────────────┘
```

**核心数据结构** (`ptobc_format.h`):

```cpp
struct PTOBCFile {
  StringTable strings;                      // 字符串表
  std::vector<std::string> typeAsm;         // 类型 ASM（1-based ID）
  std::vector<std::string> attrAsm;         // 属性 ASM（1-based ID）
  std::vector<ConstEntry>  consts;          // 常量池
  std::vector<DebugFileEntry>      dbgFiles;       // 源文件信息
  std::vector<DebugValueNameEntry> dbgValueNames;  // 值命名
  std::vector<DebugLocationEntry>  dbgLocations;   // 源码位置
  std::vector<DebugSnippetEntry>   dbgSnippets;    // 代码片段

  std::vector<uint8_t> moduleBytes;         // MLIR 模块二进制

  std::vector<uint8_t> serialize() const;   // 序列化为完整文件
};
```

**实现文件**:

| 文件 | 职责 |
|------|------|
| `ptobc_format.cpp` | 二进制格式序列化/反序列化 |
| `mlir_encode.cpp` | MLIR IR → PTOBC 编码（21KB） |
| `ptobc_decode_print.cpp` | PTOBC → 文本解码打印（32KB） |
| `canonical_printer.cpp` | 规范化打印 |
| `leb128.cpp` | LEB128 变长整数编码 |
| `mlir_helpers.cpp` | MLIR 辅助工具 |

### 5.3 pto-isa — C++ 模板头文件库（外部依赖）

**仓库**: 独立项目（`https://gitcode.com/cann/pto-isa.git`），通过 CI pin 的 commit 版本与 PTOAS 对齐。

**定位**: pto-isa 是 PTOAS 生成的 C++ 代码的**运行时库**。PTOAS 通过 EmitC 生成 `TLOAD(dst, src)` 这样的 C++ 函数调用，而这些函数的实际实现在 pto-isa 的头文件中。

**工作原理**:

```
PTOAS 生成的 C++ 代码：           pto-isa 头文件中的实现：
━━━━━━━━━━━━━━━━━━━━━━━          ━━━━━━━━━━━━━━━━━━━━━━━━━━━━━

#include "pto/pto-inst.hpp"       // pto-isa 提供
using namespace pto;

void kernel(GlobalTensor<half> a) {
  LocalTensor<half> buf;
  TLOAD(buf, a);                  → template<typename T>
                                    void TLOAD(LocalTensor<T>& dst,
                                              GlobalTensor<T>& src) {
                                      // 内部展开为向量循环
                                      // + 硬件 DMA intrinsic
                                    }

  Add(buf, buf, buf2);            → template<typename T>
                                    void Add(LocalTensor<T>& dst, ...) {
                                      for (i = 0; i < ...; i += VEC) {
                                        __builtin_vadd(...);
                                      }
                                    }

  set_flag(PIPE_MTE2, PIPE_V, 0); → 直接映射为硬件同步指令
}
```

**核心内容**:
- `include/pto/pto-inst.hpp` — 主头文件，包含所有 Tile 操作的 C++ 模板实现
- `include/pto/comm/pto_comm_inst.hpp` — 通信操作实现
- `tests/common/` — 公共测试头文件

**与 PTOAS 的关系**:
- PTOAS 不包含 pto-isa 的源码，仅在生成的 C++ 中 `#include` 它
- CI 通过 `.github/workflows/update_pto_isa_pin.yml` 锁定 pto-isa 的 commit 版本
- NPU 板测时需要 `PTO_ISA_ROOT` 环境变量指向 pto-isa 的安装路径
- pto-isa 的版本必须与 CANN/bisheng 版本对齐，否则会出现编译错误（尤其 A5 架构）

---

## 6. 关键数据结构与算法

### 6.1 同步分析数据结构

```cpp
// 同步分析模式
enum class SyncAnalysisMode {
  NORMALSYNC,   // 核内：流水线冲突解析
  BLOCKSYNC     // 核间：CV 分离通信
};

// 流水线类型（见 §4.1.3 PIPE 枚举）
enum class PipelineType : uint32_t { ... };

// 核心类型
enum class TCoreType {
  VECTOR,           // 向量核
  CUBE,             // 矩阵核
  CUBE_OR_VECTOR,   // 二选一
  CUBE_AND_VECTOR   // 两者
};

// 同步操作类型
enum class TYPE {
  SET_EVENT,           // 设置事件标志
  WAIT_EVENT,          // 等待事件标志
  PIPE_BARRIER,        // 全流水线屏障
  PIPE_BARRIER_CUBE,   // Cube 专用屏障
  PIPE_BARRIER_VECTOR, // Vector 专用屏障
  SYNC_BLOCK_SET,      // 核间同步设置
  SYNC_BLOCK_WAIT,     // 核间同步等待
  SYNC_BLOCK_ALL       // 核间全同步
};

// UnitFlag 模式（循环优化）
enum class UNIT_FLAG {
  DISABLED,
  ENABLED_WITH_UPDATE,
  ENABLED_ONLY_FIRST_ITER,
  ENABLED_ONLY_LAST_ITER,
  ENABLED_ONLY_FIRST_AND_LAST_ITERS
};
```

### 6.2 内存信息

```cpp
struct BaseMemInfo {
  Value baseBuffer;                    // 操作数直接缓冲区
  Value rootBuffer;                    // 根分配源
  pto::AddressSpace scope;             // 内存作用域
  SmallVector<uint64_t> baseAddresses; // 基地址列表
  uint64_t allocateSize;               // 缓冲区大小
};
```

### 6.3 同步操作

```cpp
class SyncOperation {
  TYPE type;
  PipelineType srcPipe, dstPipe;
  SmallVector<int> eventIds;
  SmallVector<Value> depRootBuffers;
  TCoreType syncCoreType;
  // ...

  bool isSyncSetType() const;
  bool isSyncWaitType() const;
  bool isBarrierType() const;
  void SetPipeAll();
};
```

### 6.4 实例元素层次（InsertSync IR）

```
InstanceElement (基类)
├── CompoundInstanceElement    计算/搬运指令
│   ├── defVec[]               写缓冲区列表 (BaseMemInfo)
│   ├── useVec[]               读缓冲区列表 (BaseMemInfo)
│   ├── kPipeValue             所属流水线
│   └── unitFlagMode*          循环优化标志
├── LoopInstanceElement        循环边界 (LOOP_BEGIN / LOOP_END)
├── BranchInstanceElement      分支边界 (IF_BEGIN / ELSE_BEGIN / IF_END)
└── PlaceHolderInstanceElement 虚拟占位（带 parentScopeId）
```

### 6.5 GraphSyncSolver 算法

```
输入: MLIR FuncOp
  ↓
翻译为 SyncSolverIR (层次化: Function → Scope → Op)
  ↓
线性化为 Occurrence 序列
  ↓
检测 ConflictPair (内存依赖冲突)
  ↓
构建邻接图 (CorePipeInfo 节点, Edge 边)
  ↓
每对 (srcPipe, dstPipe) 运行 Dijkstra
  ↓
EventIdSolver 分配事件 ID (上限: event-id-num-max)
  ↓
SyncSolverCodeGen 生成 set_flag/wait_flag
  ↓
输出: 带同步指令的 MLIR FuncOp
```

---

## 7. Python 绑定

**源码位置**: `lib/Bindings/Python/PTOModule.cpp`（40KB）

通过 pybind11 向 Python 暴露 PTO 方言的完整 API。

### 7.1 模块结构

```
python/
└── pto/
    └── dialects/
        ├── pto.py              手写方言绑定
        └── _pto_ops_gen.py     TableGen 自动生成的 Op 绑定

mlir/
└── _mlir_libs/
    └── _pto.cpython-*.so       pybind11 编译产物
```

### 7.2 暴露的 Python API

**枚举类型**:

```python
import pto

# 地址空间
pto.AddressSpace.GM          # Global Memory
pto.AddressSpace.VEC         # Vector Buffer (UB)
pto.AddressSpace.MAT         # Matrix Buffer
pto.AddressSpace.LEFT        # L0A
pto.AddressSpace.RIGHT       # L0B
pto.AddressSpace.ACC         # L0C
pto.AddressSpace.BIAS        # Bias Buffer
pto.AddressSpace.SCALING     # Scaling Buffer

# 流水线
pto.PIPE.PIPE_S              # 标量
pto.PIPE.PIPE_V              # 向量
pto.PIPE.PIPE_M              # 矩阵
pto.PIPE.PIPE_MTE1           # L1→L0
pto.PIPE.PIPE_MTE2           # GM→L1
pto.PIPE.PIPE_MTE3           # L1→GM
pto.PIPE.PIPE_ALL            # Barrier

# 布局
pto.BLayout.RowMajor         # 行优先
pto.BLayout.ColMajor         # 列优先
pto.SLayout.NoneBox / RowMajor / ColMajor

# 计算属性
pto.PadValue.Zero / Max / Min
pto.CompactMode.Normal / RowPlusOne
pto.RoundMode.RINT / ROUND / FLOOR / CEIL / TRUNC
pto.SaturationMode.ON / OFF
pto.CmpMode.EQ / NE / LT / LE / GT / GE
```

**方言注册函数**:

```python
pto.register_dialect(context, load=True)
```

---

## 8. 构建系统

### 8.1 前置依赖

| 依赖 | 版本要求 | 说明 |
|------|---------|------|
| CMake | >= 3.20 | 构建系统 |
| GCC | >= 9 | 或 Clang（需 C++17 支持） |
| LLVM/MLIR | 19.1.7 (精确版本) | 编译器框架 |
| Python | >= 3.8 | Python 绑定（可选） |
| pybind11 | 2.12.0 | Python/C++ 桥接（可选） |
| Ninja | 推荐 | 构建后端 |

### 8.2 构建步骤

**1) 构建 LLVM 19.1.7**:

```bash
git clone https://github.com/llvm/llvm-project.git -b llvmorg-19.1.7
cd llvm-project
cmake -B build -G Ninja llvm \
  -DLLVM_ENABLE_PROJECTS="mlir" \
  -DBUILD_SHARED_LIBS=ON \
  -DMLIR_ENABLE_BINDINGS_PYTHON=ON \
  -DCMAKE_BUILD_TYPE=Release
ninja -C build
```

**2) 构建 PTOAS**:

```bash
cd PTOAS
cmake -B build -G Ninja \
  -DLLVM_DIR=$LLVM_BUILD/lib/cmake/llvm \
  -DMLIR_DIR=$LLVM_BUILD/lib/cmake/mlir \
  -DPTO_ENABLE_PYTHON_BINDING=ON \
  -Dpybind11_DIR=$(python -m pybind11 --cmakedir) \
  -DCMAKE_BUILD_TYPE=Release
ninja -C build
ninja -C build install
```

### 8.3 CMake 构建目标

```
CMakeLists.txt (root)
│
├── lib/PTO/IR/
│   └── PTOIR                    方言 IR 库
│       └── 依赖: PTOOpsIncGen (TableGen)
│
├── lib/PTO/Transforms/
│   └── PTOTransforms            变换 Pass 库 (52 源文件)
│       └── 依赖: PTOPassesIncGen, PTOIR
│
├── lib/CAPI/Dialect/
│   └── PTOCAPI                  C 语言 API 库
│       └── 依赖: PTOIR
│
├── lib/Bindings/Python/
│   └── _pto                     Python 扩展模块
│       └── 依赖: PTOIR, PTOPythonGen
│
├── tools/ptoas/
│   └── pto-opt → ptoas          主编译器可执行文件
│       └── 依赖: PTOIR, PTOTransforms, ptobc_lib
│
└── tools/ptobc/
    ├── ptobc_lib                字节码静态库 (6 源文件)
    └── ptobc                    字节码可执行文件
        └── 依赖: ptobc_lib, PTOIR
```

### 8.4 构建选项

| CMake 选项 | 默认 | 说明 |
|-----------|------|------|
| `PTO_ENABLE_PYTHON_BINDING` | OFF | 启用 Python 绑定 |
| `PTOAS_ENABLE_WERROR` | ON | 警告视为错误 |

### 8.5 构建产出

| 产出 | 路径 |
|------|------|
| ptoas 编译器 | `build/tools/ptoas/ptoas` |
| ptobc 工具 | `build/tools/ptobc/ptobc` |
| PTO IR 库 | `build/lib/libPTOIR.so` |
| 变换 Pass 库 | `build/lib/libPTOTransforms.so` |
| Python 扩展 | `build/python/mlir/_mlir_libs/_pto.cpython-*.so` |
| Python 方言 | `build/python/mlir/dialects/pto.py` |
| CMake 包 | `install/lib/cmake/PTOAS/PTOASConfig.cmake` |

### 8.6 运行环境变量

```bash
export MLIR_PYTHON_ROOT=$LLVM_BUILD/tools/mlir/python_packages/mlir_core
export PTO_PYTHON_ROOT=$PTO_INSTALL_DIR/
export PYTHONPATH=$MLIR_PYTHON_ROOT:$PTO_PYTHON_ROOT:$PYTHONPATH
export LD_LIBRARY_PATH=$LLVM_BUILD/lib:$PTO_INSTALL_DIR/lib:$LD_LIBRARY_PATH
export PATH=$PTO_BUILD/tools/ptoas:$PTO_BUILD/tools/ptobc:$PATH
```

---

## 9. 测试体系

PTOAS 采用四层测试策略：

### 9.1 LIT 回归测试

**框架**: LLVM Integrated Tester (LIT)
**路径**: `test/lit/pto/`
**数量**: 205 个 `.pto` 测试文件
**运行**: `ninja -C build check-pto`

**测试覆盖**:

| 类别 | 示例文件 | 测试内容 |
|------|---------|---------|
| 异步操作 | `async_put_get_emitc.pto` | 异步 DMA 编码 |
| 通信操作 | `comm_collective_emitc.pto` | 集合通信代码生成 |
| 布局推断 | `compact_left_blayout_parser_a3.pto` | A3 布局解析 |
| 事件 ID | `eventid_array_*.pto` | 事件 ID 管理 |
| 同步插入 | `graph_sync_solver_*.pto`, `insert_sync_*.pto` | 同步正确性 |
| 内存规划 | `infer_layout_*.pto` | 布局/内存推断 |
| 架构特定 | `*_a3.pto`, `*_a5.pto` | A3/A5 差异化行为 |

**测试格式**: 基于 `FileCheck` 的模式匹配

```
// RUN: ptoas %s --pto-arch a3 --enable-insert-sync | FileCheck %s
// CHECK: pto.set_flag
// CHECK: pto.wait_flag
```

### 9.2 ptobc 单元测试

**框架**: CTest
**路径**: `tools/ptobc/tests/`
**数量**: 12 个测试
**运行**: `ctest --test-dir build`

| 测试名 | 内容 |
|--------|------|
| `ptobc_stage9_e2e` | 端到端字节码编译 |
| `ptobc_to_ptoas_smoke` | 字节码 → ptoas 往返测试 |
| `ptobc_opcode_coverage_check` | 操作码覆盖率检查（Python） |
| `*_v0_encode` (9 个) | 各操作编码正确性 |

### 9.3 算子样例测试

**路径**: `test/samples/`
**数量**: 130 个子目录，449 个 Python 文件

每个算子样例包含三个 Python 文件：

```
test/samples/<op_name>/
├── <op_name>.py          PTO IR 生成脚本
├── <op_name>_golden.py   Golden 数据生成（NPU 执行参考）
└── <op_name>_compare.py  结果对比验证
```

**覆盖算子类别**:
- 算术: Abs, Add, Addc, Adds, And, Cmp, Mul, Sub, ...
- 向量: ColMax, ColMin, ColSum, ColProd, ...
- 矩阵: MatMul, MatmulVectorMix, ...
- 高级: FFN, FlashAttention, GQA, Qwen3DecodeA3/A5
- 内存: AllocTile, Load, Store, SubView, ...
- 通信: AsyncComm, CommSync, ...
- 控制流: ControlFlow, ...

### 9.4 NPU 硬件验证

**路径**: `test/npu_validation/` + `test/samples/*/board_validation/`
**数量**: 7 个完整硬件验证用例

每个验证用例包含：
- `CMakeLists.txt` — C++ 构建配置
- C++ kernel 源码 — 编译后的算子实现
- Launch 代码 — NPU 调用入口
- Python golden/compare 脚本 — 数据验证

**验证用例**: ControlFlow, FFN, FlashAttention, GQA, MatmulVectorMix, SubView, TInsert

**远程执行**:

```bash
# 通过 SSH 在远端 NPU 机器运行验证
bash test/npu_validation/run_remote_npu_validation.sh
```

---

## 10. CI/CD

**平台**: GitHub Actions
**路径**: `.github/workflows/`

### 工作流列表

| 工作流 | 触发条件 | 说明 |
|--------|---------|------|
| `ci.yml` | push / PR / 每日 14:00 UTC / 手动 | 主 CI 管线 |
| `build_wheel.yml` | 手动 | Linux Python wheel 构建 |
| `build_wheel_mac.yml` | 手动 | macOS Python wheel 构建 |
| `sync_base_version.yml` | — | 版本号同步 |
| `update_pto_isa_pin.yml` | — | pto-isa 依赖版本更新 |

### 主 CI 管线（ci.yml）

```
┌─────────────────────┐
│ license-header-check │  检查 CANN 开源许可证头
└──────────┬──────────┘
           ▼
┌─────────────────────┐
│   build-and-test    │  LLVM 构建 → PTOAS 编译 → LIT 测试
└─────────────────────┘
```

**CI 参数**:

| 参数 | 说明 |
|------|------|
| `validation_stage` | 验证阶段（build / run） |
| `run_mode` | 运行模式（npu / sim） |
| `soc_version` | SoC 版本（Ascend910 / A3 / A5） |
| `device_id` | 设备 ID（auto / 物理设备号） |
| `skip_cases` | 跳过的测试用例 |
| `run_only_cases` | 仅运行的测试用例 |

### CI 辅助脚本

| 脚本 | 职责 |
|------|------|
| `.github/scripts/check_license_headers.py` | 许可证头检查 |
| `.github/scripts/compute_ptoas_version.py` | 版本号计算 |
| `.github/scripts/update_ptoas_base_version.py` | 基础版本同步 |
| `.github/scripts/update_pto_isa_pin.py` | pto-isa 依赖锁定 |

---

## 11. 依赖关系图

### 11.1 外部依赖

```
  LLVM 19.1.7
  ├── LLVM Core (Support, Core)
  └── MLIR
      ├── MLIRDialect (EmitC, SCF, GPU, Affine, Arith,
      │                Tensor, ControlFlow, Bufferization, Func)
      ├── MLIRPass
      ├── MLIRParser
      ├── MLIRMlirOptMain
      └── MLIRPythonBindings
             └── pybind11 2.12.0
```

### 11.2 内部模块依赖

```
  ┌──────────────────────────────────────────────────┐
  │                    ptoas (CLI)                    │
  │  tools/ptoas/ptoas.cpp                           │
  └──┬──────────────┬──────────────┬─────────────────┘
     │              │              │
     ▼              ▼              ▼
  ┌────────┐  ┌───────────────┐  ┌──────────┐
  │ PTOIR  │  │ PTOTransforms │  │ ptobc_lib│
  │        │◄─┤  (52 files)   │  │          │
  │ 5 files│  └───────────────┘  └──────────┘
  └───┬────┘         │                 │
      │              │                 │
      ▼              ▼                 ▼
  ┌──────────────────────────────────────────────────┐
  │              MLIR / LLVM 核心库                    │
  │  MLIRDialect · MLIRPass · MLIRParser · LLVMSupport│
  └──────────────────────────────────────────────────┘

  ┌────────┐      ┌───────────┐
  │ PTOCAPI│─────►│   PTOIR   │
  │ (C API)│      └───────────┘
  └────────┘

  ┌────────────┐  ┌───────────┐
  │ _pto       │──►│   PTOIR   │
  │ (Python)   │  └───────────┘
  └────────────┘
```

### 11.3 Pass 间依赖（编译顺序）

```
  InferPTOMemScope
       │
       ▼
  InferPTOLayout
       │
       ▼
  PlanMemory
       │
       ├──► PTOInsertSync ──────────┐
       │    (传统模式)               │
       ├──► PTOGraphSyncSolver ─────┤  三选一
       │    (图求解器)               │
       └──► PTOInjectBarrierAllSync ┘
                                    │
                                    ▼
                        PTORemoveRedundantBarrier
                                    │
                                    ▼
                     PTOAssignDefaultFrontendPipeId
                                    │
                                    ▼
                      PTOLowerFrontendPipeOps
                                    │
                                    ▼
                     PTOInferValidatePipeInit
                                    │
                                    ▼
                      PTOLoweringSyncToPipe
                                    │
                                    ▼
                   PTOWrapFunctionsInSections
                                    │
                                    ▼
                   PTOResolveReservedBuffers
                                    │
                                    ▼
                  PTOMaterializeTileHandles
                                    │
                                    ▼
                      PTOViewToMemref
                                    │
                                    ▼
                       PTOToEmitC
                                    │
                                    ▼
                      C++ 源码输出
```

---

## 12. 开发指南

### 12.1 环境搭建

```bash
# 1. 克隆仓库
git clone <repo-url> PTOAS && cd PTOAS

# 2. 构建 LLVM 19.1.7（如尚未构建）
#    参见 §8.2 步骤 1

# 3. 构建 PTOAS
cmake -B build -G Ninja \
  -DLLVM_DIR=$LLVM_BUILD/lib/cmake/llvm \
  -DMLIR_DIR=$LLVM_BUILD/lib/cmake/mlir \
  -DPTO_ENABLE_PYTHON_BINDING=ON \
  -Dpybind11_DIR=$(python -m pybind11 --cmakedir) \
  -DCMAKE_BUILD_TYPE=Debug
ninja -C build

# 4. 设置环境变量（参见 §8.6）
```

### 12.2 运行测试

```bash
# LIT 回归测试
ninja -C build check-pto

# ptobc 单元测试
cd build && ctest --output-on-failure

# 单个 LIT 测试
build/bin/llvm-lit test/lit/pto/insert_sync_basic.pto -v

# 算子样例测试（需 Python 环境）
cd test/samples/Abs && python abs.py
```

### 12.3 添加新 Pass

1. **定义 Pass** — 在 `include/PTO/Transforms/Passes.td` 中添加 TableGen 定义：

```tablegen
def MyNewPass : Pass<"pto-my-new-pass", "func::FuncOp"> {
  let summary = "My new optimization pass";
  let constructor = "createMyNewPass()";
}
```

2. **声明工厂函数** — 在 `include/PTO/Transforms/Passes.h` 中添加：

```cpp
std::unique_ptr<mlir::Pass> createMyNewPass();
```

3. **实现 Pass** — 在 `lib/PTO/Transforms/` 下新建 `MyNewPass.cpp`：

```cpp
#include "PTO/Transforms/Passes.h"

namespace {
struct MyNewPass : public impl::MyNewPassBase<MyNewPass> {
  void runOnOperation() override {
    auto funcOp = getOperation();
    // ... 变换逻辑
  }
};
} // namespace

std::unique_ptr<mlir::Pass> createMyNewPass() {
  return std::make_unique<MyNewPass>();
}
```

4. **注册到 CMake** — 在 `lib/PTO/Transforms/CMakeLists.txt` 的 `SRCS` 列表中添加 `MyNewPass.cpp`

5. **编排到管线** — 在 `tools/ptoas/ptoas.cpp` 的 pass pipeline 中添加调用

6. **添加测试** — 在 `test/lit/pto/` 下添加 `.pto` 测试文件

### 12.4 代码规范

- **严格警告**: `-Werror` 默认开启，所有警告视为错误
- **命名风格**: 遵循 LLVM/MLIR 命名规范（驼峰命名）
- **许可证头**: 所有源文件必须包含 CANN Open Software License 头（CI 自动检查）
- **TableGen 优先**: 新增 Op/Type/Attr 优先使用 TableGen 定义，避免手写 C++

### 12.5 常用调试技巧

```bash
# 查看 IR 在各 Pass 后的变化
ptoas input.pto --pto-arch a3 --enable-insert-sync \
  --mlir-print-ir-after-all 2>ir_dump.txt

# 只运行特定 Pass
ptoas input.pto --pto-arch a3 \
  --pass-pipeline="builtin.module(func.func(pto-insert-sync))"

# 打印内存分配大小
ptoas input.pto --pto-arch a3 \
  --pass-pipeline="builtin.module(pto-plan-memory{enable-print-memory-allocated-size=true})"
```

---

## 13. 架构演进：跳过 CCE 的新编译路径（设计中）

> **状态**：设计文档阶段，详见 `docs/designs/ptoas-tileop-expand-design.md`。相关代码（`--pto-backend=vpto`、`--vpto-emit-hivm-llvm` 等 flag）尚未在 PTOAS 仓库落地。

### 13.1 动机

当前编译路径中，PTOAS 生成的 C++ 代码调用 pto-isa 的模板函数（如 `Add<float>`）。bisheng 编译这些 C++ 代码时需要做 **C++ 模板实例化**——这是整条编译链中最慢的环节。

新方案的目标是**跳过 C++/CCE 编译**，直接从 PTOAS 输出 LLVM IR。

### 13.2 新旧路径对比

```
当前路径（慢）：                               新路径（快，跳过 CCE）：

PTO DSL (TileLang/PyPTO)                     PTO DSL (TileLang/PyPTO)
      ↓                                            ↓
    PTOAS                                         PTOAS
      ↓ PTOToEmitC                                  ↓ Expand TileOp
C++ 源码 (调用 pto-isa)                        向量 IR (pto.vlds/vadd/vsts)
      ↓ bisheng -xcce                               ↓ VPTO 后端
  模板实例化 ← 瓶颈                              直接输出 LLVM IR
      ↓                                            ↓ bisheng -x ir
   LLVM IR                                      NPU 二进制
      ↓ bisheng
   NPU 二进制
```

### 13.3 核心机制：TileOp Expand

新路径的关键在于把 pto-isa 模板库做的事（TileOp → 向量指令展开）搬到 MLIR 层面，由 **Python 模板 + VPTO 后端** 完成。

#### TileOp 与向量指令的关系

```
TileOp（高层，一条顶一片）：
  pto.tadd ins(%a, %b) outs(%c)         ← "把整个 16×64 的 tile 做加法"

向量指令（低层，一条做一行）：
  for row in 0..16:                      ← 需要显式循环
    for col in 0..64 step 64:            ← 按向量宽度切分
      %va = pto.vlds %a[row, col]        ← 加载一个向量寄存器
      %vb = pto.vlds %b[row, col]
      %vc = pto.vadd %va, %vb, mask      ← 一次加 64 个元素
      pto.vsts %vc, %c[row, col]         ← 写回
```

#### Python 模板代替 pto-isa

使用 TileLang Python DSL 编写 `@pto.vkernel` 模板函数，在 PTOAS 编译时将 TileOp 展开为向量 IR：

```python
@pto.vkernel(target="a5", op="tadd",
             dtypes=[(pto.f32, pto.f32, pto.f32)])
def template_tadd(src0: pto.Tile, src1: pto.Tile, dst: pto.Tile):
    dtype = dst.element_type
    valid_rows, valid_cols = dst.valid_shape
    for row in range(0, valid_rows, 1):
        remained = valid_cols
        for col in range(0, valid_cols, pto.get_lanes(dtype)):
            mask, remained = pto.make_mask(dtype, remained)
            lhs = pto.vlds(src0[row, col:])
            rhs = pto.vlds(src1[row, col:])
            summed = pto.vadd(lhs, rhs, mask)
            pto.vsts(summed, dst[row, col:], mask)
```

一份模板覆盖所有 dtype × shape 组合（参数化），解决了 pto-isa C++ 模板的穷举问题。

#### 展开的三步 Pass 协作

```
pto.tadd ins(%a, %b) outs(%c)

  ↓  ① Expand TileOp
     调用 Python DSL 模板，根据 dtype/shape 特化
     替换为 func.call @__pto_tilelang_tadd_f32_16_64(%a, %b, %c)

  ↓  ② Inline
     将模板函数体内联到调用点
     tile_buf 形参绑定到实际值 %a, %b, %c

  ↓  ③ Fold TileBuf Intrinsics
     pto.tile_buf_addr  → 折叠为具体 memref
     pto.tile_valid_rows → 折叠为常量 16（静态）或 SSA 值（动态）
     pto.tile_valid_cols → 折叠为常量 64（静态）或 SSA 值（动态）

  ↓
纯向量 IR（pto.vlds / pto.vadd / pto.vsts）
```

#### VPTO 后端

展开后的向量 IR 由 VPTO 后端直接映射为 LLVM IR：

```
pto.vadd %va, %vb, %mask
    ↓ VPTO 后端（不走 EmitC/C++）
llvm.call @hivm.vadd(...)          ← 直接映射为硬件 intrinsic
    ↓ MLIR translateModuleToLLVMIR()
LLVM IR 文本 (.ll)
    ↓ bisheng -x ir                ← 直接编译 IR，无 C++ 模板开销
NPU 二进制
```

### 13.4 Python 模板 + VPTO 后端 vs pto-isa

| 职责 | 当前（pto-isa） | 新路径 |
|------|----------------|--------|
| TileOp → 向量循环展开 | C++ 模板（`Add<T>` 内部循环） | Python 模板（`@pto.vkernel`） |
| 向量操作 → 硬件 intrinsic | C++ 模板（`__builtin_vadd`） | VPTO 后端（LLVM IR intrinsic） |
| 展开发生时机 | bisheng C++ 编译期 | PTOAS MLIR 编译期 |
| 输出格式 | C++ 源码 | LLVM IR |
| 编译速度 | 慢（模板实例化） | 快（跳过 C++ 编译） |

新路径中 **pto-isa 不再需要**——其工作被拆为 Python 模板（展开逻辑）和 VPTO 后端（硬件映射），全部在 MLIR 层面完成。

### 13.5 新路径完整编译管线

```
输入：TileOp / 向量指令 / 混合
       ↓
  VF Fusion Analysis        ← TileOp 层融合分析
       ↓
  PlanMemory                ← UB 内存分配规划
       ↓
  InsertSync                ← 管线同步插入
       ↓
  Expand TileOp             ← Python 模板展开 TileOp → 向量 IR
       ↓
  Inline                    ← 模板函数体内联到调用点
       ↓
  Fold TileBuf Intrinsics   ← 折叠 tile_buf intrinsic
       ↓
  VF Fusion                 ← 合并相邻向量循环，消除中间 UB 读写
       ↓
  VPTO 后端                 ← 向量 IR → LLVM IR
       ↓
  LLVM IR                   ← 最终输出
```

---

> 本文档基于 PTOAS v0.39 (commit `b6c082d`) 生成。
