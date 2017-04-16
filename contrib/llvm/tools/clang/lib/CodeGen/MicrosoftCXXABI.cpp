//===--- MicrosoftCXXABI.cpp - Emit LLVM Code from ASTs for a Module ------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This provides C++ code generation targeting the Microsoft Visual C++ ABI.
// The class in this file generates structures that follow the Microsoft
// Visual C++ ABI, which is actually not very well documented at all outside
// of Microsoft.
//
//===----------------------------------------------------------------------===//

#include "CGCXXABI.h"
#include "CGCleanup.h"
#include "CGVTables.h"
#include "CodeGenModule.h"
#include "CodeGenTypes.h"
#include "TargetInfo.h"
#include "clang/CodeGen/ConstantInitBuilder.h"
#include "clang/AST/Decl.h"
#include "clang/AST/DeclCXX.h"
#include "clang/AST/StmtCXX.h"
#include "clang/AST/VTableBuilder.h"
#include "llvm/ADT/StringExtras.h"
#include "llvm/ADT/StringSet.h"
#include "llvm/IR/CallSite.h"
#include "llvm/IR/Intrinsics.h"

using namespace clang;
using namespace CodeGen;

namespace {

/// Holds all the vbtable globals for a given class.
struct VBTableGlobals {
  const VPtrInfoVector *VBTables;
  SmallVector<llvm::GlobalVariable *, 2> Globals;
};

class MicrosoftCXXABI : public CGCXXABI {
public:
  MicrosoftCXXABI(CodeGenModule &CGM)
      : CGCXXABI(CGM), BaseClassDescriptorType(nullptr),
        ClassHierarchyDescriptorType(nullptr),
        CompleteObjectLocatorType(nullptr), CatchableTypeType(nullptr),
        ThrowInfoType(nullptr) {}

  bool HasThisReturn(GlobalDecl GD) const override;
  bool hasMostDerivedReturn(GlobalDecl GD) const override;

  bool classifyReturnType(CGFunctionInfo &FI) const override;

  RecordArgABI getRecordArgABI(const CXXRecordDecl *RD) const override;

  bool isSRetParameterAfterThis() const override { return true; }

  bool isThisCompleteObject(GlobalDecl GD) const override {
    // The Microsoft ABI doesn't use separate complete-object vs.
    // base-object variants of constructors, but it does of destructors.
    if (isa<CXXDestructorDecl>(GD.getDecl())) {
      switch (GD.getDtorType()) {
      case Dtor_Complete:
      case Dtor_Deleting:
        return true;

      case Dtor_Base:
        return false;

      case Dtor_Comdat: llvm_unreachable("emitting dtor comdat as function?");
      }
      llvm_unreachable("bad dtor kind");
    }

    // No other kinds.
    return false;
  }

  size_t getSrcArgforCopyCtor(const CXXConstructorDecl *CD,
                              FunctionArgList &Args) const override {
    assert(Args.size() >= 2 &&
           "expected the arglist to have at least two args!");
    // The 'most_derived' parameter goes second if the ctor is variadic and
    // has v-bases.
    if (CD->getParent()->getNumVBases() > 0 &&
        CD->getType()->castAs<FunctionProtoType>()->isVariadic())
      return 2;
    return 1;
  }

  std::vector<CharUnits> getVBPtrOffsets(const CXXRecordDecl *RD) override {
    std::vector<CharUnits> VBPtrOffsets;
    const ASTContext &Context = getContext();
    const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);

    const VBTableGlobals &VBGlobals = enumerateVBTables(RD);
    for (const std::unique_ptr<VPtrInfo> &VBT : *VBGlobals.VBTables) {
      const ASTRecordLayout &SubobjectLayout =
          Context.getASTRecordLayout(VBT->IntroducingObject);
      CharUnits Offs = VBT->NonVirtualOffset;
      Offs += SubobjectLayout.getVBPtrOffset();
      if (VBT->getVBaseWithVPtr())
        Offs += Layout.getVBaseClassOffset(VBT->getVBaseWithVPtr());
      VBPtrOffsets.push_back(Offs);
    }
    llvm::array_pod_sort(VBPtrOffsets.begin(), VBPtrOffsets.end());
    return VBPtrOffsets;
  }

  StringRef GetPureVirtualCallName() override { return "_purecall"; }
  StringRef GetDeletedVirtualCallName() override { return "_purecall"; }

  void emitVirtualObjectDelete(CodeGenFunction &CGF, const CXXDeleteExpr *DE,
                               Address Ptr, QualType ElementType,
                               const CXXDestructorDecl *Dtor) override;

  void emitRethrow(CodeGenFunction &CGF, bool isNoReturn) override;
  void emitThrow(CodeGenFunction &CGF, const CXXThrowExpr *E) override;

  void emitBeginCatch(CodeGenFunction &CGF, const CXXCatchStmt *C) override;

  llvm::GlobalVariable *getMSCompleteObjectLocator(const CXXRecordDecl *RD,
                                                   const VPtrInfo &Info);

  llvm::Constant *getAddrOfRTTIDescriptor(QualType Ty) override;
  CatchTypeInfo
  getAddrOfCXXCatchHandlerType(QualType Ty, QualType CatchHandlerType) override;

  /// MSVC needs an extra flag to indicate a catchall.
  CatchTypeInfo getCatchAllTypeInfo() override {
    return CatchTypeInfo{nullptr, 0x40};
  }

  bool shouldTypeidBeNullChecked(bool IsDeref, QualType SrcRecordTy) override;
  void EmitBadTypeidCall(CodeGenFunction &CGF) override;
  llvm::Value *EmitTypeid(CodeGenFunction &CGF, QualType SrcRecordTy,
                          Address ThisPtr,
                          llvm::Type *StdTypeInfoPtrTy) override;

  bool shouldDynamicCastCallBeNullChecked(bool SrcIsPtr,
                                          QualType SrcRecordTy) override;

  llvm::Value *EmitDynamicCastCall(CodeGenFunction &CGF, Address Value,
                                   QualType SrcRecordTy, QualType DestTy,
                                   QualType DestRecordTy,
                                   llvm::BasicBlock *CastEnd) override;

  llvm::Value *EmitDynamicCastToVoid(CodeGenFunction &CGF, Address Value,
                                     QualType SrcRecordTy,
                                     QualType DestTy) override;

  bool EmitBadCastCall(CodeGenFunction &CGF) override;
  bool canSpeculativelyEmitVTable(const CXXRecordDecl *RD) const override {
    return false;
  }

  llvm::Value *
  GetVirtualBaseClassOffset(CodeGenFunction &CGF, Address This,
                            const CXXRecordDecl *ClassDecl,
                            const CXXRecordDecl *BaseClassDecl) override;

  llvm::BasicBlock *
  EmitCtorCompleteObjectHandler(CodeGenFunction &CGF,
                                const CXXRecordDecl *RD) override;
  
  llvm::BasicBlock *
  EmitDtorCompleteObjectHandler(CodeGenFunction &CGF);

  void initializeHiddenVirtualInheritanceMembers(CodeGenFunction &CGF,
                                              const CXXRecordDecl *RD) override;

  void EmitCXXConstructors(const CXXConstructorDecl *D) override;

  // Background on MSVC destructors
  // ==============================
  //
  // Both Itanium and MSVC ABIs have destructor variants.  The variant names
  // roughly correspond in the following way:
  //   Itanium       Microsoft
  //   Base       -> no name, just ~Class
  //   Complete   -> vbase destructor
  //   Deleting   -> scalar deleting destructor
  //                 vector deleting destructor
  //
  // The base and complete destructors are the same as in Itanium, although the
  // complete destructor does not accept a VTT parameter when there are virtual
  // bases.  A separate mechanism involving vtordisps is used to ensure that
  // virtual methods of destroyed subobjects are not called.
  //
  // The deleting destructors accept an i32 bitfield as a second parameter.  Bit
  // 1 indicates if the memory should be deleted.  Bit 2 indicates if the this
  // pointer points to an array.  The scalar deleting destructor assumes that
  // bit 2 is zero, and therefore does not contain a loop.
  //
  // For virtual destructors, only one entry is reserved in the vftable, and it
  // always points to the vector deleting destructor.  The vector deleting
  // destructor is the most general, so it can be used to destroy objects in
  // place, delete single heap objects, or delete arrays.
  //
  // A TU defining a non-inline destructor is only guaranteed to emit a base
  // destructor, and all of the other variants are emitted on an as-needed basis
  // in COMDATs.  Because a non-base destructor can be emitted in a TU that
  // lacks a definition for the destructor, non-base destructors must always
  // delegate to or alias the base destructor.

  AddedStructorArgs
  buildStructorSignature(const CXXMethodDecl *MD, StructorType T,
                         SmallVectorImpl<CanQualType> &ArgTys) override;

  /// Non-base dtors should be emitted as delegating thunks in this ABI.
  bool useThunkForDtorVariant(const CXXDestructorDecl *Dtor,
                              CXXDtorType DT) const override {
    return DT != Dtor_Base;
  }

  void EmitCXXDestructors(const CXXDestructorDecl *D) override;

  const CXXRecordDecl *
  getThisArgumentTypeForMethod(const CXXMethodDecl *MD) override {
    MD = MD->getCanonicalDecl();
    if (MD->isVirtual() && !isa<CXXDestructorDecl>(MD)) {
      MicrosoftVTableContext::MethodVFTableLocation ML =
          CGM.getMicrosoftVTableContext().getMethodVFTableLocation(MD);
      // The vbases might be ordered differently in the final overrider object
      // and the complete object, so the "this" argument may sometimes point to
      // memory that has no particular type (e.g. past the complete object).
      // In this case, we just use a generic pointer type.
      // FIXME: might want to have a more precise type in the non-virtual
      // multiple inheritance case.
      if (ML.VBase || !ML.VFPtrOffset.isZero())
        return nullptr;
    }
    return MD->getParent();
  }

  Address
  adjustThisArgumentForVirtualFunctionCall(CodeGenFunction &CGF, GlobalDecl GD,
                                           Address This,
                                           bool VirtualCall) override;

  void addImplicitStructorParams(CodeGenFunction &CGF, QualType &ResTy,
                                 FunctionArgList &Params) override;

  llvm::Value *adjustThisParameterInVirtualFunctionPrologue(
      CodeGenFunction &CGF, GlobalDecl GD, llvm::Value *This) override;

  void EmitInstanceFunctionProlog(CodeGenFunction &CGF) override;

  AddedStructorArgs
  addImplicitConstructorArgs(CodeGenFunction &CGF, const CXXConstructorDecl *D,
                             CXXCtorType Type, bool ForVirtualBase,
                             bool Delegating, CallArgList &Args) override;

  void EmitDestructorCall(CodeGenFunction &CGF, const CXXDestructorDecl *DD,
                          CXXDtorType Type, bool ForVirtualBase,
                          bool Delegating, Address This) override;

  void emitVTableTypeMetadata(const VPtrInfo &Info, const CXXRecordDecl *RD,
                              llvm::GlobalVariable *VTable);

  void emitVTableDefinitions(CodeGenVTables &CGVT,
                             const CXXRecordDecl *RD) override;

  bool isVirtualOffsetNeededForVTableField(CodeGenFunction &CGF,
                                           CodeGenFunction::VPtr Vptr) override;

  /// Don't initialize vptrs if dynamic class
  /// is marked with with the 'novtable' attribute.
  bool doStructorsInitializeVPtrs(const CXXRecordDecl *VTableClass) override {
    return !VTableClass->hasAttr<MSNoVTableAttr>();
  }

  llvm::Constant *
  getVTableAddressPoint(BaseSubobject Base,
                        const CXXRecordDecl *VTableClass) override;

  llvm::Value *getVTableAddressPointInStructor(
      CodeGenFunction &CGF, const CXXRecordDecl *VTableClass,
      BaseSubobject Base, const CXXRecordDecl *NearestVBase) override;

  llvm::Constant *
  getVTableAddressPointForConstExpr(BaseSubobject Base,
                                    const CXXRecordDecl *VTableClass) override;

  llvm::GlobalVariable *getAddrOfVTable(const CXXRecordDecl *RD,
                                        CharUnits VPtrOffset) override;

  CGCallee getVirtualFunctionPointer(CodeGenFunction &CGF, GlobalDecl GD,
                                     Address This, llvm::Type *Ty,
                                     SourceLocation Loc) override;

  llvm::Value *EmitVirtualDestructorCall(CodeGenFunction &CGF,
                                         const CXXDestructorDecl *Dtor,
                                         CXXDtorType DtorType,
                                         Address This,
                                         const CXXMemberCallExpr *CE) override;

  void adjustCallArgsForDestructorThunk(CodeGenFunction &CGF, GlobalDecl GD,
                                        CallArgList &CallArgs) override {
    assert(GD.getDtorType() == Dtor_Deleting &&
           "Only deleting destructor thunks are available in this ABI");
    CallArgs.add(RValue::get(getStructorImplicitParamValue(CGF)),
                 getContext().IntTy);
  }

  void emitVirtualInheritanceTables(const CXXRecordDecl *RD) override;

  llvm::GlobalVariable *
  getAddrOfVBTable(const VPtrInfo &VBT, const CXXRecordDecl *RD,
                   llvm::GlobalVariable::LinkageTypes Linkage);

  llvm::GlobalVariable *
  getAddrOfVirtualDisplacementMap(const CXXRecordDecl *SrcRD,
                                  const CXXRecordDecl *DstRD) {
    SmallString<256> OutName;
    llvm::raw_svector_ostream Out(OutName);
    getMangleContext().mangleCXXVirtualDisplacementMap(SrcRD, DstRD, Out);
    StringRef MangledName = OutName.str();

    if (auto *VDispMap = CGM.getModule().getNamedGlobal(MangledName))
      return VDispMap;

    MicrosoftVTableContext &VTContext = CGM.getMicrosoftVTableContext();
    unsigned NumEntries = 1 + SrcRD->getNumVBases();
    SmallVector<llvm::Constant *, 4> Map(NumEntries,
                                         llvm::UndefValue::get(CGM.IntTy));
    Map[0] = llvm::ConstantInt::get(CGM.IntTy, 0);
    bool AnyDifferent = false;
    for (const auto &I : SrcRD->vbases()) {
      const CXXRecordDecl *VBase = I.getType()->getAsCXXRecordDecl();
      if (!DstRD->isVirtuallyDerivedFrom(VBase))
        continue;

      unsigned SrcVBIndex = VTContext.getVBTableIndex(SrcRD, VBase);
      unsigned DstVBIndex = VTContext.getVBTableIndex(DstRD, VBase);
      Map[SrcVBIndex] = llvm::ConstantInt::get(CGM.IntTy, DstVBIndex * 4);
      AnyDifferent |= SrcVBIndex != DstVBIndex;
    }
    // This map would be useless, don't use it.
    if (!AnyDifferent)
      return nullptr;

    llvm::ArrayType *VDispMapTy = llvm::ArrayType::get(CGM.IntTy, Map.size());
    llvm::Constant *Init = llvm::ConstantArray::get(VDispMapTy, Map);
    llvm::GlobalValue::LinkageTypes Linkage =
        SrcRD->isExternallyVisible() && DstRD->isExternallyVisible()
            ? llvm::GlobalValue::LinkOnceODRLinkage
            : llvm::GlobalValue::InternalLinkage;
    auto *VDispMap = new llvm::GlobalVariable(
        CGM.getModule(), VDispMapTy, /*Constant=*/true, Linkage,
        /*Initializer=*/Init, MangledName);
    return VDispMap;
  }

  void emitVBTableDefinition(const VPtrInfo &VBT, const CXXRecordDecl *RD,
                             llvm::GlobalVariable *GV) const;

  void setThunkLinkage(llvm::Function *Thunk, bool ForVTable,
                       GlobalDecl GD, bool ReturnAdjustment) override {
    // Never dllimport/dllexport thunks.
    Thunk->setDLLStorageClass(llvm::GlobalValue::DefaultStorageClass);

    GVALinkage Linkage =
        getContext().GetGVALinkageForFunction(cast<FunctionDecl>(GD.getDecl()));

    if (Linkage == GVA_Internal)
      Thunk->setLinkage(llvm::GlobalValue::InternalLinkage);
    else if (ReturnAdjustment)
      Thunk->setLinkage(llvm::GlobalValue::WeakODRLinkage);
    else
      Thunk->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
  }

  llvm::Value *performThisAdjustment(CodeGenFunction &CGF, Address This,
                                     const ThisAdjustment &TA) override;

  llvm::Value *performReturnAdjustment(CodeGenFunction &CGF, Address Ret,
                                       const ReturnAdjustment &RA) override;

  void EmitThreadLocalInitFuncs(
      CodeGenModule &CGM, ArrayRef<const VarDecl *> CXXThreadLocals,
      ArrayRef<llvm::Function *> CXXThreadLocalInits,
      ArrayRef<const VarDecl *> CXXThreadLocalInitVars) override;

  bool usesThreadWrapperFunction() const override { return false; }
  LValue EmitThreadLocalVarDeclLValue(CodeGenFunction &CGF, const VarDecl *VD,
                                      QualType LValType) override;

  void EmitGuardedInit(CodeGenFunction &CGF, const VarDecl &D,
                       llvm::GlobalVariable *DeclPtr,
                       bool PerformInit) override;
  void registerGlobalDtor(CodeGenFunction &CGF, const VarDecl &D,
                          llvm::Constant *Dtor, llvm::Constant *Addr) override;

  // ==== Notes on array cookies =========
  //
  // MSVC seems to only use cookies when the class has a destructor; a
  // two-argument usual array deallocation function isn't sufficient.
  //
  // For example, this code prints "100" and "1":
  //   struct A {
  //     char x;
  //     void *operator new[](size_t sz) {
  //       printf("%u\n", sz);
  //       return malloc(sz);
  //     }
  //     void operator delete[](void *p, size_t sz) {
  //       printf("%u\n", sz);
  //       free(p);
  //     }
  //   };
  //   int main() {
  //     A *p = new A[100];
  //     delete[] p;
  //   }
  // Whereas it prints "104" and "104" if you give A a destructor.

  bool requiresArrayCookie(const CXXDeleteExpr *expr,
                           QualType elementType) override;
  bool requiresArrayCookie(const CXXNewExpr *expr) override;
  CharUnits getArrayCookieSizeImpl(QualType type) override;
  Address InitializeArrayCookie(CodeGenFunction &CGF,
                                Address NewPtr,
                                llvm::Value *NumElements,
                                const CXXNewExpr *expr,
                                QualType ElementType) override;
  llvm::Value *readArrayCookieImpl(CodeGenFunction &CGF,
                                   Address allocPtr,
                                   CharUnits cookieSize) override;

  friend struct MSRTTIBuilder;

  bool isImageRelative() const {
    return CGM.getTarget().getPointerWidth(/*AddressSpace=*/0) == 64;
  }

  // 5 routines for constructing the llvm types for MS RTTI structs.
  llvm::StructType *getTypeDescriptorType(StringRef TypeInfoString) {
    llvm::SmallString<32> TDTypeName("rtti.TypeDescriptor");
    TDTypeName += llvm::utostr(TypeInfoString.size());
    llvm::StructType *&TypeDescriptorType =
        TypeDescriptorTypeMap[TypeInfoString.size()];
    if (TypeDescriptorType)
      return TypeDescriptorType;
    llvm::Type *FieldTypes[] = {
        CGM.Int8PtrPtrTy,
        CGM.Int8PtrTy,
        llvm::ArrayType::get(CGM.Int8Ty, TypeInfoString.size() + 1)};
    TypeDescriptorType =
        llvm::StructType::create(CGM.getLLVMContext(), FieldTypes, TDTypeName);
    return TypeDescriptorType;
  }

  llvm::Type *getImageRelativeType(llvm::Type *PtrType) {
    if (!isImageRelative())
      return PtrType;
    return CGM.IntTy;
  }

  llvm::StructType *getBaseClassDescriptorType() {
    if (BaseClassDescriptorType)
      return BaseClassDescriptorType;
    llvm::Type *FieldTypes[] = {
        getImageRelativeType(CGM.Int8PtrTy),
        CGM.IntTy,
        CGM.IntTy,
        CGM.IntTy,
        CGM.IntTy,
        CGM.IntTy,
        getImageRelativeType(getClassHierarchyDescriptorType()->getPointerTo()),
    };
    BaseClassDescriptorType = llvm::StructType::create(
        CGM.getLLVMContext(), FieldTypes, "rtti.BaseClassDescriptor");
    return BaseClassDescriptorType;
  }

  llvm::StructType *getClassHierarchyDescriptorType() {
    if (ClassHierarchyDescriptorType)
      return ClassHierarchyDescriptorType;
    // Forward-declare RTTIClassHierarchyDescriptor to break a cycle.
    ClassHierarchyDescriptorType = llvm::StructType::create(
        CGM.getLLVMContext(), "rtti.ClassHierarchyDescriptor");
    llvm::Type *FieldTypes[] = {
        CGM.IntTy,
        CGM.IntTy,
        CGM.IntTy,
        getImageRelativeType(
            getBaseClassDescriptorType()->getPointerTo()->getPointerTo()),
    };
    ClassHierarchyDescriptorType->setBody(FieldTypes);
    return ClassHierarchyDescriptorType;
  }

  llvm::StructType *getCompleteObjectLocatorType() {
    if (CompleteObjectLocatorType)
      return CompleteObjectLocatorType;
    CompleteObjectLocatorType = llvm::StructType::create(
        CGM.getLLVMContext(), "rtti.CompleteObjectLocator");
    llvm::Type *FieldTypes[] = {
        CGM.IntTy,
        CGM.IntTy,
        CGM.IntTy,
        getImageRelativeType(CGM.Int8PtrTy),
        getImageRelativeType(getClassHierarchyDescriptorType()->getPointerTo()),
        getImageRelativeType(CompleteObjectLocatorType),
    };
    llvm::ArrayRef<llvm::Type *> FieldTypesRef(FieldTypes);
    if (!isImageRelative())
      FieldTypesRef = FieldTypesRef.drop_back();
    CompleteObjectLocatorType->setBody(FieldTypesRef);
    return CompleteObjectLocatorType;
  }

  llvm::GlobalVariable *getImageBase() {
    StringRef Name = "__ImageBase";
    if (llvm::GlobalVariable *GV = CGM.getModule().getNamedGlobal(Name))
      return GV;

    return new llvm::GlobalVariable(CGM.getModule(), CGM.Int8Ty,
                                    /*isConstant=*/true,
                                    llvm::GlobalValue::ExternalLinkage,
                                    /*Initializer=*/nullptr, Name);
  }

  llvm::Constant *getImageRelativeConstant(llvm::Constant *PtrVal) {
    if (!isImageRelative())
      return PtrVal;

    if (PtrVal->isNullValue())
      return llvm::Constant::getNullValue(CGM.IntTy);

    llvm::Constant *ImageBaseAsInt =
        llvm::ConstantExpr::getPtrToInt(getImageBase(), CGM.IntPtrTy);
    llvm::Constant *PtrValAsInt =
        llvm::ConstantExpr::getPtrToInt(PtrVal, CGM.IntPtrTy);
    llvm::Constant *Diff =
        llvm::ConstantExpr::getSub(PtrValAsInt, ImageBaseAsInt,
                                   /*HasNUW=*/true, /*HasNSW=*/true);
    return llvm::ConstantExpr::getTrunc(Diff, CGM.IntTy);
  }

private:
  MicrosoftMangleContext &getMangleContext() {
    return cast<MicrosoftMangleContext>(CodeGen::CGCXXABI::getMangleContext());
  }

  llvm::Constant *getZeroInt() {
    return llvm::ConstantInt::get(CGM.IntTy, 0);
  }

  llvm::Constant *getAllOnesInt() {
    return  llvm::Constant::getAllOnesValue(CGM.IntTy);
  }

  CharUnits getVirtualFunctionPrologueThisAdjustment(GlobalDecl GD) override;

  void
  GetNullMemberPointerFields(const MemberPointerType *MPT,
                             llvm::SmallVectorImpl<llvm::Constant *> &fields);

  /// \brief Shared code for virtual base adjustment.  Returns the offset from
  /// the vbptr to the virtual base.  Optionally returns the address of the
  /// vbptr itself.
  llvm::Value *GetVBaseOffsetFromVBPtr(CodeGenFunction &CGF,
                                       Address Base,
                                       llvm::Value *VBPtrOffset,
                                       llvm::Value *VBTableOffset,
                                       llvm::Value **VBPtr = nullptr);

  llvm::Value *GetVBaseOffsetFromVBPtr(CodeGenFunction &CGF,
                                       Address Base,
                                       int32_t VBPtrOffset,
                                       int32_t VBTableOffset,
                                       llvm::Value **VBPtr = nullptr) {
    assert(VBTableOffset % 4 == 0 && "should be byte offset into table of i32s");
    llvm::Value *VBPOffset = llvm::ConstantInt::get(CGM.IntTy, VBPtrOffset),
                *VBTOffset = llvm::ConstantInt::get(CGM.IntTy, VBTableOffset);
    return GetVBaseOffsetFromVBPtr(CGF, Base, VBPOffset, VBTOffset, VBPtr);
  }

