//===--- ParseObjC.cpp - Objective C Parsing ------------------------------===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//
//
//  This file implements the Objective-C portions of the Parser interface.
//
//===----------------------------------------------------------------------===//

#include "clang/Parse/Parser.h"
#include "clang/Parse/DeclSpec.h"
#include "clang/Parse/Scope.h"
#include "clang/Parse/ParseDiagnostic.h"
#include "llvm/ADT/SmallVector.h"
using namespace clang;


/// ParseObjCAtDirectives - Handle parts of the external-declaration production:
///       external-declaration: [C99 6.9]
/// [OBJC]  objc-class-definition
/// [OBJC]  objc-class-declaration
/// [OBJC]  objc-alias-declaration
/// [OBJC]  objc-protocol-definition
/// [OBJC]  objc-method-definition
/// [OBJC]  '@' 'end'
Parser::DeclPtrTy Parser::ParseObjCAtDirectives() {
  SourceLocation AtLoc = ConsumeToken(); // the "@"

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteObjCAtDirective(CurScope, ObjCImpDecl, false);
    ConsumeCodeCompletionToken();
  }

  switch (Tok.getObjCKeywordID()) {
  case tok::objc_class:
    return ParseObjCAtClassDeclaration(AtLoc);
  case tok::objc_interface:
    return ParseObjCAtInterfaceDeclaration(AtLoc);
  case tok::objc_protocol:
    return ParseObjCAtProtocolDeclaration(AtLoc);
  case tok::objc_implementation:
    return ParseObjCAtImplementationDeclaration(AtLoc);
  case tok::objc_end:
    return ParseObjCAtEndDeclaration(AtLoc);
  case tok::objc_compatibility_alias:
    return ParseObjCAtAliasDeclaration(AtLoc);
  case tok::objc_synthesize:
    return ParseObjCPropertySynthesize(AtLoc);
  case tok::objc_dynamic:
    return ParseObjCPropertyDynamic(AtLoc);
  default:
    Diag(AtLoc, diag::err_unexpected_at);
    SkipUntil(tok::semi);
    return DeclPtrTy();
  }
}

///
/// objc-class-declaration:
///    '@' 'class' identifier-list ';'
///
Parser::DeclPtrTy Parser::ParseObjCAtClassDeclaration(SourceLocation atLoc) {
  ConsumeToken(); // the identifier "class"
  llvm::SmallVector<IdentifierInfo *, 8> ClassNames;
  llvm::SmallVector<SourceLocation, 8> ClassLocs;


  while (1) {
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected_ident);
      SkipUntil(tok::semi);
      return DeclPtrTy();
    }
    ClassNames.push_back(Tok.getIdentifierInfo());
    ClassLocs.push_back(Tok.getLocation());
    ConsumeToken();

    if (Tok.isNot(tok::comma))
      break;

    ConsumeToken();
  }

  // Consume the ';'.
  if (ExpectAndConsume(tok::semi, diag::err_expected_semi_after, "@class"))
    return DeclPtrTy();

  return Actions.ActOnForwardClassDeclaration(atLoc, ClassNames.data(),
                                              ClassLocs.data(),
                                              ClassNames.size());
}

///
///   objc-interface:
///     objc-class-interface-attributes[opt] objc-class-interface
///     objc-category-interface
///
///   objc-class-interface:
///     '@' 'interface' identifier objc-superclass[opt]
///       objc-protocol-refs[opt]
///       objc-class-instance-variables[opt]
///       objc-interface-decl-list
///     @end
///
///   objc-category-interface:
///     '@' 'interface' identifier '(' identifier[opt] ')'
///       objc-protocol-refs[opt]
///       objc-interface-decl-list
///     @end
///
///   objc-superclass:
///     ':' identifier
///
///   objc-class-interface-attributes:
///     __attribute__((visibility("default")))
///     __attribute__((visibility("hidden")))
///     __attribute__((deprecated))
///     __attribute__((unavailable))
///     __attribute__((objc_exception)) - used by NSException on 64-bit
///
Parser::DeclPtrTy Parser::ParseObjCAtInterfaceDeclaration(
  SourceLocation atLoc, AttributeList *attrList) {
  assert(Tok.isObjCAtKeyword(tok::objc_interface) &&
         "ParseObjCAtInterfaceDeclaration(): Expected @interface");
  ConsumeToken(); // the "interface" identifier

  // Code completion after '@interface'.
  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteObjCInterfaceDecl(CurScope);
    ConsumeCodeCompletionToken();
  }

  if (Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_expected_ident); // missing class or category name.
    return DeclPtrTy();
  }

  // We have a class or category name - consume it.
  IdentifierInfo *nameId = Tok.getIdentifierInfo();
  SourceLocation nameLoc = ConsumeToken();
  if (Tok.is(tok::l_paren) && 
      !isKnownToBeTypeSpecifier(GetLookAheadToken(1))) { // we have a category.
    SourceLocation lparenLoc = ConsumeParen();
    SourceLocation categoryLoc, rparenLoc;
    IdentifierInfo *categoryId = 0;
    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteObjCInterfaceCategory(CurScope, nameId, nameLoc);
      ConsumeCodeCompletionToken();
    }
    
    // For ObjC2, the category name is optional (not an error).
    if (Tok.is(tok::identifier)) {
      categoryId = Tok.getIdentifierInfo();
      categoryLoc = ConsumeToken();
    }
    else if (!getLang().ObjC2) {
      Diag(Tok, diag::err_expected_ident); // missing category name.
      return DeclPtrTy();
    }
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected_rparen);
      SkipUntil(tok::r_paren, false); // don't stop at ';'
      return DeclPtrTy();
    }
    rparenLoc = ConsumeParen();
    // Next, we need to check for any protocol references.
    SourceLocation LAngleLoc, EndProtoLoc;
    llvm::SmallVector<DeclPtrTy, 8> ProtocolRefs;
    llvm::SmallVector<SourceLocation, 8> ProtocolLocs;
    if (Tok.is(tok::less) &&
        ParseObjCProtocolReferences(ProtocolRefs, ProtocolLocs, true,
                                    LAngleLoc, EndProtoLoc))
      return DeclPtrTy();

    if (attrList) // categories don't support attributes.
      Diag(Tok, diag::err_objc_no_attributes_on_category);

    DeclPtrTy CategoryType =
    Actions.ActOnStartCategoryInterface(atLoc,
                                        nameId, nameLoc,
                                        categoryId, categoryLoc,
                                        ProtocolRefs.data(),
                                        ProtocolRefs.size(),
                                        ProtocolLocs.data(),
                                        EndProtoLoc);
    if (Tok.is(tok::l_brace))
      ParseObjCClassInstanceVariables(CategoryType, tok::objc_private,
                                      atLoc);
    
    ParseObjCInterfaceDeclList(CategoryType, tok::objc_not_keyword);
    return CategoryType;
  }
  // Parse a class interface.
  IdentifierInfo *superClassId = 0;
  SourceLocation superClassLoc;

  if (Tok.is(tok::colon)) { // a super class is specified.
    ConsumeToken();

    // Code completion of superclass names.
    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteObjCSuperclass(CurScope, nameId, nameLoc);
      ConsumeCodeCompletionToken();
    }

    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected_ident); // missing super class name.
      return DeclPtrTy();
    }
    superClassId = Tok.getIdentifierInfo();
    superClassLoc = ConsumeToken();
  }
  // Next, we need to check for any protocol references.
  llvm::SmallVector<Action::DeclPtrTy, 8> ProtocolRefs;
  llvm::SmallVector<SourceLocation, 8> ProtocolLocs;
  SourceLocation LAngleLoc, EndProtoLoc;
  if (Tok.is(tok::less) &&
      ParseObjCProtocolReferences(ProtocolRefs, ProtocolLocs, true,
                                  LAngleLoc, EndProtoLoc))
    return DeclPtrTy();

  DeclPtrTy ClsType =
    Actions.ActOnStartClassInterface(atLoc, nameId, nameLoc,
                                     superClassId, superClassLoc,
                                     ProtocolRefs.data(), ProtocolRefs.size(),
                                     ProtocolLocs.data(),
                                     EndProtoLoc, attrList);

  if (Tok.is(tok::l_brace))
    ParseObjCClassInstanceVariables(ClsType, tok::objc_protected, atLoc);

  ParseObjCInterfaceDeclList(ClsType, tok::objc_interface);
  return ClsType;
}

/// The Objective-C property callback.  This should be defined where
/// it's used, but instead it's been lifted to here to support VS2005.
struct Parser::ObjCPropertyCallback : FieldCallback {
  Parser &P;
  DeclPtrTy IDecl;
  llvm::SmallVectorImpl<DeclPtrTy> &Props;
  ObjCDeclSpec &OCDS;
  SourceLocation AtLoc;
  tok::ObjCKeywordKind MethodImplKind;
        
  ObjCPropertyCallback(Parser &P, DeclPtrTy IDecl,
                       llvm::SmallVectorImpl<DeclPtrTy> &Props,
                       ObjCDeclSpec &OCDS, SourceLocation AtLoc,
                       tok::ObjCKeywordKind MethodImplKind) :
    P(P), IDecl(IDecl), Props(Props), OCDS(OCDS), AtLoc(AtLoc),
    MethodImplKind(MethodImplKind) {
  }

  DeclPtrTy invoke(FieldDeclarator &FD) {
    if (FD.D.getIdentifier() == 0) {
      P.Diag(AtLoc, diag::err_objc_property_requires_field_name)
        << FD.D.getSourceRange();
      return DeclPtrTy();
    }
    if (FD.BitfieldSize) {
      P.Diag(AtLoc, diag::err_objc_property_bitfield)
        << FD.D.getSourceRange();
      return DeclPtrTy();
    }

    // Install the property declarator into interfaceDecl.
    IdentifierInfo *SelName =
      OCDS.getGetterName() ? OCDS.getGetterName() : FD.D.getIdentifier();

    Selector GetterSel =
      P.PP.getSelectorTable().getNullarySelector(SelName);
    IdentifierInfo *SetterName = OCDS.getSetterName();
    Selector SetterSel;
    if (SetterName)
      SetterSel = P.PP.getSelectorTable().getSelector(1, &SetterName);
    else
      SetterSel = SelectorTable::constructSetterName(P.PP.getIdentifierTable(),
                                                     P.PP.getSelectorTable(),
                                                     FD.D.getIdentifier());
    bool isOverridingProperty = false;
    DeclPtrTy Property =
      P.Actions.ActOnProperty(P.CurScope, AtLoc, FD, OCDS,
                              GetterSel, SetterSel, IDecl,
                              &isOverridingProperty,
                              MethodImplKind);
    if (!isOverridingProperty)
      Props.push_back(Property);

    return Property;
  }
};

