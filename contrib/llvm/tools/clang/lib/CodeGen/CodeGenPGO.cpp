//===--- CodeGenPGO.cpp - PGO Instrumentation for LLVM CodeGen --*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Instrumentation-based profile-guided optimization
//
//===----------------------------------------------------------------------===//

#include "CodeGenPGO.h"
#include "CodeGenFunction.h"
#include "clang/AST/RecursiveASTVisitor.h"
#include "clang/AST/StmtVisitor.h"
#include "llvm/IR/MDBuilder.h"
#include "llvm/ProfileData/InstrProfReader.h"
#include "llvm/Support/Endian.h"
#include "llvm/Support/FileSystem.h"
#include "llvm/Support/MD5.h"

using namespace clang;
using namespace CodeGen;

void CodeGenPGO::setFuncName(llvm::Function *Fn) {
  RawFuncName = Fn->getName();

  // Function names may be prefixed with a binary '1' to indicate
  // that the backend should not modify the symbols due to any platform
  // naming convention. Do not include that '1' in the PGO profile name.
  if (RawFuncName[0] == '\1')
    RawFuncName = RawFuncName.substr(1);

  if (!Fn->hasLocalLinkage()) {
    PrefixedFuncName.reset(new std::string(RawFuncName));
    return;
  }

  // For local symbols, prepend the main file name to distinguish them.
  // Do not include the full path in the file name since there's no guarantee
  // that it will stay the same, e.g., if the files are checked out from
  // version control in different locations.
  PrefixedFuncName.reset(new std::string(CGM.getCodeGenOpts().MainFileName));
  if (PrefixedFuncName->empty())
    PrefixedFuncName->assign("<unknown>");
  PrefixedFuncName->append(":");
  PrefixedFuncName->append(RawFuncName);
}

static llvm::Function *getRegisterFunc(CodeGenModule &CGM) {
  return CGM.getModule().getFunction("__llvm_profile_register_functions");
}

static llvm::BasicBlock *getOrInsertRegisterBB(CodeGenModule &CGM) {
  // Don't do this for Darwin.  compiler-rt uses linker magic.
  if (CGM.getTarget().getTriple().isOSDarwin())
    return nullptr;

  // Only need to insert this once per module.
  if (llvm::Function *RegisterF = getRegisterFunc(CGM))
    return &RegisterF->getEntryBlock();

  // Construct the function.
  auto *VoidTy = llvm::Type::getVoidTy(CGM.getLLVMContext());
  auto *RegisterFTy = llvm::FunctionType::get(VoidTy, false);
  auto *RegisterF = llvm::Function::Create(RegisterFTy,
                                           llvm::GlobalValue::InternalLinkage,
                                           "__llvm_profile_register_functions",
                                           &CGM.getModule());
  RegisterF->setUnnamedAddr(true);
  if (CGM.getCodeGenOpts().DisableRedZone)
    RegisterF->addFnAttr(llvm::Attribute::NoRedZone);

  // Construct and return the entry block.
  auto *BB = llvm::BasicBlock::Create(CGM.getLLVMContext(), "", RegisterF);
  CGBuilderTy Builder(BB);
  Builder.CreateRetVoid();
  return BB;
}

static llvm::Constant *getOrInsertRuntimeRegister(CodeGenModule &CGM) {
  auto *VoidTy = llvm::Type::getVoidTy(CGM.getLLVMContext());
  auto *VoidPtrTy = llvm::Type::getInt8PtrTy(CGM.getLLVMContext());
  auto *RuntimeRegisterTy = llvm::FunctionType::get(VoidTy, VoidPtrTy, false);
  return CGM.getModule().getOrInsertFunction("__llvm_profile_register_function",
                                             RuntimeRegisterTy);
}

static bool isMachO(const CodeGenModule &CGM) {
  return CGM.getTarget().getTriple().isOSBinFormatMachO();
}

static StringRef getCountersSection(const CodeGenModule &CGM) {
  return isMachO(CGM) ? "__DATA,__llvm_prf_cnts" : "__llvm_prf_cnts";
}

static StringRef getNameSection(const CodeGenModule &CGM) {
  return isMachO(CGM) ? "__DATA,__llvm_prf_names" : "__llvm_prf_names";
}

static StringRef getDataSection(const CodeGenModule &CGM) {
  return isMachO(CGM) ? "__DATA,__llvm_prf_data" : "__llvm_prf_data";
}

