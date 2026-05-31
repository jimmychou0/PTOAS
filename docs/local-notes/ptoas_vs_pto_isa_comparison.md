# PTOOps.td vs pto-isa 函数参数对比报告

> 生成日期: 2026-05-22
>
> **对比规则:**
> - ptoas 的 op arguments 是 pto-isa 所有同名重载函数参数的**并集**
> - pto-isa 中部分重载有、部分重载无的参数，在 ptoas 中一般为 `Optional` / `OptionalAttr`
> - pto-isa 所有函数末尾的 `WaitEvents &...events` **不计入比较**（同步机制，非数据参数）
> - pto-isa 的模板参数（如 `<PrecisionType>`, `<AccPhase>`, `<PadValue>`）对应 ptoas 的 Attr 属性
> - pto-isa 的 `dst` 在 ptoas 中为 DPS `outs(...)` 参数

---

## 符号说明

| 符号 | 含义 |
|------|------|
| OK | 完全对应 |
| MISMATCH | 存在不对应参数（详见具体说明） |
| PTOAS_ONLY | 仅 ptoas 有此 op，pto-isa 无对应函数 |
| ISA_ONLY | 仅 pto-isa 有此函数，ptoas 无对应 op |

---

## 一、数据搬运类 (DMA / Data Movement)

### 1. TLoadOp (`pto.tload`) ↔ `TLOAD` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src_global (GlobalData) | OK |
| dst | PTODpsType | dst_tile (TileData) | OK |
| pad_mode | OptionalAttr\<PTO_PadModeAttr\> | **不存在** | **MISMATCH: ptoas 多出 pad_mode** |
| pad_value | Optional\<AnyType\> | **不存在** | **MISMATCH: ptoas 多出 pad_value** |
| left_padding_num | Optional\<Index\> | **不存在** | **MISMATCH: ptoas 多出 left_padding_num** |
| right_padding_num | Optional\<AnyType\> | **不存在** | **MISMATCH: ptoas 多出 right_padding_num** |
| init_out_buffer | DefaultValuedOptionalAttr\<BoolAttr\> | **不存在** | **MISMATCH: ptoas 多出 init_out_buffer** |
| init_condition | Optional\<AnyType\> | **不存在** | **MISMATCH: ptoas 多出 init_condition** |

**说明:** pto-isa `TLOAD(dst_tile, src_global, events...)` 没有任何 padding 相关参数。ptoas 中的 padding/init 参数在 pto-isa 中无对应。

---

### 2. TPrefetchOp (`pto.tprefetch`) ↔ `TPREFETCH` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src_global | OK |
| dst | PTODpsType | dst_tile | OK |

---

### 3. TStoreOp (`pto.tstore`) ↔ `TSTORE` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src_tile | OK |
| dst | PTODpsType | dst_global | OK |
| preQuantScalar | Optional\<I64\> | uint64_t preQuantScalar (部分重载) | OK (Optional 对应部分重载) |
| stPhase | DefaultValuedAttr\<PTO_STPhaseAttr\> | \<STPhase\> 模板参数 (部分重载) | OK |
| atomicType | DefaultValuedAttr\<PTO_AtomicTypeAttr\> | \<AtomicType\> 模板参数 (部分重载) | OK |
| reluPreMode | DefaultValuedAttr\<PTO_ReluPreModeAttr\> | \<ReluPreMode\> 模板参数 (部分重载) | OK |

---

### 4. TStoreFPOp (`pto.tstore_fp`) ↔ `TSTORE_FP` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src_tile | OK |
| fp | PTODpsType | fp_tile | OK |
| dst | PTODpsType | dst_global | OK |

---

### 5. TTransOp (`pto.ttrans`) ↔ `TTRANS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| tmp | PTODpsType | tmp | OK |
| dst | PTODpsType | dst | OK |

---

### 6. TMovOp (`pto.tmov`) ↔ `TMOV` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |
| fp | Optional\<PTODpsType\> | FpTileData (部分重载) | OK |
| preQuantScalar | Optional\<I64\> | preQuantScalar (部分重载) | OK |
| accToVecMode | OptionalAttr\<PTO_AccToVecModeAttr\> | AccToVecMode (部分重载) | OK |
| reluPreMode | DefaultValuedAttr\<PTO_ReluPreModeAttr\> | ReluPreMode (部分重载) | OK |

---

### 7. TMovFPOp (`pto.tmov.fp`) ↔ `TMOV_FP` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| fp | PTODpsType | fp | OK |
| dst | PTODpsType | dst | OK |

---

## 二、矩阵乘法类 (Matrix Multiply)

### 8. TMatmulOp (`pto.tmatmul`) ↔ `TMATMUL` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| lhs (a) | PTODpsType | aMatrix | OK |
| rhs (b) | PTODpsType | bMatrix | OK |
| dst (c) | PTODpsType | cMatrix | OK |
| AccPhase | **不存在** | \<AccPhase\> 模板参数 (部分重载) | **MISMATCH: ptoas 缺少 AccPhase 属性** |

---

### 9. TMatmulAccOp (`pto.tmatmul.acc`) ↔ `TMATMUL_ACC` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| acc_in (cIn) | PTODpsType | cIn | OK |
| lhs (a) | PTODpsType | aMatrix | OK |
| rhs (b) | PTODpsType | bMatrix | OK |
| dst (cOut) | PTODpsType | cOut | OK |
| AccPhase | **不存在** | \<AccPhase\> 模板参数 (部分重载) | **MISMATCH: ptoas 缺少 AccPhase 属性** |

---

### 10. TMatmulBiasOp (`pto.tmatmul.bias`) ↔ `TMATMUL_BIAS` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| a | PTODpsType | aMatrix | OK |
| b | PTODpsType | bMatrix | OK |
| bias | PTODpsType | biasData | OK |
| dst (c) | PTODpsType | cMatrix | OK |
| AccPhase | **不存在** | \<AccPhase\> 模板参数 (部分重载) | **MISMATCH: ptoas 缺少 AccPhase 属性** |

