//===-- NVPTXInstPrinter.cpp - PTX assembly instruction printing ----------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Print MCInst instructions to .ptx format.
//
//===----------------------------------------------------------------------===//

#include "InstPrinter/NVPTXInstPrinter.h"
#include "MCTargetDesc/NVPTXBaseInfo.h"
#include "NVPTX.h"
#include "llvm/MC/MCExpr.h"
#include "llvm/MC/MCInst.h"
#include "llvm/MC/MCInstrInfo.h"
#include "llvm/MC/MCSubtargetInfo.h"
#include "llvm/MC/MCSymbol.h"
#include "llvm/Support/ErrorHandling.h"
#include "llvm/Support/FormattedStream.h"
#include <cctype>
using namespace llvm;

#define DEBUG_TYPE "asm-printer"

#include "NVPTXGenAsmWriter.inc"

NVPTXInstPrinter::NVPTXInstPrinter(const MCAsmInfo &MAI, const MCInstrInfo &MII,
                                   const MCRegisterInfo &MRI)
    : MCInstPrinter(MAI, MII, MRI) {}

void NVPTXInstPrinter::printRegName(raw_ostream &OS, unsigned RegNo) const {
  // Decode the virtual register
  // Must be kept in sync with NVPTXAsmPrinter::encodeVirtualRegister
  unsigned RCId = (RegNo >> 28);
  switch (RCId) {
  default: report_fatal_error("Bad virtual register encoding");
  case 0:
    // This is actually a physical register, so defer to the autogenerated
    // register printer
    OS << getRegisterName(RegNo);
    return;
  case 1:
    OS << "%p";
    break;
  case 2:
    OS << "%rs";
    break;
  case 3:
    OS << "%r";
    break;
  case 4:
    OS << "%rd";
    break;
  case 5:
    OS << "%f";
    break;
  case 6:
    OS << "%fd";
    break;
  case 7:
    OS << "%h";
    break;
  case 8:
    OS << "%hh";
    break;
  }

  unsigned VReg = RegNo & 0x0FFFFFFF;
  OS << VReg;
}

void NVPTXInstPrinter::printInst(const MCInst *MI, raw_ostream &OS,
                                 StringRef Annot, const MCSubtargetInfo &STI) {
  printInstruction(MI, OS);

  // Next always print the annotation.
  printAnnotation(OS, Annot);
}

void NVPTXInstPrinter::printOperand(const MCInst *MI, unsigned OpNo,
                                    raw_ostream &O) {
  const MCOperand &Op = MI->getOperand(OpNo);
  if (Op.isReg()) {
    unsigned Reg = Op.getReg();
    printRegName(O, Reg);
  } else if (Op.isImm()) {
    O << markup("<imm:") << formatImm(Op.getImm()) << markup(">");
  } else {
    assert(Op.isExpr() && "Unknown operand kind in printOperand");
    Op.getExpr()->print(O, &MAI);
  }
}

void NVPTXInstPrinter::printCvtMode(const MCInst *MI, int OpNum, raw_ostream &O,
                                    const char *Modifier) {
  const MCOperand &MO = MI->getOperand(OpNum);
  int64_t Imm = MO.getImm();

  if (strcmp(Modifier, "ftz") == 0) {
    // FTZ flag
    if (Imm & NVPTX::PTXCvtMode::FTZ_FLAG)
      O << ".ftz";
  } else if (strcmp(Modifier, "sat") == 0) {
    // SAT flag
    if (Imm & NVPTX::PTXCvtMode::SAT_FLAG)
      O << ".sat";
  } else if (strcmp(Modifier, "base") == 0) {
    // Default operand
    switch (Imm & NVPTX::PTXCvtMode::BASE_MASK) {
    default:
      return;
    case NVPTX::PTXCvtMode::NONE:
      break;
    case NVPTX::PTXCvtMode::RNI:
      O << ".rni";
      break;
    case NVPTX::PTXCvtMode::RZI:
      O << ".rzi";
      break;
    case NVPTX::PTXCvtMode::RMI:
      O << ".rmi";
      break;
    case NVPTX::PTXCvtMode::RPI:
      O << ".rpi";
      break;
    case NVPTX::PTXCvtMode::RN:
      O << ".rn";
      break;
    case NVPTX::PTXCvtMode::RZ:
      O << ".rz";
      break;
    case NVPTX::PTXCvtMode::RM:
      O << ".rm";
      break;
    case NVPTX::PTXCvtMode::RP:
      O << ".rp";
      break;
    }
  } else {
    llvm_unreachable("Invalid conversion modifier");
  }
}