llvm::GlobalVariable *CodeGenPGO::buildDataVar() {
  // Create name variable.
  llvm::LLVMContext &Ctx = CGM.getLLVMContext();
  auto *VarName = llvm::ConstantDataArray::getString(Ctx, getFuncName(),
                                                     false);
  auto *Name = new llvm::GlobalVariable(CGM.getModule(), VarName->getType(),
                                        true, VarLinkage, VarName,
                                        getFuncVarName("name"));
  Name->setSection(getNameSection(CGM));
  Name->setAlignment(1);

  // Create data variable.
  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  auto *Int64Ty = llvm::Type::getInt64Ty(Ctx);
  auto *Int8PtrTy = llvm::Type::getInt8PtrTy(Ctx);
  auto *Int64PtrTy = llvm::Type::getInt64PtrTy(Ctx);
  llvm::Type *DataTypes[] = {
    Int32Ty, Int32Ty, Int64Ty, Int8PtrTy, Int64PtrTy
  };
  auto *DataTy = llvm::StructType::get(Ctx, makeArrayRef(DataTypes));
  llvm::Constant *DataVals[] = {
    llvm::ConstantInt::get(Int32Ty, getFuncName().size()),
    llvm::ConstantInt::get(Int32Ty, NumRegionCounters),
    llvm::ConstantInt::get(Int64Ty, FunctionHash),
    llvm::ConstantExpr::getBitCast(Name, Int8PtrTy),
    llvm::ConstantExpr::getBitCast(RegionCounters, Int64PtrTy)
  };
  auto *Data =
    new llvm::GlobalVariable(CGM.getModule(), DataTy, true, VarLinkage,
                             llvm::ConstantStruct::get(DataTy, DataVals),
                             getFuncVarName("data"));

  // All the data should be packed into an array in its own section.
  Data->setSection(getDataSection(CGM));
  Data->setAlignment(8);

  // Hide all these symbols so that we correctly get a copy for each
  // executable.  The profile format expects names and counters to be
  // contiguous, so references into shared objects would be invalid.
  if (!llvm::GlobalValue::isLocalLinkage(VarLinkage)) {
    Name->setVisibility(llvm::GlobalValue::HiddenVisibility);
    Data->setVisibility(llvm::GlobalValue::HiddenVisibility);
    RegionCounters->setVisibility(llvm::GlobalValue::HiddenVisibility);
  }

  // Make sure the data doesn't get deleted.
  CGM.addUsedGlobal(Data);
  return Data;
}

void CodeGenPGO::emitInstrumentationData() {
  if (!RegionCounters)
    return;

  // Build the data.
  auto *Data = buildDataVar();

  // Register the data.
  auto *RegisterBB = getOrInsertRegisterBB(CGM);
  if (!RegisterBB)
    return;
  CGBuilderTy Builder(RegisterBB->getTerminator());
  auto *VoidPtrTy = llvm::Type::getInt8PtrTy(CGM.getLLVMContext());
  Builder.CreateCall(getOrInsertRuntimeRegister(CGM),
                     Builder.CreateBitCast(Data, VoidPtrTy));
}

llvm::Function *CodeGenPGO::emitInitialization(CodeGenModule &CGM) {
  if (!CGM.getCodeGenOpts().ProfileInstrGenerate)
    return nullptr;

  assert(CGM.getModule().getFunction("__llvm_profile_init") == nullptr &&
         "profile initialization already emitted");

  // Get the function to call at initialization.
  llvm::Constant *RegisterF = getRegisterFunc(CGM);
  if (!RegisterF)
    return nullptr;

  // Create the initialization function.
  auto *VoidTy = llvm::Type::getVoidTy(CGM.getLLVMContext());
  auto *F = llvm::Function::Create(llvm::FunctionType::get(VoidTy, false),
                                   llvm::GlobalValue::InternalLinkage,
                                   "__llvm_profile_init", &CGM.getModule());
  F->setUnnamedAddr(true);
  F->addFnAttr(llvm::Attribute::NoInline);
  if (CGM.getCodeGenOpts().DisableRedZone)
    F->addFnAttr(llvm::Attribute::NoRedZone);

  // Add the basic block and the necessary calls.
  CGBuilderTy Builder(llvm::BasicBlock::Create(CGM.getLLVMContext(), "", F));
  Builder.CreateCall(RegisterF);
  Builder.CreateRetVoid();

  return F;
}

namespace {
/// \brief Stable hasher for PGO region counters.
///
/// PGOHash produces a stable hash of a given function's control flow.
///
/// Changing the output of this hash will invalidate all previously generated
/// profiles -- i.e., don't do it.
///
/// \note  When this hash does eventually change (years?), we still need to
/// support old hashes.  We'll need to pull in the version number from the
/// profile data format and use the matching hash function.
class PGOHash {
  uint64_t Working;
  unsigned Count;
  llvm::MD5 MD5;

  static const int NumBitsPerType = 6;
  static const unsigned NumTypesPerWord = sizeof(uint64_t) * 8 / NumBitsPerType;
  static const unsigned TooBig = 1u << NumBitsPerType;

public:
  /// \brief Hash values for AST nodes.
  ///
  /// Distinct values for AST nodes that have region counters attached.
  ///
  /// These values must be stable.  All new members must be added at the end,
  /// and no members should be removed.  Changing the enumeration value for an
  /// AST node will affect the hash of every function that contains that node.
  enum HashType : unsigned char {
    None = 0,
    LabelStmt = 1,
    WhileStmt,
    DoStmt,
    ForStmt,
    CXXForRangeStmt,
    ObjCForCollectionStmt,
    SwitchStmt,
    CaseStmt,
    DefaultStmt,
    IfStmt,
    CXXTryStmt,
    CXXCatchStmt,
    ConditionalOperator,
    BinaryOperatorLAnd,
    BinaryOperatorLOr,
    BinaryConditionalOperator,

    // Keep this last.  It's for the static assert that follows.
    LastHashType
  };
  static_assert(LastHashType <= TooBig, "Too many types in HashType");

