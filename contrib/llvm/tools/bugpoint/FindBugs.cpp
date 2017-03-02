//===-- FindBugs.cpp - Run Many Different Optimizations -------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines an interface that allows bugpoint to choose different
// combinations of optimizations to run on the selected input. Bugpoint will
// run these optimizations and record the success/failure of each. This way
// we can hopefully spot bugs in the optimizations.
//
//===----------------------------------------------------------------------===//

#include "BugDriver.h"
#include "ToolRunner.h"
#include "llvm/Pass.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/raw_ostream.h"
#include <algorithm>
#include <ctime>
using namespace llvm;

Error
BugDriver::runManyPasses(const std::vector<std::string> &AllPasses) {
  setPassesToRun(AllPasses);
  outs() << "Starting bug finding procedure...\n\n";

  // Creating a reference output if necessary
  if (Error E = initializeExecutionEnvironment())
    return E;

  outs() << "\n";
  if (ReferenceOutputFile.empty()) {
    outs() << "Generating reference output from raw program: \n";
    if (Error E = createReferenceFile(Program))
      return E;
  }

  srand(time(nullptr));

  unsigned num = 1;
  while (1) {
    //
    // Step 1: Randomize the order of the optimizer passes.
    //
    std::random_shuffle(PassesToRun.begin(), PassesToRun.end());

    //
    // Step 2: Run optimizer passes on the program and check for success.
    //
    outs() << "Running selected passes on program to test for crash: ";
    for (int i = 0, e = PassesToRun.size(); i != e; i++) {
      outs() << "-" << PassesToRun[i] << " ";
    }

    std::string Filename;
    if (runPasses(Program, PassesToRun, Filename, false)) {
      outs() << "\n";
      outs() << "Optimizer passes caused failure!\n\n";
      return debugOptimizerCrash();
    } else {
      outs() << "Combination " << num << " optimized successfully!\n";
    }

    //
    // Step 3: Compile the optimized code.
    //
    outs() << "Running the code generator to test for a crash: ";
    if (Error E = compileProgram(Program)) {
      outs() << "\n*** compileProgram threw an exception: ";
      outs() << toString(std::move(E));
      return debugCodeGeneratorCrash();
    }
    outs() << '\n';

    //
    // Step 4: Run the program and compare its output to the reference
    // output (created above).
    //
    outs() << "*** Checking if passes caused miscompliation:\n";
    Expected<bool> Diff = diffProgram(Program, Filename, "", false);
    if (Error E = Diff.takeError()) {
      errs() << toString(std::move(E));
      return debugCodeGeneratorCrash();
    }
    if (*Diff) {
      outs() << "\n*** diffProgram returned true!\n";
      Error E = debugMiscompilation();
      if (!E)
        return Error::success();
    }
    outs() << "\n*** diff'd output matches!\n";

    sys::fs::remove(Filename);

    outs() << "\n\n";
    num++;
  } // end while

  // Unreachable.
}
