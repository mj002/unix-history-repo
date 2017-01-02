//===--- TypoCorrection.h - Class for typo correction results ---*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
// This file defines the TypoCorrection class, which stores the results of
// Sema's typo correction (Sema::CorrectTypo).
//
//===----------------------------------------------------------------------===//

#ifndef LLVM_CLANG_SEMA_TYPOCORRECTION_H
#define LLVM_CLANG_SEMA_TYPOCORRECTION_H

#include "clang/AST/DeclCXX.h"
#include "clang/Sema/DeclSpec.h"
#include "clang/Sema/Ownership.h"
#include "llvm/ADT/SmallVector.h"

namespace clang {

/// @brief Simple class containing the result of Sema::CorrectTypo
class TypoCorrection {
public:
  // "Distance" for unusable corrections
  static const unsigned InvalidDistance = ~0U;
  // The largest distance still considered valid (larger edit distances are
  // mapped to InvalidDistance by getEditDistance).
  static const unsigned MaximumDistance = 10000U;

  // Relative weightings of the "edit distance" components. The higher the
  // weight, the more of a penalty to fitness the component will give (higher
  // weights mean greater contribution to the total edit distance, with the
  // best correction candidates having the lowest edit distance).
  static const unsigned CharDistanceWeight = 100U;
  static const unsigned QualifierDistanceWeight = 110U;
  static const unsigned CallbackDistanceWeight = 150U;

  TypoCorrection(const DeclarationName &Name, NamedDecl *NameDecl,
                 NestedNameSpecifier *NNS = nullptr, unsigned CharDistance = 0,
                 unsigned QualifierDistance = 0)
      : CorrectionName(Name), CorrectionNameSpec(NNS),
        CharDistance(CharDistance), QualifierDistance(QualifierDistance),
        CallbackDistance(0), ForceSpecifierReplacement(false),
        RequiresImport(false) {
    if (NameDecl)
      CorrectionDecls.push_back(NameDecl);
  }

  TypoCorrection(NamedDecl *Name, NestedNameSpecifier *NNS = nullptr,
                 unsigned CharDistance = 0)
      : CorrectionName(Name->getDeclName()), CorrectionNameSpec(NNS),
        CharDistance(CharDistance), QualifierDistance(0), CallbackDistance(0),
        ForceSpecifierReplacement(false), RequiresImport(false) {
    if (Name)
      CorrectionDecls.push_back(Name);
  }

  TypoCorrection(DeclarationName Name, NestedNameSpecifier *NNS = nullptr,
                 unsigned CharDistance = 0)
      : CorrectionName(Name), CorrectionNameSpec(NNS),
        CharDistance(CharDistance), QualifierDistance(0), CallbackDistance(0),
        ForceSpecifierReplacement(false), RequiresImport(false) {}

  TypoCorrection()
      : CorrectionNameSpec(nullptr), CharDistance(0), QualifierDistance(0),
        CallbackDistance(0), ForceSpecifierReplacement(false),
        RequiresImport(false) {}

  /// \brief Gets the DeclarationName of the typo correction
  DeclarationName getCorrection() const { return CorrectionName; }
  IdentifierInfo *getCorrectionAsIdentifierInfo() const {
    return CorrectionName.getAsIdentifierInfo();
  }

  /// \brief Gets the NestedNameSpecifier needed to use the typo correction
  NestedNameSpecifier *getCorrectionSpecifier() const {
    return CorrectionNameSpec;
  }
  void setCorrectionSpecifier(NestedNameSpecifier *NNS) {
    CorrectionNameSpec = NNS;
    ForceSpecifierReplacement = (NNS != nullptr);
  }

  void WillReplaceSpecifier(bool ForceReplacement) {
    ForceSpecifierReplacement = ForceReplacement;
  }

  bool WillReplaceSpecifier() const {
    return ForceSpecifierReplacement;
  }

  void setQualifierDistance(unsigned ED) {
    QualifierDistance = ED;
  }

  void setCallbackDistance(unsigned ED) {
    CallbackDistance = ED;
  }

  // Convert the given weighted edit distance to a roughly equivalent number of
  // single-character edits (typically for comparison to the length of the
  // string being edited).
  static unsigned NormalizeEditDistance(unsigned ED) {
    if (ED > MaximumDistance)
      return InvalidDistance;
    return (ED + CharDistanceWeight / 2) / CharDistanceWeight;
  }

  /// \brief Gets the "edit distance" of the typo correction from the typo.
  /// If Normalized is true, scale the distance down by the CharDistanceWeight
  /// to return the edit distance in terms of single-character edits.
  unsigned getEditDistance(bool Normalized = true) const {
    if (CharDistance > MaximumDistance || QualifierDistance > MaximumDistance ||
        CallbackDistance > MaximumDistance)
      return InvalidDistance;
    unsigned ED =
        CharDistance * CharDistanceWeight +
        QualifierDistance * QualifierDistanceWeight +
        CallbackDistance * CallbackDistanceWeight;
    if (ED > MaximumDistance)
      return InvalidDistance;
    // Half the CharDistanceWeight is added to ED to simulate rounding since
    // integer division truncates the value (i.e. round-to-nearest-int instead
    // of round-to-zero).
    return Normalized ? NormalizeEditDistance(ED) : ED;
  }