  std::pair<Address, llvm::Value *>
  performBaseAdjustment(CodeGenFunction &CGF, Address Value,
                        QualType SrcRecordTy);

  /// \brief Performs a full virtual base adjustment.  Used to dereference
  /// pointers to members of virtual bases.
  llvm::Value *AdjustVirtualBase(CodeGenFunction &CGF, const Expr *E,
                                 const CXXRecordDecl *RD, Address Base,
                                 llvm::Value *VirtualBaseAdjustmentOffset,
                                 llvm::Value *VBPtrOffset /* optional */);

  /// \brief Emits a full member pointer with the fields common to data and
  /// function member pointers.
  llvm::Constant *EmitFullMemberPointer(llvm::Constant *FirstField,
                                        bool IsMemberFunction,
                                        const CXXRecordDecl *RD,
                                        CharUnits NonVirtualBaseAdjustment,
                                        unsigned VBTableIndex);

  bool MemberPointerConstantIsNull(const MemberPointerType *MPT,
                                   llvm::Constant *MP);

  /// \brief - Initialize all vbptrs of 'this' with RD as the complete type.
  void EmitVBPtrStores(CodeGenFunction &CGF, const CXXRecordDecl *RD);

  /// \brief Caching wrapper around VBTableBuilder::enumerateVBTables().
  const VBTableGlobals &enumerateVBTables(const CXXRecordDecl *RD);

  /// \brief Generate a thunk for calling a virtual member function MD.
  llvm::Function *EmitVirtualMemPtrThunk(
      const CXXMethodDecl *MD,
      const MicrosoftVTableContext::MethodVFTableLocation &ML);

public:
  llvm::Type *ConvertMemberPointerType(const MemberPointerType *MPT) override;

  bool isZeroInitializable(const MemberPointerType *MPT) override;

  bool isMemberPointerConvertible(const MemberPointerType *MPT) const override {
    const CXXRecordDecl *RD = MPT->getMostRecentCXXRecordDecl();
    return RD->hasAttr<MSInheritanceAttr>();
  }

  llvm::Constant *EmitNullMemberPointer(const MemberPointerType *MPT) override;

  llvm::Constant *EmitMemberDataPointer(const MemberPointerType *MPT,
                                        CharUnits offset) override;
  llvm::Constant *EmitMemberFunctionPointer(const CXXMethodDecl *MD) override;
  llvm::Constant *EmitMemberPointer(const APValue &MP, QualType MPT) override;

  llvm::Value *EmitMemberPointerComparison(CodeGenFunction &CGF,
                                           llvm::Value *L,
                                           llvm::Value *R,
                                           const MemberPointerType *MPT,
                                           bool Inequality) override;

  llvm::Value *EmitMemberPointerIsNotNull(CodeGenFunction &CGF,
                                          llvm::Value *MemPtr,
                                          const MemberPointerType *MPT) override;

  llvm::Value *
  EmitMemberDataPointerAddress(CodeGenFunction &CGF, const Expr *E,
                               Address Base, llvm::Value *MemPtr,
                               const MemberPointerType *MPT) override;

  llvm::Value *EmitNonNullMemberPointerConversion(
      const MemberPointerType *SrcTy, const MemberPointerType *DstTy,
      CastKind CK, CastExpr::path_const_iterator PathBegin,
      CastExpr::path_const_iterator PathEnd, llvm::Value *Src,
      CGBuilderTy &Builder);

  llvm::Value *EmitMemberPointerConversion(CodeGenFunction &CGF,
                                           const CastExpr *E,
                                           llvm::Value *Src) override;

  llvm::Constant *EmitMemberPointerConversion(const CastExpr *E,
                                              llvm::Constant *Src) override;

  llvm::Constant *EmitMemberPointerConversion(
      const MemberPointerType *SrcTy, const MemberPointerType *DstTy,
      CastKind CK, CastExpr::path_const_iterator PathBegin,
      CastExpr::path_const_iterator PathEnd, llvm::Constant *Src);

  CGCallee
  EmitLoadOfMemberFunctionPointer(CodeGenFunction &CGF, const Expr *E,
                                  Address This, llvm::Value *&ThisPtrForCall,
                                  llvm::Value *MemPtr,
                                  const MemberPointerType *MPT) override;

  void emitCXXStructor(const CXXMethodDecl *MD, StructorType Type) override;

  llvm::StructType *getCatchableTypeType() {
    if (CatchableTypeType)
      return CatchableTypeType;
    llvm::Type *FieldTypes[] = {
        CGM.IntTy,                           // Flags
        getImageRelativeType(CGM.Int8PtrTy), // TypeDescriptor
        CGM.IntTy,                           // NonVirtualAdjustment
        CGM.IntTy,                           // OffsetToVBPtr
        CGM.IntTy,                           // VBTableIndex
        CGM.IntTy,                           // Size
        getImageRelativeType(CGM.Int8PtrTy)  // CopyCtor
    };
    CatchableTypeType = llvm::StructType::create(
        CGM.getLLVMContext(), FieldTypes, "eh.CatchableType");
    return CatchableTypeType;
  }

  llvm::StructType *getCatchableTypeArrayType(uint32_t NumEntries) {
    llvm::StructType *&CatchableTypeArrayType =
        CatchableTypeArrayTypeMap[NumEntries];
    if (CatchableTypeArrayType)
      return CatchableTypeArrayType;

    llvm::SmallString<23> CTATypeName("eh.CatchableTypeArray.");
    CTATypeName += llvm::utostr(NumEntries);
    llvm::Type *CTType =
        getImageRelativeType(getCatchableTypeType()->getPointerTo());
    llvm::Type *FieldTypes[] = {
        CGM.IntTy,                               // NumEntries
        llvm::ArrayType::get(CTType, NumEntries) // CatchableTypes
    };
    CatchableTypeArrayType =
        llvm::StructType::create(CGM.getLLVMContext(), FieldTypes, CTATypeName);
    return CatchableTypeArrayType;
  }

  llvm::StructType *getThrowInfoType() {
    if (ThrowInfoType)
      return ThrowInfoType;
    llvm::Type *FieldTypes[] = {
        CGM.IntTy,                           // Flags
        getImageRelativeType(CGM.Int8PtrTy), // CleanupFn
        getImageRelativeType(CGM.Int8PtrTy), // ForwardCompat
        getImageRelativeType(CGM.Int8PtrTy)  // CatchableTypeArray
    };
    ThrowInfoType = llvm::StructType::create(CGM.getLLVMContext(), FieldTypes,
                                             "eh.ThrowInfo");
    return ThrowInfoType;
  }

  llvm::Constant *getThrowFn() {
    // _CxxThrowException is passed an exception object and a ThrowInfo object
    // which describes the exception.
    llvm::Type *Args[] = {CGM.Int8PtrTy, getThrowInfoType()->getPointerTo()};
    llvm::FunctionType *FTy =
        llvm::FunctionType::get(CGM.VoidTy, Args, /*IsVarArgs=*/false);
    auto *Fn = cast<llvm::Function>(
        CGM.CreateRuntimeFunction(FTy, "_CxxThrowException"));
    // _CxxThrowException is stdcall on 32-bit x86 platforms.
    if (CGM.getTarget().getTriple().getArch() == llvm::Triple::x86)
      Fn->setCallingConv(llvm::CallingConv::X86_StdCall);
    return Fn;
  }

  llvm::Function *getAddrOfCXXCtorClosure(const CXXConstructorDecl *CD,
                                          CXXCtorType CT);

  llvm::Constant *getCatchableType(QualType T,
                                   uint32_t NVOffset = 0,
                                   int32_t VBPtrOffset = -1,
                                   uint32_t VBIndex = 0);

  llvm::GlobalVariable *getCatchableTypeArray(QualType T);

  llvm::GlobalVariable *getThrowInfo(QualType T) override;

private:
  typedef std::pair<const CXXRecordDecl *, CharUnits> VFTableIdTy;
  typedef llvm::DenseMap<VFTableIdTy, llvm::GlobalVariable *> VTablesMapTy;
  typedef llvm::DenseMap<VFTableIdTy, llvm::GlobalValue *> VFTablesMapTy;
  /// \brief All the vftables that have been referenced.
  VFTablesMapTy VFTablesMap;
  VTablesMapTy VTablesMap;

  /// \brief This set holds the record decls we've deferred vtable emission for.
  llvm::SmallPtrSet<const CXXRecordDecl *, 4> DeferredVFTables;


  /// \brief All the vbtables which have been referenced.
  llvm::DenseMap<const CXXRecordDecl *, VBTableGlobals> VBTablesMap;

  /// Info on the global variable used to guard initialization of static locals.
  /// The BitIndex field is only used for externally invisible declarations.
  struct GuardInfo {
    GuardInfo() : Guard(nullptr), BitIndex(0) {}
    llvm::GlobalVariable *Guard;
    unsigned BitIndex;
  };

  /// Map from DeclContext to the current guard variable.  We assume that the
  /// AST is visited in source code order.
  llvm::DenseMap<const DeclContext *, GuardInfo> GuardVariableMap;
  llvm::DenseMap<const DeclContext *, GuardInfo> ThreadLocalGuardVariableMap;
  llvm::DenseMap<const DeclContext *, unsigned> ThreadSafeGuardNumMap;

  llvm::DenseMap<size_t, llvm::StructType *> TypeDescriptorTypeMap;
  llvm::StructType *BaseClassDescriptorType;
  llvm::StructType *ClassHierarchyDescriptorType;
  llvm::StructType *CompleteObjectLocatorType;

  llvm::DenseMap<QualType, llvm::GlobalVariable *> CatchableTypeArrays;

  llvm::StructType *CatchableTypeType;
  llvm::DenseMap<uint32_t, llvm::StructType *> CatchableTypeArrayTypeMap;
  llvm::StructType *ThrowInfoType;
};

}

CGCXXABI::RecordArgABI
MicrosoftCXXABI::getRecordArgABI(const CXXRecordDecl *RD) const {
  switch (CGM.getTarget().getTriple().getArch()) {
  default:
    // FIXME: Implement for other architectures.
    return RAA_Default;

  case llvm::Triple::thumb:
    // Use the simple Itanium rules for now.
    // FIXME: This is incompatible with MSVC for arguments with a dtor and no
    // copy ctor.
    return !canCopyArgument(RD) ? RAA_Indirect : RAA_Default;

  case llvm::Triple::x86:
    // All record arguments are passed in memory on x86.  Decide whether to
    // construct the object directly in argument memory, or to construct the
    // argument elsewhere and copy the bytes during the call.

    // If C++ prohibits us from making a copy, construct the arguments directly
    // into argument memory.
    if (!canCopyArgument(RD))
      return RAA_DirectInMemory;

    // Otherwise, construct the argument into a temporary and copy the bytes
    // into the outgoing argument memory.
    return RAA_Default;

  case llvm::Triple::x86_64:
    // Win64 passes objects with non-trivial copy ctors indirectly.
    if (RD->hasNonTrivialCopyConstructor())
      return RAA_Indirect;

    // If an object has a destructor, we'd really like to pass it indirectly
    // because it allows us to elide copies.  Unfortunately, MSVC makes that
    // impossible for small types, which it will pass in a single register or
    // stack slot. Most objects with dtors are large-ish, so handle that early.
    // We can't call out all large objects as being indirect because there are
    // multiple x64 calling conventions and the C++ ABI code shouldn't dictate
    // how we pass large POD types.
    if (RD->hasNonTrivialDestructor() &&
        getContext().getTypeSize(RD->getTypeForDecl()) > 64)
      return RAA_Indirect;

    // If this is true, the implicit copy constructor that Sema would have
    // created would not be deleted. FIXME: We should provide a more direct way
    // for CodeGen to ask whether the constructor was deleted.
    if (!RD->hasUserDeclaredCopyConstructor() &&
        !RD->hasUserDeclaredMoveConstructor() &&
        !RD->needsOverloadResolutionForMoveConstructor() &&
        !RD->hasUserDeclaredMoveAssignment() &&
        !RD->needsOverloadResolutionForMoveAssignment())
      return RAA_Default;

    // Otherwise, Sema should have created an implicit copy constructor if
    // needed.
    assert(!RD->needsImplicitCopyConstructor());

    // We have to make sure the trivial copy constructor isn't deleted.
    for (const CXXConstructorDecl *CD : RD->ctors()) {
      if (CD->isCopyConstructor()) {
        assert(CD->isTrivial());
        // We had at least one undeleted trivial copy ctor.  Return directly.
        if (!CD->isDeleted())
          return RAA_Default;
      }
    }

    // The trivial copy constructor was deleted.  Return indirectly.
    return RAA_Indirect;
  }

  llvm_unreachable("invalid enum");
}

void MicrosoftCXXABI::emitVirtualObjectDelete(CodeGenFunction &CGF,
                                              const CXXDeleteExpr *DE,
                                              Address Ptr,
                                              QualType ElementType,
                                              const CXXDestructorDecl *Dtor) {
  // FIXME: Provide a source location here even though there's no
  // CXXMemberCallExpr for dtor call.
  bool UseGlobalDelete = DE->isGlobalDelete();
  CXXDtorType DtorType = UseGlobalDelete ? Dtor_Complete : Dtor_Deleting;
  llvm::Value *MDThis =
      EmitVirtualDestructorCall(CGF, Dtor, DtorType, Ptr, /*CE=*/nullptr);
  if (UseGlobalDelete)
    CGF.EmitDeleteCall(DE->getOperatorDelete(), MDThis, ElementType);
}

void MicrosoftCXXABI::emitRethrow(CodeGenFunction &CGF, bool isNoReturn) {
  llvm::Value *Args[] = {
      llvm::ConstantPointerNull::get(CGM.Int8PtrTy),
      llvm::ConstantPointerNull::get(getThrowInfoType()->getPointerTo())};
  auto *Fn = getThrowFn();
  if (isNoReturn)
    CGF.EmitNoreturnRuntimeCallOrInvoke(Fn, Args);
  else
    CGF.EmitRuntimeCallOrInvoke(Fn, Args);
}

namespace {
struct CatchRetScope final : EHScopeStack::Cleanup {
  llvm::CatchPadInst *CPI;

  CatchRetScope(llvm::CatchPadInst *CPI) : CPI(CPI) {}

  void Emit(CodeGenFunction &CGF, Flags flags) override {
    llvm::BasicBlock *BB = CGF.createBasicBlock("catchret.dest");
    CGF.Builder.CreateCatchRet(CPI, BB);
    CGF.EmitBlock(BB);
  }
};
}

void MicrosoftCXXABI::emitBeginCatch(CodeGenFunction &CGF,
                                     const CXXCatchStmt *S) {
  // In the MS ABI, the runtime handles the copy, and the catch handler is
  // responsible for destruction.
  VarDecl *CatchParam = S->getExceptionDecl();
  llvm::BasicBlock *CatchPadBB = CGF.Builder.GetInsertBlock();
  llvm::CatchPadInst *CPI =
      cast<llvm::CatchPadInst>(CatchPadBB->getFirstNonPHI());
  CGF.CurrentFuncletPad = CPI;

  // If this is a catch-all or the catch parameter is unnamed, we don't need to
  // emit an alloca to the object.
  if (!CatchParam || !CatchParam->getDeclName()) {
    CGF.EHStack.pushCleanup<CatchRetScope>(NormalCleanup, CPI);
    return;
  }

  CodeGenFunction::AutoVarEmission var = CGF.EmitAutoVarAlloca(*CatchParam);
  CPI->setArgOperand(2, var.getObjectAddress(CGF).getPointer());
  CGF.EHStack.pushCleanup<CatchRetScope>(NormalCleanup, CPI);
  CGF.EmitAutoVarCleanups(var);
}

/// We need to perform a generic polymorphic operation (like a typeid
/// or a cast), which requires an object with a vfptr.  Adjust the
/// address to point to an object with a vfptr.
std::pair<Address, llvm::Value *>
MicrosoftCXXABI::performBaseAdjustment(CodeGenFunction &CGF, Address Value,
                                       QualType SrcRecordTy) {
  Value = CGF.Builder.CreateBitCast(Value, CGF.Int8PtrTy);
  const CXXRecordDecl *SrcDecl = SrcRecordTy->getAsCXXRecordDecl();
  const ASTContext &Context = getContext();

  // If the class itself has a vfptr, great.  This check implicitly
  // covers non-virtual base subobjects: a class with its own virtual
  // functions would be a candidate to be a primary base.
  if (Context.getASTRecordLayout(SrcDecl).hasExtendableVFPtr())
    return std::make_pair(Value, llvm::ConstantInt::get(CGF.Int32Ty, 0));

  // Okay, one of the vbases must have a vfptr, or else this isn't
  // actually a polymorphic class.
  const CXXRecordDecl *PolymorphicBase = nullptr;
  for (auto &Base : SrcDecl->vbases()) {
    const CXXRecordDecl *BaseDecl = Base.getType()->getAsCXXRecordDecl();
    if (Context.getASTRecordLayout(BaseDecl).hasExtendableVFPtr()) {
      PolymorphicBase = BaseDecl;
      break;
    }
  }
  assert(PolymorphicBase && "polymorphic class has no apparent vfptr?");

  llvm::Value *Offset =
    GetVirtualBaseClassOffset(CGF, Value, SrcDecl, PolymorphicBase);
  llvm::Value *Ptr = CGF.Builder.CreateInBoundsGEP(Value.getPointer(), Offset);
  CharUnits VBaseAlign =
    CGF.CGM.getVBaseAlignment(Value.getAlignment(), SrcDecl, PolymorphicBase);
  return std::make_pair(Address(Ptr, VBaseAlign), Offset);
}

bool MicrosoftCXXABI::shouldTypeidBeNullChecked(bool IsDeref,
                                                QualType SrcRecordTy) {
  const CXXRecordDecl *SrcDecl = SrcRecordTy->getAsCXXRecordDecl();
  return IsDeref &&
         !getContext().getASTRecordLayout(SrcDecl).hasExtendableVFPtr();
}

static llvm::CallSite emitRTtypeidCall(CodeGenFunction &CGF,
                                       llvm::Value *Argument) {
  llvm::Type *ArgTypes[] = {CGF.Int8PtrTy};
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(CGF.Int8PtrTy, ArgTypes, false);
  llvm::Value *Args[] = {Argument};
  llvm::Constant *Fn = CGF.CGM.CreateRuntimeFunction(FTy, "__RTtypeid");
  return CGF.EmitRuntimeCallOrInvoke(Fn, Args);
}

void MicrosoftCXXABI::EmitBadTypeidCall(CodeGenFunction &CGF) {
  llvm::CallSite Call =
      emitRTtypeidCall(CGF, llvm::Constant::getNullValue(CGM.VoidPtrTy));
  Call.setDoesNotReturn();
  CGF.Builder.CreateUnreachable();
}

llvm::Value *MicrosoftCXXABI::EmitTypeid(CodeGenFunction &CGF,
                                         QualType SrcRecordTy,
                                         Address ThisPtr,
                                         llvm::Type *StdTypeInfoPtrTy) {
  std::tie(ThisPtr, std::ignore) =
      performBaseAdjustment(CGF, ThisPtr, SrcRecordTy);
  auto Typeid = emitRTtypeidCall(CGF, ThisPtr.getPointer()).getInstruction();
  return CGF.Builder.CreateBitCast(Typeid, StdTypeInfoPtrTy);
}

bool MicrosoftCXXABI::shouldDynamicCastCallBeNullChecked(bool SrcIsPtr,
                                                         QualType SrcRecordTy) {
  const CXXRecordDecl *SrcDecl = SrcRecordTy->getAsCXXRecordDecl();
  return SrcIsPtr &&
         !getContext().getASTRecordLayout(SrcDecl).hasExtendableVFPtr();
}

llvm::Value *MicrosoftCXXABI::EmitDynamicCastCall(
    CodeGenFunction &CGF, Address This, QualType SrcRecordTy,
    QualType DestTy, QualType DestRecordTy, llvm::BasicBlock *CastEnd) {
  llvm::Type *DestLTy = CGF.ConvertType(DestTy);

  llvm::Value *SrcRTTI =
      CGF.CGM.GetAddrOfRTTIDescriptor(SrcRecordTy.getUnqualifiedType());
  llvm::Value *DestRTTI =
      CGF.CGM.GetAddrOfRTTIDescriptor(DestRecordTy.getUnqualifiedType());

  llvm::Value *Offset;
  std::tie(This, Offset) = performBaseAdjustment(CGF, This, SrcRecordTy);
  llvm::Value *ThisPtr = This.getPointer();
  Offset = CGF.Builder.CreateTrunc(Offset, CGF.Int32Ty);

  // PVOID __RTDynamicCast(
  //   PVOID inptr,
  //   LONG VfDelta,
  //   PVOID SrcType,
  //   PVOID TargetType,
  //   BOOL isReference)
  llvm::Type *ArgTypes[] = {CGF.Int8PtrTy, CGF.Int32Ty, CGF.Int8PtrTy,
                            CGF.Int8PtrTy, CGF.Int32Ty};
  llvm::Constant *Function = CGF.CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(CGF.Int8PtrTy, ArgTypes, false),
      "__RTDynamicCast");
  llvm::Value *Args[] = {
      ThisPtr, Offset, SrcRTTI, DestRTTI,
      llvm::ConstantInt::get(CGF.Int32Ty, DestTy->isReferenceType())};
  ThisPtr = CGF.EmitRuntimeCallOrInvoke(Function, Args).getInstruction();
  return CGF.Builder.CreateBitCast(ThisPtr, DestLTy);
}

llvm::Value *
MicrosoftCXXABI::EmitDynamicCastToVoid(CodeGenFunction &CGF, Address Value,
                                       QualType SrcRecordTy,
                                       QualType DestTy) {
  std::tie(Value, std::ignore) = performBaseAdjustment(CGF, Value, SrcRecordTy);

  // PVOID __RTCastToVoid(
  //   PVOID inptr)
  llvm::Type *ArgTypes[] = {CGF.Int8PtrTy};
  llvm::Constant *Function = CGF.CGM.CreateRuntimeFunction(
      llvm::FunctionType::get(CGF.Int8PtrTy, ArgTypes, false),
      "__RTCastToVoid");
  llvm::Value *Args[] = {Value.getPointer()};
  return CGF.EmitRuntimeCall(Function, Args);
}

bool MicrosoftCXXABI::EmitBadCastCall(CodeGenFunction &CGF) {
  return false;
}

llvm::Value *MicrosoftCXXABI::GetVirtualBaseClassOffset(
    CodeGenFunction &CGF, Address This, const CXXRecordDecl *ClassDecl,
    const CXXRecordDecl *BaseClassDecl) {
  const ASTContext &Context = getContext();
  int64_t VBPtrChars =
      Context.getASTRecordLayout(ClassDecl).getVBPtrOffset().getQuantity();
  llvm::Value *VBPtrOffset = llvm::ConstantInt::get(CGM.PtrDiffTy, VBPtrChars);
  CharUnits IntSize = Context.getTypeSizeInChars(Context.IntTy);
  CharUnits VBTableChars =
      IntSize *
      CGM.getMicrosoftVTableContext().getVBTableIndex(ClassDecl, BaseClassDecl);
  llvm::Value *VBTableOffset =
      llvm::ConstantInt::get(CGM.IntTy, VBTableChars.getQuantity());

  llvm::Value *VBPtrToNewBase =
      GetVBaseOffsetFromVBPtr(CGF, This, VBPtrOffset, VBTableOffset);
  VBPtrToNewBase =
      CGF.Builder.CreateSExtOrBitCast(VBPtrToNewBase, CGM.PtrDiffTy);
  return CGF.Builder.CreateNSWAdd(VBPtrOffset, VBPtrToNewBase);
}

bool MicrosoftCXXABI::HasThisReturn(GlobalDecl GD) const {
  return isa<CXXConstructorDecl>(GD.getDecl());
}

static bool isDeletingDtor(GlobalDecl GD) {
  return isa<CXXDestructorDecl>(GD.getDecl()) &&
         GD.getDtorType() == Dtor_Deleting;
}

bool MicrosoftCXXABI::hasMostDerivedReturn(GlobalDecl GD) const {
  return isDeletingDtor(GD);
}

