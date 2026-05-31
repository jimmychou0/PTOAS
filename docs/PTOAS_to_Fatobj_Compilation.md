# PTOAS 生成的 C++ 到 Fatobj 编译流程

> 本文记录从 PTOAS 生成的 C++ 代码编译为 fatobj（胖目标文件）的完整流程，包括实际使用的命令、fatobj 的 ELF 内部结构和各环节的作用。
>
> 实验环境：192.168.1.52（CANN 9.0.0, bisheng clang 15.0.5, aarch64, A5 架构 dav-c310）

---

## 1. 什么是 Fatobj

**Fat Object（胖目标文件）** 是昇腾 CANN 工具链中由 bisheng 编译器生成的一种特殊 ELF `.o` 文件，内部同时包含：

- **Host 代码**（aarch64/x86）：kernel 启动逻辑、参数准备、`rtKernelLaunch` 调用
- **Device 代码**（`.aicore_binary` 段）：NPU AICore 可直接执行的机器码

```
fatobj (.o) 的本质：
┌──────────────────────────────────┐
│ 标准 ELF 文件（aarch64）          │
│                                  │
│  .text           ← Host CPU 代码 │
│  .aicore_binary  ← NPU 机器码 ★  │
│  .aicoreBinRec   ← NPU 元数据    │
│  __aicore_relocs ← NPU 重定位    │
│  .symtab         ← 符号表        │
└──────────────────────────────────┘
```

## 2. 完整编译链

```
PTO IR (.pto)
    │
    │  Step 1: ptoas
    ▼
C++ 源码 (kernel.cpp)
    │
    │  Step 2: bisheng -xcce（两趟编译）
    │  ├── Device pass: C++ → 模板展开 → LLVM IR → AICore 机器码
    │  └── Host pass: C++ → Launch 函数 → aarch64 代码
    ▼
kernel.o (fatobj) + launch.o
    │
    │  Step 3: bisheng --cce-fatobj-link
    ▼
libkernel.so (共享库)
    │
    │  Step 4: bisheng -xc++ 链接 main.cpp + ACL 运行时
    ▼
可执行文件 (app)
    │
    │  运行时: rtDevBinaryRegister → NPU 执行
    ▼
结果
```

## 3. 实际命令

### Step 1: ptoas 生成 C++

```bash
ptoas input.pto --pto-arch a5 --pto-backend=emitc --enable-insert-sync -o kernel.cpp
```

| 参数 | 说明 |
|------|------|
| `--pto-arch a5` | 目标架构：昇腾 950 (A5) |
| `--pto-backend=emitc` | 使用 EmitC 后端生成 C++ |
| `--enable-insert-sync` | 自动插入流水线同步指令 |
| `-o kernel.cpp` | 输出 C++ 文件 |

产出的 `kernel.cpp` 包含 `#include "pto/pto-inst.hpp"` 并调用 pto-isa 的模板函数（`TLOAD`, `TABS`, `TSTORE`, `set_flag`, `wait_flag` 等）。

### Step 2: bisheng -xcce 编译为 fatobj

```bash
source /usr/local/Ascend/cann-9.0.0/set_env.sh

bisheng -xcce \
    -fenable-matrix \
    --cce-aicore-arch=dav-c310-vec \
    -fPIC -std=c++17 \
    -I${PTO_ISA_ROOT}/include \
    -I${PTO_ISA_ROOT}/tests/common \
    -I${ASCEND_HOME_PATH}/include \
    -I/usr/local/Ascend/driver/kernel/inc \
    -mllvm -cce-aicore-stack-size=0x8000 \
    -mllvm -cce-aicore-function-stack-size=0x8000 \
    -c kernel.cpp launch.cpp
```

| 参数 | 说明 |
|------|------|
| `-xcce` | CCE 编译模式（触发两趟编译：device + host） |
| `-fenable-matrix` | 启用矩阵（Cube）指令 |
| `--cce-aicore-arch=dav-c310-vec` | 目标 AICore 架构（A5 向量核） |
| `-fPIC` | 生成位置无关代码（链接为 .so 需要） |
| `-I${PTO_ISA_ROOT}/include` | pto-isa 头文件（TLOAD/Add 等模板实现） |
| `-I${ASCEND_HOME_PATH}/include` | CANN 运行时头文件 |
| `-mllvm -cce-aicore-stack-size=0x8000` | AICore 栈大小 |
| `-c` | 只编译，不链接 |

