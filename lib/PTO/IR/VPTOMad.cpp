// Copyright (c) 2026 Huawei Technologies Co., Ltd.
// This program is free software, you can redistribute it and/or modify it under the terms and conditions of
// CANN Open Software License Agreement Version 2.0 (the "License").
// Please refer to the License for details. You may not use this file except in compliance with the License.
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
// See LICENSE in the root of the software repository for the full text of the License.

//===- VPTOMad.cpp - VPTO Mad ops -------------------------------------------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//

#include "VPTOInternal.h"

#include "mlir/IR/BuiltinAttributes.h"
#include "llvm/ADT/STLExtras.h"

using namespace mlir;
using namespace mlir::pto;

void MadOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), &getLhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getRhsMutable());
  effects.emplace_back(MemoryEffects::Write::get(), &getDstMutable());
  if (getAccInitValue()) {
    // Runtime acc-init may select the accumulate path (flag 0), which reads
    // the existing accumulator; model dst as read+write conservatively.
    effects.emplace_back(MemoryEffects::Read::get(), &getDstMutable());
  }
}

static LogicalResult verifyMadPointerKinds(Operation *op, Type lhsTy, Type rhsTy,
                                           Type dstTy,
                                           std::optional<Type> biasTy =
                                               std::nullopt) {
  auto lhsType = dyn_cast<pto::PtrType>(lhsTy);
  auto rhsType = dyn_cast<pto::PtrType>(rhsTy);
  auto dstType = dyn_cast<pto::PtrType>(dstTy);
  if (!lhsType || !rhsType || !dstType) {
    return op->emitOpError("requires typed !pto.ptr lhs/rhs/dst operands");
  }

  const auto lhsAS = lhsType.getMemorySpace().getAddressSpace();
  const auto rhsAS = rhsType.getMemorySpace().getAddressSpace();
  const auto dstAS = dstType.getMemorySpace().getAddressSpace();

  const bool isStrongCube =
      lhsAS == pto::AddressSpace::LEFT && rhsAS == pto::AddressSpace::RIGHT &&
      dstAS == pto::AddressSpace::ACC;
  if (!isStrongCube) {
    return op->emitOpError("requires l0a/l0b/l0c-typed lhs/rhs/dst pointers");
  }

  if (!biasTy) {
    return success();
  }

  auto biasType = dyn_cast<pto::PtrType>(*biasTy);
  if (!biasType) {
    return op->emitOpError("requires typed !pto.ptr bias operand");
  }
  if (biasType.getMemorySpace().getAddressSpace() != pto::AddressSpace::BIAS) {
    return op->emitOpError("requires bias pointer in !pto.ptr<..., bt>");
  }
  if (biasType.getElementType() != dstType.getElementType()) {
    return op->emitOpError("requires bias element type to match dst element type");
  }
  return success();
}

void MadAccOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), &getLhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getRhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getDstMutable());
  effects.emplace_back(MemoryEffects::Write::get(), &getDstMutable());
}

void MadBiasOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), &getLhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getRhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getBiasMutable());
  effects.emplace_back(MemoryEffects::Write::get(), &getDstMutable());
}

static LogicalResult verifyMadMxCommon(Operation *op, Type lhsTy, Type rhsTy,
                                       Type dstTy,
                                       std::optional<Type> biasTy =
                                           std::nullopt) {
  if (failed(verifyMadPointerKinds(op, lhsTy, rhsTy, dstTy, biasTy))) {
    return failure();
  }

  auto lhsType = cast<pto::PtrType>(lhsTy);
  auto rhsType = cast<pto::PtrType>(rhsTy);
  auto dstType = cast<pto::PtrType>(dstTy);
  const auto lhsAS = lhsType.getMemorySpace().getAddressSpace();
  const auto rhsAS = rhsType.getMemorySpace().getAddressSpace();
  const auto dstAS = dstType.getMemorySpace().getAddressSpace();
  const bool isStrongCube =
      lhsAS == pto::AddressSpace::LEFT && rhsAS == pto::AddressSpace::RIGHT &&
      dstAS == pto::AddressSpace::ACC;
  if (!isStrongCube) {
    return op->emitOpError("requires l0a/l0b/l0c-typed lhs/rhs/dst pointers");
  }

  if (!isMxElementType(lhsType.getElementType()) ||
      !isMxElementType(rhsType.getElementType())) {
    return op->emitOpError(
        "requires MX lhs/rhs element types (f8E4M3FN, f8E5M2, f4E1M2x2, or "
        "f4E2M1x2)");
  }
  return success();
}

void MadMxOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), &getLhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getRhsMutable());
  effects.emplace_back(MemoryEffects::Write::get(), &getDstMutable());
}

void MadMxAccOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), &getLhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getRhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getDstMutable());
  effects.emplace_back(MemoryEffects::Write::get(), &getDstMutable());
}

void MadMxBiasOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), &getLhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getRhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getBiasMutable());
  effects.emplace_back(MemoryEffects::Write::get(), &getDstMutable());
}

static std::optional<pto::MadUnitFlagMode>
parseMadUnitFlagModeToken(StringRef token) {
  if (token == "check_only") {
    return pto::MadUnitFlagMode::CheckOnly;
  }
  if (token == "check_and_set") {
    return pto::MadUnitFlagMode::CheckAndSet;
  }
  return std::nullopt;
}

static StringRef stringifyMadUnitFlagModeToken(pto::MadUnitFlagMode mode) {
  switch (mode) {
  case pto::MadUnitFlagMode::CheckOnly:
    return "check_only";
  case pto::MadUnitFlagMode::CheckAndSet:
    return "check_and_set";
  }
  llvm_unreachable("unexpected mad unit flag mode");
}

static std::optional<pto::Tf32Mode> parseTf32ModeToken(StringRef token) {
  if (token == "round_even") {
    return pto::Tf32Mode::RoundEven;
  }
  if (token == "round_away") {
    return pto::Tf32Mode::RoundAway;
  }
  return std::nullopt;
}

static StringRef stringifyTf32ModeToken(pto::Tf32Mode mode) {
  switch (mode) {
  case pto::Tf32Mode::RoundEven:
    return "round_even";
  case pto::Tf32Mode::RoundAway:
    return "round_away";
  }
  llvm_unreachable("unexpected tf32 mode");
}

static StringRef stringifyMadSatModeToken(pto::MadSatMode mode) {
  switch (mode) {
  case pto::MadSatMode::Sat:
    return "sat";
  case pto::MadSatMode::NoSat:
    return "nosat";
  }
  llvm_unreachable("unexpected mad sat mode");
}

static LogicalResult verifyMadSemanticClauses(Operation *op, Type lhsTy,
                                              Type rhsTy, Type dstTy,
                                              std::optional<Type> biasTy,
                                              std::optional<pto::Tf32Mode> tf32Mode,
                                              std::optional<pto::MadSatMode> satMode,
                                              bool hasNDir) {
  if (failed(verifyMadPointerKinds(op, lhsTy, rhsTy, dstTy, biasTy))) {
    return failure();
  }

  auto lhsType = dyn_cast<pto::PtrType>(lhsTy);
  auto rhsType = dyn_cast<pto::PtrType>(rhsTy);
  auto dstType = dyn_cast<pto::PtrType>(dstTy);
  if (!lhsType || !rhsType || !dstType) {
    return op->emitOpError("requires typed !pto.ptr lhs/rhs/dst operands");
  }

  if (tf32Mode) {
    if (!(lhsType.getElementType().isF32() && rhsType.getElementType().isF32() &&
          dstType.getElementType().isF32())) {
      return op->emitOpError(
          "requires tf32_mode only for f32 lhs/rhs/dst element types");
    }
  }
  if (pto::isPTOHiFloat8Type(lhsType.getElementType()) !=
      pto::isPTOHiFloat8Type(rhsType.getElementType())) {
    return op->emitOpError(
        "requires lhs/rhs to both use hif8 or both use non-hif8 element types");
  }
  if (satMode) {
    auto isFloatLike = [](Type type) {
      if (isa<FloatType>(type)) {
        return true;
      }
      return pto::isPTOLowPrecisionType(type);
    };
    if (!(isFloatLike(lhsType.getElementType()) &&
          isFloatLike(rhsType.getElementType()) &&
          isFloatLike(dstType.getElementType()))) {
      return op->emitOpError(
          "requires sat/nosat only for floating lhs/rhs/dst element types");
    }
  }
  (void)hasNDir;
  return success();
}

