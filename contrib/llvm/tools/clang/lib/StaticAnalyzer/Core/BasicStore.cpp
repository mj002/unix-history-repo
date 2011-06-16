//== BasicStore.cpp - Basic map from Locations to Values --------*- C++ -*--==//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file defined the BasicStore and BasicStoreManager classes.
//
//===----------------------------------------------------------------------===//

#include "clang/AST/DeclCXX.h"
#include "clang/AST/ExprObjC.h"
#include "clang/Analysis/Analyses/LiveVariables.h"
#include "clang/Analysis/AnalysisContext.h"
#include "clang/StaticAnalyzer/Core/PathSensitive/GRState.h"
#include "llvm/ADT/ImmutableMap.h"

using namespace clang;
using namespace ento;

typedef llvm::ImmutableMap<const MemRegion*,SVal> BindingsTy;

namespace {

class BasicStoreSubRegionMap : public SubRegionMap {
public:
  BasicStoreSubRegionMap() {}

  bool iterSubRegions(const MemRegion* R, Visitor& V) const {
    return true; // Do nothing.  No subregions.
  }
};

class BasicStoreManager : public StoreManager {
  BindingsTy::Factory VBFactory;
public:
  BasicStoreManager(GRStateManager& mgr)
    : StoreManager(mgr), VBFactory(mgr.getAllocator()) {}

  ~BasicStoreManager() {}

  SubRegionMap *getSubRegionMap(Store store) {
    return new BasicStoreSubRegionMap();
  }

  SVal Retrieve(Store store, Loc loc, QualType T = QualType());

  StoreRef invalidateRegion(Store store, const MemRegion *R, const Expr *E,
                            unsigned Count, InvalidatedSymbols &IS);

  StoreRef invalidateRegions(Store store, const MemRegion * const *Begin,
                             const MemRegion * const *End, const Expr *E,
                             unsigned Count, InvalidatedSymbols &IS,
                             bool invalidateGlobals,
                             InvalidatedRegions *Regions);

  StoreRef scanForIvars(Stmt *B, const Decl* SelfDecl,
                        const MemRegion *SelfRegion, Store St);

  StoreRef Bind(Store St, Loc loc, SVal V);
  StoreRef Remove(Store St, Loc loc);
  StoreRef getInitialStore(const LocationContext *InitLoc);

  StoreRef BindCompoundLiteral(Store store, const CompoundLiteralExpr*,
                            const LocationContext*, SVal val) {
    return StoreRef(store, *this);
  }

  /// ArrayToPointer - Used by ExprEngine::VistCast to handle implicit
  ///  conversions between arrays and pointers.
  SVal ArrayToPointer(Loc Array) { return Array; }

  /// removeDeadBindings - Scans a BasicStore of 'state' for dead values.
  ///  It updatees the GRState object in place with the values removed.
  StoreRef removeDeadBindings(Store store, const StackFrameContext *LCtx,
                              SymbolReaper& SymReaper,
                          llvm::SmallVectorImpl<const MemRegion*>& RegionRoots);

  void iterBindings(Store store, BindingsHandler& f);

  StoreRef BindDecl(Store store, const VarRegion *VR, SVal InitVal) {
    return BindDeclInternal(store, VR, &InitVal);
  }

  StoreRef BindDeclWithNoInit(Store store, const VarRegion *VR) {
    return BindDeclInternal(store, VR, 0);
  }

  StoreRef BindDeclInternal(Store store, const VarRegion *VR, SVal *InitVal);

  static inline BindingsTy GetBindings(Store store) {
    return BindingsTy(static_cast<const BindingsTy::TreeTy*>(store));
  }

  void print(Store store, llvm::raw_ostream& Out, const char* nl,
             const char *sep);

private:
  SVal LazyRetrieve(Store store, const TypedRegion *R);
};

} // end anonymous namespace


StoreManager* ento::CreateBasicStoreManager(GRStateManager& StMgr) {
  return new BasicStoreManager(StMgr);
}

static bool isHigherOrderRawPtr(QualType T, ASTContext &C) {
  bool foundPointer = false;
  while (1) {
    const PointerType *PT = T->getAs<PointerType>();
    if (!PT) {
      if (!foundPointer)
        return false;

      // intptr_t* or intptr_t**, etc?
      if (T->isIntegerType() && C.getTypeSize(T) == C.getTypeSize(C.VoidPtrTy))
        return true;

      QualType X = C.getCanonicalType(T).getUnqualifiedType();
      return X == C.VoidTy;
    }

    foundPointer = true;
    T = PT->getPointeeType();
  }
}

