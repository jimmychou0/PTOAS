// Copyright (c) 2026 Huawei Technologies Co., Ltd.
// CANN Open Software License Agreement Version 2.0

#ifndef PTOAS_EMITC_FATOBJ_EMISSION_H
#define PTOAS_EMITC_FATOBJ_EMISSION_H

#include "mlir/Support/LogicalResult.h"
#include "llvm/ADT/StringRef.h"

namespace llvm {
class ToolOutputFile;
class raw_ostream;
} // namespace llvm

namespace mlir::pto {

mlir::LogicalResult emitEmitCFatobj(llvm::StringRef cppSource,
                                    llvm::StringRef ptoIsaRoot,
                                    llvm::StringRef aicoreArch,
                                    llvm::ToolOutputFile &outputFile,
                                    llvm::raw_ostream &diagOS);

} // namespace mlir::pto

#endif // PTOAS_EMITC_FATOBJ_EMISSION_H