static ParseResult parseMadSemanticClauses(OpAsmParser &parser,
                                       NamedAttrList &attrs,
                                       bool parseTf32ModeClause) {
  StringRef unitFlagKeyword;
  if (failed(parser.parseOptionalKeyword("unit_flag"))) {
    /* no unit_flag clause */
  } else {
    if (parser.parseLParen() || parser.parseKeyword(&unitFlagKeyword) ||
        parser.parseRParen()) {
      return failure();
    }
    auto mode = parseMadUnitFlagModeToken(unitFlagKeyword);
    if (!mode) {
      return parser.emitError(parser.getCurrentLocation())
             << "expected unit_flag(check_only|check_and_set)";
    }
    attrs.set("unit_flag_mode",
              pto::MadUnitFlagModeAttr::get(parser.getContext(), *mode));
  }
  if (succeeded(parser.parseOptionalKeyword("disable_gemv"))) {
    attrs.set("disable_gemv", UnitAttr::get(parser.getContext()));
  }
  if (succeeded(parser.parseOptionalKeyword("sat"))) {
    attrs.set("sat_mode",
              pto::MadSatModeAttr::get(parser.getContext(),
                                       pto::MadSatMode::Sat));
  } else if (succeeded(parser.parseOptionalKeyword("nosat"))) {
    attrs.set("sat_mode",
              pto::MadSatModeAttr::get(parser.getContext(),
                                       pto::MadSatMode::NoSat));
  }
  if (parseTf32ModeClause &&
      succeeded(parser.parseOptionalKeyword("tf32_mode"))) {
    StringRef tf32Keyword;
    if (parser.parseLParen() || parser.parseKeyword(&tf32Keyword) ||
        parser.parseRParen()) {
      return failure();
    }
    auto mode = parseTf32ModeToken(tf32Keyword);
    if (!mode) {
      return parser.emitError(parser.getCurrentLocation())
             << "expected tf32_mode(round_even|round_away)";
    }
    attrs.set("tf32_mode", pto::Tf32ModeAttr::get(parser.getContext(), *mode));
  }
  if (succeeded(parser.parseOptionalKeyword("n_dir"))) {
    attrs.set("n_dir", UnitAttr::get(parser.getContext()));
  }
  return success();
}

static ParseResult parseMadSemanticTypes(OpAsmParser &parser, bool hasBias,
                                         Type &lhsType, Type &rhsType,
                                         Type &dstType, Type &biasType,
                                         Type &mType, Type &nType,
                                         Type &kType,
                                         bool hasUnitFlagValue,
                                         bool hasAccInitValue,
                                         bool hasDisableGemvValue,
                                         bool hasBiasInitValue,
                                         Type &unitFlagType, Type &accInitType,
                                         Type &disableGemvType,
                                         Type &biasInitType) {
  if (parser.parseType(lhsType) || parser.parseComma() ||
      parser.parseType(rhsType) || parser.parseComma() ||
      parser.parseType(dstType) || parser.parseComma()) {
    return failure();
  }
  if (hasBias) {
    if (parser.parseType(biasType) || parser.parseComma()) {
      return failure();
    }
  }
  if (parser.parseType(mType) || parser.parseComma() ||
      parser.parseType(nType) || parser.parseComma() ||
      parser.parseType(kType)) {
    return failure();
  }
  // Runtime flag operand types trail the shape types, in operand order. The
  // concrete width/signedness is whatever the frontend produced (i1/i32/si32),
  // so parse them generically and let the op verifier enforce the contract.
  auto parseFlagType = [&](Type &out) {
    return failure(parser.parseComma() || parser.parseType(out));
  };
  if (hasUnitFlagValue && failed(parseFlagType(unitFlagType))) {
    return failure();
  }
  if (hasAccInitValue && failed(parseFlagType(accInitType))) {
    return failure();
  }
  if (hasDisableGemvValue && failed(parseFlagType(disableGemvType))) {
    return failure();
  }
  if (hasBiasInitValue && failed(parseFlagType(biasInitType))) {
    return failure();
  }
  return success();
}