SVal BasicStoreManager::LazyRetrieve(Store store, const TypedRegion *R) {
  const VarRegion *VR = dyn_cast<VarRegion>(R);
  if (!VR)
    return UnknownVal();

  const VarDecl *VD = VR->getDecl();
  QualType T = VD->getType();

  // Only handle simple types that we can symbolicate.
  if (!SymbolManager::canSymbolicate(T) || !T->isScalarType())
    return UnknownVal();

  // Globals and parameters start with symbolic values.
  // Local variables initially are undefined.

  // Non-static globals may have had their values reset by invalidateRegions.
  const MemSpaceRegion *MS = VR->getMemorySpace();
  if (isa<NonStaticGlobalSpaceRegion>(MS)) {
    BindingsTy B = GetBindings(store);
    // FIXME: Copy-and-pasted from RegionStore.cpp.
    if (BindingsTy::data_type *Val = B.lookup(MS)) {
      if (SymbolRef parentSym = Val->getAsSymbol())
        return svalBuilder.getDerivedRegionValueSymbolVal(parentSym, R);

      if (Val->isZeroConstant())
        return svalBuilder.makeZeroVal(T);

      if (Val->isUnknownOrUndef())
        return *Val;

      assert(0 && "Unknown default value.");
    }
  }

  if (VR->hasGlobalsOrParametersStorage() ||
      isa<UnknownSpaceRegion>(VR->getMemorySpace()))
    return svalBuilder.getRegionValueSymbolVal(R);

  return UndefinedVal();
}

SVal BasicStoreManager::Retrieve(Store store, Loc loc, QualType T) {
  if (isa<UnknownVal>(loc))
    return UnknownVal();

  assert(!isa<UndefinedVal>(loc));

  switch (loc.getSubKind()) {

    case loc::MemRegionKind: {
      const MemRegion* R = cast<loc::MemRegionVal>(loc).getRegion();

      if (!(isa<VarRegion>(R) || isa<ObjCIvarRegion>(R) ||
          isa<CXXThisRegion>(R)))
        return UnknownVal();

      BindingsTy B = GetBindings(store);
      BindingsTy::data_type *Val = B.lookup(R);
      const TypedRegion *TR = cast<TypedRegion>(R);

      if (Val)
        return CastRetrievedVal(*Val, TR, T);

      SVal V = LazyRetrieve(store, TR);
      return V.isUnknownOrUndef() ? V : CastRetrievedVal(V, TR, T);
    }

    case loc::ObjCPropRefKind:
    case loc::ConcreteIntKind:
      // Support direct accesses to memory.  It's up to individual checkers
      // to flag an error.
      return UnknownVal();

    default:
      assert (false && "Invalid Loc.");
      break;
  }

  return UnknownVal();
}

StoreRef BasicStoreManager::Bind(Store store, Loc loc, SVal V) {
  if (isa<loc::ConcreteInt>(loc))
    return StoreRef(store, *this);

  const MemRegion* R = cast<loc::MemRegionVal>(loc).getRegion();

  // Special case: a default symbol assigned to the NonStaticGlobalsSpaceRegion
  //  that is used to derive other symbols.
  if (isa<NonStaticGlobalSpaceRegion>(R)) {
    BindingsTy B = GetBindings(store);
    return StoreRef(VBFactory.add(B, R, V).getRoot(), *this);
  }

  // Special case: handle store of pointer values (Loc) to pointers via
  // a cast to intXX_t*, void*, etc.  This is needed to handle
  // OSCompareAndSwap32Barrier/OSCompareAndSwap64Barrier.
  if (isa<Loc>(V) || isa<nonloc::LocAsInteger>(V))
    if (const ElementRegion *ER = dyn_cast<ElementRegion>(R)) {
      // FIXME: Should check for index 0.
      QualType T = ER->getLocationType();

      if (isHigherOrderRawPtr(T, Ctx))
        R = ER->getSuperRegion();
    }

  if (!(isa<VarRegion>(R) || isa<ObjCIvarRegion>(R) || isa<CXXThisRegion>(R)))
    return StoreRef(store, *this);

  const TypedRegion *TyR = cast<TypedRegion>(R);

  // Do not bind to arrays.  We need to explicitly check for this so that
  // we do not encounter any weirdness of trying to load/store from arrays.
  if (TyR->isBoundable() && TyR->getValueType()->isArrayType())
    return StoreRef(store, *this);

  if (nonloc::LocAsInteger *X = dyn_cast<nonloc::LocAsInteger>(&V)) {
    // Only convert 'V' to a location iff the underlying region type
    // is a location as well.
    // FIXME: We are allowing a store of an arbitrary location to
    // a pointer.  We may wish to flag a type error here if the types
    // are incompatible.  This may also cause lots of breakage
    // elsewhere. Food for thought.
    if (TyR->isBoundable() && Loc::isLocType(TyR->getValueType()))
      V = X->getLoc();
  }

  BindingsTy B = GetBindings(store);
  return StoreRef(V.isUnknown()
                    ? VBFactory.remove(B, R).getRoot()
                    : VBFactory.add(B, R, V).getRoot(), *this);
}

