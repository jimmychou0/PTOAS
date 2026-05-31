# VPTO 路径：ptoas 直接输出 Fatobj

> 本文记录 PTOAS 的 VPTO 后端如何跳过 C++ 中间层，一步到位输出 fatobj。
> 包括与 EmitC 路径的对比、ptoas 内部组件、输入 IR 格式差异和实验验证。
>
> 设计文档来源：[mouliangyu/PTOAS feature-vpto-backend 分支](https://github.com/mouliangyu/PTOAS/blob/feature-vpto-backend/docs/designs/ptoas-emit-fatobj.md)
>
> 实验环境：192.168.1.52（ptoas 0.33, CANN 9.0.0, bisheng clang 15.0.5, A5 架构）

---

## 1. 核心变化

```
EmitC 路径（3 步到 fatobj）：
  ptoas --pto-backend=emitc → kernel.cpp → bisheng -xcce → kernel.o
                                              ↑
                                         C++ 模板实例化（慢）

VPTO 路径（1 步到 fatobj）：
  ptoas --pto-backend=vpto → kernel.fatobj.o
                               ↑
                          ptoas 内部直接完成，跳过 C++
```

两条路径产出的 fatobj **ELF 结构完全相同**（`.text` + `.aicore_binary` + `.aicoreBinRec` + `__aicore_relocs`），区别只在生成方式——VPTO 路径省掉了 C++ 模板实例化这个最慢的环节。

---

## 2. ptoas 内部组件

`ptoas --pto-backend=vpto` 内部由三个组件协作完成 fatobj 生成：

```
ptoas --pto-backend=vpto kernel.pto -o kernel.fatobj.o

┌──────────────────────────────────────────────────────────┐
│ ptoas 主调度                                              │
│                                                          │
│  ① 解析输入 kernel.pto                                    │
│     自动嵌套为统一结构（支持 vector + cube 双 module）      │
│                                                          │
│  ② VPTOHostStubEmission                                  │
│     读取 pto.aicore 函数签名                              │
│     生成 host stub C++ 源码字符串                         │
│     （等价于 launch.cpp 中的 device stub 部分）            │
│                                                          │
│  ③ VPTOLLVMEmitter                                       │
│     PTO IR → LLVM IR                                     │
│     按 kernel_kind 拆分为 vector/cube 两个 LLVM Module    │
│     自动补后缀：vector → _mix_aiv, cube → _mix_aic        │
│                                                          │
│  ④ VPTOFatobjEmission                                    │
│     将 stub + LLVM Module 写入临时文件                     │
│     调用 bisheng 子进程编译 LLVM IR → device .o            │
│     组装 fatobj → 写入 -o 输出文件                        │
└──────────────────────────────────────────────────────────┘
```

### 各组件职责

| 组件 | 输入 | 输出 | 职责 |
|------|------|------|------|
| **VPTOHostStubEmission** | pto.aicore 函数签名 | stub C++ 源码字符串 | 生成 host→device 调用桥接代码 |
| **VPTOLLVMEmitter** | 嵌套 PTO IR module | vector/cube LLVM Module | IR lower + 拆分 + 符号重命名 |
| **VPTOFatobjEmission** | stub + LLVM Modules | fatobj (.o) | 临时文件管理 + 调用 bisheng + 组装 |

### 混合 Cube+Vector 场景

一个 kernel.pto 可以同时包含两个 module：

```mlir
module {
  module attributes {pto.kernel_kind = #pto.kernel_kind<vector>} {
    func.func @matmul_kernel(...) attributes {pto.aicore} { ... }
  }
  module attributes {pto.kernel_kind = #pto.kernel_kind<cube>} {
    func.func @matmul_kernel(...) attributes {pto.aicore} { ... }
  }
}
```

ptoas 自动处理：
- vector 的 `matmul_kernel` → 重命名为 `matmul_kernel_mix_aiv`
- cube 的 `matmul_kernel` → 重命名为 `matmul_kernel_mix_aic`
- 共享同一个 host stub
- 两份 device binary 打包进同一个 fatobj

---

## 3. 输入 IR 格式差异

VPTO 路径的输入是**向量级 IR**，比 EmitC 路径的 TileOp 级 IR 更底层：

### EmitC 路径输入（TileOp 级）

```mlir
module {
  func.func @abs_kernel(%arg0: !pto.ptr<f32>, %arg1: !pto.ptr<f32>) {
    %view = pto.make_tensor_view %arg0, shape=[32,32], strides=[32,1]
    %part = pto.partition_view %view, offsets=[0,0], sizes=[32,32]
    %buf  = pto.alloc_tile : !pto.tile_buf<vec, 32x32xf32>
    pto.tload ins(%part) outs(%buf)         ← 高层：一条搞定搬运
    pto.tabs ins(%buf) outs(%buf2)          ← 高层：一条搞定计算
    pto.tstore ins(%buf2) outs(%part2)
    return
  }
}
```

### VPTO 路径输入（向量级）

```mlir
module attributes {pto.target_arch = "a5",
                   pto.kernel_kind = #pto.kernel_kind<vector>} {
  func.func @add_kernel_2d(%arg0: !pto.ptr<f32, gm>,
                           %arg1: !pto.ptr<f32, gm>,
                           %arg2: !pto.ptr<f32, gm>)
      attributes {pto.aicore} {

    // 直接操作 UB 指针
    %ub_lhs = pto.castptr %c0_i64 : i64 -> !pto.ptr<f32, ub>

    // 底层 DMA 搬运 + buffer 同步
    pto.get_buf "PIPE_MTE2", 0, 0
    pto.mte_gm_ub %arg0, %ub_lhs, ...      ← 底层：显式 DMA 参数
    pto.rls_buf "PIPE_MTE2", 0, 0

    // 显式向量循环 + mask
    pto.vecscope {
      scf.for %offset = 0 to 1024 step 64 {
        %mask, %next = pto.plt_b32 %remaining
        %lhs = pto.vlds %ub_lhs[%offset]   ← 底层：显式向量 load
        %rhs = pto.vlds %ub_rhs[%offset]
        %sum = pto.vadd %lhs, %rhs, %mask   ← 底层：显式向量加法
        pto.vsts %sum, %ub_out[%offset], %mask
      }
    }

    // 底层 DMA 回写
    pto.mte_ub_gm %ub_out, %arg2, ...
    pto.barrier #pto.pipe<PIPE_ALL>
    return
  }
}
```

### 对照表

| 对比项 | EmitC 路径输入 | VPTO 路径输入 |
|--------|--------------|-------------|
| 抽象层级 | TileOp（`pto.tload`） | 向量指令（`pto.vlds`） |
| 内存操作 | `pto.tload ins(%view) outs(%buf)` | `pto.mte_gm_ub %ptr, %ub, ...` |
| 计算操作 | `pto.tabs ins(%a) outs(%b)` | `pto.vadd %vreg, %vreg, %mask` |
| 同步 | `set_flag/wait_flag` 或自动插入 | `get_buf/rls_buf`（buffer 同步） |
| 循环 | 无（TileOp 隐式处理） | 显式 `scf.for` + `pto.vecscope` |
| Mask | 无 | 显式 `pto.plt_b32` |
| 模块标记 | 无 kernel_kind | `pto.kernel_kind = vector/cube` |
| 函数标记 | 无 | `pto.aicore` 属性 |

---

## 4. 实验验证

### 4.1 实验命令

```bash
# 在 192.168.1.52 上执行
ssh root@192.168.1.52

PTOAS=/opt/ptoas-bin-aarch64/bin/ptoas

# VPTO 路径：一步输出 fatobj
$PTOAS --pto-arch a5 --pto-backend=vpto \
    /root/zjm/fatobj_demo/vadd_kernel.pto \
    -o /root/zjm/fatobj_demo/vadd_kernel.fatobj.o
```

### 4.2 实验结果

```
产出：vadd_kernel.fatobj.o (20,432 bytes, ELF 64-bit aarch64)

readelf -S vadd_kernel.fatobj.o:
  [ 1] .text             PROGBITS    0x1114   Host aarch64 代码
  [ 3] .aicore_binary    PROGBITS    0x0588   ★ NPU AICore 机器码
  [ 6] .aicoreBinRec     PROGBITS    0x0018   NPU 元数据
  [14] __aicore_relocs   PROGBITS    0x0619   NPU 重定位
  [15] __aicore_rel_rec  PROGBITS    0x0018   NPU 重定位记录

readelf -s | grep kernel:
  78: ... GLOBAL DEFAULT add_kernel_2d        ← kernel 入口符号
```

### 4.3 与 EmitC 路径的 fatobj 对比

```
EmitC 路径的 kernel.o:                    VPTO 路径的 vadd_kernel.fatobj.o:
  .text             0x1104                  .text             0x1114
  .aicore_binary    0x06f8                  .aicore_binary    0x0588
  .aicoreBinRec     0x0018                  .aicoreBinRec     0x0018
  __aicore_relocs   0x0629                  __aicore_relocs   0x0619
  __aicore_rel_rec  0x0018                  __aicore_rel_rec  0x0018
```

**ELF section 结构完全相同**，只是各段大小因 kernel 逻辑不同略有差异。两种路径产出的 fatobj 对昇腾 runtime 来说是等价的。

### 4.4 实验文件位置

```
root@192.168.1.52:/root/zjm/fatobj_demo/
├── input.pto                ← abs 测试输入（EmitC 路径用）
├── kernel.cpp               ← EmitC 路径生成的 C++
├── kernel.o                 ← EmitC 路径的 fatobj（bisheng -xcce 编译）
├── launch.cpp               ← launch 文件
├── launch.o                 ← launch 编译产物
├── libkernel.so             ← fatobj 链接的 .so
├── vadd_kernel.pto          ← VPTO 路径的 vadd 输入
└── vadd_kernel.fatobj.o     ← ★ VPTO 路径直接输出的 fatobj
```

---

## 5. 完整编译链对比

### EmitC 路径（当前主线）

```bash
# Step 1: ptoas → C++
ptoas --pto-backend=emitc kernel.pto -o kernel.cpp

# Step 2: bisheng -xcce → fatobj（慢：C++ 模板实例化）
bisheng -xcce --cce-aicore-arch=dav-c310-vec \
    -I${PTO_ISA_ROOT}/include ... \
    -c kernel.cpp launch.cpp

# Step 3: fatobj → .so
bisheng --cce-fatobj-link -shared -o libkernel.so kernel.o launch.o
```

### VPTO 路径（新）

```bash
# Step 1: ptoas 直接 → fatobj（快：跳过 C++ 模板实例化）
ptoas --pto-backend=vpto kernel.pto -o kernel.fatobj.o

# Step 2: 编译 launch.cpp（仍需 bisheng）
bisheng -xcce --cce-aicore-arch=dav-c310 \
    -c launch.cpp -o launch.o

# Step 3: fatobj → .so
bisheng --cce-fatobj-link -shared -o libkernel.so kernel.fatobj.o launch.o
```

### 差异汇总

| 对比项 | EmitC 路径 | VPTO 路径 |
|--------|----------|----------|
| 到 fatobj 的步数 | 2 步（ptoas→C++ → bisheng→fatobj） | 1 步（ptoas→fatobj） |
| 是否需要 pto-isa | 需要（C++ #include 模板库） | 不需要 |
| C++ 模板实例化 | 需要（编译时间瓶颈） | 跳过 |
| 输入 IR 层级 | TileOp 级 | 向量指令级 |
| launch.cpp 编译 | 需要 bisheng | 仍需要 bisheng |
| 最终 fatobj 格式 | 标准 ELF + .aicore_binary | 完全相同 |

---

## 6. 设计约束（来自 ptoas-emit-fatobj.md）

| 约束 | 说明 |
|------|------|
| 不修改 EmitC 路径 | 所有修改发生在 VPTO 路径内 |
| 统一嵌套 module | 单 module 也自动包裹为嵌套结构 |
| nest pm 驱动 | 所有 pass 通过 nest pm 驱动，不手动切分 module |
| 文件级临时管理 | 禁止目录级临时文件管理（降低删除风险） |
| 最多两个 module | 每个 kernel_kind（vector/cube）最多一个 module |
| 函数名后缀自动补 | vector → `_mix_aiv`，cube → `_mix_aic`，输入 IR 不手写后缀 |
| stub 共享 | cube 和 vector 中同名 pto.aicore 函数共享同一个 stub |

---

> 实验时间：2026-05-21
> fork 仓库：https://github.com/mouliangyu/PTOAS/tree/feature-vpto-backend
> 本地代码：D:\work\ptoas\PTOAS-vpto