static ParseResult resolveMadSemanticOperands(
    OpAsmParser &parser, OperationState &result, bool hasBias,
    OpAsmParser::UnresolvedOperand lhs, Type lhsType,
    OpAsmParser::UnresolvedOperand rhs, Type rhsType,
    OpAsmParser::UnresolvedOperand dst, Type dstType,
    OpAsmParser::UnresolvedOperand bias, Type biasType,
    OpAsmParser::UnresolvedOperand m, Type mType,
    OpAsmParser::UnresolvedOperand n, Type nType,
    OpAsmParser::UnresolvedOperand k, Type kType) {
  if (parser.resolveOperand(lhs, lhsType, result.operands) ||
      parser.resolveOperand(rhs, rhsType, result.operands) ||
      parser.resolveOperand(dst, dstType, result.operands)) {
    return failure();
  }
  if (hasBias) {
    if (parser.resolveOperand(bias, biasType, result.operands)) {
      return failure();
    }
  }
  if (parser.resolveOperand(m, mType, result.operands) ||
      parser.resolveOperand(n, nType, result.operands) ||
      parser.resolveOperand(k, kType, result.operands)) {
    return failure();
  }
  return success();
}

template <typename OpT>
[[maybe_unused]] static ParseResult parseMadSemanticOpCommon(OpAsmParser &parser,
                                            OperationState &result,
                                            bool hasBias,
                                            bool parseTf32ModeClause) {
  OpAsmParser::UnresolvedOperand lhs, rhs, dst, bias;
  OpAsmParser::UnresolvedOperand m, n, k;
  if (parseRequiredOperandWithComma(parser, lhs) ||
      parseRequiredOperandWithComma(parser, rhs) ||
      parseRequiredOperandWithComma(parser, dst) ||
      (hasBias && parseRequiredOperandWithComma(parser, bias)) ||
      parseRequiredOperandWithComma(parser, m) ||
      parseRequiredOperandWithComma(parser, n) ||
      parser.parseOperand(k)) {
    return failure();
  }
  OpAsmParser::UnresolvedOperand unitFlagValue, accInitValue,
      disableGemvValue, biasInitValue;
  bool hasUnitFlagValue = false, hasAccInitValue = false,
       hasDisableGemvValue = false, hasBiasInitValue = false;
  if (succeeded(parser.parseOptionalKeyword("unit_flag_value"))) {
    hasUnitFlagValue = true;
    if (parser.parseLParen() || parser.parseOperand(unitFlagValue) ||
        parser.parseRParen()) {
      return failure();
    }
  }
  if (succeeded(parser.parseOptionalKeyword("acc_init"))) {
    hasAccInitValue = true;
    if (parser.parseLParen() || parser.parseOperand(accInitValue) ||
        parser.parseRParen()) {
      return failure();
    }
  }
  if (succeeded(parser.parseOptionalKeyword("disable_gemv_value"))) {
    hasDisableGemvValue = true;
    if (parser.parseLParen() || parser.parseOperand(disableGemvValue) ||
        parser.parseRParen()) {
      return failure();
    }
  }
  if (succeeded(parser.parseOptionalKeyword("bias_init"))) {
    hasBiasInitValue = true;
    if (parser.parseLParen() || parser.parseOperand(biasInitValue) ||
        parser.parseRParen()) {
      return failure();
    }
  }
  NamedAttrList attrs;
  if (failed(parseMadSemanticClauses(parser, attrs, parseTf32ModeClause))) {
    return failure();
  }
  if (parser.parseOptionalAttrDict(attrs) || parser.parseColon()) {
    return failure();
  }
  Type lhsType, rhsType, dstType, mType, nType, kType, biasType;
  Type unitFlagType, accInitType, disableGemvType, biasInitType;
  if (failed(parseMadSemanticTypes(parser, hasBias, lhsType, rhsType, dstType,
                                   biasType, mType, nType, kType,
                                   hasUnitFlagValue, hasAccInitValue,
                                   hasDisableGemvValue, hasBiasInitValue,
                                   unitFlagType, accInitType, disableGemvType,
                                   biasInitType))) {
    return failure();
  }
  Builder &builder = parser.getBuilder();
  Type i32Type = builder.getI32Type();
  Type i1Type = builder.getI1Type();
  (void)i32Type;
  (void)i1Type;
  result.addAttributes(attrs);
  {
    // AttrSizedOperandSegments is mandatory once multiple Optional variadic
    // operands exist. Lit/ptodsl inputs usually omit it; prefill the canonical
    // segment sizes ([1]*fixed + optional-flags-present flags) when absent.
    constexpr int kFlagKinds = 4;
    if (!result.attributes.get("operandSegmentSizes")) {
      int fixedCount = (hasBias ? 4 : 3) + 3;
      llvm::SmallVector<int32_t, 10> sizes(fixedCount, 1);
      sizes.push_back(hasUnitFlagValue ? 1 : 0);
      sizes.push_back(hasAccInitValue ? 1 : 0);
      sizes.push_back(hasDisableGemvValue ? 1 : 0);
      sizes.push_back(hasBiasInitValue ? 1 : 0);
      (void)kFlagKinds;
      result.addAttribute(
          "operandSegmentSizes",
          mlir::DenseI32ArrayAttr::get(parser.getContext(), sizes));
    }
  }
  if (failed(resolveMadSemanticOperands(parser, result, hasBias, lhs, lhsType,
                                        rhs, rhsType, dst, dstType, bias,
                                        biasType, m, mType, n, nType, k,
                                        kType))) {
    return failure();
  }
  // Resolve the runtime flag operands in operand order.
  if (hasUnitFlagValue &&
      parser.resolveOperand(unitFlagValue, unitFlagType, result.operands)) {
    return failure();
  }
  if (hasAccInitValue &&
      parser.resolveOperand(accInitValue, accInitType, result.operands)) {
    return failure();
  }
  if (hasDisableGemvValue &&
      parser.resolveOperand(disableGemvValue, disableGemvType,
                            result.operands)) {
    return failure();
  }
  if (hasBiasInitValue &&
      parser.resolveOperand(biasInitValue, biasInitType, result.operands)) {
    return failure();
  }
  return success();
}