bool MicrosoftCXXABI::classifyReturnType(CGFunctionInfo &FI) const {
  const CXXRecordDecl *RD = FI.getReturnType()->getAsCXXRecordDecl();
  if (!RD)
    return false;

  CharUnits Align = CGM.getContext().getTypeAlignInChars(FI.getReturnType());
  if (FI.isInstanceMethod()) {
    // If it's an instance method, aggregates are always returned indirectly via
    // the second parameter.
    FI.getReturnInfo() = ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
    FI.getReturnInfo().setSRetAfterThis(FI.isInstanceMethod());
    return true;
  } else if (!RD->isPOD()) {
    // If it's a free function, non-POD types are returned indirectly.
    FI.getReturnInfo() = ABIArgInfo::getIndirect(Align, /*ByVal=*/false);
    return true;
  }

  // Otherwise, use the C ABI rules.
  return false;
}

llvm::BasicBlock *
MicrosoftCXXABI::EmitCtorCompleteObjectHandler(CodeGenFunction &CGF,
                                               const CXXRecordDecl *RD) {
  llvm::Value *IsMostDerivedClass = getStructorImplicitParamValue(CGF);
  assert(IsMostDerivedClass &&
         "ctor for a class with virtual bases must have an implicit parameter");
  llvm::Value *IsCompleteObject =
    CGF.Builder.CreateIsNotNull(IsMostDerivedClass, "is_complete_object");

  llvm::BasicBlock *CallVbaseCtorsBB = CGF.createBasicBlock("ctor.init_vbases");
  llvm::BasicBlock *SkipVbaseCtorsBB = CGF.createBasicBlock("ctor.skip_vbases");
  CGF.Builder.CreateCondBr(IsCompleteObject,
                           CallVbaseCtorsBB, SkipVbaseCtorsBB);

  CGF.EmitBlock(CallVbaseCtorsBB);

  // Fill in the vbtable pointers here.
  EmitVBPtrStores(CGF, RD);

  // CGF will put the base ctor calls in this basic block for us later.

  return SkipVbaseCtorsBB;
}

llvm::BasicBlock *
MicrosoftCXXABI::EmitDtorCompleteObjectHandler(CodeGenFunction &CGF) {
  llvm::Value *IsMostDerivedClass = getStructorImplicitParamValue(CGF);
  assert(IsMostDerivedClass &&
         "ctor for a class with virtual bases must have an implicit parameter");
  llvm::Value *IsCompleteObject =
      CGF.Builder.CreateIsNotNull(IsMostDerivedClass, "is_complete_object");

  llvm::BasicBlock *CallVbaseDtorsBB = CGF.createBasicBlock("Dtor.dtor_vbases");
  llvm::BasicBlock *SkipVbaseDtorsBB = CGF.createBasicBlock("Dtor.skip_vbases");
  CGF.Builder.CreateCondBr(IsCompleteObject,
                           CallVbaseDtorsBB, SkipVbaseDtorsBB);

  CGF.EmitBlock(CallVbaseDtorsBB);
  // CGF will put the base dtor calls in this basic block for us later.
    
  return SkipVbaseDtorsBB;
}

void MicrosoftCXXABI::initializeHiddenVirtualInheritanceMembers(
    CodeGenFunction &CGF, const CXXRecordDecl *RD) {
  // In most cases, an override for a vbase virtual method can adjust
  // the "this" parameter by applying a constant offset.
  // However, this is not enough while a constructor or a destructor of some
  // class X is being executed if all the following conditions are met:
  //  - X has virtual bases, (1)
  //  - X overrides a virtual method M of a vbase Y, (2)
  //  - X itself is a vbase of the most derived class.
  //
  // If (1) and (2) are true, the vtorDisp for vbase Y is a hidden member of X
  // which holds the extra amount of "this" adjustment we must do when we use
  // the X vftables (i.e. during X ctor or dtor).
  // Outside the ctors and dtors, the values of vtorDisps are zero.

  const ASTRecordLayout &Layout = getContext().getASTRecordLayout(RD);
  typedef ASTRecordLayout::VBaseOffsetsMapTy VBOffsets;
  const VBOffsets &VBaseMap = Layout.getVBaseOffsetsMap();
  CGBuilderTy &Builder = CGF.Builder;

  unsigned AS = getThisAddress(CGF).getAddressSpace();
  llvm::Value *Int8This = nullptr;  // Initialize lazily.

  for (VBOffsets::const_iterator I = VBaseMap.begin(), E = VBaseMap.end();
        I != E; ++I) {
    if (!I->second.hasVtorDisp())
      continue;

    llvm::Value *VBaseOffset =
        GetVirtualBaseClassOffset(CGF, getThisAddress(CGF), RD, I->first);
    uint64_t ConstantVBaseOffset =
        Layout.getVBaseClassOffset(I->first).getQuantity();

    // vtorDisp_for_vbase = vbptr[vbase_idx] - offsetof(RD, vbase).
    llvm::Value *VtorDispValue = Builder.CreateSub(
        VBaseOffset, llvm::ConstantInt::get(CGM.PtrDiffTy, ConstantVBaseOffset),
        "vtordisp.value");
    VtorDispValue = Builder.CreateTruncOrBitCast(VtorDispValue, CGF.Int32Ty);

    if (!Int8This)
      Int8This = Builder.CreateBitCast(getThisValue(CGF),
                                       CGF.Int8Ty->getPointerTo(AS));
    llvm::Value *VtorDispPtr = Builder.CreateInBoundsGEP(Int8This, VBaseOffset);
    // vtorDisp is always the 32-bits before the vbase in the class layout.
    VtorDispPtr = Builder.CreateConstGEP1_32(VtorDispPtr, -4);
    VtorDispPtr = Builder.CreateBitCast(
        VtorDispPtr, CGF.Int32Ty->getPointerTo(AS), "vtordisp.ptr");

    Builder.CreateAlignedStore(VtorDispValue, VtorDispPtr,
                               CharUnits::fromQuantity(4));
  }
}

static bool hasDefaultCXXMethodCC(ASTContext &Context,
                                  const CXXMethodDecl *MD) {
  CallingConv ExpectedCallingConv = Context.getDefaultCallingConvention(
      /*IsVariadic=*/false, /*IsCXXMethod=*/true);
  CallingConv ActualCallingConv =
      MD->getType()->getAs<FunctionProtoType>()->getCallConv();
  return ExpectedCallingConv == ActualCallingConv;
}

void MicrosoftCXXABI::EmitCXXConstructors(const CXXConstructorDecl *D) {
  // There's only one constructor type in this ABI.
  CGM.EmitGlobal(GlobalDecl(D, Ctor_Complete));

  // Exported default constructors either have a simple call-site where they use
  // the typical calling convention and have a single 'this' pointer for an
  // argument -or- they get a wrapper function which appropriately thunks to the
  // real default constructor.  This thunk is the default constructor closure.
  if (D->hasAttr<DLLExportAttr>() && D->isDefaultConstructor())
    if (!hasDefaultCXXMethodCC(getContext(), D) || D->getNumParams() != 0) {
      llvm::Function *Fn = getAddrOfCXXCtorClosure(D, Ctor_DefaultClosure);
      Fn->setLinkage(llvm::GlobalValue::WeakODRLinkage);
      Fn->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);
    }
}

void MicrosoftCXXABI::EmitVBPtrStores(CodeGenFunction &CGF,
                                      const CXXRecordDecl *RD) {
  Address This = getThisAddress(CGF);
  This = CGF.Builder.CreateElementBitCast(This, CGM.Int8Ty, "this.int8");
  const ASTContext &Context = getContext();
  const ASTRecordLayout &Layout = Context.getASTRecordLayout(RD);

  const VBTableGlobals &VBGlobals = enumerateVBTables(RD);
  for (unsigned I = 0, E = VBGlobals.VBTables->size(); I != E; ++I) {
    const std::unique_ptr<VPtrInfo> &VBT = (*VBGlobals.VBTables)[I];
    llvm::GlobalVariable *GV = VBGlobals.Globals[I];
    const ASTRecordLayout &SubobjectLayout =
        Context.getASTRecordLayout(VBT->IntroducingObject);
    CharUnits Offs = VBT->NonVirtualOffset;
    Offs += SubobjectLayout.getVBPtrOffset();
    if (VBT->getVBaseWithVPtr())
      Offs += Layout.getVBaseClassOffset(VBT->getVBaseWithVPtr());
    Address VBPtr = CGF.Builder.CreateConstInBoundsByteGEP(This, Offs);
    llvm::Value *GVPtr =
        CGF.Builder.CreateConstInBoundsGEP2_32(GV->getValueType(), GV, 0, 0);
    VBPtr = CGF.Builder.CreateElementBitCast(VBPtr, GVPtr->getType(),
                                      "vbptr." + VBT->ObjectWithVPtr->getName());
    CGF.Builder.CreateStore(GVPtr, VBPtr);
  }
}

CGCXXABI::AddedStructorArgs
MicrosoftCXXABI::buildStructorSignature(const CXXMethodDecl *MD, StructorType T,
                                        SmallVectorImpl<CanQualType> &ArgTys) {
  AddedStructorArgs Added;
  // TODO: 'for base' flag
  if (T == StructorType::Deleting) {
    // The scalar deleting destructor takes an implicit int parameter.
    ArgTys.push_back(getContext().IntTy);
    ++Added.Suffix;
  }
  auto *CD = dyn_cast<CXXConstructorDecl>(MD);
  if (!CD)
    return Added;

  // All parameters are already in place except is_most_derived, which goes
  // after 'this' if it's variadic and last if it's not.

  const CXXRecordDecl *Class = CD->getParent();
  const FunctionProtoType *FPT = CD->getType()->castAs<FunctionProtoType>();
  if (Class->getNumVBases()) {
    if (FPT->isVariadic()) {
      ArgTys.insert(ArgTys.begin() + 1, getContext().IntTy);
      ++Added.Prefix;
    } else {
      ArgTys.push_back(getContext().IntTy);
      ++Added.Suffix;
    }
  }

  return Added;
}

void MicrosoftCXXABI::EmitCXXDestructors(const CXXDestructorDecl *D) {
  // The TU defining a dtor is only guaranteed to emit a base destructor.  All
  // other destructor variants are delegating thunks.
  CGM.EmitGlobal(GlobalDecl(D, Dtor_Base));
}

CharUnits
MicrosoftCXXABI::getVirtualFunctionPrologueThisAdjustment(GlobalDecl GD) {
  GD = GD.getCanonicalDecl();
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(GD.getDecl());

  GlobalDecl LookupGD = GD;
  if (const CXXDestructorDecl *DD = dyn_cast<CXXDestructorDecl>(MD)) {
    // Complete destructors take a pointer to the complete object as a
    // parameter, thus don't need this adjustment.
    if (GD.getDtorType() == Dtor_Complete)
      return CharUnits();

    // There's no Dtor_Base in vftable but it shares the this adjustment with
    // the deleting one, so look it up instead.
    LookupGD = GlobalDecl(DD, Dtor_Deleting);
  }

  MicrosoftVTableContext::MethodVFTableLocation ML =
      CGM.getMicrosoftVTableContext().getMethodVFTableLocation(LookupGD);
  CharUnits Adjustment = ML.VFPtrOffset;

  // Normal virtual instance methods need to adjust from the vfptr that first
  // defined the virtual method to the virtual base subobject, but destructors
  // do not.  The vector deleting destructor thunk applies this adjustment for
  // us if necessary.
  if (isa<CXXDestructorDecl>(MD))
    Adjustment = CharUnits::Zero();

  if (ML.VBase) {
    const ASTRecordLayout &DerivedLayout =
        getContext().getASTRecordLayout(MD->getParent());
    Adjustment += DerivedLayout.getVBaseClassOffset(ML.VBase);
  }

  return Adjustment;
}

Address MicrosoftCXXABI::adjustThisArgumentForVirtualFunctionCall(
    CodeGenFunction &CGF, GlobalDecl GD, Address This,
    bool VirtualCall) {
  if (!VirtualCall) {
    // If the call of a virtual function is not virtual, we just have to
    // compensate for the adjustment the virtual function does in its prologue.
    CharUnits Adjustment = getVirtualFunctionPrologueThisAdjustment(GD);
    if (Adjustment.isZero())
      return This;

    This = CGF.Builder.CreateElementBitCast(This, CGF.Int8Ty);
    assert(Adjustment.isPositive());
    return CGF.Builder.CreateConstByteGEP(This, Adjustment);
  }

  GD = GD.getCanonicalDecl();
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(GD.getDecl());

  GlobalDecl LookupGD = GD;
  if (const CXXDestructorDecl *DD = dyn_cast<CXXDestructorDecl>(MD)) {
    // Complete dtors take a pointer to the complete object,
    // thus don't need adjustment.
    if (GD.getDtorType() == Dtor_Complete)
      return This;

    // There's only Dtor_Deleting in vftable but it shares the this adjustment
    // with the base one, so look up the deleting one instead.
    LookupGD = GlobalDecl(DD, Dtor_Deleting);
  }
  MicrosoftVTableContext::MethodVFTableLocation ML =
      CGM.getMicrosoftVTableContext().getMethodVFTableLocation(LookupGD);

  CharUnits StaticOffset = ML.VFPtrOffset;

  // Base destructors expect 'this' to point to the beginning of the base
  // subobject, not the first vfptr that happens to contain the virtual dtor.
  // However, we still need to apply the virtual base adjustment.
  if (isa<CXXDestructorDecl>(MD) && GD.getDtorType() == Dtor_Base)
    StaticOffset = CharUnits::Zero();

  Address Result = This;
  if (ML.VBase) {
    Result = CGF.Builder.CreateElementBitCast(Result, CGF.Int8Ty);
    
    const CXXRecordDecl *Derived = MD->getParent();
    const CXXRecordDecl *VBase = ML.VBase;
    llvm::Value *VBaseOffset =
      GetVirtualBaseClassOffset(CGF, Result, Derived, VBase);
    llvm::Value *VBasePtr =
      CGF.Builder.CreateInBoundsGEP(Result.getPointer(), VBaseOffset);
    CharUnits VBaseAlign =
      CGF.CGM.getVBaseAlignment(Result.getAlignment(), Derived, VBase);
    Result = Address(VBasePtr, VBaseAlign);
  }
  if (!StaticOffset.isZero()) {
    assert(StaticOffset.isPositive());
    Result = CGF.Builder.CreateElementBitCast(Result, CGF.Int8Ty);
    if (ML.VBase) {
      // Non-virtual adjustment might result in a pointer outside the allocated
      // object, e.g. if the final overrider class is laid out after the virtual
      // base that declares a method in the most derived class.
      // FIXME: Update the code that emits this adjustment in thunks prologues.
      Result = CGF.Builder.CreateConstByteGEP(Result, StaticOffset);
    } else {
      Result = CGF.Builder.CreateConstInBoundsByteGEP(Result, StaticOffset);
    }
  }
  return Result;
}

void MicrosoftCXXABI::addImplicitStructorParams(CodeGenFunction &CGF,
                                                QualType &ResTy,
                                                FunctionArgList &Params) {
  ASTContext &Context = getContext();
  const CXXMethodDecl *MD = cast<CXXMethodDecl>(CGF.CurGD.getDecl());
  assert(isa<CXXConstructorDecl>(MD) || isa<CXXDestructorDecl>(MD));
  if (isa<CXXConstructorDecl>(MD) && MD->getParent()->getNumVBases()) {
    ImplicitParamDecl *IsMostDerived
      = ImplicitParamDecl::Create(Context, nullptr,
                                  CGF.CurGD.getDecl()->getLocation(),
                                  &Context.Idents.get("is_most_derived"),
                                  Context.IntTy);
    // The 'most_derived' parameter goes second if the ctor is variadic and last
    // if it's not.  Dtors can't be variadic.
    const FunctionProtoType *FPT = MD->getType()->castAs<FunctionProtoType>();
    if (FPT->isVariadic())
      Params.insert(Params.begin() + 1, IsMostDerived);
    else
      Params.push_back(IsMostDerived);
    getStructorImplicitParamDecl(CGF) = IsMostDerived;
  } else if (isDeletingDtor(CGF.CurGD)) {
    ImplicitParamDecl *ShouldDelete
      = ImplicitParamDecl::Create(Context, nullptr,
                                  CGF.CurGD.getDecl()->getLocation(),
                                  &Context.Idents.get("should_call_delete"),
                                  Context.IntTy);
    Params.push_back(ShouldDelete);
    getStructorImplicitParamDecl(CGF) = ShouldDelete;
  }
}

llvm::Value *MicrosoftCXXABI::adjustThisParameterInVirtualFunctionPrologue(
    CodeGenFunction &CGF, GlobalDecl GD, llvm::Value *This) {
  // In this ABI, every virtual function takes a pointer to one of the
  // subobjects that first defines it as the 'this' parameter, rather than a
  // pointer to the final overrider subobject. Thus, we need to adjust it back
  // to the final overrider subobject before use.
  // See comments in the MicrosoftVFTableContext implementation for the details.
  CharUnits Adjustment = getVirtualFunctionPrologueThisAdjustment(GD);
  if (Adjustment.isZero())
    return This;

  unsigned AS = cast<llvm::PointerType>(This->getType())->getAddressSpace();
  llvm::Type *charPtrTy = CGF.Int8Ty->getPointerTo(AS),
             *thisTy = This->getType();

  This = CGF.Builder.CreateBitCast(This, charPtrTy);
  assert(Adjustment.isPositive());
  This = CGF.Builder.CreateConstInBoundsGEP1_32(CGF.Int8Ty, This,
                                                -Adjustment.getQuantity());
  return CGF.Builder.CreateBitCast(This, thisTy);
}

void MicrosoftCXXABI::EmitInstanceFunctionProlog(CodeGenFunction &CGF) {
  // Naked functions have no prolog.
  if (CGF.CurFuncDecl && CGF.CurFuncDecl->hasAttr<NakedAttr>())
    return;

  EmitThisParam(CGF);

  /// If this is a function that the ABI specifies returns 'this', initialize
  /// the return slot to 'this' at the start of the function.
  ///
  /// Unlike the setting of return types, this is done within the ABI
  /// implementation instead of by clients of CGCXXABI because:
  /// 1) getThisValue is currently protected
  /// 2) in theory, an ABI could implement 'this' returns some other way;
  ///    HasThisReturn only specifies a contract, not the implementation    
  if (HasThisReturn(CGF.CurGD))
    CGF.Builder.CreateStore(getThisValue(CGF), CGF.ReturnValue);
  else if (hasMostDerivedReturn(CGF.CurGD))
    CGF.Builder.CreateStore(CGF.EmitCastToVoidPtr(getThisValue(CGF)),
                            CGF.ReturnValue);

  const CXXMethodDecl *MD = cast<CXXMethodDecl>(CGF.CurGD.getDecl());
  if (isa<CXXConstructorDecl>(MD) && MD->getParent()->getNumVBases()) {
    assert(getStructorImplicitParamDecl(CGF) &&
           "no implicit parameter for a constructor with virtual bases?");
    getStructorImplicitParamValue(CGF)
      = CGF.Builder.CreateLoad(
          CGF.GetAddrOfLocalVar(getStructorImplicitParamDecl(CGF)),
          "is_most_derived");
  }

  if (isDeletingDtor(CGF.CurGD)) {
    assert(getStructorImplicitParamDecl(CGF) &&
           "no implicit parameter for a deleting destructor?");
    getStructorImplicitParamValue(CGF)
      = CGF.Builder.CreateLoad(
          CGF.GetAddrOfLocalVar(getStructorImplicitParamDecl(CGF)),
          "should_call_delete");
  }
}

CGCXXABI::AddedStructorArgs MicrosoftCXXABI::addImplicitConstructorArgs(
    CodeGenFunction &CGF, const CXXConstructorDecl *D, CXXCtorType Type,
    bool ForVirtualBase, bool Delegating, CallArgList &Args) {
  assert(Type == Ctor_Complete || Type == Ctor_Base);

  // Check if we need a 'most_derived' parameter.
  if (!D->getParent()->getNumVBases())
    return AddedStructorArgs{};

  // Add the 'most_derived' argument second if we are variadic or last if not.
  const FunctionProtoType *FPT = D->getType()->castAs<FunctionProtoType>();
  llvm::Value *MostDerivedArg;
  if (Delegating) {
    MostDerivedArg = getStructorImplicitParamValue(CGF);
  } else {
    MostDerivedArg = llvm::ConstantInt::get(CGM.Int32Ty, Type == Ctor_Complete);
  }
  RValue RV = RValue::get(MostDerivedArg);
  if (FPT->isVariadic()) {
    Args.insert(Args.begin() + 1,
                CallArg(RV, getContext().IntTy, /*needscopy=*/false));
    return AddedStructorArgs::prefix(1);
  }
  Args.add(RV, getContext().IntTy);
  return AddedStructorArgs::suffix(1);
}

void MicrosoftCXXABI::EmitDestructorCall(CodeGenFunction &CGF,
                                         const CXXDestructorDecl *DD,
                                         CXXDtorType Type, bool ForVirtualBase,
                                         bool Delegating, Address This) {
  CGCallee Callee = CGCallee::forDirect(
                          CGM.getAddrOfCXXStructor(DD, getFromDtorType(Type)),
                                        DD);

  if (DD->isVirtual()) {
    assert(Type != CXXDtorType::Dtor_Deleting &&
           "The deleting destructor should only be called via a virtual call");
    This = adjustThisArgumentForVirtualFunctionCall(CGF, GlobalDecl(DD, Type),
                                                    This, false);
  }
  
  llvm::BasicBlock *BaseDtorEndBB = nullptr;
  if (ForVirtualBase && isa<CXXConstructorDecl>(CGF.CurCodeDecl)) {
    BaseDtorEndBB = EmitDtorCompleteObjectHandler(CGF);
  }  

  CGF.EmitCXXDestructorCall(DD, Callee, This.getPointer(),
                            /*ImplicitParam=*/nullptr,
                            /*ImplicitParamTy=*/QualType(), nullptr,
                            getFromDtorType(Type));
  if (BaseDtorEndBB) {
    // Complete object handler should continue to be the remaining 
    CGF.Builder.CreateBr(BaseDtorEndBB);
    CGF.EmitBlock(BaseDtorEndBB);
  } 
}

void MicrosoftCXXABI::emitVTableTypeMetadata(const VPtrInfo &Info,
                                             const CXXRecordDecl *RD,
                                             llvm::GlobalVariable *VTable) {
  if (!CGM.getCodeGenOpts().LTOUnit)
    return;

  // The location of the first virtual function pointer in the virtual table,
  // aka the "address point" on Itanium. This is at offset 0 if RTTI is
  // disabled, or sizeof(void*) if RTTI is enabled.
  CharUnits AddressPoint =
      getContext().getLangOpts().RTTIData
          ? getContext().toCharUnitsFromBits(
                getContext().getTargetInfo().getPointerWidth(0))
          : CharUnits::Zero();

  if (Info.PathToIntroducingObject.empty()) {
    CGM.AddVTableTypeMetadata(VTable, AddressPoint, RD);
    return;
  }

  // Add a bitset entry for the least derived base belonging to this vftable.
  CGM.AddVTableTypeMetadata(VTable, AddressPoint,
                            Info.PathToIntroducingObject.back());

  // Add a bitset entry for each derived class that is laid out at the same
  // offset as the least derived base.
  for (unsigned I = Info.PathToIntroducingObject.size() - 1; I != 0; --I) {
    const CXXRecordDecl *DerivedRD = Info.PathToIntroducingObject[I - 1];
    const CXXRecordDecl *BaseRD = Info.PathToIntroducingObject[I];

    const ASTRecordLayout &Layout =
        getContext().getASTRecordLayout(DerivedRD);
    CharUnits Offset;
    auto VBI = Layout.getVBaseOffsetsMap().find(BaseRD);
    if (VBI == Layout.getVBaseOffsetsMap().end())
      Offset = Layout.getBaseClassOffset(BaseRD);
    else
      Offset = VBI->second.VBaseOffset;
    if (!Offset.isZero())
      return;
    CGM.AddVTableTypeMetadata(VTable, AddressPoint, DerivedRD);
  }

  // Finally do the same for the most derived class.
  if (Info.FullOffsetInMDC.isZero())
    CGM.AddVTableTypeMetadata(VTable, AddressPoint, RD);
}

void MicrosoftCXXABI::emitVTableDefinitions(CodeGenVTables &CGVT,
                                            const CXXRecordDecl *RD) {
  MicrosoftVTableContext &VFTContext = CGM.getMicrosoftVTableContext();
  const VPtrInfoVector &VFPtrs = VFTContext.getVFPtrOffsets(RD);

  for (const std::unique_ptr<VPtrInfo>& Info : VFPtrs) {
    llvm::GlobalVariable *VTable = getAddrOfVTable(RD, Info->FullOffsetInMDC);
    if (VTable->hasInitializer())
      continue;

    const VTableLayout &VTLayout =
      VFTContext.getVFTableLayout(RD, Info->FullOffsetInMDC);

    llvm::Constant *RTTI = nullptr;
    if (any_of(VTLayout.vtable_components(),
               [](const VTableComponent &VTC) { return VTC.isRTTIKind(); }))
      RTTI = getMSCompleteObjectLocator(RD, *Info);

    ConstantInitBuilder Builder(CGM);
    auto Components = Builder.beginStruct();
    CGVT.createVTableInitializer(Components, VTLayout, RTTI);
    Components.finishAndSetAsInitializer(VTable);

    emitVTableTypeMetadata(*Info, RD, VTable);
  }
}