  // TODO: When this format changes, take in a version number here, and use the
  // old hash calculation for file formats that used the old hash.
  PGOHash() : Working(0), Count(0) {}
  void combine(HashType Type);
  uint64_t finalize();
};
const int PGOHash::NumBitsPerType;
const unsigned PGOHash::NumTypesPerWord;
const unsigned PGOHash::TooBig;

  /// A RecursiveASTVisitor that fills a map of statements to PGO counters.
  struct MapRegionCounters : public RecursiveASTVisitor<MapRegionCounters> {
    /// The next counter value to assign.
    unsigned NextCounter;
    /// The function hash.
    PGOHash Hash;
    /// The map of statements to counters.
    llvm::DenseMap<const Stmt *, unsigned> &CounterMap;

    MapRegionCounters(llvm::DenseMap<const Stmt *, unsigned> &CounterMap)
        : NextCounter(0), CounterMap(CounterMap) {}

    // Blocks and lambdas are handled as separate functions, so we need not
    // traverse them in the parent context.
    bool TraverseBlockExpr(BlockExpr *BE) { return true; }
    bool TraverseLambdaBody(LambdaExpr *LE) { return true; }
    bool TraverseCapturedStmt(CapturedStmt *CS) { return true; }

    bool VisitDecl(const Decl *D) {
      switch (D->getKind()) {
      default:
        break;
      case Decl::Function:
      case Decl::CXXMethod:
      case Decl::CXXConstructor:
      case Decl::CXXDestructor:
      case Decl::CXXConversion:
      case Decl::ObjCMethod:
      case Decl::Block:
      case Decl::Captured:
        CounterMap[D->getBody()] = NextCounter++;
        break;
      }
      return true;
    }

    bool VisitStmt(const Stmt *S) {
      auto Type = getHashType(S);
      if (Type == PGOHash::None)
        return true;

      CounterMap[S] = NextCounter++;
      Hash.combine(Type);
      return true;
    }
    PGOHash::HashType getHashType(const Stmt *S) {
      switch (S->getStmtClass()) {
      default:
        break;
      case Stmt::LabelStmtClass:
        return PGOHash::LabelStmt;
      case Stmt::WhileStmtClass:
        return PGOHash::WhileStmt;
      case Stmt::DoStmtClass:
        return PGOHash::DoStmt;
      case Stmt::ForStmtClass:
        return PGOHash::ForStmt;
      case Stmt::CXXForRangeStmtClass:
        return PGOHash::CXXForRangeStmt;
      case Stmt::ObjCForCollectionStmtClass:
        return PGOHash::ObjCForCollectionStmt;
      case Stmt::SwitchStmtClass:
        return PGOHash::SwitchStmt;
      case Stmt::CaseStmtClass:
        return PGOHash::CaseStmt;
      case Stmt::DefaultStmtClass:
        return PGOHash::DefaultStmt;
      case Stmt::IfStmtClass:
        return PGOHash::IfStmt;
      case Stmt::CXXTryStmtClass:
        return PGOHash::CXXTryStmt;
      case Stmt::CXXCatchStmtClass:
        return PGOHash::CXXCatchStmt;
      case Stmt::ConditionalOperatorClass:
        return PGOHash::ConditionalOperator;
      case Stmt::BinaryConditionalOperatorClass:
        return PGOHash::BinaryConditionalOperator;
      case Stmt::BinaryOperatorClass: {
        const BinaryOperator *BO = cast<BinaryOperator>(S);
        if (BO->getOpcode() == BO_LAnd)
          return PGOHash::BinaryOperatorLAnd;
        if (BO->getOpcode() == BO_LOr)
          return PGOHash::BinaryOperatorLOr;
        break;
      }
      }
      return PGOHash::None;
    }
  };

  /// A StmtVisitor that propagates the raw counts through the AST and
  /// records the count at statements where the value may change.
  struct ComputeRegionCounts : public ConstStmtVisitor<ComputeRegionCounts> {
    /// PGO state.
    CodeGenPGO &PGO;

    /// A flag that is set when the current count should be recorded on the
    /// next statement, such as at the exit of a loop.
    bool RecordNextStmtCount;

    /// The map of statements to count values.
    llvm::DenseMap<const Stmt *, uint64_t> &CountMap;

    /// BreakContinueStack - Keep counts of breaks and continues inside loops.
    struct BreakContinue {
      uint64_t BreakCount;
      uint64_t ContinueCount;
      BreakContinue() : BreakCount(0), ContinueCount(0) {}
    };
    SmallVector<BreakContinue, 8> BreakContinueStack;

    ComputeRegionCounts(llvm::DenseMap<const Stmt *, uint64_t> &CountMap,
                        CodeGenPGO &PGO)
        : PGO(PGO), RecordNextStmtCount(false), CountMap(CountMap) {}

    void RecordStmtCount(const Stmt *S) {
      if (RecordNextStmtCount) {
        CountMap[S] = PGO.getCurrentRegionCount();
        RecordNextStmtCount = false;
      }
    }

    void VisitStmt(const Stmt *S) {
      RecordStmtCount(S);
      for (Stmt::const_child_range I = S->children(); I; ++I) {
        if (*I)
         this->Visit(*I);
      }
    }