static void printMadSemanticClauses(OpAsmPrinter &printer, Operation *op,
                                    bool allowTf32Mode) {
  if (auto unitFlagMode = op->getAttrOfType<pto::MadUnitFlagModeAttr>(
          "unit_flag_mode")) {
    printer << " unit_flag("
            << stringifyMadUnitFlagModeToken(unitFlagMode.getValue()) << ")";
  }
  if (op->hasAttr("disable_gemv")) {
    printer << " disable_gemv";
  }
  if (auto satMode = op->getAttrOfType<pto::MadSatModeAttr>("sat_mode")) {
    printer << ' ' << stringifyMadSatModeToken(satMode.getValue());
  }
  if (allowTf32Mode) {
    if (auto tf32Mode = op->getAttrOfType<pto::Tf32ModeAttr>("tf32_mode")) {
      printer << " tf32_mode(" << stringifyTf32ModeToken(tf32Mode.getValue())
              << ")";
    }
  }
  if (op->hasAttr("n_dir")) {
    printer << " n_dir";
  }
}

template <typename OpT>
static void printMadRuntimeFlagClauses(OpAsmPrinter &printer, OpT op) {
  // All mad-family ops carry the optional runtime flag operands.
  if constexpr (std::is_same_v<OpT, OpT>) {
    if (auto uf = op.getUnitFlagValue()) {
      printer << " unit_flag_value(" << uf << ")";
    }
    if (auto acc = op.getAccInitValue()) {
      printer << " acc_init(" << acc << ")";
    }
    if (auto gemv = op.getDisableGemvValue()) {
      printer << " disable_gemv_value(" << gemv << ")";
    }
    if (auto bias = op.getBiasInitValue()) {
      printer << " bias_init(" << bias << ")";
    }
  } else {
    (void)op;
  }
}

static ArrayRef<StringRef> getMadSemanticElidedAttrs(bool allowTf32Mode) {
  static constexpr StringRef kWithTf32[] = {"unit_flag_mode", "disable_gemv",
                                            "sat_mode", "tf32_mode", "n_dir"};
  static constexpr StringRef kWithoutTf32[] = {"unit_flag_mode",
                                               "disable_gemv", "sat_mode",
                                               "n_dir"};
  return allowTf32Mode ? ArrayRef<StringRef>(kWithTf32)
                       : ArrayRef<StringRef>(kWithoutTf32);
}