///   objc-interface-decl-list:
///     empty
///     objc-interface-decl-list objc-property-decl [OBJC2]
///     objc-interface-decl-list objc-method-requirement [OBJC2]
///     objc-interface-decl-list objc-method-proto ';'
///     objc-interface-decl-list declaration
///     objc-interface-decl-list ';'
///
///   objc-method-requirement: [OBJC2]
///     @required
///     @optional
///
void Parser::ParseObjCInterfaceDeclList(DeclPtrTy interfaceDecl,
                                        tok::ObjCKeywordKind contextKey) {
  llvm::SmallVector<DeclPtrTy, 32> allMethods;
  llvm::SmallVector<DeclPtrTy, 16> allProperties;
  llvm::SmallVector<DeclGroupPtrTy, 8> allTUVariables;
  tok::ObjCKeywordKind MethodImplKind = tok::objc_not_keyword;

  SourceRange AtEnd;

  while (1) {
    // If this is a method prototype, parse it.
    if (Tok.is(tok::minus) || Tok.is(tok::plus)) {
      DeclPtrTy methodPrototype =
        ParseObjCMethodPrototype(interfaceDecl, MethodImplKind);
      allMethods.push_back(methodPrototype);
      // Consume the ';' here, since ParseObjCMethodPrototype() is re-used for
      // method definitions.
      ExpectAndConsume(tok::semi, diag::err_expected_semi_after_method_proto,
                       "", tok::semi);
      continue;
    }
    if (Tok.is(tok::l_paren)) {
      Diag(Tok, diag::err_expected_minus_or_plus);
      DeclPtrTy methodPrototype = ParseObjCMethodDecl(Tok.getLocation(), 
                                                      tok::minus, 
                                                      interfaceDecl,
                                                      MethodImplKind);
      continue;
    }
    // Ignore excess semicolons.
    if (Tok.is(tok::semi)) {
      ConsumeToken();
      continue;
    }

    // If we got to the end of the file, exit the loop.
    if (Tok.is(tok::eof))
      break;

    // Code completion within an Objective-C interface.
    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteOrdinaryName(CurScope, 
                                  ObjCImpDecl? Action::CCC_ObjCImplementation
                                             : Action::CCC_ObjCInterface);
      ConsumeCodeCompletionToken();
    }
    
    // If we don't have an @ directive, parse it as a function definition.
    if (Tok.isNot(tok::at)) {
      // The code below does not consume '}'s because it is afraid of eating the
      // end of a namespace.  Because of the way this code is structured, an
      // erroneous r_brace would cause an infinite loop if not handled here.
      if (Tok.is(tok::r_brace))
        break;

      // FIXME: as the name implies, this rule allows function definitions.
      // We could pass a flag or check for functions during semantic analysis.
      allTUVariables.push_back(ParseDeclarationOrFunctionDefinition(0));
      continue;
    }

    // Otherwise, we have an @ directive, eat the @.
    SourceLocation AtLoc = ConsumeToken(); // the "@"
    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteObjCAtDirective(CurScope, ObjCImpDecl, true);
      ConsumeCodeCompletionToken();
      break;
    }

    tok::ObjCKeywordKind DirectiveKind = Tok.getObjCKeywordID();

    if (DirectiveKind == tok::objc_end) { // @end -> terminate list
      AtEnd.setBegin(AtLoc);
      AtEnd.setEnd(Tok.getLocation());
      break;
    } else if (DirectiveKind == tok::objc_not_keyword) {
      Diag(Tok, diag::err_objc_unknown_at);
      SkipUntil(tok::semi);
      continue;
    }

    // Eat the identifier.
    ConsumeToken();

    switch (DirectiveKind) {
    default:
      // FIXME: If someone forgets an @end on a protocol, this loop will
      // continue to eat up tons of stuff and spew lots of nonsense errors.  It
      // would probably be better to bail out if we saw an @class or @interface
      // or something like that.
      Diag(AtLoc, diag::err_objc_illegal_interface_qual);
      // Skip until we see an '@' or '}' or ';'.
      SkipUntil(tok::r_brace, tok::at);
      break;

    case tok::objc_required:
    case tok::objc_optional:
      // This is only valid on protocols.
      // FIXME: Should this check for ObjC2 being enabled?
      if (contextKey != tok::objc_protocol)
        Diag(AtLoc, diag::err_objc_directive_only_in_protocol);
      else
        MethodImplKind = DirectiveKind;
      break;

    case tok::objc_property:
      if (!getLang().ObjC2)
        Diag(AtLoc, diag::err_objc_propertoes_require_objc2);

      ObjCDeclSpec OCDS;
      // Parse property attribute list, if any.
      if (Tok.is(tok::l_paren))
        ParseObjCPropertyAttribute(OCDS, interfaceDecl,
                                   allMethods.data(), allMethods.size());

      ObjCPropertyCallback Callback(*this, interfaceDecl, allProperties,
                                    OCDS, AtLoc, MethodImplKind);

      // Parse all the comma separated declarators.
      DeclSpec DS;
      ParseStructDeclaration(DS, Callback);

      ExpectAndConsume(tok::semi, diag::err_expected_semi_decl_list, "",
                       tok::at);
      break;
    }
  }

  // We break out of the big loop in two cases: when we see @end or when we see
  // EOF.  In the former case, eat the @end.  In the later case, emit an error.
  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteObjCAtDirective(CurScope, ObjCImpDecl, true);
    ConsumeCodeCompletionToken();
  } else if (Tok.isObjCAtKeyword(tok::objc_end))
    ConsumeToken(); // the "end" identifier
  else
    Diag(Tok, diag::err_objc_missing_end);

  // Insert collected methods declarations into the @interface object.
  // This passes in an invalid SourceLocation for AtEndLoc when EOF is hit.
  Actions.ActOnAtEnd(CurScope, AtEnd, interfaceDecl,
                     allMethods.data(), allMethods.size(),
                     allProperties.data(), allProperties.size(),
                     allTUVariables.data(), allTUVariables.size());
}

///   Parse property attribute declarations.
///
///   property-attr-decl: '(' property-attrlist ')'
///   property-attrlist:
///     property-attribute
///     property-attrlist ',' property-attribute
///   property-attribute:
///     getter '=' identifier
///     setter '=' identifier ':'
///     readonly
///     readwrite
///     assign
///     retain
///     copy
///     nonatomic
///
void Parser::ParseObjCPropertyAttribute(ObjCDeclSpec &DS, DeclPtrTy ClassDecl,
                                        DeclPtrTy *Methods, 
                                        unsigned NumMethods) {
  assert(Tok.getKind() == tok::l_paren);
  SourceLocation LHSLoc = ConsumeParen(); // consume '('

  while (1) {
    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteObjCPropertyFlags(CurScope, DS);
      ConsumeCodeCompletionToken();
    }
    const IdentifierInfo *II = Tok.getIdentifierInfo();

    // If this is not an identifier at all, bail out early.
    if (II == 0) {
      MatchRHSPunctuation(tok::r_paren, LHSLoc);
      return;
    }

    SourceLocation AttrName = ConsumeToken(); // consume last attribute name

    if (II->isStr("readonly"))
      DS.setPropertyAttributes(ObjCDeclSpec::DQ_PR_readonly);
    else if (II->isStr("assign"))
      DS.setPropertyAttributes(ObjCDeclSpec::DQ_PR_assign);
    else if (II->isStr("readwrite"))
      DS.setPropertyAttributes(ObjCDeclSpec::DQ_PR_readwrite);
    else if (II->isStr("retain"))
      DS.setPropertyAttributes(ObjCDeclSpec::DQ_PR_retain);
    else if (II->isStr("copy"))
      DS.setPropertyAttributes(ObjCDeclSpec::DQ_PR_copy);
    else if (II->isStr("nonatomic"))
      DS.setPropertyAttributes(ObjCDeclSpec::DQ_PR_nonatomic);
    else if (II->isStr("getter") || II->isStr("setter")) {
      // getter/setter require extra treatment.
      if (ExpectAndConsume(tok::equal, diag::err_objc_expected_equal, "",
                           tok::r_paren))
        return;

      if (Tok.is(tok::code_completion)) {
        if (II->getNameStart()[0] == 's')
          Actions.CodeCompleteObjCPropertySetter(CurScope, ClassDecl,
                                                 Methods, NumMethods);
        else
          Actions.CodeCompleteObjCPropertyGetter(CurScope, ClassDecl,
                                                 Methods, NumMethods);
        ConsumeCodeCompletionToken();
      }

      if (Tok.isNot(tok::identifier)) {
        Diag(Tok, diag::err_expected_ident);
        SkipUntil(tok::r_paren);
        return;
      }

      if (II->getNameStart()[0] == 's') {
        DS.setPropertyAttributes(ObjCDeclSpec::DQ_PR_setter);
        DS.setSetterName(Tok.getIdentifierInfo());
        ConsumeToken();  // consume method name

        if (ExpectAndConsume(tok::colon, 
                             diag::err_expected_colon_after_setter_name, "",
                             tok::r_paren))
          return;
      } else {
        DS.setPropertyAttributes(ObjCDeclSpec::DQ_PR_getter);
        DS.setGetterName(Tok.getIdentifierInfo());
        ConsumeToken();  // consume method name
      }
    } else {
      Diag(AttrName, diag::err_objc_expected_property_attr) << II;
      SkipUntil(tok::r_paren);
      return;
    }

    if (Tok.isNot(tok::comma))
      break;

    ConsumeToken();
  }

  MatchRHSPunctuation(tok::r_paren, LHSLoc);
}

