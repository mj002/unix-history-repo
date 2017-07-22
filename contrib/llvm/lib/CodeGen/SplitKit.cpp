//===---------- SplitKit.cpp - Toolkit for splitting live ranges ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file contains the SplitAnalysis class as well as mutator functions for
// live range splitting.
//
//===----------------------------------------------------------------------===//

#include "SplitKit.h"
#include "llvm/ADT/Statistic.h"
#include "llvm/CodeGen/LiveIntervalAnalysis.h"
#include "llvm/CodeGen/LiveRangeEdit.h"
#include "llvm/CodeGen/MachineBlockFrequencyInfo.h"
#include "llvm/CodeGen/MachineDominators.h"
#include "llvm/CodeGen/MachineInstrBuilder.h"
#include "llvm/CodeGen/MachineLoopInfo.h"
#include "llvm/CodeGen/MachineRegisterInfo.h"
#include "llvm/CodeGen/VirtRegMap.h"
#include "llvm/Support/Debug.h"
#include "llvm/Support/MathExtras.h"
#include "llvm/Support/raw_ostream.h"
#include "llvm/Target/TargetInstrInfo.h"
#include "llvm/Target/TargetMachine.h"

using namespace llvm;

#define DEBUG_TYPE "regalloc"

STATISTIC(NumFinished, "Number of splits finished");
STATISTIC(NumSimple,   "Number of splits that were simple");
STATISTIC(NumCopies,   "Number of copies inserted for splitting");
STATISTIC(NumRemats,   "Number of rematerialized defs for splitting");
STATISTIC(NumRepairs,  "Number of invalid live ranges repaired");

//===----------------------------------------------------------------------===//
//                     Last Insert Point Analysis
//===----------------------------------------------------------------------===//

InsertPointAnalysis::InsertPointAnalysis(const LiveIntervals &lis,
                                         unsigned BBNum)
    : LIS(lis), LastInsertPoint(BBNum) {}

SlotIndex
InsertPointAnalysis::computeLastInsertPoint(const LiveInterval &CurLI,
                                            const MachineBasicBlock &MBB) {
  unsigned Num = MBB.getNumber();
  std::pair<SlotIndex, SlotIndex> &LIP = LastInsertPoint[Num];
  SlotIndex MBBEnd = LIS.getMBBEndIdx(&MBB);

  SmallVector<const MachineBasicBlock *, 1> EHPadSuccessors;
  for (const MachineBasicBlock *SMBB : MBB.successors())
    if (SMBB->isEHPad())
      EHPadSuccessors.push_back(SMBB);

  // Compute insert points on the first call. The pair is independent of the
  // current live interval.
  if (!LIP.first.isValid()) {
    MachineBasicBlock::const_iterator FirstTerm = MBB.getFirstTerminator();
    if (FirstTerm == MBB.end())
      LIP.first = MBBEnd;
    else
      LIP.first = LIS.getInstructionIndex(*FirstTerm);

    // If there is a landing pad successor, also find the call instruction.
    if (EHPadSuccessors.empty())
      return LIP.first;
    // There may not be a call instruction (?) in which case we ignore LPad.
    LIP.second = LIP.first;
    for (MachineBasicBlock::const_iterator I = MBB.end(), E = MBB.begin();
         I != E;) {
      --I;
      if (I->isCall()) {
        LIP.second = LIS.getInstructionIndex(*I);
        break;
      }
    }
  }

  // If CurLI is live into a landing pad successor, move the last insert point
  // back to the call that may throw.
  if (!LIP.second)
    return LIP.first;

  if (none_of(EHPadSuccessors, [&](const MachineBasicBlock *EHPad) {
        return LIS.isLiveInToMBB(CurLI, EHPad);
      }))
    return LIP.first;

  // Find the value leaving MBB.
  const VNInfo *VNI = CurLI.getVNInfoBefore(MBBEnd);
  if (!VNI)
    return LIP.first;

  // If the value leaving MBB was defined after the call in MBB, it can't
  // really be live-in to the landing pad.  This can happen if the landing pad
  // has a PHI, and this register is undef on the exceptional edge.
  // <rdar://problem/10664933>
  if (!SlotIndex::isEarlierInstr(VNI->def, LIP.second) && VNI->def < MBBEnd)
    return LIP.first;

  // Value is properly live-in to the landing pad.
  // Only allow inserts before the call.
  return LIP.second;
}

MachineBasicBlock::iterator
InsertPointAnalysis::getLastInsertPointIter(const LiveInterval &CurLI,
                                            MachineBasicBlock &MBB) {
  SlotIndex LIP = getLastInsertPoint(CurLI, MBB);
  if (LIP == LIS.getMBBEndIdx(&MBB))
    return MBB.end();
  return LIS.getInstructionFromIndex(LIP);
}

//===----------------------------------------------------------------------===//
//                                 Split Analysis
//===----------------------------------------------------------------------===//

SplitAnalysis::SplitAnalysis(const VirtRegMap &vrm, const LiveIntervals &lis,
                             const MachineLoopInfo &mli)
    : MF(vrm.getMachineFunction()), VRM(vrm), LIS(lis), Loops(mli),
      TII(*MF.getSubtarget().getInstrInfo()), CurLI(nullptr),
      IPA(lis, MF.getNumBlockIDs()) {}

void SplitAnalysis::clear() {
  UseSlots.clear();
  UseBlocks.clear();
  ThroughBlocks.clear();
  CurLI = nullptr;
  DidRepairRange = false;
}

/// analyzeUses - Count instructions, basic blocks, and loops using CurLI.
void SplitAnalysis::analyzeUses() {
  assert(UseSlots.empty() && "Call clear first");

  // First get all the defs from the interval values. This provides the correct
  // slots for early clobbers.
  for (const VNInfo *VNI : CurLI->valnos)
    if (!VNI->isPHIDef() && !VNI->isUnused())
      UseSlots.push_back(VNI->def);

  // Get use slots form the use-def chain.
  const MachineRegisterInfo &MRI = MF.getRegInfo();
  for (MachineOperand &MO : MRI.use_nodbg_operands(CurLI->reg))
    if (!MO.isUndef())
      UseSlots.push_back(LIS.getInstructionIndex(*MO.getParent()).getRegSlot());

  array_pod_sort(UseSlots.begin(), UseSlots.end());

  // Remove duplicates, keeping the smaller slot for each instruction.
  // That is what we want for early clobbers.
  UseSlots.erase(std::unique(UseSlots.begin(), UseSlots.end(),
                             SlotIndex::isSameInstr),
                 UseSlots.end());

  // Compute per-live block info.
  if (!calcLiveBlockInfo()) {
    // FIXME: calcLiveBlockInfo found inconsistencies in the live range.
    // I am looking at you, RegisterCoalescer!
    DidRepairRange = true;
    ++NumRepairs;
    DEBUG(dbgs() << "*** Fixing inconsistent live interval! ***\n");
    const_cast<LiveIntervals&>(LIS)
      .shrinkToUses(const_cast<LiveInterval*>(CurLI));
    UseBlocks.clear();
    ThroughBlocks.clear();
    bool fixed = calcLiveBlockInfo();
    (void)fixed;
    assert(fixed && "Couldn't fix broken live interval");
  }

  DEBUG(dbgs() << "Analyze counted "
               << UseSlots.size() << " instrs in "
               << UseBlocks.size() << " blocks, through "
               << NumThroughBlocks << " blocks.\n");
}

/// calcLiveBlockInfo - Fill the LiveBlocks array with information about blocks
/// where CurLI is live.
bool SplitAnalysis::calcLiveBlockInfo() {
  ThroughBlocks.resize(MF.getNumBlockIDs());
  NumThroughBlocks = NumGapBlocks = 0;
  if (CurLI->empty())
    return true;

  LiveInterval::const_iterator LVI = CurLI->begin();
  LiveInterval::const_iterator LVE = CurLI->end();

  SmallVectorImpl<SlotIndex>::const_iterator UseI, UseE;
  UseI = UseSlots.begin();
  UseE = UseSlots.end();

  // Loop over basic blocks where CurLI is live.
  MachineFunction::iterator MFI =
      LIS.getMBBFromIndex(LVI->start)->getIterator();
  for (;;) {
    BlockInfo BI;
    BI.MBB = &*MFI;
    SlotIndex Start, Stop;
    std::tie(Start, Stop) = LIS.getSlotIndexes()->getMBBRange(BI.MBB);

    // If the block contains no uses, the range must be live through. At one
    // point, RegisterCoalescer could create dangling ranges that ended
    // mid-block.
    if (UseI == UseE || *UseI >= Stop) {
      ++NumThroughBlocks;
      ThroughBlocks.set(BI.MBB->getNumber());
      // The range shouldn't end mid-block if there are no uses. This shouldn't
      // happen.
      if (LVI->end < Stop)
        return false;
    } else {
      // This block has uses. Find the first and last uses in the block.
      BI.FirstInstr = *UseI;
      assert(BI.FirstInstr >= Start);
      do ++UseI;
      while (UseI != UseE && *UseI < Stop);
      BI.LastInstr = UseI[-1];
      assert(BI.LastInstr < Stop);

      // LVI is the first live segment overlapping MBB.
      BI.LiveIn = LVI->start <= Start;

      // When not live in, the first use should be a def.
      if (!BI.LiveIn) {
        assert(LVI->start == LVI->valno->def && "Dangling Segment start");
        assert(LVI->start == BI.FirstInstr && "First instr should be a def");
        BI.FirstDef = BI.FirstInstr;
      }

      // Look for gaps in the live range.
      BI.LiveOut = true;
      while (LVI->end < Stop) {
        SlotIndex LastStop = LVI->end;
        if (++LVI == LVE || LVI->start >= Stop) {
          BI.LiveOut = false;
          BI.LastInstr = LastStop;
          break;
        }

        if (LastStop < LVI->start) {
          // There is a gap in the live range. Create duplicate entries for the
          // live-in snippet and the live-out snippet.
          ++NumGapBlocks;

          // Push the Live-in part.
          BI.LiveOut = false;
          UseBlocks.push_back(BI);
          UseBlocks.back().LastInstr = LastStop;

          // Set up BI for the live-out part.
          BI.LiveIn = false;
          BI.LiveOut = true;
          BI.FirstInstr = BI.FirstDef = LVI->start;
        }

        // A Segment that starts in the middle of the block must be a def.
        assert(LVI->start == LVI->valno->def && "Dangling Segment start");
        if (!BI.FirstDef)
          BI.FirstDef = LVI->start;
      }

      UseBlocks.push_back(BI);

      // LVI is now at LVE or LVI->end >= Stop.
      if (LVI == LVE)
        break;
    }

    // Live segment ends exactly at Stop. Move to the next segment.
    if (LVI->end == Stop && ++LVI == LVE)
      break;

    // Pick the next basic block.
    if (LVI->start < Stop)
      ++MFI;
    else
      MFI = LIS.getMBBFromIndex(LVI->start)->getIterator();
  }

  assert(getNumLiveBlocks() == countLiveBlocks(CurLI) && "Bad block count");
  return true;
}