bool MicrosoftCXXABI::isVirtualOffsetNeededForVTableField(
    CodeGenFunction &CGF, CodeGenFunction::VPtr Vptr) {
  return Vptr.NearestVBase != nullptr;
}

llvm::Value *MicrosoftCXXABI::getVTableAddressPointInStructor(
    CodeGenFunction &CGF, const CXXRecordDecl *VTableClass, BaseSubobject Base,
    const CXXRecordDecl *NearestVBase) {
  llvm::Constant *VTableAddressPoint = getVTableAddressPoint(Base, VTableClass);
  if (!VTableAddressPoint) {
    assert(Base.getBase()->getNumVBases() &&
           !getContext().getASTRecordLayout(Base.getBase()).hasOwnVFPtr());
  }
  return VTableAddressPoint;
}

static void mangleVFTableName(MicrosoftMangleContext &MangleContext,
                              const CXXRecordDecl *RD, const VPtrInfo &VFPtr,
                              SmallString<256> &Name) {
  llvm::raw_svector_ostream Out(Name);
  MangleContext.mangleCXXVFTable(RD, VFPtr.MangledPath, Out);
}

llvm::Constant *
MicrosoftCXXABI::getVTableAddressPoint(BaseSubobject Base,
                                       const CXXRecordDecl *VTableClass) {
  (void)getAddrOfVTable(VTableClass, Base.getBaseOffset());
  VFTableIdTy ID(VTableClass, Base.getBaseOffset());
  return VFTablesMap[ID];
}

llvm::Constant *MicrosoftCXXABI::getVTableAddressPointForConstExpr(
    BaseSubobject Base, const CXXRecordDecl *VTableClass) {
  llvm::Constant *VFTable = getVTableAddressPoint(Base, VTableClass);
  assert(VFTable && "Couldn't find a vftable for the given base?");
  return VFTable;
}

llvm::GlobalVariable *MicrosoftCXXABI::getAddrOfVTable(const CXXRecordDecl *RD,
                                                       CharUnits VPtrOffset) {
  // getAddrOfVTable may return 0 if asked to get an address of a vtable which
  // shouldn't be used in the given record type. We want to cache this result in
  // VFTablesMap, thus a simple zero check is not sufficient.

  VFTableIdTy ID(RD, VPtrOffset);
  VTablesMapTy::iterator I;
  bool Inserted;
  std::tie(I, Inserted) = VTablesMap.insert(std::make_pair(ID, nullptr));
  if (!Inserted)
    return I->second;

  llvm::GlobalVariable *&VTable = I->second;

  MicrosoftVTableContext &VTContext = CGM.getMicrosoftVTableContext();
  const VPtrInfoVector &VFPtrs = VTContext.getVFPtrOffsets(RD);

  if (DeferredVFTables.insert(RD).second) {
    // We haven't processed this record type before.
    // Queue up this vtable for possible deferred emission.
    CGM.addDeferredVTable(RD);

#ifndef NDEBUG
    // Create all the vftables at once in order to make sure each vftable has
    // a unique mangled name.
    llvm::StringSet<> ObservedMangledNames;
    for (size_t J = 0, F = VFPtrs.size(); J != F; ++J) {
      SmallString<256> Name;
      mangleVFTableName(getMangleContext(), RD, *VFPtrs[J], Name);
      if (!ObservedMangledNames.insert(Name.str()).second)
        llvm_unreachable("Already saw this mangling before?");
    }
#endif
  }

  const std::unique_ptr<VPtrInfo> *VFPtrI = std::find_if(
      VFPtrs.begin(), VFPtrs.end(), [&](const std::unique_ptr<VPtrInfo>& VPI) {
        return VPI->FullOffsetInMDC == VPtrOffset;
      });
  if (VFPtrI == VFPtrs.end()) {
    VFTablesMap[ID] = nullptr;
    return nullptr;
  }
  const std::unique_ptr<VPtrInfo> &VFPtr = *VFPtrI;

  SmallString<256> VFTableName;
  mangleVFTableName(getMangleContext(), RD, *VFPtr, VFTableName);

  // Classes marked __declspec(dllimport) need vftables generated on the
  // import-side in order to support features like constexpr.  No other
  // translation unit relies on the emission of the local vftable, translation
  // units are expected to generate them as needed.
  //
  // Because of this unique behavior, we maintain this logic here instead of
  // getVTableLinkage.
  llvm::GlobalValue::LinkageTypes VFTableLinkage =
      RD->hasAttr<DLLImportAttr>() ? llvm::GlobalValue::LinkOnceODRLinkage
                                   : CGM.getVTableLinkage(RD);
  bool VFTableComesFromAnotherTU =
      llvm::GlobalValue::isAvailableExternallyLinkage(VFTableLinkage) ||
      llvm::GlobalValue::isExternalLinkage(VFTableLinkage);
  bool VTableAliasIsRequred =
      !VFTableComesFromAnotherTU && getContext().getLangOpts().RTTIData;

  if (llvm::GlobalValue *VFTable =
          CGM.getModule().getNamedGlobal(VFTableName)) {
    VFTablesMap[ID] = VFTable;
    VTable = VTableAliasIsRequred
                 ? cast<llvm::GlobalVariable>(
                       cast<llvm::GlobalAlias>(VFTable)->getBaseObject())
                 : cast<llvm::GlobalVariable>(VFTable);
    return VTable;
  }

  const VTableLayout &VTLayout =
      VTContext.getVFTableLayout(RD, VFPtr->FullOffsetInMDC);
  llvm::GlobalValue::LinkageTypes VTableLinkage =
      VTableAliasIsRequred ? llvm::GlobalValue::PrivateLinkage : VFTableLinkage;

  StringRef VTableName = VTableAliasIsRequred ? StringRef() : VFTableName.str();

  llvm::Type *VTableType = CGM.getVTables().getVTableType(VTLayout);

  // Create a backing variable for the contents of VTable.  The VTable may
  // or may not include space for a pointer to RTTI data.
  llvm::GlobalValue *VFTable;
  VTable = new llvm::GlobalVariable(CGM.getModule(), VTableType,
                                    /*isConstant=*/true, VTableLinkage,
                                    /*Initializer=*/nullptr, VTableName);
  VTable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  llvm::Comdat *C = nullptr;
  if (!VFTableComesFromAnotherTU &&
      (llvm::GlobalValue::isWeakForLinker(VFTableLinkage) ||
       (llvm::GlobalValue::isLocalLinkage(VFTableLinkage) &&
        VTableAliasIsRequred)))
    C = CGM.getModule().getOrInsertComdat(VFTableName.str());

  // Only insert a pointer into the VFTable for RTTI data if we are not
  // importing it.  We never reference the RTTI data directly so there is no
  // need to make room for it.
  if (VTableAliasIsRequred) {
    llvm::Value *GEPIndices[] = {llvm::ConstantInt::get(CGM.Int32Ty, 0),
                                 llvm::ConstantInt::get(CGM.Int32Ty, 0),
                                 llvm::ConstantInt::get(CGM.Int32Ty, 1)};
    // Create a GEP which points just after the first entry in the VFTable,
    // this should be the location of the first virtual method.
    llvm::Constant *VTableGEP = llvm::ConstantExpr::getInBoundsGetElementPtr(
        VTable->getValueType(), VTable, GEPIndices);
    if (llvm::GlobalValue::isWeakForLinker(VFTableLinkage)) {
      VFTableLinkage = llvm::GlobalValue::ExternalLinkage;
      if (C)
        C->setSelectionKind(llvm::Comdat::Largest);
    }
    VFTable = llvm::GlobalAlias::create(CGM.Int8PtrTy,
                                        /*AddressSpace=*/0, VFTableLinkage,
                                        VFTableName.str(), VTableGEP,
                                        &CGM.getModule());
    VFTable->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  } else {
    // We don't need a GlobalAlias to be a symbol for the VTable if we won't
    // be referencing any RTTI data.
    // The GlobalVariable will end up being an appropriate definition of the
    // VFTable.
    VFTable = VTable;
  }
  if (C)
    VTable->setComdat(C);

  if (RD->hasAttr<DLLExportAttr>())
    VFTable->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);

  VFTablesMap[ID] = VFTable;
  return VTable;
}

CGCallee MicrosoftCXXABI::getVirtualFunctionPointer(CodeGenFunction &CGF,
                                                    GlobalDecl GD,
                                                    Address This,
                                                    llvm::Type *Ty,
                                                    SourceLocation Loc) {
  GD = GD.getCanonicalDecl();
  CGBuilderTy &Builder = CGF.Builder;

  Ty = Ty->getPointerTo()->getPointerTo();
  Address VPtr =
      adjustThisArgumentForVirtualFunctionCall(CGF, GD, This, true);

  auto *MethodDecl = cast<CXXMethodDecl>(GD.getDecl());
  llvm::Value *VTable = CGF.GetVTablePtr(VPtr, Ty, MethodDecl->getParent());

  MicrosoftVTableContext &VFTContext = CGM.getMicrosoftVTableContext();
  MicrosoftVTableContext::MethodVFTableLocation ML =
      VFTContext.getMethodVFTableLocation(GD);

  // Compute the identity of the most derived class whose virtual table is
  // located at the MethodVFTableLocation ML.
  auto getObjectWithVPtr = [&] {
    return llvm::find_if(VFTContext.getVFPtrOffsets(
                             ML.VBase ? ML.VBase : MethodDecl->getParent()),
                         [&](const std::unique_ptr<VPtrInfo> &Info) {
                           return Info->FullOffsetInMDC == ML.VFPtrOffset;
                         })
        ->get()
        ->ObjectWithVPtr;
  };

  llvm::Value *VFunc;
  if (CGF.ShouldEmitVTableTypeCheckedLoad(MethodDecl->getParent())) {
    VFunc = CGF.EmitVTableTypeCheckedLoad(
        getObjectWithVPtr(), VTable,
        ML.Index * CGM.getContext().getTargetInfo().getPointerWidth(0) / 8);
  } else {
    if (CGM.getCodeGenOpts().PrepareForLTO)
      CGF.EmitTypeMetadataCodeForVCall(getObjectWithVPtr(), VTable, Loc);

    llvm::Value *VFuncPtr =
        Builder.CreateConstInBoundsGEP1_64(VTable, ML.Index, "vfn");
    VFunc = Builder.CreateAlignedLoad(VFuncPtr, CGF.getPointerAlign());
  }

  CGCallee Callee(MethodDecl, VFunc);
  return Callee;
}

llvm::Value *MicrosoftCXXABI::EmitVirtualDestructorCall(
    CodeGenFunction &CGF, const CXXDestructorDecl *Dtor, CXXDtorType DtorType,
    Address This, const CXXMemberCallExpr *CE) {
  assert(CE == nullptr || CE->arg_begin() == CE->arg_end());
  assert(DtorType == Dtor_Deleting || DtorType == Dtor_Complete);

  // We have only one destructor in the vftable but can get both behaviors
  // by passing an implicit int parameter.
  GlobalDecl GD(Dtor, Dtor_Deleting);
  const CGFunctionInfo *FInfo = &CGM.getTypes().arrangeCXXStructorDeclaration(
      Dtor, StructorType::Deleting);
  llvm::Type *Ty = CGF.CGM.getTypes().GetFunctionType(*FInfo);
  CGCallee Callee = getVirtualFunctionPointer(
      CGF, GD, This, Ty, CE ? CE->getLocStart() : SourceLocation());

  ASTContext &Context = getContext();
  llvm::Value *ImplicitParam = llvm::ConstantInt::get(
      llvm::IntegerType::getInt32Ty(CGF.getLLVMContext()),
      DtorType == Dtor_Deleting);

  This = adjustThisArgumentForVirtualFunctionCall(CGF, GD, This, true);
  RValue RV =
      CGF.EmitCXXDestructorCall(Dtor, Callee, This.getPointer(), ImplicitParam,
                                Context.IntTy, CE, StructorType::Deleting);
  return RV.getScalarVal();
}

const VBTableGlobals &
MicrosoftCXXABI::enumerateVBTables(const CXXRecordDecl *RD) {
  // At this layer, we can key the cache off of a single class, which is much
  // easier than caching each vbtable individually.
  llvm::DenseMap<const CXXRecordDecl*, VBTableGlobals>::iterator Entry;
  bool Added;
  std::tie(Entry, Added) =
      VBTablesMap.insert(std::make_pair(RD, VBTableGlobals()));
  VBTableGlobals &VBGlobals = Entry->second;
  if (!Added)
    return VBGlobals;

  MicrosoftVTableContext &Context = CGM.getMicrosoftVTableContext();
  VBGlobals.VBTables = &Context.enumerateVBTables(RD);

  // Cache the globals for all vbtables so we don't have to recompute the
  // mangled names.
  llvm::GlobalVariable::LinkageTypes Linkage = CGM.getVTableLinkage(RD);
  for (VPtrInfoVector::const_iterator I = VBGlobals.VBTables->begin(),
                                      E = VBGlobals.VBTables->end();
       I != E; ++I) {
    VBGlobals.Globals.push_back(getAddrOfVBTable(**I, RD, Linkage));
  }

  return VBGlobals;
}

llvm::Function *MicrosoftCXXABI::EmitVirtualMemPtrThunk(
    const CXXMethodDecl *MD,
    const MicrosoftVTableContext::MethodVFTableLocation &ML) {
  assert(!isa<CXXConstructorDecl>(MD) && !isa<CXXDestructorDecl>(MD) &&
         "can't form pointers to ctors or virtual dtors");

  // Calculate the mangled name.
  SmallString<256> ThunkName;
  llvm::raw_svector_ostream Out(ThunkName);
  getMangleContext().mangleVirtualMemPtrThunk(MD, Out);

  // If the thunk has been generated previously, just return it.
  if (llvm::GlobalValue *GV = CGM.getModule().getNamedValue(ThunkName))
    return cast<llvm::Function>(GV);

  // Create the llvm::Function.
  const CGFunctionInfo &FnInfo = CGM.getTypes().arrangeMSMemberPointerThunk(MD);
  llvm::FunctionType *ThunkTy = CGM.getTypes().GetFunctionType(FnInfo);
  llvm::Function *ThunkFn =
      llvm::Function::Create(ThunkTy, llvm::Function::ExternalLinkage,
                             ThunkName.str(), &CGM.getModule());
  assert(ThunkFn->getName() == ThunkName && "name was uniqued!");

  ThunkFn->setLinkage(MD->isExternallyVisible()
                          ? llvm::GlobalValue::LinkOnceODRLinkage
                          : llvm::GlobalValue::InternalLinkage);
  if (MD->isExternallyVisible())
    ThunkFn->setComdat(CGM.getModule().getOrInsertComdat(ThunkFn->getName()));

  CGM.SetLLVMFunctionAttributes(MD, FnInfo, ThunkFn);
  CGM.SetLLVMFunctionAttributesForDefinition(MD, ThunkFn);

  // Add the "thunk" attribute so that LLVM knows that the return type is
  // meaningless. These thunks can be used to call functions with differing
  // return types, and the caller is required to cast the prototype
  // appropriately to extract the correct value.
  ThunkFn->addFnAttr("thunk");

  // These thunks can be compared, so they are not unnamed.
  ThunkFn->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::None);

  // Start codegen.
  CodeGenFunction CGF(CGM);
  CGF.CurGD = GlobalDecl(MD);
  CGF.CurFuncIsThunk = true;

  // Build FunctionArgs, but only include the implicit 'this' parameter
  // declaration.
  FunctionArgList FunctionArgs;
  buildThisParam(CGF, FunctionArgs);

  // Start defining the function.
  CGF.StartFunction(GlobalDecl(), FnInfo.getReturnType(), ThunkFn, FnInfo,
                    FunctionArgs, MD->getLocation(), SourceLocation());
  EmitThisParam(CGF);

  // Load the vfptr and then callee from the vftable.  The callee should have
  // adjusted 'this' so that the vfptr is at offset zero.
  llvm::Value *VTable = CGF.GetVTablePtr(
      getThisAddress(CGF), ThunkTy->getPointerTo()->getPointerTo(), MD->getParent());

  llvm::Value *VFuncPtr =
      CGF.Builder.CreateConstInBoundsGEP1_64(VTable, ML.Index, "vfn");
  llvm::Value *Callee =
    CGF.Builder.CreateAlignedLoad(VFuncPtr, CGF.getPointerAlign());

  CGF.EmitMustTailThunk(MD, getThisValue(CGF), Callee);

  return ThunkFn;
}

void MicrosoftCXXABI::emitVirtualInheritanceTables(const CXXRecordDecl *RD) {
  const VBTableGlobals &VBGlobals = enumerateVBTables(RD);
  for (unsigned I = 0, E = VBGlobals.VBTables->size(); I != E; ++I) {
    const std::unique_ptr<VPtrInfo>& VBT = (*VBGlobals.VBTables)[I];
    llvm::GlobalVariable *GV = VBGlobals.Globals[I];
    if (GV->isDeclaration())
      emitVBTableDefinition(*VBT, RD, GV);
  }
}

llvm::GlobalVariable *
MicrosoftCXXABI::getAddrOfVBTable(const VPtrInfo &VBT, const CXXRecordDecl *RD,
                                  llvm::GlobalVariable::LinkageTypes Linkage) {
  SmallString<256> OutName;
  llvm::raw_svector_ostream Out(OutName);
  getMangleContext().mangleCXXVBTable(RD, VBT.MangledPath, Out);
  StringRef Name = OutName.str();

  llvm::ArrayType *VBTableType =
      llvm::ArrayType::get(CGM.IntTy, 1 + VBT.ObjectWithVPtr->getNumVBases());

  assert(!CGM.getModule().getNamedGlobal(Name) &&
         "vbtable with this name already exists: mangling bug?");
  llvm::GlobalVariable *GV =
      CGM.CreateOrReplaceCXXRuntimeVariable(Name, VBTableType, Linkage);
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);

  if (RD->hasAttr<DLLImportAttr>())
    GV->setDLLStorageClass(llvm::GlobalValue::DLLImportStorageClass);
  else if (RD->hasAttr<DLLExportAttr>())
    GV->setDLLStorageClass(llvm::GlobalValue::DLLExportStorageClass);

  if (!GV->hasExternalLinkage())
    emitVBTableDefinition(VBT, RD, GV);

  return GV;
}

void MicrosoftCXXABI::emitVBTableDefinition(const VPtrInfo &VBT,
                                            const CXXRecordDecl *RD,
                                            llvm::GlobalVariable *GV) const {
  const CXXRecordDecl *ObjectWithVPtr = VBT.ObjectWithVPtr;

  assert(RD->getNumVBases() && ObjectWithVPtr->getNumVBases() &&
         "should only emit vbtables for classes with vbtables");

  const ASTRecordLayout &BaseLayout =
      getContext().getASTRecordLayout(VBT.IntroducingObject);
  const ASTRecordLayout &DerivedLayout = getContext().getASTRecordLayout(RD);

  SmallVector<llvm::Constant *, 4> Offsets(1 + ObjectWithVPtr->getNumVBases(),
                                           nullptr);

  // The offset from ObjectWithVPtr's vbptr to itself always leads.
  CharUnits VBPtrOffset = BaseLayout.getVBPtrOffset();
  Offsets[0] = llvm::ConstantInt::get(CGM.IntTy, -VBPtrOffset.getQuantity());

  MicrosoftVTableContext &Context = CGM.getMicrosoftVTableContext();
  for (const auto &I : ObjectWithVPtr->vbases()) {
    const CXXRecordDecl *VBase = I.getType()->getAsCXXRecordDecl();
    CharUnits Offset = DerivedLayout.getVBaseClassOffset(VBase);
    assert(!Offset.isNegative());

    // Make it relative to the subobject vbptr.
    CharUnits CompleteVBPtrOffset = VBT.NonVirtualOffset + VBPtrOffset;
    if (VBT.getVBaseWithVPtr())
      CompleteVBPtrOffset +=
          DerivedLayout.getVBaseClassOffset(VBT.getVBaseWithVPtr());
    Offset -= CompleteVBPtrOffset;

    unsigned VBIndex = Context.getVBTableIndex(ObjectWithVPtr, VBase);
    assert(Offsets[VBIndex] == nullptr && "The same vbindex seen twice?");
    Offsets[VBIndex] = llvm::ConstantInt::get(CGM.IntTy, Offset.getQuantity());
  }

  assert(Offsets.size() ==
         cast<llvm::ArrayType>(cast<llvm::PointerType>(GV->getType())
                               ->getElementType())->getNumElements());
  llvm::ArrayType *VBTableType =
    llvm::ArrayType::get(CGM.IntTy, Offsets.size());
  llvm::Constant *Init = llvm::ConstantArray::get(VBTableType, Offsets);
  GV->setInitializer(Init);

  if (RD->hasAttr<DLLImportAttr>())
    GV->setLinkage(llvm::GlobalVariable::AvailableExternallyLinkage);
}

llvm::Value *MicrosoftCXXABI::performThisAdjustment(CodeGenFunction &CGF,
                                                    Address This,
                                                    const ThisAdjustment &TA) {
  if (TA.isEmpty())
    return This.getPointer();

  This = CGF.Builder.CreateElementBitCast(This, CGF.Int8Ty);

  llvm::Value *V;
  if (TA.Virtual.isEmpty()) {
    V = This.getPointer();
  } else {
    assert(TA.Virtual.Microsoft.VtordispOffset < 0);
    // Adjust the this argument based on the vtordisp value.
    Address VtorDispPtr =
        CGF.Builder.CreateConstInBoundsByteGEP(This,
                 CharUnits::fromQuantity(TA.Virtual.Microsoft.VtordispOffset));
    VtorDispPtr = CGF.Builder.CreateElementBitCast(VtorDispPtr, CGF.Int32Ty);
    llvm::Value *VtorDisp = CGF.Builder.CreateLoad(VtorDispPtr, "vtordisp");
    V = CGF.Builder.CreateGEP(This.getPointer(),
                              CGF.Builder.CreateNeg(VtorDisp));

    // Unfortunately, having applied the vtordisp means that we no
    // longer really have a known alignment for the vbptr step.
    // We'll assume the vbptr is pointer-aligned.

    if (TA.Virtual.Microsoft.VBPtrOffset) {
      // If the final overrider is defined in a virtual base other than the one
      // that holds the vfptr, we have to use a vtordispex thunk which looks up
      // the vbtable of the derived class.
      assert(TA.Virtual.Microsoft.VBPtrOffset > 0);
      assert(TA.Virtual.Microsoft.VBOffsetOffset >= 0);
      llvm::Value *VBPtr;
      llvm::Value *VBaseOffset =
          GetVBaseOffsetFromVBPtr(CGF, Address(V, CGF.getPointerAlign()),
                                  -TA.Virtual.Microsoft.VBPtrOffset,
                                  TA.Virtual.Microsoft.VBOffsetOffset, &VBPtr);
      V = CGF.Builder.CreateInBoundsGEP(VBPtr, VBaseOffset);
    }
  }

  if (TA.NonVirtual) {
    // Non-virtual adjustment might result in a pointer outside the allocated
    // object, e.g. if the final overrider class is laid out after the virtual
    // base that declares a method in the most derived class.
    V = CGF.Builder.CreateConstGEP1_32(V, TA.NonVirtual);
  }

  // Don't need to bitcast back, the call CodeGen will handle this.
  return V;
}

llvm::Value *
MicrosoftCXXABI::performReturnAdjustment(CodeGenFunction &CGF, Address Ret,
                                         const ReturnAdjustment &RA) {
  if (RA.isEmpty())
    return Ret.getPointer();

  auto OrigTy = Ret.getType();
  Ret = CGF.Builder.CreateElementBitCast(Ret, CGF.Int8Ty);

  llvm::Value *V = Ret.getPointer();
  if (RA.Virtual.Microsoft.VBIndex) {
    assert(RA.Virtual.Microsoft.VBIndex > 0);
    int32_t IntSize = CGF.getIntSize().getQuantity();
    llvm::Value *VBPtr;
    llvm::Value *VBaseOffset =
        GetVBaseOffsetFromVBPtr(CGF, Ret, RA.Virtual.Microsoft.VBPtrOffset,
                                IntSize * RA.Virtual.Microsoft.VBIndex, &VBPtr);
    V = CGF.Builder.CreateInBoundsGEP(VBPtr, VBaseOffset);
  }

  if (RA.NonVirtual)
    V = CGF.Builder.CreateConstInBoundsGEP1_32(CGF.Int8Ty, V, RA.NonVirtual);

  // Cast back to the original type.
  return CGF.Builder.CreateBitCast(V, OrigTy);
}