StoreRef BasicStoreManager::Remove(Store store, Loc loc) {
  switch (loc.getSubKind()) {
    case loc::MemRegionKind: {
      const MemRegion* R = cast<loc::MemRegionVal>(loc).getRegion();

      if (!(isa<VarRegion>(R) || isa<ObjCIvarRegion>(R) ||
          isa<CXXThisRegion>(R)))
        return StoreRef(store, *this);

      return StoreRef(VBFactory.remove(GetBindings(store), R).getRoot(), *this);
    }
    default:
      assert ("Remove for given Loc type not yet implemented.");
      return StoreRef(store, *this);
  }
}

StoreRef BasicStoreManager::removeDeadBindings(Store store,
                                               const StackFrameContext *LCtx,
                                               SymbolReaper& SymReaper,
                           llvm::SmallVectorImpl<const MemRegion*>& RegionRoots)
{
  BindingsTy B = GetBindings(store);
  typedef SVal::symbol_iterator symbol_iterator;

  // Iterate over the variable bindings.
  for (BindingsTy::iterator I=B.begin(), E=B.end(); I!=E ; ++I) {
    if (const VarRegion *VR = dyn_cast<VarRegion>(I.getKey())) {
      if (SymReaper.isLive(VR))
        RegionRoots.push_back(VR);
      else
        continue;
    }
    else if (isa<ObjCIvarRegion>(I.getKey()) ||
             isa<NonStaticGlobalSpaceRegion>(I.getKey()) ||
             isa<CXXThisRegion>(I.getKey()))
      RegionRoots.push_back(I.getKey());
    else
      continue;

    // Mark the bindings in the data as live.
    SVal X = I.getData();
    for (symbol_iterator SI=X.symbol_begin(), SE=X.symbol_end(); SI!=SE; ++SI)
      SymReaper.markLive(*SI);
  }

  // Scan for live variables and live symbols.
  llvm::SmallPtrSet<const MemRegion*, 10> Marked;

  while (!RegionRoots.empty()) {
    const MemRegion* MR = RegionRoots.back();
    RegionRoots.pop_back();

    while (MR) {
      if (const SymbolicRegion* SymR = dyn_cast<SymbolicRegion>(MR)) {
        SymReaper.markLive(SymR->getSymbol());
        break;
      }
      else if (isa<VarRegion>(MR) || isa<ObjCIvarRegion>(MR) ||
               isa<NonStaticGlobalSpaceRegion>(MR) || isa<CXXThisRegion>(MR)) {
        if (Marked.count(MR))
          break;

        Marked.insert(MR);
        SVal X = Retrieve(store, loc::MemRegionVal(MR));

        // FIXME: We need to handle symbols nested in region definitions.
        for (symbol_iterator SI=X.symbol_begin(),SE=X.symbol_end();SI!=SE;++SI)
          SymReaper.markLive(*SI);

        if (!isa<loc::MemRegionVal>(X))
          break;

        const loc::MemRegionVal& LVD = cast<loc::MemRegionVal>(X);
        RegionRoots.push_back(LVD.getRegion());
        break;
      }
      else if (const SubRegion* R = dyn_cast<SubRegion>(MR))
        MR = R->getSuperRegion();
      else
        break;
    }
  }

  // Remove dead variable bindings.
  StoreRef newStore(store, *this);
  for (BindingsTy::iterator I=B.begin(), E=B.end(); I!=E ; ++I) {
    const MemRegion* R = I.getKey();

    if (!Marked.count(R)) {
      newStore = Remove(newStore.getStore(), svalBuilder.makeLoc(R));
      SVal X = I.getData();

      for (symbol_iterator SI=X.symbol_begin(), SE=X.symbol_end(); SI!=SE; ++SI)
        SymReaper.maybeDead(*SI);
    }
  }

  return newStore;
}