unsigned SplitAnalysis::countLiveBlocks(const LiveInterval *cli) const {
  if (cli->empty())
    return 0;
  LiveInterval *li = const_cast<LiveInterval*>(cli);
  LiveInterval::iterator LVI = li->begin();
  LiveInterval::iterator LVE = li->end();
  unsigned Count = 0;

  // Loop over basic blocks where li is live.
  MachineFunction::const_iterator MFI =
      LIS.getMBBFromIndex(LVI->start)->getIterator();
  SlotIndex Stop = LIS.getMBBEndIdx(&*MFI);
  for (;;) {
    ++Count;
    LVI = li->advanceTo(LVI, Stop);
    if (LVI == LVE)
      return Count;
    do {
      ++MFI;
      Stop = LIS.getMBBEndIdx(&*MFI);
    } while (Stop <= LVI->start);
  }
}

bool SplitAnalysis::isOriginalEndpoint(SlotIndex Idx) const {
  unsigned OrigReg = VRM.getOriginal(CurLI->reg);
  const LiveInterval &Orig = LIS.getInterval(OrigReg);
  assert(!Orig.empty() && "Splitting empty interval?");
  LiveInterval::const_iterator I = Orig.find(Idx);

  // Range containing Idx should begin at Idx.
  if (I != Orig.end() && I->start <= Idx)
    return I->start == Idx;

  // Range does not contain Idx, previous must end at Idx.
  return I != Orig.begin() && (--I)->end == Idx;
}

void SplitAnalysis::analyze(const LiveInterval *li) {
  clear();
  CurLI = li;
  analyzeUses();
}


//===----------------------------------------------------------------------===//
//                               Split Editor
//===----------------------------------------------------------------------===//

/// Create a new SplitEditor for editing the LiveInterval analyzed by SA.
SplitEditor::SplitEditor(SplitAnalysis &sa, AliasAnalysis &aa,
                         LiveIntervals &lis, VirtRegMap &vrm,
                         MachineDominatorTree &mdt,
                         MachineBlockFrequencyInfo &mbfi)
    : SA(sa), AA(aa), LIS(lis), VRM(vrm),
      MRI(vrm.getMachineFunction().getRegInfo()), MDT(mdt),
      TII(*vrm.getMachineFunction().getSubtarget().getInstrInfo()),
      TRI(*vrm.getMachineFunction().getSubtarget().getRegisterInfo()),
      MBFI(mbfi), Edit(nullptr), OpenIdx(0), SpillMode(SM_Partition),
      RegAssign(Allocator) {}

void SplitEditor::reset(LiveRangeEdit &LRE, ComplementSpillMode SM) {
  Edit = &LRE;
  SpillMode = SM;
  OpenIdx = 0;
  RegAssign.clear();
  Values.clear();

  // Reset the LiveRangeCalc instances needed for this spill mode.
  LRCalc[0].reset(&VRM.getMachineFunction(), LIS.getSlotIndexes(), &MDT,
                  &LIS.getVNInfoAllocator());
  if (SpillMode)
    LRCalc[1].reset(&VRM.getMachineFunction(), LIS.getSlotIndexes(), &MDT,
                    &LIS.getVNInfoAllocator());

  // We don't need an AliasAnalysis since we will only be performing
  // cheap-as-a-copy remats anyway.
  Edit->anyRematerializable(nullptr);
}

#if !defined(NDEBUG) || defined(LLVM_ENABLE_DUMP)
LLVM_DUMP_METHOD void SplitEditor::dump() const {
  if (RegAssign.empty()) {
    dbgs() << " empty\n";
    return;
  }

  for (RegAssignMap::const_iterator I = RegAssign.begin(); I.valid(); ++I)
    dbgs() << " [" << I.start() << ';' << I.stop() << "):" << I.value();
  dbgs() << '\n';
}
#endif

LiveInterval::SubRange &SplitEditor::getSubRangeForMask(LaneBitmask LM,
                                                        LiveInterval &LI) {
  for (LiveInterval::SubRange &S : LI.subranges())
    if (S.LaneMask == LM)
      return S;
  llvm_unreachable("SubRange for this mask not found");
}

void SplitEditor::addDeadDef(LiveInterval &LI, VNInfo *VNI, bool Original) {
  if (!LI.hasSubRanges()) {
    LI.createDeadDef(VNI);
    return;
  }

  SlotIndex Def = VNI->def;
  if (Original) {
    // If we are transferring a def from the original interval, make sure
    // to only update the subranges for which the original subranges had
    // a def at this location.
    for (LiveInterval::SubRange &S : LI.subranges()) {
      auto &PS = getSubRangeForMask(S.LaneMask, Edit->getParent());
      VNInfo *PV = PS.getVNInfoAt(Def);
      if (PV != nullptr && PV->def == Def)
        S.createDeadDef(Def, LIS.getVNInfoAllocator());
    }
  } else {
    // This is a new def: either from rematerialization, or from an inserted
    // copy. Since rematerialization can regenerate a definition of a sub-
    // register, we need to check which subranges need to be updated.
    const MachineInstr *DefMI = LIS.getInstructionFromIndex(Def);
    assert(DefMI != nullptr);
    LaneBitmask LM;
    for (const MachineOperand &DefOp : DefMI->defs()) {
      unsigned R = DefOp.getReg();
      if (R != LI.reg)
        continue;
      if (unsigned SR = DefOp.getSubReg())
        LM |= TRI.getSubRegIndexLaneMask(SR);
      else {
        LM = MRI.getMaxLaneMaskForVReg(R);
        break;
      }
    }
    for (LiveInterval::SubRange &S : LI.subranges())
      if ((S.LaneMask & LM).any())
        S.createDeadDef(Def, LIS.getVNInfoAllocator());
  }
}

VNInfo *SplitEditor::defValue(unsigned RegIdx,
                              const VNInfo *ParentVNI,
                              SlotIndex Idx,
                              bool Original) {
  assert(ParentVNI && "Mapping  NULL value");
  assert(Idx.isValid() && "Invalid SlotIndex");
  assert(Edit->getParent().getVNInfoAt(Idx) == ParentVNI && "Bad Parent VNI");
  LiveInterval *LI = &LIS.getInterval(Edit->get(RegIdx));

  // Create a new value.
  VNInfo *VNI = LI->getNextValue(Idx, LIS.getVNInfoAllocator());

  bool Force = LI->hasSubRanges();
  ValueForcePair FP(Force ? nullptr : VNI, Force);
  // Use insert for lookup, so we can add missing values with a second lookup.
  std::pair<ValueMap::iterator, bool> InsP =
    Values.insert(std::make_pair(std::make_pair(RegIdx, ParentVNI->id), FP));

  // This was the first time (RegIdx, ParentVNI) was mapped, and it is not
  // forced. Keep it as a simple def without any liveness.
  if (!Force && InsP.second)
    return VNI;

  // If the previous value was a simple mapping, add liveness for it now.
  if (VNInfo *OldVNI = InsP.first->second.getPointer()) {
    addDeadDef(*LI, OldVNI, Original);

    // No longer a simple mapping.  Switch to a complex mapping. If the
    // interval has subranges, make it a forced mapping.
    InsP.first->second = ValueForcePair(nullptr, Force);
  }

  // This is a complex mapping, add liveness for VNI
  addDeadDef(*LI, VNI, Original);
  return VNI;
}

void SplitEditor::forceRecompute(unsigned RegIdx, const VNInfo *ParentVNI) {
  assert(ParentVNI && "Mapping  NULL value");
  ValueForcePair &VFP = Values[std::make_pair(RegIdx, ParentVNI->id)];
  VNInfo *VNI = VFP.getPointer();

  // ParentVNI was either unmapped or already complex mapped. Either way, just
  // set the force bit.
  if (!VNI) {
    VFP.setInt(true);
    return;
  }

  // This was previously a single mapping. Make sure the old def is represented
  // by a trivial live range.
  addDeadDef(LIS.getInterval(Edit->get(RegIdx)), VNI, false);

  // Mark as complex mapped, forced.
  VFP = ValueForcePair(nullptr, true);
}