    void VisitFunctionDecl(const FunctionDecl *D) {
      // Counter tracks entry to the function body.
      RegionCounter Cnt(PGO, D->getBody());
      Cnt.beginRegion();
      CountMap[D->getBody()] = PGO.getCurrentRegionCount();
      Visit(D->getBody());
    }

    // Skip lambda expressions. We visit these as FunctionDecls when we're
    // generating them and aren't interested in the body when generating a
    // parent context.
    void VisitLambdaExpr(const LambdaExpr *LE) {}

    void VisitCapturedDecl(const CapturedDecl *D) {
      // Counter tracks entry to the capture body.
      RegionCounter Cnt(PGO, D->getBody());
      Cnt.beginRegion();
      CountMap[D->getBody()] = PGO.getCurrentRegionCount();
      Visit(D->getBody());
    }

    void VisitObjCMethodDecl(const ObjCMethodDecl *D) {
      // Counter tracks entry to the method body.
      RegionCounter Cnt(PGO, D->getBody());
      Cnt.beginRegion();
      CountMap[D->getBody()] = PGO.getCurrentRegionCount();
      Visit(D->getBody());
    }

    void VisitBlockDecl(const BlockDecl *D) {
      // Counter tracks entry to the block body.
      RegionCounter Cnt(PGO, D->getBody());
      Cnt.beginRegion();
      CountMap[D->getBody()] = PGO.getCurrentRegionCount();
      Visit(D->getBody());
    }

    void VisitReturnStmt(const ReturnStmt *S) {
      RecordStmtCount(S);
      if (S->getRetValue())
        Visit(S->getRetValue());
      PGO.setCurrentRegionUnreachable();
      RecordNextStmtCount = true;
    }

    void VisitGotoStmt(const GotoStmt *S) {
      RecordStmtCount(S);
      PGO.setCurrentRegionUnreachable();
      RecordNextStmtCount = true;
    }

    void VisitLabelStmt(const LabelStmt *S) {
      RecordNextStmtCount = false;
      // Counter tracks the block following the label.
      RegionCounter Cnt(PGO, S);
      Cnt.beginRegion();
      CountMap[S] = PGO.getCurrentRegionCount();
      Visit(S->getSubStmt());
    }

    void VisitBreakStmt(const BreakStmt *S) {
      RecordStmtCount(S);
      assert(!BreakContinueStack.empty() && "break not in a loop or switch!");
      BreakContinueStack.back().BreakCount += PGO.getCurrentRegionCount();
      PGO.setCurrentRegionUnreachable();
      RecordNextStmtCount = true;
    }

    void VisitContinueStmt(const ContinueStmt *S) {
      RecordStmtCount(S);
      assert(!BreakContinueStack.empty() && "continue stmt not in a loop!");
      BreakContinueStack.back().ContinueCount += PGO.getCurrentRegionCount();
      PGO.setCurrentRegionUnreachable();
      RecordNextStmtCount = true;
    }

    void VisitWhileStmt(const WhileStmt *S) {
      RecordStmtCount(S);
      // Counter tracks the body of the loop.
      RegionCounter Cnt(PGO, S);
      BreakContinueStack.push_back(BreakContinue());
      // Visit the body region first so the break/continue adjustments can be
      // included when visiting the condition.
      Cnt.beginRegion();
      CountMap[S->getBody()] = PGO.getCurrentRegionCount();
      Visit(S->getBody());
      Cnt.adjustForControlFlow();

      // ...then go back and propagate counts through the condition. The count
      // at the start of the condition is the sum of the incoming edges,
      // the backedge from the end of the loop body, and the edges from
      // continue statements.
      BreakContinue BC = BreakContinueStack.pop_back_val();
      Cnt.setCurrentRegionCount(Cnt.getParentCount() +
                                Cnt.getAdjustedCount() + BC.ContinueCount);
      CountMap[S->getCond()] = PGO.getCurrentRegionCount();
      Visit(S->getCond());
      Cnt.adjustForControlFlow();
      Cnt.applyAdjustmentsToRegion(BC.BreakCount + BC.ContinueCount);
      RecordNextStmtCount = true;
    }

    void VisitDoStmt(const DoStmt *S) {
      RecordStmtCount(S);
      // Counter tracks the body of the loop.
      RegionCounter Cnt(PGO, S);
      BreakContinueStack.push_back(BreakContinue());
      Cnt.beginRegion(/*AddIncomingFallThrough=*/true);
      CountMap[S->getBody()] = PGO.getCurrentRegionCount();
      Visit(S->getBody());
      Cnt.adjustForControlFlow();

      BreakContinue BC = BreakContinueStack.pop_back_val();
      // The count at the start of the condition is equal to the count at the
      // end of the body. The adjusted count does not include either the
      // fall-through count coming into the loop or the continue count, so add
      // both of those separately. This is coincidentally the same equation as
      // with while loops but for different reasons.
      Cnt.setCurrentRegionCount(Cnt.getParentCount() +
                                Cnt.getAdjustedCount() + BC.ContinueCount);
      CountMap[S->getCond()] = PGO.getCurrentRegionCount();
      Visit(S->getCond());
      Cnt.adjustForControlFlow();
      Cnt.applyAdjustmentsToRegion(BC.BreakCount + BC.ContinueCount);
      RecordNextStmtCount = true;
    }

