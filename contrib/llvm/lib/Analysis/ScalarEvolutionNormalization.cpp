//===- ScalarEvolutionNormalization.cpp - See below -------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file implements utilities for working with "normalized" expressions.
// See the comments at the top of ScalarEvolutionNormalization.h for details.
//
//===----------------------------------------------------------------------===//

#include "llvm/Analysis/Dominators.h"
#include "llvm/Analysis/LoopInfo.h"
#include "llvm/Analysis/ScalarEvolutionExpressions.h"
#include "llvm/Analysis/ScalarEvolutionNormalization.h"
using namespace llvm;

/// IVUseShouldUsePostIncValue - We have discovered a "User" of an IV expression
/// and now we need to decide whether the user should use the preinc or post-inc
/// value.  If this user should use the post-inc version of the IV, return true.
///
/// Choosing wrong here can break dominance properties (if we choose to use the
/// post-inc value when we cannot) or it can end up adding extra live-ranges to
/// the loop, resulting in reg-reg copies (if we use the pre-inc value when we
/// should use the post-inc value).
static bool IVUseShouldUsePostIncValue(Instruction *User, Instruction *IV,
                                       const Loop *L, DominatorTree *DT) {
  // If the user is in the loop, use the preinc value.
  if (L->contains(User)) return false;

  BasicBlock *LatchBlock = L->getLoopLatch();
  if (!LatchBlock)
    return false;

  // Ok, the user is outside of the loop.  If it is dominated by the latch
  // block, use the post-inc value.
  if (DT->dominates(LatchBlock, User->getParent()))
    return true;

  // There is one case we have to be careful of: PHI nodes.  These little guys
  // can live in blocks that are not dominated by the latch block, but (since
  // their uses occur in the predecessor block, not the block the PHI lives in)
  // should still use the post-inc value.  Check for this case now.
  PHINode *PN = dyn_cast<PHINode>(User);
  if (!PN) return false;  // not a phi, not dominated by latch block.

  // Look at all of the uses of IV by the PHI node.  If any use corresponds to
  // a block that is not dominated by the latch block, give up and use the
  // preincremented value.
  unsigned NumUses = 0;
  for (unsigned i = 0, e = PN->getNumIncomingValues(); i != e; ++i)
    if (PN->getIncomingValue(i) == IV) {
      ++NumUses;
      if (!DT->dominates(LatchBlock, PN->getIncomingBlock(i)))
        return false;
    }

  // Okay, all uses of IV by PN are in predecessor blocks that really are
  // dominated by the latch block.  Use the post-incremented value.
  return true;
}

const SCEV *llvm::TransformForPostIncUse(TransformKind Kind,
                                         const SCEV *S,
                                         Instruction *User,
                                         Value *OperandValToReplace,
                                         PostIncLoopSet &Loops,
                                         ScalarEvolution &SE,
                                         DominatorTree &DT) {
  if (isa<SCEVConstant>(S) || isa<SCEVUnknown>(S))
    return S;
  if (const SCEVCastExpr *X = dyn_cast<SCEVCastExpr>(S)) {
    const SCEV *O = X->getOperand();
    const SCEV *N = TransformForPostIncUse(Kind, O, User, OperandValToReplace,
                                           Loops, SE, DT);
    if (O != N)
      switch (S->getSCEVType()) {
      case scZeroExtend: return SE.getZeroExtendExpr(N, S->getType());
      case scSignExtend: return SE.getSignExtendExpr(N, S->getType());
      case scTruncate: return SE.getTruncateExpr(N, S->getType());
      default: llvm_unreachable("Unexpected SCEVCastExpr kind!");
      }
    return S;
  }
  if (const SCEVNAryExpr *X = dyn_cast<SCEVNAryExpr>(S)) {
    SmallVector<const SCEV *, 8> Operands;
    bool Changed = false;
    for (SCEVNAryExpr::op_iterator I = X->op_begin(), E = X->op_end();
         I != E; ++I) {
      const SCEV *O = *I;
      const SCEV *N = TransformForPostIncUse(Kind, O, User, OperandValToReplace,
                                             Loops, SE, DT);
      Changed |= N != O;
      Operands.push_back(N);
    }
    if (const SCEVAddRecExpr *AR = dyn_cast<SCEVAddRecExpr>(S)) {
      // An addrec. This is the interesting part.
      const Loop *L = AR->getLoop();
      const SCEV *Result = SE.getAddRecExpr(Operands, L);
      switch (Kind) {
      default: llvm_unreachable("Unexpected transform name!");
      case NormalizeAutodetect:
        if (Instruction *OI = dyn_cast<Instruction>(OperandValToReplace))
          if (IVUseShouldUsePostIncValue(User, OI, L, &DT)) {
            const SCEV *TransformedStep =
              TransformForPostIncUse(Kind, AR->getStepRecurrence(SE),
                                     User, OperandValToReplace, Loops, SE, DT);
            Result = SE.getMinusSCEV(Result, TransformedStep);
            Loops.insert(L);
          }
        break;
      case Normalize:
        if (Loops.count(L)) {
          const SCEV *TransformedStep =
            TransformForPostIncUse(Kind, AR->getStepRecurrence(SE),
                                   User, OperandValToReplace, Loops, SE, DT);
          Result = SE.getMinusSCEV(Result, TransformedStep);
        }
        break;
      case Denormalize:
        if (Loops.count(L))
          Result = SE.getAddExpr(Result, AR->getStepRecurrence(SE));
        break;
      }
      return Result;
    }
    if (Changed)
      switch (S->getSCEVType()) {
      case scAddExpr: return SE.getAddExpr(Operands);
      case scMulExpr: return SE.getMulExpr(Operands);
      case scSMaxExpr: return SE.getSMaxExpr(Operands);
      case scUMaxExpr: return SE.getUMaxExpr(Operands);
      default: llvm_unreachable("Unexpected SCEVNAryExpr kind!");
      }
    return S;
  }
  if (const SCEVUDivExpr *X = dyn_cast<SCEVUDivExpr>(S)) {
    const SCEV *LO = X->getLHS();
    const SCEV *RO = X->getRHS();
    const SCEV *LN = TransformForPostIncUse(Kind, LO, User, OperandValToReplace,
                                            Loops, SE, DT);
    const SCEV *RN = TransformForPostIncUse(Kind, RO, User, OperandValToReplace,
                                            Loops, SE, DT);
    if (LO != LN || RO != RN)
      return SE.getUDivExpr(LN, RN);
    return S;
  }
  llvm_unreachable("Unexpected SCEV kind!");
  return 0;
}