bool MicrosoftCXXABI::requiresArrayCookie(const CXXDeleteExpr *expr,
                                   QualType elementType) {
  // Microsoft seems to completely ignore the possibility of a
  // two-argument usual deallocation function.
  return elementType.isDestructedType();
}

bool MicrosoftCXXABI::requiresArrayCookie(const CXXNewExpr *expr) {
  // Microsoft seems to completely ignore the possibility of a
  // two-argument usual deallocation function.
  return expr->getAllocatedType().isDestructedType();
}

CharUnits MicrosoftCXXABI::getArrayCookieSizeImpl(QualType type) {
  // The array cookie is always a size_t; we then pad that out to the
  // alignment of the element type.
  ASTContext &Ctx = getContext();
  return std::max(Ctx.getTypeSizeInChars(Ctx.getSizeType()),
                  Ctx.getTypeAlignInChars(type));
}

llvm::Value *MicrosoftCXXABI::readArrayCookieImpl(CodeGenFunction &CGF,
                                                  Address allocPtr,
                                                  CharUnits cookieSize) {
  Address numElementsPtr =
    CGF.Builder.CreateElementBitCast(allocPtr, CGF.SizeTy);
  return CGF.Builder.CreateLoad(numElementsPtr);
}

Address MicrosoftCXXABI::InitializeArrayCookie(CodeGenFunction &CGF,
                                               Address newPtr,
                                               llvm::Value *numElements,
                                               const CXXNewExpr *expr,
                                               QualType elementType) {
  assert(requiresArrayCookie(expr));

  // The size of the cookie.
  CharUnits cookieSize = getArrayCookieSizeImpl(elementType);

  // Compute an offset to the cookie.
  Address cookiePtr = newPtr;

  // Write the number of elements into the appropriate slot.
  Address numElementsPtr
    = CGF.Builder.CreateElementBitCast(cookiePtr, CGF.SizeTy);
  CGF.Builder.CreateStore(numElements, numElementsPtr);

  // Finally, compute a pointer to the actual data buffer by skipping
  // over the cookie completely.
  return CGF.Builder.CreateConstInBoundsByteGEP(newPtr, cookieSize);
}

static void emitGlobalDtorWithTLRegDtor(CodeGenFunction &CGF, const VarDecl &VD,
                                        llvm::Constant *Dtor,
                                        llvm::Constant *Addr) {
  // Create a function which calls the destructor.
  llvm::Constant *DtorStub = CGF.createAtExitStub(VD, Dtor, Addr);

  // extern "C" int __tlregdtor(void (*f)(void));
  llvm::FunctionType *TLRegDtorTy = llvm::FunctionType::get(
      CGF.IntTy, DtorStub->getType(), /*IsVarArg=*/false);

  llvm::Constant *TLRegDtor = CGF.CGM.CreateRuntimeFunction(
      TLRegDtorTy, "__tlregdtor", llvm::AttributeList(), /*Local=*/true);
  if (llvm::Function *TLRegDtorFn = dyn_cast<llvm::Function>(TLRegDtor))
    TLRegDtorFn->setDoesNotThrow();

  CGF.EmitNounwindRuntimeCall(TLRegDtor, DtorStub);
}

void MicrosoftCXXABI::registerGlobalDtor(CodeGenFunction &CGF, const VarDecl &D,
                                         llvm::Constant *Dtor,
                                         llvm::Constant *Addr) {
  if (D.getTLSKind())
    return emitGlobalDtorWithTLRegDtor(CGF, D, Dtor, Addr);

  // The default behavior is to use atexit.
  CGF.registerGlobalDtorWithAtExit(D, Dtor, Addr);
}

void MicrosoftCXXABI::EmitThreadLocalInitFuncs(
    CodeGenModule &CGM, ArrayRef<const VarDecl *> CXXThreadLocals,
    ArrayRef<llvm::Function *> CXXThreadLocalInits,
    ArrayRef<const VarDecl *> CXXThreadLocalInitVars) {
  if (CXXThreadLocalInits.empty())
    return;

  CGM.AppendLinkerOptions(CGM.getTarget().getTriple().getArch() ==
                                  llvm::Triple::x86
                              ? "/include:___dyn_tls_init@12"
                              : "/include:__dyn_tls_init");

  // This will create a GV in the .CRT$XDU section.  It will point to our
  // initialization function.  The CRT will call all of these function
  // pointers at start-up time and, eventually, at thread-creation time.
  auto AddToXDU = [&CGM](llvm::Function *InitFunc) {
    llvm::GlobalVariable *InitFuncPtr = new llvm::GlobalVariable(
        CGM.getModule(), InitFunc->getType(), /*IsConstant=*/true,
        llvm::GlobalVariable::InternalLinkage, InitFunc,
        Twine(InitFunc->getName(), "$initializer$"));
    InitFuncPtr->setSection(".CRT$XDU");
    // This variable has discardable linkage, we have to add it to @llvm.used to
    // ensure it won't get discarded.
    CGM.addUsedGlobal(InitFuncPtr);
    return InitFuncPtr;
  };

  std::vector<llvm::Function *> NonComdatInits;
  for (size_t I = 0, E = CXXThreadLocalInitVars.size(); I != E; ++I) {
    llvm::GlobalVariable *GV = cast<llvm::GlobalVariable>(
        CGM.GetGlobalValue(CGM.getMangledName(CXXThreadLocalInitVars[I])));
    llvm::Function *F = CXXThreadLocalInits[I];

    // If the GV is already in a comdat group, then we have to join it.
    if (llvm::Comdat *C = GV->getComdat())
      AddToXDU(F)->setComdat(C);
    else
      NonComdatInits.push_back(F);
  }

  if (!NonComdatInits.empty()) {
    llvm::FunctionType *FTy =
        llvm::FunctionType::get(CGM.VoidTy, /*isVarArg=*/false);
    llvm::Function *InitFunc = CGM.CreateGlobalInitOrDestructFunction(
        FTy, "__tls_init", CGM.getTypes().arrangeNullaryFunction(),
        SourceLocation(), /*TLS=*/true);
    CodeGenFunction(CGM).GenerateCXXGlobalInitFunc(InitFunc, NonComdatInits);

    AddToXDU(InitFunc);
  }
}

LValue MicrosoftCXXABI::EmitThreadLocalVarDeclLValue(CodeGenFunction &CGF,
                                                     const VarDecl *VD,
                                                     QualType LValType) {
  CGF.CGM.ErrorUnsupported(VD, "thread wrappers");
  return LValue();
}

static ConstantAddress getInitThreadEpochPtr(CodeGenModule &CGM) {
  StringRef VarName("_Init_thread_epoch");
  CharUnits Align = CGM.getIntAlign();
  if (auto *GV = CGM.getModule().getNamedGlobal(VarName))
    return ConstantAddress(GV, Align);
  auto *GV = new llvm::GlobalVariable(
      CGM.getModule(), CGM.IntTy,
      /*Constant=*/false, llvm::GlobalVariable::ExternalLinkage,
      /*Initializer=*/nullptr, VarName,
      /*InsertBefore=*/nullptr, llvm::GlobalVariable::GeneralDynamicTLSModel);
  GV->setAlignment(Align.getQuantity());
  return ConstantAddress(GV, Align);
}

static llvm::Constant *getInitThreadHeaderFn(CodeGenModule &CGM) {
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(llvm::Type::getVoidTy(CGM.getLLVMContext()),
                              CGM.IntTy->getPointerTo(), /*isVarArg=*/false);
  return CGM.CreateRuntimeFunction(
      FTy, "_Init_thread_header",
      llvm::AttributeList::get(CGM.getLLVMContext(),
                               llvm::AttributeList::FunctionIndex,
                               llvm::Attribute::NoUnwind),
      /*Local=*/true);
}

static llvm::Constant *getInitThreadFooterFn(CodeGenModule &CGM) {
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(llvm::Type::getVoidTy(CGM.getLLVMContext()),
                              CGM.IntTy->getPointerTo(), /*isVarArg=*/false);
  return CGM.CreateRuntimeFunction(
      FTy, "_Init_thread_footer",
      llvm::AttributeList::get(CGM.getLLVMContext(),
                               llvm::AttributeList::FunctionIndex,
                               llvm::Attribute::NoUnwind),
      /*Local=*/true);
}

static llvm::Constant *getInitThreadAbortFn(CodeGenModule &CGM) {
  llvm::FunctionType *FTy =
      llvm::FunctionType::get(llvm::Type::getVoidTy(CGM.getLLVMContext()),
                              CGM.IntTy->getPointerTo(), /*isVarArg=*/false);
  return CGM.CreateRuntimeFunction(
      FTy, "_Init_thread_abort",
      llvm::AttributeList::get(CGM.getLLVMContext(),
                               llvm::AttributeList::FunctionIndex,
                               llvm::Attribute::NoUnwind),
      /*Local=*/true);
}

namespace {
struct ResetGuardBit final : EHScopeStack::Cleanup {
  Address Guard;
  unsigned GuardNum;
  ResetGuardBit(Address Guard, unsigned GuardNum)
      : Guard(Guard), GuardNum(GuardNum) {}

  void Emit(CodeGenFunction &CGF, Flags flags) override {
    // Reset the bit in the mask so that the static variable may be
    // reinitialized.
    CGBuilderTy &Builder = CGF.Builder;
    llvm::LoadInst *LI = Builder.CreateLoad(Guard);
    llvm::ConstantInt *Mask =
        llvm::ConstantInt::get(CGF.IntTy, ~(1ULL << GuardNum));
    Builder.CreateStore(Builder.CreateAnd(LI, Mask), Guard);
  }
};

struct CallInitThreadAbort final : EHScopeStack::Cleanup {
  llvm::Value *Guard;
  CallInitThreadAbort(Address Guard) : Guard(Guard.getPointer()) {}

  void Emit(CodeGenFunction &CGF, Flags flags) override {
    // Calling _Init_thread_abort will reset the guard's state.
    CGF.EmitNounwindRuntimeCall(getInitThreadAbortFn(CGF.CGM), Guard);
  }
};
}

void MicrosoftCXXABI::EmitGuardedInit(CodeGenFunction &CGF, const VarDecl &D,
                                      llvm::GlobalVariable *GV,
                                      bool PerformInit) {
  // MSVC only uses guards for static locals.
  if (!D.isStaticLocal()) {
    assert(GV->hasWeakLinkage() || GV->hasLinkOnceLinkage());
    // GlobalOpt is allowed to discard the initializer, so use linkonce_odr.
    llvm::Function *F = CGF.CurFn;
    F->setLinkage(llvm::GlobalValue::LinkOnceODRLinkage);
    F->setComdat(CGM.getModule().getOrInsertComdat(F->getName()));
    CGF.EmitCXXGlobalVarDeclInit(D, GV, PerformInit);
    return;
  }

  bool ThreadlocalStatic = D.getTLSKind();
  bool ThreadsafeStatic = getContext().getLangOpts().ThreadsafeStatics;

  // Thread-safe static variables which aren't thread-specific have a
  // per-variable guard.
  bool HasPerVariableGuard = ThreadsafeStatic && !ThreadlocalStatic;

  CGBuilderTy &Builder = CGF.Builder;
  llvm::IntegerType *GuardTy = CGF.Int32Ty;
  llvm::ConstantInt *Zero = llvm::ConstantInt::get(GuardTy, 0);
  CharUnits GuardAlign = CharUnits::fromQuantity(4);

  // Get the guard variable for this function if we have one already.
  GuardInfo *GI = nullptr;
  if (ThreadlocalStatic)
    GI = &ThreadLocalGuardVariableMap[D.getDeclContext()];
  else if (!ThreadsafeStatic)
    GI = &GuardVariableMap[D.getDeclContext()];

  llvm::GlobalVariable *GuardVar = GI ? GI->Guard : nullptr;
  unsigned GuardNum;
  if (D.isExternallyVisible()) {
    // Externally visible variables have to be numbered in Sema to properly
    // handle unreachable VarDecls.
    GuardNum = getContext().getStaticLocalNumber(&D);
    assert(GuardNum > 0);
    GuardNum--;
  } else if (HasPerVariableGuard) {
    GuardNum = ThreadSafeGuardNumMap[D.getDeclContext()]++;
  } else {
    // Non-externally visible variables are numbered here in CodeGen.
    GuardNum = GI->BitIndex++;
  }

  if (!HasPerVariableGuard && GuardNum >= 32) {
    if (D.isExternallyVisible())
      ErrorUnsupportedABI(CGF, "more than 32 guarded initializations");
    GuardNum %= 32;
    GuardVar = nullptr;
  }

  if (!GuardVar) {
    // Mangle the name for the guard.
    SmallString<256> GuardName;
    {
      llvm::raw_svector_ostream Out(GuardName);
      if (HasPerVariableGuard)
        getMangleContext().mangleThreadSafeStaticGuardVariable(&D, GuardNum,
                                                               Out);
      else
        getMangleContext().mangleStaticGuardVariable(&D, Out);
    }

    // Create the guard variable with a zero-initializer. Just absorb linkage,
    // visibility and dll storage class from the guarded variable.
    GuardVar =
        new llvm::GlobalVariable(CGM.getModule(), GuardTy, /*isConstant=*/false,
                                 GV->getLinkage(), Zero, GuardName.str());
    GuardVar->setVisibility(GV->getVisibility());
    GuardVar->setDLLStorageClass(GV->getDLLStorageClass());
    GuardVar->setAlignment(GuardAlign.getQuantity());
    if (GuardVar->isWeakForLinker())
      GuardVar->setComdat(
          CGM.getModule().getOrInsertComdat(GuardVar->getName()));
    if (D.getTLSKind())
      GuardVar->setThreadLocal(true);
    if (GI && !HasPerVariableGuard)
      GI->Guard = GuardVar;
  }

  ConstantAddress GuardAddr(GuardVar, GuardAlign);

  assert(GuardVar->getLinkage() == GV->getLinkage() &&
         "static local from the same function had different linkage");

  if (!HasPerVariableGuard) {
    // Pseudo code for the test:
    // if (!(GuardVar & MyGuardBit)) {
    //   GuardVar |= MyGuardBit;
    //   ... initialize the object ...;
    // }

    // Test our bit from the guard variable.
    llvm::ConstantInt *Bit = llvm::ConstantInt::get(GuardTy, 1ULL << GuardNum);
    llvm::LoadInst *LI = Builder.CreateLoad(GuardAddr);
    llvm::Value *IsInitialized =
        Builder.CreateICmpNE(Builder.CreateAnd(LI, Bit), Zero);
    llvm::BasicBlock *InitBlock = CGF.createBasicBlock("init");
    llvm::BasicBlock *EndBlock = CGF.createBasicBlock("init.end");
    Builder.CreateCondBr(IsInitialized, EndBlock, InitBlock);

    // Set our bit in the guard variable and emit the initializer and add a global
    // destructor if appropriate.
    CGF.EmitBlock(InitBlock);
    Builder.CreateStore(Builder.CreateOr(LI, Bit), GuardAddr);
    CGF.EHStack.pushCleanup<ResetGuardBit>(EHCleanup, GuardAddr, GuardNum);
    CGF.EmitCXXGlobalVarDeclInit(D, GV, PerformInit);
    CGF.PopCleanupBlock();
    Builder.CreateBr(EndBlock);

    // Continue.
    CGF.EmitBlock(EndBlock);
  } else {
    // Pseudo code for the test:
    // if (TSS > _Init_thread_epoch) {
    //   _Init_thread_header(&TSS);
    //   if (TSS == -1) {
    //     ... initialize the object ...;
    //     _Init_thread_footer(&TSS);
    //   }
    // }
    //
    // The algorithm is almost identical to what can be found in the appendix
    // found in N2325.

    // This BasicBLock determines whether or not we have any work to do.
    llvm::LoadInst *FirstGuardLoad = Builder.CreateLoad(GuardAddr);
    FirstGuardLoad->setOrdering(llvm::AtomicOrdering::Unordered);
    llvm::LoadInst *InitThreadEpoch =
        Builder.CreateLoad(getInitThreadEpochPtr(CGM));
    llvm::Value *IsUninitialized =
        Builder.CreateICmpSGT(FirstGuardLoad, InitThreadEpoch);
    llvm::BasicBlock *AttemptInitBlock = CGF.createBasicBlock("init.attempt");
    llvm::BasicBlock *EndBlock = CGF.createBasicBlock("init.end");
    Builder.CreateCondBr(IsUninitialized, AttemptInitBlock, EndBlock);

    // This BasicBlock attempts to determine whether or not this thread is
    // responsible for doing the initialization.
    CGF.EmitBlock(AttemptInitBlock);
    CGF.EmitNounwindRuntimeCall(getInitThreadHeaderFn(CGM),
                                GuardAddr.getPointer());
    llvm::LoadInst *SecondGuardLoad = Builder.CreateLoad(GuardAddr);
    SecondGuardLoad->setOrdering(llvm::AtomicOrdering::Unordered);
    llvm::Value *ShouldDoInit =
        Builder.CreateICmpEQ(SecondGuardLoad, getAllOnesInt());
    llvm::BasicBlock *InitBlock = CGF.createBasicBlock("init");
    Builder.CreateCondBr(ShouldDoInit, InitBlock, EndBlock);

    // Ok, we ended up getting selected as the initializing thread.
    CGF.EmitBlock(InitBlock);
    CGF.EHStack.pushCleanup<CallInitThreadAbort>(EHCleanup, GuardAddr);
    CGF.EmitCXXGlobalVarDeclInit(D, GV, PerformInit);
    CGF.PopCleanupBlock();
    CGF.EmitNounwindRuntimeCall(getInitThreadFooterFn(CGM),
                                GuardAddr.getPointer());
    Builder.CreateBr(EndBlock);

    CGF.EmitBlock(EndBlock);
  }
}

bool MicrosoftCXXABI::isZeroInitializable(const MemberPointerType *MPT) {
  // Null-ness for function memptrs only depends on the first field, which is
  // the function pointer.  The rest don't matter, so we can zero initialize.
  if (MPT->isMemberFunctionPointer())
    return true;

  // The virtual base adjustment field is always -1 for null, so if we have one
  // we can't zero initialize.  The field offset is sometimes also -1 if 0 is a
  // valid field offset.
  const CXXRecordDecl *RD = MPT->getMostRecentCXXRecordDecl();
  MSInheritanceAttr::Spelling Inheritance = RD->getMSInheritanceModel();
  return (!MSInheritanceAttr::hasVBTableOffsetField(Inheritance) &&
          RD->nullFieldOffsetIsZero());
}

llvm::Type *
MicrosoftCXXABI::ConvertMemberPointerType(const MemberPointerType *MPT) {
  const CXXRecordDecl *RD = MPT->getMostRecentCXXRecordDecl();
  MSInheritanceAttr::Spelling Inheritance = RD->getMSInheritanceModel();
  llvm::SmallVector<llvm::Type *, 4> fields;
  if (MPT->isMemberFunctionPointer())
    fields.push_back(CGM.VoidPtrTy);  // FunctionPointerOrVirtualThunk
  else
    fields.push_back(CGM.IntTy);  // FieldOffset

  if (MSInheritanceAttr::hasNVOffsetField(MPT->isMemberFunctionPointer(),
                                          Inheritance))
    fields.push_back(CGM.IntTy);
  if (MSInheritanceAttr::hasVBPtrOffsetField(Inheritance))
    fields.push_back(CGM.IntTy);
  if (MSInheritanceAttr::hasVBTableOffsetField(Inheritance))
    fields.push_back(CGM.IntTy);  // VirtualBaseAdjustmentOffset

  if (fields.size() == 1)
    return fields[0];
  return llvm::StructType::get(CGM.getLLVMContext(), fields);
}

void MicrosoftCXXABI::
GetNullMemberPointerFields(const MemberPointerType *MPT,
                           llvm::SmallVectorImpl<llvm::Constant *> &fields) {
  assert(fields.empty());
  const CXXRecordDecl *RD = MPT->getMostRecentCXXRecordDecl();
  MSInheritanceAttr::Spelling Inheritance = RD->getMSInheritanceModel();
  if (MPT->isMemberFunctionPointer()) {
    // FunctionPointerOrVirtualThunk
    fields.push_back(llvm::Constant::getNullValue(CGM.VoidPtrTy));
  } else {
    if (RD->nullFieldOffsetIsZero())
      fields.push_back(getZeroInt());  // FieldOffset
    else
      fields.push_back(getAllOnesInt());  // FieldOffset
  }

  if (MSInheritanceAttr::hasNVOffsetField(MPT->isMemberFunctionPointer(),
                                          Inheritance))
    fields.push_back(getZeroInt());
  if (MSInheritanceAttr::hasVBPtrOffsetField(Inheritance))
    fields.push_back(getZeroInt());
  if (MSInheritanceAttr::hasVBTableOffsetField(Inheritance))
    fields.push_back(getAllOnesInt());
}

llvm::Constant *
MicrosoftCXXABI::EmitNullMemberPointer(const MemberPointerType *MPT) {
  llvm::SmallVector<llvm::Constant *, 4> fields;
  GetNullMemberPointerFields(MPT, fields);
  if (fields.size() == 1)
    return fields[0];
  llvm::Constant *Res = llvm::ConstantStruct::getAnon(fields);
  assert(Res->getType() == ConvertMemberPointerType(MPT));
  return Res;
}

llvm::Constant *
MicrosoftCXXABI::EmitFullMemberPointer(llvm::Constant *FirstField,
                                       bool IsMemberFunction,
                                       const CXXRecordDecl *RD,
                                       CharUnits NonVirtualBaseAdjustment,
                                       unsigned VBTableIndex) {
  MSInheritanceAttr::Spelling Inheritance = RD->getMSInheritanceModel();

  // Single inheritance class member pointer are represented as scalars instead
  // of aggregates.
  if (MSInheritanceAttr::hasOnlyOneField(IsMemberFunction, Inheritance))
    return FirstField;

  llvm::SmallVector<llvm::Constant *, 4> fields;
  fields.push_back(FirstField);

  if (MSInheritanceAttr::hasNVOffsetField(IsMemberFunction, Inheritance))
    fields.push_back(llvm::ConstantInt::get(
      CGM.IntTy, NonVirtualBaseAdjustment.getQuantity()));

  if (MSInheritanceAttr::hasVBPtrOffsetField(Inheritance)) {
    CharUnits Offs = CharUnits::Zero();
    if (VBTableIndex)
      Offs = getContext().getASTRecordLayout(RD).getVBPtrOffset();
    fields.push_back(llvm::ConstantInt::get(CGM.IntTy, Offs.getQuantity()));
  }

  // The rest of the fields are adjusted by conversions to a more derived class.
  if (MSInheritanceAttr::hasVBTableOffsetField(Inheritance))
    fields.push_back(llvm::ConstantInt::get(CGM.IntTy, VBTableIndex));

  return llvm::ConstantStruct::getAnon(fields);
}

llvm::Constant *
MicrosoftCXXABI::EmitMemberDataPointer(const MemberPointerType *MPT,
                                       CharUnits offset) {
  const CXXRecordDecl *RD = MPT->getMostRecentCXXRecordDecl();
  if (RD->getMSInheritanceModel() ==
      MSInheritanceAttr::Keyword_virtual_inheritance)
    offset -= getContext().getOffsetOfBaseWithVBPtr(RD);
  llvm::Constant *FirstField =
    llvm::ConstantInt::get(CGM.IntTy, offset.getQuantity());
  return EmitFullMemberPointer(FirstField, /*IsMemberFunction=*/false, RD,
                               CharUnits::Zero(), /*VBTableIndex=*/0);
}