---

### 11. TMatmulMxOp (`pto.tmatmul.mx`) ↔ `TMATMUL_MX` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| a | PTODpsType | a | OK |
| a_scale | PTODpsType | aScale | OK |
| b | PTODpsType | b | OK |
| b_scale | PTODpsType | bScale | OK |
| dst | PTODpsType | c | OK |
| AccPhase | **不存在** | \<AccPhase\> 模板参数 (部分重载) | **MISMATCH: ptoas 缺少 AccPhase 属性** |

---

### 12. TMatmulMxAccOp (`pto.tmatmul.mx.acc`) ↔ `TMATMUL_MX` (acc overload) — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| c_in | PTODpsType | cIn | OK |
| a | PTODpsType | a | OK |
| a_scale | PTODpsType | aScale | OK |
| b | PTODpsType | b | OK |
| b_scale | PTODpsType | bScale | OK |
| dst | PTODpsType | cOut | OK |
| AccPhase | **不存在** | \<AccPhase\> 模板参数 (部分重载) | **MISMATCH: ptoas 缺少 AccPhase 属性** |

---

### 13. TMatmulMxBiasOp (`pto.tmatmul.mx.bias`) ↔ `TMATMUL_MX` (bias overload) — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| a | PTODpsType | a | OK |
| a_scale | PTODpsType | aScale | OK |
| b | PTODpsType | b | OK |
| b_scale | PTODpsType | bScale | OK |
| bias | PTODpsType | bias | OK |
| dst | PTODpsType | c | OK |
| AccPhase | **不存在** | \<AccPhase\> 模板参数 (部分重载) | **MISMATCH: ptoas 缺少 AccPhase 属性** |

---

### 14. TGemvOp (`pto.tgemv`) ↔ `TGEMV` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| lhs (a) | PTODpsType | aMatrix | OK |
| rhs (b) | PTODpsType | bMatrix | OK |
| dst (c) | PTODpsType | cMatrix | OK |
| AccPhase | **不存在** | \<AccPhase\> 模板参数 (部分重载) | **MISMATCH: ptoas 缺少 AccPhase 属性** |

---

### 15. TGemvAccOp (`pto.tgemv.acc`) ↔ `TGEMV_ACC` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| acc_in | PTODpsType | cIn | OK |
| lhs | PTODpsType | aMatrix | OK |
| rhs | PTODpsType | bMatrix | OK |
| dst | PTODpsType | cOut | OK |
| AccPhase | **不存在** | \<AccPhase\> 模板参数 (部分重载) | **MISMATCH: ptoas 缺少 AccPhase 属性** |

---

### 16. TGemvBiasOp (`pto.tgemv.bias`) ↔ `TGEMV_BIAS` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| a | PTODpsType | aMatrix | OK |
| b | PTODpsType | bMatrix | OK |
| bias | PTODpsType | biasData | OK |
| dst | PTODpsType | cMatrix | OK |
| AccPhase | **不存在** | \<AccPhase\> 模板参数 (部分重载) | **MISMATCH: ptoas 缺少 AccPhase 属性** |

---

### 17. TGemvMxOp (`pto.tgemv.mx`) ↔ `TGEMV_MX` — MISMATCH

同 TMatmulMxOp，**缺少 AccPhase**。

---

### 18. TGemvMxAccOp (`pto.tgemv.mx.acc`) ↔ `TGEMV_MX` (acc) — MISMATCH

同上，**缺少 AccPhase**。

---

### 19. TGemvMxBiasOp (`pto.tgemv.mx.bias`) ↔ `TGEMV_MX` (bias) — MISMATCH

同上，**缺少 AccPhase**。

---

## 三、向量/逐元素运算 (Vector/Elementwise)

### 20. TAbsOp (`pto.tabs`) ↔ `TABS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

### 21. TAddOp (`pto.tadd`) ↔ `TADD` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 22. TAddCOp (`pto.taddc`) ↔ `TADDC` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| src2 | PTODpsType | src2 | OK |
| dst | PTODpsType | dst | OK |

---

### 23. TAddSOp (`pto.tadds`) ↔ `TADDS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src0 | OK |
| scalar | ScalarType | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 24. TAxpyOp (`pto.taxpy`) ↔ `TAXPY` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src0 | OK |
| scalar | ScalarType | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 25. TAddSCOp (`pto.taddsc`) ↔ `TADDSC` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| scalar | ScalarType | scalar | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 26. TSubOp (`pto.tsub`) ↔ `TSUB` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 27. TSubCOp (`pto.tsubc`) ↔ `TSUBC` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| src2 | PTODpsType | src2 | OK |
| dst | PTODpsType | dst | OK |

---

### 28. TSubSOp (`pto.tsubs`) ↔ `TSUBS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src0 | OK |
| scalar | ScalarType | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 29. TSubSCOp (`pto.tsubsc`) ↔ `TSUBSC` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| scalar | ScalarType | scalar | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 30. TMulOp (`pto.tmul`) ↔ `TMUL` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 31. TMulSOp (`pto.tmuls`) ↔ `TMULS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| scalar | ScalarType | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 32. TDivOp (`pto.tdiv`) ↔ `TDIV` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |
| PrecisionType | **不存在** | \<PrecisionType=DivAlgorithm::DEFAULT\> | **MISMATCH: ptoas 缺少 PrecisionType/DivAlgorithm 属性** |

---

### 33. TDivSOp (`pto.tdivs`) ↔ `TDIVS` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | AnyType | src0 (tile) 或 scalar | OK (AnyType 覆盖两种重载) |
| scalar | AnyType | scalar 或 src0 (tile) | OK |
| dst | PTODpsType | dst | OK |
| PrecisionType | **不存在** | \<PrecisionType\> | **MISMATCH: ptoas 缺少 PrecisionType 属性** |

---

### 34. TFModOp (`pto.tfmod`) ↔ `TFMOD` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |
| PrecisionType | **不存在** | \<PrecisionType=FmodAlgorithm::DEFAULT\> | **MISMATCH: ptoas 缺少 PrecisionType/FmodAlgorithm 属性** |

---

### 35. TFModSOp (`pto.tfmods`) ↔ `TFMODS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| scalar | ScalarType | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 36. TRemOp (`pto.trem`) ↔ `TREM` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| tmp | PTODpsType | tmp | OK |
| dst | PTODpsType | dst | OK |
| PrecisionType | **不存在** | \<PrecisionType=RemAlgorithm::DEFAULT\> | **MISMATCH: ptoas 缺少 PrecisionType/RemAlgorithm 属性** |