**`-xcce` 的两趟编译**：

```
bisheng -xcce kernel.cpp
    │
    ├── 第 1 趟：Device 编译（__CCE_AICORE__ 宏生效）
    │   ├── 识别 __global__ AICORE 修饰的函数
    │   ├── 展开 pto-isa C++ 模板（TLOAD<T> → copy_gm_to_ubuf_align_v2 等硬件 intrinsic）
    │   ├── 生成 LLVM IR（面向 AICore 的目标 triple）
    │   ├── LLVM 后端：寄存器分配、指令调度
    │   └── 产出 AICore 机器码 → 嵌入 .aicore_binary 段
    │
    └── 第 2 趟：Host 编译（普通 aarch64 C++ 模式）
        ├── 编译 <<<>>> kernel launch 语法 → __cce_rtKernelLaunch 调用
        ├── 生成 device stub（host→device 调用桥接）
        └── 产出 aarch64 代码 → .text 段
```

产出：
- `kernel.o`（20,632 bytes）— **fatobj**，包含 host + device 代码
- `launch.o`（2,624 bytes）— 只含 host 侧 launch 函数和 device stub

### Step 3: fatobj 链接为共享库

```bash
bisheng --cce-fatobj-link -shared -o libkernel.so kernel.o launch.o
```

| 参数 | 说明 |
|------|------|
| `--cce-fatobj-link` | fatobj 链接模式（正确处理 .aicore_binary 段） |
| `-shared` | 输出共享库 |

产出：`libkernel.so`（19,952 bytes）

### Step 4: 编译 host 可执行文件（完整场景）

```bash
bisheng -xc++ -std=c++17 \
    -I${PTO_ISA_ROOT}/include \
    -I${ASCEND_HOME_PATH}/include \
    main.cpp -o app \
    -L. -lkernel \
    -L${ASCEND_HOME_PATH}/lib64 \
    -lruntime -lascendcl -ltiling_api -lplatform -lc_sec -lnnopbase
```

## 4. Fatobj (kernel.o) 的 ELF 内部结构

通过 `readelf -S kernel.o` 查看的实际 section 布局：

```
Section Headers:
  [Nr] Name                Type        Size     说明
  ─────────────────────────────────────────────────────────────
  [ 1] .text               PROGBITS    0x1104   Host aarch64 代码
                                                （__cce_rtKernelLaunch 等）
  [ 3] .aicore_binary      PROGBITS    0x06f8   ★ NPU AICore 机器码
                                                （device 侧指令流）
  [ 4] .rodata             PROGBITS    0x0260   只读数据
  [ 5] .data               PROGBITS    0x0002   可写数据
  [ 6] .aicoreBinRec       PROGBITS    0x0018   AICore binary 元数据记录
  [ 8] .bss                NOBITS      0x032d   未初始化数据
  [ 9] .rodata.str1.1      PROGBITS    0x0409   字符串常量
  [11] .eh_frame           PROGBITS    0x0270   异常处理帧
  [14] __aicore_relocs     PROGBITS    0x0629   AICore 重定位信息
  [15] __aicore_rel_rec    PROGBITS    0x0018   AICore 重定位记录
  [17] .init_array         INIT_ARRAY  0x0008   初始化函数数组
  [20] .symtab             SYMTAB      0x0780   符号表
  [22] .strtab             STRTAB      0x046e   字符串表
```

### 关键段说明

| 段名 | 内容 | 谁生成 | 谁消费 |
|------|------|--------|--------|
| `.text` | Host CPU 代码：kernel launch 桥接函数 | bisheng host pass | OS loader |
| `.aicore_binary` | NPU 机器码：AICore 指令流 | bisheng device pass | 昇腾 runtime（`rtDevBinaryRegister`） |
| `.aicoreBinRec` | AICore binary 的元数据（大小、入口等） | bisheng device pass | 昇腾 runtime |
| `__aicore_relocs` | AICore 指令的重定位信息 | bisheng device pass | 昇腾 runtime |
| `__aicore_rel_rec` | 重定位记录索引 | bisheng device pass | 昇腾 runtime |
| `.init_array` | C++ 静态初始化函数 | bisheng host pass | OS loader |

### 符号表中的关键符号