llvm::Constant *MicrosoftCXXABI::EmitMemberPointer(const APValue &MP,
                                                   QualType MPType) {
  const MemberPointerType *DstTy = MPType->castAs<MemberPointerType>();
  const ValueDecl *MPD = MP.getMemberPointerDecl();
  if (!MPD)
    return EmitNullMemberPointer(DstTy);

  ASTContext &Ctx = getContext();
  ArrayRef<const CXXRecordDecl *> MemberPointerPath = MP.getMemberPointerPath();

  llvm::Constant *C;
  if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(MPD)) {
    C = EmitMemberFunctionPointer(MD);
  } else {
    CharUnits FieldOffset = Ctx.toCharUnitsFromBits(Ctx.getFieldOffset(MPD));
    C = EmitMemberDataPointer(DstTy, FieldOffset);
  }

  if (!MemberPointerPath.empty()) {
    const CXXRecordDecl *SrcRD = cast<CXXRecordDecl>(MPD->getDeclContext());
    const Type *SrcRecTy = Ctx.getTypeDeclType(SrcRD).getTypePtr();
    const MemberPointerType *SrcTy =
        Ctx.getMemberPointerType(DstTy->getPointeeType(), SrcRecTy)
            ->castAs<MemberPointerType>();

    bool DerivedMember = MP.isMemberPointerToDerivedMember();
    SmallVector<const CXXBaseSpecifier *, 4> DerivedToBasePath;
    const CXXRecordDecl *PrevRD = SrcRD;
    for (const CXXRecordDecl *PathElem : MemberPointerPath) {
      const CXXRecordDecl *Base = nullptr;
      const CXXRecordDecl *Derived = nullptr;
      if (DerivedMember) {
        Base = PathElem;
        Derived = PrevRD;
      } else {
        Base = PrevRD;
        Derived = PathElem;
      }
      for (const CXXBaseSpecifier &BS : Derived->bases())
        if (BS.getType()->getAsCXXRecordDecl()->getCanonicalDecl() ==
            Base->getCanonicalDecl())
          DerivedToBasePath.push_back(&BS);
      PrevRD = PathElem;
    }
    assert(DerivedToBasePath.size() == MemberPointerPath.size());

    CastKind CK = DerivedMember ? CK_DerivedToBaseMemberPointer
                                : CK_BaseToDerivedMemberPointer;
    C = EmitMemberPointerConversion(SrcTy, DstTy, CK, DerivedToBasePath.begin(),
                                    DerivedToBasePath.end(), C);
  }
  return C;
}

llvm::Constant *
MicrosoftCXXABI::EmitMemberFunctionPointer(const CXXMethodDecl *MD) {
  assert(MD->isInstance() && "Member function must not be static!");

  MD = MD->getCanonicalDecl();
  CharUnits NonVirtualBaseAdjustment = CharUnits::Zero();
  const CXXRecordDecl *RD = MD->getParent()->getMostRecentDecl();
  CodeGenTypes &Types = CGM.getTypes();

  unsigned VBTableIndex = 0;
  llvm::Constant *FirstField;
  const FunctionProtoType *FPT = MD->getType()->castAs<FunctionProtoType>();
  if (!MD->isVirtual()) {
    llvm::Type *Ty;
    // Check whether the function has a computable LLVM signature.
    if (Types.isFuncTypeConvertible(FPT)) {
      // The function has a computable LLVM signature; use the correct type.
      Ty = Types.GetFunctionType(Types.arrangeCXXMethodDeclaration(MD));
    } else {
      // Use an arbitrary non-function type to tell GetAddrOfFunction that the
      // function type is incomplete.
      Ty = CGM.PtrDiffTy;
    }
    FirstField = CGM.GetAddrOfFunction(MD, Ty);
  } else {
    auto &VTableContext = CGM.getMicrosoftVTableContext();
    MicrosoftVTableContext::MethodVFTableLocation ML =
        VTableContext.getMethodVFTableLocation(MD);
    FirstField = EmitVirtualMemPtrThunk(MD, ML);
    // Include the vfptr adjustment if the method is in a non-primary vftable.
    NonVirtualBaseAdjustment += ML.VFPtrOffset;
    if (ML.VBase)
      VBTableIndex = VTableContext.getVBTableIndex(RD, ML.VBase) * 4;
  }

  if (VBTableIndex == 0 &&
      RD->getMSInheritanceModel() ==
          MSInheritanceAttr::Keyword_virtual_inheritance)
    NonVirtualBaseAdjustment -= getContext().getOffsetOfBaseWithVBPtr(RD);

  // The rest of the fields are common with data member pointers.
  FirstField = llvm::ConstantExpr::getBitCast(FirstField, CGM.VoidPtrTy);
  return EmitFullMemberPointer(FirstField, /*IsMemberFunction=*/true, RD,
                               NonVirtualBaseAdjustment, VBTableIndex);
}

/// Member pointers are the same if they're either bitwise identical *or* both
/// null.  Null-ness for function members is determined by the first field,
/// while for data member pointers we must compare all fields.
llvm::Value *
MicrosoftCXXABI::EmitMemberPointerComparison(CodeGenFunction &CGF,
                                             llvm::Value *L,
                                             llvm::Value *R,
                                             const MemberPointerType *MPT,
                                             bool Inequality) {
  CGBuilderTy &Builder = CGF.Builder;

  // Handle != comparisons by switching the sense of all boolean operations.
  llvm::ICmpInst::Predicate Eq;
  llvm::Instruction::BinaryOps And, Or;
  if (Inequality) {
    Eq = llvm::ICmpInst::ICMP_NE;
    And = llvm::Instruction::Or;
    Or = llvm::Instruction::And;
  } else {
    Eq = llvm::ICmpInst::ICMP_EQ;
    And = llvm::Instruction::And;
    Or = llvm::Instruction::Or;
  }

  // If this is a single field member pointer (single inheritance), this is a
  // single icmp.
  const CXXRecordDecl *RD = MPT->getMostRecentCXXRecordDecl();
  MSInheritanceAttr::Spelling Inheritance = RD->getMSInheritanceModel();
  if (MSInheritanceAttr::hasOnlyOneField(MPT->isMemberFunctionPointer(),
                                         Inheritance))
    return Builder.CreateICmp(Eq, L, R);

  // Compare the first field.
  llvm::Value *L0 = Builder.CreateExtractValue(L, 0, "lhs.0");
  llvm::Value *R0 = Builder.CreateExtractValue(R, 0, "rhs.0");
  llvm::Value *Cmp0 = Builder.CreateICmp(Eq, L0, R0, "memptr.cmp.first");

  // Compare everything other than the first field.
  llvm::Value *Res = nullptr;
  llvm::StructType *LType = cast<llvm::StructType>(L->getType());
  for (unsigned I = 1, E = LType->getNumElements(); I != E; ++I) {
    llvm::Value *LF = Builder.CreateExtractValue(L, I);
    llvm::Value *RF = Builder.CreateExtractValue(R, I);
    llvm::Value *Cmp = Builder.CreateICmp(Eq, LF, RF, "memptr.cmp.rest");
    if (Res)
      Res = Builder.CreateBinOp(And, Res, Cmp);
    else
      Res = Cmp;
  }

  // Check if the first field is 0 if this is a function pointer.
  if (MPT->isMemberFunctionPointer()) {
    // (l1 == r1 && ...) || l0 == 0
    llvm::Value *Zero = llvm::Constant::getNullValue(L0->getType());
    llvm::Value *IsZero = Builder.CreateICmp(Eq, L0, Zero, "memptr.cmp.iszero");
    Res = Builder.CreateBinOp(Or, Res, IsZero);
  }

  // Combine the comparison of the first field, which must always be true for
  // this comparison to succeeed.
  return Builder.CreateBinOp(And, Res, Cmp0, "memptr.cmp");
}

llvm::Value *
MicrosoftCXXABI::EmitMemberPointerIsNotNull(CodeGenFunction &CGF,
                                            llvm::Value *MemPtr,
                                            const MemberPointerType *MPT) {
  CGBuilderTy &Builder = CGF.Builder;
  llvm::SmallVector<llvm::Constant *, 4> fields;
  // We only need one field for member functions.
  if (MPT->isMemberFunctionPointer())
    fields.push_back(llvm::Constant::getNullValue(CGM.VoidPtrTy));
  else
    GetNullMemberPointerFields(MPT, fields);
  assert(!fields.empty());
  llvm::Value *FirstField = MemPtr;
  if (MemPtr->getType()->isStructTy())
    FirstField = Builder.CreateExtractValue(MemPtr, 0);
  llvm::Value *Res = Builder.CreateICmpNE(FirstField, fields[0], "memptr.cmp0");

  // For function member pointers, we only need to test the function pointer
  // field.  The other fields if any can be garbage.
  if (MPT->isMemberFunctionPointer())
    return Res;

  // Otherwise, emit a series of compares and combine the results.
  for (int I = 1, E = fields.size(); I < E; ++I) {
    llvm::Value *Field = Builder.CreateExtractValue(MemPtr, I);
    llvm::Value *Next = Builder.CreateICmpNE(Field, fields[I], "memptr.cmp");
    Res = Builder.CreateOr(Res, Next, "memptr.tobool");
  }
  return Res;
}

bool MicrosoftCXXABI::MemberPointerConstantIsNull(const MemberPointerType *MPT,
                                                  llvm::Constant *Val) {
  // Function pointers are null if the pointer in the first field is null.
  if (MPT->isMemberFunctionPointer()) {
    llvm::Constant *FirstField = Val->getType()->isStructTy() ?
      Val->getAggregateElement(0U) : Val;
    return FirstField->isNullValue();
  }

  // If it's not a function pointer and it's zero initializable, we can easily
  // check zero.
  if (isZeroInitializable(MPT) && Val->isNullValue())
    return true;

  // Otherwise, break down all the fields for comparison.  Hopefully these
  // little Constants are reused, while a big null struct might not be.
  llvm::SmallVector<llvm::Constant *, 4> Fields;
  GetNullMemberPointerFields(MPT, Fields);
  if (Fields.size() == 1) {
    assert(Val->getType()->isIntegerTy());
    return Val == Fields[0];
  }

  unsigned I, E;
  for (I = 0, E = Fields.size(); I != E; ++I) {
    if (Val->getAggregateElement(I) != Fields[I])
      break;
  }
  return I == E;
}

llvm::Value *
MicrosoftCXXABI::GetVBaseOffsetFromVBPtr(CodeGenFunction &CGF,
                                         Address This,
                                         llvm::Value *VBPtrOffset,
                                         llvm::Value *VBTableOffset,
                                         llvm::Value **VBPtrOut) {
  CGBuilderTy &Builder = CGF.Builder;
  // Load the vbtable pointer from the vbptr in the instance.
  This = Builder.CreateElementBitCast(This, CGM.Int8Ty);
  llvm::Value *VBPtr =
    Builder.CreateInBoundsGEP(This.getPointer(), VBPtrOffset, "vbptr");
  if (VBPtrOut) *VBPtrOut = VBPtr;
  VBPtr = Builder.CreateBitCast(VBPtr,
            CGM.Int32Ty->getPointerTo(0)->getPointerTo(This.getAddressSpace()));

  CharUnits VBPtrAlign;
  if (auto CI = dyn_cast<llvm::ConstantInt>(VBPtrOffset)) {
    VBPtrAlign = This.getAlignment().alignmentAtOffset(
                                   CharUnits::fromQuantity(CI->getSExtValue()));
  } else {
    VBPtrAlign = CGF.getPointerAlign();
  }

  llvm::Value *VBTable = Builder.CreateAlignedLoad(VBPtr, VBPtrAlign, "vbtable");

  // Translate from byte offset to table index. It improves analyzability.
  llvm::Value *VBTableIndex = Builder.CreateAShr(
      VBTableOffset, llvm::ConstantInt::get(VBTableOffset->getType(), 2),
      "vbtindex", /*isExact=*/true);

  // Load an i32 offset from the vb-table.
  llvm::Value *VBaseOffs = Builder.CreateInBoundsGEP(VBTable, VBTableIndex);
  VBaseOffs = Builder.CreateBitCast(VBaseOffs, CGM.Int32Ty->getPointerTo(0));
  return Builder.CreateAlignedLoad(VBaseOffs, CharUnits::fromQuantity(4),
                                   "vbase_offs");
}

// Returns an adjusted base cast to i8*, since we do more address arithmetic on
// it.
llvm::Value *MicrosoftCXXABI::AdjustVirtualBase(
    CodeGenFunction &CGF, const Expr *E, const CXXRecordDecl *RD,
    Address Base, llvm::Value *VBTableOffset, llvm::Value *VBPtrOffset) {
  CGBuilderTy &Builder = CGF.Builder;
  Base = Builder.CreateElementBitCast(Base, CGM.Int8Ty);
  llvm::BasicBlock *OriginalBB = nullptr;
  llvm::BasicBlock *SkipAdjustBB = nullptr;
  llvm::BasicBlock *VBaseAdjustBB = nullptr;

  // In the unspecified inheritance model, there might not be a vbtable at all,
  // in which case we need to skip the virtual base lookup.  If there is a
  // vbtable, the first entry is a no-op entry that gives back the original
  // base, so look for a virtual base adjustment offset of zero.
  if (VBPtrOffset) {
    OriginalBB = Builder.GetInsertBlock();
    VBaseAdjustBB = CGF.createBasicBlock("memptr.vadjust");
    SkipAdjustBB = CGF.createBasicBlock("memptr.skip_vadjust");
    llvm::Value *IsVirtual =
      Builder.CreateICmpNE(VBTableOffset, getZeroInt(),
                           "memptr.is_vbase");
    Builder.CreateCondBr(IsVirtual, VBaseAdjustBB, SkipAdjustBB);
    CGF.EmitBlock(VBaseAdjustBB);
  }

  // If we weren't given a dynamic vbptr offset, RD should be complete and we'll
  // know the vbptr offset.
  if (!VBPtrOffset) {
    CharUnits offs = CharUnits::Zero();
    if (!RD->hasDefinition()) {
      DiagnosticsEngine &Diags = CGF.CGM.getDiags();
      unsigned DiagID = Diags.getCustomDiagID(
          DiagnosticsEngine::Error,
          "member pointer representation requires a "
          "complete class type for %0 to perform this expression");
      Diags.Report(E->getExprLoc(), DiagID) << RD << E->getSourceRange();
    } else if (RD->getNumVBases())
      offs = getContext().getASTRecordLayout(RD).getVBPtrOffset();
    VBPtrOffset = llvm::ConstantInt::get(CGM.IntTy, offs.getQuantity());
  }
  llvm::Value *VBPtr = nullptr;
  llvm::Value *VBaseOffs =
    GetVBaseOffsetFromVBPtr(CGF, Base, VBPtrOffset, VBTableOffset, &VBPtr);
  llvm::Value *AdjustedBase = Builder.CreateInBoundsGEP(VBPtr, VBaseOffs);

  // Merge control flow with the case where we didn't have to adjust.
  if (VBaseAdjustBB) {
    Builder.CreateBr(SkipAdjustBB);
    CGF.EmitBlock(SkipAdjustBB);
    llvm::PHINode *Phi = Builder.CreatePHI(CGM.Int8PtrTy, 2, "memptr.base");
    Phi->addIncoming(Base.getPointer(), OriginalBB);
    Phi->addIncoming(AdjustedBase, VBaseAdjustBB);
    return Phi;
  }
  return AdjustedBase;
}

llvm::Value *MicrosoftCXXABI::EmitMemberDataPointerAddress(
    CodeGenFunction &CGF, const Expr *E, Address Base, llvm::Value *MemPtr,
    const MemberPointerType *MPT) {
  assert(MPT->isMemberDataPointer());
  unsigned AS = Base.getAddressSpace();
  llvm::Type *PType =
      CGF.ConvertTypeForMem(MPT->getPointeeType())->getPointerTo(AS);
  CGBuilderTy &Builder = CGF.Builder;
  const CXXRecordDecl *RD = MPT->getMostRecentCXXRecordDecl();
  MSInheritanceAttr::Spelling Inheritance = RD->getMSInheritanceModel();

  // Extract the fields we need, regardless of model.  We'll apply them if we
  // have them.
  llvm::Value *FieldOffset = MemPtr;
  llvm::Value *VirtualBaseAdjustmentOffset = nullptr;
  llvm::Value *VBPtrOffset = nullptr;
  if (MemPtr->getType()->isStructTy()) {
    // We need to extract values.
    unsigned I = 0;
    FieldOffset = Builder.CreateExtractValue(MemPtr, I++);
    if (MSInheritanceAttr::hasVBPtrOffsetField(Inheritance))
      VBPtrOffset = Builder.CreateExtractValue(MemPtr, I++);
    if (MSInheritanceAttr::hasVBTableOffsetField(Inheritance))
      VirtualBaseAdjustmentOffset = Builder.CreateExtractValue(MemPtr, I++);
  }

  llvm::Value *Addr;
  if (VirtualBaseAdjustmentOffset) {
    Addr = AdjustVirtualBase(CGF, E, RD, Base, VirtualBaseAdjustmentOffset,
                             VBPtrOffset);
  } else {
    Addr = Base.getPointer();
  }

  // Cast to char*.
  Addr = Builder.CreateBitCast(Addr, CGF.Int8Ty->getPointerTo(AS));

  // Apply the offset, which we assume is non-null.
  Addr = Builder.CreateInBoundsGEP(Addr, FieldOffset, "memptr.offset");

  // Cast the address to the appropriate pointer type, adopting the address
  // space of the base pointer.
  return Builder.CreateBitCast(Addr, PType);
}

llvm::Value *
MicrosoftCXXABI::EmitMemberPointerConversion(CodeGenFunction &CGF,
                                             const CastExpr *E,
                                             llvm::Value *Src) {
  assert(E->getCastKind() == CK_DerivedToBaseMemberPointer ||
         E->getCastKind() == CK_BaseToDerivedMemberPointer ||
         E->getCastKind() == CK_ReinterpretMemberPointer);

  // Use constant emission if we can.
  if (isa<llvm::Constant>(Src))
    return EmitMemberPointerConversion(E, cast<llvm::Constant>(Src));

  // We may be adding or dropping fields from the member pointer, so we need
  // both types and the inheritance models of both records.
  const MemberPointerType *SrcTy =
    E->getSubExpr()->getType()->castAs<MemberPointerType>();
  const MemberPointerType *DstTy = E->getType()->castAs<MemberPointerType>();
  bool IsFunc = SrcTy->isMemberFunctionPointer();

  // If the classes use the same null representation, reinterpret_cast is a nop.
  bool IsReinterpret = E->getCastKind() == CK_ReinterpretMemberPointer;
  if (IsReinterpret && IsFunc)
    return Src;

  CXXRecordDecl *SrcRD = SrcTy->getMostRecentCXXRecordDecl();
  CXXRecordDecl *DstRD = DstTy->getMostRecentCXXRecordDecl();
  if (IsReinterpret &&
      SrcRD->nullFieldOffsetIsZero() == DstRD->nullFieldOffsetIsZero())
    return Src;

  CGBuilderTy &Builder = CGF.Builder;

  // Branch past the conversion if Src is null.
  llvm::Value *IsNotNull = EmitMemberPointerIsNotNull(CGF, Src, SrcTy);
  llvm::Constant *DstNull = EmitNullMemberPointer(DstTy);

  // C++ 5.2.10p9: The null member pointer value is converted to the null member
  //   pointer value of the destination type.
  if (IsReinterpret) {
    // For reinterpret casts, sema ensures that src and dst are both functions
    // or data and have the same size, which means the LLVM types should match.
    assert(Src->getType() == DstNull->getType());
    return Builder.CreateSelect(IsNotNull, Src, DstNull);
  }

  llvm::BasicBlock *OriginalBB = Builder.GetInsertBlock();
  llvm::BasicBlock *ConvertBB = CGF.createBasicBlock("memptr.convert");
  llvm::BasicBlock *ContinueBB = CGF.createBasicBlock("memptr.converted");
  Builder.CreateCondBr(IsNotNull, ConvertBB, ContinueBB);
  CGF.EmitBlock(ConvertBB);

  llvm::Value *Dst = EmitNonNullMemberPointerConversion(
      SrcTy, DstTy, E->getCastKind(), E->path_begin(), E->path_end(), Src,
      Builder);

  Builder.CreateBr(ContinueBB);

  // In the continuation, choose between DstNull and Dst.
  CGF.EmitBlock(ContinueBB);
  llvm::PHINode *Phi = Builder.CreatePHI(DstNull->getType(), 2, "memptr.converted");
  Phi->addIncoming(DstNull, OriginalBB);
  Phi->addIncoming(Dst, ConvertBB);
  return Phi;
}

llvm::Value *MicrosoftCXXABI::EmitNonNullMemberPointerConversion(
    const MemberPointerType *SrcTy, const MemberPointerType *DstTy, CastKind CK,
    CastExpr::path_const_iterator PathBegin,
    CastExpr::path_const_iterator PathEnd, llvm::Value *Src,
    CGBuilderTy &Builder) {
  const CXXRecordDecl *SrcRD = SrcTy->getMostRecentCXXRecordDecl();
  const CXXRecordDecl *DstRD = DstTy->getMostRecentCXXRecordDecl();
  MSInheritanceAttr::Spelling SrcInheritance = SrcRD->getMSInheritanceModel();
  MSInheritanceAttr::Spelling DstInheritance = DstRD->getMSInheritanceModel();
  bool IsFunc = SrcTy->isMemberFunctionPointer();
  bool IsConstant = isa<llvm::Constant>(Src);

  // Decompose src.
  llvm::Value *FirstField = Src;
  llvm::Value *NonVirtualBaseAdjustment = getZeroInt();
  llvm::Value *VirtualBaseAdjustmentOffset = getZeroInt();
  llvm::Value *VBPtrOffset = getZeroInt();
  if (!MSInheritanceAttr::hasOnlyOneField(IsFunc, SrcInheritance)) {
    // We need to extract values.
    unsigned I = 0;
    FirstField = Builder.CreateExtractValue(Src, I++);
    if (MSInheritanceAttr::hasNVOffsetField(IsFunc, SrcInheritance))
      NonVirtualBaseAdjustment = Builder.CreateExtractValue(Src, I++);
    if (MSInheritanceAttr::hasVBPtrOffsetField(SrcInheritance))
      VBPtrOffset = Builder.CreateExtractValue(Src, I++);
    if (MSInheritanceAttr::hasVBTableOffsetField(SrcInheritance))
      VirtualBaseAdjustmentOffset = Builder.CreateExtractValue(Src, I++);
  }

  bool IsDerivedToBase = (CK == CK_DerivedToBaseMemberPointer);
  const MemberPointerType *DerivedTy = IsDerivedToBase ? SrcTy : DstTy;
  const CXXRecordDecl *DerivedClass = DerivedTy->getMostRecentCXXRecordDecl();

  // For data pointers, we adjust the field offset directly.  For functions, we
  // have a separate field.
  llvm::Value *&NVAdjustField = IsFunc ? NonVirtualBaseAdjustment : FirstField;

  // The virtual inheritance model has a quirk: the virtual base table is always
  // referenced when dereferencing a member pointer even if the member pointer
  // is non-virtual.  This is accounted for by adjusting the non-virtual offset
  // to point backwards to the top of the MDC from the first VBase.  Undo this
  // adjustment to normalize the member pointer.
  llvm::Value *SrcVBIndexEqZero =
      Builder.CreateICmpEQ(VirtualBaseAdjustmentOffset, getZeroInt());
  if (SrcInheritance == MSInheritanceAttr::Keyword_virtual_inheritance) {
    if (int64_t SrcOffsetToFirstVBase =
            getContext().getOffsetOfBaseWithVBPtr(SrcRD).getQuantity()) {
      llvm::Value *UndoSrcAdjustment = Builder.CreateSelect(
          SrcVBIndexEqZero,
          llvm::ConstantInt::get(CGM.IntTy, SrcOffsetToFirstVBase),
          getZeroInt());
      NVAdjustField = Builder.CreateNSWAdd(NVAdjustField, UndoSrcAdjustment);
    }
  }

  // A non-zero vbindex implies that we are dealing with a source member in a
  // floating virtual base in addition to some non-virtual offset.  If the
  // vbindex is zero, we are dealing with a source that exists in a non-virtual,
  // fixed, base.  The difference between these two cases is that the vbindex +
  // nvoffset *always* point to the member regardless of what context they are
  // evaluated in so long as the vbindex is adjusted.  A member inside a fixed
  // base requires explicit nv adjustment.
  llvm::Constant *BaseClassOffset = llvm::ConstantInt::get(
      CGM.IntTy,
      CGM.computeNonVirtualBaseClassOffset(DerivedClass, PathBegin, PathEnd)
          .getQuantity());

  llvm::Value *NVDisp;
  if (IsDerivedToBase)
    NVDisp = Builder.CreateNSWSub(NVAdjustField, BaseClassOffset, "adj");
  else
    NVDisp = Builder.CreateNSWAdd(NVAdjustField, BaseClassOffset, "adj");

  NVAdjustField = Builder.CreateSelect(SrcVBIndexEqZero, NVDisp, getZeroInt());

  // Update the vbindex to an appropriate value in the destination because
  // SrcRD's vbtable might not be a strict prefix of the one in DstRD.
  llvm::Value *DstVBIndexEqZero = SrcVBIndexEqZero;
  if (MSInheritanceAttr::hasVBTableOffsetField(DstInheritance) &&
      MSInheritanceAttr::hasVBTableOffsetField(SrcInheritance)) {
    if (llvm::GlobalVariable *VDispMap =
            getAddrOfVirtualDisplacementMap(SrcRD, DstRD)) {
      llvm::Value *VBIndex = Builder.CreateExactUDiv(
          VirtualBaseAdjustmentOffset, llvm::ConstantInt::get(CGM.IntTy, 4));
      if (IsConstant) {
        llvm::Constant *Mapping = VDispMap->getInitializer();
        VirtualBaseAdjustmentOffset =
            Mapping->getAggregateElement(cast<llvm::Constant>(VBIndex));
      } else {
        llvm::Value *Idxs[] = {getZeroInt(), VBIndex};
        VirtualBaseAdjustmentOffset =
            Builder.CreateAlignedLoad(Builder.CreateInBoundsGEP(VDispMap, Idxs),
                                      CharUnits::fromQuantity(4));
      }

      DstVBIndexEqZero =
          Builder.CreateICmpEQ(VirtualBaseAdjustmentOffset, getZeroInt());
    }
  }

  // Set the VBPtrOffset to zero if the vbindex is zero.  Otherwise, initialize
  // it to the offset of the vbptr.
  if (MSInheritanceAttr::hasVBPtrOffsetField(DstInheritance)) {
    llvm::Value *DstVBPtrOffset = llvm::ConstantInt::get(
        CGM.IntTy,
        getContext().getASTRecordLayout(DstRD).getVBPtrOffset().getQuantity());
    VBPtrOffset =
        Builder.CreateSelect(DstVBIndexEqZero, getZeroInt(), DstVBPtrOffset);
  }

  // Likewise, apply a similar adjustment so that dereferencing the member
  // pointer correctly accounts for the distance between the start of the first
  // virtual base and the top of the MDC.
  if (DstInheritance == MSInheritanceAttr::Keyword_virtual_inheritance) {
    if (int64_t DstOffsetToFirstVBase =
            getContext().getOffsetOfBaseWithVBPtr(DstRD).getQuantity()) {
      llvm::Value *DoDstAdjustment = Builder.CreateSelect(
          DstVBIndexEqZero,
          llvm::ConstantInt::get(CGM.IntTy, DstOffsetToFirstVBase),
          getZeroInt());
      NVAdjustField = Builder.CreateNSWSub(NVAdjustField, DoDstAdjustment);
    }
  }

  // Recompose dst from the null struct and the adjusted fields from src.
  llvm::Value *Dst;
  if (MSInheritanceAttr::hasOnlyOneField(IsFunc, DstInheritance)) {
    Dst = FirstField;
  } else {
    Dst = llvm::UndefValue::get(ConvertMemberPointerType(DstTy));
    unsigned Idx = 0;
    Dst = Builder.CreateInsertValue(Dst, FirstField, Idx++);
    if (MSInheritanceAttr::hasNVOffsetField(IsFunc, DstInheritance))
      Dst = Builder.CreateInsertValue(Dst, NonVirtualBaseAdjustment, Idx++);
    if (MSInheritanceAttr::hasVBPtrOffsetField(DstInheritance))
      Dst = Builder.CreateInsertValue(Dst, VBPtrOffset, Idx++);
    if (MSInheritanceAttr::hasVBTableOffsetField(DstInheritance))
      Dst = Builder.CreateInsertValue(Dst, VirtualBaseAdjustmentOffset, Idx++);
  }
  return Dst;
}