///   objc-method-proto:
///     objc-instance-method objc-method-decl objc-method-attributes[opt]
///     objc-class-method objc-method-decl objc-method-attributes[opt]
///
///   objc-instance-method: '-'
///   objc-class-method: '+'
///
///   objc-method-attributes:         [OBJC2]
///     __attribute__((deprecated))
///
Parser::DeclPtrTy Parser::ParseObjCMethodPrototype(DeclPtrTy IDecl,
                                          tok::ObjCKeywordKind MethodImplKind) {
  assert((Tok.is(tok::minus) || Tok.is(tok::plus)) && "expected +/-");

  tok::TokenKind methodType = Tok.getKind();
  SourceLocation mLoc = ConsumeToken();

  DeclPtrTy MDecl = ParseObjCMethodDecl(mLoc, methodType, IDecl,MethodImplKind);
  // Since this rule is used for both method declarations and definitions,
  // the caller is (optionally) responsible for consuming the ';'.
  return MDecl;
}

///   objc-selector:
///     identifier
///     one of
///       enum struct union if else while do for switch case default
///       break continue return goto asm sizeof typeof __alignof
///       unsigned long const short volatile signed restrict _Complex
///       in out inout bycopy byref oneway int char float double void _Bool
///
IdentifierInfo *Parser::ParseObjCSelectorPiece(SourceLocation &SelectorLoc) {
  switch (Tok.getKind()) {
  default:
    return 0;
  case tok::identifier:
  case tok::kw_asm:
  case tok::kw_auto:
  case tok::kw_bool:
  case tok::kw_break:
  case tok::kw_case:
  case tok::kw_catch:
  case tok::kw_char:
  case tok::kw_class:
  case tok::kw_const:
  case tok::kw_const_cast:
  case tok::kw_continue:
  case tok::kw_default:
  case tok::kw_delete:
  case tok::kw_do:
  case tok::kw_double:
  case tok::kw_dynamic_cast:
  case tok::kw_else:
  case tok::kw_enum:
  case tok::kw_explicit:
  case tok::kw_export:
  case tok::kw_extern:
  case tok::kw_false:
  case tok::kw_float:
  case tok::kw_for:
  case tok::kw_friend:
  case tok::kw_goto:
  case tok::kw_if:
  case tok::kw_inline:
  case tok::kw_int:
  case tok::kw_long:
  case tok::kw_mutable:
  case tok::kw_namespace:
  case tok::kw_new:
  case tok::kw_operator:
  case tok::kw_private:
  case tok::kw_protected:
  case tok::kw_public:
  case tok::kw_register:
  case tok::kw_reinterpret_cast:
  case tok::kw_restrict:
  case tok::kw_return:
  case tok::kw_short:
  case tok::kw_signed:
  case tok::kw_sizeof:
  case tok::kw_static:
  case tok::kw_static_cast:
  case tok::kw_struct:
  case tok::kw_switch:
  case tok::kw_template:
  case tok::kw_this:
  case tok::kw_throw:
  case tok::kw_true:
  case tok::kw_try:
  case tok::kw_typedef:
  case tok::kw_typeid:
  case tok::kw_typename:
  case tok::kw_typeof:
  case tok::kw_union:
  case tok::kw_unsigned:
  case tok::kw_using:
  case tok::kw_virtual:
  case tok::kw_void:
  case tok::kw_volatile:
  case tok::kw_wchar_t:
  case tok::kw_while:
  case tok::kw__Bool:
  case tok::kw__Complex:
  case tok::kw___alignof:
    IdentifierInfo *II = Tok.getIdentifierInfo();
    SelectorLoc = ConsumeToken();
    return II;
  }
}

///  objc-for-collection-in: 'in'
///
bool Parser::isTokIdentifier_in() const {
  // FIXME: May have to do additional look-ahead to only allow for
  // valid tokens following an 'in'; such as an identifier, unary operators,
  // '[' etc.
  return (getLang().ObjC2 && Tok.is(tok::identifier) &&
          Tok.getIdentifierInfo() == ObjCTypeQuals[objc_in]);
}

/// ParseObjCTypeQualifierList - This routine parses the objective-c's type
/// qualifier list and builds their bitmask representation in the input
/// argument.
///
///   objc-type-qualifiers:
///     objc-type-qualifier
///     objc-type-qualifiers objc-type-qualifier
///
void Parser::ParseObjCTypeQualifierList(ObjCDeclSpec &DS) {
  while (1) {
    if (Tok.isNot(tok::identifier))
      return;

    const IdentifierInfo *II = Tok.getIdentifierInfo();
    for (unsigned i = 0; i != objc_NumQuals; ++i) {
      if (II != ObjCTypeQuals[i])
        continue;

      ObjCDeclSpec::ObjCDeclQualifier Qual;
      switch (i) {
      default: assert(0 && "Unknown decl qualifier");
      case objc_in:     Qual = ObjCDeclSpec::DQ_In; break;
      case objc_out:    Qual = ObjCDeclSpec::DQ_Out; break;
      case objc_inout:  Qual = ObjCDeclSpec::DQ_Inout; break;
      case objc_oneway: Qual = ObjCDeclSpec::DQ_Oneway; break;
      case objc_bycopy: Qual = ObjCDeclSpec::DQ_Bycopy; break;
      case objc_byref:  Qual = ObjCDeclSpec::DQ_Byref; break;
      }
      DS.setObjCDeclQualifier(Qual);
      ConsumeToken();
      II = 0;
      break;
    }

    // If this wasn't a recognized qualifier, bail out.
    if (II) return;
  }
}

///   objc-type-name:
///     '(' objc-type-qualifiers[opt] type-name ')'
///     '(' objc-type-qualifiers[opt] ')'
///
Parser::TypeTy *Parser::ParseObjCTypeName(ObjCDeclSpec &DS) {
  assert(Tok.is(tok::l_paren) && "expected (");

  SourceLocation LParenLoc = ConsumeParen();
  SourceLocation TypeStartLoc = Tok.getLocation();

  // Parse type qualifiers, in, inout, etc.
  ParseObjCTypeQualifierList(DS);

  TypeTy *Ty = 0;
  if (isTypeSpecifierQualifier()) {
    TypeResult TypeSpec = ParseTypeName();
    if (!TypeSpec.isInvalid())
      Ty = TypeSpec.get();
  }

  if (Tok.is(tok::r_paren))
    ConsumeParen();
  else if (Tok.getLocation() == TypeStartLoc) {
    // If we didn't eat any tokens, then this isn't a type.
    Diag(Tok, diag::err_expected_type);
    SkipUntil(tok::r_paren);
  } else {
    // Otherwise, we found *something*, but didn't get a ')' in the right
    // place.  Emit an error then return what we have as the type.
    MatchRHSPunctuation(tok::r_paren, LParenLoc);
  }
  return Ty;
}