template <typename OpT>
static void printMadOperandSegmentSizesIfNeeded(OpAsmPrinter &printer, OpT op) {
  // When any runtime flag operand is present and the op does not already
  // carry operandSegmentSizes, emit the canonical attribute so the printed
  // form always re-parses (AttrSizedOperandSegments is mandatory).
  if (op->getAttr("operandSegmentSizes")) {
    return;
  }
  bool any = op.getUnitFlagValue() || op.getAccInitValue() ||
             op.getDisableGemvValue() || op.getBiasInitValue();
  if (!any) {
    return;
  }
  llvm::SmallVector<int32_t, 10> sizes;
  auto push = [&sizes](Value v) { sizes.push_back(v ? 1 : 0); };
  push(op.getLhs());
  push(op.getRhs());
  push(op.getDst());
  if constexpr (std::is_same_v<OpT, MadBiasOp> ||
                std::is_same_v<OpT, MadMxBiasOp>) {
    push(op.getBias());
  }
  push(op.getM());
  push(op.getN());
  push(op.getK());
  push(op.getUnitFlagValue());
  push(op.getAccInitValue());
  push(op.getDisableGemvValue());
  push(op.getBiasInitValue());
  printer << " {operandSegmentSizes = array<i32:";
  llvm::interleave(sizes, printer, [&printer](int32_t s) { printer << " " << s; }, ",");
  printer << "}";
}

template <typename OpT>
static void appendMadRuntimeFlagTypes(OpAsmPrinter &printer, OpT op) {
  if constexpr (std::is_same_v<OpT, OpT>) {
    if (auto uf = op.getUnitFlagValue()) {
      printer << ", " << uf.getType();
    }
    if (auto acc = op.getAccInitValue()) {
      printer << ", " << acc.getType();
    }
    if (auto gemv = op.getDisableGemvValue()) {
      printer << ", " << gemv.getType();
    }
    if (auto bias = op.getBiasInitValue()) {
      printer << ", " << bias.getType();
    }
  } else {
    (void)op;
  }
}

template <typename OpT>
static void printMadSemanticOpNoBias(OpAsmPrinter &printer, OpT op,
                                     bool allowTf32Mode) {
  printer << ' ' << op.getLhs() << ", " << op.getRhs() << ", " << op.getDst()
          << ", " << op.getM() << ", " << op.getN() << ", " << op.getK();
  printMadRuntimeFlagClauses(printer, op);
  printMadSemanticClauses(printer, op, allowTf32Mode);
  printMadOperandSegmentSizesIfNeeded(printer, op);
  printer.printOptionalAttrDict(op->getAttrs(),
                                getMadSemanticElidedAttrs(allowTf32Mode));
  printer << " : " << op.getLhs().getType() << ", " << op.getRhs().getType()
          << ", " << op.getDst().getType() << ", " << op.getM().getType()
          << ", " << op.getN().getType() << ", " << op.getK().getType();
  appendMadRuntimeFlagTypes(printer, op);
}

template <typename OpT>
static void printMadSemanticOpWithBias(OpAsmPrinter &printer, OpT op,
                                       bool allowTf32Mode) {
  printer << ' ' << op.getLhs() << ", " << op.getRhs() << ", " << op.getDst()
          << ", " << op.getBias() << ", " << op.getM() << ", " << op.getN()
          << ", " << op.getK();
  printMadRuntimeFlagClauses(printer, op);
  printMadSemanticClauses(printer, op, allowTf32Mode);
  printMadOperandSegmentSizesIfNeeded(printer, op);
  printer.printOptionalAttrDict(op->getAttrs(),
                                getMadSemanticElidedAttrs(allowTf32Mode));
  printer << " : " << op.getLhs().getType() << ", " << op.getRhs().getType()
          << ", " << op.getDst().getType() << ", " << op.getBias().getType()
          << ", " << op.getM().getType() << ", " << op.getN().getType()
          << ", " << op.getK().getType();
  appendMadRuntimeFlagTypes(printer, op);
}

// Batch7: Mad 家族共用 tf32_mode 读取 + 语义校验
template <typename OpTy>
static LogicalResult verifyMadSemanticWithTf32(OpTy op) {
  std::optional<pto::Tf32Mode> tf32Mode;
  if (auto tf32ModeAttr =
          op->template getAttrOfType<pto::Tf32ModeAttr>("tf32_mode")) {
    tf32Mode = tf32ModeAttr.getValue();
  }
  return verifyMadSemanticClauses(op, op.getLhs().getType(), op.getRhs().getType(),
                                  op.getDst().getType(), std::nullopt, tf32Mode,
                                  op.getSatMode(),
                                  op->hasAttr("n_dir"));
}

LogicalResult MadOp::verify() { return verifyMadSemanticWithTf32(*this); }

