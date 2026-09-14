// Copyright (c) 2026 Huawei Technologies Co., Ltd.
// This program is free software, you can redistribute it and/or modify it under the terms and conditions of
// CANN Open Software License Agreement Version 2.0 (the "License").
// Please refer to the License for details. You may not use this file except in compliance with the License.
// THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND, EITHER EXPRESS OR IMPLIED,
// INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT, MERCHANTABILITY, OR FITNESS FOR A PARTICULAR PURPOSE.
// See LICENSE in the root of the software repository for the full text of the License.

//===- VPTOMadInternal.h - Mad-family declarations and templates ----------===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
// Internal to lib/PTO/IR/VPTO/mad; not installed.

#ifndef PTO_IR_VPTO_MAD_INTERNAL_H
#define PTO_IR_VPTO_MAD_INTERNAL_H

#include "VPTOInternal.h"

// Shared Mad-family helpers (defined in mad/VPTOMad.cpp).
mlir::LogicalResult verifyMadPointerKinds(mlir::Operation *op, mlir::Type lhsTy,
                                          mlir::Type rhsTy, mlir::Type dstTy,
                                          std::optional<mlir::Type> biasTy = std::nullopt);
mlir::LogicalResult verifyMadMxCommon(mlir::Operation *op, mlir::Type lhsTy,
                                      mlir::Type rhsTy, mlir::Type dstTy,
                                      std::optional<mlir::Type> biasTy = std::nullopt);
mlir::LogicalResult verifyMadSemanticClauses(mlir::Operation *op, mlir::Type lhsTy,
                                             mlir::Type rhsTy, mlir::Type dstTy,
                                             std::optional<mlir::Type> biasTy,
                                             std::optional<mlir::pto::Tf32Mode> tf32Mode,
                                             std::optional<mlir::pto::MadSatMode> satMode,
                                             bool hasNDir);
mlir::ParseResult parseMadSemanticClauses(mlir::OpAsmParser &parser,
                                          mlir::NamedAttrList &attrs,
                                          bool parseTf32ModeClause);
mlir::ParseResult parseMadSemanticTypes(
    mlir::OpAsmParser &parser, bool hasBias, mlir::Type &lhsType,
    mlir::Type &rhsType, mlir::Type &dstType, mlir::Type &biasType,
    mlir::Type &mType, mlir::Type &nType, mlir::Type &kType,
    bool hasUnitFlagValue, bool hasAccInitValue, bool hasDisableGemvValue,
    bool hasBiasInitValue, mlir::Type &unitFlagType, mlir::Type &accInitType,
    mlir::Type &disableGemvType, mlir::Type &biasInitType);
mlir::ParseResult resolveMadSemanticOperands(
    mlir::OpAsmParser &parser, mlir::OperationState &result, bool hasBias,
    mlir::OpAsmParser::UnresolvedOperand lhs, mlir::Type lhsType,
    mlir::OpAsmParser::UnresolvedOperand rhs, mlir::Type rhsType,
    mlir::OpAsmParser::UnresolvedOperand dst, mlir::Type dstType,
    mlir::OpAsmParser::UnresolvedOperand bias, mlir::Type biasType,
    mlir::OpAsmParser::UnresolvedOperand m, mlir::Type mType,
    mlir::OpAsmParser::UnresolvedOperand n, mlir::Type nType,
    mlir::OpAsmParser::UnresolvedOperand k, mlir::Type kType);
void printMadSemanticClauses(mlir::OpAsmPrinter &printer, mlir::Operation *op,
                             bool allowTf32Mode);
llvm::ArrayRef<llvm::StringRef> getMadSemanticElidedAttrs(bool allowTf32Mode);

