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
#include "llvm/IR/Intrinsics.h"
#include "llvm/IR/Value.h"
#include "llvm/Pass.h"
#include "llvm/Support/Casting.h"
#include "llvm/Support/CommandLine.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/raw_ostream.h"

using namespace llvm;

#define DEBUG_TYPE "[NVScope Pass]"

STATISTIC(NVScopeFunctions, "Scanned functions");
STATISTIC(NVScopeCallInsts, "CallInst instructions");
STATISTIC(NVScopeMMapOps, "mmap() calls");
STATISTIC(NVScopeStoreInsts, "StoreInst instructions");
STATISTIC(NVScopeCLFlushOps, "CLFLUSH operations");
STATISTIC(NVScopeCLFOptOps, "CLFLUSHOPT operations");
STATISTIC(NVScopeCLWBOps, "CLWB operations");
STATISTIC(NVScopeSFenceOps, "SFENCE operations");

/* --- */

uint64_t SFenceId = 0;

namespace {
// NVScopeProbes
struct NVScopeProbes : public FunctionPass {
  static char ID;  // Pass identification, replacement for typeid
  NVScopeProbes() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    NVScopeFunctions++;
    errs() << "NVS-Pass: probing function ";
    errs().write_escaped(F.getName()) << "()\n";

    // Get the function to call from our runtime library.
    LLVMContext &Ctx = F.getContext();
    Type *VoidTy = Type::getVoidTy(Ctx);

    Type *Int8PtrTy = Type::getInt8PtrTy(Ctx);
    Type *Int64Ty = Type::getInt64Ty(Ctx);
    Type *Int64PtrTy = Type::getInt64PtrTy(Ctx);

#ifdef NDEBUG
    std::vector<Type *> ProbeMmapParams = {Int64Ty, Int64Ty};
    std::vector<Type *> ProbeStore64Params = {Int64PtrTy, Int64Ty};
    std::vector<Type *> ProbeSFenceParams = {Int64Ty};
#else
    Type *Int32Ty = Type::getInt32Ty(Ctx);
    std::vector<Type *> ProbeMmapParams = {Int64Ty, Int64Ty, Int8PtrTy,
                                           Int8PtrTy, Int32Ty};
    std::vector<Type *> ProbeStore64Params = {Int64PtrTy, Int64Ty, Int8PtrTy,
                                              Int8PtrTy, Int32Ty};
    std::vector<Type *> ProbeSFenceParams = {Int64Ty, Int8PtrTy, Int8PtrTy,
                                             Int32Ty};
#endif
    FunctionType *ProbeMmapType =
        FunctionType::get(VoidTy, ProbeMmapParams, false);
    Constant *ProbeMmap =
        F.getParent()->getOrInsertFunction("__nvs_probe_mmap", ProbeMmapType);

    FunctionType *ProbeStore64Type =
        FunctionType::get(VoidTy, ProbeStore64Params, false);
    Constant *ProbeStore64 = F.getParent()->getOrInsertFunction(
        "__nvs_probe_store64", ProbeStore64Type);

    FunctionType *ProbeSFenceType =
        FunctionType::get(VoidTy, ProbeSFenceParams, false);
    Constant *ProbeSFence = F.getParent()->getOrInsertFunction(
        "__nvs_probe_sfence", ProbeSFenceType);

    // std::vector<StoreInst *> StoreInsts;