---

### 37. TRemSOp (`pto.trems`) ↔ `TREMS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| scalar | ScalarType | scalar | OK |
| tmp | PTODpsType | tmp | OK |
| dst | PTODpsType | dst | OK |

---

### 38. TNegOp (`pto.tneg`) ↔ `TNEG` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

### 39. TNotOp (`pto.tnot`) ↔ `TNOT` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

### 40. TReluOp (`pto.trelu`) ↔ `TRELU` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

### 41. TLReluOp (`pto.tlrelu`) ↔ `TLRELU` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| slope | F32 | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 42. TPReluOp (`pto.tprelu`) ↔ `TPRELU` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 (slope) | PTODpsType | src1 | OK |
| tmp | PTODpsType | tmp | OK |
| dst | PTODpsType | dst | OK |

---

### 43. TRecipOp (`pto.trecip`) ↔ `TRECIP` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |
| PrecisionType | **不存在** | \<PrecisionType=RecipAlgorithm::DEFAULT\> | **MISMATCH: ptoas 缺少 PrecisionType 属性** |

---

### 44. TExpOp (`pto.texp`) ↔ `TEXP` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |
| PrecisionType | **不存在** | \<PrecisionType=ExpAlgorithm::DEFAULT\> | **MISMATCH: ptoas 缺少 PrecisionType/ExpAlgorithm 属性** |

---

### 45. TLogOp (`pto.tlog`) ↔ `TLOG` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |
| PrecisionType | **不存在** | \<PrecisionType=LogAlgorithm::DEFAULT\> | **MISMATCH: ptoas 缺少 PrecisionType/LogAlgorithm 属性** |

---

### 46. TSqrtOp (`pto.tsqrt`) ↔ `TSQRT` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |
| PrecisionType | **不存在** | \<PrecisionType=SqrtAlgorithm::DEFAULT\> | **MISMATCH: ptoas 缺少 PrecisionType/SqrtAlgorithm 属性** |

---

### 47. TRsqrtOp (`pto.trsqrt`) ↔ `TRSQRT` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |
| tmp | Optional\<PTODpsType\> | tmp (部分重载有) | OK |
| PrecisionType | **不存在** | \<PrecisionType=RsqrtAlgorithm::DEFAULT\> | **MISMATCH: ptoas 缺少 PrecisionType/RsqrtAlgorithm 属性** |

---

### 48. TExpandsOp (`pto.texpands`) ↔ `TEXPANDS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| scalar | ScalarType | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 49. TAndOp (`pto.tand`) ↔ `TAND` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 50. TAndSOp (`pto.tands`) ↔ `TANDS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| scalar | AnySignlessInteger | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 51. TOrOp (`pto.tor`) ↔ `TOR` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 52. TOrSOp (`pto.tors`) ↔ `TORS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| scalar | AnySignlessInteger | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 53. TXorOp (`pto.txor`) ↔ `TXOR` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| tmp | PTODpsType | tmp | OK |
| dst | PTODpsType | dst | OK |

---

### 54. TXorSOp (`pto.txors`) ↔ `TXORS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src0 | OK |
| scalar | AnySignlessInteger | scalar | OK |
| tmp | PTODpsType | tmp | OK |
| dst | PTODpsType | dst | OK |

---

### 55. TShlOp (`pto.tshl`) ↔ `TSHL` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 56. TShrOp (`pto.tshr`) ↔ `TSHR` — OK

同 TShlOp 结构，完全对应。

---

### 57. TShlSOp (`pto.tshls`) ↔ `TSHLS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| scalar | AnySignlessInteger | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 58. TShrSOp (`pto.tshrs`) ↔ `TSHRS` — OK

同 TShlSOp 结构。

---

### 59. TMinOp (`pto.tmin`) ↔ `TMIN` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 60. TMinSOp (`pto.tmins`) ↔ `TMINS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| scalar | ScalarType | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 61. TMaxOp (`pto.tmax`) ↔ `TMAX` — OK

同 TMinOp 结构。

---

### 62. TMaxSOp (`pto.tmaxs`) ↔ `TMAXS` — OK

同 TMinSOp 结构。

---

### 63. TCmpOp (`pto.tcmp`) ↔ `TCMP` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |
| cmpMode | OptionalAttr\<PTO_CmpModeAttr\> | CmpMode | OK |

---

### 64. TCmpSOp (`pto.tcmps`) ↔ `TCMPS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src0 | OK |
| scalar | ScalarType | scalar | OK |
| cmpMode | DefaultValuedAttr\<PTO_CmpModeAttr\> | CmpMode | OK |
| dst | PTODpsType | dst | OK |

**注:** pto-isa `TCMPS` 还有 tile-vs-tile 重载 `(dst, src0, src1, CmpMode)`，此形式在 ptoas 中由 `TCmpOp` 覆盖。

---