    void VisitForStmt(const ForStmt *S) {
      RecordStmtCount(S);
      if (S->getInit())
        Visit(S->getInit());
      // Counter tracks the body of the loop.
      RegionCounter Cnt(PGO, S);
      BreakContinueStack.push_back(BreakContinue());
      // Visit the body region first. (This is basically the same as a while
      // loop; see further comments in VisitWhileStmt.)
      Cnt.beginRegion();
      CountMap[S->getBody()] = PGO.getCurrentRegionCount();
      Visit(S->getBody());
      Cnt.adjustForControlFlow();

      // The increment is essentially part of the body but it needs to include
      // the count for all the continue statements.
      if (S->getInc()) {
        Cnt.setCurrentRegionCount(PGO.getCurrentRegionCount() +
                                  BreakContinueStack.back().ContinueCount);
        CountMap[S->getInc()] = PGO.getCurrentRegionCount();
        Visit(S->getInc());
        Cnt.adjustForControlFlow();
      }

      BreakContinue BC = BreakContinueStack.pop_back_val();

      // ...then go back and propagate counts through the condition.
      if (S->getCond()) {
        Cnt.setCurrentRegionCount(Cnt.getParentCount() +
                                  Cnt.getAdjustedCount() +
                                  BC.ContinueCount);
        CountMap[S->getCond()] = PGO.getCurrentRegionCount();
        Visit(S->getCond());
        Cnt.adjustForControlFlow();
      }
      Cnt.applyAdjustmentsToRegion(BC.BreakCount + BC.ContinueCount);
      RecordNextStmtCount = true;
    }

    void VisitCXXForRangeStmt(const CXXForRangeStmt *S) {
      RecordStmtCount(S);
      Visit(S->getRangeStmt());
      Visit(S->getBeginEndStmt());
      // Counter tracks the body of the loop.
      RegionCounter Cnt(PGO, S);
      BreakContinueStack.push_back(BreakContinue());
      // Visit the body region first. (This is basically the same as a while
      // loop; see further comments in VisitWhileStmt.)
      Cnt.beginRegion();
      CountMap[S->getLoopVarStmt()] = PGO.getCurrentRegionCount();
      Visit(S->getLoopVarStmt());
      Visit(S->getBody());
      Cnt.adjustForControlFlow();

      // The increment is essentially part of the body but it needs to include
      // the count for all the continue statements.
      Cnt.setCurrentRegionCount(PGO.getCurrentRegionCount() +
                                BreakContinueStack.back().ContinueCount);
      CountMap[S->getInc()] = PGO.getCurrentRegionCount();
      Visit(S->getInc());
      Cnt.adjustForControlFlow();

      BreakContinue BC = BreakContinueStack.pop_back_val();

      // ...then go back and propagate counts through the condition.
      Cnt.setCurrentRegionCount(Cnt.getParentCount() +
                                Cnt.getAdjustedCount() +
                                BC.ContinueCount);
      CountMap[S->getCond()] = PGO.getCurrentRegionCount();
      Visit(S->getCond());
      Cnt.adjustForControlFlow();
      Cnt.applyAdjustmentsToRegion(BC.BreakCount + BC.ContinueCount);
      RecordNextStmtCount = true;
    }

    void VisitObjCForCollectionStmt(const ObjCForCollectionStmt *S) {
      RecordStmtCount(S);
      Visit(S->getElement());
      // Counter tracks the body of the loop.
      RegionCounter Cnt(PGO, S);
      BreakContinueStack.push_back(BreakContinue());
      Cnt.beginRegion();
      CountMap[S->getBody()] = PGO.getCurrentRegionCount();
      Visit(S->getBody());
      BreakContinue BC = BreakContinueStack.pop_back_val();
      Cnt.adjustForControlFlow();
      Cnt.applyAdjustmentsToRegion(BC.BreakCount + BC.ContinueCount);
      RecordNextStmtCount = true;
    }

    void VisitSwitchStmt(const SwitchStmt *S) {
      RecordStmtCount(S);
      Visit(S->getCond());
      PGO.setCurrentRegionUnreachable();
      BreakContinueStack.push_back(BreakContinue());
      Visit(S->getBody());
      // If the switch is inside a loop, add the continue counts.
      BreakContinue BC = BreakContinueStack.pop_back_val();
      if (!BreakContinueStack.empty())
        BreakContinueStack.back().ContinueCount += BC.ContinueCount;
      // Counter tracks the exit block of the switch.
      RegionCounter ExitCnt(PGO, S);
      ExitCnt.beginRegion();
      RecordNextStmtCount = true;
    }

    void VisitCaseStmt(const CaseStmt *S) {
      RecordNextStmtCount = false;
      // Counter for this particular case. This counts only jumps from the
      // switch header and does not include fallthrough from the case before
      // this one.
      RegionCounter Cnt(PGO, S);
      Cnt.beginRegion(/*AddIncomingFallThrough=*/true);
      CountMap[S] = Cnt.getCount();
      RecordNextStmtCount = true;
      Visit(S->getSubStmt());
    }

    void VisitDefaultStmt(const DefaultStmt *S) {
      RecordNextStmtCount = false;
      // Counter for this default case. This does not include fallthrough from
      // the previous case.
      RegionCounter Cnt(PGO, S);
      Cnt.beginRegion(/*AddIncomingFallThrough=*/true);
      CountMap[S] = Cnt.getCount();
      RecordNextStmtCount = true;
      Visit(S->getSubStmt());
    }