```
$ readelf -s kernel.o | grep kernel

  57: ... LOCAL  HIDDEN  __cce_rtKernelLaunch     ← Host 侧 kernel 启动桥接
  58: ... LOCAL  HIDDEN  __cce_rtKernelLaunchWith  ← Host 侧 kernel 启动（带参数）
  78: ... GLOBAL DEFAULT _Z14vec_abs_kernel        ← Kernel 入口（C++ mangled name）
```

## 5. Launch.o 的 ELF 结构

```
launch.o (2,624 bytes):

  [ 1] .text               ← Host 侧 Launch 包装函数
  [ 3] __aicore_relocs     ← AICore 重定位引用（指向 kernel.o 中的 device binary）
  [ 4] __cce_device_stub   ← Device stub（host→device 调用桥接代码）
  [ 6] .init_array         ← 初始化函数
```

launch.o 没有 `.aicore_binary` 段——它只包含 host 侧的启动函数和 device stub。真正的 NPU 机器码在 kernel.o 的 `.aicore_binary` 中。

## 6. Fatobj vs 普通 .o 的区别

```
普通 .o（gcc/clang 编译）：            fatobj（bisheng -xcce 编译）：
┌─────────────────────┐              ┌─────────────────────┐
│ .text  (CPU 代码)    │              │ .text  (CPU 代码)    │
│ .data               │              │ .data               │
│ .rodata             │              │ .rodata             │
│ .symtab             │              │ .symtab             │
└─────────────────────┘              │ .aicore_binary ★    │ ← NPU 机器码
                                     │ .aicoreBinRec  ★    │ ← NPU 元数据
                                     │ __aicore_relocs ★   │ ← NPU 重定位
                                     │ __aicore_rel_rec ★  │
                                     └─────────────────────┘

多出的 4 个段就是 fatobj 的"胖"的部分——NPU device 代码被嵌入 ELF 中。
```

## 7. 运行时加载机制

```
应用程序运行时：

main()
  → aclInit()                           初始化 ACL
  → aclrtSetDevice(0)                   选择 NPU 设备
  → aclrtCreateStream(&stream)          创建执行流
  → LaunchVecAbs(stream)                调用 launch 函数
      → vec_abs_kernel<<<1, nullptr, stream>>>()
          → __cce_rtKernelLaunch()       host 侧桥接
              → rtDevBinaryRegister()    注册 .aicore_binary 段中的 NPU 机器码
              → rtKernelLaunch()         启动 NPU 执行
  → aclrtSynchronizeStream(stream)      等待执行完成
  → 读取结果
```

## 8. 架构参数对照

| NPU 型号 | --pto-arch | --cce-aicore-arch | 说明 |
|----------|-----------|-------------------|------|
| 910B (A3) | `a3` | `dav-c220-cube` | A3 Cube+Vec 同核 |
| 910B (A3) Vec only | `a3` | `dav-c220-vec` | A3 仅向量核 |
| 950 (A5) | `a5` | `dav-c310` | A5 Cube+Vec 同核 |
| 950 (A5) Vec only | `a5` | `dav-c310-vec` | A5 仅向量核 |

**注意**：`--cce-aicore-arch` 必须与实际 NPU 硬件匹配，否则 pto-isa 的硬件 intrinsic 会报 "does not support the given target feature" 错误。

## 9. 常见问题

### pto-isa 版本与 CANN 不匹配

```
error: function type '...' of 'copy_gm_to_ubuf_align_v2' does not support the given target feature
```

pto-isa 的 A5 intrinsic（如 `copy_gm_to_ubuf_align_v2`）需要与 CANN/bisheng 版本对齐。解决方法：
1. 确认 pto-isa commit 与 CI pin 的版本一致
2. 确认 CANN Toolkit 版本与 pto-isa 兼容

### A5 的 set_flag/wait_flag 参数约束

```
error: the ranges of 1st parameter must be [0, 0], [2, 5], [10, 10]
```

A5 架构的 `set_flag`/`wait_flag` 不接受 `PIPE_V` (=1)。A5 使用 `get_buf`/`rls_buf` 进行 buffer 同步，或者用 `pipe_barrier(PIPE_ALL)` 做全局同步。这是 A3 和 A5 的架构差异。

---

> 实验时间：2026-05-21
> 实验文件位置：`root@192.168.1.52:/root/zjm/fatobj_demo/`
