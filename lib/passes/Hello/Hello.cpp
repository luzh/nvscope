//===- Hello.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Transforms/Utils/BasicBlockUtils.h"
using namespace llvm;

#define DEBUG_TYPE "hello"

STATISTIC(HelloCounter, "Counts number of functions greeted");

namespace {
// Hello - The first implementation, without getAnalysisUsage.
struct Hello : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  Hello() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    ++HelloCounter;
    errs() << "Hello: ";
    errs().write_escaped(F.getName()) << '\n';
    return false;
  }
};
} // namespace

char Hello::ID = 0;
static RegisterPass<Hello> X("hello", "Hello World Pass");

namespace {
// Hello2 - The second implementation with getAnalysisUsage implemented.
struct Hello2 : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  Hello2() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    ++HelloCounter;
    errs() << "Hello: ";
    errs().write_escaped(F.getName()) << '\n';
    return false;
  }

  // We don't modify the program, so we preserve all analyses.
  void getAnalysisUsage(AnalysisUsage &AU) const override {
    AU.setPreservesAll();
  }
};
} // namespace

char Hello2::ID = 0;
static RegisterPass<Hello2>
    Y("hello2", "Hello World Pass (with getAnalysisUsage implemented)");

namespace {
// Bin2Mul - Replace the first binary operator (+, -, etc.) in every function
// with a multiply.
// Source: www.cs.cornell.edu/~asampson/blog/llvm.html
struct Bin2Mul : public FunctionPass {
  static char ID; // Pass identification, replacement for typeid
  Bin2Mul() : FunctionPass(ID) {}

  bool runOnFunction(Function &F) override {
    ++HelloCounter;
    errs() << "Hello: transforming function ";
    errs().write_escaped(F.getName()) << "()\n";

    for (auto &B : F) {
      for (auto &I : B) {
        if (auto *Op = dyn_cast<BinaryOperator>(&I)) {
          // Insert at the point where the instruction `Op` appears.
          IRBuilder<> IRB(Op);

          // Make a multiply with the same operands as `Op`.
          Value *Lhs = Op->getOperand(0);
          Value *Rhs = Op->getOperand(1);
          Value *Mul = IRB.CreateMul(Lhs, Rhs);

          // Everywhere the old instruction was used as an operand, use our
          // new multiply instruction instead.
          for (auto &U : Op->uses()) {
            User *user = U.getUser(); // A User is anything with operands.
            user->setOperand(U.getOperandNo(), Mul);
          }

          ReplaceInstWithInst(Op, dyn_cast<Instruction>(Mul));

          // We modified the code.
          return true;
        }
      }
    }

    return false;
  }
};
} // namespace

char Bin2Mul::ID = 0;
static RegisterPass<Bin2Mul>
    Bin2MulPass("bin2mul", "Binary Operator to Multiplication Pass");
