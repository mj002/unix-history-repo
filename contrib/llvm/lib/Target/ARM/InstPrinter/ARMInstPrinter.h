//===- ARMInstPrinter.h - Convert ARM MCInst to assembly syntax -*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This class prints an ARM MCInst to a .s file.
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_LIB_TARGET_ARM_INSTPRINTER_ARMINSTPRINTER_H
#define LLVM_LIB_TARGET_ARM_INSTPRINTER_ARMINSTPRINTER_H

#include "llvm/MC/MCInstPrinter.h"
#include "llvm/MC/MCSubtargetInfo.h"

namespace llvm {

class MCOperand;

class ARMInstPrinter : public MCInstPrinter {
public:
  ARMInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                 const MCRegisterInfo &MRI, const MCSubtargetInfo &STI);

  void printInst(const MCInst *MI, raw_ostream &O, StringRef Annot) override;
  void printRegName(raw_ostream &OS, unsigned RegNo) const override;

  // Autogenerated by tblgen.
  void printInstruction(const MCInst *MI, raw_ostream &O);
  static const char *getRegisterName(unsigned RegNo);


  void printOperand(const MCInst *MI, unsigned OpNo, raw_ostream &O);

  void printSORegRegOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printSORegImmOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);

  void printAddrModeTBB(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printAddrModeTBH(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printAddrMode2Operand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printAM2PostIndexOp(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printAM2PreOrOffsetIndexOp(const MCInst *MI, unsigned OpNum,
                                  raw_ostream &O);
  void printAddrMode2OffsetOperand(const MCInst *MI, unsigned OpNum,
                                   raw_ostream &O);
  template <bool AlwaysPrintImm0>
  void printAddrMode3Operand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printAddrMode3OffsetOperand(const MCInst *MI, unsigned OpNum,
                                   raw_ostream &O);
  void printAM3PreOrOffsetIndexOp(const MCInst *MI, unsigned Op, raw_ostream &O,
                                  bool AlwaysPrintImm0);
  void printPostIdxImm8Operand(const MCInst *MI, unsigned OpNum,
                               raw_ostream &O);
  void printPostIdxRegOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printPostIdxImm8s4Operand(const MCInst *MI, unsigned OpNum,
                               raw_ostream &O);

  void printLdStmModeOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  template <bool AlwaysPrintImm0>
  void printAddrMode5Operand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printAddrMode6Operand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printAddrMode7Operand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printAddrMode6OffsetOperand(const MCInst *MI, unsigned OpNum,
                                   raw_ostream &O);

  void printBitfieldInvMaskImmOperand(const MCInst *MI, unsigned OpNum,
                                      raw_ostream &O);
  void printMemBOption(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printInstSyncBOption(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printShiftImmOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printPKHLSLShiftImm(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printPKHASRShiftImm(const MCInst *MI, unsigned OpNum, raw_ostream &O);

  template <unsigned scale>
  void printAdrLabelOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printThumbS4ImmOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printThumbSRImm(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printThumbITMask(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printThumbAddrModeRROperand(const MCInst *MI, unsigned OpNum,
                                   raw_ostream &O);
  void printThumbAddrModeImm5SOperand(const MCInst *MI, unsigned OpNum,
                                      raw_ostream &O, unsigned Scale);
  void printThumbAddrModeImm5S1Operand(const MCInst *MI, unsigned OpNum,
                                       raw_ostream &O);
  void printThumbAddrModeImm5S2Operand(const MCInst *MI, unsigned OpNum,
                                       raw_ostream &O);
  void printThumbAddrModeImm5S4Operand(const MCInst *MI, unsigned OpNum,
                                       raw_ostream &O);
  void printThumbAddrModeSPOperand(const MCInst *MI, unsigned OpNum,
                                   raw_ostream &O);

  void printT2SOOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  template<bool AlwaysPrintImm0>
  void printAddrModeImm12Operand(const MCInst *MI, unsigned OpNum,
                                 raw_ostream &O);
  template<bool AlwaysPrintImm0>
  void printT2AddrModeImm8Operand(const MCInst *MI, unsigned OpNum,
                                  raw_ostream &O);
  template<bool AlwaysPrintImm0>
  void printT2AddrModeImm8s4Operand(const MCInst *MI, unsigned OpNum,
                                    raw_ostream &O);
  void printT2AddrModeImm0_1020s4Operand(const MCInst *MI, unsigned OpNum,
                                    raw_ostream &O);
  void printT2AddrModeImm8OffsetOperand(const MCInst *MI, unsigned OpNum,
                                        raw_ostream &O);
  void printT2AddrModeImm8s4OffsetOperand(const MCInst *MI, unsigned OpNum,
                                          raw_ostream &O);
  void printT2AddrModeSoRegOperand(const MCInst *MI, unsigned OpNum,
                                   raw_ostream &O);

  void printSetendOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printCPSIMod(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printCPSIFlag(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printMSRMaskOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printBankedRegOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printPredicateOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printMandatoryPredicateOperand(const MCInst *MI, unsigned OpNum,
                                      raw_ostream &O);
  void printSBitModifierOperand(const MCInst *MI, unsigned OpNum,
                                raw_ostream &O);
  void printRegisterList(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printNoHashImmediate(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printPImmediate(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printCImmediate(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printCoprocOptionImm(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printFPImmOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printNEONModImmOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printImmPlusOneOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printRotImmOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printModImmOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printGPRPairOperand(const MCInst *MI, unsigned OpNum, raw_ostream &O);

  void printPCLabel(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printThumbLdrLabelOperand(const MCInst *MI, unsigned OpNum,
                                 raw_ostream &O);
  void printFBits16(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printFBits32(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printVectorIndex(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printVectorListOne(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printVectorListTwo(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printVectorListTwoSpaced(const MCInst *MI, unsigned OpNum,
                               raw_ostream &O);
  void printVectorListThree(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printVectorListFour(const MCInst *MI, unsigned OpNum, raw_ostream &O);
  void printVectorListOneAllLanes(const MCInst *MI, unsigned OpNum,
                                  raw_ostream &O);
  void printVectorListTwoAllLanes(const MCInst *MI, unsigned OpNum,
                                  raw_ostream &O);
  void printVectorListThreeAllLanes(const MCInst *MI, unsigned OpNum,
                                    raw_ostream &O);
  void printVectorListFourAllLanes(const MCInst *MI, unsigned OpNum,
                                   raw_ostream &O);
  void printVectorListTwoSpacedAllLanes(const MCInst *MI, unsigned OpNum,
                                        raw_ostream &O);
  void printVectorListThreeSpacedAllLanes(const MCInst *MI, unsigned OpNum,
                                          raw_ostream &O);
  void printVectorListFourSpacedAllLanes(const MCInst *MI, unsigned OpNum,
                                         raw_ostream &O);
  void printVectorListThreeSpaced(const MCInst *MI, unsigned OpNum,
                                  raw_ostream &O);
  void printVectorListFourSpaced(const MCInst *MI, unsigned OpNum,
                                  raw_ostream &O);
};

} // end namespace llvm

#endif