///   objc-method-decl:
///     objc-selector
///     objc-keyword-selector objc-parmlist[opt]
///     objc-type-name objc-selector
///     objc-type-name objc-keyword-selector objc-parmlist[opt]
///
///   objc-keyword-selector:
///     objc-keyword-decl
///     objc-keyword-selector objc-keyword-decl
///
///   objc-keyword-decl:
///     objc-selector ':' objc-type-name objc-keyword-attributes[opt] identifier
///     objc-selector ':' objc-keyword-attributes[opt] identifier
///     ':' objc-type-name objc-keyword-attributes[opt] identifier
///     ':' objc-keyword-attributes[opt] identifier
///
///   objc-parmlist:
///     objc-parms objc-ellipsis[opt]
///
///   objc-parms:
///     objc-parms , parameter-declaration
///
///   objc-ellipsis:
///     , ...
///
///   objc-keyword-attributes:         [OBJC2]
///     __attribute__((unused))
///
Parser::DeclPtrTy Parser::ParseObjCMethodDecl(SourceLocation mLoc,
                                              tok::TokenKind mType,
                                              DeclPtrTy IDecl,
                                          tok::ObjCKeywordKind MethodImplKind) {
  ParsingDeclRAIIObject PD(*this);

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteObjCMethodDecl(CurScope, mType == tok::minus, 
                                       /*ReturnType=*/0, IDecl);
    ConsumeCodeCompletionToken();
  }

  // Parse the return type if present.
  TypeTy *ReturnType = 0;
  ObjCDeclSpec DSRet;
  if (Tok.is(tok::l_paren))
    ReturnType = ParseObjCTypeName(DSRet);

  // If attributes exist before the method, parse them.
  llvm::OwningPtr<AttributeList> MethodAttrs;
  if (getLang().ObjC2 && Tok.is(tok::kw___attribute))
    MethodAttrs.reset(ParseGNUAttributes());

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteObjCMethodDecl(CurScope, mType == tok::minus, 
                                       ReturnType, IDecl);
    ConsumeCodeCompletionToken();
  }

  // Now parse the selector.
  SourceLocation selLoc;
  IdentifierInfo *SelIdent = ParseObjCSelectorPiece(selLoc);

  // An unnamed colon is valid.
  if (!SelIdent && Tok.isNot(tok::colon)) { // missing selector name.
    Diag(Tok, diag::err_expected_selector_for_method)
      << SourceRange(mLoc, Tok.getLocation());
    // Skip until we get a ; or {}.
    SkipUntil(tok::r_brace);
    return DeclPtrTy();
  }

  llvm::SmallVector<DeclaratorChunk::ParamInfo, 8> CParamInfo;
  if (Tok.isNot(tok::colon)) {
    // If attributes exist after the method, parse them.
    if (getLang().ObjC2 && Tok.is(tok::kw___attribute))
      MethodAttrs.reset(addAttributeLists(MethodAttrs.take(),
                                          ParseGNUAttributes()));

    Selector Sel = PP.getSelectorTable().getNullarySelector(SelIdent);
    DeclPtrTy Result
         = Actions.ActOnMethodDeclaration(mLoc, Tok.getLocation(),
                                          mType, IDecl, DSRet, ReturnType, Sel,
                                          0, 
                                          CParamInfo.data(), CParamInfo.size(),
                                          MethodAttrs.get(),
                                          MethodImplKind);
    PD.complete(Result);
    return Result;
  }

  llvm::SmallVector<IdentifierInfo *, 12> KeyIdents;
  llvm::SmallVector<Action::ObjCArgInfo, 12> ArgInfos;

  while (1) {
    Action::ObjCArgInfo ArgInfo;

    // Each iteration parses a single keyword argument.
    if (Tok.isNot(tok::colon)) {
      Diag(Tok, diag::err_expected_colon);
      break;
    }
    ConsumeToken(); // Eat the ':'.

    ArgInfo.Type = 0;
    if (Tok.is(tok::l_paren)) // Parse the argument type if present.
      ArgInfo.Type = ParseObjCTypeName(ArgInfo.DeclSpec);

    // If attributes exist before the argument name, parse them.
    ArgInfo.ArgAttrs = 0;
    if (getLang().ObjC2 && Tok.is(tok::kw___attribute))
      ArgInfo.ArgAttrs = ParseGNUAttributes();

    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected_ident); // missing argument name.
      break;
    }

    ArgInfo.Name = Tok.getIdentifierInfo();
    ArgInfo.NameLoc = Tok.getLocation();
    ConsumeToken(); // Eat the identifier.

    ArgInfos.push_back(ArgInfo);
    KeyIdents.push_back(SelIdent);

    // Check for another keyword selector.
    SourceLocation Loc;
    SelIdent = ParseObjCSelectorPiece(Loc);
    if (!SelIdent && Tok.isNot(tok::colon))
      break;
    // We have a selector or a colon, continue parsing.
  }

  bool isVariadic = false;

  // Parse the (optional) parameter list.
  while (Tok.is(tok::comma)) {
    ConsumeToken();
    if (Tok.is(tok::ellipsis)) {
      isVariadic = true;
      ConsumeToken();
      break;
    }
    DeclSpec DS;
    ParseDeclarationSpecifiers(DS);
    // Parse the declarator.
    Declarator ParmDecl(DS, Declarator::PrototypeContext);
    ParseDeclarator(ParmDecl);
    IdentifierInfo *ParmII = ParmDecl.getIdentifier();
    DeclPtrTy Param = Actions.ActOnParamDeclarator(CurScope, ParmDecl);
    CParamInfo.push_back(DeclaratorChunk::ParamInfo(ParmII,
                                                    ParmDecl.getIdentifierLoc(), 
                                                    Param,
                                                   0));

  }

  // FIXME: Add support for optional parmameter list...
  // If attributes exist after the method, parse them.
  if (getLang().ObjC2 && Tok.is(tok::kw___attribute))
    MethodAttrs.reset(addAttributeLists(MethodAttrs.take(),
                                        ParseGNUAttributes()));

  if (KeyIdents.size() == 0)
    return DeclPtrTy();
  Selector Sel = PP.getSelectorTable().getSelector(KeyIdents.size(),
                                                   &KeyIdents[0]);
  DeclPtrTy Result
       = Actions.ActOnMethodDeclaration(mLoc, Tok.getLocation(),
                                        mType, IDecl, DSRet, ReturnType, Sel,
                                        &ArgInfos[0], 
                                        CParamInfo.data(), CParamInfo.size(),
                                        MethodAttrs.get(),
                                        MethodImplKind, isVariadic);
  PD.complete(Result);
  
  // Delete referenced AttributeList objects.
  for (llvm::SmallVectorImpl<Action::ObjCArgInfo>::iterator
       I = ArgInfos.begin(), E = ArgInfos.end(); I != E; ++I)
    delete I->ArgAttrs;
  
  return Result;
}

///   objc-protocol-refs:
///     '<' identifier-list '>'
///
bool Parser::
ParseObjCProtocolReferences(llvm::SmallVectorImpl<Action::DeclPtrTy> &Protocols,
                            llvm::SmallVectorImpl<SourceLocation> &ProtocolLocs,
                            bool WarnOnDeclarations,
                            SourceLocation &LAngleLoc, SourceLocation &EndLoc) {
  assert(Tok.is(tok::less) && "expected <");

  LAngleLoc = ConsumeToken(); // the "<"

  llvm::SmallVector<IdentifierLocPair, 8> ProtocolIdents;

  while (1) {
    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteObjCProtocolReferences(ProtocolIdents.data(), 
                                                 ProtocolIdents.size());
      ConsumeCodeCompletionToken();
    }

    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected_ident);
      SkipUntil(tok::greater);
      return true;
    }
    ProtocolIdents.push_back(std::make_pair(Tok.getIdentifierInfo(),
                                       Tok.getLocation()));
    ProtocolLocs.push_back(Tok.getLocation());
    ConsumeToken();

    if (Tok.isNot(tok::comma))
      break;
    ConsumeToken();
  }

  // Consume the '>'.
  if (Tok.isNot(tok::greater)) {
    Diag(Tok, diag::err_expected_greater);
    return true;
  }

  EndLoc = ConsumeAnyToken();

  // Convert the list of protocols identifiers into a list of protocol decls.
  Actions.FindProtocolDeclaration(WarnOnDeclarations,
                                  &ProtocolIdents[0], ProtocolIdents.size(),
                                  Protocols);
  return false;
}

///   objc-class-instance-variables:
///     '{' objc-instance-variable-decl-list[opt] '}'
///
///   objc-instance-variable-decl-list:
///     objc-visibility-spec
///     objc-instance-variable-decl ';'
///     ';'
///     objc-instance-variable-decl-list objc-visibility-spec
///     objc-instance-variable-decl-list objc-instance-variable-decl ';'
///     objc-instance-variable-decl-list ';'
///
///   objc-visibility-spec:
///     @private
///     @protected
///     @public
///     @package [OBJC2]
///
///   objc-instance-variable-decl:
///     struct-declaration
///
void Parser::ParseObjCClassInstanceVariables(DeclPtrTy interfaceDecl,
                                             tok::ObjCKeywordKind visibility,
                                             SourceLocation atLoc) {
  assert(Tok.is(tok::l_brace) && "expected {");
  llvm::SmallVector<DeclPtrTy, 32> AllIvarDecls;

  ParseScope ClassScope(this, Scope::DeclScope|Scope::ClassScope);

  SourceLocation LBraceLoc = ConsumeBrace(); // the "{"

  // While we still have something to read, read the instance variables.
  while (Tok.isNot(tok::r_brace) && Tok.isNot(tok::eof)) {
    // Each iteration of this loop reads one objc-instance-variable-decl.

    // Check for extraneous top-level semicolon.
    if (Tok.is(tok::semi)) {
      Diag(Tok, diag::ext_extra_struct_semi)
        << FixItHint::CreateRemoval(Tok.getLocation());
      ConsumeToken();
      continue;
    }

    // Set the default visibility to private.
    if (Tok.is(tok::at)) { // parse objc-visibility-spec
      ConsumeToken(); // eat the @ sign
      
      if (Tok.is(tok::code_completion)) {
        Actions.CodeCompleteObjCAtVisibility(CurScope);
        ConsumeCodeCompletionToken();
      }
      
      switch (Tok.getObjCKeywordID()) {
      case tok::objc_private:
      case tok::objc_public:
      case tok::objc_protected:
      case tok::objc_package:
        visibility = Tok.getObjCKeywordID();
        ConsumeToken();
        continue;
      default:
        Diag(Tok, diag::err_objc_illegal_visibility_spec);
        continue;
      }
    }

    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteOrdinaryName(CurScope, 
                                       Action::CCC_ObjCInstanceVariableList);
      ConsumeCodeCompletionToken();
    }
    
    struct ObjCIvarCallback : FieldCallback {
      Parser &P;
      DeclPtrTy IDecl;
      tok::ObjCKeywordKind visibility;
      llvm::SmallVectorImpl<DeclPtrTy> &AllIvarDecls;

      ObjCIvarCallback(Parser &P, DeclPtrTy IDecl, tok::ObjCKeywordKind V,
                       llvm::SmallVectorImpl<DeclPtrTy> &AllIvarDecls) :
        P(P), IDecl(IDecl), visibility(V), AllIvarDecls(AllIvarDecls) {
      }

      DeclPtrTy invoke(FieldDeclarator &FD) {
        // Install the declarator into the interface decl.
        DeclPtrTy Field
          = P.Actions.ActOnIvar(P.CurScope,
                                FD.D.getDeclSpec().getSourceRange().getBegin(),
                                IDecl, FD.D, FD.BitfieldSize, visibility);
        if (Field)
          AllIvarDecls.push_back(Field);
        return Field;
      }
    } Callback(*this, interfaceDecl, visibility, AllIvarDecls);

    // Parse all the comma separated declarators.
    DeclSpec DS;
    ParseStructDeclaration(DS, Callback);

    if (Tok.is(tok::semi)) {
      ConsumeToken();
    } else {
      Diag(Tok, diag::err_expected_semi_decl_list);
      // Skip to end of block or statement
      SkipUntil(tok::r_brace, true, true);
    }
  }
  SourceLocation RBraceLoc = MatchRHSPunctuation(tok::r_brace, LBraceLoc);
  // Call ActOnFields() even if we don't have any decls. This is useful
  // for code rewriting tools that need to be aware of the empty list.
  Actions.ActOnFields(CurScope, atLoc, interfaceDecl,
                      AllIvarDecls.data(), AllIvarDecls.size(),
                      LBraceLoc, RBraceLoc, 0);
  return;
}