SlotIndex SplitEditor::buildSingleSubRegCopy(unsigned FromReg, unsigned ToReg,
    MachineBasicBlock &MBB, MachineBasicBlock::iterator InsertBefore,
    unsigned SubIdx, LiveInterval &DestLI, bool Late, SlotIndex Def) {
  const MCInstrDesc &Desc = TII.get(TargetOpcode::COPY);
  bool FirstCopy = !Def.isValid();
  MachineInstr *CopyMI = BuildMI(MBB, InsertBefore, DebugLoc(), Desc)
      .addReg(ToReg, RegState::Define | getUndefRegState(FirstCopy)
              | getInternalReadRegState(!FirstCopy), SubIdx)
      .addReg(FromReg, 0, SubIdx);

  BumpPtrAllocator &Allocator = LIS.getVNInfoAllocator();
  if (FirstCopy) {
    SlotIndexes &Indexes = *LIS.getSlotIndexes();
    Def = Indexes.insertMachineInstrInMaps(*CopyMI, Late).getRegSlot();
  } else {
    CopyMI->bundleWithPred();
  }
  LaneBitmask LaneMask = TRI.getSubRegIndexLaneMask(SubIdx);
  DestLI.refineSubRanges(Allocator, LaneMask,
                         [Def, &Allocator](LiveInterval::SubRange& SR) {
    SR.createDeadDef(Def, Allocator);
  });
  return Def;
}

SlotIndex SplitEditor::buildCopy(unsigned FromReg, unsigned ToReg,
    LaneBitmask LaneMask, MachineBasicBlock &MBB,
    MachineBasicBlock::iterator InsertBefore, bool Late, unsigned RegIdx) {
  const MCInstrDesc &Desc = TII.get(TargetOpcode::COPY);
  if (LaneMask.all() || LaneMask == MRI.getMaxLaneMaskForVReg(FromReg)) {
    // The full vreg is copied.
    MachineInstr *CopyMI =
        BuildMI(MBB, InsertBefore, DebugLoc(), Desc, ToReg).addReg(FromReg);
    SlotIndexes &Indexes = *LIS.getSlotIndexes();
    return Indexes.insertMachineInstrInMaps(*CopyMI, Late).getRegSlot();
  }

  // Only a subset of lanes needs to be copied. The following is a simple
  // heuristic to construct a sequence of COPYs. We could add a target
  // specific callback if this turns out to be suboptimal.
  LiveInterval &DestLI = LIS.getInterval(Edit->get(RegIdx));

  // First pass: Try to find a perfectly matching subregister index. If none
  // exists find the one covering the most lanemask bits.
  SmallVector<unsigned, 8> PossibleIndexes;
  unsigned BestIdx = 0;
  unsigned BestCover = 0;
  const TargetRegisterClass *RC = MRI.getRegClass(FromReg);
  assert(RC == MRI.getRegClass(ToReg) && "Should have same reg class");
  for (unsigned Idx = 1, E = TRI.getNumSubRegIndices(); Idx < E; ++Idx) {
    // Is this index even compatible with the given class?
    if (TRI.getSubClassWithSubReg(RC, Idx) != RC)
      continue;
    LaneBitmask SubRegMask = TRI.getSubRegIndexLaneMask(Idx);
    // Early exit if we found a perfect match.
    if (SubRegMask == LaneMask) {
      BestIdx = Idx;
      break;
    }

    // The index must not cover any lanes outside \p LaneMask.
    if ((SubRegMask & ~LaneMask).any())
      continue;

    unsigned PopCount = countPopulation(SubRegMask.getAsInteger());
    PossibleIndexes.push_back(Idx);
    if (PopCount > BestCover) {
      BestCover = PopCount;
      BestIdx = Idx;
    }
  }

  // Abort if we cannot possibly implement the COPY with the given indexes.
  if (BestIdx == 0)
    report_fatal_error("Impossible to implement partial COPY");

  SlotIndex Def = buildSingleSubRegCopy(FromReg, ToReg, MBB, InsertBefore,
                                        BestIdx, DestLI, Late, SlotIndex());

  // Greedy heuristic: Keep iterating keeping the best covering subreg index
  // each time.
  LaneBitmask LanesLeft = LaneMask & ~(TRI.getSubRegIndexLaneMask(BestIdx));
  while (LanesLeft.any()) {
    unsigned BestIdx = 0;
    int BestCover = INT_MIN;
    for (unsigned Idx : PossibleIndexes) {
      LaneBitmask SubRegMask = TRI.getSubRegIndexLaneMask(Idx);
      // Early exit if we found a perfect match.
      if (SubRegMask == LanesLeft) {
        BestIdx = Idx;
        break;
      }

      // Try to cover as much of the remaining lanes as possible but
      // as few of the already covered lanes as possible.
      int Cover = countPopulation((SubRegMask & LanesLeft).getAsInteger())
                - countPopulation((SubRegMask & ~LanesLeft).getAsInteger());
      if (Cover > BestCover) {
        BestCover = Cover;
        BestIdx = Idx;
      }
    }

    if (BestIdx == 0)
      report_fatal_error("Impossible to implement partial COPY");

    buildSingleSubRegCopy(FromReg, ToReg, MBB, InsertBefore, BestIdx,
                          DestLI, Late, Def);
    LanesLeft &= ~TRI.getSubRegIndexLaneMask(BestIdx);
  }

  return Def;
}

VNInfo *SplitEditor::defFromParent(unsigned RegIdx,
                                   VNInfo *ParentVNI,
                                   SlotIndex UseIdx,
                                   MachineBasicBlock &MBB,
                                   MachineBasicBlock::iterator I) {
  SlotIndex Def;
  LiveInterval *LI = &LIS.getInterval(Edit->get(RegIdx));

  // We may be trying to avoid interference that ends at a deleted instruction,
  // so always begin RegIdx 0 early and all others late.
  bool Late = RegIdx != 0;

  // Attempt cheap-as-a-copy rematerialization.
  unsigned Original = VRM.getOriginal(Edit->get(RegIdx));
  LiveInterval &OrigLI = LIS.getInterval(Original);
  VNInfo *OrigVNI = OrigLI.getVNInfoAt(UseIdx);

  unsigned Reg = LI->reg;
  bool DidRemat = false;
  if (OrigVNI) {
    LiveRangeEdit::Remat RM(ParentVNI);
    RM.OrigMI = LIS.getInstructionFromIndex(OrigVNI->def);
    if (Edit->canRematerializeAt(RM, OrigVNI, UseIdx, true)) {
      Def = Edit->rematerializeAt(MBB, I, Reg, RM, TRI, Late);
      ++NumRemats;
      DidRemat = true;
    }
  }
  if (!DidRemat) {
    LaneBitmask LaneMask;
    if (LI->hasSubRanges()) {
      LaneMask = LaneBitmask::getNone();
      for (LiveInterval::SubRange &S : LI->subranges())
        LaneMask |= S.LaneMask;
    } else {
      LaneMask = LaneBitmask::getAll();
    }

    ++NumCopies;
    Def = buildCopy(Edit->getReg(), Reg, LaneMask, MBB, I, Late, RegIdx);
  }

  // Define the value in Reg.
  return defValue(RegIdx, ParentVNI, Def, false);
}

/// Create a new virtual register and live interval.
unsigned SplitEditor::openIntv() {
  // Create the complement as index 0.
  if (Edit->empty())
    Edit->createEmptyInterval();

  // Create the open interval.
  OpenIdx = Edit->size();
  Edit->createEmptyInterval();
  return OpenIdx;
}

void SplitEditor::selectIntv(unsigned Idx) {
  assert(Idx != 0 && "Cannot select the complement interval");
  assert(Idx < Edit->size() && "Can only select previously opened interval");
  DEBUG(dbgs() << "    selectIntv " << OpenIdx << " -> " << Idx << '\n');
  OpenIdx = Idx;
}

SlotIndex SplitEditor::enterIntvBefore(SlotIndex Idx) {
  assert(OpenIdx && "openIntv not called before enterIntvBefore");
  DEBUG(dbgs() << "    enterIntvBefore " << Idx);
  Idx = Idx.getBaseIndex();
  VNInfo *ParentVNI = Edit->getParent().getVNInfoAt(Idx);
  if (!ParentVNI) {
    DEBUG(dbgs() << ": not live\n");
    return Idx;
  }
  DEBUG(dbgs() << ": valno " << ParentVNI->id << '\n');
  MachineInstr *MI = LIS.getInstructionFromIndex(Idx);
  assert(MI && "enterIntvBefore called with invalid index");

  VNInfo *VNI = defFromParent(OpenIdx, ParentVNI, Idx, *MI->getParent(), MI);
  return VNI->def;
}

SlotIndex SplitEditor::enterIntvAfter(SlotIndex Idx) {
  assert(OpenIdx && "openIntv not called before enterIntvAfter");
  DEBUG(dbgs() << "    enterIntvAfter " << Idx);
  Idx = Idx.getBoundaryIndex();
  VNInfo *ParentVNI = Edit->getParent().getVNInfoAt(Idx);
  if (!ParentVNI) {
    DEBUG(dbgs() << ": not live\n");
    return Idx;
  }
  DEBUG(dbgs() << ": valno " << ParentVNI->id << '\n');
  MachineInstr *MI = LIS.getInstructionFromIndex(Idx);
  assert(MI && "enterIntvAfter called with invalid index");

  VNInfo *VNI = defFromParent(OpenIdx, ParentVNI, Idx, *MI->getParent(),
                              std::next(MachineBasicBlock::iterator(MI)));
  return VNI->def;
}