    void VisitIfStmt(const IfStmt *S) {
      RecordStmtCount(S);
      // Counter tracks the "then" part of an if statement. The count for
      // the "else" part, if it exists, will be calculated from this counter.
      RegionCounter Cnt(PGO, S);
      Visit(S->getCond());

      Cnt.beginRegion();
      CountMap[S->getThen()] = PGO.getCurrentRegionCount();
      Visit(S->getThen());
      Cnt.adjustForControlFlow();

      if (S->getElse()) {
        Cnt.beginElseRegion();
        CountMap[S->getElse()] = PGO.getCurrentRegionCount();
        Visit(S->getElse());
        Cnt.adjustForControlFlow();
      }
      Cnt.applyAdjustmentsToRegion(0);
      RecordNextStmtCount = true;
    }

    void VisitCXXTryStmt(const CXXTryStmt *S) {
      RecordStmtCount(S);
      Visit(S->getTryBlock());
      for (unsigned I = 0, E = S->getNumHandlers(); I < E; ++I)
        Visit(S->getHandler(I));
      // Counter tracks the continuation block of the try statement.
      RegionCounter Cnt(PGO, S);
      Cnt.beginRegion();
      RecordNextStmtCount = true;
    }

    void VisitCXXCatchStmt(const CXXCatchStmt *S) {
      RecordNextStmtCount = false;
      // Counter tracks the catch statement's handler block.
      RegionCounter Cnt(PGO, S);
      Cnt.beginRegion();
      CountMap[S] = PGO.getCurrentRegionCount();
      Visit(S->getHandlerBlock());
    }

    void VisitAbstractConditionalOperator(
        const AbstractConditionalOperator *E) {
      RecordStmtCount(E);
      // Counter tracks the "true" part of a conditional operator. The
      // count in the "false" part will be calculated from this counter.
      RegionCounter Cnt(PGO, E);
      Visit(E->getCond());

      Cnt.beginRegion();
      CountMap[E->getTrueExpr()] = PGO.getCurrentRegionCount();
      Visit(E->getTrueExpr());
      Cnt.adjustForControlFlow();

      Cnt.beginElseRegion();
      CountMap[E->getFalseExpr()] = PGO.getCurrentRegionCount();
      Visit(E->getFalseExpr());
      Cnt.adjustForControlFlow();

      Cnt.applyAdjustmentsToRegion(0);
      RecordNextStmtCount = true;
    }

    void VisitBinLAnd(const BinaryOperator *E) {
      RecordStmtCount(E);
      // Counter tracks the right hand side of a logical and operator.
      RegionCounter Cnt(PGO, E);
      Visit(E->getLHS());
      Cnt.beginRegion();
      CountMap[E->getRHS()] = PGO.getCurrentRegionCount();
      Visit(E->getRHS());
      Cnt.adjustForControlFlow();
      Cnt.applyAdjustmentsToRegion(0);
      RecordNextStmtCount = true;
    }

    void VisitBinLOr(const BinaryOperator *E) {
      RecordStmtCount(E);
      // Counter tracks the right hand side of a logical or operator.
      RegionCounter Cnt(PGO, E);
      Visit(E->getLHS());
      Cnt.beginRegion();
      CountMap[E->getRHS()] = PGO.getCurrentRegionCount();
      Visit(E->getRHS());
      Cnt.adjustForControlFlow();
      Cnt.applyAdjustmentsToRegion(0);
      RecordNextStmtCount = true;
    }
  };
}

void PGOHash::combine(HashType Type) {
  // Check that we never combine 0 and only have six bits.
  assert(Type && "Hash is invalid: unexpected type 0");
  assert(unsigned(Type) < TooBig && "Hash is invalid: too many types");

  // Pass through MD5 if enough work has built up.
  if (Count && Count % NumTypesPerWord == 0) {
    using namespace llvm::support;
    uint64_t Swapped = endian::byte_swap<uint64_t, little>(Working);
    MD5.update(llvm::makeArrayRef((uint8_t *)&Swapped, sizeof(Swapped)));
    Working = 0;
  }

  // Accumulate the current type.
  ++Count;
  Working = Working << NumBitsPerType | Type;
}

uint64_t PGOHash::finalize() {
  // Use Working as the hash directly if we never used MD5.
  if (Count <= NumTypesPerWord)
    // No need to byte swap here, since none of the math was endian-dependent.
    // This number will be byte-swapped as required on endianness transitions,
    // so we will see the same value on the other side.
    return Working;

  // Check for remaining work in Working.
  if (Working)
    MD5.update(Working);

  // Finalize the MD5 and return the hash.
  llvm::MD5::MD5Result Result;
  MD5.final(Result);
  using namespace llvm::support;
  return endian::read<uint64_t, little, unaligned>(Result);
}