### 65. TSelOp (`pto.tsel`) ↔ `TSEL` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| mask | PTODpsType | selMask | OK |
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| tmp | PTODpsType | tmp | OK |
| dst | PTODpsType | dst | OK |

---

### 66. TSelSOp (`pto.tsels`) ↔ `TSELS` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| mask | PTODpsType | mask | OK |
| src | PTODpsType | src | OK |
| tmp | PTODpsType | tmp | OK |
| scalar | ScalarType | scalar | OK |
| dst | PTODpsType | dst | OK |

---

### 67. TCvtOp (`pto.tcvt`) ↔ `TCVT` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |
| tmp | Optional\<PTODpsType\> | tmp (部分重载) | OK |
| rmode | DefaultValuedAttr\<PTO_RoundModeAttr\> | RoundMode | OK |
| sat_mode | OptionalAttr\<PTO_SaturationModeAttr\> | SaturationMode (部分重载) | OK |

---

### 68. TConcatOp (`pto.tconcat`) ↔ `TCONCAT` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 69. TConcatidxOp (`pto.tconcatidx`) ↔ `TCONCAT` (5-arg overload) — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| src0Idx | PTODpsType | src0Idx | OK |
| src1Idx | PTODpsType | src1Idx | OK |
| dst | PTODpsType | dst | OK |

---

### 70. TCIOp (`pto.tci`) ↔ `TCI` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| S (start) | AnyInteger | start | OK |
| dst | PTODpsType | dst | OK |
| descending | DefaultValuedAttr\<BoolAttr\> | \<descending\> 模板参数 | OK |

**注:** pto-isa 还有带 tmp 的重载 `(dst, start, tmp, events...)`，ptoas 缺少可选 tmp。这是轻微 mismatch 但 TCI 的 tmp 重载较少用。

---

### 71. TTriOp (`pto.ttri`) ↔ `TTRI` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| diagonal | AnyInteger | diagonal | OK |
| dst | PTODpsType | dst | OK |
| upperOrLower | DefaultValuedAttr\<I32Attr\> | \<isUpperOrLower\> 模板参数 | OK |

---

### 72. TRandomOp (`pto.trandom`) ↔ `TRANDOM` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| key0, key1 | 2 × AnySignlessInteger | key (单个 struct/aggregate) | **MISMATCH: ptoas 拆分为 2 个标量，pto-isa 用单一 key 参数** |
| counter0..3 | 4 × AnySignlessInteger | counter (单个 struct/aggregate) | **MISMATCH: ptoas 拆分为 4 个标量，pto-isa 用单一 counter 参数** |
| dst | PTODpsType | dst | OK |
| rounds | DefaultValuedAttr\<I32Attr, "10"\> | \<Rounds=10\> 模板参数 | OK |

**说明:** pto-isa 的 `TRANDOM<Rounds>(dst, key, counter)` 中 key 和 counter 是聚合类型，ptoas 将其展开为 6 个独立标量。

---

## 四、Extract / Insert 类

### 73. TExtractOp (`pto.textract`) ↔ `TEXTRACT` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| indexRow | Index | indexRow | OK |
| indexCol | Index | indexCol | OK |
| dst | PTODpsType | dst | OK |
| ReluPreMode | **不存在** | ReluPreMode (部分重载) | **MISMATCH: ptoas 缺少 ReluPreMode** |
| AccToVecMode | **不存在** | AccToVecMode (部分重载) | **MISMATCH: ptoas 缺少 AccToVecMode** |
| preQuantScalar | **不存在** | preQuantScalar (部分重载) | **MISMATCH: ptoas 缺少 preQuantScalar** |

**说明:** pto-isa `TEXTRACT` 有 5+ 重载组合包含 ReluPreMode, AccToVecMode, preQuantScalar 参数，这些在 ptoas 中完全缺失。

---

### 74. TExtractFPOp (`pto.textract_fp`) ↔ `TEXTRACT_FP` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| fp | PTODpsType | fp | OK |
| indexRow | Index | indexRow | OK |
| indexCol | Index | indexCol | OK |
| dst | PTODpsType | dst | OK |
| AccToVecMode | **不存在** | AccToVecMode (部分重载) | **MISMATCH: ptoas 缺少 AccToVecMode** |

---

### 75. TInsertOp (`pto.tinsert`) ↔ `TINSERT` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| indexRow | Index | indexRow | OK |
| indexCol | Index | indexCol | OK |
| dst | PTODpsType | dst | OK |
| ReluPreMode | **不存在** | ReluPreMode (部分重载) | **MISMATCH: ptoas 缺少 ReluPreMode** |
| AccToVecMode | **不存在** | AccToVecMode (部分重载) | **MISMATCH: ptoas 缺少 AccToVecMode** |
| preQuantScalar | **不存在** | preQuantScalar (部分重载) | **MISMATCH: ptoas 缺少 preQuantScalar** |
| FpTileData | **不存在** | FpTileData (部分重载) | **MISMATCH: ptoas 缺少 fp 参数** |
| TInsertMode | **不存在** | TInsertMode (A5, 部分重载) | **MISMATCH: ptoas 缺少 TInsertMode** |

**说明:** pto-isa `TINSERT` 有 7+ 重载变体，ptoas 仅建模了最基础的 4 参数形式。

---

### 76. TInsertFPOp (`pto.tinsert_fp`) ↔ `TINSERT_FP` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| fp | PTODpsType | fp | OK |
| indexRow | Index | indexRow | OK |
| indexCol | Index | indexCol | OK |
| dst | PTODpsType | dst | OK |
| AccToVecMode | **不存在** | AccToVecMode (部分重载) | **MISMATCH: ptoas 缺少 AccToVecMode** |

---

## 五、Pad / Fill 类

### 77. TFillPadOp (`pto.tfillpad`) ↔ `TFILLPAD` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |
| PadValue | **不存在** | \<PadValue=Zero\> 模板参数 (Mat 重载) | **MISMATCH: ptoas 缺少 PadValue 模板参数** |