SlotIndex SplitEditor::enterIntvAtEnd(MachineBasicBlock &MBB) {
  assert(OpenIdx && "openIntv not called before enterIntvAtEnd");
  SlotIndex End = LIS.getMBBEndIdx(&MBB);
  SlotIndex Last = End.getPrevSlot();
  DEBUG(dbgs() << "    enterIntvAtEnd BB#" << MBB.getNumber() << ", " << Last);
  VNInfo *ParentVNI = Edit->getParent().getVNInfoAt(Last);
  if (!ParentVNI) {
    DEBUG(dbgs() << ": not live\n");
    return End;
  }
  DEBUG(dbgs() << ": valno " << ParentVNI->id);
  VNInfo *VNI = defFromParent(OpenIdx, ParentVNI, Last, MBB,
                              SA.getLastSplitPointIter(&MBB));
  RegAssign.insert(VNI->def, End, OpenIdx);
  DEBUG(dump());
  return VNI->def;
}

/// useIntv - indicate that all instructions in MBB should use OpenLI.
void SplitEditor::useIntv(const MachineBasicBlock &MBB) {
  useIntv(LIS.getMBBStartIdx(&MBB), LIS.getMBBEndIdx(&MBB));
}

void SplitEditor::useIntv(SlotIndex Start, SlotIndex End) {
  assert(OpenIdx && "openIntv not called before useIntv");
  DEBUG(dbgs() << "    useIntv [" << Start << ';' << End << "):");
  RegAssign.insert(Start, End, OpenIdx);
  DEBUG(dump());
}

SlotIndex SplitEditor::leaveIntvAfter(SlotIndex Idx) {
  assert(OpenIdx && "openIntv not called before leaveIntvAfter");
  DEBUG(dbgs() << "    leaveIntvAfter " << Idx);

  // The interval must be live beyond the instruction at Idx.
  SlotIndex Boundary = Idx.getBoundaryIndex();
  VNInfo *ParentVNI = Edit->getParent().getVNInfoAt(Boundary);
  if (!ParentVNI) {
    DEBUG(dbgs() << ": not live\n");
    return Boundary.getNextSlot();
  }
  DEBUG(dbgs() << ": valno " << ParentVNI->id << '\n');
  MachineInstr *MI = LIS.getInstructionFromIndex(Boundary);
  assert(MI && "No instruction at index");

  // In spill mode, make live ranges as short as possible by inserting the copy
  // before MI.  This is only possible if that instruction doesn't redefine the
  // value.  The inserted COPY is not a kill, and we don't need to recompute
  // the source live range.  The spiller also won't try to hoist this copy.
  if (SpillMode && !SlotIndex::isSameInstr(ParentVNI->def, Idx) &&
      MI->readsVirtualRegister(Edit->getReg())) {
    forceRecompute(0, ParentVNI);
    defFromParent(0, ParentVNI, Idx, *MI->getParent(), MI);
    return Idx;
  }

  VNInfo *VNI = defFromParent(0, ParentVNI, Boundary, *MI->getParent(),
                              std::next(MachineBasicBlock::iterator(MI)));
  return VNI->def;
}

SlotIndex SplitEditor::leaveIntvBefore(SlotIndex Idx) {
  assert(OpenIdx && "openIntv not called before leaveIntvBefore");
  DEBUG(dbgs() << "    leaveIntvBefore " << Idx);

  // The interval must be live into the instruction at Idx.
  Idx = Idx.getBaseIndex();
  VNInfo *ParentVNI = Edit->getParent().getVNInfoAt(Idx);
  if (!ParentVNI) {
    DEBUG(dbgs() << ": not live\n");
    return Idx.getNextSlot();
  }
  DEBUG(dbgs() << ": valno " << ParentVNI->id << '\n');

  MachineInstr *MI = LIS.getInstructionFromIndex(Idx);
  assert(MI && "No instruction at index");
  VNInfo *VNI = defFromParent(0, ParentVNI, Idx, *MI->getParent(), MI);
  return VNI->def;
}

SlotIndex SplitEditor::leaveIntvAtTop(MachineBasicBlock &MBB) {
  assert(OpenIdx && "openIntv not called before leaveIntvAtTop");
  SlotIndex Start = LIS.getMBBStartIdx(&MBB);
  DEBUG(dbgs() << "    leaveIntvAtTop BB#" << MBB.getNumber() << ", " << Start);

  VNInfo *ParentVNI = Edit->getParent().getVNInfoAt(Start);
  if (!ParentVNI) {
    DEBUG(dbgs() << ": not live\n");
    return Start;
  }

  VNInfo *VNI = defFromParent(0, ParentVNI, Start, MBB,
                              MBB.SkipPHIsLabelsAndDebug(MBB.begin()));
  RegAssign.insert(Start, VNI->def, OpenIdx);
  DEBUG(dump());
  return VNI->def;
}

void SplitEditor::overlapIntv(SlotIndex Start, SlotIndex End) {
  assert(OpenIdx && "openIntv not called before overlapIntv");
  const VNInfo *ParentVNI = Edit->getParent().getVNInfoAt(Start);
  assert(ParentVNI == Edit->getParent().getVNInfoBefore(End) &&
         "Parent changes value in extended range");
  assert(LIS.getMBBFromIndex(Start) == LIS.getMBBFromIndex(End) &&
         "Range cannot span basic blocks");

  // The complement interval will be extended as needed by LRCalc.extend().
  if (ParentVNI)
    forceRecompute(0, ParentVNI);
  DEBUG(dbgs() << "    overlapIntv [" << Start << ';' << End << "):");
  RegAssign.insert(Start, End, OpenIdx);
  DEBUG(dump());
}

//===----------------------------------------------------------------------===//
//                                  Spill modes
//===----------------------------------------------------------------------===//

void SplitEditor::removeBackCopies(SmallVectorImpl<VNInfo*> &Copies) {
  LiveInterval *LI = &LIS.getInterval(Edit->get(0));
  DEBUG(dbgs() << "Removing " << Copies.size() << " back-copies.\n");
  RegAssignMap::iterator AssignI;
  AssignI.setMap(RegAssign);

  for (unsigned i = 0, e = Copies.size(); i != e; ++i) {
    SlotIndex Def = Copies[i]->def;
    MachineInstr *MI = LIS.getInstructionFromIndex(Def);
    assert(MI && "No instruction for back-copy");

    MachineBasicBlock *MBB = MI->getParent();
    MachineBasicBlock::iterator MBBI(MI);
    bool AtBegin;
    do AtBegin = MBBI == MBB->begin();
    while (!AtBegin && (--MBBI)->isDebugValue());

    DEBUG(dbgs() << "Removing " << Def << '\t' << *MI);
    LIS.removeVRegDefAt(*LI, Def);
    LIS.RemoveMachineInstrFromMaps(*MI);
    MI->eraseFromParent();

    // Adjust RegAssign if a register assignment is killed at Def. We want to
    // avoid calculating the live range of the source register if possible.
    AssignI.find(Def.getPrevSlot());
    if (!AssignI.valid() || AssignI.start() >= Def)
      continue;
    // If MI doesn't kill the assigned register, just leave it.
    if (AssignI.stop() != Def)
      continue;
    unsigned RegIdx = AssignI.value();
    if (AtBegin || !MBBI->readsVirtualRegister(Edit->getReg())) {
      DEBUG(dbgs() << "  cannot find simple kill of RegIdx " << RegIdx << '\n');
      forceRecompute(RegIdx, Edit->getParent().getVNInfoAt(Def));
    } else {
      SlotIndex Kill = LIS.getInstructionIndex(*MBBI).getRegSlot();
      DEBUG(dbgs() << "  move kill to " << Kill << '\t' << *MBBI);
      AssignI.setStop(Kill);
    }
  }
}

MachineBasicBlock*
SplitEditor::findShallowDominator(MachineBasicBlock *MBB,
                                  MachineBasicBlock *DefMBB) {
  if (MBB == DefMBB)
    return MBB;
  assert(MDT.dominates(DefMBB, MBB) && "MBB must be dominated by the def.");

  const MachineLoopInfo &Loops = SA.Loops;
  const MachineLoop *DefLoop = Loops.getLoopFor(DefMBB);
  MachineDomTreeNode *DefDomNode = MDT[DefMBB];

  // Best candidate so far.
  MachineBasicBlock *BestMBB = MBB;
  unsigned BestDepth = UINT_MAX;

  for (;;) {
    const MachineLoop *Loop = Loops.getLoopFor(MBB);

    // MBB isn't in a loop, it doesn't get any better.  All dominators have a
    // higher frequency by definition.
    if (!Loop) {
      DEBUG(dbgs() << "Def in BB#" << DefMBB->getNumber() << " dominates BB#"
                   << MBB->getNumber() << " at depth 0\n");
      return MBB;
    }

    // We'll never be able to exit the DefLoop.
    if (Loop == DefLoop) {
      DEBUG(dbgs() << "Def in BB#" << DefMBB->getNumber() << " dominates BB#"
                   << MBB->getNumber() << " in the same loop\n");
      return MBB;
    }

    // Least busy dominator seen so far.
    unsigned Depth = Loop->getLoopDepth();
    if (Depth < BestDepth) {
      BestMBB = MBB;
      BestDepth = Depth;
      DEBUG(dbgs() << "Def in BB#" << DefMBB->getNumber() << " dominates BB#"
                   << MBB->getNumber() << " at depth " << Depth << '\n');
    }

    // Leave loop by going to the immediate dominator of the loop header.
    // This is a bigger stride than simply walking up the dominator tree.
    MachineDomTreeNode *IDom = MDT[Loop->getHeader()]->getIDom();

    // Too far up the dominator tree?
    if (!IDom || !MDT.dominates(DefDomNode, IDom))
      return BestMBB;

    MBB = IDom->getBlock();
  }
}