static void emitRuntimeHook(CodeGenModule &CGM) {
  const char *const RuntimeVarName = "__llvm_profile_runtime";
  const char *const RuntimeUserName = "__llvm_profile_runtime_user";
  if (CGM.getModule().getGlobalVariable(RuntimeVarName))
    return;

  // Declare the runtime hook.
  llvm::LLVMContext &Ctx = CGM.getLLVMContext();
  auto *Int32Ty = llvm::Type::getInt32Ty(Ctx);
  auto *Var = new llvm::GlobalVariable(CGM.getModule(), Int32Ty, false,
                                       llvm::GlobalValue::ExternalLinkage,
                                       nullptr, RuntimeVarName);

  // Make a function that uses it.
  auto *User = llvm::Function::Create(llvm::FunctionType::get(Int32Ty, false),
                                      llvm::GlobalValue::LinkOnceODRLinkage,
                                      RuntimeUserName, &CGM.getModule());
  User->addFnAttr(llvm::Attribute::NoInline);
  if (CGM.getCodeGenOpts().DisableRedZone)
    User->addFnAttr(llvm::Attribute::NoRedZone);
  CGBuilderTy Builder(llvm::BasicBlock::Create(CGM.getLLVMContext(), "", User));
  auto *Load = Builder.CreateLoad(Var);
  Builder.CreateRet(Load);

  // Create a use of the function.  Now the definition of the runtime variable
  // should get pulled in, along with any static initializears.
  CGM.addUsedGlobal(User);
}

void CodeGenPGO::assignRegionCounters(const Decl *D, llvm::Function *Fn) {
  bool InstrumentRegions = CGM.getCodeGenOpts().ProfileInstrGenerate;
  llvm::IndexedInstrProfReader *PGOReader = CGM.getPGOReader();
  if (!InstrumentRegions && !PGOReader)
    return;
  if (D->isImplicit())
    return;
  setFuncName(Fn);

  // Set the linkage for variables based on the function linkage.  Usually, we
  // want to match it, but available_externally and extern_weak both have the
  // wrong semantics.
  VarLinkage = Fn->getLinkage();
  switch (VarLinkage) {
  case llvm::GlobalValue::ExternalWeakLinkage:
    VarLinkage = llvm::GlobalValue::LinkOnceAnyLinkage;
    break;
  case llvm::GlobalValue::AvailableExternallyLinkage:
    VarLinkage = llvm::GlobalValue::LinkOnceODRLinkage;
    break;
  default:
    break;
  }

  mapRegionCounters(D);
  if (InstrumentRegions) {
    emitRuntimeHook(CGM);
    emitCounterVariables();
  }
  if (PGOReader) {
    SourceManager &SM = CGM.getContext().getSourceManager();
    loadRegionCounts(PGOReader, SM.isInMainFile(D->getLocation()));
    computeRegionCounts(D);
    applyFunctionAttributes(PGOReader, Fn);
  }
}

void CodeGenPGO::mapRegionCounters(const Decl *D) {
  RegionCounterMap.reset(new llvm::DenseMap<const Stmt *, unsigned>);
  MapRegionCounters Walker(*RegionCounterMap);
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D))
    Walker.TraverseDecl(const_cast<FunctionDecl *>(FD));
  else if (const ObjCMethodDecl *MD = dyn_cast_or_null<ObjCMethodDecl>(D))
    Walker.TraverseDecl(const_cast<ObjCMethodDecl *>(MD));
  else if (const BlockDecl *BD = dyn_cast_or_null<BlockDecl>(D))
    Walker.TraverseDecl(const_cast<BlockDecl *>(BD));
  else if (const CapturedDecl *CD = dyn_cast_or_null<CapturedDecl>(D))
    Walker.TraverseDecl(const_cast<CapturedDecl *>(CD));
  assert(Walker.NextCounter > 0 && "no entry counter mapped for decl");
  NumRegionCounters = Walker.NextCounter;
  FunctionHash = Walker.Hash.finalize();
}

void CodeGenPGO::computeRegionCounts(const Decl *D) {
  StmtCountMap.reset(new llvm::DenseMap<const Stmt *, uint64_t>);
  ComputeRegionCounts Walker(*StmtCountMap, *this);
  if (const FunctionDecl *FD = dyn_cast_or_null<FunctionDecl>(D))
    Walker.VisitFunctionDecl(FD);
  else if (const ObjCMethodDecl *MD = dyn_cast_or_null<ObjCMethodDecl>(D))
    Walker.VisitObjCMethodDecl(MD);
  else if (const BlockDecl *BD = dyn_cast_or_null<BlockDecl>(D))
    Walker.VisitBlockDecl(BD);
  else if (const CapturedDecl *CD = dyn_cast_or_null<CapturedDecl>(D))
    Walker.VisitCapturedDecl(const_cast<CapturedDecl *>(CD));
}

void
CodeGenPGO::applyFunctionAttributes(llvm::IndexedInstrProfReader *PGOReader,
                                    llvm::Function *Fn) {
  if (!haveRegionCounts())
    return;

  uint64_t MaxFunctionCount = PGOReader->getMaximumFunctionCount();
  uint64_t FunctionCount = getRegionCount(0);
  if (FunctionCount >= (uint64_t)(0.3 * (double)MaxFunctionCount))
    // Turn on InlineHint attribute for hot functions.
    // FIXME: 30% is from preliminary tuning on SPEC, it may not be optimal.
    Fn->addFnAttr(llvm::Attribute::InlineHint);
  else if (FunctionCount <= (uint64_t)(0.01 * (double)MaxFunctionCount))
    // Turn on Cold attribute for cold functions.
    // FIXME: 1% is from preliminary tuning on SPEC, it may not be optimal.
    Fn->addFnAttr(llvm::Attribute::Cold);
}

