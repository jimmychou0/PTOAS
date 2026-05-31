# PTOBC v0 vs NVIDIA Tile IR Bytecode 结构对比分析

> 对比对象：PTOAS PTOBC v0 (`tools/ptobc/include/ptobc/ptobc_format.h`) vs NVIDIA CUDA Tile IR 13.2 ([Binary Format Spec](https://docs.nvidia.com/cuda/tile-ir/13.2/sections/bytecode.html))

---

## 目录

1. [定位差异](#1-定位差异) — 术语表、Driver JIT 概念
2. [文件头对比](#2-文件头对比)
3. [Section 结构对比](#3-section-结构对比) — 封装格式、类型对照表
4. [关键差距详解](#4-关键差距详解) — Function Table、Type、Global、Debug、类型vs属性角色、指令编码机制、Attribute、版本兼容性
5. [总结：PTOBC 缺少什么](#5-总结ptobc-缺少什么)
6. [Tile IR Section 间引用关系图](#6-tile-ir-section-间引用关系图)
7. [JIT 关键特性汇总](#7-jit-关键特性汇总) — 加载阶段、编译阶段、特性对照表
8. [附录 A：Section 布局可视化对比](#附录-a-section-布局可视化对比)

---

## 1. 定位差异

| | PTOBC v0 | NVIDIA Tile IR Bytecode |
|---|---|---|
| **定位** | MLIR IR 的压缩序列化格式 | 面向 driver JIT 的虚拟 ISA |
| **消费者** | ptoas 编译器（需反序列化回 MLIR 后编译） | CUDA driver 直接加载并 JIT 编译 |
| **可移植性** | 绑定目标架构（`pto.target_arch = "a3"/"a5"`） | 架构无关（同一 .tilebc 可 JIT 到 sm_100/sm_120 等） |
| **成熟度** | 内部分发格式，v0 阶段 | 正式发布的稳定规范（13.2），配套完整工具链 |

### 1.1 术语表

| 术语 | 含义 |
|------|------|
| **blob** | Binary Large Object，一大块不透明的二进制数据。只能整体读取，不能按字段访问内部结构。PTOBC 的 Module Section 就是一个 blob——52KB 的数据混在一起，无法跳到第 2 个函数 |
| **ASM 文本** | Assembly text，MLIR IR 的文本表示形式。例如 `"!pto.tile_buf<vec, 32x32xf32>"` 就是 tile_buf 类型的 ASM 文本。读回时需要调用 MLIR parser 逐字符解析才能还原为结构化数据 |
| **AoT** | Ahead-of-Time，提前编译。在部署前把源码/IR 编译为目标机器码。当前 PTOAS 的方式：`.pto → ptoas → .cpp → bisheng → NPU 二进制` |
| **JIT** | Just-in-Time，即时编译。在程序运行时才把 IR/bytecode 编译为目标机器码。Tile IR 的方式：应用调用 `cuModuleLoadData(.tilebc)`，CUDA Driver 在运行时编译为 GPU 机器码 |
| **Driver JIT** | 由硬件驱动程序内嵌的 JIT 编译器完成的即时编译。应用只需发送 bytecode，Driver 负责编译和执行 |
| **Virtual ISA** | 虚拟指令集架构。像真实 ISA（x86/ARM）一样有明确的 opcode 和编码规范，但不直接在硬件上执行，需要 JIT 编译为真实 ISA |
| **varint** | Variable-width Integer，变长整数编码（LEB128 变体）。小整数占 1 字节，大整数最多 9 字节。比固定 8 字节的 int64 节省空间 |
| **typeTag / attributeTag** | 类型/属性的标识字节。parser 读到 tag 后，按预定义格式解析后续字段 |

### 1.2 Driver JIT 概念说明

**Driver JIT 是 Tile IR bytecode 许多设计决策的根本驱动力**。理解它才能理解为什么 Tile IR 需要 Function Table、结构化类型编码、版本兼容性等特性。

```
AoT 编译（当前 PTOAS 的方式）：

  开发机                                  部署机
  ━━━━━━━━━━━━━━━━━━━━━━━━━━             ━━━━━━━━━━━━━━━
  .pto → ptoas → .cpp → bisheng → .bin    拿到 .bin 直接跑

  全部编译在部署前完成
  类比：出版社先把书印好 → 读者拿到成品书


Driver JIT（Tile IR 的方式）：

  开发机                                  部署机（运行时）
  ━━━━━━━━━━━━━━━━━━━━━━━━━━             ━━━━━━━━━━━━━━━━━━━━━━━━━━━
  .mlir → cuda-tile-translate → .tilebc    应用调用 cuModuleLoadData(.tilebc)
                                           → CUDA Driver 收到 bytecode
                                           → Driver 内部 JIT 编译为当前 GPU 的机器码
                                           → 直接执行

  类比：发一份乐谱（bytecode）→ 乐团现场演奏（JIT 编译）
```

Driver JIT 带来的核心优势：

| 优势 | 解释 |
|------|------|
| **跨架构** | 发一份 .tilebc，在 sm_100/sm_120 不同 GPU 上都能跑（Driver 适配） |
| **部署简单** | 不需要为每种 GPU 预编译一份二进制 |
| **运行时优化** | Driver 知道当前 GPU 的精确型号和配置，可做更精准的优化 |
| **版本解耦** | 编译器版本和 Driver 版本可独立升级 |

**对 bytecode 格式的要求**：Driver 是一个轻量级嵌入式编译器，不能依赖 MLIR 等重型框架。因此 bytecode 必须：① 格式公开自描述（Driver 能独立解析）；② 支持懒加载（只编译需要的函数）；③ 版本兼容（新旧 Driver 都能工作）。本文档中标注 `[JIT]` 的条目都是为满足这些要求而设计的。

---

## 2. 文件头对比

| 字段 | PTOBC v0 | Tile IR 13.2 |
|------|----------|-------------|
| Magic | `PTOBC\0`（6 字节） | `\x7FTileIR\0`（8 字节） |
| 版本 | `uint16_t version` + `uint16_t flags`（固定 4 字节） | `uint8_t major` + `uint8_t minor` + `uint16_t tag`（4 字节） |
| 长度 | `uint32_t` 整体 payload 长度 | 无整体长度（每 section 独立标记） |

**差距**：
- PTOBC 的版本只有一个 `uint16_t`（当前硬编码 `0x0000`），无 major/minor 区分
- PTOBC 用整体 payload 长度包裹所有 section；Tile IR 每个 section 独立，支持流式读取和跳过
- `[JIT]` Tile IR 的 major/minor 版本机制允许 **Driver 判断自己能否 JIT 编译这份 bytecode**——版本不匹配时 graceful fail，而不是 crash。PTOBC 无此能力。

---

## 3. Section 结构对比

### 3.1 Section 封装格式

```
PTOBC:                              Tile IR:
section {                           section {
  id: uint8_t,                        idAndIsAligned: byte,    // 低7位=ID, 高位=对齐标志
  length: uint32_t,                   length: varint,
  data: byte[length]                  alignment: varint?,      // 可选对齐
}                                     padding: byte[],         // 0xCB 填充
                                      data: byte[]
                                    }
```

**差距**：Tile IR 支持 **section 级对齐**（alignment + padding），有利于内存映射和零拷贝访问。PTOBC 不支持。

`[JIT]` Section 对齐使 Driver 可以 **直接 mmap bytecode 文件**，section 数据天然对齐到硬件要求的边界，无需拷贝到对齐的缓冲区。这对 JIT 加载性能至关重要。

### 3.2 Section 类型完整对照

| Section ID | PTOBC v0 | Tile IR 13.2 | 差距 |
|------------|----------|-------------|------|
| **Strings** | `0x01` — LEB128 顺序编码 | `0x01` — 偏移索引 + blob | Tile IR 支持**随机访问**（`stringStartIndex[i]`），PTOBC 需顺序扫描 |
| **Function Table** | (无) | `0x02` — 函数级粒度编码 | **PTOBC 完全缺失**，见 §4.1 |
| **Debug** | `0x07` — 4 张平面表 | `0x03` — 多级间接 + DWARF 风格条目 | Tile IR 远更丰富，见 §4.4 |
| **Constant Data** | `0x04` — tag + payload | `0x04` — 偏移索引 + blob | Tile IR 支持随机访问（`constantStartIndex[i]`），PTOBC 需顺序扫描 |
| **Type** | `0x02` — 存 ASM 文本字符串 | `0x05` — 结构化 typeTag 编码 | **核心差距**，见 §4.2 |
| **Global** | (无) | `0x06` — 全局变量独立 section | **PTOBC 缺失**，见 §4.3 |
| **Attrs** | `0x03` — 存 ASM 文本字符串 | (内联在指令/类型中) | PTOBC 用文本；Tile IR 结构化编码 |
| **OpcodeSchema** | `0x05` — 扩展操作码 | (内建 opcode + 版本化) | 功能类似 |
| **Module** | `0x06` — 整个 MLIR 的不透明 blob | (无对应，由 Function Table 替代) | **PTOBC 的根本问题**，见 §4.1 |
| **Extra** | `0x7F` — 扩展元数据 | (未知 section 可跳过) | 功能类似 |
| **End marker** | (无) | `0x00` — 字节码结束标记 | PTOBC 依赖 payload 长度判定结束 |

---

## 4. 关键差距详解

### 4.1 Function Table — PTOBC 最大的结构缺失

**PTOBC 现状**：整个 MLIR module 序列化为一个 **不透明的二进制 blob**（Section `0x06` Module），无法按函数查找、无法懒加载。

**Tile IR 方案**：

```
functionTable {
  numFunctions : varint
  // 每个函数独立编码：
  nameIndex[i] : varint         // 函数名（引用 String Section）
  signatureIndex[i] : varint    // 函数签名（引用 Type Section，可共享）
  entryFlag[i] : byte           // Bit0: Public/Private
                                // Bit1: Device Func / Kernel Entry
                                // Bit2: 是否有 OptimizationHints
  functionLocIndex[i] : varint  // Debug 位置（引用 Debug Section）
  optimizationHints[i]? : ...   // 可选优化提示
  lengthOfFunction[i] : varint  // 函数体字节数
  functionBody[i] : byte[]      // 指令流（可跳过或懒解析）
}
```

**带来的能力差距**：

| 能力 | PTOBC | Tile IR |
|------|-------|---------|
| 按函数名查找 | 必须反序列化整个 module | 扫描函数表即可 | `[JIT]` Driver 需要按名字找到 kernel entry 来启动执行 |
| 懒加载（只解析需要的函数） | 不可能 | `lengthOfFunction` 支持跳过函数体 | `[JIT]` Driver 只需编译被调用的函数，跳过其余 |
| 区分 kernel / device function | 无 | `entryFlag` Bit1 | `[JIT]` Driver 必须知道哪些函数是 kernel entry point（可被 host 启动） |
| 函数可见性（public/private） | 无 | `entryFlag` Bit0 | `[JIT]` Driver 只暴露 public 函数给 host API |
| 函数签名共享 | 无 | `signatureIndex` 指向 Type Section | |
| 函数级优化提示 | 无 | `optimizationHints` 字段 | `[JIT]` Driver JIT 编译器利用 hints 做目标架构特定优化 |
| 函数级 debug 定位 | 无 | `functionLocIndex` | |

#### 为什么 ELF 不需要 Function Table 而 Tile IR 需要

熟悉 ELF 的读者可能会问：ELF 只有 `.symtab`（符号表）+ `.text`（代码段），为什么不需要 Function Table？

根本区别在于 **ELF 里的代码是"成品"（机器码），Tile IR 里的代码是"半成品"（IR 指令）**：

```
ELF（成品）：
  .text 里是机器码 → OS loader mmap → CPU 直接执行
  .symtab 只需要 (名字, 偏移, 大小) → 够了

Tile IR（半成品）：
  functionBody 里是 IR 指令 → Driver 必须 JIT 编译 → 才能执行
  Driver 需要更多信息来完成编译：
    签名（参数类型）→ 决定调用约定和寄存器分配
    entryFlag       → 知道哪些函数可被 host 启动
    优化提示        → 指导 JIT 编译器做目标架构特定优化
    函数体长度      → 支持跳过不需要编译的函数
```

Tile IR 的 Function Table ≈ ELF 的 `.symtab` + `.text` + 额外编译元数据的合体，但本质不同：ELF 的 `.text` 存的是最终机器码，Function Table 的 `functionBody` 存的是还需要再编译一轮的 IR 指令流。

### 4.2 Type Section — 结构化 vs 文本

**PTOBC 现状**：类型存储为 ASM 文本字符串，引用 String Section：

```
// PTOBC Type Section
numTypes: ULEB128
for each type:
  tag: 0x00 (opaque)
  flags: 0x01 (has_asm)
  stringId: ULEB128   → 指向字符串池中的 "!pto.tile_buf<vec, 32x32xf32>"
```

反序列化时需要**重新 parse 文本**，慢且脆弱。

**Tile IR 方案**：类型以结构化 typeTag 编码：

```
// Tile IR Type Section — 以 Tile Type 为例
typeTag: 0x0D          // Tile type
elementTypeIndex: varint   // 引用另一个类型条目（如 F32 = 0x07）
shape: int64_t[]       // [32, 32]
```

**差距**：

| 对比项 | PTOBC | Tile IR |
|--------|-------|---------|
| 编码方式 | ASM 文本（`"!pto.tile_buf<vec, 32x32xf32>"`） | 结构化二进制（typeTag + 字段） |
| 解析速度 | 需要 MLIR parser 重新 parse | 直接读取字段 |
| 类型引用 | 无（每个类型独立存储文本） | typeIndex 交叉引用（如 Tile 引用 F32） |
| 随机访问 | 不支持 | `typeStartIndex[i]` 偏移数组 |
| 覆盖类型数 | 不区分（全部 opaque） | 18 种 typeTag（I1-I64, F16-F64, Ptr, Tile, TensorView, PartitionView, Func, Token） |
| 前向兼容 | 无 | 新增 typeTag 旧 parser 可跳过 |

### 4.3 Global Section — PTOBC 缺失

**PTOBC**：无独立全局变量 section，全局数据混在 Module blob 里。

**Tile IR**：

```
global {
  numGlobals: varint
  symbolNameIndex[i] : varint       // 变量名
  valueTypeIndex[i] : varint        // 类型
  constantValueIndex[i] : varint    // 初始值（引用 Constant Section）
}
```

对于包含全局常量（如权重、查找表）的 kernel，Global Section 允许 driver 在 JIT 前提前分配和初始化全局内存。

`[JIT]` Driver 加载 bytecode 时，先扫描 Global Section 分配设备内存并初始化全局变量，再编译函数体。这种分离式设计使 Driver 可以在 JIT 编译前就完成内存准备，kernel 启动时全局变量已就绪。

### 4.4 Debug Section — 深度差距

**PTOBC Debug**（4 张平面表）：

```
DebugFileEntry:      pathSid, hashKind, hashBytes
DebugValueNameEntry: funcId, valueId, nameSid
DebugLocationEntry:  funcId, opId, fileId, sl, sc, el, ec
DebugSnippetEntry:   funcId, opId, snippetSid
```

**Tile IR Debug**（DWARF 风格层次结构）：

```
多级间接架构：
  Op → diIndexOffsets → diIndices → diOffsets → diData

Debug 条目类型：
  0x00  Unknown
  0x01  DICompileUnit  (language, producer, optimized, emissionKind)
  0x02  DIFile         (filename, directory)
  0x03  DILexicalBlock (line, column, scope)
  0x04  DILoc          (line, column, scope, inlinedAt)
  0x05  DISubprogram   (name, linkageName, file, line, type, flags, unit)
  0x06  CallSite       (callee, caller)
```

**差距**：

| 能力 | PTOBC | Tile IR |
|------|-------|---------|
| 编译单元信息 | 无 | DICompileUnit（language, producer） |
| 内联追踪 | 无 | DILoc.inlinedAt → CallSite 链 |
| 词法作用域 | 无 | DILexicalBlock + DISubprogram |
| 逐指令绑定 | 独立表（funcId+opId 查找） | locationIndex 嵌入每条指令 |
| linkage name | 无 | DISubprogram.linkageName |
| 优化标记 | 无 | DICompileUnit.optimized |

### 4.5 类型编码 vs 属性编码 — 各自的角色

在理解指令编码之前，先理解 bytecode 中一条指令的完整信息由四部分组成：

```
一条指令 = opcode + 类型 + 属性 + 操作数

  opcode:  做什么操作（load / add / matmul）
  类型:    数据长什么样                     → "食材是什么"
  属性:    操作怎么配置                     → "怎么做、火候多大"
  操作数:  操作哪些值                       → "哪几份食材"
```

**类型编码**回答"数据长什么样"——形状、精度、内存布局：

```
Tile<f16, 32, 64> 告诉 JIT 编译器：
  - 32行×64列 的数据块
  - 每个元素是 f16（2字节）
  - 总大小: 32 × 64 × 2 = 4096 字节
  → JIT 据此决定分配多大寄存器/共享内存、用什么硬件指令
```

**属性编码**回答"操作怎么配置"——舍入模式、缓存策略、优化偏好：

```
cuda_tile.load_view_tko 的属性：
  cache_hint = L2_PERSISTENT   → 告诉硬件缓存策略
  latency = high               → 告诉 JIT 这个 load 延迟高，可提前发射
  partition = DivBy{along=0}   → 沿第 0 维分块加载
```

**类型 vs 属性的关键区别**：

| | 类型 | 属性 |
|---|---|---|
| 回答 | 数据长什么样 | 操作怎么配置 |
| 作用域 | 全局共享（多个值/指令引用同一类型） | 局部（只属于当前这条指令） |
| 存在哪 | Type Section（集中存储，通过 typeIndex 引用） | 内联在指令流中 |
| 举例 | `Tile<f16, 32, 64>` | `rounding=nearest`, `latency=low` |

### 4.6 Operation Encoding — 指令级编码（详细机制）

**PTOBC**：无独立指令编码层，操作混在 Module blob 中，外部工具无法解析。

**Tile IR**：每条指令自描述，存储在 Function Table 的 `functionBody` 中。

#### 基本结构

```
instruction {
  opcode : byte                    // 1 字节：做什么
  locationIndex : varint           // debug 位置（0=无）
  [flags : varint]                 // 可选：哪些可选字段存在
  [attributes + operands + ...]    // opcode 决定后续字段的格式
}
```

#### 核心机制：opcode 决定字段布局

**每个 opcode 有自己固定的字段布局定义**，parser 根据 opcode 决定后面怎么读。不同 opcode 的字段完全不同：

```
opcode = 0x15 (add):
  parser 知道：两个操作数，无可选字段，结果类型从操作数推断
  读取：[locationIndex] [lhs] [rhs]

opcode = 0x20 (load_view_tko):
  parser 知道：有 resultType、flags、多个可选属性、一个操作数
  读取：[locationIndex] [resultType] [flags] [可选字段...] [address]

opcode = 0x30 (if):
  parser 知道：有 condition、结果类型列表、两个 region（递归嵌套）
  读取：[locationIndex] [condition] [numResults] [resultTypes...]
        [then region] [else region]
```

#### flags bitfield 控制可选字段

对于有可选属性的指令，用 flags 的每个 bit 表示对应字段是否存在——**bit=0 则该字段完全不占字节**：

```
load_view_tko 的规范定义：
  字段 #1: resultType       — 必须
  字段 #2: flags            — 必须（bitfield）
  字段 #3: cache_hint       — 可选（对应 flags bit0）
  字段 #4: partition        — 可选（对应 flags bit1）
  字段 #5: opt_hints        — 可选（对应 flags bit2）
  字段 #6: address operand  — 必须

parser 读到 flags = 0b00000101 时：
  bit0 = 1 → cache_hint:   存在，读取它
  bit1 = 0 → partition:    不存在，跳过（不读任何字节）
  bit2 = 1 → opt_hints:    存在，读取它
```

#### SSA 值编号：不存储，自动递增

Tile IR **不显式存储每条指令的结果编号**。parser 按顺序扫描指令，遇到有结果的指令自动分配编号：

```
functionBody 中的指令流：

  inst[0]: const 1.0          → 产出 %0（自动）
  inst[1]: const 2.0          → 产出 %1（自动）
  inst[2]: load %view         → 产出 %2（自动）
  inst[3]: load %view2        → 产出 %3（自动）
  inst[4]: add %2, %3         → 产出 %4（自动，不写入字节流）
  inst[5]: store %4, %out     → 无产出

后续指令用 operandIndex（如 %2, %3）引用之前的结果。
```

#### Region 嵌套（控制流）

if/for 等控制流指令包含 Region，Region 内部递归包含完整的指令流：

```
cuda_tile.if %cond:
  opcode = 0x30
  condition = %9
  numResults = 2
  resultTypes = [Type#3, Type#7]
  then region:                     ← 递归嵌套
    block[0]: 3 条指令
      [instruction] [instruction] [instruction]
  else region:                     ← 递归嵌套
    block[0]: 2 条指令
      [instruction] [instruction]
```

#### Parser 实现模式

parser 的核心是一个 **根据 opcode 分发的 switch**：

```cpp
// 伪代码
while (offset < functionBody.end()) {
    uint8_t opcode = readByte();
    uint64_t locIdx = readVarInt();

    switch (opcode) {
    case 0x15: {  // add
        auto lhs = readVarInt();
        auto rhs = readVarInt();
        break;
    }
    case 0x20: {  // load_view_tko
        auto resultType = readVarInt();
        auto flags = readVarInt();
        if (flags & 0x01) cache_hint = readVarInt();
        if (flags & 0x02) partition = readPartition();
        if (flags & 0x04) opt_hints = readSelfContained();
        auto address = readVarInt();
        break;
    }
    case 0x30: {  // if
        auto cond = readVarInt();
        auto thenRegion = readRegion();  // 递归
        auto elseRegion = readRegion();
        break;
    }
    default:
        skipUnknownOp();  // 前向兼容：跳过未知 opcode
    }
}
```

#### 与 PTOBC 的本质差异

```
Tile IR:  opcode 是公开契约（Virtual ISA）
          parser 看到 0x15 就知道后面是 [lhs, rhs]
          看到 0x20 就知道后面是 [resultType, flags, ...]
          任何人都能写 parser

PTOBC:    指令混在 Module blob 里
          编码格式是 MLIR 内部实现细节
          只有 MLIR 的 deserializer 能解析
          外部工具（如 Driver）无法直接读取
```

`[JIT]` 结构化指令编码是 Driver JIT 的核心前提——Driver 内嵌的 JIT 编译器需要**直接遍历指令流**进行寄存器分配和指令选择，不可能先重建完整的 MLIR Module 再编译。每条指令必须自描述（opcode + operands + attrs），Driver 才能流式处理。

### 4.7 Attribute Encoding — PTOBC 缺失

**PTOBC**：属性存为 ASM 文本字符串（与 Types 相同问题）。

**Tile IR**：12 种结构化 attributeTag，按二进制编码，无需文本解析：

| Tag | 名称 | 用途 |
|-----|------|------|
| `0x01` | IntegerAttr | 整数常量 |
| `0x02` | FloatAttr | 浮点常量 |
| `0x03` | BoolAttr | 布尔值 |
| `0x04` | TypeAttr | 类型引用 |
| `0x05` | StringAttr | 字符串引用 |
| `0x06` | ArrayAttr | 属性数组 |
| `0x07` | DenseElementsAttr | 稠密张量常量 |
| `0x08` | DivByAttr | 除法约束 |
| `0x09` | SameElementsAttr | 同值张量 |
| `0x0A` | DictionaryAttr | 键值对字典 |
| `0x0B` | OptimizationHintsAttr | 优化提示 `[JIT]` Driver JIT 编译器读取 latency 等 hints 做目标架构特定调度 |
| `0x0C` | NonNegativeAttr | 非负约束 `[JIT]` 告知 Driver 值域范围，可用于强度削减优化 |

### 4.7 版本兼容性

**PTOBC**：硬编码 `kVersionV0 = 0x0000`，无任何兼容性机制。格式变更将 break 所有已有文件。

**Tile IR**：完整的四层兼容性保证：

1. **后向兼容**：新 parser 必须能读旧 bytecode `[JIT]` 新 Driver 能运行旧编译器产出的 bytecode
2. **前向兼容**：旧 parser 遇到未知 section/opcode 可跳过或报错 `[JIT]` 旧 Driver 对新 bytecode 给出清晰错误而非 crash
3. **版本定向**：serializer 可指定输出旧版本格式 `[JIT]` 编译器可为旧 Driver 生成兼容 bytecode
4. **可选 vs 必须**：新特性明确标记为 optional 或 required `[JIT]` Driver 升级不 break 已部署的 bytecode

---

## 5. 总结：PTOBC 缺少什么

### 缺失的 Sections

| 缺失项 | 影响 | 建议优先级 |
|--------|------|-----------|
| **Function Table Section** | 无法懒加载、无法按函数查找、无 kernel/device 区分 | P0（最关键） |
| **Global Section** | 全局变量混在 Module blob 中 | P1 |
| **结构化 Type Section** | 类型依赖文本 parse，慢且脆弱 | P1 |
| **结构化 Attribute Encoding** | 属性依赖文本 parse | P1 |
| **结构化 Operation Encoding** | 指令流不可独立解析 | P1 |
| **End-of-Bytecode Marker** | 依赖 payload 长度 | P2 |

### 缺失的能力

| 缺失能力 | Tile IR 如何实现 | 建议优先级 |
|----------|-----------------|-----------|
| **版本兼容性** | major/minor + 四层兼容策略 | P0 |
| **Section 对齐** | idAndIsAligned 高位 + alignment + padding | P1 |
| **随机访问**（string/type/const） | 偏移索引数组（`startIndex[i]`） | P1 |
| **逐指令 Debug 绑定** | `locationIndex` 嵌入每条指令 | P1 |
| **DWARF 风格 Debug** | DICompileUnit/DIFile/DISubprogram/DILoc/CallSite | P2 |
| **优化提示** | OptimizationHintsAttr + 函数级 hints | P2 |
| **架构无关可移植性** | 由 driver JIT 到目标架构 | P2（需架构重新设计） |
| **Driver 级集成** | CUDA driver 直接加载 .tilebc | P3（需生态配合） |

### 根本性架构差距

PTOBC 的 Module Section（`0x06`）把整个 MLIR module 当作**一个不透明 blob** 存储，这是大部分问题的根源。要达到 Tile IR 的水平，需要将 Module blob **拆解为独立的 Function Table + 结构化 Type + 结构化指令编码**，这是一个架构级重构。

---

## 6. Tile IR Section 间引用关系图

Tile IR 的各 Section 通过索引（varint）互相引用，形成有向无环的依赖图：

```
                      String Section (0x01)
                     ▲  ▲  ▲  ▲
                     │  │  │  │
      ┌──────────────┘  │  │  └──────────────┐
      │     ┌───────────┘  └──────────┐      │
      │     │                         │      │
 Function Table (0x02)    Global (0x06)    Debug (0x03)
      │         │              │  │
      │         │              │  │
      │         ▼              │  ▼
      │    Type Section (0x05) ◄──┘
      │     ▲  │  ▲
      │     │  │  │ (自引用: Ptr→pointeeType, Tile→elementType,
      │     │  ▼  │          Func→paramTypes/resultTypes)
      │     │  Type Section (内部)
      │     │
      │     └──────────────────────────────┐
      │                                    │
      ▼                                    ▼
  指令流 (在 functionBody 内)       Constant Data (0x04)
      │                               ▲
      │   operands: varint (SSA编号)   │
      │   attrs: DenseElementsAttr ───┘  (constantIndex 引用)
      │   resultType ─────────────► Type Section
      │   locationIndex ──────────► Debug Section
      │
      └──► 其他指令（operandIndex 是函数内的 SSA 值编号）
```

**引用方向汇总**：

| 源 Section | 被引用 Section | 引用字段 |
|-----------|---------------|---------|
| Function Table | String | `nameIndex` (函数名) |
| Function Table | Type | `signatureIndex` (函数签名) |
| Function Table | Debug | `functionLocIndex` (函数定义位置) |
| Global | String | `symbolNameIndex` (变量名) |
| Global | Type | `valueTypeIndex` (变量类型) |
| Global | Constant Data | `constantValueIndex` (初始值) |
| Debug | String | DIFile.filename, DISubprogram.name 等 |
| 指令流 | Type | `resultType`, 属性中的 `typeIndex` |
| 指令流 | Debug | `locationIndex` (每条指令的源码位置) |
| 指令流 | Constant Data | DenseElementsAttr.`constantIndex` |
| Type (内部) | Type | Ptr.`pointeeTypeIndex`, Tile.`elementTypeIndex`, Func.`paramTypeIndices` |

**reader 的处理顺序**：先发现所有 section（存储 payload），再按依赖序处理——String 最先（被所有人引用），Type 次之，然后是 Constant/Debug/Global，最后是 Function Table（引用所有其他 section）。

---

## 7. JIT 关键特性汇总

以下汇总 Tile IR bytecode 中**专为支持 Driver JIT 而设计的特性**，以及 PTOBC 的对应现状：

### 7.1 JIT 加载阶段

Driver 调用 `cuModuleLoadData(.tilebc)` 时的处理流程，以及每个步骤依赖的 bytecode 特性：

```
cuModuleLoadData(.tilebc)
  │
  ├── ① 验证 Magic + Version
  │     [JIT] 版本兼容性 → 决定能否处理这份 bytecode
  │     PTOBC: ❌ 无兼容性机制
  │
  ├── ② 发现所有 Section（按 ID + length 跳过不需要的）
  │     [JIT] Section 对齐 → 支持 mmap 零拷贝加载
  │     PTOBC: ❌ 无 section 对齐
  │
  ├── ③ 解析 String Section
  │     [JIT] 偏移索引 → 按名字随机查找
  │     PTOBC: ⚠️ 需顺序扫描
  │
  ├── ④ 解析 Type Section
  │     [JIT] 结构化 typeTag → 直接读取字段，无需文本解析
  │     PTOBC: ❌ ASM 文本，需要 MLIR parser
  │
  ├── ⑤ 解析 Global Section → 分配设备内存 + 初始化全局变量
  │     [JIT] 独立 section → JIT 编译前就完成内存准备
  │     PTOBC: ❌ 无 Global Section
  │
  └── ⑥ 解析 Function Table → 注册可用 kernel
        [JIT] entryFlag → 识别 kernel entry point
        PTOBC: ❌ 无 Function Table
```

### 7.2 JIT 编译阶段

应用调用 `cuModuleGetFunction("kernel_name")` 时触发 JIT 编译：

```
cuModuleGetFunction("matmul_kernel")
  │
  ├── ① 按名字查找函数
  │     [JIT] Function Table 的 nameIndex → O(N) 扫描或建索引
  │     PTOBC: ❌ 必须反序列化整个 Module blob
  │
  ├── ② 读取函数签名
  │     [JIT] signatureIndex → Type Section → 参数/返回类型
  │     用于验证 launch 参数和生成调用约定
  │     PTOBC: ❌ 无函数级签名
  │
  ├── ③ 读取优化提示
  │     [JIT] optimizationHints → latency/throughput 偏好
  │     JIT 编译器据此做指令调度、寄存器分配决策
  │     PTOBC: ❌ 无优化提示
  │
  ├── ④ 解析 functionBody 指令流
  │     [JIT] opcode → 自描述字段布局 → 流式解析
  │     JIT 编译器遍历指令做降级 + 寄存器分配
  │     PTOBC: ❌ blob 无法流式解析
  │
  ├── ⑤ 跳过不需要的函数
  │     [JIT] lengthOfFunction → 跳过 N 字节即可
  │     PTOBC: ❌ 不可能（blob 是整体）
  │
  └── ⑥ 输出目标架构机器码
        [JIT] bytecode 架构无关 → 同一份 JIT 到不同 GPU
        PTOBC: ❌ 编译时已绑定 A3/A5
```

### 7.3 特性对照表

| 特性 | Tile IR | PTOBC | JIT 作用 |
|------|---------|-------|---------|
| 版本兼容（major/minor） | ✅ | ❌ | Driver 判断能否处理 bytecode |
| Section 对齐 | ✅ | ❌ | mmap 零拷贝加载 |
| 随机访问索引（string/type/const） | ✅ | ❌ | 按需查找，避免全量解析 |
| Function Table（函数级粒度） | ✅ | ❌ | 按名查找 kernel、懒加载、跳过不需要的函数 |
| entryFlag（kernel/device/public/private） | ✅ | ❌ | 识别 kernel entry、控制 API 可见性 |
| 结构化 Type Section | ✅ | ❌ | JIT 编译器直接读取类型信息做寄存器分配 |
| 结构化指令编码（opcode + fields） | ✅ | ❌ | JIT 编译器流式遍历指令流 |
| flags bitfield（可选字段） | ✅ | ❌ | 紧凑编码，减少 JIT 解析开销 |
| 优化提示（OptimizationHints） | ✅ | ❌ | JIT 编译器做目标架构特定调度 |
| 逐指令 Debug 绑定 | ✅ | ❌ | 运行时 profiler 关联源码行号 |
| Global Section | ✅ | ❌ | JIT 前预分配设备内存 |
| 架构无关 | ✅ | ❌ | 同一 bytecode JIT 到不同 GPU |
| End marker | ✅ | ❌ | Driver 确认 bytecode 完整性 |

---

## 附录 A：Section 布局可视化对比

```
NVIDIA Tile IR 13.2                      PTOBC v0
━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━          ━━━━━━━━━━━━━━━━━━━━━━━━━━━━
┌─────────────────────────────┐          ┌─────────────────────────────┐
│ \x7FTileIR\0 + ver 13.2    │          │ PTOBC\0 + v0 + flags       │
├─────────────────────────────┤          │ + uint32 payload_length     │
│ Global Section (0x06)       │          ├─────────────────────────────┤
│  globals: name,type,init    │          │ Strings (0x01)              │
├─────────────────────────────┤          │  ULEB128 顺序编码           │
│ Function Table (0x02)       │          ├─────────────────────────────┤
│ ├─ func[0]: name, sig,     │          │ Types (0x02)                │
│ │  entryFlag, hints, body   │          │  ASM 文本引用               │
│ ├─ func[1]: ...             │          ├─────────────────────────────┤
│ └─ func[N]: ...             │          │ Attrs (0x03)                │
├─────────────────────────────┤          │  ASM 文本引用               │
│ Constant Data (0x04)        │          ├─────────────────────────────┤
│  偏移索引 + blob             │          │ ConstPool (0x04)            │
├─────────────────────────────┤          │  tag + payload 顺序编码     │
│ Debug (0x03)                │          ├─────────────────────────────┤
│  DWARF 风格多级间接          │          │ OpcodeSchema (0x05)         │
│  DICompileUnit/DIFile/      │          ├─────────────────────────────┤
│  DISubprogram/DILoc/        │          │ Module (0x06) ★             │
│  CallSite                   │          │  不透明 MLIR blob            │
├─────────────────────────────┤          │  (函数/类型/指令全部混合)     │
│ Type Section (0x05)         │          ├─────────────────────────────┤
│  结构化: typeTag + payload   │          │ DebugInfo (0x07)            │
│  18 种 typeTag              │          │  4 张平面表                  │
│  偏移索引支持随机访问         │          ├─────────────────────────────┤
├─────────────────────────────┤          │ Extra (0x7F)                │
│ String Section (0x01)       │          └─────────────────────────────┘
│  偏移索引 + UTF-8 blob       │
├─────────────────────────────┤          缺失：
│ End marker (0x00)           │          ❌ Function Table（函数级粒度）
└─────────────────────────────┘          ❌ Global Section（全局变量）
                                         ❌ 结构化 Type/Attr 编码
设计目标：                                ❌ 结构化 Operation 编码
✅ 架构无关 JIT                           ❌ 版本兼容性策略
✅ 懒加载（按函数跳过）                    ❌ Section 对齐
✅ 前向/后向兼容                           ❌ 随机访问索引
✅ Driver 直接加载                         ❌ DWARF 风格 Debug
✅ 12 种结构化属性编码                     ❌ 优化提示
✅ 逐指令 Debug 绑定                      ❌ 逐指令 Debug
✅ 18 种结构化类型编码                     ❌ End marker
```

---

> 参考：[NVIDIA Tile IR 13.2 Binary Format](https://docs.nvidia.com/cuda/tile-ir/13.2/sections/bytecode.html) | [NVIDIA/cuda-tile GitHub](https://github.com/NVIDIA/cuda-tile)
