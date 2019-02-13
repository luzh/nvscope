//===- Hola.cpp - Example code from "Writing an LLVM Pass" ---------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements two versions of the LLVM "Hello World" pass described
// in docs/WritingAnLLVMPass.html, but renamed them to "Hola Mundo" to avoid
// conflict with LLVM's Hello passes.
//
//===----------------------------------------------------------------------===//

#include "llvm/ADT/Statistic.h"
#include "llvm/IR/Function.h"
#include "llvm/Pass.h"
#include "llvm/Support/raw_ostream.h"
using namespace llvm;

#define DEBUG_TYPE "hola"

STATISTIC(HolaCounter, "Counts number of functions greeted");

namespace {
  // Hola - The first implementation, without getAnalysisUsage.
  struct Hola : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    Hola() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      ++HolaCounter;
      errs() << "Hola: ";
      errs().write_escaped(F.getName()) << '\n';
      return false;
    }
  };
}

char Hola::ID = 0;
static RegisterPass<Hola> X("hola", "Hola Mundo Pass");

namespace {
  // Hola2 - The second implementation with getAnalysisUsage implemented.
  struct Hola2 : public FunctionPass {
    static char ID; // Pass identification, replacement for typeid
    Hola2() : FunctionPass(ID) {}

    bool runOnFunction(Function &F) override {
      ++HolaCounter;
      errs() << "Hola: ";
      errs().write_escaped(F.getName()) << '\n';
      return false;
    }

    // We don't modify the program, so we preserve all analyses.
    void getAnalysisUsage(AnalysisUsage &AU) const override {
      AU.setPreservesAll();
    }
  };
}

char Hola2::ID = 0;
static RegisterPass<Hola2>
Y("hola2", "Hola Mundo Pass (with getAnalysisUsage implemented)");