void SplitEditor::computeRedundantBackCopies(
    DenseSet<unsigned> &NotToHoistSet, SmallVectorImpl<VNInfo *> &BackCopies) {
  LiveInterval *LI = &LIS.getInterval(Edit->get(0));
  LiveInterval *Parent = &Edit->getParent();
  SmallVector<SmallPtrSet<VNInfo *, 8>, 8> EqualVNs(Parent->getNumValNums());
  SmallPtrSet<VNInfo *, 8> DominatedVNIs;

  // Aggregate VNIs having the same value as ParentVNI.
  for (VNInfo *VNI : LI->valnos) {
    if (VNI->isUnused())
      continue;
    VNInfo *ParentVNI = Edit->getParent().getVNInfoAt(VNI->def);
    EqualVNs[ParentVNI->id].insert(VNI);
  }

  // For VNI aggregation of each ParentVNI, collect dominated, i.e.,
  // redundant VNIs to BackCopies.
  for (unsigned i = 0, e = Parent->getNumValNums(); i != e; ++i) {
    VNInfo *ParentVNI = Parent->getValNumInfo(i);
    if (!NotToHoistSet.count(ParentVNI->id))
      continue;
    SmallPtrSetIterator<VNInfo *> It1 = EqualVNs[ParentVNI->id].begin();
    SmallPtrSetIterator<VNInfo *> It2 = It1;
    for (; It1 != EqualVNs[ParentVNI->id].end(); ++It1) {
      It2 = It1;
      for (++It2; It2 != EqualVNs[ParentVNI->id].end(); ++It2) {
        if (DominatedVNIs.count(*It1) || DominatedVNIs.count(*It2))
          continue;

        MachineBasicBlock *MBB1 = LIS.getMBBFromIndex((*It1)->def);
        MachineBasicBlock *MBB2 = LIS.getMBBFromIndex((*It2)->def);
        if (MBB1 == MBB2) {
          DominatedVNIs.insert((*It1)->def < (*It2)->def ? (*It2) : (*It1));
        } else if (MDT.dominates(MBB1, MBB2)) {
          DominatedVNIs.insert(*It2);
        } else if (MDT.dominates(MBB2, MBB1)) {
          DominatedVNIs.insert(*It1);
        }
      }
    }
    if (!DominatedVNIs.empty()) {
      forceRecompute(0, ParentVNI);
      for (auto VNI : DominatedVNIs) {
        BackCopies.push_back(VNI);
      }
      DominatedVNIs.clear();
    }
  }
}

/// For SM_Size mode, find a common dominator for all the back-copies for
/// the same ParentVNI and hoist the backcopies to the dominator BB.
/// For SM_Speed mode, if the common dominator is hot and it is not beneficial
/// to do the hoisting, simply remove the dominated backcopies for the same
/// ParentVNI.
void SplitEditor::hoistCopies() {
  // Get the complement interval, always RegIdx 0.
  LiveInterval *LI = &LIS.getInterval(Edit->get(0));
  LiveInterval *Parent = &Edit->getParent();

  // Track the nearest common dominator for all back-copies for each ParentVNI,
  // indexed by ParentVNI->id.
  typedef std::pair<MachineBasicBlock*, SlotIndex> DomPair;
  SmallVector<DomPair, 8> NearestDom(Parent->getNumValNums());
  // The total cost of all the back-copies for each ParentVNI.
  SmallVector<BlockFrequency, 8> Costs(Parent->getNumValNums());
  // The ParentVNI->id set for which hoisting back-copies are not beneficial
  // for Speed.
  DenseSet<unsigned> NotToHoistSet;

  // Find the nearest common dominator for parent values with multiple
  // back-copies.  If a single back-copy dominates, put it in DomPair.second.
  for (VNInfo *VNI : LI->valnos) {
    if (VNI->isUnused())
      continue;
    VNInfo *ParentVNI = Edit->getParent().getVNInfoAt(VNI->def);
    assert(ParentVNI && "Parent not live at complement def");

    // Don't hoist remats.  The complement is probably going to disappear
    // completely anyway.
    if (Edit->didRematerialize(ParentVNI))
      continue;

    MachineBasicBlock *ValMBB = LIS.getMBBFromIndex(VNI->def);

    DomPair &Dom = NearestDom[ParentVNI->id];

    // Keep directly defined parent values.  This is either a PHI or an
    // instruction in the complement range.  All other copies of ParentVNI
    // should be eliminated.
    if (VNI->def == ParentVNI->def) {
      DEBUG(dbgs() << "Direct complement def at " << VNI->def << '\n');
      Dom = DomPair(ValMBB, VNI->def);
      continue;
    }
    // Skip the singly mapped values.  There is nothing to gain from hoisting a
    // single back-copy.
    if (Values.lookup(std::make_pair(0, ParentVNI->id)).getPointer()) {
      DEBUG(dbgs() << "Single complement def at " << VNI->def << '\n');
      continue;
    }

    if (!Dom.first) {
      // First time we see ParentVNI.  VNI dominates itself.
      Dom = DomPair(ValMBB, VNI->def);
    } else if (Dom.first == ValMBB) {
      // Two defs in the same block.  Pick the earlier def.
      if (!Dom.second.isValid() || VNI->def < Dom.second)
        Dom.second = VNI->def;
    } else {
      // Different basic blocks. Check if one dominates.
      MachineBasicBlock *Near =
        MDT.findNearestCommonDominator(Dom.first, ValMBB);
      if (Near == ValMBB)
        // Def ValMBB dominates.
        Dom = DomPair(ValMBB, VNI->def);
      else if (Near != Dom.first)
        // None dominate. Hoist to common dominator, need new def.
        Dom = DomPair(Near, SlotIndex());
      Costs[ParentVNI->id] += MBFI.getBlockFreq(ValMBB);
    }

    DEBUG(dbgs() << "Multi-mapped complement " << VNI->id << '@' << VNI->def
                 << " for parent " << ParentVNI->id << '@' << ParentVNI->def
                 << " hoist to BB#" << Dom.first->getNumber() << ' '
                 << Dom.second << '\n');
  }

  // Insert the hoisted copies.
  for (unsigned i = 0, e = Parent->getNumValNums(); i != e; ++i) {
    DomPair &Dom = NearestDom[i];
    if (!Dom.first || Dom.second.isValid())
      continue;
    // This value needs a hoisted copy inserted at the end of Dom.first.
    VNInfo *ParentVNI = Parent->getValNumInfo(i);
    MachineBasicBlock *DefMBB = LIS.getMBBFromIndex(ParentVNI->def);
    // Get a less loopy dominator than Dom.first.
    Dom.first = findShallowDominator(Dom.first, DefMBB);
    if (SpillMode == SM_Speed &&
        MBFI.getBlockFreq(Dom.first) > Costs[ParentVNI->id]) {
      NotToHoistSet.insert(ParentVNI->id);
      continue;
    }
    SlotIndex Last = LIS.getMBBEndIdx(Dom.first).getPrevSlot();
    Dom.second =
      defFromParent(0, ParentVNI, Last, *Dom.first,
                    SA.getLastSplitPointIter(Dom.first))->def;
  }

  // Remove redundant back-copies that are now known to be dominated by another
  // def with the same value.
  SmallVector<VNInfo*, 8> BackCopies;
  for (VNInfo *VNI : LI->valnos) {
    if (VNI->isUnused())
      continue;
    VNInfo *ParentVNI = Edit->getParent().getVNInfoAt(VNI->def);
    const DomPair &Dom = NearestDom[ParentVNI->id];
    if (!Dom.first || Dom.second == VNI->def ||
        NotToHoistSet.count(ParentVNI->id))
      continue;
    BackCopies.push_back(VNI);
    forceRecompute(0, ParentVNI);
  }

  // If it is not beneficial to hoist all the BackCopies, simply remove
  // redundant BackCopies in speed mode.
  if (SpillMode == SM_Speed && !NotToHoistSet.empty())
    computeRedundantBackCopies(NotToHoistSet, BackCopies);

  removeBackCopies(BackCopies);
}