---

### 78. TFillPadExpandOp (`pto.tfillpad_expand`) ↔ `TFILLPAD_EXPAND` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

### 79. TFillPadInplaceOp (`pto.tfillpad_inplace`) ↔ `TFILLPAD_INPLACE` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

## 六、Reduction 类

### 80. TRowMaxOp (`pto.trowmax`) ↔ `TROWMAX` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| tmp | PTODpsType | tmp | OK |
| dst | PTODpsType | dst | OK |

---

### 81. TRowMinOp (`pto.trowmin`) ↔ `TROWMIN` — OK

同 TRowMaxOp 结构。

---

### 82. TRowSumOp (`pto.trowsum`) ↔ `TROWSUM` — OK

同 TRowMaxOp 结构。

---

### 83. TRowProdOp (`pto.trowprod`) ↔ `TROWPROD` — OK

同 TRowMaxOp 结构。

---

### 84. TRowArgMaxOp (`pto.trowargmax`) ↔ `TROWARGMAX` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| tmp | PTODpsType | tmp | OK |
| dst | PTODpsType | dst | OK |

**注:** pto-isa 还有 5 参数重载 `(dstVal, dstIdx, src, tmp, events...)`，ptoas 未建模此形式。但基础 3 参数形式对应。

---

### 85. TRowArgMinOp (`pto.trowargmin`) ↔ `TROWARGMIN` — OK

同 TRowArgMaxOp。

---

### 86. TColMaxOp (`pto.tcolmax`) ↔ `TCOLMAX` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

### 87. TColMinOp (`pto.tcolmin`) ↔ `TCOLMIN` — OK

同 TColMaxOp。

---

### 88. TColSumOp (`pto.tcolsum`) ↔ `TCOLSUM` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| tmp | Optional\<PTODpsType\> | tmp (部分重载) | OK |
| dst | PTODpsType | dst | OK |
| isBinary | DefaultValuedOptionalAttr\<BoolAttr\> | isBinary (部分重载) | OK |

---

### 89. TColProdOp (`pto.tcolprod`) ↔ `TCOLPROD` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

### 90. TColArgMaxOp (`pto.tcolargmax`) ↔ `TCOLARGMAX` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| tmp | PTODpsType | tmp | OK |
| dst | PTODpsType | dst | OK |

---

### 91. TColArgMinOp (`pto.tcolargmin`) ↔ `TCOLARGMIN` — OK

同 TColArgMaxOp。

---

## 七、Expand / Broadcast 类

### 92. TRowExpandOp (`pto.trowexpand`) ↔ `TROWEXPAND` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

### 93. TRowExpandAddOp (`pto.trowexpandadd`) ↔ `TROWEXPANDADD` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |
| tmp | **不存在** | tmp (部分重载有) | **MISMATCH: ptoas 缺少 Optional tmp** |

**说明:** pto-isa `TROWEXPANDADD` 有带 tmp 的重载，ptoas 未建模 tmp 参数。

---

### 94. TRowExpandSubOp (`pto.trowexpandsub`) ↔ `TROWEXPANDSUB` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| tmp | Optional\<PTODpsType\> | tmp (部分重载) | OK |
| dst | PTODpsType | dst | OK |

---

### 95. TRowExpandMulOp (`pto.trowexpandmul`) ↔ `TROWEXPANDMUL` — OK

同 TRowExpandSubOp 结构 (有 Optional tmp)。

---

### 96. TRowExpandDivOp (`pto.trowexpanddiv`) ↔ `TROWEXPANDDIV` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| tmp | Optional\<PTODpsType\> | tmp (部分重载) | OK |
| dst | PTODpsType | dst | OK |
| PrecisionType | **不存在** | \<PrecisionType\> 模板参数 | **MISMATCH: ptoas 缺少 PrecisionType** |

---

### 97. TRowExpandExpdifOp (`pto.trowexpandexpdif`) ↔ `TROWEXPANDEXPDIF` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| tmp | Optional\<PTODpsType\> | tmp (部分重载) | OK |
| dst | PTODpsType | dst | OK |

---

### 98. TRowExpandMaxOp / TRowExpandMinOp — OK

同 TRowExpandExpdifOp 结构。

---

### 99. TColExpandOp (`pto.tcolexpand`) ↔ `TCOLEXPAND` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

### 100. TColExpandAddOp (`pto.tcolexpandadd`) ↔ `TCOLEXPANDADD` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 101-107. TColExpand{Sub,Mul,Div,Max,Min,Expdif}Op — OK

均为 `(src0, src1, dst)` 形式，与 pto-isa 对应。

**注:** pto-isa `TCOLEXPANDDIV` 有 `<PrecisionType>` 模板参数，ptoas 未建模。→ **TColExpandDivOp MISMATCH: 缺少 PrecisionType**

---

## 八、Partial Reduction 类

### 108. TPartAddOp (`pto.tpartadd`) ↔ `TPARTADD` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| dst | PTODpsType | dst | OK |

---

### 109-111. TPartMaxOp / TPartMinOp / TPartMulOp — OK

同 TPartAddOp 结构。

---

### 112. TPartArgMaxOp (`pto.tpartargmax`) ↔ `TPARTARGMAX` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src0 | PTODpsType | src0 | OK |
| src1 | PTODpsType | src1 | OK |
| src0Idx | PTODpsType | src0Idx | OK |
| src1Idx | PTODpsType | src1Idx | OK |
| dst | PTODpsType | dst | OK |
| dstIdx | PTODpsType | dstIdx | OK |

---

### 113. TPartArgMinOp — OK

同 TPartArgMaxOp。

---

## 九、Gather / Scatter / Sort 类