StoreRef BasicStoreManager::scanForIvars(Stmt *B, const Decl* SelfDecl,
                                         const MemRegion *SelfRegion,
                                         Store St) {
  
  StoreRef newStore(St, *this);

  for (Stmt::child_iterator CI=B->child_begin(), CE=B->child_end();
       CI != CE; ++CI) {

    if (!*CI)
      continue;

    // Check if the statement is an ivar reference.  We only
    // care about self.ivar.
    if (ObjCIvarRefExpr *IV = dyn_cast<ObjCIvarRefExpr>(*CI)) {
      const Expr *Base = IV->getBase()->IgnoreParenCasts();
      if (const DeclRefExpr *DR = dyn_cast<DeclRefExpr>(Base)) {
        if (DR->getDecl() == SelfDecl) {
          const ObjCIvarRegion *IVR = MRMgr.getObjCIvarRegion(IV->getDecl(),
                                                         SelfRegion);
          SVal X = svalBuilder.getRegionValueSymbolVal(IVR);
          newStore = Bind(newStore.getStore(), svalBuilder.makeLoc(IVR), X);
        }
      }
    }
    else
      newStore = scanForIvars(*CI, SelfDecl, SelfRegion, newStore.getStore());
  }

  return newStore;
}

StoreRef BasicStoreManager::getInitialStore(const LocationContext *InitLoc) {
  // The LiveVariables information already has a compilation of all VarDecls
  // used in the function.  Iterate through this set, and "symbolicate"
  // any VarDecl whose value originally comes from outside the function.
  typedef LiveVariables::AnalysisDataTy LVDataTy;
  LVDataTy& D = InitLoc->getLiveVariables()->getAnalysisData();
  StoreRef St(VBFactory.getEmptyMap().getRoot(), *this);

  for (LVDataTy::decl_iterator I=D.begin_decl(), E=D.end_decl(); I != E; ++I) {
    const NamedDecl* ND = I->first;

    // Handle implicit parameters.
    if (const ImplicitParamDecl* PD = dyn_cast<ImplicitParamDecl>(ND)) {
      const Decl& CD = *InitLoc->getDecl();
      if (const ObjCMethodDecl* MD = dyn_cast<ObjCMethodDecl>(&CD)) {
        if (MD->getSelfDecl() == PD) {
          // FIXME: Add type constraints (when they become available) to
          // SelfRegion?  (i.e., it implements MD->getClassInterface()).
          const VarRegion *VR = MRMgr.getVarRegion(PD, InitLoc);
          const MemRegion *SelfRegion =
            svalBuilder.getRegionValueSymbolVal(VR).getAsRegion();
          assert(SelfRegion);
          St = Bind(St.getStore(), svalBuilder.makeLoc(VR),
                    loc::MemRegionVal(SelfRegion));
          // Scan the method for ivar references.  While this requires an
          // entire AST scan, the cost should not be high in practice.
          St = scanForIvars(MD->getBody(), PD, SelfRegion, St.getStore());
        }
      }
    }
  }

  if (const CXXMethodDecl *MD = dyn_cast<CXXMethodDecl>(InitLoc->getDecl())) {
    // For C++ non-static member variables, add a symbolic region for 'this' in
    // the initial stack frame.
    if (MD->isInstance()) {
      QualType ThisT = MD->getThisType(StateMgr.getContext());
      MemRegionManager &RegMgr = svalBuilder.getRegionManager();
      const CXXThisRegion *ThisR = RegMgr.getCXXThisRegion(ThisT, InitLoc);
      SVal ThisV = svalBuilder.getRegionValueSymbolVal(ThisR);
      St = Bind(St.getStore(), svalBuilder.makeLoc(ThisR), ThisV);
    }
  }

  return St;
}

StoreRef BasicStoreManager::BindDeclInternal(Store store, const VarRegion* VR,
                                             SVal* InitVal) {

  BasicValueFactory& BasicVals = StateMgr.getBasicVals();
  const VarDecl *VD = VR->getDecl();
  StoreRef newStore(store, *this);

  // BasicStore does not model arrays and structs.
  if (VD->getType()->isArrayType() || VD->getType()->isStructureOrClassType())
    return newStore;
  
  if (VD->hasGlobalStorage()) {
    // Handle variables with global storage: extern, static, PrivateExtern.

    // FIXME:: static variables may have an initializer, but the second time a
    // function is called those values may not be current. Currently, a function
    // will not be called more than once.

    // Static global variables should not be visited here.
    assert(!(VD->getStorageClass() == SC_Static &&
             VD->isFileVarDecl()));

    // Process static variables.
    if (VD->getStorageClass() == SC_Static) {
      // C99: 6.7.8 Initialization
      //  If an object that has static storage duration is not initialized
      //  explicitly, then:
      //   -if it has pointer type, it is initialized to a null pointer;
      //   -if it has arithmetic type, it is initialized to (positive or
      //     unsigned) zero;
      if (!InitVal) {
        QualType T = VD->getType();
        if (Loc::isLocType(T))
          newStore = Bind(store, loc::MemRegionVal(VR),
                          loc::ConcreteInt(BasicVals.getValue(0, T)));
        else if (T->isIntegerType() && T->isScalarType())
          newStore = Bind(store, loc::MemRegionVal(VR),
                          nonloc::ConcreteInt(BasicVals.getValue(0, T)));
      } else {
          newStore = Bind(store, loc::MemRegionVal(VR), *InitVal);
      }
    }
  } else {
    // Process local scalar variables.
    QualType T = VD->getType();
    // BasicStore only supports scalars.
    if ((T->isScalarType() || T->isReferenceType()) &&
        svalBuilder.getSymbolManager().canSymbolicate(T)) {
      SVal V = InitVal ? *InitVal : UndefinedVal();
      newStore = Bind(store, loc::MemRegionVal(VR), V);
    }
  }

  return newStore;
}