llvm::Constant *
MicrosoftCXXABI::EmitMemberPointerConversion(const CastExpr *E,
                                             llvm::Constant *Src) {
  const MemberPointerType *SrcTy =
      E->getSubExpr()->getType()->castAs<MemberPointerType>();
  const MemberPointerType *DstTy = E->getType()->castAs<MemberPointerType>();

  CastKind CK = E->getCastKind();

  return EmitMemberPointerConversion(SrcTy, DstTy, CK, E->path_begin(),
                                     E->path_end(), Src);
}

llvm::Constant *MicrosoftCXXABI::EmitMemberPointerConversion(
    const MemberPointerType *SrcTy, const MemberPointerType *DstTy, CastKind CK,
    CastExpr::path_const_iterator PathBegin,
    CastExpr::path_const_iterator PathEnd, llvm::Constant *Src) {
  assert(CK == CK_DerivedToBaseMemberPointer ||
         CK == CK_BaseToDerivedMemberPointer ||
         CK == CK_ReinterpretMemberPointer);
  // If src is null, emit a new null for dst.  We can't return src because dst
  // might have a new representation.
  if (MemberPointerConstantIsNull(SrcTy, Src))
    return EmitNullMemberPointer(DstTy);

  // We don't need to do anything for reinterpret_casts of non-null member
  // pointers.  We should only get here when the two type representations have
  // the same size.
  if (CK == CK_ReinterpretMemberPointer)
    return Src;

  CGBuilderTy Builder(CGM, CGM.getLLVMContext());
  auto *Dst = cast<llvm::Constant>(EmitNonNullMemberPointerConversion(
      SrcTy, DstTy, CK, PathBegin, PathEnd, Src, Builder));

  return Dst;
}

CGCallee MicrosoftCXXABI::EmitLoadOfMemberFunctionPointer(
    CodeGenFunction &CGF, const Expr *E, Address This,
    llvm::Value *&ThisPtrForCall, llvm::Value *MemPtr,
    const MemberPointerType *MPT) {
  assert(MPT->isMemberFunctionPointer());
  const FunctionProtoType *FPT =
    MPT->getPointeeType()->castAs<FunctionProtoType>();
  const CXXRecordDecl *RD = MPT->getMostRecentCXXRecordDecl();
  llvm::FunctionType *FTy = CGM.getTypes().GetFunctionType(
      CGM.getTypes().arrangeCXXMethodType(RD, FPT, /*FD=*/nullptr));
  CGBuilderTy &Builder = CGF.Builder;

  MSInheritanceAttr::Spelling Inheritance = RD->getMSInheritanceModel();

  // Extract the fields we need, regardless of model.  We'll apply them if we
  // have them.
  llvm::Value *FunctionPointer = MemPtr;
  llvm::Value *NonVirtualBaseAdjustment = nullptr;
  llvm::Value *VirtualBaseAdjustmentOffset = nullptr;
  llvm::Value *VBPtrOffset = nullptr;
  if (MemPtr->getType()->isStructTy()) {
    // We need to extract values.
    unsigned I = 0;
    FunctionPointer = Builder.CreateExtractValue(MemPtr, I++);
    if (MSInheritanceAttr::hasNVOffsetField(MPT, Inheritance))
      NonVirtualBaseAdjustment = Builder.CreateExtractValue(MemPtr, I++);
    if (MSInheritanceAttr::hasVBPtrOffsetField(Inheritance))
      VBPtrOffset = Builder.CreateExtractValue(MemPtr, I++);
    if (MSInheritanceAttr::hasVBTableOffsetField(Inheritance))
      VirtualBaseAdjustmentOffset = Builder.CreateExtractValue(MemPtr, I++);
  }

  if (VirtualBaseAdjustmentOffset) {
    ThisPtrForCall = AdjustVirtualBase(CGF, E, RD, This,
                                   VirtualBaseAdjustmentOffset, VBPtrOffset);
  } else {
    ThisPtrForCall = This.getPointer();
  }

  if (NonVirtualBaseAdjustment) {
    // Apply the adjustment and cast back to the original struct type.
    llvm::Value *Ptr = Builder.CreateBitCast(ThisPtrForCall, CGF.Int8PtrTy);
    Ptr = Builder.CreateInBoundsGEP(Ptr, NonVirtualBaseAdjustment);
    ThisPtrForCall = Builder.CreateBitCast(Ptr, ThisPtrForCall->getType(),
                                           "this.adjusted");
  }

  FunctionPointer =
    Builder.CreateBitCast(FunctionPointer, FTy->getPointerTo());
  CGCallee Callee(FPT, FunctionPointer);
  return Callee;
}

CGCXXABI *clang::CodeGen::CreateMicrosoftCXXABI(CodeGenModule &CGM) {
  return new MicrosoftCXXABI(CGM);
}

// MS RTTI Overview:
// The run time type information emitted by cl.exe contains 5 distinct types of
// structures.  Many of them reference each other.
//
// TypeInfo:  Static classes that are returned by typeid.
//
// CompleteObjectLocator:  Referenced by vftables.  They contain information
//   required for dynamic casting, including OffsetFromTop.  They also contain
//   a reference to the TypeInfo for the type and a reference to the
//   CompleteHierarchyDescriptor for the type.
//
// ClassHieararchyDescriptor: Contains information about a class hierarchy.
//   Used during dynamic_cast to walk a class hierarchy.  References a base
//   class array and the size of said array.
//
// BaseClassArray: Contains a list of classes in a hierarchy.  BaseClassArray is
//   somewhat of a misnomer because the most derived class is also in the list
//   as well as multiple copies of virtual bases (if they occur multiple times
//   in the hiearchy.)  The BaseClassArray contains one BaseClassDescriptor for
//   every path in the hierarchy, in pre-order depth first order.  Note, we do
//   not declare a specific llvm type for BaseClassArray, it's merely an array
//   of BaseClassDescriptor pointers.
//
// BaseClassDescriptor: Contains information about a class in a class hierarchy.
//   BaseClassDescriptor is also somewhat of a misnomer for the same reason that
//   BaseClassArray is.  It contains information about a class within a
//   hierarchy such as: is this base is ambiguous and what is its offset in the
//   vbtable.  The names of the BaseClassDescriptors have all of their fields
//   mangled into them so they can be aggressively deduplicated by the linker.

static llvm::GlobalVariable *getTypeInfoVTable(CodeGenModule &CGM) {
  StringRef MangledName("\01??_7type_info@@6B@");
  if (auto VTable = CGM.getModule().getNamedGlobal(MangledName))
    return VTable;
  return new llvm::GlobalVariable(CGM.getModule(), CGM.Int8PtrTy,
                                  /*Constant=*/true,
                                  llvm::GlobalVariable::ExternalLinkage,
                                  /*Initializer=*/nullptr, MangledName);
}

namespace {

/// \brief A Helper struct that stores information about a class in a class
/// hierarchy.  The information stored in these structs struct is used during
/// the generation of ClassHierarchyDescriptors and BaseClassDescriptors.
// During RTTI creation, MSRTTIClasses are stored in a contiguous array with
// implicit depth first pre-order tree connectivity.  getFirstChild and
// getNextSibling allow us to walk the tree efficiently.
struct MSRTTIClass {
  enum {
    IsPrivateOnPath = 1 | 8,
    IsAmbiguous = 2,
    IsPrivate = 4,
    IsVirtual = 16,
    HasHierarchyDescriptor = 64
  };
  MSRTTIClass(const CXXRecordDecl *RD) : RD(RD) {}
  uint32_t initialize(const MSRTTIClass *Parent,
                      const CXXBaseSpecifier *Specifier);

  MSRTTIClass *getFirstChild() { return this + 1; }
  static MSRTTIClass *getNextChild(MSRTTIClass *Child) {
    return Child + 1 + Child->NumBases;
  }

  const CXXRecordDecl *RD, *VirtualRoot;
  uint32_t Flags, NumBases, OffsetInVBase;
};

/// \brief Recursively initialize the base class array.
uint32_t MSRTTIClass::initialize(const MSRTTIClass *Parent,
                                 const CXXBaseSpecifier *Specifier) {
  Flags = HasHierarchyDescriptor;
  if (!Parent) {
    VirtualRoot = nullptr;
    OffsetInVBase = 0;
  } else {
    if (Specifier->getAccessSpecifier() != AS_public)
      Flags |= IsPrivate | IsPrivateOnPath;
    if (Specifier->isVirtual()) {
      Flags |= IsVirtual;
      VirtualRoot = RD;
      OffsetInVBase = 0;
    } else {
      if (Parent->Flags & IsPrivateOnPath)
        Flags |= IsPrivateOnPath;
      VirtualRoot = Parent->VirtualRoot;
      OffsetInVBase = Parent->OffsetInVBase + RD->getASTContext()
          .getASTRecordLayout(Parent->RD).getBaseClassOffset(RD).getQuantity();
    }
  }
  NumBases = 0;
  MSRTTIClass *Child = getFirstChild();
  for (const CXXBaseSpecifier &Base : RD->bases()) {
    NumBases += Child->initialize(this, &Base) + 1;
    Child = getNextChild(Child);
  }
  return NumBases;
}

static llvm::GlobalValue::LinkageTypes getLinkageForRTTI(QualType Ty) {
  switch (Ty->getLinkage()) {
  case NoLinkage:
  case InternalLinkage:
  case UniqueExternalLinkage:
    return llvm::GlobalValue::InternalLinkage;

  case VisibleNoLinkage:
  case ExternalLinkage:
    return llvm::GlobalValue::LinkOnceODRLinkage;
  }
  llvm_unreachable("Invalid linkage!");
}

/// \brief An ephemeral helper class for building MS RTTI types.  It caches some
/// calls to the module and information about the most derived class in a
/// hierarchy.
struct MSRTTIBuilder {
  enum {
    HasBranchingHierarchy = 1,
    HasVirtualBranchingHierarchy = 2,
    HasAmbiguousBases = 4
  };

  MSRTTIBuilder(MicrosoftCXXABI &ABI, const CXXRecordDecl *RD)
      : CGM(ABI.CGM), Context(CGM.getContext()),
        VMContext(CGM.getLLVMContext()), Module(CGM.getModule()), RD(RD),
        Linkage(getLinkageForRTTI(CGM.getContext().getTagDeclType(RD))),
        ABI(ABI) {}

  llvm::GlobalVariable *getBaseClassDescriptor(const MSRTTIClass &Classes);
  llvm::GlobalVariable *
  getBaseClassArray(SmallVectorImpl<MSRTTIClass> &Classes);
  llvm::GlobalVariable *getClassHierarchyDescriptor();
  llvm::GlobalVariable *getCompleteObjectLocator(const VPtrInfo &Info);

  CodeGenModule &CGM;
  ASTContext &Context;
  llvm::LLVMContext &VMContext;
  llvm::Module &Module;
  const CXXRecordDecl *RD;
  llvm::GlobalVariable::LinkageTypes Linkage;
  MicrosoftCXXABI &ABI;
};

} // namespace

/// \brief Recursively serializes a class hierarchy in pre-order depth first
/// order.
static void serializeClassHierarchy(SmallVectorImpl<MSRTTIClass> &Classes,
                                    const CXXRecordDecl *RD) {
  Classes.push_back(MSRTTIClass(RD));
  for (const CXXBaseSpecifier &Base : RD->bases())
    serializeClassHierarchy(Classes, Base.getType()->getAsCXXRecordDecl());
}

/// \brief Find ambiguity among base classes.
static void
detectAmbiguousBases(SmallVectorImpl<MSRTTIClass> &Classes) {
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> VirtualBases;
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> UniqueBases;
  llvm::SmallPtrSet<const CXXRecordDecl *, 8> AmbiguousBases;
  for (MSRTTIClass *Class = &Classes.front(); Class <= &Classes.back();) {
    if ((Class->Flags & MSRTTIClass::IsVirtual) &&
        !VirtualBases.insert(Class->RD).second) {
      Class = MSRTTIClass::getNextChild(Class);
      continue;
    }
    if (!UniqueBases.insert(Class->RD).second)
      AmbiguousBases.insert(Class->RD);
    Class++;
  }
  if (AmbiguousBases.empty())
    return;
  for (MSRTTIClass &Class : Classes)
    if (AmbiguousBases.count(Class.RD))
      Class.Flags |= MSRTTIClass::IsAmbiguous;
}

llvm::GlobalVariable *MSRTTIBuilder::getClassHierarchyDescriptor() {
  SmallString<256> MangledName;
  {
    llvm::raw_svector_ostream Out(MangledName);
    ABI.getMangleContext().mangleCXXRTTIClassHierarchyDescriptor(RD, Out);
  }

  // Check to see if we've already declared this ClassHierarchyDescriptor.
  if (auto CHD = Module.getNamedGlobal(MangledName))
    return CHD;

  // Serialize the class hierarchy and initialize the CHD Fields.
  SmallVector<MSRTTIClass, 8> Classes;
  serializeClassHierarchy(Classes, RD);
  Classes.front().initialize(/*Parent=*/nullptr, /*Specifier=*/nullptr);
  detectAmbiguousBases(Classes);
  int Flags = 0;
  for (auto Class : Classes) {
    if (Class.RD->getNumBases() > 1)
      Flags |= HasBranchingHierarchy;
    // Note: cl.exe does not calculate "HasAmbiguousBases" correctly.  We
    // believe the field isn't actually used.
    if (Class.Flags & MSRTTIClass::IsAmbiguous)
      Flags |= HasAmbiguousBases;
  }
  if ((Flags & HasBranchingHierarchy) && RD->getNumVBases() != 0)
    Flags |= HasVirtualBranchingHierarchy;
  // These gep indices are used to get the address of the first element of the
  // base class array.
  llvm::Value *GEPIndices[] = {llvm::ConstantInt::get(CGM.IntTy, 0),
                               llvm::ConstantInt::get(CGM.IntTy, 0)};

  // Forward-declare the class hierarchy descriptor
  auto Type = ABI.getClassHierarchyDescriptorType();
  auto CHD = new llvm::GlobalVariable(Module, Type, /*Constant=*/true, Linkage,
                                      /*Initializer=*/nullptr,
                                      MangledName);
  if (CHD->isWeakForLinker())
    CHD->setComdat(CGM.getModule().getOrInsertComdat(CHD->getName()));

  auto *Bases = getBaseClassArray(Classes);

  // Initialize the base class ClassHierarchyDescriptor.
  llvm::Constant *Fields[] = {
      llvm::ConstantInt::get(CGM.IntTy, 0), // reserved by the runtime
      llvm::ConstantInt::get(CGM.IntTy, Flags),
      llvm::ConstantInt::get(CGM.IntTy, Classes.size()),
      ABI.getImageRelativeConstant(llvm::ConstantExpr::getInBoundsGetElementPtr(
          Bases->getValueType(), Bases,
          llvm::ArrayRef<llvm::Value *>(GEPIndices))),
  };
  CHD->setInitializer(llvm::ConstantStruct::get(Type, Fields));
  return CHD;
}

llvm::GlobalVariable *
MSRTTIBuilder::getBaseClassArray(SmallVectorImpl<MSRTTIClass> &Classes) {
  SmallString<256> MangledName;
  {
    llvm::raw_svector_ostream Out(MangledName);
    ABI.getMangleContext().mangleCXXRTTIBaseClassArray(RD, Out);
  }

  // Forward-declare the base class array.
  // cl.exe pads the base class array with 1 (in 32 bit mode) or 4 (in 64 bit
  // mode) bytes of padding.  We provide a pointer sized amount of padding by
  // adding +1 to Classes.size().  The sections have pointer alignment and are
  // marked pick-any so it shouldn't matter.
  llvm::Type *PtrType = ABI.getImageRelativeType(
      ABI.getBaseClassDescriptorType()->getPointerTo());
  auto *ArrType = llvm::ArrayType::get(PtrType, Classes.size() + 1);
  auto *BCA =
      new llvm::GlobalVariable(Module, ArrType,
                               /*Constant=*/true, Linkage,
                               /*Initializer=*/nullptr, MangledName);
  if (BCA->isWeakForLinker())
    BCA->setComdat(CGM.getModule().getOrInsertComdat(BCA->getName()));

  // Initialize the BaseClassArray.
  SmallVector<llvm::Constant *, 8> BaseClassArrayData;
  for (MSRTTIClass &Class : Classes)
    BaseClassArrayData.push_back(
        ABI.getImageRelativeConstant(getBaseClassDescriptor(Class)));
  BaseClassArrayData.push_back(llvm::Constant::getNullValue(PtrType));
  BCA->setInitializer(llvm::ConstantArray::get(ArrType, BaseClassArrayData));
  return BCA;
}

llvm::GlobalVariable *
MSRTTIBuilder::getBaseClassDescriptor(const MSRTTIClass &Class) {
  // Compute the fields for the BaseClassDescriptor.  They are computed up front
  // because they are mangled into the name of the object.
  uint32_t OffsetInVBTable = 0;
  int32_t VBPtrOffset = -1;
  if (Class.VirtualRoot) {
    auto &VTableContext = CGM.getMicrosoftVTableContext();
    OffsetInVBTable = VTableContext.getVBTableIndex(RD, Class.VirtualRoot) * 4;
    VBPtrOffset = Context.getASTRecordLayout(RD).getVBPtrOffset().getQuantity();
  }

  SmallString<256> MangledName;
  {
    llvm::raw_svector_ostream Out(MangledName);
    ABI.getMangleContext().mangleCXXRTTIBaseClassDescriptor(
        Class.RD, Class.OffsetInVBase, VBPtrOffset, OffsetInVBTable,
        Class.Flags, Out);
  }

  // Check to see if we've already declared this object.
  if (auto BCD = Module.getNamedGlobal(MangledName))
    return BCD;

  // Forward-declare the base class descriptor.
  auto Type = ABI.getBaseClassDescriptorType();
  auto BCD =
      new llvm::GlobalVariable(Module, Type, /*Constant=*/true, Linkage,
                               /*Initializer=*/nullptr, MangledName);
  if (BCD->isWeakForLinker())
    BCD->setComdat(CGM.getModule().getOrInsertComdat(BCD->getName()));

  // Initialize the BaseClassDescriptor.
  llvm::Constant *Fields[] = {
      ABI.getImageRelativeConstant(
          ABI.getAddrOfRTTIDescriptor(Context.getTypeDeclType(Class.RD))),
      llvm::ConstantInt::get(CGM.IntTy, Class.NumBases),
      llvm::ConstantInt::get(CGM.IntTy, Class.OffsetInVBase),
      llvm::ConstantInt::get(CGM.IntTy, VBPtrOffset),
      llvm::ConstantInt::get(CGM.IntTy, OffsetInVBTable),
      llvm::ConstantInt::get(CGM.IntTy, Class.Flags),
      ABI.getImageRelativeConstant(
          MSRTTIBuilder(ABI, Class.RD).getClassHierarchyDescriptor()),
  };
  BCD->setInitializer(llvm::ConstantStruct::get(Type, Fields));
  return BCD;
}

llvm::GlobalVariable *
MSRTTIBuilder::getCompleteObjectLocator(const VPtrInfo &Info) {
  SmallString<256> MangledName;
  {
    llvm::raw_svector_ostream Out(MangledName);
    ABI.getMangleContext().mangleCXXRTTICompleteObjectLocator(RD, Info.MangledPath, Out);
  }

  // Check to see if we've already computed this complete object locator.
  if (auto COL = Module.getNamedGlobal(MangledName))
    return COL;

  // Compute the fields of the complete object locator.
  int OffsetToTop = Info.FullOffsetInMDC.getQuantity();
  int VFPtrOffset = 0;
  // The offset includes the vtordisp if one exists.
  if (const CXXRecordDecl *VBase = Info.getVBaseWithVPtr())
    if (Context.getASTRecordLayout(RD)
      .getVBaseOffsetsMap()
      .find(VBase)
      ->second.hasVtorDisp())
      VFPtrOffset = Info.NonVirtualOffset.getQuantity() + 4;

  // Forward-declare the complete object locator.
  llvm::StructType *Type = ABI.getCompleteObjectLocatorType();
  auto COL = new llvm::GlobalVariable(Module, Type, /*Constant=*/true, Linkage,
    /*Initializer=*/nullptr, MangledName);

  // Initialize the CompleteObjectLocator.
  llvm::Constant *Fields[] = {
      llvm::ConstantInt::get(CGM.IntTy, ABI.isImageRelative()),
      llvm::ConstantInt::get(CGM.IntTy, OffsetToTop),
      llvm::ConstantInt::get(CGM.IntTy, VFPtrOffset),
      ABI.getImageRelativeConstant(
          CGM.GetAddrOfRTTIDescriptor(Context.getTypeDeclType(RD))),
      ABI.getImageRelativeConstant(getClassHierarchyDescriptor()),
      ABI.getImageRelativeConstant(COL),
  };
  llvm::ArrayRef<llvm::Constant *> FieldsRef(Fields);
  if (!ABI.isImageRelative())
    FieldsRef = FieldsRef.drop_back();
  COL->setInitializer(llvm::ConstantStruct::get(Type, FieldsRef));
  if (COL->isWeakForLinker())
    COL->setComdat(CGM.getModule().getOrInsertComdat(COL->getName()));
  return COL;
}

static QualType decomposeTypeForEH(ASTContext &Context, QualType T,
                                   bool &IsConst, bool &IsVolatile,
                                   bool &IsUnaligned) {
  T = Context.getExceptionObjectType(T);

  // C++14 [except.handle]p3:
  //   A handler is a match for an exception object of type E if [...]
  //     - the handler is of type cv T or const T& where T is a pointer type and
  //       E is a pointer type that can be converted to T by [...]
  //         - a qualification conversion
  IsConst = false;
  IsVolatile = false;
  IsUnaligned = false;
  QualType PointeeType = T->getPointeeType();
  if (!PointeeType.isNull()) {
    IsConst = PointeeType.isConstQualified();
    IsVolatile = PointeeType.isVolatileQualified();
    IsUnaligned = PointeeType.getQualifiers().hasUnaligned();
  }

  // Member pointer types like "const int A::*" are represented by having RTTI
  // for "int A::*" and separately storing the const qualifier.
  if (const auto *MPTy = T->getAs<MemberPointerType>())
    T = Context.getMemberPointerType(PointeeType.getUnqualifiedType(),
                                     MPTy->getClass());

  // Pointer types like "const int * const *" are represented by having RTTI
  // for "const int **" and separately storing the const qualifier.
  if (T->isPointerType())
    T = Context.getPointerType(PointeeType.getUnqualifiedType());

  return T;
}

