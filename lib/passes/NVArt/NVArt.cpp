//===- NVArt.cpp ----------------------------------------------------------===//
// Performs code instrumentation.
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

#define DEBUG_TYPE "[NVArt Pass]"

STATISTIC(NVArtFunctions, "Number of scanned functions");
STATISTIC(NVArtCallInsts, "Number of CallInst instructions");
STATISTIC(NVArtMmapOps, "Number of mmap operations");
STATISTIC(NVArtStoreInsts, "Number of StoreInst instructions");
STATISTIC(NVArtCacheOps, "Number of cache flush/wb operations");
STATISTIC(NVArtSFenceOps, "Number of sfence operations");

namespace {
// NVArtHello - Replace the first binary operator (+, -, etc.) in every
// function with a multiply.
// Source: www.cs.cornell.edu/~asampson/blog/llvm.html
struct NVArtHello : public FunctionPass {
  static char ID;  // Pass identification, replacement for typeid
  NVArtHello() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    NVArtFunctions++;
    errs() << "NVArt: transforming function ";
    errs().write_escaped(F.getName()) << "()\n";

    for (auto &B : F) {
      for (auto &I : B) {
        if (auto *op = dyn_cast<BinaryOperator>(&I)) {
          // Insert at the point where the instruction `op` appears.
          IRBuilder<> builder(op);

          // Make a multiply with the same operands as `op`.
          Value *lhs = op->getOperand(0);
          Value *rhs = op->getOperand(1);
          Value *mul = builder.CreateMul(lhs, rhs);

          // Everywhere the old instruction was used as an operand, use our
          // new multiply instruction instead.
          for (auto &U : op->uses()) {
            User *user = U.getUser();  // A User is anything with operands.
            user->setOperand(U.getOperandNo(), mul);
          }

          // We modified the code.
          return true;
        }
      }
    }

    return false;
  }
};
}  // namespace

char NVArtHello::ID = 0;
static RegisterPass<NVArtHello> NVArtHelloPass("hello", "NVArt Hello Pass");

/* --- */

namespace {
// NVArtTransformStores
struct NVArtTransformStores : public FunctionPass {
  static char ID;  // Pass identification, replacement for typeid
  NVArtTransformStores() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    NVArtFunctions++;
    errs() << "NVArt: Transforming stores in function ";
    errs().write_escaped(F.getName()) << "()\n";

    // Get the function to call from our runtime library.
    LLVMContext &Ctx = F.getContext();
    Type *RetType = Type::getVoidTy(Ctx);

    std::vector<Type *> Store64ParamTypes = {Type::getInt64PtrTy(Ctx),
                                             Type::getInt64Ty(Ctx)};
    FunctionType *ProcStore64Type =
        FunctionType::get(RetType, Store64ParamTypes, false);
    Constant *ProcStore64 =
        F.getParent()->getOrInsertFunction("process_store64", ProcStore64Type);

    std::vector<StoreInst *> StoreInsts;

    bool modified = false;
    for (auto &B : F) {
      for (auto &I : B) {
        if (StoreInst *SI = dyn_cast<StoreInst>(&I)) {
          Value *Val = SI->getValueOperand();
          Value *Ptr = SI->getPointerOperand();

          // Todo: Should also handle other sizes.
          if (Val->getType() != Type::getInt64Ty(Ctx) ||
              Ptr->getType() != Type::getInt64PtrTy(Ctx))
            continue;

          // Insert after the store instruction.
          IRBuilder<> IRB(SI);
          IRB.SetInsertPoint(&B, IRB.GetInsertPoint());

          // Insert a call to our function.
          Value *Args[] = {Ptr, Val};
          IRB.CreateCall(ProcStore64, Args);

          StoreInsts.push_back(SI);

          modified = true;
          NVArtStoreInsts++;

          continue;
        }

        if (CallInst *CI = dyn_cast<CallInst>(&I)) {
          Function *CIF = CI->getCalledFunction();

          if (CIF) {
            StringRef FName = CIF->getName();

            if (FName == "llvm.x86.sse2.clflush") {
              errs() << "NVArt: _mm_clflush(?)\n";
              NVArtCacheOps++;
            } else if (FName == "llvm.x86.sse.sfence") {
              errs() << "NVArt: _mm_sfence()\n";
              NVArtSFenceOps++;
            } else if (FName == "mmap") {
              errs() << "NVArt: mmap()\n";
              NVArtMmapOps++;
            }
          } else {
            // stackoverflow.com/questions/11686951/how-can-i-get-function-name-from-callinst-in-llvm
            errs() << "NVArt: Indirect call\n";
          }

          NVArtCallInsts++;

          continue;
        }
      }
    }

    // Moving this loop into for (auto &B : F) causes segfault, why?
    for (auto &SI : StoreInsts) {
      SI->eraseFromParent();
    }

    return modified;
  }
};
}  // namespace

char NVArtTransformStores::ID = 0;
static RegisterPass<NVArtTransformStores> NVArtTransformStoresPass(
    "transform-stores", "NVArt Store Transformation Pass");