template <typename OpT>
[[maybe_unused]] static mlir::ParseResult
parseMadSemanticOpCommon(mlir::OpAsmParser &parser, mlir::OperationState &result,
                         bool hasBias, bool parseTf32ModeClause) {
  mlir::OpAsmParser::UnresolvedOperand lhs, rhs, dst, bias;
  mlir::OpAsmParser::UnresolvedOperand m, n, k;
  if (parseRequiredOperandWithComma(parser, lhs) ||
      parseRequiredOperandWithComma(parser, rhs) ||
      parseRequiredOperandWithComma(parser, dst) ||
      (hasBias && parseRequiredOperandWithComma(parser, bias)) ||
      parseRequiredOperandWithComma(parser, m) ||
      parseRequiredOperandWithComma(parser, n) ||
      parser.parseOperand(k)) {
    return mlir::failure();
  }
  mlir::OpAsmParser::UnresolvedOperand unitFlagValue, accInitValue,
      disableGemvValue, biasInitValue;
  bool hasUnitFlagValue = false, hasAccInitValue = false,
       hasDisableGemvValue = false, hasBiasInitValue = false;
  if (mlir::succeeded(parser.parseOptionalKeyword("unit_flag_value"))) {
    hasUnitFlagValue = true;
    if (parser.parseLParen() || parser.parseOperand(unitFlagValue) ||
        parser.parseRParen()) {
      return mlir::failure();
    }
  }
  if (mlir::succeeded(parser.parseOptionalKeyword("acc_init"))) {
    hasAccInitValue = true;
    if (parser.parseLParen() || parser.parseOperand(accInitValue) ||
        parser.parseRParen()) {
      return mlir::failure();
    }
  }
  if (mlir::succeeded(parser.parseOptionalKeyword("disable_gemv_value"))) {
    hasDisableGemvValue = true;
    if (parser.parseLParen() || parser.parseOperand(disableGemvValue) ||
        parser.parseRParen()) {
      return mlir::failure();
    }
  }
  if (mlir::succeeded(parser.parseOptionalKeyword("bias_init"))) {
    hasBiasInitValue = true;
    if (parser.parseLParen() || parser.parseOperand(biasInitValue) ||
        parser.parseRParen()) {
      return mlir::failure();
    }
  }
  mlir::NamedAttrList attrs;
  if (mlir::failed(parseMadSemanticClauses(parser, attrs, parseTf32ModeClause))) {
    return mlir::failure();
  }
  if (parser.parseOptionalAttrDict(attrs) || parser.parseColon()) {
    return mlir::failure();
  }
  mlir::Type lhsType, rhsType, dstType, mType, nType, kType, biasType;
  mlir::Type unitFlagType, accInitType, disableGemvType, biasInitType;
  if (mlir::failed(parseMadSemanticTypes(
          parser, hasBias, lhsType, rhsType, dstType, biasType, mType, nType,
          kType, hasUnitFlagValue, hasAccInitValue, hasDisableGemvValue,
          hasBiasInitValue, unitFlagType, accInitType, disableGemvType,
          biasInitType))) {
    return mlir::failure();
  }
  result.addAttributes(attrs);
  {
    // AttrSizedOperandSegments is mandatory once multiple Optional variadic
    // operands exist. Inputs usually omit it; prefill the canonical segment
    // sizes (fixed 1s + runtime-flag presence bits) when absent.
    if (!result.attributes.get("operandSegmentSizes")) {
      int fixedCount = (hasBias ? 4 : 3) + 3;
      llvm::SmallVector<int32_t, 10> sizes(fixedCount, 1);
      sizes.push_back(hasUnitFlagValue ? 1 : 0);
      sizes.push_back(hasAccInitValue ? 1 : 0);
      sizes.push_back(hasDisableGemvValue ? 1 : 0);
      sizes.push_back(hasBiasInitValue ? 1 : 0);
      result.addAttribute(
          "operandSegmentSizes",
          mlir::DenseI32ArrayAttr::get(parser.getContext(), sizes));
    }
  }
  if (mlir::failed(resolveMadSemanticOperands(parser, result, hasBias, lhs,
                                              lhsType, rhs, rhsType, dst,
                                              dstType, bias, biasType, m, mType,
                                              n, nType, k, kType))) {
    return mlir::failure();
  }
  // Resolve the runtime flag operands in operand order.
  if (hasUnitFlagValue &&
      parser.resolveOperand(unitFlagValue, unitFlagType, result.operands)) {
    return mlir::failure();
  }
  if (hasAccInitValue &&
      parser.resolveOperand(accInitValue, accInitType, result.operands)) {
    return mlir::failure();
  }
  if (hasDisableGemvValue &&
      parser.resolveOperand(disableGemvValue, disableGemvType,
                            result.operands)) {
    return mlir::failure();
  }
  if (hasBiasInitValue &&
      parser.resolveOperand(biasInitValue, biasInitType, result.operands)) {
    return mlir::failure();
  }
  return mlir::success();
}

template <typename OpT>
static void printMadRuntimeFlagClauses(mlir::OpAsmPrinter &printer, OpT op) {
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
}

template <typename OpT>
static void appendMadRuntimeFlagTypes(mlir::OpAsmPrinter &printer, OpT op) {
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
}

template <typename OpT>
static void printMadOperandSegmentSizesIfNeeded(mlir::OpAsmPrinter &printer,
                                                OpT op) {
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
  auto push = [&sizes](mlir::Value v) { sizes.push_back(v ? 1 : 0); };
  push(op.getLhs());
  push(op.getRhs());
  push(op.getDst());
  if constexpr (std::is_same_v<OpT, mlir::pto::MadBiasOp> ||
                std::is_same_v<OpT, mlir::pto::MadMxBiasOp>) {
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
  llvm::interleave(
      sizes, printer, [&printer](int32_t s) { printer << " " << s; }, ",");
  printer << "}";
}

template <typename OpT>
static void printMadSemanticOpNoBias(mlir::OpAsmPrinter &printer, OpT op,
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
static void printMadSemanticOpWithBias(mlir::OpAsmPrinter &printer, OpT op,
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
static mlir::LogicalResult verifyMadSemanticWithTf32(OpTy op) {
  std::optional<mlir::pto::Tf32Mode> tf32Mode;
  if (auto tf32ModeAttr =
          op->template getAttrOfType<mlir::pto::Tf32ModeAttr>("tf32_mode")) {
    tf32Mode = tf32ModeAttr.getValue();
  }
  return verifyMadSemanticClauses(op, op.getLhs().getType(),
                                  op.getRhs().getType(), op.getDst().getType(),
                                  std::nullopt, tf32Mode, op.getSatMode(),
                                  op->hasAttr("n_dir"));
}

#endif // PTO_IR_VPTO_MAD_INTERNAL_H