///   objc-protocol-declaration:
///     objc-protocol-definition
///     objc-protocol-forward-reference
///
///   objc-protocol-definition:
///     @protocol identifier
///       objc-protocol-refs[opt]
///       objc-interface-decl-list
///     @end
///
///   objc-protocol-forward-reference:
///     @protocol identifier-list ';'
///
///   "@protocol identifier ;" should be resolved as "@protocol
///   identifier-list ;": objc-interface-decl-list may not start with a
///   semicolon in the first alternative if objc-protocol-refs are omitted.
Parser::DeclPtrTy Parser::ParseObjCAtProtocolDeclaration(SourceLocation AtLoc,
                                                      AttributeList *attrList) {
  assert(Tok.isObjCAtKeyword(tok::objc_protocol) &&
         "ParseObjCAtProtocolDeclaration(): Expected @protocol");
  ConsumeToken(); // the "protocol" identifier

  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteObjCProtocolDecl(CurScope);
    ConsumeCodeCompletionToken();
  }

  if (Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_expected_ident); // missing protocol name.
    return DeclPtrTy();
  }
  // Save the protocol name, then consume it.
  IdentifierInfo *protocolName = Tok.getIdentifierInfo();
  SourceLocation nameLoc = ConsumeToken();

  if (Tok.is(tok::semi)) { // forward declaration of one protocol.
    IdentifierLocPair ProtoInfo(protocolName, nameLoc);
    ConsumeToken();
    return Actions.ActOnForwardProtocolDeclaration(AtLoc, &ProtoInfo, 1,
                                                   attrList);
  }

  if (Tok.is(tok::comma)) { // list of forward declarations.
    llvm::SmallVector<IdentifierLocPair, 8> ProtocolRefs;
    ProtocolRefs.push_back(std::make_pair(protocolName, nameLoc));

    // Parse the list of forward declarations.
    while (1) {
      ConsumeToken(); // the ','
      if (Tok.isNot(tok::identifier)) {
        Diag(Tok, diag::err_expected_ident);
        SkipUntil(tok::semi);
        return DeclPtrTy();
      }
      ProtocolRefs.push_back(IdentifierLocPair(Tok.getIdentifierInfo(),
                                               Tok.getLocation()));
      ConsumeToken(); // the identifier

      if (Tok.isNot(tok::comma))
        break;
    }
    // Consume the ';'.
    if (ExpectAndConsume(tok::semi, diag::err_expected_semi_after, "@protocol"))
      return DeclPtrTy();

    return Actions.ActOnForwardProtocolDeclaration(AtLoc,
                                                   &ProtocolRefs[0],
                                                   ProtocolRefs.size(),
                                                   attrList);
  }

  // Last, and definitely not least, parse a protocol declaration.
  SourceLocation LAngleLoc, EndProtoLoc;

  llvm::SmallVector<DeclPtrTy, 8> ProtocolRefs;
  llvm::SmallVector<SourceLocation, 8> ProtocolLocs;
  if (Tok.is(tok::less) &&
      ParseObjCProtocolReferences(ProtocolRefs, ProtocolLocs, false,
                                  LAngleLoc, EndProtoLoc))
    return DeclPtrTy();

  DeclPtrTy ProtoType =
    Actions.ActOnStartProtocolInterface(AtLoc, protocolName, nameLoc,
                                        ProtocolRefs.data(),
                                        ProtocolRefs.size(),
                                        ProtocolLocs.data(),
                                        EndProtoLoc, attrList);
  ParseObjCInterfaceDeclList(ProtoType, tok::objc_protocol);
  return ProtoType;
}

///   objc-implementation:
///     objc-class-implementation-prologue
///     objc-category-implementation-prologue
///
///   objc-class-implementation-prologue:
///     @implementation identifier objc-superclass[opt]
///       objc-class-instance-variables[opt]
///
///   objc-category-implementation-prologue:
///     @implementation identifier ( identifier )
Parser::DeclPtrTy Parser::ParseObjCAtImplementationDeclaration(
  SourceLocation atLoc) {
  assert(Tok.isObjCAtKeyword(tok::objc_implementation) &&
         "ParseObjCAtImplementationDeclaration(): Expected @implementation");
  ConsumeToken(); // the "implementation" identifier

  // Code completion after '@implementation'.
  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteObjCImplementationDecl(CurScope);
    ConsumeCodeCompletionToken();
  }

  if (Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_expected_ident); // missing class or category name.
    return DeclPtrTy();
  }
  // We have a class or category name - consume it.
  IdentifierInfo *nameId = Tok.getIdentifierInfo();
  SourceLocation nameLoc = ConsumeToken(); // consume class or category name

  if (Tok.is(tok::l_paren)) {
    // we have a category implementation.
    SourceLocation lparenLoc = ConsumeParen();
    SourceLocation categoryLoc, rparenLoc;
    IdentifierInfo *categoryId = 0;

    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteObjCImplementationCategory(CurScope, nameId, nameLoc);
      ConsumeCodeCompletionToken();
    }
    
    if (Tok.is(tok::identifier)) {
      categoryId = Tok.getIdentifierInfo();
      categoryLoc = ConsumeToken();
    } else {
      Diag(Tok, diag::err_expected_ident); // missing category name.
      return DeclPtrTy();
    }
    if (Tok.isNot(tok::r_paren)) {
      Diag(Tok, diag::err_expected_rparen);
      SkipUntil(tok::r_paren, false); // don't stop at ';'
      return DeclPtrTy();
    }
    rparenLoc = ConsumeParen();
    DeclPtrTy ImplCatType = Actions.ActOnStartCategoryImplementation(
                                    atLoc, nameId, nameLoc, categoryId,
                                    categoryLoc);
    ObjCImpDecl = ImplCatType;
    PendingObjCImpDecl.push_back(ObjCImpDecl);
    return DeclPtrTy();
  }
  // We have a class implementation
  SourceLocation superClassLoc;
  IdentifierInfo *superClassId = 0;
  if (Tok.is(tok::colon)) {
    // We have a super class
    ConsumeToken();
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected_ident); // missing super class name.
      return DeclPtrTy();
    }
    superClassId = Tok.getIdentifierInfo();
    superClassLoc = ConsumeToken(); // Consume super class name
  }
  DeclPtrTy ImplClsType = Actions.ActOnStartClassImplementation(
                                  atLoc, nameId, nameLoc,
                                  superClassId, superClassLoc);

  if (Tok.is(tok::l_brace)) // we have ivars
    ParseObjCClassInstanceVariables(ImplClsType/*FIXME*/, 
                                    tok::objc_private, atLoc);
  ObjCImpDecl = ImplClsType;
  PendingObjCImpDecl.push_back(ObjCImpDecl);
  
  return DeclPtrTy();
}

Parser::DeclPtrTy Parser::ParseObjCAtEndDeclaration(SourceRange atEnd) {
  assert(Tok.isObjCAtKeyword(tok::objc_end) &&
         "ParseObjCAtEndDeclaration(): Expected @end");
  DeclPtrTy Result = ObjCImpDecl;
  ConsumeToken(); // the "end" identifier
  if (ObjCImpDecl) {
    Actions.ActOnAtEnd(CurScope, atEnd, ObjCImpDecl);
    ObjCImpDecl = DeclPtrTy();
    PendingObjCImpDecl.pop_back();
  }
  else {
    // missing @implementation
    Diag(atEnd.getBegin(), diag::warn_expected_implementation);
  }
  return Result;
}

Parser::DeclGroupPtrTy Parser::RetrievePendingObjCImpDecl() {
  if (PendingObjCImpDecl.empty())
    return Actions.ConvertDeclToDeclGroup(DeclPtrTy());
  DeclPtrTy ImpDecl = PendingObjCImpDecl.pop_back_val();
  Actions.ActOnAtEnd(CurScope, SourceRange(), ImpDecl);
  return Actions.ConvertDeclToDeclGroup(ImpDecl);
}

///   compatibility-alias-decl:
///     @compatibility_alias alias-name  class-name ';'
///
Parser::DeclPtrTy Parser::ParseObjCAtAliasDeclaration(SourceLocation atLoc) {
  assert(Tok.isObjCAtKeyword(tok::objc_compatibility_alias) &&
         "ParseObjCAtAliasDeclaration(): Expected @compatibility_alias");
  ConsumeToken(); // consume compatibility_alias
  if (Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_expected_ident);
    return DeclPtrTy();
  }
  IdentifierInfo *aliasId = Tok.getIdentifierInfo();
  SourceLocation aliasLoc = ConsumeToken(); // consume alias-name
  if (Tok.isNot(tok::identifier)) {
    Diag(Tok, diag::err_expected_ident);
    return DeclPtrTy();
  }
  IdentifierInfo *classId = Tok.getIdentifierInfo();
  SourceLocation classLoc = ConsumeToken(); // consume class-name;
  if (Tok.isNot(tok::semi)) {
    Diag(Tok, diag::err_expected_semi_after) << "@compatibility_alias";
    return DeclPtrTy();
  }
  return Actions.ActOnCompatiblityAlias(atLoc, aliasId, aliasLoc,
                                        classId, classLoc);
}

///   property-synthesis:
///     @synthesize property-ivar-list ';'
///
///   property-ivar-list:
///     property-ivar
///     property-ivar-list ',' property-ivar
///
///   property-ivar:
///     identifier
///     identifier '=' identifier
///
Parser::DeclPtrTy Parser::ParseObjCPropertySynthesize(SourceLocation atLoc) {
  assert(Tok.isObjCAtKeyword(tok::objc_synthesize) &&
         "ParseObjCPropertyDynamic(): Expected '@synthesize'");
  SourceLocation loc = ConsumeToken(); // consume synthesize

  while (true) {
    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteObjCPropertyDefinition(CurScope, ObjCImpDecl);
      ConsumeCodeCompletionToken();
    }
    
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_synthesized_property_name);
      SkipUntil(tok::semi);
      return DeclPtrTy();
    }
    
    IdentifierInfo *propertyIvar = 0;
    IdentifierInfo *propertyId = Tok.getIdentifierInfo();
    SourceLocation propertyLoc = ConsumeToken(); // consume property name
    if (Tok.is(tok::equal)) {
      // property '=' ivar-name
      ConsumeToken(); // consume '='
      
      if (Tok.is(tok::code_completion)) {
        Actions.CodeCompleteObjCPropertySynthesizeIvar(CurScope, propertyId,
                                                       ObjCImpDecl);
        ConsumeCodeCompletionToken();
      }
      
      if (Tok.isNot(tok::identifier)) {
        Diag(Tok, diag::err_expected_ident);
        break;
      }
      propertyIvar = Tok.getIdentifierInfo();
      ConsumeToken(); // consume ivar-name
    }
    Actions.ActOnPropertyImplDecl(CurScope, atLoc, propertyLoc, true, ObjCImpDecl,
                                  propertyId, propertyIvar);
    if (Tok.isNot(tok::comma))
      break;
    ConsumeToken(); // consume ','
  }
  if (Tok.isNot(tok::semi)) {
    Diag(Tok, diag::err_expected_semi_after) << "@synthesize";
    SkipUntil(tok::semi);
  }
  else
    ConsumeToken(); // consume ';'
  return DeclPtrTy();
}

