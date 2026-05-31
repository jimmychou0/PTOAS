// Copyright (c) 2026 Huawei Technologies Co., Ltd.
// CANN Open Software License Agreement Version 2.0

#include "EmitCFatobjEmission.h"

#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MemoryBuffer.h"
#include "llvm/Support/Path.h"
#include "llvm/Support/Process.h"
#include "llvm/Support/Program.h"
#include "llvm/Support/ToolOutputFile.h"
#include "llvm/Support/raw_ostream.h"

#include <cstdlib>
#include <optional>
#include <string>

using mlir::failure;
using mlir::LogicalResult;
using mlir::success;

namespace {

using llvm::StringRef;

// ---------------------------------------------------------------------------
// TempFileRegistry — RAII temporary file manager (same pattern as VPTO)
// ---------------------------------------------------------------------------
class TempFileRegistry {
public:
  ~TempFileRegistry() { cleanup(); }

  void cleanup() {
    for (const std::string &path : paths)
      llvm::sys::fs::remove(path);
    paths.clear();
  }

  bool create(StringRef prefix, StringRef suffix, std::string &path,
              llvm::raw_ostream &diagOS) {
    llvm::SmallString<128> tempPath;
    int fd = -1;
    std::error_code ec =
        llvm::sys::fs::createTemporaryFile(prefix, suffix, fd, tempPath);
    if (ec) {
      diagOS << "Error: failed to create temporary file for " << prefix
             << suffix << ": " << ec.message() << "\n";
      return false;
    }
    llvm::sys::Process::SafelyCloseFileDescriptor(fd);
    path = tempPath.str().str();
    paths.push_back(path);
    return true;
  }

private:
  llvm::SmallVector<std::string, 8> paths;
};

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------
static bool writeTextFile(StringRef path, StringRef content,
                          llvm::raw_ostream &diagOS) {
  std::error_code ec;
  llvm::raw_fd_ostream os(path, ec, llvm::sys::fs::OF_Text);
  if (ec) {
    diagOS << "Error: failed to open " << path
           << " for write: " << ec.message() << "\n";
    return false;
  }
  os << content;
  os.flush();
  return true;
}

static bool copyBinaryFile(StringRef srcPath, llvm::ToolOutputFile &outputFile,
                           llvm::raw_ostream &diagOS) {
  auto bufOrErr = llvm::MemoryBuffer::getFile(srcPath);
  if (!bufOrErr) {
    diagOS << "Error: failed to read " << srcPath << ": "
           << bufOrErr.getError().message() << "\n";
    return false;
  }
  outputFile.os() << bufOrErr.get()->getBuffer();
  outputFile.os().flush();
  return true;
}

static std::string joinPath(StringRef lhs, StringRef rhs) {
  llvm::SmallString<256> joined(lhs);
  llvm::sys::path::append(joined, rhs);
  return std::string(joined.str());
}

// ---------------------------------------------------------------------------
// runCommandWithStderr — subprocess execution (same pattern as VPTO)
// ---------------------------------------------------------------------------
static bool runCommandWithStderr(StringRef program,
                                 llvm::ArrayRef<std::string> ownedArgs,
                                 StringRef stderrPath,
                                 llvm::raw_ostream &diagOS, StringRef what) {
  llvm::SmallVector<llvm::StringRef, 32> args;
  args.reserve(ownedArgs.size());
  for (const std::string &arg : ownedArgs)
    args.push_back(arg);

  llvm::SmallVector<std::optional<llvm::StringRef>, 3> redirects = {
      std::nullopt, std::nullopt, stderrPath};

  std::string execErr;
  bool execFailed = false;
  int rc = llvm::sys::ExecuteAndWait(program, args, std::nullopt, redirects, 0,
                                     0, &execErr, &execFailed);
  if (!execFailed && rc == 0)
    return true;

  diagOS << "Error: " << what << " failed\n";
  diagOS << "Command:";
  for (llvm::StringRef arg : args)
    diagOS << " " << arg;
  diagOS << "\n";
  if (!execErr.empty())
    diagOS << execErr << "\n";
  if (auto buffer = llvm::MemoryBuffer::getFile(stderrPath))
    diagOS << buffer.get()->getBuffer() << "\n";
  return false;
}

// ---------------------------------------------------------------------------
// EmitCFatobjToolchain — locate bisheng from ASCEND_HOME_PATH
// ---------------------------------------------------------------------------
class EmitCFatobjToolchain {
public:
  static std::optional<EmitCFatobjToolchain>
  create(llvm::raw_ostream &diagOS) {
    const char *env = std::getenv("ASCEND_HOME_PATH");
    if (!env || !*env) {
      diagOS << "Error: ASCEND_HOME_PATH is required for EmitC fatobj "
                "emission.\n";
      return std::nullopt;
    }
    EmitCFatobjToolchain tc(env);
    if (!tc.validate(diagOS))
      return std::nullopt;
    return tc;
  }