  /// \brief Get the correction declaration found by name lookup (before we
  /// looked through using shadow declarations and the like).
  NamedDecl *getFoundDecl() const {
    return hasCorrectionDecl() ? *(CorrectionDecls.begin()) : nullptr;
  }

  /// \brief Gets the pointer to the declaration of the typo correction
  NamedDecl *getCorrectionDecl() const {
    auto *D = getFoundDecl();
    return D ? D->getUnderlyingDecl() : nullptr;
  }
  template <class DeclClass>
  DeclClass *getCorrectionDeclAs() const {
    return dyn_cast_or_null<DeclClass>(getCorrectionDecl());
  }

  /// \brief Clears the list of NamedDecls.
  void ClearCorrectionDecls() {
    CorrectionDecls.clear();
  }

  /// \brief Clears the list of NamedDecls before adding the new one.
  void setCorrectionDecl(NamedDecl *CDecl) {
    CorrectionDecls.clear();
    addCorrectionDecl(CDecl);
  }

  /// \brief Clears the list of NamedDecls and adds the given set.
  void setCorrectionDecls(ArrayRef<NamedDecl*> Decls) {
    CorrectionDecls.clear();
    CorrectionDecls.insert(CorrectionDecls.begin(), Decls.begin(), Decls.end());
  }

  /// \brief Add the given NamedDecl to the list of NamedDecls that are the
  /// declarations associated with the DeclarationName of this TypoCorrection
  void addCorrectionDecl(NamedDecl *CDecl);

  std::string getAsString(const LangOptions &LO) const;
  std::string getQuoted(const LangOptions &LO) const {
    return "'" + getAsString(LO) + "'";
  }

  /// \brief Returns whether this TypoCorrection has a non-empty DeclarationName
  explicit operator bool() const { return bool(CorrectionName); }

  /// \brief Mark this TypoCorrection as being a keyword.
  /// Since addCorrectionDeclsand setCorrectionDecl don't allow NULL to be
  /// added to the list of the correction's NamedDecl pointers, NULL is added
  /// as the only element in the list to mark this TypoCorrection as a keyword.
  void makeKeyword() {
    CorrectionDecls.clear();
    CorrectionDecls.push_back(nullptr);
    ForceSpecifierReplacement = true;
  }

  // Check if this TypoCorrection is a keyword by checking if the first
  // item in CorrectionDecls is NULL.
  bool isKeyword() const {
    return !CorrectionDecls.empty() && CorrectionDecls.front() == nullptr;
  }

  // Check if this TypoCorrection is the given keyword.
  template<std::size_t StrLen>
  bool isKeyword(const char (&Str)[StrLen]) const {
    return isKeyword() && getCorrectionAsIdentifierInfo()->isStr(Str);
  }

  // Returns true if the correction either is a keyword or has a known decl.
  bool isResolved() const { return !CorrectionDecls.empty(); }

  bool isOverloaded() const {
    return CorrectionDecls.size() > 1;
  }

  void setCorrectionRange(CXXScopeSpec *SS,
                          const DeclarationNameInfo &TypoName) {
    CorrectionRange = TypoName.getSourceRange();
    if (ForceSpecifierReplacement && SS && !SS->isEmpty())
      CorrectionRange.setBegin(SS->getBeginLoc());
  }

  SourceRange getCorrectionRange() const {
    return CorrectionRange;
  }

  typedef SmallVectorImpl<NamedDecl *>::iterator decl_iterator;
  decl_iterator begin() {
    return isKeyword() ? CorrectionDecls.end() : CorrectionDecls.begin();
  }
  decl_iterator end() { return CorrectionDecls.end(); }
  typedef SmallVectorImpl<NamedDecl *>::const_iterator const_decl_iterator;
  const_decl_iterator begin() const {
    return isKeyword() ? CorrectionDecls.end() : CorrectionDecls.begin();
  }
  const_decl_iterator end() const { return CorrectionDecls.end(); }

  /// \brief Returns whether this typo correction is correcting to a
  /// declaration that was declared in a module that has not been imported.
  bool requiresImport() const { return RequiresImport; }
  void setRequiresImport(bool Req) { RequiresImport = Req; }

  /// Extra diagnostics are printed after the first diagnostic for the typo.
  /// This can be used to attach external notes to the diag.
  void addExtraDiagnostic(PartialDiagnostic PD) {
    ExtraDiagnostics.push_back(std::move(PD));
  }
  ArrayRef<PartialDiagnostic> getExtraDiagnostics() const {
    return ExtraDiagnostics;
  }

private:
  bool hasCorrectionDecl() const {
    return (!isKeyword() && !CorrectionDecls.empty());
  }

  // Results.
  DeclarationName CorrectionName;
  NestedNameSpecifier *CorrectionNameSpec;
  SmallVector<NamedDecl *, 1> CorrectionDecls;
  unsigned CharDistance;
  unsigned QualifierDistance;
  unsigned CallbackDistance;
  SourceRange CorrectionRange;
  bool ForceSpecifierReplacement;
  bool RequiresImport;