    bool Modified = false;
    for (auto &B : F) {
      for (auto &I : B) {
#ifndef NDEBUG
        int LineNr = -1;
        StringRef FileName = "unknown source file";
        if (DILocation *Loc = I.getDebugLoc()) {
          LineNr = Loc->getLine();
          FileName = Loc->getFilename();
          // StringRef Dir = Loc->getDirectory();
          // bool ImplicitCode = Loc->isImplicitCode();
        }
#endif

        if (I.getOpcode() == Instruction::Store) {
          StoreInst *StI = dyn_cast<StoreInst>(&I);
          Value *Val = StI->getValueOperand();
          Value *Ptr = StI->getPointerOperand();

          // Todo: Should also handle other sizes.
          if (Ptr->getType() != Int64PtrTy || Val->getType() != Int64Ty)
            continue;

          // Insert before the store instruction.
          IRBuilder<> IRB(StI);
          IRB.SetInsertPoint(&B, IRB.GetInsertPoint());

#ifdef NDEBUG
          Value *StArgs[] = {Ptr, Val};
#else
          // Debug information
          Value *File = IRB.CreateGlobalStringPtr(FileName);
          Value *Func = IRB.CreateGlobalStringPtr(F.getName());
          Value *Line = ConstantInt::get(Int32Ty, LineNr, false);
          Value *StArgs[] = {Ptr, Val, File, Func, Line};
#endif
          // Insert a call to the probe function.
          IRB.CreateCall(ProbeStore64, StArgs);

          // StoreInsts.push_back(StI);

          Modified = true;
          NVScopeStoreInsts++;

          continue;
        }

        if (I.getOpcode() == Instruction::Call) {
          CallInst *CI = dyn_cast<CallInst>(&I);
          Function *CIF = CI->getCalledFunction();

          if (CIF) {
            StringRef FNameStr = CIF->getName();

            if (FNameStr == "mmap") {
              errs() << "NVS-Pass: mmap()\n";

              CallInst *MmapI = dyn_cast<CallInst>(&I);
              IRBuilder<> IRB(MmapI);
              // Insert after the mmap() call.
              IRB.SetInsertPoint(&B, ++IRB.GetInsertPoint());

              // Todo: How to get the mmap'ed size and address?
              Value *MapAddr = ConstantInt::get(Int64Ty, 0, false);
              Value *MapSize = ConstantInt::get(Int64Ty, 0, false);
#ifdef NDEBUG
              Value *MmapArgs[] = {MapAddr, MapSize};
#else
              // Debug information
              Value *File = IRB.CreateGlobalStringPtr(FileName);
              Value *Func = IRB.CreateGlobalStringPtr(F.getName());
              Value *Line = ConstantInt::get(Int32Ty, LineNr, false);
              Value *MmapArgs[] = {MapAddr, MapSize, File, Func, Line};
#endif
              // Insert a call to the probe function.
              IRB.CreateCall(ProbeMmap, MmapArgs);
              Modified = true;
              NVScopeMMapOps++;
            } else if (FNameStr == "llvm.x86.sse2.clflush") {
              errs() << "NVS-Pass: _mm_clflush()\n";
              NVScopeCLFlushOps++;
            } else if (FNameStr == "llvm.x86.clflushopt") {
              errs() << "NVS-Pass: _mm_clflushopt()\n";
              NVScopeCLFOptOps++;
            } else if (FNameStr == "llvm.x86.clwb") {
              errs() << "NVS-Pass: _mm_clwb()\n";
              NVScopeCLWBOps++;
            } else if (FNameStr == "llvm.x86.sse.sfence") {
              CallInst *SfI = dyn_cast<CallInst>(&I);
              // Insert before the sfence instruction.
              IRBuilder<> IRB(SfI);
              IRB.SetInsertPoint(&B, IRB.GetInsertPoint());

              SFenceId++;
              Value *SfId = ConstantInt::get(Int64Ty, SFenceId, false);
#ifdef NDEBUG
              Value *SfArgs[] = {SfId};
#else
              // Debug information
              Value *File = IRB.CreateGlobalStringPtr(FileName);
              Value *Func = IRB.CreateGlobalStringPtr(F.getName());
              Value *Line = ConstantInt::get(Int32Ty, LineNr, false);
              Value *SfArgs[] = {SfId, File, Func, Line};
#endif
              // Insert a call to the probe function.
              IRB.CreateCall(ProbeSFence, SfArgs);
              Modified = true;
              errs() << "NVS-Pass: _mm_sfence() #" << SFenceId << "\n";
              NVScopeSFenceOps = SFenceId;
            }
          } else {
            // Calls through function pointers can be this type.
            // stackoverflow.com/questions/11686951/how-can-i-get-function-name-from-callinst-in-llvm
            errs() << "NVS-Pass: Indirect call\n";
          }

          NVScopeCallInsts++;

          continue;
        }
      }
    }

    // Moving this loop into for (auto &B : F) causes segfault, why?
    // for (auto &StI : StoreInsts) {
    //  StI->eraseFromParent();
    //}

    return Modified;
  }
};
}  // namespace

char NVScopeProbes::ID = 0;
static RegisterPass<NVScopeProbes> NVScopeProbesPass(
    "probes", "NVScope Probes Insertion Pass");