### 114. TGatherOp (`pto.tgather`) ↔ `TGATHER` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |
| cdst | Optional\<PTODpsType\> | cdst (compare 重载) | OK |
| indices | Optional\<PTODpsType\> | indices (index 重载) | OK |
| tmp | Optional\<PTODpsType\> | tmp (部分重载) | OK |
| kValue | Optional\<ScalarType\> | k_value (compare 重载) | OK |
| maskPattern | OptionalAttr\<PTO_MaskPatternAttr\> | \<MaskPattern\> (mask 重载) | OK |
| cmpMode | OptionalAttr\<PTO_CmpModeAttr\> | CmpMode (部分重载) | OK |
| offset | OptionalAttr\<I32Attr\> | offset (部分重载) | OK |

---

### 115. TGatherBOp (`pto.tgatherb`) ↔ `TGATHERB` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| offsets | PTODpsType | offset | OK |
| dst | PTODpsType | dst | OK |

---

### 116. TScatterOp (`pto.tscatter`) ↔ `TSCATTER` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| indexes | PTODpsType | indexes | OK |
| dst | PTODpsType | dst | OK |

---

### 117. MGatherOp (`pto.mgather`) ↔ `MGATHER` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| mem | PTODpsType | src_global | OK |
| idx | PTODpsType | indexes | OK |
| dst | PTODpsType | dst_tile | OK |
| gatherOob | DefaultValuedAttr\<PTO_GatherOOBAttr\> | \<GatherOOB\> (A5 重载) | OK |
| Coalesce | **不存在** | \<Coalesce\> (A5 重载) | **MISMATCH: ptoas 缺少 Coalesce 模板参数** |

---

### 118. MScatterOp (`pto.mscatter`) ↔ `MSCATTER` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src_tile | OK |
| idx | PTODpsType | indexes | OK |
| mem | PTODpsType | dst_global | OK |
| scatterAtomicOp | DefaultValuedAttr\<PTO_ScatterAtomicOpAttr\> | \<ScatterAtomicOp\> | OK |
| scatterOob | DefaultValuedAttr\<PTO_ScatterOOBAttr\> | \<ScatterOOB\> | OK |
| Coalesce | **不存在** | \<Coalesce\> (A5 重载) | **MISMATCH: ptoas 缺少 Coalesce** |
| ScatterConflict | **不存在** | \<ScatterConflict\> (A5 重载) | **MISMATCH: ptoas 缺少 ScatterConflict** |

---

### 119. TSort32Op (`pto.tsort32`) ↔ `TSORT32` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| idx | PTODpsType | idx | OK |
| tmp | Optional\<PTODpsType\> | tmp (部分重载) | OK |
| dst | PTODpsType | dst | OK |

---

### 120. TMrgSortOp (`pto.tmrgsort`) ↔ `TMRGSORT` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| srcs | Variadic\<PTODpsType\> | 2-4 sources 或 src | OK |
| blockLen | Optional\<AnyInteger\> | blockLen (format1) | OK |
| dsts | Variadic\<PTODpsType\> | dst | OK |
| tmp | Optional\<PTODpsType\> | tmp (format2) | OK |
| excuted | Optional\<AnyType\> | MrgSortExecutedNumList (format2) | OK |
| exhausted | DefaultValuedAttr\<BoolAttr\> | exhausted flag | OK |

---

## 十、Quantization 类

### 121. TQuantOp (`pto.tquant`) ↔ `TQUANT` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| fp | PTODpsType | scale/exp | OK |
| offset | Optional\<PTODpsType\> | offset (INT8_ASYM) | OK |
| dst | PTODpsType | dst | OK |
| quant_type | PTO_QuantTypeAttr | \<quant_type\> 模板参数 | OK |
| max | **不存在** | max (A5 QuantScaleAlg 重载) | **MISMATCH: ptoas 缺少 A5 max 参数** |
| scaling | **不存在** | scaling (A5 QuantScaleAlg 重载) | **MISMATCH: ptoas 缺少 A5 scaling 参数** |
| exp_zz | **不存在** | exp_zz (A5 store_mode 重载) | **MISMATCH: ptoas 缺少 A5 exp_zz 参数** |
| store_mode | **不存在** | \<store_mode\> (A5 重载) | **MISMATCH: ptoas 缺少 A5 store_mode** |
| QuantScaleAlg | **不存在** | QuantScaleAlg (A5 重载) | **MISMATCH: ptoas 缺少 A5 QuantScaleAlg** |

**说明:** pto-isa 的 A5 专用 TQUANT 重载有多个额外参数 (max, scaling, exp_zz, store_mode, QuantScaleAlg)，ptoas 完全未建模。

---

### 122. TDequantOp (`pto.tdequant`) ↔ `TDEQUANT` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| scale | PTODpsType | scale | OK |
| offset | PTODpsType | offset | OK |
| dst | PTODpsType | dst | OK |

---

### 123. TGetScaleAddrOp (`pto.tget_scale_addr`) ↔ `TGET_SCALE_ADDR` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| dst | PTODpsType | dst | OK |

---

## 十一、Scalar Get/Set 类

### 124. TSetValOp (`pto.tsetval`) ↔ (pto-isa 内部) — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| dst | PTODpsType | dst | OK |
| offset | Index | offset | OK |
| val | ScalarType | val | OK |

---

### 125. TGetValOp (`pto.tgetval`) ↔ (pto-isa 内部) — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| offset | Index | offset | OK |
| dst (result) | ScalarType | 返回值 | OK |

---

### 126. THistogramOp (`pto.thistogram`) ↔ `THISTOGRAM` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| idx | PTODpsType | idx | OK |
| dst | PTODpsType | dst | OK |
| isMSB | DefaultValuedOptionalAttr\<BoolAttr, "true"\> | \<HistByte byte\> 模板参数 | **MISMATCH: ptoas 用 bool isMSB，pto-isa 用 HistByte 枚举** |