  std::vector<PartialDiagnostic> ExtraDiagnostics;
};

/// @brief Base class for callback objects used by Sema::CorrectTypo to check
/// the validity of a potential typo correction.
class CorrectionCandidateCallback {
public:
  static const unsigned InvalidDistance = TypoCorrection::InvalidDistance;

  explicit CorrectionCandidateCallback(IdentifierInfo *Typo = nullptr,
                                       NestedNameSpecifier *TypoNNS = nullptr)
      : WantTypeSpecifiers(true), WantExpressionKeywords(true),
        WantCXXNamedCasts(true), WantFunctionLikeCasts(true),
        WantRemainingKeywords(true), WantObjCSuper(false),
        IsObjCIvarLookup(false), IsAddressOfOperand(false), Typo(Typo),
        TypoNNS(TypoNNS) {}

  virtual ~CorrectionCandidateCallback() {}

  /// \brief Simple predicate used by the default RankCandidate to
  /// determine whether to return an edit distance of 0 or InvalidDistance.
  /// This can be overrided by validators that only need to determine if a
  /// candidate is viable, without ranking potentially viable candidates.
  /// Only ValidateCandidate or RankCandidate need to be overriden by a
  /// callback wishing to check the viability of correction candidates.
  /// The default predicate always returns true if the candidate is not a type
  /// name or keyword, true for types if WantTypeSpecifiers is true, and true
  /// for keywords if WantTypeSpecifiers, WantExpressionKeywords,
  /// WantCXXNamedCasts, WantRemainingKeywords, or WantObjCSuper is true.
  virtual bool ValidateCandidate(const TypoCorrection &candidate);

  /// \brief Method used by Sema::CorrectTypo to assign an "edit distance" rank
  /// to a candidate (where a lower value represents a better candidate), or
  /// returning InvalidDistance if the candidate is not at all viable. For
  /// validation callbacks that only need to determine if a candidate is viable,
  /// the default RankCandidate returns either 0 or InvalidDistance depending
  /// whether ValidateCandidate returns true or false.
  virtual unsigned RankCandidate(const TypoCorrection &candidate) {
    return (!MatchesTypo(candidate) && ValidateCandidate(candidate))
               ? 0
               : InvalidDistance;
  }

  void setTypoName(IdentifierInfo *II) { Typo = II; }
  void setTypoNNS(NestedNameSpecifier *NNS) { TypoNNS = NNS; }

  // Flags for context-dependent keywords. WantFunctionLikeCasts is only
  // used/meaningful when WantCXXNamedCasts is false.
  // TODO: Expand these to apply to non-keywords or possibly remove them.
  bool WantTypeSpecifiers;
  bool WantExpressionKeywords;
  bool WantCXXNamedCasts;
  bool WantFunctionLikeCasts;
  bool WantRemainingKeywords;
  bool WantObjCSuper;
  // Temporary hack for the one case where a CorrectTypoContext enum is used
  // when looking up results.
  bool IsObjCIvarLookup;
  bool IsAddressOfOperand;

protected:
  bool MatchesTypo(const TypoCorrection &candidate) {
    return Typo && candidate.isResolved() && !candidate.requiresImport() &&
           candidate.getCorrectionAsIdentifierInfo() == Typo &&
           // FIXME: This probably does not return true when both
           // NestedNameSpecifiers have the same textual representation.
           candidate.getCorrectionSpecifier() == TypoNNS;
  }

  IdentifierInfo *Typo;
  NestedNameSpecifier *TypoNNS;
};

/// @brief Simple template class for restricting typo correction candidates
/// to ones having a single Decl* of the given type.
template <class C>
class DeclFilterCCC : public CorrectionCandidateCallback {
public:
  bool ValidateCandidate(const TypoCorrection &candidate) override {
    return candidate.getCorrectionDeclAs<C>();
  }
};

// @brief Callback class to limit the allowed keywords and to only accept typo
// corrections that are keywords or whose decls refer to functions (or template
// functions) that accept the given number of arguments.
class FunctionCallFilterCCC : public CorrectionCandidateCallback {
public:
  FunctionCallFilterCCC(Sema &SemaRef, unsigned NumArgs,
                        bool HasExplicitTemplateArgs,
                        MemberExpr *ME = nullptr);

  bool ValidateCandidate(const TypoCorrection &candidate) override;

 private:
  unsigned NumArgs;
  bool HasExplicitTemplateArgs;
  DeclContext *CurContext;
  MemberExpr *MemberFn;
};

// @brief Callback class that effectively disabled typo correction
class NoTypoCorrectionCCC : public CorrectionCandidateCallback {
public:
  NoTypoCorrectionCCC() {
    WantTypeSpecifiers = false;
    WantExpressionKeywords = false;
    WantCXXNamedCasts = false;
    WantFunctionLikeCasts = false;
    WantRemainingKeywords = false;
  }

  bool ValidateCandidate(const TypoCorrection &candidate) override {
    return false;
  }
};

}

#endif