void NVPTXInstPrinter::printCmpMode(const MCInst *MI, int OpNum, raw_ostream &O,
                                    const char *Modifier) {
  const MCOperand &MO = MI->getOperand(OpNum);
  int64_t Imm = MO.getImm();

  if (strcmp(Modifier, "ftz") == 0) {
    // FTZ flag
    if (Imm & NVPTX::PTXCmpMode::FTZ_FLAG)
      O << ".ftz";
  } else if (strcmp(Modifier, "base") == 0) {
    switch (Imm & NVPTX::PTXCmpMode::BASE_MASK) {
    default:
      return;
    case NVPTX::PTXCmpMode::EQ:
      O << ".eq";
      break;
    case NVPTX::PTXCmpMode::NE:
      O << ".ne";
      break;
    case NVPTX::PTXCmpMode::LT:
      O << ".lt";
      break;
    case NVPTX::PTXCmpMode::LE:
      O << ".le";
      break;
    case NVPTX::PTXCmpMode::GT:
      O << ".gt";
      break;
    case NVPTX::PTXCmpMode::GE:
      O << ".ge";
      break;
    case NVPTX::PTXCmpMode::LO:
      O << ".lo";
      break;
    case NVPTX::PTXCmpMode::LS:
      O << ".ls";
      break;
    case NVPTX::PTXCmpMode::HI:
      O << ".hi";
      break;
    case NVPTX::PTXCmpMode::HS:
      O << ".hs";
      break;
    case NVPTX::PTXCmpMode::EQU:
      O << ".equ";
      break;
    case NVPTX::PTXCmpMode::NEU:
      O << ".neu";
      break;
    case NVPTX::PTXCmpMode::LTU:
      O << ".ltu";
      break;
    case NVPTX::PTXCmpMode::LEU:
      O << ".leu";
      break;
    case NVPTX::PTXCmpMode::GTU:
      O << ".gtu";
      break;
    case NVPTX::PTXCmpMode::GEU:
      O << ".geu";
      break;
    case NVPTX::PTXCmpMode::NUM:
      O << ".num";
      break;
    case NVPTX::PTXCmpMode::NotANumber:
      O << ".nan";
      break;
    }
  } else {
    llvm_unreachable("Empty Modifier");
  }
}

void NVPTXInstPrinter::printLdStCode(const MCInst *MI, int OpNum,
                                     raw_ostream &O, const char *Modifier) {
  if (Modifier) {
    const MCOperand &MO = MI->getOperand(OpNum);
    int Imm = (int) MO.getImm();
    if (!strcmp(Modifier, "volatile")) {
      if (Imm)
        O << ".volatile";
    } else if (!strcmp(Modifier, "addsp")) {
      switch (Imm) {
      case NVPTX::PTXLdStInstCode::GLOBAL:
        O << ".global";
        break;
      case NVPTX::PTXLdStInstCode::SHARED:
        O << ".shared";
        break;
      case NVPTX::PTXLdStInstCode::LOCAL:
        O << ".local";
        break;
      case NVPTX::PTXLdStInstCode::PARAM:
        O << ".param";
        break;
      case NVPTX::PTXLdStInstCode::CONSTANT:
        O << ".const";
        break;
      case NVPTX::PTXLdStInstCode::GENERIC:
        break;
      default:
        llvm_unreachable("Wrong Address Space");
      }
    } else if (!strcmp(Modifier, "sign")) {
      if (Imm == NVPTX::PTXLdStInstCode::Signed)
        O << "s";
      else if (Imm == NVPTX::PTXLdStInstCode::Unsigned)
        O << "u";
      else if (Imm == NVPTX::PTXLdStInstCode::Untyped)
        O << "b";
      else if (Imm == NVPTX::PTXLdStInstCode::Float)
        O << "f";
      else
        llvm_unreachable("Unknown register type");
    } else if (!strcmp(Modifier, "vec")) {
      if (Imm == NVPTX::PTXLdStInstCode::V2)
        O << ".v2";
      else if (Imm == NVPTX::PTXLdStInstCode::V4)
        O << ".v4";
    } else
      llvm_unreachable("Unknown Modifier");
  } else
    llvm_unreachable("Empty Modifier");
}

void NVPTXInstPrinter::printMemOperand(const MCInst *MI, int OpNum,
                                       raw_ostream &O, const char *Modifier) {
  printOperand(MI, OpNum, O);

  if (Modifier && !strcmp(Modifier, "add")) {
    O << ", ";
    printOperand(MI, OpNum + 1, O);
  } else {
    if (MI->getOperand(OpNum + 1).isImm() &&
        MI->getOperand(OpNum + 1).getImm() == 0)
      return; // don't print ',0' or '+0'
    O << "+";
    printOperand(MI, OpNum + 1, O);
  }
}

void NVPTXInstPrinter::printProtoIdent(const MCInst *MI, int OpNum,
                                       raw_ostream &O, const char *Modifier) {
  const MCOperand &Op = MI->getOperand(OpNum);
  assert(Op.isExpr() && "Call prototype is not an MCExpr?");
  const MCExpr *Expr = Op.getExpr();
  const MCSymbol &Sym = cast<MCSymbolRefExpr>(Expr)->getSymbol();
  O << Sym.getName();
}
