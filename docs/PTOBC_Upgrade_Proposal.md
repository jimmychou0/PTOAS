# PTOBC 二进制格式升级方案

> 本文分析 PTOBC v0 与 NVIDIA Tile IR Bytecode 13.2 的结构差距，并提出 PTOBC v1 的改进方案。
>
> 面向对象：PTOAS 开发团队、ptobc 工具维护者

---

## 核心结论

**PTOBC v0 与 Tile IR 的最大差距是缺少 Function Table Section。**

PTOBC v0 将整个 MLIR Module 存为一个不透明的二进制 blob（Module Section 0x06），这是大部分问题的根源。Function Table 不是一个孤立的缺失——它的缺失导致了一系列连锁后果：

```
根因：没有 Function Table
  │
  ├── 所以需要 Module blob 装所有东西
  │     ├── 指令没有独立编码（混在 blob 里）
  │     ├── 类型/属性只能存 ASM 文本（依赖 MLIR 内部格式）
  │     └── 无法懒加载、无法按函数查找
  │
  ├── 所以没有 entryFlag（无处标记 kernel vs device function）
  ├── 所以没有函数签名共享（无处引用 Type Section）
  └── 所以没有函数级 debug / 优化提示（无处挂载）
```

文档中列举的 10 项差距中，**6 项可被 Function Table 直接解决**，剩余 4 项（结构化 Type/Attr 编码、Global Section、Debug 升级）是独立问题，但 blob 消失后自然有了改进的基础。

**因此，升级方案的 Phase 1（P0 优先级）聚焦于一件事：用 Function Table 替代 Module blob。** 这一步完成后，其余改进可在此基础上逐步迭代。

---

## 1. 背景

### 1.1 PTOBC v0 的现状

PTOBC v0 是 PTOAS 编译器的二进制序列化格式，用于将 PTO IR（MLIR 文本）压缩为紧凑的二进制文件，便于分发和传输。

当前文件结构：

```
┌──────────────────────────────┐
│ Magic: "PTOBC\0" (6 bytes)   │
│ Version: uint16 (0x0000)     │
│ Flags: uint16 (0x0000)       │
│ PayloadLength: uint32        │
├──────────────────────────────┤
│ Strings      (0x01)          │  LEB128 顺序编码的字符串池
├──────────────────────────────┤
│ Types        (0x02)          │  类型的 ASM 文本引用
├──────────────────────────────┤
│ Attrs        (0x03)          │  属性的 ASM 文本引用
├──────────────────────────────┤
│ ConstPool    (0x04)          │  tag + payload 顺序编码
├──────────────────────────────┤
│ OpcodeSchema (0x05)          │  扩展操作码
├──────────────────────────────┤
│ Module       (0x06) ★        │  整个 MLIR Module 的不透明 blob
├──────────────────────────────┤
│ DebugInfo    (0x07)          │  可选，4 张平面表
├──────────────────────────────┤
│ Extra        (0x7F)          │  扩展元数据
└──────────────────────────────┘
```

### 1.2 对标：NVIDIA Tile IR Bytecode 13.2