/// transferValues - Transfer all possible values to the new live ranges.
/// Values that were rematerialized are left alone, they need LRCalc.extend().
bool SplitEditor::transferValues() {
  bool Skipped = false;
  RegAssignMap::const_iterator AssignI = RegAssign.begin();
  for (const LiveRange::Segment &S : Edit->getParent()) {
    DEBUG(dbgs() << "  blit " << S << ':');
    VNInfo *ParentVNI = S.valno;
    // RegAssign has holes where RegIdx 0 should be used.
    SlotIndex Start = S.start;
    AssignI.advanceTo(Start);
    do {
      unsigned RegIdx;
      SlotIndex End = S.end;
      if (!AssignI.valid()) {
        RegIdx = 0;
      } else if (AssignI.start() <= Start) {
        RegIdx = AssignI.value();
        if (AssignI.stop() < End) {
          End = AssignI.stop();
          ++AssignI;
        }
      } else {
        RegIdx = 0;
        End = std::min(End, AssignI.start());
      }

      // The interval [Start;End) is continuously mapped to RegIdx, ParentVNI.
      DEBUG(dbgs() << " [" << Start << ';' << End << ")=" << RegIdx
                   << '(' << PrintReg(Edit->get(RegIdx)) << ')');
      LiveInterval &LI = LIS.getInterval(Edit->get(RegIdx));

      // Check for a simply defined value that can be blitted directly.
      ValueForcePair VFP = Values.lookup(std::make_pair(RegIdx, ParentVNI->id));
      if (VNInfo *VNI = VFP.getPointer()) {
        DEBUG(dbgs() << ':' << VNI->id);
        LI.addSegment(LiveInterval::Segment(Start, End, VNI));
        Start = End;
        continue;
      }

      // Skip values with forced recomputation.
      if (VFP.getInt()) {
        DEBUG(dbgs() << "(recalc)");
        Skipped = true;
        Start = End;
        continue;
      }

      LiveRangeCalc &LRC = getLRCalc(RegIdx);

      // This value has multiple defs in RegIdx, but it wasn't rematerialized,
      // so the live range is accurate. Add live-in blocks in [Start;End) to the
      // LiveInBlocks.
      MachineFunction::iterator MBB = LIS.getMBBFromIndex(Start)->getIterator();
      SlotIndex BlockStart, BlockEnd;
      std::tie(BlockStart, BlockEnd) = LIS.getSlotIndexes()->getMBBRange(&*MBB);

      // The first block may be live-in, or it may have its own def.
      if (Start != BlockStart) {
        VNInfo *VNI = LI.extendInBlock(BlockStart, std::min(BlockEnd, End));
        assert(VNI && "Missing def for complex mapped value");
        DEBUG(dbgs() << ':' << VNI->id << "*BB#" << MBB->getNumber());
        // MBB has its own def. Is it also live-out?
        if (BlockEnd <= End)
          LRC.setLiveOutValue(&*MBB, VNI);

        // Skip to the next block for live-in.
        ++MBB;
        BlockStart = BlockEnd;
      }

      // Handle the live-in blocks covered by [Start;End).
      assert(Start <= BlockStart && "Expected live-in block");
      while (BlockStart < End) {
        DEBUG(dbgs() << ">BB#" << MBB->getNumber());
        BlockEnd = LIS.getMBBEndIdx(&*MBB);
        if (BlockStart == ParentVNI->def) {
          // This block has the def of a parent PHI, so it isn't live-in.
          assert(ParentVNI->isPHIDef() && "Non-phi defined at block start?");
          VNInfo *VNI = LI.extendInBlock(BlockStart, std::min(BlockEnd, End));
          assert(VNI && "Missing def for complex mapped parent PHI");
          if (End >= BlockEnd)
            LRC.setLiveOutValue(&*MBB, VNI); // Live-out as well.
        } else {
          // This block needs a live-in value.  The last block covered may not
          // be live-out.
          if (End < BlockEnd)
            LRC.addLiveInBlock(LI, MDT[&*MBB], End);
          else {
            // Live-through, and we don't know the value.
            LRC.addLiveInBlock(LI, MDT[&*MBB]);
            LRC.setLiveOutValue(&*MBB, nullptr);
          }
        }
        BlockStart = BlockEnd;
        ++MBB;
      }
      Start = End;
    } while (Start != S.end);
    DEBUG(dbgs() << '\n');
  }

  LRCalc[0].calculateValues();
  if (SpillMode)
    LRCalc[1].calculateValues();

  return Skipped;
}

static bool removeDeadSegment(SlotIndex Def, LiveRange &LR) {
  const LiveRange::Segment *Seg = LR.getSegmentContaining(Def);
  if (Seg == nullptr)
    return true;
  if (Seg->end != Def.getDeadSlot())
    return false;
  // This is a dead PHI. Remove it.
  LR.removeSegment(*Seg, true);
  return true;
}

void SplitEditor::extendPHIRange(MachineBasicBlock &B, LiveRangeCalc &LRC,
                                 LiveRange &LR, LaneBitmask LM,
                                 ArrayRef<SlotIndex> Undefs) {
  for (MachineBasicBlock *P : B.predecessors()) {
    SlotIndex End = LIS.getMBBEndIdx(P);
    SlotIndex LastUse = End.getPrevSlot();
    // The predecessor may not have a live-out value. That is OK, like an
    // undef PHI operand.
    LiveInterval &PLI = Edit->getParent();
    // Need the cast because the inputs to ?: would otherwise be deemed
    // "incompatible": SubRange vs LiveInterval.
    LiveRange &PSR = !LM.all() ? getSubRangeForMask(LM, PLI)
                               : static_cast<LiveRange&>(PLI);
    if (PSR.liveAt(LastUse))
      LRC.extend(LR, End, /*PhysReg=*/0, Undefs);
  }
}

void SplitEditor::extendPHIKillRanges() {
  // Extend live ranges to be live-out for successor PHI values.

  // Visit each PHI def slot in the parent live interval. If the def is dead,
  // remove it. Otherwise, extend the live interval to reach the end indexes
  // of all predecessor blocks.

  LiveInterval &ParentLI = Edit->getParent();
  for (const VNInfo *V : ParentLI.valnos) {
    if (V->isUnused() || !V->isPHIDef())
      continue;

    unsigned RegIdx = RegAssign.lookup(V->def);
    LiveInterval &LI = LIS.getInterval(Edit->get(RegIdx));
    LiveRangeCalc &LRC = getLRCalc(RegIdx);
    MachineBasicBlock &B = *LIS.getMBBFromIndex(V->def);
    if (!removeDeadSegment(V->def, LI))
      extendPHIRange(B, LRC, LI, LaneBitmask::getAll(), /*Undefs=*/{});
  }

  SmallVector<SlotIndex, 4> Undefs;
  LiveRangeCalc SubLRC;

  for (LiveInterval::SubRange &PS : ParentLI.subranges()) {
    for (const VNInfo *V : PS.valnos) {
      if (V->isUnused() || !V->isPHIDef())
        continue;
      unsigned RegIdx = RegAssign.lookup(V->def);
      LiveInterval &LI = LIS.getInterval(Edit->get(RegIdx));
      LiveInterval::SubRange &S = getSubRangeForMask(PS.LaneMask, LI);
      if (removeDeadSegment(V->def, S))
        continue;

      MachineBasicBlock &B = *LIS.getMBBFromIndex(V->def);
      SubLRC.reset(&VRM.getMachineFunction(), LIS.getSlotIndexes(), &MDT,
                   &LIS.getVNInfoAllocator());
      Undefs.clear();
      LI.computeSubRangeUndefs(Undefs, PS.LaneMask, MRI, *LIS.getSlotIndexes());
      extendPHIRange(B, SubLRC, S, PS.LaneMask, Undefs);
    }
  }
}

/// rewriteAssigned - Rewrite all uses of Edit->getReg().
void SplitEditor::rewriteAssigned(bool ExtendRanges) {
  struct ExtPoint {
    ExtPoint(const MachineOperand &O, unsigned R, SlotIndex N)
      : MO(O), RegIdx(R), Next(N) {}
    MachineOperand MO;
    unsigned RegIdx;
    SlotIndex Next;
  };

  SmallVector<ExtPoint,4> ExtPoints;

  for (MachineRegisterInfo::reg_iterator RI = MRI.reg_begin(Edit->getReg()),
       RE = MRI.reg_end(); RI != RE;) {
    MachineOperand &MO = *RI;
    MachineInstr *MI = MO.getParent();
    ++RI;
    // LiveDebugVariables should have handled all DBG_VALUE instructions.
    if (MI->isDebugValue()) {
      DEBUG(dbgs() << "Zapping " << *MI);
      MO.setReg(0);
      continue;
    }

    // <undef> operands don't really read the register, so it doesn't matter
    // which register we choose.  When the use operand is tied to a def, we must
    // use the same register as the def, so just do that always.
    SlotIndex Idx = LIS.getInstructionIndex(*MI);
    if (MO.isDef() || MO.isUndef())
      Idx = Idx.getRegSlot(MO.isEarlyClobber());

    // Rewrite to the mapped register at Idx.
    unsigned RegIdx = RegAssign.lookup(Idx);
    LiveInterval &LI = LIS.getInterval(Edit->get(RegIdx));
    MO.setReg(LI.reg);
    DEBUG(dbgs() << "  rewr BB#" << MI->getParent()->getNumber() << '\t'
                 << Idx << ':' << RegIdx << '\t' << *MI);

    // Extend liveness to Idx if the instruction reads reg.
    if (!ExtendRanges || MO.isUndef())
      continue;

    // Skip instructions that don't read Reg.
    if (MO.isDef()) {
      if (!MO.getSubReg() && !MO.isEarlyClobber())
        continue;
      // We may want to extend a live range for a partial redef, or for a use
      // tied to an early clobber.
      Idx = Idx.getPrevSlot();
      if (!Edit->getParent().liveAt(Idx))
        continue;
    } else
      Idx = Idx.getRegSlot(true);

    SlotIndex Next = Idx.getNextSlot();
    if (LI.hasSubRanges()) {
      // We have to delay extending subranges until we have seen all operands
      // defining the register. This is because a <def,read-undef> operand
      // will create an "undef" point, and we cannot extend any subranges
      // until all of them have been accounted for.
      if (MO.isUse())
        ExtPoints.push_back(ExtPoint(MO, RegIdx, Next));
    } else {
      LiveRangeCalc &LRC = getLRCalc(RegIdx);
      LRC.extend(LI, Next, 0, ArrayRef<SlotIndex>());
    }
  }

  for (ExtPoint &EP : ExtPoints) {
    LiveInterval &LI = LIS.getInterval(Edit->get(EP.RegIdx));
    assert(LI.hasSubRanges());

    LiveRangeCalc SubLRC;
    unsigned Reg = EP.MO.getReg(), Sub = EP.MO.getSubReg();
    LaneBitmask LM = Sub != 0 ? TRI.getSubRegIndexLaneMask(Sub)
                              : MRI.getMaxLaneMaskForVReg(Reg);
    for (LiveInterval::SubRange &S : LI.subranges()) {
      if ((S.LaneMask & LM).none())
        continue;
      // The problem here can be that the new register may have been created
      // for a partially defined original register. For example:
      //   %vreg827:subreg_hireg<def,read-undef> = ...
      //   ...
      //   %vreg828<def> = COPY %vreg827
      if (S.empty())
        continue;
      SubLRC.reset(&VRM.getMachineFunction(), LIS.getSlotIndexes(), &MDT,
                   &LIS.getVNInfoAllocator());
      SmallVector<SlotIndex, 4> Undefs;
      LI.computeSubRangeUndefs(Undefs, S.LaneMask, MRI, *LIS.getSlotIndexes());
      SubLRC.extend(S, EP.Next, 0, Undefs);
    }
  }

  for (unsigned R : *Edit) {
    LiveInterval &LI = LIS.getInterval(R);
    if (!LI.hasSubRanges())
      continue;
    LI.clear();
    LI.removeEmptySubRanges();
    LIS.constructMainRangeFromSubranges(LI);
  }
}

