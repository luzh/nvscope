//===- NVScope.cpp --------------------------------------------------------===//
// NVScope instrumentation pass
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/ADT/Twine.h"
#include "llvm/Analysis/MemoryBuiltins.h"
#include "llvm/Analysis/ScalarEvolution.h"
#include "llvm/Analysis/TargetFolder.h"
#include "llvm/Analysis/TargetLibraryInfo.h"
#include "llvm/IR/BasicBlock.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/DebugInfoMetadata.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/InstIterator.h"
#include "llvm/IR/InstrTypes.h"
#include "llvm/IR/Instruction.h"
#include "llvm/IR/Instructions.h"
#include "llvm/IR/IntrinsicInst.h"
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

#include <unordered_map>
#include <unordered_set>

using namespace llvm;

#define DEBUG_TYPE "[NVScope Pass]"

STATISTIC(NVScopeFunctions, "Scanned functions");
STATISTIC(NVScopeMMapOps, "mmap() calls");
STATISTIC(NVScopeCallInsts, "CallInst instructions");
STATISTIC(NVScopeStoreInsts, "StoreInst instructions");
STATISTIC(NVScopeCLWBOps, "CLWB operations");
STATISTIC(NVScopeCLFOptOps, "CLFLUSHOPT operations");
STATISTIC(NVScopeCLFlushOps, "CLFLUSH operations");
STATISTIC(NVScopeSFenceOps, "SFENCE operations");

namespace {
// NVScopeProbes
struct NVScopeProbes : public FunctionPass {
  static char ID;  // Pass identification, replacement for typeid
  NVScopeProbes() : FunctionPass(ID) {}
  bool runOnFunction(Function &F) override;

 private:
  void collectStackVariables(Function &F);
  void printInstrumentedCall(StringRef &callee, StringRef &file, int &line);
  bool instrumentCLOps(Function &F, CallInst *CI, StringRef Probe,
                       StringRef &callee, StringRef &func, StringRef &file,
                       int &line);
  bool instrumentCall(Function &F, CallInst *CI);
  bool instrumentStore(Function &F, StoreInst *StI);
  bool instrumentMemIntrinsic(Function &F, MemIntrinsic *MI);

  Value *findName(StringRef name, std::unordered_map<std::string, Value *> &map,
                  IRBuilder<> &irb);
  void getDebugInfo(Instruction *I, StringRef &func, StringRef &file,
                    int &line);

  std::unordered_set<Value *> _stack;
  std::unordered_map<std::string, Value *> _files;
  std::unordered_map<std::string, Value *> _funcs;