ParseResult MadOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseMadSemanticOpCommon<MadOp>(parser, result, /*hasBias=*/false,
                                         /*parseTf32ModeClause=*/true);
}

void MadOp::print(OpAsmPrinter &printer) {
  printMadSemanticOpNoBias(printer, *this, /*allowTf32Mode=*/true);
}

bool MadOp::isMadMxFamily() { return false; }
bool MadOp::hasBiasOperand() { return false; }
bool MadOp::readsAccumulator() { return false; }
bool MadOp::supportsTf32Mode() { return true; }
Value MadOp::getBiasOrNull() { return {}; }

LogicalResult MadAccOp::verify() { return verifyMadSemanticWithTf32(*this); }

ParseResult MadAccOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseMadSemanticOpCommon<MadAccOp>(parser, result, /*hasBias=*/false,
                                            /*parseTf32ModeClause=*/true);
}

void MadAccOp::print(OpAsmPrinter &printer) {
  printMadSemanticOpNoBias(printer, *this, /*allowTf32Mode=*/true);
}

bool MadAccOp::isMadMxFamily() { return false; }
bool MadAccOp::hasBiasOperand() { return false; }
bool MadAccOp::readsAccumulator() { return true; }
bool MadAccOp::supportsTf32Mode() { return true; }
Value MadAccOp::getBiasOrNull() { return {}; }

LogicalResult MadBiasOp::verify() {
  std::optional<pto::Tf32Mode> tf32Mode;
  if (auto tf32ModeAttr =
          (*this)->getAttrOfType<pto::Tf32ModeAttr>("tf32_mode")) {
    tf32Mode = tf32ModeAttr.getValue();
  }
  return verifyMadSemanticClauses(*this, getLhs().getType(), getRhs().getType(),
                                  getDst().getType(), getBias().getType(),
                                  tf32Mode, getSatMode(),
                                  (*this)->hasAttr("n_dir"));
}

ParseResult MadBiasOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseMadSemanticOpCommon<MadBiasOp>(parser, result, /*hasBias=*/true,
                                             /*parseTf32ModeClause=*/true);
}

void MadBiasOp::print(OpAsmPrinter &printer) {
  printMadSemanticOpWithBias(printer, *this, /*allowTf32Mode=*/true);
}

bool MadBiasOp::isMadMxFamily() { return false; }
bool MadBiasOp::hasBiasOperand() { return true; }
bool MadBiasOp::readsAccumulator() { return false; }
bool MadBiasOp::supportsTf32Mode() { return true; }
Value MadBiasOp::getBiasOrNull() { return getBias(); }

LogicalResult MadMxOp::verify() {
  if (failed(verifyMadMxCommon(*this, getLhs().getType(), getRhs().getType(),
                               getDst().getType()))) {
    return failure();
  }
  return verifyMadSemanticClauses(*this, getLhs().getType(), getRhs().getType(),
                                  getDst().getType(), std::nullopt, std::nullopt,
                                  getSatMode(),
                                  (*this)->hasAttr("n_dir"));
}

ParseResult MadMxOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseMadSemanticOpCommon<MadMxOp>(parser, result, /*hasBias=*/false,
                                           /*parseTf32ModeClause=*/false);
}

void MadMxOp::print(OpAsmPrinter &printer) {
  printMadSemanticOpNoBias(printer, *this, /*allowTf32Mode=*/false);
}

bool MadMxOp::isMadMxFamily() { return true; }
bool MadMxOp::hasBiasOperand() { return false; }
bool MadMxOp::readsAccumulator() { return false; }
bool MadMxOp::supportsTf32Mode() { return false; }
Value MadMxOp::getBiasOrNull() { return {}; }
Attribute MadMxOp::getTf32ModeAttr() { return {}; }

LogicalResult MadMxAccOp::verify() {
  if (failed(verifyMadMxCommon(*this, getLhs().getType(), getRhs().getType(),
                               getDst().getType()))) {
    return failure();
  }
  return verifyMadSemanticClauses(*this, getLhs().getType(), getRhs().getType(),
                                  getDst().getType(), std::nullopt, std::nullopt,
                                  getSatMode(),
                                  (*this)->hasAttr("n_dir"));
}

ParseResult MadMxAccOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseMadSemanticOpCommon<MadMxAccOp>(parser, result, /*hasBias=*/false,
                                              /*parseTf32ModeClause=*/false);
}