void SplitEditor::deleteRematVictims() {
  SmallVector<MachineInstr*, 8> Dead;
  for (LiveRangeEdit::iterator I = Edit->begin(), E = Edit->end(); I != E; ++I){
    LiveInterval *LI = &LIS.getInterval(*I);
    for (const LiveRange::Segment &S : LI->segments) {
      // Dead defs end at the dead slot.
      if (S.end != S.valno->def.getDeadSlot())
        continue;
      if (S.valno->isPHIDef())
        continue;
      MachineInstr *MI = LIS.getInstructionFromIndex(S.valno->def);
      assert(MI && "Missing instruction for dead def");
      MI->addRegisterDead(LI->reg, &TRI);

      if (!MI->allDefsAreDead())
        continue;

      DEBUG(dbgs() << "All defs dead: " << *MI);
      Dead.push_back(MI);
    }
  }

  if (Dead.empty())
    return;

  Edit->eliminateDeadDefs(Dead, None, &AA);
}

void SplitEditor::finish(SmallVectorImpl<unsigned> *LRMap) {
  ++NumFinished;

  // At this point, the live intervals in Edit contain VNInfos corresponding to
  // the inserted copies.

  // Add the original defs from the parent interval.
  for (const VNInfo *ParentVNI : Edit->getParent().valnos) {
    if (ParentVNI->isUnused())
      continue;
    unsigned RegIdx = RegAssign.lookup(ParentVNI->def);
    defValue(RegIdx, ParentVNI, ParentVNI->def, true);

    // Force rematted values to be recomputed everywhere.
    // The new live ranges may be truncated.
    if (Edit->didRematerialize(ParentVNI))
      for (unsigned i = 0, e = Edit->size(); i != e; ++i)
        forceRecompute(i, ParentVNI);
  }

  // Hoist back-copies to the complement interval when in spill mode.
  switch (SpillMode) {
  case SM_Partition:
    // Leave all back-copies as is.
    break;
  case SM_Size:
  case SM_Speed:
    // hoistCopies will behave differently between size and speed.
    hoistCopies();
  }

  // Transfer the simply mapped values, check if any are skipped.
  bool Skipped = transferValues();

  // Rewrite virtual registers, possibly extending ranges.
  rewriteAssigned(Skipped);

  if (Skipped)
    extendPHIKillRanges();
  else
    ++NumSimple;

  // Delete defs that were rematted everywhere.
  if (Skipped)
    deleteRematVictims();

  // Get rid of unused values and set phi-kill flags.
  for (unsigned Reg : *Edit) {
    LiveInterval &LI = LIS.getInterval(Reg);
    LI.removeEmptySubRanges();
    LI.RenumberValues();
  }

  // Provide a reverse mapping from original indices to Edit ranges.
  if (LRMap) {
    LRMap->clear();
    for (unsigned i = 0, e = Edit->size(); i != e; ++i)
      LRMap->push_back(i);
  }

  // Now check if any registers were separated into multiple components.
  ConnectedVNInfoEqClasses ConEQ(LIS);
  for (unsigned i = 0, e = Edit->size(); i != e; ++i) {
    // Don't use iterators, they are invalidated by create() below.
    unsigned VReg = Edit->get(i);
    LiveInterval &LI = LIS.getInterval(VReg);
    SmallVector<LiveInterval*, 8> SplitLIs;
    LIS.splitSeparateComponents(LI, SplitLIs);
    unsigned Original = VRM.getOriginal(VReg);
    for (LiveInterval *SplitLI : SplitLIs)
      VRM.setIsSplitFromReg(SplitLI->reg, Original);

    // The new intervals all map back to i.
    if (LRMap)
      LRMap->resize(Edit->size(), i);
  }

  // Calculate spill weight and allocation hints for new intervals.
  Edit->calculateRegClassAndHint(VRM.getMachineFunction(), SA.Loops, MBFI);

  assert(!LRMap || LRMap->size() == Edit->size());
}


//===----------------------------------------------------------------------===//
//                            Single Block Splitting
//===----------------------------------------------------------------------===//

bool SplitAnalysis::shouldSplitSingleBlock(const BlockInfo &BI,
                                           bool SingleInstrs) const {
  // Always split for multiple instructions.
  if (!BI.isOneInstr())
    return true;
  // Don't split for single instructions unless explicitly requested.
  if (!SingleInstrs)
    return false;
  // Splitting a live-through range always makes progress.
  if (BI.LiveIn && BI.LiveOut)
    return true;
  // No point in isolating a copy. It has no register class constraints.
  if (LIS.getInstructionFromIndex(BI.FirstInstr)->isCopyLike())
    return false;
  // Finally, don't isolate an end point that was created by earlier splits.
  return isOriginalEndpoint(BI.FirstInstr);
}

void SplitEditor::splitSingleBlock(const SplitAnalysis::BlockInfo &BI) {
  openIntv();
  SlotIndex LastSplitPoint = SA.getLastSplitPoint(BI.MBB->getNumber());
  SlotIndex SegStart = enterIntvBefore(std::min(BI.FirstInstr,
    LastSplitPoint));
  if (!BI.LiveOut || BI.LastInstr < LastSplitPoint) {
    useIntv(SegStart, leaveIntvAfter(BI.LastInstr));
  } else {
      // The last use is after the last valid split point.
    SlotIndex SegStop = leaveIntvBefore(LastSplitPoint);
    useIntv(SegStart, SegStop);
    overlapIntv(SegStop, BI.LastInstr);
  }
}


//===----------------------------------------------------------------------===//
//                    Global Live Range Splitting Support
//===----------------------------------------------------------------------===//

// These methods support a method of global live range splitting that uses a
// global algorithm to decide intervals for CFG edges. They will insert split
// points and color intervals in basic blocks while avoiding interference.
//
// Note that splitSingleBlock is also useful for blocks where both CFG edges
// are on the stack.