**说明:** 语义不同 — ptoas 是 bool 标志，pto-isa 是 HistByte 枚举值。

---

## 十二、Reshape / Print / Debug

### 127. TReshapeOp (`pto.treshape`) ↔ `TRESHAPE` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src | OK |
| result | PTODpsType | dst | OK |

---

### 128. TPrintOp (`pto.tprint`) ↔ `TPRINT` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | src (TileData) | OK |
| Format | **不存在** | \<Format\> 模板参数 | **MISMATCH: ptoas 缺少 Format 模板参数** |
| tmp | **不存在** | tmp (GlobalData, 部分重载) | **MISMATCH: ptoas 缺少 Optional tmp** |

---

## 十三、通信类 (pto_comm_inst.hpp)

### 129. TPutOp (`pto.comm.tput`) ↔ `comm::TPUT` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| dst | PTODpsType | dstGlobalData | OK |
| src | PTODpsType | srcGlobalData | OK |
| ping | PTODpsType | stagingTileData / pingTile | OK |
| pong | Optional\<PTODpsType\> | pongTile (double-buffer 重载) | OK |
| atomicType | DefaultValuedAttr\<PTO_AtomicTypeAttr\> | \<AtomicType\> / 运行时 AtomicType | OK |

---

### 130. TGetOp (`pto.comm.tget`) ↔ `comm::TGET` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| dst | PTODpsType | dstGlobalData | OK |
| src | PTODpsType | srcGlobalData | OK |
| ping | PTODpsType | stagingTileData / pingTile | OK |
| pong | Optional\<PTODpsType\> | pongTile | OK |

---

### 131. TNotifyOp (`pto.comm.tnotify`) ↔ `comm::TNOTIFY` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| signal | PTODpsType | dstSignalData | OK |
| value | AnySignlessInteger | int32_t value | OK |
| notifyOp | PTO_NotifyOpAttr | NotifyOp op | OK |

---

### 132. TWaitOp (`pto.comm.twait`) ↔ `comm::TWAIT` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| signal | PTODpsType | signalData | OK |
| cmpValue | AnySignlessInteger | int32_t cmpValue | OK |
| cmp | PTO_WaitCmpAttr | WaitCmp cmp | OK |

---

### 133. TTestOp (`pto.comm.ttest`) ↔ `comm::TTEST` — OK

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| signal | PTODpsType | signalData | OK |
| cmpValue | AnySignlessInteger | int32_t cmpValue | OK |
| cmp | PTO_WaitCmpAttr | WaitCmp cmp | OK |
| result | I1 | bool 返回 | OK |

---

### 134. TBroadcastOp (`pto.comm.tbroadcast`) ↔ `comm::TBROADCAST` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| src | PTODpsType | srcGlobalData | OK |
| ping | PTODpsType | stagingTileData / pingTile | OK |
| pong | Optional\<PTODpsType\> | pongTile | OK |
| group | Variadic\<PTODpsType\> | ParallelGroupType | OK (不同表达) |
| root | I32Attr | 通过 ParallelGroupType 隐含 | OK |
| CollEngine | **不存在** | \<CollEngine=AIV\> 模板参数 | **MISMATCH: ptoas 缺少 CollEngine** |

---

### 135. CommTGatherOp (`pto.comm.tgather`) ↔ `comm::TGATHER` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| dst | PTODpsType | dstGlobalData | OK |
| ping | PTODpsType | stagingTileData / pingTile | OK |
| pong | Optional\<PTODpsType\> | pongTile | OK |
| group | Variadic\<PTODpsType\> | ParallelGroupType | OK |
| root | I32Attr | root | OK |
| CollEngine | **不存在** | \<CollEngine=AIV\> 模板参数 | **MISMATCH: ptoas 缺少 CollEngine** |

---

### 136. CommTScatterOp (`pto.comm.tscatter`) ↔ `comm::TSCATTER` — MISMATCH

同 CommTGatherOp，**缺少 CollEngine**。

---

### 137. TReduceOp (`pto.comm.treduce`) ↔ `comm::TREDUCE` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| dst | PTODpsType | dstGlobalData | OK |
| acc | PTODpsType | accTileData | OK |
| recvPing | PTODpsType | recvTileData / pingTileData | OK |
| recvPong | Optional\<PTODpsType\> | pongTileData | OK |
| group | Variadic\<PTODpsType\> | ParallelGroupType | OK |
| reduceOp | PTO_ReduceOpAttr | ReduceOp op | OK |
| root | I32Attr | root | OK |
| CollEngine | **不存在** | \<CollEngine=AIV\> 模板参数 | **MISMATCH: ptoas 缺少 CollEngine** |

---

### 138. TPutAsyncOp (`pto.comm.tput_async`) ↔ `comm::TPUT_ASYNC` — MISMATCH

| 参数 | ptoas | pto-isa | 状态 |
|------|-------|---------|------|
| dst | PTODpsType | dstGlobalData | OK |
| src | PTODpsType | srcGlobalData | OK |
| session | AsyncSessionType | const AsyncSession& | OK |
| DmaEngine | **不存在** | \<DmaEngine=SDMA\> 模板参数 | **MISMATCH: ptoas 缺少 DmaEngine** |

---

### 139. TGetAsyncOp (`pto.comm.tget_async`) ↔ `comm::TGET_ASYNC` — MISMATCH

同 TPutAsyncOp，**缺少 DmaEngine**。

---

## 十四、仅 pto-isa 存在（ptoas 无对应 op）