///   property-dynamic:
///     @dynamic  property-list
///
///   property-list:
///     identifier
///     property-list ',' identifier
///
Parser::DeclPtrTy Parser::ParseObjCPropertyDynamic(SourceLocation atLoc) {
  assert(Tok.isObjCAtKeyword(tok::objc_dynamic) &&
         "ParseObjCPropertyDynamic(): Expected '@dynamic'");
  SourceLocation loc = ConsumeToken(); // consume dynamic
  while (true) {
    if (Tok.is(tok::code_completion)) {
      Actions.CodeCompleteObjCPropertyDefinition(CurScope, ObjCImpDecl);
      ConsumeCodeCompletionToken();
    }
    
    if (Tok.isNot(tok::identifier)) {
      Diag(Tok, diag::err_expected_ident);
      SkipUntil(tok::semi);
      return DeclPtrTy();
    }
    
    IdentifierInfo *propertyId = Tok.getIdentifierInfo();
    SourceLocation propertyLoc = ConsumeToken(); // consume property name
    Actions.ActOnPropertyImplDecl(CurScope, atLoc, propertyLoc, false, ObjCImpDecl,
                                  propertyId, 0);

    if (Tok.isNot(tok::comma))
      break;
    ConsumeToken(); // consume ','
  }
  if (Tok.isNot(tok::semi)) {
    Diag(Tok, diag::err_expected_semi_after) << "@dynamic";
    SkipUntil(tok::semi);
  }
  else
    ConsumeToken(); // consume ';'
  return DeclPtrTy();
}

///  objc-throw-statement:
///    throw expression[opt];
///
Parser::OwningStmtResult Parser::ParseObjCThrowStmt(SourceLocation atLoc) {
  OwningExprResult Res(Actions);
  ConsumeToken(); // consume throw
  if (Tok.isNot(tok::semi)) {
    Res = ParseExpression();
    if (Res.isInvalid()) {
      SkipUntil(tok::semi);
      return StmtError();
    }
  }
  // consume ';'
  ExpectAndConsume(tok::semi, diag::err_expected_semi_after, "@throw");
  return Actions.ActOnObjCAtThrowStmt(atLoc, move(Res), CurScope);
}

/// objc-synchronized-statement:
///   @synchronized '(' expression ')' compound-statement
///
Parser::OwningStmtResult
Parser::ParseObjCSynchronizedStmt(SourceLocation atLoc) {
  ConsumeToken(); // consume synchronized
  if (Tok.isNot(tok::l_paren)) {
    Diag(Tok, diag::err_expected_lparen_after) << "@synchronized";
    return StmtError();
  }
  ConsumeParen();  // '('
  OwningExprResult Res(ParseExpression());
  if (Res.isInvalid()) {
    SkipUntil(tok::semi);
    return StmtError();
  }
  if (Tok.isNot(tok::r_paren)) {
    Diag(Tok, diag::err_expected_lbrace);
    return StmtError();
  }
  ConsumeParen();  // ')'
  if (Tok.isNot(tok::l_brace)) {
    Diag(Tok, diag::err_expected_lbrace);
    return StmtError();
  }
  // Enter a scope to hold everything within the compound stmt.  Compound
  // statements can always hold declarations.
  ParseScope BodyScope(this, Scope::DeclScope);

  OwningStmtResult SynchBody(ParseCompoundStatementBody());

  BodyScope.Exit();
  if (SynchBody.isInvalid())
    SynchBody = Actions.ActOnNullStmt(Tok.getLocation());
  return Actions.ActOnObjCAtSynchronizedStmt(atLoc, move(Res), move(SynchBody));
}

///  objc-try-catch-statement:
///    @try compound-statement objc-catch-list[opt]
///    @try compound-statement objc-catch-list[opt] @finally compound-statement
///
///  objc-catch-list:
///    @catch ( parameter-declaration ) compound-statement
///    objc-catch-list @catch ( catch-parameter-declaration ) compound-statement
///  catch-parameter-declaration:
///     parameter-declaration
///     '...' [OBJC2]
///
Parser::OwningStmtResult Parser::ParseObjCTryStmt(SourceLocation atLoc) {
  bool catch_or_finally_seen = false;

  ConsumeToken(); // consume try
  if (Tok.isNot(tok::l_brace)) {
    Diag(Tok, diag::err_expected_lbrace);
    return StmtError();
  }
  StmtVector CatchStmts(Actions);
  OwningStmtResult FinallyStmt(Actions);
  ParseScope TryScope(this, Scope::DeclScope);
  OwningStmtResult TryBody(ParseCompoundStatementBody());
  TryScope.Exit();
  if (TryBody.isInvalid())
    TryBody = Actions.ActOnNullStmt(Tok.getLocation());

  while (Tok.is(tok::at)) {
    // At this point, we need to lookahead to determine if this @ is the start
    // of an @catch or @finally.  We don't want to consume the @ token if this
    // is an @try or @encode or something else.
    Token AfterAt = GetLookAheadToken(1);
    if (!AfterAt.isObjCAtKeyword(tok::objc_catch) &&
        !AfterAt.isObjCAtKeyword(tok::objc_finally))
      break;

    SourceLocation AtCatchFinallyLoc = ConsumeToken();
    if (Tok.isObjCAtKeyword(tok::objc_catch)) {
      DeclPtrTy FirstPart;
      ConsumeToken(); // consume catch
      if (Tok.is(tok::l_paren)) {
        ConsumeParen();
        ParseScope CatchScope(this, Scope::DeclScope|Scope::AtCatchScope);
        if (Tok.isNot(tok::ellipsis)) {
          DeclSpec DS;
          ParseDeclarationSpecifiers(DS);
          // For some odd reason, the name of the exception variable is
          // optional. As a result, we need to use "PrototypeContext", because
          // we must accept either 'declarator' or 'abstract-declarator' here.
          Declarator ParmDecl(DS, Declarator::PrototypeContext);
          ParseDeclarator(ParmDecl);

          // Inform the actions module about the declarator, so it
          // gets added to the current scope.
          FirstPart = Actions.ActOnObjCExceptionDecl(CurScope, ParmDecl);
        } else
          ConsumeToken(); // consume '...'

        SourceLocation RParenLoc;

        if (Tok.is(tok::r_paren))
          RParenLoc = ConsumeParen();
        else // Skip over garbage, until we get to ')'.  Eat the ')'.
          SkipUntil(tok::r_paren, true, false);

        OwningStmtResult CatchBody(Actions, true);
        if (Tok.is(tok::l_brace))
          CatchBody = ParseCompoundStatementBody();
        else
          Diag(Tok, diag::err_expected_lbrace);
        if (CatchBody.isInvalid())
          CatchBody = Actions.ActOnNullStmt(Tok.getLocation());
        
        OwningStmtResult Catch = Actions.ActOnObjCAtCatchStmt(AtCatchFinallyLoc,
                                                              RParenLoc, 
                                                              FirstPart, 
                                                              move(CatchBody));
        if (!Catch.isInvalid())
          CatchStmts.push_back(Catch.release());
        
      } else {
        Diag(AtCatchFinallyLoc, diag::err_expected_lparen_after)
          << "@catch clause";
        return StmtError();
      }
      catch_or_finally_seen = true;
    } else {
      assert(Tok.isObjCAtKeyword(tok::objc_finally) && "Lookahead confused?");
      ConsumeToken(); // consume finally
      ParseScope FinallyScope(this, Scope::DeclScope);

      OwningStmtResult FinallyBody(Actions, true);
      if (Tok.is(tok::l_brace))
        FinallyBody = ParseCompoundStatementBody();
      else
        Diag(Tok, diag::err_expected_lbrace);
      if (FinallyBody.isInvalid())
        FinallyBody = Actions.ActOnNullStmt(Tok.getLocation());
      FinallyStmt = Actions.ActOnObjCAtFinallyStmt(AtCatchFinallyLoc,
                                                   move(FinallyBody));
      catch_or_finally_seen = true;
      break;
    }
  }
  if (!catch_or_finally_seen) {
    Diag(atLoc, diag::err_missing_catch_finally);
    return StmtError();
  }
  
  return Actions.ActOnObjCAtTryStmt(atLoc, move(TryBody), 
                                    move_arg(CatchStmts),
                                    move(FinallyStmt));
}

///   objc-method-def: objc-method-proto ';'[opt] '{' body '}'
///
Parser::DeclPtrTy Parser::ParseObjCMethodDefinition() {
  DeclPtrTy MDecl = ParseObjCMethodPrototype(ObjCImpDecl);

  PrettyStackTraceActionsDecl CrashInfo(MDecl, Tok.getLocation(), Actions,
                                        PP.getSourceManager(),
                                        "parsing Objective-C method");

  // parse optional ';'
  if (Tok.is(tok::semi)) {
    if (ObjCImpDecl) {
      Diag(Tok, diag::warn_semicolon_before_method_body)
        << FixItHint::CreateRemoval(Tok.getLocation());
    }
    ConsumeToken();
  }

  // We should have an opening brace now.
  if (Tok.isNot(tok::l_brace)) {
    Diag(Tok, diag::err_expected_method_body);

    // Skip over garbage, until we get to '{'.  Don't eat the '{'.
    SkipUntil(tok::l_brace, true, true);

    // If we didn't find the '{', bail out.
    if (Tok.isNot(tok::l_brace))
      return DeclPtrTy();
  }
  SourceLocation BraceLoc = Tok.getLocation();

  // Enter a scope for the method body.
  ParseScope BodyScope(this,
                       Scope::ObjCMethodScope|Scope::FnScope|Scope::DeclScope);

  // Tell the actions module that we have entered a method definition with the
  // specified Declarator for the method.
  Actions.ActOnStartOfObjCMethodDef(CurScope, MDecl);

  OwningStmtResult FnBody(ParseCompoundStatementBody());

  // If the function body could not be parsed, make a bogus compoundstmt.
  if (FnBody.isInvalid())
    FnBody = Actions.ActOnCompoundStmt(BraceLoc, BraceLoc,
                                       MultiStmtArg(Actions), false);

  // TODO: Pass argument information.
  Actions.ActOnFinishFunctionBody(MDecl, move(FnBody));

  // Leave the function body scope.
  BodyScope.Exit();

  return MDecl;
}