NVIDIA 在 CUDA Toolkit 13.1 中发布了 Tile IR，一个面向 GPU Tile 编程的 MLIR 方言 + 字节码格式。其 bytecode 格式（[规范](https://docs.nvidia.com/cuda/tile-ir/13.2/sections/bytecode.html)）被 CUDA Driver 直接加载并 JIT 编译，设计成熟度远超 PTOBC v0。

Tile IR bytecode 的核心特点：
- **函数级粒度**：Function Table Section 支持按名查找、懒加载、跳过不需要的函数
- **结构化编码**：类型（18 种 typeTag）、属性（12 种 attributeTag）、指令（opcode + fields）全部二进制编码
- **版本兼容**：major/minor 版本号 + 四层兼容性保证（前向/后向/版本定向/可选vs必须）
- **Driver JIT 友好**：Section 对齐、随机访问索引、entryFlag 区分 kernel/device function

---

## 2. 差距总览

### 2.1 一图看差距

```
NVIDIA Tile IR 13.2                      PTOBC v0 现状
━━━━━━━━━━━━━━━━━━━━━━━━━              ━━━━━━━━━━━━━━━━━━━━━━━━━
┌───────────────────────┐              ┌───────────────────────┐
│ \x7FTileIR\0          │              │ PTOBC\0               │
│ ver: 13.2 (major+minor)│             │ ver: 0x0000 (单值)    │
├───────────────────────┤              ├───────────────────────┤
│ Global Section (0x06) │              │ Strings (0x01) 顺序   │
│  name, type, initValue│              ├───────────────────────┤
├───────────────────────┤              │ Types (0x02) ASM文本  │
│ Function Table (0x02) │              ├───────────────────────┤
│  name, sig, entryFlag │              │ Attrs (0x03) ASM文本  │
│  hints, length, body  │              ├───────────────────────┤
├───────────────────────┤              │ ConstPool (0x04) 顺序 │
│ Constant Data (0x04)  │              ├───────────────────────┤
│  偏移索引 + blob       │              │ OpcodeSchema (0x05)   │
├───────────────────────┤              ├───────────────────────┤
│ Debug (0x03) DWARF    │              │ Module (0x06) ★★★     │
│  DICompileUnit/DIFile │              │  不透明 MLIR blob      │
│  DISubprogram/DILoc   │              │  (全部混在一起)        │
├───────────────────────┤              ├───────────────────────┤
│ Type Section (0x05)   │              │ DebugInfo (0x07) 平面  │
│  结构化 typeTag        │              ├───────────────────────┤
├───────────────────────┤              │ Extra (0x7F)          │
│ String Section (0x01) │              └───────────────────────┘
│  偏移索引 + blob       │
├───────────────────────┤              ★★★ Module blob 是
│ End marker (0x00)     │              所有问题的根源
└───────────────────────┘
```

### 2.2 差距分类

| 类别 | 具体差距 | 影响 | 优先级 |
|------|---------|------|--------|
| **结构性** | Module blob 不可分解 | 无法懒加载、无法按函数查找 | P0 |
| **结构性** | 无 Function Table | 无函数级元数据（签名、entry flag） | P0 |
| **编码** | 类型/属性存为 ASM 文本 | 反序列化需文本解析，慢且脆弱 | P1 |
| **编码** | 无结构化指令编码 | 外部工具无法独立解析指令流 | P1 |
| **兼容性** | 无版本兼容机制 | 格式变更 break 所有旧文件 | P0 |
| **索引** | String/Type/Const 无随机访问 | 必须顺序扫描 | P1 |
| **元数据** | 无 Global Section | 全局变量信息混在 blob 中 | P2 |
| **Debug** | 平面表，无作用域层次 | 无法追踪内联、无编译单元信息 | P2 |
| **格式** | Section 无对齐 | 不支持 mmap 零拷贝 | P2 |
| **格式** | 无 End marker | 无法确认文件完整性 | P3 |

---

## 3. 升级方案

建议分三期迭代，每期可独立发版，后向兼容。

### 3.1 Phase 1：基础架构（v1.0）— 解决 P0 问题

**目标**：消除 Module blob，建立函数级粒度 + 版本兼容机制。

#### 3.1.1 文件头升级

```diff
// v0
  Magic: "PTOBC\0" (6 bytes)
  Version: uint16
  Flags: uint16
  PayloadLength: uint32

// v1
  Magic: "PTOBC\0" (6 bytes)          ← 保持不变
+ MajorVersion: uint8                 ← 新增：主版本号
+ MinorVersion: uint8                 ← 新增：次版本号
+ Tag: uint16                         ← 保留，替代原 Flags
  // 删除 PayloadLength，改为每 section 独立标记长度
```

**兼容策略**：
- v1 parser 读到 Magic 后检查版本：如果是 v0 格式（version=0x0000），走 v0 解析路径
- v0 parser 读到 v1 文件时，version 字段不为 0x0000，报错提示升级

#### 3.1.2 Section 封装格式升级

```diff
// v0
  section { id: uint8, length: uint32, data: byte[] }

// v1
  section {
    idAndIsAligned: byte,       // 低 7 位 = section ID，高位 = 对齐标志
    length: varint,             // 变长编码，节省空间
+   alignment: varint?,         // 仅当高位=1 时存在
+   padding: byte[],            // 0xCB 填充到对齐边界
    data: byte[]
  }
```

#### 3.1.3 新增 Function Table Section（替代 Module blob）

这是最核心的变更——将 Module blob **拆解**为函数级粒度：

```
// 新 Section ID: 0x02 (复用 v0 的 Types ID，v0 Types 迁移到 0x08)
functionTable {
  numFunctions : varint
  // 每个函数：
  nameIndex[i] : varint              // → String Section
  signatureIndex[i] : varint         // → Type Section
  entryFlag[i] : byte                // bit0: Public/Private
                                     // bit1: DeviceFunc/KernelEntry
  functionLocIndex[i] : varint       // → Debug Section (0=无)
  lengthOfFunction[i] : varint       // 函数体字节数
  functionBody[i] : byte[length]     // 指令流
}
```

**entryFlag 设计**：

| Bit | 值 | 含义 |
|-----|-----|------|
| 0 | 0/1 | Public / Private |
| 1 | 0/1 | Device Function / Kernel Entry（对应 PTO IR 的 `pto.entry`） |
| 2-7 | - | 保留 |

**指令流编码**（functionBody 内部）：

```
instruction {
  opcode : uint16                    // PTO Op 的操作码（2 字节，支持 160+ Ops）
  locationIndex : varint             // → Debug Section (0=无)
  flags : varint                     // 可选字段存在性 bitfield
  operands/attrs : opcode-specific   // 按 opcode 定义解析
}
```

opcode 分配方案：

| 范围 | 分配 |
|------|------|
| `0x0000-0x00FF` | 核心 Tile Ops（tload, tstore, tabs, tmatmul 等） |
| `0x0100-0x01FF` | 同步 Ops（set_flag, wait_flag, barrier 等） |
| `0x0200-0x02FF` | 内存 Ops（alloc_tile, pointer_cast, bind_tile 等） |
| `0x0300-0x03FF` | 视图 Ops（make_tensor_view, partition_view 等） |
| `0x0400-0x04FF` | 通信 Ops（tput, tget, tbroadcast 等） |
| `0x0500-0x0FFF` | 保留扩展 |
| `0xFFFF` | Generic（回退到文本编码） |

#### 3.1.4 删除 Module Section (0x06)

Function Table 完全替代了 Module blob。v1 中不再有 0x06 section。

如果遇到无法用结构化编码表示的 Op（新增但尚未分配 opcode 的），可以用 `opcode = 0xFFFF (Generic)` + ASM 文本 fallback，保证前向兼容。

#### 3.1.5 新增 End Marker

```
文件末尾追加 1 字节: 0x00
```

parser 读到 0x00 确认文件完整。

#### 3.1.6 修改涉及的文件

| 文件 | 修改内容 |
|------|---------|
| `ptobc_format.h` | 新增 Section ID 常量、FunctionEntry 结构、Instruction 结构、版本常量 |
| `ptobc_format.cpp` | 新增 `buildFunctionTableSection()`、`serializeV1()`，保留 `serialize()` 兼容 v0 |
| `mlir_encode.cpp` | 将 MLIR Module 拆解为函数列表，逐函数序列化为指令流 |
| `ptobc_decode_print.cpp` | 新增 v1 Function Table 解析和打印 |
| `main.cpp` | 新增 `--version=v1` 选项，默认仍为 v0 |
| `leb128.cpp` | 无变更（varint 编码已有） |

### 3.2 Phase 2：结构化编码（v1.1）— 解决 P1 问题

**目标**：消除 ASM 文本依赖，全面结构化。

#### 3.2.1 结构化 Type Section

```diff
// v0: 存 ASM 文本
  tag: 0x00 (opaque)
  flags: 0x01 (has_asm)
  stringId → "!pto.tile_buf<vec, 32x32xf32>"

// v1.1: 结构化 typeTag
+ typeTag: byte
+ payload: typeTag-specific fields
```

PTO 类型的 typeTag 分配：

| typeTag | PTO 类型 | Payload |
|---------|---------|---------|
| `0x00`-`0x04` | I1, I8, I16, I32, I64 | 无 |
| `0x05`-`0x09` | F16, BF16, F32, F64, HIF8 | 无 |
| `0x0A` | `!pto.ptr<T>` | `elementTypeIndex: varint` |
| `0x0B` | `!pto.tile_buf<...>` | `elementTypeIndex, shape[], addressSpace, config` |
| `0x0C` | `!pto.tensor_view<...>` | `elementTypeIndex, shape[], strides[]` |
| `0x0D` | `!pto.partition_tensor_view<...>` | `elementTypeIndex, shape[]` |
| `0x0E` | `!pto.tile<...>` | `elementTypeIndex, shape[]` |
| `0x0F` | `!pto.pipe` | 无 |
| `0x10` | `!pto.async_session` | 无 |
| `0x11` | `!pto.async_event` | 无 |
| `0x12` | `!pto.eventid_array<N>` | `size: varint` |
| `0x13` | `!pto.local_array<...>` | `elementTypeIndex, shape[]` |
| `0x14` | Function Type | `numParams, paramTypes[], numResults, resultTypes[]` |
| `0xFF` | Opaque (fallback) | `asmStringIndex: varint` ← 兼容 v0 |

#### 3.2.2 结构化 Attribute Encoding

```
attributeTag : byte
attributeData : tag-specific
```

| attributeTag | 属性类型 | Payload |
|-------------|---------|---------|
| `0x01` | IntegerAttr | `typeIndex, value: varint` |
| `0x02` | FloatAttr | `typeIndex, value: bytes` |
| `0x03` | BoolAttr | `value: byte` |
| `0x04` | AddressSpaceAttr | `space: byte` (GM=1, MAT=2, LEFT=3, ...) |
| `0x05` | PipeAttr | `pipe: byte` (PIPE_S=0, PIPE_V=1, ...) |
| `0x06` | LayoutAttr | `layout: byte` (ND=0, DN=1, NZ=2) |
| `0x07` | TileBufConfigAttr | `blayout, slayout, fractal, pad, compact` |
| `0x08` | StringAttr | `stringIndex: varint` |
| `0x09` | ArrayAttr | `numElements, elements[]` |
| `0x0A` | DenseElementsAttr | `typeIndex, constantIndex` |
| `0x0B` | DictionaryAttr | `numEntries, entries[]` |
| `0xFF` | Opaque (fallback) | `asmStringIndex: varint` |

#### 3.2.3 String/Constant 随机访问索引

```diff
// v0 Strings: 顺序 LEB128
  numStrings: ULEB128
  for each: size + data  ← 只能顺序扫描

// v1.1 Strings: 偏移索引 + blob
+ numStrings: varint
+ stringStartIndex: uint32[]    ← 随机访问：跳到第 i 个字符串
+ stringData: byte[]            ← UTF-8 blob
```

Constant Data Section 同理。

#### 3.2.4 修改涉及的文件

| 文件 | 修改内容 |
|------|---------|
| `ptobc_format.h` | 新增 TypeTag/AttributeTag 枚举、结构化 type/attr 结构 |
| `ptobc_format.cpp` | 新增 `buildStructuredTypesSection()`、`buildStructuredAttrsSection()`、索引化 String/Const 构建 |
| `mlir_encode.cpp` | Type/Attr 序列化从文本模式改为 typeTag/attributeTag 模式 |
| `ptobc_decode_print.cpp` | 按 typeTag/attributeTag 解码并打印 |

### 3.3 Phase 3：丰富元数据（v1.2）— 解决 P2 问题

**目标**：补全 Global Section、升级 Debug Section。

#### 3.3.1 新增 Global Section

```
// Section ID: 0x09
global {
  numGlobals: varint
  symbolNameIndex[i] : varint       // → String Section
  valueTypeIndex[i] : varint        // → Type Section
  constantValueIndex[i] : varint    // → Constant Data Section
}
```

#### 3.3.2 升级 Debug Section

从 4 张平面表升级为层次化结构：

```diff
// v0: 4 张平面表
  DebugFileEntry:      pathSid, hashKind, hashBytes
  DebugValueNameEntry: funcId, valueId, nameSid
  DebugLocationEntry:  funcId, opId, fileId, sl, sc, el, ec
  DebugSnippetEntry:   funcId, opId, snippetSid

// v1.2: 层次化 debug 条目
+ debugEntryType: byte
+ 0x01 DICompileUnit: language, fileIndex, producer, optimized
+ 0x02 DIFile: filename, directory
+ 0x03 DILexicalBlock: line, column, scopeIndex
+ 0x04 DILoc: line, column, scopeIndex, inlinedAtIndex
+ 0x05 DISubprogram: name, linkageName, fileIndex, line, type, flags
```

同时，指令级的 `locationIndex` 已在 Phase 1 中嵌入每条指令，Phase 3 补全 Debug Section 的条目类型。

---

## 4. Section ID 分配方案

统一规划 v1 的 Section ID，避免与 v0 冲突：

| Section ID | v0 用途 | v1 用途 | 说明 |
|-----------|---------|---------|------|
| `0x00` | (无) | End Marker | 文件结束标记 |
| `0x01` | Strings | Strings（升级为索引化） | 兼容 |
| `0x02` | Types (ASM) | **Function Table** ★ | **用途变更** |
| `0x03` | Attrs (ASM) | Debug（升级为层次化） | 用途变更 |
| `0x04` | ConstPool | Constant Data（升级为索引化） | 兼容 |
| `0x05` | OpcodeSchema | OpcodeSchema | 保持 |
| `0x06` | Module blob | (废弃) | v1 中不再使用 |
| `0x07` | DebugInfo | (废弃，迁移到 0x03) | v1 中不再使用 |
| `0x08` | (无) | **Type Section（结构化）** | 新增 |
| `0x09` | (无) | **Global Section** | 新增 |
| `0x0A` | (无) | **Attribute Section（结构化）** | 新增（如需独立存储） |
| `0x7F` | Extra | Extra | 保持 |

---

## 5. 兼容性策略

### 5.1 版本判断

```
parser 读取文件：
  1. 校验 Magic = "PTOBC\0"
  2. 读取版本字段
     - 如果前 2 字节 = 0x0000 → v0 格式，走 v0 路径
     - 否则 major = byte[0], minor = byte[1] → v1+ 格式
  3. v1 parser 支持 v0 文件（后向兼容）
  4. v0 parser 遇到 v1 文件报错（前向兼容：清晰错误信息）
```

### 5.2 Section 兼容

```
v1 parser 遇到未知 Section ID：
  → 读取 length → 跳过 → 继续解析下一个 section
  → 不报错（前向兼容）

v1 parser 遇到已知但可选的 Section（如 Debug、Global）：
  → 不存在时正常工作（这些 section 是可选的）
```

### 5.3 指令兼容

```
v1 parser 遇到未知 opcode：
  → opcode = 0xFFFF (Generic)：读取 ASM 文本 fallback，交给 MLIR 解析
  → 其他未知 opcode：报错（需要升级 parser）

新增 Op 的上线流程：
  1. 先分配 opcode，定义字段布局
  2. 更新 encoder/decoder
  3. 旧 parser 对新 opcode 报错 → 提示升级
  4. 过渡期可用 0xFFFF Generic fallback
```

---

## 6. 实施路径

### 6.1 Phase 1 实施步骤

```
Step 1: ptobc_format.h — 新增数据结构
  - FunctionTableEntry { nameIndex, sigIndex, entryFlag, locIndex, bodyLength }
  - InstructionHeader { opcode, locationIndex, flags }
  - 版本常量 kVersionV1Major = 1, kVersionV1Minor = 0

Step 2: mlir_encode.cpp — 拆解 Module
  - 遍历 ModuleOp 中的所有 FuncOp
  - 每个 FuncOp 序列化为 FunctionTableEntry + instructionBody
  - instructionBody 中逐 Op 编码为 opcode + operands

Step 3: ptobc_format.cpp — 新增构建方法
  - buildFunctionTableSection()
  - serializeV1() — v1 格式输出
  - 保留 serialize() 不变 — v0 兼容

Step 4: ptobc_decode_print.cpp — 新增解析
  - parseFunctionTable()
  - printFunctionTable()

Step 5: main.cpp — 版本选项
  - --format=v0 (默认) / --format=v1

Step 6: 测试
  - 新增 v1 编码 LIT 测试
  - v0 往返测试不回归
  - v1 → v0 parser 报错测试（前向兼容）
```

### 6.2 时间估计

| Phase | 工作量 | 内容 |
|-------|--------|------|
| Phase 1 (v1.0) | 2-3 周 | Function Table + 版本兼容 + Section 格式升级 |
| Phase 2 (v1.1) | 2-3 周 | 结构化 Type/Attr 编码 + 随机访问索引 |
| Phase 3 (v1.2) | 1-2 周 | Global Section + Debug 升级 |

Phase 1 是基础，Phase 2/3 可并行开发。

### 6.3 风险与缓解

| 风险 | 缓解措施 |
|------|---------|
| 指令编码定义不稳定（PTO IR 还在频繁新增 Op） | 保留 `0xFFFF Generic` fallback，新 Op 先用文本编码，稳定后分配 opcode |
| v0→v1 迁移期间两种格式共存 | 默认仍输出 v0，`--format=v1` 显式启用，逐步切换 |
| 指令流的 operand index 编码复杂 | Phase 1 先实现简化版（只编码 opcode + 文本 body），Phase 2 再结构化 operands |
| 类型系统变更频繁 | typeTag `0xFF` (Opaque) 兜底，新类型先用文本，稳定后分配 tag |

---

## 7. 预期收益

| 收益 | Phase | 说明 |
|------|-------|------|
| **函数级懒加载** | Phase 1 | 只解析需要的函数，大幅缩短加载时间 |
| **按函数名查找** | Phase 1 | 无需反序列化整个 Module |
| **版本前向/后向兼容** | Phase 1 | 格式升级不 break 旧文件 |
| **解析速度提升** | Phase 2 | 类型/属性直接读取二进制字段，消除文本 parse |
| **外部工具可解析** | Phase 1+2 | 指令流自描述，不依赖 MLIR 内部实现 |
| **文件体积减小** | Phase 2 | 结构化编码比 ASM 文本更紧凑 |
| **Debug 追踪能力** | Phase 3 | 支持内联追踪、编译单元信息 |
| **为未来 Driver JIT 铺路** | 全部 | 如果昇腾未来支持 Driver JIT，格式已就绪 |

---

> 参考文档：
> - [PTOBC vs Tile IR 详细对比分析](PTOBC_vs_TileIR_Bytecode.md)
> - [NVIDIA Tile IR 13.2 Binary Format](https://docs.nvidia.com/cuda/tile-ir/13.2/sections/bytecode.html)
> - [PTOBC v0 格式定义](../tools/ptobc/include/ptobc/ptobc_format.h)