  const std::string &ascendHome() const { return ascendHomePath; }
  const std::string &bisheng() const { return bishengPath; }
  const std::string &driverKernelInc() const { return driverKernelIncPath; }

private:
  explicit EmitCFatobjToolchain(StringRef ascendHome)
      : ascendHomePath(ascendHome.str()),
        bishengPath(joinPath(ascendHomePath, "bin/bisheng")),
        driverKernelIncPath("/usr/local/Ascend/driver/kernel/inc") {}

  bool validate(llvm::raw_ostream &diagOS) const {
    if (!llvm::sys::fs::exists(bishengPath)) {
      diagOS << "Error: unable to locate bisheng: " << bishengPath << "\n";
      return false;
    }
    return true;
  }

  std::string ascendHomePath;
  std::string bishengPath;
  std::string driverKernelIncPath;
};

} // namespace

// ---------------------------------------------------------------------------
// emitEmitCFatobj — main entry point
// ---------------------------------------------------------------------------
LogicalResult mlir::pto::emitEmitCFatobj(StringRef cppSource,
                                         StringRef ptoIsaRoot,
                                         StringRef aicoreArch,
                                         llvm::ToolOutputFile &outputFile,
                                         llvm::raw_ostream &diagOS) {
  if (ptoIsaRoot.empty()) {
    diagOS << "Error: PTO_ISA_ROOT is required for EmitC fatobj emission.\n"
           << "Set via --pto-isa-root or $PTO_ISA_ROOT environment variable.\n";
    return failure();
  }

  std::optional<EmitCFatobjToolchain> toolchain =
      EmitCFatobjToolchain::create(diagOS);
  if (!toolchain)
    return failure();

  TempFileRegistry tempFiles;

  std::string cppPath, objPath, stderrPath;
  if (!tempFiles.create("ptoas-emitc-kernel", ".cpp", cppPath, diagOS))
    return failure();
  if (!tempFiles.create("ptoas-emitc-kernel", ".o", objPath, diagOS))
    return failure();
  if (!tempFiles.create("ptoas-emitc-stderr", ".log", stderrPath, diagOS))
    return failure();

  if (!writeTextFile(cppPath, cppSource, diagOS))
    return failure();

  // bisheng -xcce compilation: device + host → fatobj
  llvm::SmallVector<std::string, 32> args = {
      toolchain->bisheng(),
      "-xcce",
      "-fenable-matrix",
      std::string("--cce-aicore-arch=") + aicoreArch.str(),
      "-fPIC",
      "-DMEMORY_BASE",
      "-std=c++17",
      "-Wno-macro-redefined",
      "-Wno-ignored-attributes",
      std::string("-I") + joinPath(ptoIsaRoot, "include"),
      std::string("-I") + joinPath(ptoIsaRoot, "tests/common"),
      std::string("-I") + joinPath(toolchain->ascendHome(), "include"),
      std::string("-I") + toolchain->driverKernelInc(),
      "-mllvm", "-cce-aicore-stack-size=0x8000",
      "-mllvm", "-cce-aicore-function-stack-size=0x8000",
      "-mllvm", "-cce-aicore-record-overflow=true",
      "-mllvm", "-cce-aicore-addr-transform",
      "-mllvm", "-cce-aicore-dcci-insert-for-scalar=false",
      "-c",
      cppPath,
      "-o",
      objPath,
  };

  if (!runCommandWithStderr(toolchain->bisheng(), args, stderrPath, diagOS,
                            "EmitC CCE compilation"))
    return failure();

  if (!copyBinaryFile(objPath, outputFile, diagOS))
    return failure();

  outputFile.keep();
  return success();
}