Parser::OwningStmtResult Parser::ParseObjCAtStatement(SourceLocation AtLoc) {
  if (Tok.is(tok::code_completion)) {
    Actions.CodeCompleteObjCAtStatement(CurScope);
    ConsumeCodeCompletionToken();
    return StmtError();
  }
  
  if (Tok.isObjCAtKeyword(tok::objc_try))
    return ParseObjCTryStmt(AtLoc);
  
  if (Tok.isObjCAtKeyword(tok::objc_throw))
    return ParseObjCThrowStmt(AtLoc);
  
  if (Tok.isObjCAtKeyword(tok::objc_synchronized))
    return ParseObjCSynchronizedStmt(AtLoc);
  
  OwningExprResult Res(ParseExpressionWithLeadingAt(AtLoc));
  if (Res.isInvalid()) {
    // If the expression is invalid, skip ahead to the next semicolon. Not
    // doing this opens us up to the possibility of infinite loops if
    // ParseExpression does not consume any tokens.
    SkipUntil(tok::semi);
    return StmtError();
  }
  
  // Otherwise, eat the semicolon.
  ExpectAndConsume(tok::semi, diag::err_expected_semi_after_expr);
  return Actions.ActOnExprStmt(Actions.MakeFullExpr(Res));
}

Parser::OwningExprResult Parser::ParseObjCAtExpression(SourceLocation AtLoc) {
  switch (Tok.getKind()) {
  case tok::code_completion:
    Actions.CodeCompleteObjCAtExpression(CurScope);
    ConsumeCodeCompletionToken();
    return ExprError();

  case tok::string_literal:    // primary-expression: string-literal
  case tok::wide_string_literal:
    return ParsePostfixExpressionSuffix(ParseObjCStringLiteral(AtLoc));
  default:
    if (Tok.getIdentifierInfo() == 0)
      return ExprError(Diag(AtLoc, diag::err_unexpected_at));

    switch (Tok.getIdentifierInfo()->getObjCKeywordID()) {
    case tok::objc_encode:
      return ParsePostfixExpressionSuffix(ParseObjCEncodeExpression(AtLoc));
    case tok::objc_protocol:
      return ParsePostfixExpressionSuffix(ParseObjCProtocolExpression(AtLoc));
    case tok::objc_selector:
      return ParsePostfixExpressionSuffix(ParseObjCSelectorExpression(AtLoc));
    default:
      return ExprError(Diag(AtLoc, diag::err_unexpected_at));
    }
  }
}

/// \brirg Parse the receiver of an Objective-C++ message send.
///
/// This routine parses the receiver of a message send in
/// Objective-C++ either as a type or as an expression. Note that this
/// routine must not be called to parse a send to 'super', since it
/// has no way to return such a result.
/// 
/// \param IsExpr Whether the receiver was parsed as an expression.
///
/// \param TypeOrExpr If the receiver was parsed as an expression (\c
/// IsExpr is true), the parsed expression. If the receiver was parsed
/// as a type (\c IsExpr is false), the parsed type.
///
/// \returns True if an error occurred during parsing or semantic
/// analysis, in which case the arguments do not have valid
/// values. Otherwise, returns false for a successful parse.
///
///   objc-receiver: [C++]
///     'super' [not parsed here]
///     expression
///     simple-type-specifier
///     typename-specifier

bool Parser::ParseObjCXXMessageReceiver(bool &IsExpr, void *&TypeOrExpr) {
  if (Tok.is(tok::identifier) || Tok.is(tok::coloncolon) || 
      Tok.is(tok::kw_typename) || Tok.is(tok::annot_cxxscope))
    TryAnnotateTypeOrScopeToken();

  if (!isCXXSimpleTypeSpecifier()) {
    //   objc-receiver:
    //     expression
    OwningExprResult Receiver = ParseExpression();
    if (Receiver.isInvalid())
      return true;

    IsExpr = true;
    TypeOrExpr = Receiver.take();
    return false;
  }

  // objc-receiver:
  //   typename-specifier
  //   simple-type-specifier
  //   expression (that starts with one of the above)
  DeclSpec DS;
  ParseCXXSimpleTypeSpecifier(DS);
  
  if (Tok.is(tok::l_paren)) {
    // If we see an opening parentheses at this point, we are
    // actually parsing an expression that starts with a
    // function-style cast, e.g.,
    //
    //   postfix-expression:
    //     simple-type-specifier ( expression-list [opt] )
    //     typename-specifier ( expression-list [opt] )
    //
    // Parse the remainder of this case, then the (optional)
    // postfix-expression suffix, followed by the (optional)
    // right-hand side of the binary expression. We have an
    // instance method.
    OwningExprResult Receiver = ParseCXXTypeConstructExpression(DS);
    if (!Receiver.isInvalid())
      Receiver = ParsePostfixExpressionSuffix(move(Receiver));
    if (!Receiver.isInvalid())
      Receiver = ParseRHSOfBinaryExpression(move(Receiver), prec::Comma);
    if (Receiver.isInvalid())
      return true;

    IsExpr = true;
    TypeOrExpr = Receiver.take();
    return false;
  }
  
  // We have a class message. Turn the simple-type-specifier or
  // typename-specifier we parsed into a type and parse the
  // remainder of the class message.
  Declarator DeclaratorInfo(DS, Declarator::TypeNameContext);
  TypeResult Type = Actions.ActOnTypeName(CurScope, DeclaratorInfo);
  if (Type.isInvalid())
    return true;

  IsExpr = false;
  TypeOrExpr = Type.get();
  return false;
}

///   objc-message-expr:
///     '[' objc-receiver objc-message-args ']'
///
///   objc-receiver: [C]
///     'super'
///     expression
///     class-name
///     type-name
///
Parser::OwningExprResult Parser::ParseObjCMessageExpression() {
  assert(Tok.is(tok::l_square) && "'[' expected");
  SourceLocation LBracLoc = ConsumeBracket(); // consume '['

  if (getLang().CPlusPlus) {
    // We completely separate the C and C++ cases because C++ requires
    // more complicated (read: slower) parsing. 
    
    // Handle send to super.  
    // FIXME: This doesn't benefit from the same typo-correction we
    // get in Objective-C.
    if (Tok.is(tok::identifier) && Tok.getIdentifierInfo() == Ident_super &&
        NextToken().isNot(tok::period) && CurScope->isInObjcMethodScope())
      return ParseObjCMessageExpressionBody(LBracLoc, ConsumeToken(), 0, 
                                            ExprArg(Actions));

    // Parse the receiver, which is either a type or an expression.
    bool IsExpr;
    void *TypeOrExpr;
    if (ParseObjCXXMessageReceiver(IsExpr, TypeOrExpr)) {
      SkipUntil(tok::r_square);
      return ExprError();
    }

    if (IsExpr)
      return ParseObjCMessageExpressionBody(LBracLoc, SourceLocation(), 0,
                                         OwningExprResult(Actions, TypeOrExpr));

    return ParseObjCMessageExpressionBody(LBracLoc, SourceLocation(), 
                                          TypeOrExpr, ExprArg(Actions));
  } else if (Tok.is(tok::identifier)) {
    IdentifierInfo *Name = Tok.getIdentifierInfo();
    SourceLocation NameLoc = Tok.getLocation();
    TypeTy *ReceiverType;
    switch (Actions.getObjCMessageKind(CurScope, Name, NameLoc,
                                       Name == Ident_super,
                                       NextToken().is(tok::period),
                                       ReceiverType)) {
    case Action::ObjCSuperMessage:
      return ParseObjCMessageExpressionBody(LBracLoc, ConsumeToken(), 0,
                                            ExprArg(Actions));

    case Action::ObjCClassMessage:
      if (!ReceiverType) {
        SkipUntil(tok::r_square);
        return ExprError();
      }

      ConsumeToken(); // the type name

      return ParseObjCMessageExpressionBody(LBracLoc, SourceLocation(), 
                                            ReceiverType,
                                            ExprArg(Actions));
        
    case Action::ObjCInstanceMessage:
      // Fall through to parse an expression.
      break;
    }
  }
  
  // Otherwise, an arbitrary expression can be the receiver of a send.
  OwningExprResult Res(ParseExpression());
  if (Res.isInvalid()) {
    SkipUntil(tok::r_square);
    return move(Res);
  }

  return ParseObjCMessageExpressionBody(LBracLoc, SourceLocation(), 0, 
                                        move(Res));
}