| pto-isa 函数 | 说明 |
|-------------|------|
| `TPOW<PrecisionType>(dst, base, exp, tmp)` | 幂运算 (标记 PTO_INTERNAL) |
| `TPOWS<PrecisionType>(dst, base, scalar_exp, tmp)` | 标量幂运算 (标记 PTO_INTERNAL) |
| `TIMG2COL<FmatrixMode>(dst, convSrc, posM, posK)` | 卷积 img2col |
| `SETFMATRIX<FmatrixMode>(convSrc)` | 卷积矩阵配置 |
| `SET_IMG2COL_RPT(convSrc)` | 卷积 repeat 配置 |
| `SET_IMG2COL_PADDING(convSrc)` | 卷积 padding 配置 |
| `TSUBVIEW(dst, src, rowIdx, colIdx)` | 子视图（ptoas 有 SubViewOp 但语义不同） |

---

## 十五、仅 ptoas 存在（pto-isa 无对应函数）

这些是 ptoas 前端/IR 辅助 op，不直接映射到 pto-isa 硬件指令：

| ptoas Op | 说明 |
|----------|------|
| AddPtrOp, LoadScalarOp, StoreScalarOp | 指针/标量操作 |
| MakeTensorViewOp, PartitionViewOp, GetTensorViewDimOp | 视图构建 |
| AllocTileOp, BindTileOp, MaterializeTileOp, SubViewOp, SetValidShapeOp, BitcastOp | Tile 管理 |
| PointerCastOp | 地址转换 |
| GetBlockIdxOp, GetSubBlockIdxOp, GetBlockNumOp, GetSubBlockNumOp | 系统查询 |
| RecordEventOp, WaitEventOp, BarrierSyncOp | 高级同步 |
| SectionCubeOp, SectionVectorOp | 代码 section |
| AicInitializePipeOp, AivInitializePipeOp, TAllocToAiv/AicOp, TPushToAiv/AicOp, TPopFromAic/AivOp, TFreeFromAic/AivOp | 前端 Pipe 通信 |
| ReserveBufferOp, ImportReservedBufferOp | Buffer 管理 |
| BuildAsyncSessionOp, WaitAsyncEventOp, TestAsyncEventOp | Async session |
| InitializeL2G2LPipeOp, InitializeL2LPipeOp | Pipe 初始化 |
| TPushOp, TPopOp, TAllocOp, TFreeOp | 统一 Pipe 操作 |
| DeclareTileOp, DeclareGlobalOp, TAssignOp | 声明/绑定 |
| DeclareEventIdArrayOp, EventIdArrayGetOp, EventIdArraySetOp | 事件数组 |
| DeclareLocalArrayOp, LocalArrayGetOp, LocalArraySetOp | 局部数组 |
| DeclareTileMemRefOp | 内部 lowering |
| SetFlagOp, WaitFlagOp, SetFlagDynOp, WaitFlagDynOp | 低级同步 |
| GetBufOp, RlsBufOp | A5 buffer-id 同步 |
| SyncSetOp, SyncWaitOp, BarrierOp, TSyncOp | 同步原语 |
| SetFFTsOp, PrintOp, TrapOp | 配置/调试 |

---

## 总结：不对应项汇总

### 类别 A — ptoas 缺少 pto-isa 模板参数（属性）

| 缺失属性 | 影响的 ptoas Ops |
|----------|-----------------|
| **AccPhase** | TMatmulOp, TMatmulAccOp, TMatmulBiasOp, TMatmulMxOp, TMatmulMxAccOp, TMatmulMxBiasOp, TGemvOp, TGemvAccOp, TGemvBiasOp, TGemvMxOp, TGemvMxAccOp, TGemvMxBiasOp (共12个) |
| **PrecisionType** | TDivOp (DivAlgorithm), TDivSOp, TFModOp (FmodAlgorithm), TRemOp (RemAlgorithm), TExpOp (ExpAlgorithm), TLogOp (LogAlgorithm), TSqrtOp (SqrtAlgorithm), TRsqrtOp (RsqrtAlgorithm), TRecipOp (RecipAlgorithm), TRowExpandDivOp, TColExpandDivOp (共11个) |
| **CollEngine** | TBroadcastOp, CommTGatherOp, CommTScatterOp, TReduceOp (共4个) |
| **DmaEngine** | TPutAsyncOp, TGetAsyncOp (共2个) |

### 类别 B — ptoas 缺少 pto-isa 重载中的可选参数

| 缺失参数 | 影响的 ptoas Op |
|----------|----------------|
| **ReluPreMode, AccToVecMode, preQuantScalar** | TExtractOp (3个参数缺失) |
| **ReluPreMode, AccToVecMode, preQuantScalar, FpTileData, TInsertMode** | TInsertOp (5个参数缺失) |
| **AccToVecMode** | TExtractFPOp, TInsertFPOp |
| **tmp (Optional)** | TRowExpandAddOp |
| **Format, tmp** | TPrintOp |
| **Coalesce** | MGatherOp |
| **Coalesce, ScatterConflict** | MScatterOp |
| **A5 quant params (max, scaling, exp_zz, store_mode, QuantScaleAlg)** | TQuantOp |

### 类别 C — ptoas 多出 pto-isa 中不存在的参数

| 多出参数 | 影响的 ptoas Op |
|----------|----------------|
| **pad_mode, pad_value, left_padding_num, right_padding_num, init_out_buffer, init_condition** | TLoadOp (6个参数多出) |

### 类别 D — 参数类型/结构差异

| 差异描述 | 影响的 ptoas Op |
|----------|----------------|
| **key/counter 拆分为 6 标量 vs pto-isa 聚合类型** | TRandomOp |
| **isMSB (bool) vs HistByte (枚举)** | THistogramOp |

### 类别 E — pto-isa 独有函数（ptoas 未覆盖）

| 函数 | 说明 |
|------|------|
| TPOW / TPOWS | 幂运算 (PTO_INTERNAL) |
| TIMG2COL / SETFMATRIX / SET_IMG2COL_RPT / SET_IMG2COL_PADDING | 卷积支持 |
| TSUBVIEW | 子视图 |