void BasicStoreManager::print(Store store, llvm::raw_ostream& Out,
                              const char* nl, const char *sep) {

  BindingsTy B = GetBindings(store);
  Out << "Variables:" << nl;

  bool isFirst = true;

  for (BindingsTy::iterator I=B.begin(), E=B.end(); I != E; ++I) {
    if (isFirst)
      isFirst = false;
    else
      Out << nl;

    Out << ' ' << I.getKey() << " : " << I.getData();
  }
}


void BasicStoreManager::iterBindings(Store store, BindingsHandler& f) {
  BindingsTy B = GetBindings(store);

  for (BindingsTy::iterator I=B.begin(), E=B.end(); I != E; ++I)
    if (!f.HandleBinding(*this, store, I.getKey(), I.getData()))
      return;

}

StoreManager::BindingsHandler::~BindingsHandler() {}

//===----------------------------------------------------------------------===//
// Binding invalidation.
//===----------------------------------------------------------------------===//


StoreRef BasicStoreManager::invalidateRegions(Store store,
                                              const MemRegion * const *I,
                                              const MemRegion * const *End,
                                              const Expr *E, unsigned Count,
                                              InvalidatedSymbols &IS,
                                              bool invalidateGlobals,
                                              InvalidatedRegions *Regions) {
  StoreRef newStore(store, *this);
  
  if (invalidateGlobals) {
    BindingsTy B = GetBindings(store);
    for (BindingsTy::iterator I=B.begin(), End=B.end(); I != End; ++I) {
      const MemRegion *R = I.getKey();
      if (isa<NonStaticGlobalSpaceRegion>(R->getMemorySpace()))
        newStore = invalidateRegion(newStore.getStore(), R, E, Count, IS);
    }
  }

  for ( ; I != End ; ++I) {
    const MemRegion *R = *I;
    // Don't invalidate globals twice.
    if (invalidateGlobals) {
      if (isa<NonStaticGlobalSpaceRegion>(R->getMemorySpace()))
        continue;
    }
    newStore = invalidateRegion(newStore.getStore(), *I, E, Count, IS);
    if (Regions)
      Regions->push_back(R);
  }

  // FIXME: This is copy-and-paste from RegionStore.cpp.
  if (invalidateGlobals) {
    // Bind the non-static globals memory space to a new symbol that we will
    // use to derive the bindings for all non-static globals.
    const GlobalsSpaceRegion *GS = MRMgr.getGlobalsRegion();
    SVal V =
      svalBuilder.getConjuredSymbolVal(/* SymbolTag = */ (void*) GS, E,
                                  /* symbol type, doesn't matter */ Ctx.IntTy,
                                  Count);

    newStore = Bind(newStore.getStore(), loc::MemRegionVal(GS), V);
    if (Regions)
      Regions->push_back(GS);
  }

  return newStore;
}


StoreRef BasicStoreManager::invalidateRegion(Store store,
                                             const MemRegion *R,
                                             const Expr *E,
                                             unsigned Count,
                                             InvalidatedSymbols &IS) {
  R = R->StripCasts();

  if (!(isa<VarRegion>(R) || isa<ObjCIvarRegion>(R)))
      return StoreRef(store, *this);

  BindingsTy B = GetBindings(store);
  if (BindingsTy::data_type *Val = B.lookup(R)) {
    if (SymbolRef Sym = Val->getAsSymbol())
      IS.insert(Sym);
  }

  QualType T = cast<TypedRegion>(R)->getValueType();
  SVal V = svalBuilder.getConjuredSymbolVal(R, E, T, Count);
  return Bind(store, loc::MemRegionVal(R), V);
}