CatchTypeInfo
MicrosoftCXXABI::getAddrOfCXXCatchHandlerType(QualType Type,
                                              QualType CatchHandlerType) {
  // TypeDescriptors for exceptions never have qualified pointer types,
  // qualifiers are stored separately in order to support qualification
  // conversions.
  bool IsConst, IsVolatile, IsUnaligned;
  Type =
      decomposeTypeForEH(getContext(), Type, IsConst, IsVolatile, IsUnaligned);

  bool IsReference = CatchHandlerType->isReferenceType();

  uint32_t Flags = 0;
  if (IsConst)
    Flags |= 1;
  if (IsVolatile)
    Flags |= 2;
  if (IsUnaligned)
    Flags |= 4;
  if (IsReference)
    Flags |= 8;

  return CatchTypeInfo{getAddrOfRTTIDescriptor(Type)->stripPointerCasts(),
                       Flags};
}

/// \brief Gets a TypeDescriptor.  Returns a llvm::Constant * rather than a
/// llvm::GlobalVariable * because different type descriptors have different
/// types, and need to be abstracted.  They are abstracting by casting the
/// address to an Int8PtrTy.
llvm::Constant *MicrosoftCXXABI::getAddrOfRTTIDescriptor(QualType Type) {
  SmallString<256> MangledName;
  {
    llvm::raw_svector_ostream Out(MangledName);
    getMangleContext().mangleCXXRTTI(Type, Out);
  }

  // Check to see if we've already declared this TypeDescriptor.
  if (llvm::GlobalVariable *GV = CGM.getModule().getNamedGlobal(MangledName))
    return llvm::ConstantExpr::getBitCast(GV, CGM.Int8PtrTy);

  // Compute the fields for the TypeDescriptor.
  SmallString<256> TypeInfoString;
  {
    llvm::raw_svector_ostream Out(TypeInfoString);
    getMangleContext().mangleCXXRTTIName(Type, Out);
  }

  // Declare and initialize the TypeDescriptor.
  llvm::Constant *Fields[] = {
    getTypeInfoVTable(CGM),                        // VFPtr
    llvm::ConstantPointerNull::get(CGM.Int8PtrTy), // Runtime data
    llvm::ConstantDataArray::getString(CGM.getLLVMContext(), TypeInfoString)};
  llvm::StructType *TypeDescriptorType =
      getTypeDescriptorType(TypeInfoString);
  auto *Var = new llvm::GlobalVariable(
      CGM.getModule(), TypeDescriptorType, /*Constant=*/false,
      getLinkageForRTTI(Type),
      llvm::ConstantStruct::get(TypeDescriptorType, Fields),
      MangledName);
  if (Var->isWeakForLinker())
    Var->setComdat(CGM.getModule().getOrInsertComdat(Var->getName()));
  return llvm::ConstantExpr::getBitCast(Var, CGM.Int8PtrTy);
}

/// \brief Gets or a creates a Microsoft CompleteObjectLocator.
llvm::GlobalVariable *
MicrosoftCXXABI::getMSCompleteObjectLocator(const CXXRecordDecl *RD,
                                            const VPtrInfo &Info) {
  return MSRTTIBuilder(*this, RD).getCompleteObjectLocator(Info);
}

static void emitCXXConstructor(CodeGenModule &CGM,
                               const CXXConstructorDecl *ctor,
                               StructorType ctorType) {
  // There are no constructor variants, always emit the complete destructor.
  llvm::Function *Fn = CGM.codegenCXXStructor(ctor, StructorType::Complete);
  CGM.maybeSetTrivialComdat(*ctor, *Fn);
}

static void emitCXXDestructor(CodeGenModule &CGM, const CXXDestructorDecl *dtor,
                              StructorType dtorType) {
  // The complete destructor is equivalent to the base destructor for
  // classes with no virtual bases, so try to emit it as an alias.
  if (!dtor->getParent()->getNumVBases() &&
      (dtorType == StructorType::Complete || dtorType == StructorType::Base)) {
    bool ProducedAlias = !CGM.TryEmitDefinitionAsAlias(
        GlobalDecl(dtor, Dtor_Complete), GlobalDecl(dtor, Dtor_Base), true);
    if (ProducedAlias) {
      if (dtorType == StructorType::Complete)
        return;
      if (dtor->isVirtual())
        CGM.getVTables().EmitThunks(GlobalDecl(dtor, Dtor_Complete));
    }
  }

  // The base destructor is equivalent to the base destructor of its
  // base class if there is exactly one non-virtual base class with a
  // non-trivial destructor, there are no fields with a non-trivial
  // destructor, and the body of the destructor is trivial.
  if (dtorType == StructorType::Base && !CGM.TryEmitBaseDestructorAsAlias(dtor))
    return;

  llvm::Function *Fn = CGM.codegenCXXStructor(dtor, dtorType);
  if (Fn->isWeakForLinker())
    Fn->setComdat(CGM.getModule().getOrInsertComdat(Fn->getName()));
}

void MicrosoftCXXABI::emitCXXStructor(const CXXMethodDecl *MD,
                                      StructorType Type) {
  if (auto *CD = dyn_cast<CXXConstructorDecl>(MD)) {
    emitCXXConstructor(CGM, CD, Type);
    return;
  }
  emitCXXDestructor(CGM, cast<CXXDestructorDecl>(MD), Type);
}

llvm::Function *
MicrosoftCXXABI::getAddrOfCXXCtorClosure(const CXXConstructorDecl *CD,
                                         CXXCtorType CT) {
  assert(CT == Ctor_CopyingClosure || CT == Ctor_DefaultClosure);

  // Calculate the mangled name.
  SmallString<256> ThunkName;
  llvm::raw_svector_ostream Out(ThunkName);
  getMangleContext().mangleCXXCtor(CD, CT, Out);

  // If the thunk has been generated previously, just return it.
  if (llvm::GlobalValue *GV = CGM.getModule().getNamedValue(ThunkName))
    return cast<llvm::Function>(GV);

  // Create the llvm::Function.
  const CGFunctionInfo &FnInfo = CGM.getTypes().arrangeMSCtorClosure(CD, CT);
  llvm::FunctionType *ThunkTy = CGM.getTypes().GetFunctionType(FnInfo);
  const CXXRecordDecl *RD = CD->getParent();
  QualType RecordTy = getContext().getRecordType(RD);
  llvm::Function *ThunkFn = llvm::Function::Create(
      ThunkTy, getLinkageForRTTI(RecordTy), ThunkName.str(), &CGM.getModule());
  ThunkFn->setCallingConv(static_cast<llvm::CallingConv::ID>(
      FnInfo.getEffectiveCallingConvention()));
  if (ThunkFn->isWeakForLinker())
    ThunkFn->setComdat(CGM.getModule().getOrInsertComdat(ThunkFn->getName()));
  bool IsCopy = CT == Ctor_CopyingClosure;

  // Start codegen.
  CodeGenFunction CGF(CGM);
  CGF.CurGD = GlobalDecl(CD, Ctor_Complete);

  // Build FunctionArgs.
  FunctionArgList FunctionArgs;

  // A constructor always starts with a 'this' pointer as its first argument.
  buildThisParam(CGF, FunctionArgs);

  // Following the 'this' pointer is a reference to the source object that we
  // are copying from.
  ImplicitParamDecl SrcParam(
      getContext(), nullptr, SourceLocation(), &getContext().Idents.get("src"),
      getContext().getLValueReferenceType(RecordTy,
                                          /*SpelledAsLValue=*/true));
  if (IsCopy)
    FunctionArgs.push_back(&SrcParam);

  // Constructors for classes which utilize virtual bases have an additional
  // parameter which indicates whether or not it is being delegated to by a more
  // derived constructor.
  ImplicitParamDecl IsMostDerived(getContext(), nullptr, SourceLocation(),
                                  &getContext().Idents.get("is_most_derived"),
                                  getContext().IntTy);
  // Only add the parameter to the list if thie class has virtual bases.
  if (RD->getNumVBases() > 0)
    FunctionArgs.push_back(&IsMostDerived);

  // Start defining the function.
  auto NL = ApplyDebugLocation::CreateEmpty(CGF);
  CGF.StartFunction(GlobalDecl(), FnInfo.getReturnType(), ThunkFn, FnInfo,
                    FunctionArgs, CD->getLocation(), SourceLocation());
  // Create a scope with an artificial location for the body of this function.
  auto AL = ApplyDebugLocation::CreateArtificial(CGF);
  EmitThisParam(CGF);
  llvm::Value *This = getThisValue(CGF);

  llvm::Value *SrcVal =
      IsCopy ? CGF.Builder.CreateLoad(CGF.GetAddrOfLocalVar(&SrcParam), "src")
             : nullptr;

  CallArgList Args;

  // Push the this ptr.
  Args.add(RValue::get(This), CD->getThisType(getContext()));

  // Push the src ptr.
  if (SrcVal)
    Args.add(RValue::get(SrcVal), SrcParam.getType());

  // Add the rest of the default arguments.
  SmallVector<const Stmt *, 4> ArgVec;
  ArrayRef<ParmVarDecl *> params = CD->parameters().drop_front(IsCopy ? 1 : 0);
  for (const ParmVarDecl *PD : params) {
    assert(PD->hasDefaultArg() && "ctor closure lacks default args");
    ArgVec.push_back(PD->getDefaultArg());
  }

  CodeGenFunction::RunCleanupsScope Cleanups(CGF);

  const auto *FPT = CD->getType()->castAs<FunctionProtoType>();
  CGF.EmitCallArgs(Args, FPT, llvm::makeArrayRef(ArgVec), CD, IsCopy ? 1 : 0);

  // Insert any ABI-specific implicit constructor arguments.
  AddedStructorArgs ExtraArgs =
      addImplicitConstructorArgs(CGF, CD, Ctor_Complete,
                                 /*ForVirtualBase=*/false,
                                 /*Delegating=*/false, Args);
  // Call the destructor with our arguments.
  llvm::Constant *CalleePtr =
    CGM.getAddrOfCXXStructor(CD, StructorType::Complete);
  CGCallee Callee = CGCallee::forDirect(CalleePtr, CD);
  const CGFunctionInfo &CalleeInfo = CGM.getTypes().arrangeCXXConstructorCall(
      Args, CD, Ctor_Complete, ExtraArgs.Prefix, ExtraArgs.Suffix);
  CGF.EmitCall(CalleeInfo, Callee, ReturnValueSlot(), Args);

  Cleanups.ForceCleanup();

  // Emit the ret instruction, remove any temporary instructions created for the
  // aid of CodeGen.
  CGF.FinishFunction(SourceLocation());

  return ThunkFn;
}

llvm::Constant *MicrosoftCXXABI::getCatchableType(QualType T,
                                                  uint32_t NVOffset,
                                                  int32_t VBPtrOffset,
                                                  uint32_t VBIndex) {
  assert(!T->isReferenceType());

  CXXRecordDecl *RD = T->getAsCXXRecordDecl();
  const CXXConstructorDecl *CD =
      RD ? CGM.getContext().getCopyConstructorForExceptionObject(RD) : nullptr;
  CXXCtorType CT = Ctor_Complete;
  if (CD)
    if (!hasDefaultCXXMethodCC(getContext(), CD) || CD->getNumParams() != 1)
      CT = Ctor_CopyingClosure;

  uint32_t Size = getContext().getTypeSizeInChars(T).getQuantity();
  SmallString<256> MangledName;
  {
    llvm::raw_svector_ostream Out(MangledName);
    getMangleContext().mangleCXXCatchableType(T, CD, CT, Size, NVOffset,
                                              VBPtrOffset, VBIndex, Out);
  }
  if (llvm::GlobalVariable *GV = CGM.getModule().getNamedGlobal(MangledName))
    return getImageRelativeConstant(GV);

  // The TypeDescriptor is used by the runtime to determine if a catch handler
  // is appropriate for the exception object.
  llvm::Constant *TD = getImageRelativeConstant(getAddrOfRTTIDescriptor(T));

  // The runtime is responsible for calling the copy constructor if the
  // exception is caught by value.
  llvm::Constant *CopyCtor;
  if (CD) {
    if (CT == Ctor_CopyingClosure)
      CopyCtor = getAddrOfCXXCtorClosure(CD, Ctor_CopyingClosure);
    else
      CopyCtor = CGM.getAddrOfCXXStructor(CD, StructorType::Complete);

    CopyCtor = llvm::ConstantExpr::getBitCast(CopyCtor, CGM.Int8PtrTy);
  } else {
    CopyCtor = llvm::Constant::getNullValue(CGM.Int8PtrTy);
  }
  CopyCtor = getImageRelativeConstant(CopyCtor);

  bool IsScalar = !RD;
  bool HasVirtualBases = false;
  bool IsStdBadAlloc = false; // std::bad_alloc is special for some reason.
  QualType PointeeType = T;
  if (T->isPointerType())
    PointeeType = T->getPointeeType();
  if (const CXXRecordDecl *RD = PointeeType->getAsCXXRecordDecl()) {
    HasVirtualBases = RD->getNumVBases() > 0;
    if (IdentifierInfo *II = RD->getIdentifier())
      IsStdBadAlloc = II->isStr("bad_alloc") && RD->isInStdNamespace();
  }

  // Encode the relevant CatchableType properties into the Flags bitfield.
  // FIXME: Figure out how bits 2 or 8 can get set.
  uint32_t Flags = 0;
  if (IsScalar)
    Flags |= 1;
  if (HasVirtualBases)
    Flags |= 4;
  if (IsStdBadAlloc)
    Flags |= 16;

  llvm::Constant *Fields[] = {
      llvm::ConstantInt::get(CGM.IntTy, Flags),       // Flags
      TD,                                             // TypeDescriptor
      llvm::ConstantInt::get(CGM.IntTy, NVOffset),    // NonVirtualAdjustment
      llvm::ConstantInt::get(CGM.IntTy, VBPtrOffset), // OffsetToVBPtr
      llvm::ConstantInt::get(CGM.IntTy, VBIndex),     // VBTableIndex
      llvm::ConstantInt::get(CGM.IntTy, Size),        // Size
      CopyCtor                                        // CopyCtor
  };
  llvm::StructType *CTType = getCatchableTypeType();
  auto *GV = new llvm::GlobalVariable(
      CGM.getModule(), CTType, /*Constant=*/true, getLinkageForRTTI(T),
      llvm::ConstantStruct::get(CTType, Fields), MangledName);
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  GV->setSection(".xdata");
  if (GV->isWeakForLinker())
    GV->setComdat(CGM.getModule().getOrInsertComdat(GV->getName()));
  return getImageRelativeConstant(GV);
}

llvm::GlobalVariable *MicrosoftCXXABI::getCatchableTypeArray(QualType T) {
  assert(!T->isReferenceType());

  // See if we've already generated a CatchableTypeArray for this type before.
  llvm::GlobalVariable *&CTA = CatchableTypeArrays[T];
  if (CTA)
    return CTA;

  // Ensure that we don't have duplicate entries in our CatchableTypeArray by
  // using a SmallSetVector.  Duplicates may arise due to virtual bases
  // occurring more than once in the hierarchy.
  llvm::SmallSetVector<llvm::Constant *, 2> CatchableTypes;

  // C++14 [except.handle]p3:
  //   A handler is a match for an exception object of type E if [...]
  //     - the handler is of type cv T or cv T& and T is an unambiguous public
  //       base class of E, or
  //     - the handler is of type cv T or const T& where T is a pointer type and
  //       E is a pointer type that can be converted to T by [...]
  //         - a standard pointer conversion (4.10) not involving conversions to
  //           pointers to private or protected or ambiguous classes
  const CXXRecordDecl *MostDerivedClass = nullptr;
  bool IsPointer = T->isPointerType();
  if (IsPointer)
    MostDerivedClass = T->getPointeeType()->getAsCXXRecordDecl();
  else
    MostDerivedClass = T->getAsCXXRecordDecl();

  // Collect all the unambiguous public bases of the MostDerivedClass.
  if (MostDerivedClass) {
    const ASTContext &Context = getContext();
    const ASTRecordLayout &MostDerivedLayout =
        Context.getASTRecordLayout(MostDerivedClass);
    MicrosoftVTableContext &VTableContext = CGM.getMicrosoftVTableContext();
    SmallVector<MSRTTIClass, 8> Classes;
    serializeClassHierarchy(Classes, MostDerivedClass);
    Classes.front().initialize(/*Parent=*/nullptr, /*Specifier=*/nullptr);
    detectAmbiguousBases(Classes);
    for (const MSRTTIClass &Class : Classes) {
      // Skip any ambiguous or private bases.
      if (Class.Flags &
          (MSRTTIClass::IsPrivateOnPath | MSRTTIClass::IsAmbiguous))
        continue;
      // Write down how to convert from a derived pointer to a base pointer.
      uint32_t OffsetInVBTable = 0;
      int32_t VBPtrOffset = -1;
      if (Class.VirtualRoot) {
        OffsetInVBTable =
          VTableContext.getVBTableIndex(MostDerivedClass, Class.VirtualRoot)*4;
        VBPtrOffset = MostDerivedLayout.getVBPtrOffset().getQuantity();
      }

      // Turn our record back into a pointer if the exception object is a
      // pointer.
      QualType RTTITy = QualType(Class.RD->getTypeForDecl(), 0);
      if (IsPointer)
        RTTITy = Context.getPointerType(RTTITy);
      CatchableTypes.insert(getCatchableType(RTTITy, Class.OffsetInVBase,
                                             VBPtrOffset, OffsetInVBTable));
    }
  }

  // C++14 [except.handle]p3:
  //   A handler is a match for an exception object of type E if
  //     - The handler is of type cv T or cv T& and E and T are the same type
  //       (ignoring the top-level cv-qualifiers)
  CatchableTypes.insert(getCatchableType(T));

  // C++14 [except.handle]p3:
  //   A handler is a match for an exception object of type E if
  //     - the handler is of type cv T or const T& where T is a pointer type and
  //       E is a pointer type that can be converted to T by [...]
  //         - a standard pointer conversion (4.10) not involving conversions to
  //           pointers to private or protected or ambiguous classes
  //
  // C++14 [conv.ptr]p2:
  //   A prvalue of type "pointer to cv T," where T is an object type, can be
  //   converted to a prvalue of type "pointer to cv void".
  if (IsPointer && T->getPointeeType()->isObjectType())
    CatchableTypes.insert(getCatchableType(getContext().VoidPtrTy));

  // C++14 [except.handle]p3:
  //   A handler is a match for an exception object of type E if [...]
  //     - the handler is of type cv T or const T& where T is a pointer or
  //       pointer to member type and E is std::nullptr_t.
  //
  // We cannot possibly list all possible pointer types here, making this
  // implementation incompatible with the standard.  However, MSVC includes an
  // entry for pointer-to-void in this case.  Let's do the same.
  if (T->isNullPtrType())
    CatchableTypes.insert(getCatchableType(getContext().VoidPtrTy));

  uint32_t NumEntries = CatchableTypes.size();
  llvm::Type *CTType =
      getImageRelativeType(getCatchableTypeType()->getPointerTo());
  llvm::ArrayType *AT = llvm::ArrayType::get(CTType, NumEntries);
  llvm::StructType *CTAType = getCatchableTypeArrayType(NumEntries);
  llvm::Constant *Fields[] = {
      llvm::ConstantInt::get(CGM.IntTy, NumEntries),    // NumEntries
      llvm::ConstantArray::get(
          AT, llvm::makeArrayRef(CatchableTypes.begin(),
                                 CatchableTypes.end())) // CatchableTypes
  };
  SmallString<256> MangledName;
  {
    llvm::raw_svector_ostream Out(MangledName);
    getMangleContext().mangleCXXCatchableTypeArray(T, NumEntries, Out);
  }
  CTA = new llvm::GlobalVariable(
      CGM.getModule(), CTAType, /*Constant=*/true, getLinkageForRTTI(T),
      llvm::ConstantStruct::get(CTAType, Fields), MangledName);
  CTA->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  CTA->setSection(".xdata");
  if (CTA->isWeakForLinker())
    CTA->setComdat(CGM.getModule().getOrInsertComdat(CTA->getName()));
  return CTA;
}

llvm::GlobalVariable *MicrosoftCXXABI::getThrowInfo(QualType T) {
  bool IsConst, IsVolatile, IsUnaligned;
  T = decomposeTypeForEH(getContext(), T, IsConst, IsVolatile, IsUnaligned);

  // The CatchableTypeArray enumerates the various (CV-unqualified) types that
  // the exception object may be caught as.
  llvm::GlobalVariable *CTA = getCatchableTypeArray(T);
  // The first field in a CatchableTypeArray is the number of CatchableTypes.
  // This is used as a component of the mangled name which means that we need to
  // know what it is in order to see if we have previously generated the
  // ThrowInfo.
  uint32_t NumEntries =
      cast<llvm::ConstantInt>(CTA->getInitializer()->getAggregateElement(0U))
          ->getLimitedValue();

  SmallString<256> MangledName;
  {
    llvm::raw_svector_ostream Out(MangledName);
    getMangleContext().mangleCXXThrowInfo(T, IsConst, IsVolatile, IsUnaligned,
                                          NumEntries, Out);
  }

  // Reuse a previously generated ThrowInfo if we have generated an appropriate
  // one before.
  if (llvm::GlobalVariable *GV = CGM.getModule().getNamedGlobal(MangledName))
    return GV;

  // The RTTI TypeDescriptor uses an unqualified type but catch clauses must
  // be at least as CV qualified.  Encode this requirement into the Flags
  // bitfield.
  uint32_t Flags = 0;
  if (IsConst)
    Flags |= 1;
  if (IsVolatile)
    Flags |= 2;
  if (IsUnaligned)
    Flags |= 4;

  // The cleanup-function (a destructor) must be called when the exception
  // object's lifetime ends.
  llvm::Constant *CleanupFn = llvm::Constant::getNullValue(CGM.Int8PtrTy);
  if (const CXXRecordDecl *RD = T->getAsCXXRecordDecl())
    if (CXXDestructorDecl *DtorD = RD->getDestructor())
      if (!DtorD->isTrivial())
        CleanupFn = llvm::ConstantExpr::getBitCast(
            CGM.getAddrOfCXXStructor(DtorD, StructorType::Complete),
            CGM.Int8PtrTy);
  // This is unused as far as we can tell, initialize it to null.
  llvm::Constant *ForwardCompat =
      getImageRelativeConstant(llvm::Constant::getNullValue(CGM.Int8PtrTy));
  llvm::Constant *PointerToCatchableTypes = getImageRelativeConstant(
      llvm::ConstantExpr::getBitCast(CTA, CGM.Int8PtrTy));
  llvm::StructType *TIType = getThrowInfoType();
  llvm::Constant *Fields[] = {
      llvm::ConstantInt::get(CGM.IntTy, Flags), // Flags
      getImageRelativeConstant(CleanupFn),      // CleanupFn
      ForwardCompat,                            // ForwardCompat
      PointerToCatchableTypes                   // CatchableTypeArray
  };
  auto *GV = new llvm::GlobalVariable(
      CGM.getModule(), TIType, /*Constant=*/true, getLinkageForRTTI(T),
      llvm::ConstantStruct::get(TIType, Fields), StringRef(MangledName));
  GV->setUnnamedAddr(llvm::GlobalValue::UnnamedAddr::Global);
  GV->setSection(".xdata");
  if (GV->isWeakForLinker())
    GV->setComdat(CGM.getModule().getOrInsertComdat(GV->getName()));
  return GV;
}

void MicrosoftCXXABI::emitThrow(CodeGenFunction &CGF, const CXXThrowExpr *E) {
  const Expr *SubExpr = E->getSubExpr();
  QualType ThrowType = SubExpr->getType();
  // The exception object lives on the stack and it's address is passed to the
  // runtime function.
  Address AI = CGF.CreateMemTemp(ThrowType);
  CGF.EmitAnyExprToMem(SubExpr, AI, ThrowType.getQualifiers(),
                       /*IsInit=*/true);

  // The so-called ThrowInfo is used to describe how the exception object may be
  // caught.
  llvm::GlobalVariable *TI = getThrowInfo(ThrowType);

  // Call into the runtime to throw the exception.
  llvm::Value *Args[] = {
    CGF.Builder.CreateBitCast(AI.getPointer(), CGM.Int8PtrTy),
    TI
  };
  CGF.EmitNoreturnRuntimeCallOrInvoke(getThrowFn(), Args);
}