  static const std::unordered_set<std::string> _clops;
  static const std::unordered_set<std::string> _excluded;
};

/**
 */
const std::unordered_set<std::string> NVScopeProbes::_clops = {
    "clwb",    "llvm.x86.sse2.clwb",   "clflushopt", "llvm.x86.sse2.clflushopt",
    "clflush", "llvm.x86.sse2.clflush"};
const std::unordered_set<std::string> NVScopeProbes::_excluded = {
    "clflush", "clflushopt", "clwb", "sfence"};

/**
 * Collect values that are allocated on the function stack.
 */
void NVScopeProbes::collectStackVariables(Function &F) {
  _stack.clear();
  for (auto &B : F) {
    for (auto &I : B) {
      if (I.getOpcode() == Instruction::Alloca) {
        _stack.insert(dyn_cast<Value>(&I));
      }
    }
  }
}

/**
 * Print source code information about instrumented locations.
 */
void NVScopeProbes::printInstrumentedCall(StringRef &callee, StringRef &file,
                                          int &line) {
  errs() << "NVS-Pass:   ";
  errs().write_escaped(file) << ":" << line << " CALLED " << callee << "\n";
}

Value *NVScopeProbes::findName(StringRef name,
                               std::unordered_map<std::string, Value *> &map,
                               IRBuilder<> &irb) {
  auto pair = map.find(name.str());
  if (pair == map.end()) {
    auto value = irb.CreateGlobalStringPtr(name);
    map.emplace(name.str(), value);
    return value;
  }
  return pair->second;
}

void NVScopeProbes::getDebugInfo(Instruction *I, StringRef &func,
                                 StringRef &file, int &line) {
  if (DILocation *Loc = I->getDebugLoc()) {
    line = Loc->getLine();
    file = Loc->getFilename();
    if (auto scope = Loc->getScope()) {
      func = scope->getSubprogram()->getName();
    }
  }
}

/**
 * Instruments store instructions. Inserts a call to a run-time function that
 * records information about the store, before the store is executed.
 */
bool NVScopeProbes::instrumentStore(Function &F, StoreInst *StI) {
  Value *Ptr = StI->getPointerOperand();
  // ignore stores to the function stack.
  auto it = _stack.find(Ptr);
  if (it != _stack.end()) {
    return false;
  }
  // Insert before the store instruction.
  IRBuilder<> IRB(StI);
  auto func = F.getName();
  int line = -1;
  StringRef file = "unknown source file (missing debug information?)";
  getDebugInfo(StI, func, file, line);
  auto size = F.getParent()->getDataLayout().getTypeStoreSize(
      StI->getValueOperand()->getType());
  std::vector<Type *> Params = {IRB.getInt8PtrTy(), IRB.getInt64Ty(),
                                IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                                IRB.getInt32Ty()};
  FunctionType *ProbeTy = FunctionType::get(IRB.getVoidTy(), Params, false);
  IRB.CreateCall(
      F.getParent()->getOrInsertFunction("__nvs_probe_store", ProbeTy),
      {Ptr->getType() == IRB.getInt8PtrTy()
           ? Ptr
           : IRB.CreatePointerCast(Ptr, IRB.getInt8PtrTy()),
       ConstantInt::get(IRB.getInt64Ty(), size, false),
       findName(func, _funcs, IRB), findName(file, _files, IRB),
       ConstantInt::get(IRB.getInt32Ty(), line, false)});
  ++NVScopeStoreInsts;

  errs() << "NVS-Pass:   ";
  errs().write_escaped(file) << ":" << line << " STORE " << size << " bytes\n";

  return true;
}

/**
 * Instrumentation to the memory intrinsic functions: memset/memcpy/memmove.
 */
bool NVScopeProbes::instrumentMemIntrinsic(Function &F, MemIntrinsic *MI) {
  IRBuilder<> IRB(MI);
  if (isa<MemTransferInst>(MI) || isa<MemSetInst>(MI)) {
    ++NVScopeStoreInsts;
    auto func = F.getName();
    int line = -1;
    StringRef file = "unknown source file (missing debug information?)";
    getDebugInfo(MI, func, file, line);
    std::vector<Type *> Params = {IRB.getInt8PtrTy(), IRB.getInt64Ty(),
                                  IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                                  IRB.getInt32Ty()};
    FunctionType *ProbeTy = FunctionType::get(IRB.getVoidTy(), Params, false);
    IRB.CreateCall(
        F.getParent()->getOrInsertFunction("__nvs_probe_store", ProbeTy),
        {MI->getOperand(0), MI->getOperand(2), findName(func, _funcs, IRB),
         findName(file, _files, IRB),
         ConstantInt::get(IRB.getInt32Ty(), line, false)});
    return true;
  }
  return false;
}

bool NVScopeProbes::instrumentCLOps(Function &F, CallInst *CI, StringRef Probe,
                                    StringRef &callee, StringRef &func,
                                    StringRef &file, int &line) {
  IRBuilder<> IRB(CI);
  std::vector<Type *> Params = {IRB.getInt8PtrTy(), IRB.getInt8PtrTy(),
                                IRB.getInt8PtrTy(), IRB.getInt32Ty()};
  FunctionType *ProbeTy = FunctionType::get(IRB.getVoidTy(), Params, false);
  IRB.CreateCall(F.getParent()->getOrInsertFunction(Probe, ProbeTy),
                 {CI->getOperand(0), findName(func, _funcs, IRB),
                  findName(file, _files, IRB),
                  ConstantInt::get(IRB.getInt32Ty(), line, false)});

  printInstrumentedCall(callee, file, line);

  return true;
}

/**
 * Instrument interesting function calls:
 *   mmap(), clflush(), clflushopt(), clwb(), sfence(), and similar ones.
 */
bool NVScopeProbes::instrumentCall(Function &F, CallInst *CI) {
  LLVMContext &Ctx = F.getContext();
  Type *Int64Ty = Type::getInt64Ty(Ctx);
  Type *Int32Ty = Type::getInt32Ty(Ctx);
  Type *Int8PtrTy = Type::getInt8PtrTy(Ctx);

  auto func = F.getName();
  int line = -1;
  StringRef file = "unknown source file (missing debug information?)";
  getDebugInfo(CI, func, file, line);
  bool Modified = false;

  /**
   * TODO: Instrumenting calls depends on the callee's names in LLVM IR. The
   * following conditions may not be comprehensive.
   */

  if (Function *CIF = CI->getCalledFunction()) {
    StringRef callee = CIF->getName();
    if (callee.endswith("mmap")) {
      ++NVScopeMMapOps;
      IRBuilder<> IRB(CI->getNextNode());
      std::vector<Type *> Params = {Int8PtrTy, Int64Ty, Int8PtrTy, Int8PtrTy,
                                    Int32Ty};
      FunctionType *ProbeTy = FunctionType::get(IRB.getVoidTy(), Params, false);
      IRB.CreateCall(
          F.getParent()->getOrInsertFunction("__nvs_probe_mapping", ProbeTy),
          {CI, CI->getOperand(1), findName(func, _funcs, IRB),
           findName(file, _files, IRB),
           ConstantInt::get(Int32Ty, line, false)});

      Modified = true;
      printInstrumentedCall(callee, file, line);

    } else if (callee.contains("memset") || callee.contains("memcpy") ||
               callee.contains("memmove")) {
      ++NVScopeStoreInsts;
      std::vector<Type *> Params = {Int8PtrTy, Int64Ty, Int8PtrTy, Int8PtrTy,
                                    Int32Ty};
      IRBuilder<> IRB(CI);
      FunctionType *ProbeTy = FunctionType::get(IRB.getVoidTy(), Params, false);
      IRB.CreateCall(
          F.getParent()->getOrInsertFunction("__nvs_probe_store", ProbeTy),
          {CI->getOperand(0), CI->getOperand(2), findName(func, _funcs, IRB),
           findName(file, _files, IRB),
           ConstantInt::get(Int32Ty, line, false)});

      Modified = true;
      printInstrumentedCall(callee, file, line);

    } else if (callee == "clwb" || callee == "llvm.x86.sse2.clwb") {
      ++NVScopeCLWBOps;
      Modified =
          instrumentCLOps(F, CI, "__nvs_probe_clwb", callee, func, file, line);

    } else if (callee == "clflushopt" || callee == "llvm.x86.sse2.clflushopt") {
      ++NVScopeCLFOptOps;
      Modified = instrumentCLOps(F, CI, "__nvs_probe_clflushopt", callee, func,
                                 file, line);

    } else if (callee == "clflush" || callee == "llvm.x86.sse2.clflush") {
      ++NVScopeCLFlushOps;
      Modified = instrumentCLOps(F, CI, "__nvs_probe_clflush", callee, func,
                                 file, line);

    } else if (callee == "sfence" || callee == "llvm.x86.sse.sfence") {
      std::vector<Type *> Params = {Int64Ty, Int8PtrTy, Int8PtrTy, Int32Ty};
      IRBuilder<> IRB(CI);
      FunctionType *ProbeTy = FunctionType::get(IRB.getVoidTy(), Params, false);

      IRB.CreateCall(
          F.getParent()->getOrInsertFunction("__nvs_probe_sfence", ProbeTy),
          {ConstantInt::get(Int64Ty, ++NVScopeSFenceOps, false),
           findName(func, _funcs, IRB), findName(file, _files, IRB),
           ConstantInt::get(Int32Ty, line, false)});

      Modified = true;
      printInstrumentedCall(callee, file, line);
    }
  } else {
    // Calls through function pointers can be this type.
    // stackoverflow.com/questions/11686951/how-can-i-get-function-name-from-callinst-in-llvm
    errs() << "NVS-Pass: WARNING - Indirect call detected but not handled!\n";
  }

  if (Modified) {
    ++NVScopeCallInsts;
  }
  return Modified;
}

/**
 * Iterates over each function's instructions and adds instrumentation to the
 * instructions of interest.
 */
bool NVScopeProbes::runOnFunction(Function &F) {
  ++NVScopeFunctions;

  if (auto fname = _excluded.find(F.getName()) != _excluded.end()) {
    errs() << "NVS-Pass: Skipping function ";
    errs().write_escaped(F.getName()) << "()\n";
    return false;
  }

  errs() << "NVS-Pass: Analyzing function ";
  errs().write_escaped(F.getName()) << "()\n";

  collectStackVariables(F);
  bool Modified = false;
  for (auto &B : F) {
    for (auto &I : B) {
      if (I.getOpcode() == Instruction::Store) {
        Modified |= instrumentStore(F, dyn_cast<StoreInst>(&I));
      } else if (isa<MemIntrinsic>(I)) {
        Modified |= instrumentMemIntrinsic(F, dyn_cast<MemIntrinsic>(&I));
      } else if (I.getOpcode() == Instruction::Call) {
        Modified |= instrumentCall(F, dyn_cast<CallInst>(&I));
      }
    }
  }
  return Modified;
}

}  // namespace

char NVScopeProbes::ID = 0;
static RegisterPass<NVScopeProbes> NVScopeProbesPass(
    "probes", "NVScope Probes Insertion Pass");
