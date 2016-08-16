//===----- CGCUDABuiltin.cpp - Codegen for CUDA builtins ------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// Generates code for built-in CUDA calls which are not runtime-specific.
// (Runtime-specific codegen lives in CGCUDARuntime.)
//
//===----------------------------------------------------------------------===//

#include "CodeGenFunction.h"
#include "clang/Basic/Builtins.h"
#include "llvm/IR/DataLayout.h"
#include "llvm/IR/Instruction.h"
#include "llvm/Support/MathExtras.h"

using namespace clang;
using namespace CodeGen;

static llvm::Function *GetVprintfDeclaration(llvm::Module &M) {
  llvm::Type *ArgTypes[] = {llvm::Type::getInt8PtrTy(M.getContext()),
                            llvm::Type::getInt8PtrTy(M.getContext())};
  llvm::FunctionType *VprintfFuncType = llvm::FunctionType::get(
      llvm::Type::getInt32Ty(M.getContext()), ArgTypes, false);

  if (auto* F = M.getFunction("vprintf")) {
    // Our CUDA system header declares vprintf with the right signature, so
    // nobody else should have been able to declare vprintf with a bogus
    // signature.
    assert(F->getFunctionType() == VprintfFuncType);
    return F;
  }

  // vprintf doesn't already exist; create a declaration and insert it into the
  // module.
  return llvm::Function::Create(
      VprintfFuncType, llvm::GlobalVariable::ExternalLinkage, "vprintf", &M);
}

// Transforms a call to printf into a call to the NVPTX vprintf syscall (which
// isn't particularly special; it's invoked just like a regular function).
// vprintf takes two args: A format string, and a pointer to a buffer containing
// the varargs.
//
// For example, the call
//
//   printf("format string", arg1, arg2, arg3);
//
// is converted into something resembling
//
//   struct Tmp {
//     Arg1 a1;
//     Arg2 a2;
//     Arg3 a3;
//   };
//   char* buf = alloca(sizeof(Tmp));
//   *(Tmp*)buf = {a1, a2, a3};
//   vprintf("format string", buf);
//
// buf is aligned to the max of {alignof(Arg1), ...}.  Furthermore, each of the
// args is itself aligned to its preferred alignment.
//
// Note that by the time this function runs, E's args have already undergone the
// standard C vararg promotion (short -> int, float -> double, etc.).
RValue
CodeGenFunction::EmitCUDADevicePrintfCallExpr(const CallExpr *E,
                                              ReturnValueSlot ReturnValue) {
  assert(getLangOpts().CUDA);
  assert(getLangOpts().CUDAIsDevice);
  assert(E->getBuiltinCallee() == Builtin::BIprintf);
  assert(E->getNumArgs() >= 1); // printf always has at least one arg.

  const llvm::DataLayout &DL = CGM.getDataLayout();
  llvm::LLVMContext &Ctx = CGM.getLLVMContext();

  CallArgList Args;
  EmitCallArgs(Args,
               E->getDirectCallee()->getType()->getAs<FunctionProtoType>(),
               E->arguments(), E->getDirectCallee(),
               /* ParamsToSkip = */ 0);

  // We don't know how to emit non-scalar varargs.
  if (std::any_of(Args.begin() + 1, Args.end(),
                  [](const CallArg &A) { return !A.RV.isScalar(); })) {
    CGM.ErrorUnsupported(E, "non-scalar arg to printf");
    return RValue::get(llvm::ConstantInt::get(IntTy, 0));
  }

  // Construct and fill the args buffer that we'll pass to vprintf.
  llvm::Value *BufferPtr;
  if (Args.size() <= 1) {
    // If there are no args, pass a null pointer to vprintf.
    BufferPtr = llvm::ConstantPointerNull::get(llvm::Type::getInt8PtrTy(Ctx));
  } else {
    llvm::SmallVector<llvm::Type *, 8> ArgTypes;
    for (unsigned I = 1, NumArgs = Args.size(); I < NumArgs; ++I)
      ArgTypes.push_back(Args[I].RV.getScalarVal()->getType());
    llvm::Type *AllocaTy = llvm::StructType::create(ArgTypes, "printf_args");
    llvm::Value *Alloca = CreateTempAlloca(AllocaTy);

    for (unsigned I = 1, NumArgs = Args.size(); I < NumArgs; ++I) {
      llvm::Value *P = Builder.CreateStructGEP(AllocaTy, Alloca, I - 1);
      llvm::Value *Arg = Args[I].RV.getScalarVal();
      Builder.CreateAlignedStore(Arg, P, DL.getPrefTypeAlignment(Arg->getType()));
    }
    BufferPtr = Builder.CreatePointerCast(Alloca, llvm::Type::getInt8PtrTy(Ctx));
  }

  // Invoke vprintf and return.
  llvm::Function* VprintfFunc = GetVprintfDeclaration(CGM.getModule());
  return RValue::get(
      Builder.CreateCall(VprintfFunc, {Args[0].RV.getScalarVal(), BufferPtr}));
}