/// \brief Parse the remainder of an Objective-C message following the
/// '[' objc-receiver.
///
/// This routine handles sends to super, class messages (sent to a
/// class name), and instance messages (sent to an object), and the
/// target is represented by \p SuperLoc, \p ReceiverType, or \p
/// ReceiverExpr, respectively. Only one of these parameters may have
/// a valid value.
///
/// \param LBracLoc The location of the opening '['.
///
/// \param SuperLoc If this is a send to 'super', the location of the
/// 'super' keyword that indicates a send to the superclass.
///
/// \param ReceiverType If this is a class message, the type of the
/// class we are sending a message to.
///
/// \param ReceiverExpr If this is an instance message, the expression
/// used to compute the receiver object.
///
///   objc-message-args:
///     objc-selector
///     objc-keywordarg-list
///
///   objc-keywordarg-list:
///     objc-keywordarg
///     objc-keywordarg-list objc-keywordarg
///
///   objc-keywordarg:
///     selector-name[opt] ':' objc-keywordexpr
///
///   objc-keywordexpr:
///     nonempty-expr-list
///
///   nonempty-expr-list:
///     assignment-expression
///     nonempty-expr-list , assignment-expression
///
Parser::OwningExprResult
Parser::ParseObjCMessageExpressionBody(SourceLocation LBracLoc,
                                       SourceLocation SuperLoc,
                                       TypeTy *ReceiverType,
                                       ExprArg ReceiverExpr) {
  if (Tok.is(tok::code_completion)) {
    if (SuperLoc.isValid())
      Actions.CodeCompleteObjCSuperMessage(CurScope, SuperLoc, 0, 0);
    else if (ReceiverType)
      Actions.CodeCompleteObjCClassMessage(CurScope, ReceiverType, 0, 0);
    else
      Actions.CodeCompleteObjCInstanceMessage(CurScope, ReceiverExpr.get(), 
                                              0, 0);
    ConsumeCodeCompletionToken();
  }
  
  // Parse objc-selector
  SourceLocation Loc;
  IdentifierInfo *selIdent = ParseObjCSelectorPiece(Loc);

  SourceLocation SelectorLoc = Loc;

  llvm::SmallVector<IdentifierInfo *, 12> KeyIdents;
  ExprVector KeyExprs(Actions);

  if (Tok.is(tok::colon)) {
    while (1) {
      // Each iteration parses a single keyword argument.
      KeyIdents.push_back(selIdent);

      if (Tok.isNot(tok::colon)) {
        Diag(Tok, diag::err_expected_colon);
        // We must manually skip to a ']', otherwise the expression skipper will
        // stop at the ']' when it skips to the ';'.  We want it to skip beyond
        // the enclosing expression.
        SkipUntil(tok::r_square);
        return ExprError();
      }

      ConsumeToken(); // Eat the ':'.
      ///  Parse the expression after ':'
      OwningExprResult Res(ParseAssignmentExpression());
      if (Res.isInvalid()) {
        // We must manually skip to a ']', otherwise the expression skipper will
        // stop at the ']' when it skips to the ';'.  We want it to skip beyond
        // the enclosing expression.
        SkipUntil(tok::r_square);
        return move(Res);
      }

      // We have a valid expression.
      KeyExprs.push_back(Res.release());

      // Code completion after each argument.
      if (Tok.is(tok::code_completion)) {
        if (SuperLoc.isValid())
          Actions.CodeCompleteObjCSuperMessage(CurScope, SuperLoc, 
                                               KeyIdents.data(), 
                                               KeyIdents.size());
        else if (ReceiverType)
          Actions.CodeCompleteObjCClassMessage(CurScope, ReceiverType,
                                               KeyIdents.data(), 
                                               KeyIdents.size());
        else
          Actions.CodeCompleteObjCInstanceMessage(CurScope, ReceiverExpr.get(),
                                                  KeyIdents.data(), 
                                                  KeyIdents.size());
        ConsumeCodeCompletionToken();
      }
            
      // Check for another keyword selector.
      selIdent = ParseObjCSelectorPiece(Loc);
      if (!selIdent && Tok.isNot(tok::colon))
        break;
      // We have a selector or a colon, continue parsing.
    }
    // Parse the, optional, argument list, comma separated.
    while (Tok.is(tok::comma)) {
      ConsumeToken(); // Eat the ','.
      ///  Parse the expression after ','
      OwningExprResult Res(ParseAssignmentExpression());
      if (Res.isInvalid()) {
        // We must manually skip to a ']', otherwise the expression skipper will
        // stop at the ']' when it skips to the ';'.  We want it to skip beyond
        // the enclosing expression.
        SkipUntil(tok::r_square);
        return move(Res);
      }

      // We have a valid expression.
      KeyExprs.push_back(Res.release());
    }
  } else if (!selIdent) {
    Diag(Tok, diag::err_expected_ident); // missing selector name.

    // We must manually skip to a ']', otherwise the expression skipper will
    // stop at the ']' when it skips to the ';'.  We want it to skip beyond
    // the enclosing expression.
    SkipUntil(tok::r_square);
    return ExprError();
  }
    
  if (Tok.isNot(tok::r_square)) {
    if (Tok.is(tok::identifier))
      Diag(Tok, diag::err_expected_colon);
    else
      Diag(Tok, diag::err_expected_rsquare);
    // We must manually skip to a ']', otherwise the expression skipper will
    // stop at the ']' when it skips to the ';'.  We want it to skip beyond
    // the enclosing expression.
    SkipUntil(tok::r_square);
    return ExprError();
  }

  SourceLocation RBracLoc = ConsumeBracket(); // consume ']'

  unsigned nKeys = KeyIdents.size();
  if (nKeys == 0)
    KeyIdents.push_back(selIdent);
  Selector Sel = PP.getSelectorTable().getSelector(nKeys, &KeyIdents[0]);

  if (SuperLoc.isValid())
    return Actions.ActOnSuperMessage(CurScope, SuperLoc, Sel,
                                     LBracLoc, SelectorLoc, RBracLoc,
                                     Action::MultiExprArg(Actions, 
                                                          KeyExprs.take(),
                                                          KeyExprs.size()));
  else if (ReceiverType)
    return Actions.ActOnClassMessage(CurScope, ReceiverType, Sel,
                                     LBracLoc, SelectorLoc, RBracLoc,
                                     Action::MultiExprArg(Actions, 
                                                          KeyExprs.take(), 
                                                          KeyExprs.size()));
  return Actions.ActOnInstanceMessage(CurScope, move(ReceiverExpr), Sel,
                                      LBracLoc, SelectorLoc, RBracLoc,
                                      Action::MultiExprArg(Actions, 
                                                           KeyExprs.take(), 
                                                           KeyExprs.size()));
}

Parser::OwningExprResult Parser::ParseObjCStringLiteral(SourceLocation AtLoc) {
  OwningExprResult Res(ParseStringLiteralExpression());
  if (Res.isInvalid()) return move(Res);

  // @"foo" @"bar" is a valid concatenated string.  Eat any subsequent string
  // expressions.  At this point, we know that the only valid thing that starts
  // with '@' is an @"".
  llvm::SmallVector<SourceLocation, 4> AtLocs;
  ExprVector AtStrings(Actions);
  AtLocs.push_back(AtLoc);
  AtStrings.push_back(Res.release());

  while (Tok.is(tok::at)) {
    AtLocs.push_back(ConsumeToken()); // eat the @.

    // Invalid unless there is a string literal.
    if (!isTokenStringLiteral())
      return ExprError(Diag(Tok, diag::err_objc_concat_string));

    OwningExprResult Lit(ParseStringLiteralExpression());
    if (Lit.isInvalid())
      return move(Lit);

    AtStrings.push_back(Lit.release());
  }

  return Owned(Actions.ParseObjCStringLiteral(&AtLocs[0], AtStrings.take(),
                                              AtStrings.size()));
}

///    objc-encode-expression:
///      @encode ( type-name )
Parser::OwningExprResult
Parser::ParseObjCEncodeExpression(SourceLocation AtLoc) {
  assert(Tok.isObjCAtKeyword(tok::objc_encode) && "Not an @encode expression!");

  SourceLocation EncLoc = ConsumeToken();

  if (Tok.isNot(tok::l_paren))
    return ExprError(Diag(Tok, diag::err_expected_lparen_after) << "@encode");

  SourceLocation LParenLoc = ConsumeParen();

  TypeResult Ty = ParseTypeName();

  SourceLocation RParenLoc = MatchRHSPunctuation(tok::r_paren, LParenLoc);

  if (Ty.isInvalid())
    return ExprError();

  return Owned(Actions.ParseObjCEncodeExpression(AtLoc, EncLoc, LParenLoc,
                                                 Ty.get(), RParenLoc));
}

///     objc-protocol-expression
///       @protocol ( protocol-name )
Parser::OwningExprResult
Parser::ParseObjCProtocolExpression(SourceLocation AtLoc) {
  SourceLocation ProtoLoc = ConsumeToken();

  if (Tok.isNot(tok::l_paren))
    return ExprError(Diag(Tok, diag::err_expected_lparen_after) << "@protocol");

  SourceLocation LParenLoc = ConsumeParen();

  if (Tok.isNot(tok::identifier))
    return ExprError(Diag(Tok, diag::err_expected_ident));

  IdentifierInfo *protocolId = Tok.getIdentifierInfo();
  ConsumeToken();

  SourceLocation RParenLoc = MatchRHSPunctuation(tok::r_paren, LParenLoc);

  return Owned(Actions.ParseObjCProtocolExpression(protocolId, AtLoc, ProtoLoc,
                                                   LParenLoc, RParenLoc));
}

///     objc-selector-expression
///       @selector '(' objc-keyword-selector ')'
Parser::OwningExprResult
Parser::ParseObjCSelectorExpression(SourceLocation AtLoc) {
  SourceLocation SelectorLoc = ConsumeToken();

  if (Tok.isNot(tok::l_paren))
    return ExprError(Diag(Tok, diag::err_expected_lparen_after) << "@selector");

  llvm::SmallVector<IdentifierInfo *, 12> KeyIdents;
  SourceLocation LParenLoc = ConsumeParen();
  SourceLocation sLoc;
  IdentifierInfo *SelIdent = ParseObjCSelectorPiece(sLoc);
  if (!SelIdent && Tok.isNot(tok::colon)) // missing selector name.
    return ExprError(Diag(Tok, diag::err_expected_ident));

  KeyIdents.push_back(SelIdent);
  unsigned nColons = 0;
  if (Tok.isNot(tok::r_paren)) {
    while (1) {
      if (Tok.isNot(tok::colon))
        return ExprError(Diag(Tok, diag::err_expected_colon));

      nColons++;
      ConsumeToken(); // Eat the ':'.
      if (Tok.is(tok::r_paren))
        break;
      // Check for another keyword selector.
      SourceLocation Loc;
      SelIdent = ParseObjCSelectorPiece(Loc);
      KeyIdents.push_back(SelIdent);
      if (!SelIdent && Tok.isNot(tok::colon))
        break;
    }
  }
  SourceLocation RParenLoc = MatchRHSPunctuation(tok::r_paren, LParenLoc);
  Selector Sel = PP.getSelectorTable().getSelector(nColons, &KeyIdents[0]);
  return Owned(Actions.ParseObjCSelectorExpression(Sel, AtLoc, SelectorLoc,
                                                   LParenLoc, RParenLoc));
 }