void SplitEditor::splitLiveThroughBlock(unsigned MBBNum,
                                        unsigned IntvIn, SlotIndex LeaveBefore,
                                        unsigned IntvOut, SlotIndex EnterAfter){
  SlotIndex Start, Stop;
  std::tie(Start, Stop) = LIS.getSlotIndexes()->getMBBRange(MBBNum);

  DEBUG(dbgs() << "BB#" << MBBNum << " [" << Start << ';' << Stop
               << ") intf " << LeaveBefore << '-' << EnterAfter
               << ", live-through " << IntvIn << " -> " << IntvOut);

  assert((IntvIn || IntvOut) && "Use splitSingleBlock for isolated blocks");

  assert((!LeaveBefore || LeaveBefore < Stop) && "Interference after block");
  assert((!IntvIn || !LeaveBefore || LeaveBefore > Start) && "Impossible intf");
  assert((!EnterAfter || EnterAfter >= Start) && "Interference before block");

  MachineBasicBlock *MBB = VRM.getMachineFunction().getBlockNumbered(MBBNum);

  if (!IntvOut) {
    DEBUG(dbgs() << ", spill on entry.\n");
    //
    //        <<<<<<<<<    Possible LeaveBefore interference.
    //    |-----------|    Live through.
    //    -____________    Spill on entry.
    //
    selectIntv(IntvIn);
    SlotIndex Idx = leaveIntvAtTop(*MBB);
    assert((!LeaveBefore || Idx <= LeaveBefore) && "Interference");
    (void)Idx;
    return;
  }

  if (!IntvIn) {
    DEBUG(dbgs() << ", reload on exit.\n");
    //
    //    >>>>>>>          Possible EnterAfter interference.
    //    |-----------|    Live through.
    //    ___________--    Reload on exit.
    //
    selectIntv(IntvOut);
    SlotIndex Idx = enterIntvAtEnd(*MBB);
    assert((!EnterAfter || Idx >= EnterAfter) && "Interference");
    (void)Idx;
    return;
  }

  if (IntvIn == IntvOut && !LeaveBefore && !EnterAfter) {
    DEBUG(dbgs() << ", straight through.\n");
    //
    //    |-----------|    Live through.
    //    -------------    Straight through, same intv, no interference.
    //
    selectIntv(IntvOut);
    useIntv(Start, Stop);
    return;
  }

  // We cannot legally insert splits after LSP.
  SlotIndex LSP = SA.getLastSplitPoint(MBBNum);
  assert((!IntvOut || !EnterAfter || EnterAfter < LSP) && "Impossible intf");

  if (IntvIn != IntvOut && (!LeaveBefore || !EnterAfter ||
                  LeaveBefore.getBaseIndex() > EnterAfter.getBoundaryIndex())) {
    DEBUG(dbgs() << ", switch avoiding interference.\n");
    //
    //    >>>>     <<<<    Non-overlapping EnterAfter/LeaveBefore interference.
    //    |-----------|    Live through.
    //    ------=======    Switch intervals between interference.
    //
    selectIntv(IntvOut);
    SlotIndex Idx;
    if (LeaveBefore && LeaveBefore < LSP) {
      Idx = enterIntvBefore(LeaveBefore);
      useIntv(Idx, Stop);
    } else {
      Idx = enterIntvAtEnd(*MBB);
    }
    selectIntv(IntvIn);
    useIntv(Start, Idx);
    assert((!LeaveBefore || Idx <= LeaveBefore) && "Interference");
    assert((!EnterAfter || Idx >= EnterAfter) && "Interference");
    return;
  }

  DEBUG(dbgs() << ", create local intv for interference.\n");
  //
  //    >>><><><><<<<    Overlapping EnterAfter/LeaveBefore interference.
  //    |-----------|    Live through.
  //    ==---------==    Switch intervals before/after interference.
  //
  assert(LeaveBefore <= EnterAfter && "Missed case");

  selectIntv(IntvOut);
  SlotIndex Idx = enterIntvAfter(EnterAfter);
  useIntv(Idx, Stop);
  assert((!EnterAfter || Idx >= EnterAfter) && "Interference");

  selectIntv(IntvIn);
  Idx = leaveIntvBefore(LeaveBefore);
  useIntv(Start, Idx);
  assert((!LeaveBefore || Idx <= LeaveBefore) && "Interference");
}


void SplitEditor::splitRegInBlock(const SplitAnalysis::BlockInfo &BI,
                                  unsigned IntvIn, SlotIndex LeaveBefore) {
  SlotIndex Start, Stop;
  std::tie(Start, Stop) = LIS.getSlotIndexes()->getMBBRange(BI.MBB);

  DEBUG(dbgs() << "BB#" << BI.MBB->getNumber() << " [" << Start << ';' << Stop
               << "), uses " << BI.FirstInstr << '-' << BI.LastInstr
               << ", reg-in " << IntvIn << ", leave before " << LeaveBefore
               << (BI.LiveOut ? ", stack-out" : ", killed in block"));

  assert(IntvIn && "Must have register in");
  assert(BI.LiveIn && "Must be live-in");
  assert((!LeaveBefore || LeaveBefore > Start) && "Bad interference");

  if (!BI.LiveOut && (!LeaveBefore || LeaveBefore >= BI.LastInstr)) {
    DEBUG(dbgs() << " before interference.\n");
    //
    //               <<<    Interference after kill.
    //     |---o---x   |    Killed in block.
    //     =========        Use IntvIn everywhere.
    //
    selectIntv(IntvIn);
    useIntv(Start, BI.LastInstr);
    return;
  }

  SlotIndex LSP = SA.getLastSplitPoint(BI.MBB->getNumber());

  if (!LeaveBefore || LeaveBefore > BI.LastInstr.getBoundaryIndex()) {
    //
    //               <<<    Possible interference after last use.
    //     |---o---o---|    Live-out on stack.
    //     =========____    Leave IntvIn after last use.
    //
    //                 <    Interference after last use.
    //     |---o---o--o|    Live-out on stack, late last use.
    //     ============     Copy to stack after LSP, overlap IntvIn.
    //            \_____    Stack interval is live-out.
    //
    if (BI.LastInstr < LSP) {
      DEBUG(dbgs() << ", spill after last use before interference.\n");
      selectIntv(IntvIn);
      SlotIndex Idx = leaveIntvAfter(BI.LastInstr);
      useIntv(Start, Idx);
      assert((!LeaveBefore || Idx <= LeaveBefore) && "Interference");
    } else {
      DEBUG(dbgs() << ", spill before last split point.\n");
      selectIntv(IntvIn);
      SlotIndex Idx = leaveIntvBefore(LSP);
      overlapIntv(Idx, BI.LastInstr);
      useIntv(Start, Idx);
      assert((!LeaveBefore || Idx <= LeaveBefore) && "Interference");
    }
    return;
  }

  // The interference is overlapping somewhere we wanted to use IntvIn. That
  // means we need to create a local interval that can be allocated a
  // different register.
  unsigned LocalIntv = openIntv();
  (void)LocalIntv;
  DEBUG(dbgs() << ", creating local interval " << LocalIntv << ".\n");

  if (!BI.LiveOut || BI.LastInstr < LSP) {
    //
    //           <<<<<<<    Interference overlapping uses.
    //     |---o---o---|    Live-out on stack.
    //     =====----____    Leave IntvIn before interference, then spill.
    //
    SlotIndex To = leaveIntvAfter(BI.LastInstr);
    SlotIndex From = enterIntvBefore(LeaveBefore);
    useIntv(From, To);
    selectIntv(IntvIn);
    useIntv(Start, From);
    assert((!LeaveBefore || From <= LeaveBefore) && "Interference");
    return;
  }

  //           <<<<<<<    Interference overlapping uses.
  //     |---o---o--o|    Live-out on stack, late last use.
  //     =====-------     Copy to stack before LSP, overlap LocalIntv.
  //            \_____    Stack interval is live-out.
  //
  SlotIndex To = leaveIntvBefore(LSP);
  overlapIntv(To, BI.LastInstr);
  SlotIndex From = enterIntvBefore(std::min(To, LeaveBefore));
  useIntv(From, To);
  selectIntv(IntvIn);
  useIntv(Start, From);
  assert((!LeaveBefore || From <= LeaveBefore) && "Interference");
}

void SplitEditor::splitRegOutBlock(const SplitAnalysis::BlockInfo &BI,
                                   unsigned IntvOut, SlotIndex EnterAfter) {
  SlotIndex Start, Stop;
  std::tie(Start, Stop) = LIS.getSlotIndexes()->getMBBRange(BI.MBB);

  DEBUG(dbgs() << "BB#" << BI.MBB->getNumber() << " [" << Start << ';' << Stop
               << "), uses " << BI.FirstInstr << '-' << BI.LastInstr
               << ", reg-out " << IntvOut << ", enter after " << EnterAfter
               << (BI.LiveIn ? ", stack-in" : ", defined in block"));

  SlotIndex LSP = SA.getLastSplitPoint(BI.MBB->getNumber());

  assert(IntvOut && "Must have register out");
  assert(BI.LiveOut && "Must be live-out");
  assert((!EnterAfter || EnterAfter < LSP) && "Bad interference");

  if (!BI.LiveIn && (!EnterAfter || EnterAfter <= BI.FirstInstr)) {
    DEBUG(dbgs() << " after interference.\n");
    //
    //    >>>>             Interference before def.
    //    |   o---o---|    Defined in block.
    //        =========    Use IntvOut everywhere.
    //
    selectIntv(IntvOut);
    useIntv(BI.FirstInstr, Stop);
    return;
  }

  if (!EnterAfter || EnterAfter < BI.FirstInstr.getBaseIndex()) {
    DEBUG(dbgs() << ", reload after interference.\n");
    //
    //    >>>>             Interference before def.
    //    |---o---o---|    Live-through, stack-in.
    //    ____=========    Enter IntvOut before first use.
    //
    selectIntv(IntvOut);
    SlotIndex Idx = enterIntvBefore(std::min(LSP, BI.FirstInstr));
    useIntv(Idx, Stop);
    assert((!EnterAfter || Idx >= EnterAfter) && "Interference");
    return;
  }

  // The interference is overlapping somewhere we wanted to use IntvOut. That
  // means we need to create a local interval that can be allocated a
  // different register.
  DEBUG(dbgs() << ", interference overlaps uses.\n");
  //
  //    >>>>>>>          Interference overlapping uses.
  //    |---o---o---|    Live-through, stack-in.
  //    ____---======    Create local interval for interference range.
  //
  selectIntv(IntvOut);
  SlotIndex Idx = enterIntvAfter(EnterAfter);
  useIntv(Idx, Stop);
  assert((!EnterAfter || Idx >= EnterAfter) && "Interference");

  openIntv();
  SlotIndex From = enterIntvBefore(std::min(Idx, BI.FirstInstr));
  useIntv(From, Idx);
}