void MadMxAccOp::print(OpAsmPrinter &printer) {
  printMadSemanticOpNoBias(printer, *this, /*allowTf32Mode=*/false);
}

bool MadMxAccOp::isMadMxFamily() { return true; }
bool MadMxAccOp::hasBiasOperand() { return false; }
bool MadMxAccOp::readsAccumulator() { return true; }
bool MadMxAccOp::supportsTf32Mode() { return false; }
Value MadMxAccOp::getBiasOrNull() { return {}; }
Attribute MadMxAccOp::getTf32ModeAttr() { return {}; }

LogicalResult MadMxBiasOp::verify() {
  if (failed(verifyMadMxCommon(*this, getLhs().getType(), getRhs().getType(),
                               getDst().getType(), getBias().getType()))) {
    return failure();
  }
  return verifyMadSemanticClauses(*this, getLhs().getType(), getRhs().getType(),
                                  getDst().getType(), getBias().getType(),
                                  std::nullopt, getSatMode(),
                                  (*this)->hasAttr("n_dir"));
}

ParseResult MadMxBiasOp::parse(OpAsmParser &parser, OperationState &result) {
  return parseMadSemanticOpCommon<MadMxBiasOp>(parser, result, /*hasBias=*/true,
                                               /*parseTf32ModeClause=*/false);
}

void MadMxBiasOp::print(OpAsmPrinter &printer) {
  printMadSemanticOpWithBias(printer, *this, /*allowTf32Mode=*/false);
}

bool MadMxBiasOp::isMadMxFamily() { return true; }
bool MadMxBiasOp::hasBiasOperand() { return true; }
bool MadMxBiasOp::readsAccumulator() { return false; }
bool MadMxBiasOp::supportsTf32Mode() { return false; }
Value MadMxBiasOp::getBiasOrNull() { return getBias(); }
Attribute MadMxBiasOp::getTf32ModeAttr() { return {}; }

void MadRawOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), &getLhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getRhsMutable());
  effects.emplace_back(MemoryEffects::Write::get(), &getDstMutable());
}

LogicalResult MadRawOp::verify() {
  return verifyMadPointerKinds(*this, getLhs().getType(), getRhs().getType(),
                               getDst().getType());
}

bool MadRawOp::isMadMxFamily() { return false; }
bool MadRawOp::hasBiasOperand() { return false; }
Value MadRawOp::getBiasOrNull() { return {}; }

void MadBiasRawOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), &getLhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getRhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getBiasMutable());
  effects.emplace_back(MemoryEffects::Write::get(), &getDstMutable());
}

LogicalResult MadBiasRawOp::verify() {
  return verifyMadPointerKinds(*this, getLhs().getType(), getRhs().getType(),
                               getDst().getType(), getBias().getType());
}

bool MadBiasRawOp::isMadMxFamily() { return false; }
bool MadBiasRawOp::hasBiasOperand() { return true; }
Value MadBiasRawOp::getBiasOrNull() { return getBias(); }

void MadMxRawOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), &getLhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getRhsMutable());
  effects.emplace_back(MemoryEffects::Write::get(), &getDstMutable());
}

LogicalResult MadMxRawOp::verify() {
  return verifyMadMxCommon(*this, getLhs().getType(), getRhs().getType(),
                           getDst().getType());
}

bool MadMxRawOp::isMadMxFamily() { return true; }
bool MadMxRawOp::hasBiasOperand() { return false; }
Value MadMxRawOp::getBiasOrNull() { return {}; }

void MadMxBiasRawOp::getEffects(
    SmallVectorImpl<SideEffects::EffectInstance<MemoryEffects::Effect>>
        &effects) {
  effects.emplace_back(MemoryEffects::Read::get(), &getLhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getRhsMutable());
  effects.emplace_back(MemoryEffects::Read::get(), &getBiasMutable());
  effects.emplace_back(MemoryEffects::Write::get(), &getDstMutable());
}

LogicalResult MadMxBiasRawOp::verify() {
  return verifyMadMxCommon(*this, getLhs().getType(), getRhs().getType(),
                           getDst().getType(), getBias().getType());
}

bool MadMxBiasRawOp::isMadMxFamily() { return true; }
bool MadMxBiasRawOp::hasBiasOperand() { return true; }
Value MadMxBiasRawOp::getBiasOrNull() { return getBias(); }