void CodeGenPGO::emitCounterVariables() {
  llvm::LLVMContext &Ctx = CGM.getLLVMContext();
  llvm::ArrayType *CounterTy = llvm::ArrayType::get(llvm::Type::getInt64Ty(Ctx),
                                                    NumRegionCounters);
  RegionCounters =
    new llvm::GlobalVariable(CGM.getModule(), CounterTy, false, VarLinkage,
                             llvm::Constant::getNullValue(CounterTy),
                             getFuncVarName("counters"));
  RegionCounters->setAlignment(8);
  RegionCounters->setSection(getCountersSection(CGM));
}

void CodeGenPGO::emitCounterIncrement(CGBuilderTy &Builder, unsigned Counter) {
  if (!RegionCounters)
    return;
  llvm::Value *Addr =
    Builder.CreateConstInBoundsGEP2_64(RegionCounters, 0, Counter);
  llvm::Value *Count = Builder.CreateLoad(Addr, "pgocount");
  Count = Builder.CreateAdd(Count, Builder.getInt64(1));
  Builder.CreateStore(Count, Addr);
}

void CodeGenPGO::loadRegionCounts(llvm::IndexedInstrProfReader *PGOReader,
                                  bool IsInMainFile) {
  CGM.getPGOStats().addVisited(IsInMainFile);
  RegionCounts.reset(new std::vector<uint64_t>);
  uint64_t Hash;
  if (PGOReader->getFunctionCounts(getFuncName(), Hash, *RegionCounts)) {
    CGM.getPGOStats().addMissing(IsInMainFile);
    RegionCounts.reset();
  } else if (Hash != FunctionHash ||
             RegionCounts->size() != NumRegionCounters) {
    CGM.getPGOStats().addMismatched(IsInMainFile);
    RegionCounts.reset();
  }
}

void CodeGenPGO::destroyRegionCounters() {
  RegionCounterMap.reset();
  StmtCountMap.reset();
  RegionCounts.reset();
  RegionCounters = nullptr;
}

/// \brief Calculate what to divide by to scale weights.
///
/// Given the maximum weight, calculate a divisor that will scale all the
/// weights to strictly less than UINT32_MAX.
static uint64_t calculateWeightScale(uint64_t MaxWeight) {
  return MaxWeight < UINT32_MAX ? 1 : MaxWeight / UINT32_MAX + 1;
}

/// \brief Scale an individual branch weight (and add 1).
///
/// Scale a 64-bit weight down to 32-bits using \c Scale.
///
/// According to Laplace's Rule of Succession, it is better to compute the
/// weight based on the count plus 1, so universally add 1 to the value.
///
/// \pre \c Scale was calculated by \a calculateWeightScale() with a weight no
/// greater than \c Weight.
static uint32_t scaleBranchWeight(uint64_t Weight, uint64_t Scale) {
  assert(Scale && "scale by 0?");
  uint64_t Scaled = Weight / Scale + 1;
  assert(Scaled <= UINT32_MAX && "overflow 32-bits");
  return Scaled;
}

llvm::MDNode *CodeGenPGO::createBranchWeights(uint64_t TrueCount,
                                              uint64_t FalseCount) {
  // Check for empty weights.
  if (!TrueCount && !FalseCount)
    return nullptr;

  // Calculate how to scale down to 32-bits.
  uint64_t Scale = calculateWeightScale(std::max(TrueCount, FalseCount));

  llvm::MDBuilder MDHelper(CGM.getLLVMContext());
  return MDHelper.createBranchWeights(scaleBranchWeight(TrueCount, Scale),
                                      scaleBranchWeight(FalseCount, Scale));
}

llvm::MDNode *CodeGenPGO::createBranchWeights(ArrayRef<uint64_t> Weights) {
  // We need at least two elements to create meaningful weights.
  if (Weights.size() < 2)
    return nullptr;

  // Check for empty weights.
  uint64_t MaxWeight = *std::max_element(Weights.begin(), Weights.end());
  if (MaxWeight == 0)
    return nullptr;

  // Calculate how to scale down to 32-bits.
  uint64_t Scale = calculateWeightScale(MaxWeight);

  SmallVector<uint32_t, 16> ScaledWeights;
  ScaledWeights.reserve(Weights.size());
  for (uint64_t W : Weights)
    ScaledWeights.push_back(scaleBranchWeight(W, Scale));

  llvm::MDBuilder MDHelper(CGM.getLLVMContext());
  return MDHelper.createBranchWeights(ScaledWeights);
}

llvm::MDNode *CodeGenPGO::createLoopWeights(const Stmt *Cond,
                                            RegionCounter &Cnt) {
  if (!haveRegionCounts())
    return nullptr;
  uint64_t LoopCount = Cnt.getCount();
  uint64_t CondCount = 0;
  bool Found = getStmtCount(Cond, CondCount);
  assert(Found && "missing expected loop condition count");
  (void)Found;
  if (CondCount == 0)
    return nullptr;
  return createBranchWeights(LoopCount,
                             std::max(CondCount, LoopCount) - LoopCount);
}
