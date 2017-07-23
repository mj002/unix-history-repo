//===- ClangOptionDocEmitter.cpp - Documentation for command line flags ---===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
// FIXME: Once this has stabilized, consider moving it to LLVM.
//
//===----------------------------------------------------------------------===//

#include "llvm/TableGen/Error.h"
#include "llvm/ADT/STLExtras.h"
#include "llvm/ADT/SmallString.h"
#include "llvm/ADT/StringSwitch.h"
#include "llvm/ADT/Twine.h"
#include "llvm/TableGen/Record.h"
#include "llvm/TableGen/TableGenBackend.h"
#include <cctype>
#include <cstring>
#include <map>

using namespace llvm;

namespace clang {
namespace docs {
namespace {
struct DocumentedOption {
  Record *Option;
  std::vector<Record*> Aliases;
};
struct DocumentedGroup;
struct Documentation {
  std::vector<DocumentedGroup> Groups;
  std::vector<DocumentedOption> Options;
};
struct DocumentedGroup : Documentation {
  Record *Group;
};

// Reorganize the records into a suitable form for emitting documentation.
Documentation extractDocumentation(RecordKeeper &Records) {
  Documentation Result;

  // Build the tree of groups. The root in the tree is the fake option group
  // (Record*)nullptr, which contains all top-level groups and options.
  std::map<Record*, std::vector<Record*> > OptionsInGroup;
  std::map<Record*, std::vector<Record*> > GroupsInGroup;
  std::map<Record*, std::vector<Record*> > Aliases;

  std::map<std::string, Record*> OptionsByName;
  for (Record *R : Records.getAllDerivedDefinitions("Option"))
    OptionsByName[R->getValueAsString("Name")] = R;

  auto Flatten = [](Record *R) {
    return R->getValue("DocFlatten") && R->getValueAsBit("DocFlatten");
  };

  auto SkipFlattened = [&](Record *R) -> Record* {
    while (R && Flatten(R)) {
      auto *G = dyn_cast<DefInit>(R->getValueInit("Group"));
      if (!G)
        return nullptr;
      R = G->getDef();
    }
    return R;
  };

  for (Record *R : Records.getAllDerivedDefinitions("OptionGroup")) {
    if (Flatten(R))
      continue;

    Record *Group = nullptr;
    if (auto *G = dyn_cast<DefInit>(R->getValueInit("Group")))
      Group = SkipFlattened(G->getDef());
    GroupsInGroup[Group].push_back(R);
  }

  for (Record *R : Records.getAllDerivedDefinitions("Option")) {
    if (auto *A = dyn_cast<DefInit>(R->getValueInit("Alias"))) {
      Aliases[A->getDef()].push_back(R);
      continue;
    }

    // Pretend no-X and Xno-Y options are aliases of X and XY.
    std::string Name = R->getValueAsString("Name");
    if (Name.size() >= 4) {
      if (Name.substr(0, 3) == "no-" && OptionsByName[Name.substr(3)]) {
        Aliases[OptionsByName[Name.substr(3)]].push_back(R);
        continue;
      }
      if (Name.substr(1, 3) == "no-" && OptionsByName[Name[0] + Name.substr(4)]) {
        Aliases[OptionsByName[Name[0] + Name.substr(4)]].push_back(R);
        continue;
      }
    }

    Record *Group = nullptr;
    if (auto *G = dyn_cast<DefInit>(R->getValueInit("Group")))
      Group = SkipFlattened(G->getDef());
    OptionsInGroup[Group].push_back(R);
  }

  auto CompareByName = [](Record *A, Record *B) {
    return A->getValueAsString("Name") < B->getValueAsString("Name");
  };

  auto CompareByLocation = [](Record *A, Record *B) {
    return A->getLoc()[0].getPointer() < B->getLoc()[0].getPointer();
  };

  auto DocumentationForOption = [&](Record *R) -> DocumentedOption {
    auto &A = Aliases[R];
    std::sort(A.begin(), A.end(), CompareByName);
    return {R, std::move(A)};
  };

  std::function<Documentation(Record *)> DocumentationForGroup =
      [&](Record *R) -> Documentation {
    Documentation D;

    auto &Groups = GroupsInGroup[R];
    std::sort(Groups.begin(), Groups.end(), CompareByLocation);
    for (Record *G : Groups) {
      D.Groups.emplace_back();
      D.Groups.back().Group = G;
      Documentation &Base = D.Groups.back();
      Base = DocumentationForGroup(G);
    }

    auto &Options = OptionsInGroup[R];
    std::sort(Options.begin(), Options.end(), CompareByName);
    for (Record *O : Options)
      D.Options.push_back(DocumentationForOption(O));

    return D;
  };

  return DocumentationForGroup(nullptr);
}

// Get the first and successive separators to use for an OptionKind.
std::pair<StringRef,StringRef> getSeparatorsForKind(const Record *OptionKind) {
  return StringSwitch<std::pair<StringRef, StringRef>>(OptionKind->getName())
    .Cases("KIND_JOINED", "KIND_JOINED_OR_SEPARATE",
           "KIND_JOINED_AND_SEPARATE",
           "KIND_REMAINING_ARGS_JOINED", {"", " "})
    .Case("KIND_COMMAJOINED", {"", ","})
    .Default({" ", " "});
}

const unsigned UnlimitedArgs = unsigned(-1);

// Get the number of arguments expected for an option, or -1 if any number of
// arguments are accepted.
unsigned getNumArgsForKind(Record *OptionKind, const Record *Option) {
  return StringSwitch<unsigned>(OptionKind->getName())
    .Cases("KIND_JOINED", "KIND_JOINED_OR_SEPARATE", "KIND_SEPARATE", 1)
    .Cases("KIND_REMAINING_ARGS", "KIND_REMAINING_ARGS_JOINED",
           "KIND_COMMAJOINED", UnlimitedArgs)
    .Case("KIND_JOINED_AND_SEPARATE", 2)
    .Case("KIND_MULTIARG", Option->getValueAsInt("NumArgs"))
    .Default(0);
}

bool hasFlag(const Record *OptionOrGroup, StringRef OptionFlag) {
  for (const Record *Flag : OptionOrGroup->getValueAsListOfDefs("Flags"))
    if (Flag->getName() == OptionFlag)
      return true;
  return false;
}

bool isExcluded(const Record *OptionOrGroup, const Record *DocInfo) {
  // FIXME: Provide a flag to specify the set of exclusions.
  for (StringRef Exclusion : DocInfo->getValueAsListOfStrings("ExcludedFlags"))
    if (hasFlag(OptionOrGroup, Exclusion))
      return true;
  return false;
}

std::string escapeRST(StringRef Str) {
  std::string Out;
  for (auto K : Str) {
    if (StringRef("`*|_[]\\").count(K))
      Out.push_back('\\');
    Out.push_back(K);
  }
  return Out;
}

StringRef getSphinxOptionID(StringRef OptionName) {
  for (auto I = OptionName.begin(), E = OptionName.end(); I != E; ++I)
    if (!isalnum(*I) && *I != '-')
      return OptionName.substr(0, I - OptionName.begin());
  return OptionName;
}

bool canSphinxCopeWithOption(const Record *Option) {
  // HACK: Work arond sphinx's inability to cope with punctuation-only options
  // such as /? by suppressing them from the option list.
  for (char C : Option->getValueAsString("Name"))
    if (isalnum(C))
      return true;
  return false;
}

void emitHeading(int Depth, std::string Heading, raw_ostream &OS) {
  assert(Depth < 8 && "groups nested too deeply");
  OS << Heading << '\n'
     << std::string(Heading.size(), "=~-_'+<>"[Depth]) << "\n";
}

/// Get the value of field \p Primary, if possible. If \p Primary does not
/// exist, get the value of \p Fallback and escape it for rST emission.
std::string getRSTStringWithTextFallback(const Record *R, StringRef Primary,
                                         StringRef Fallback) {
  for (auto Field : {Primary, Fallback}) {
    if (auto *V = R->getValue(Field)) {
      StringRef Value;
      if (auto *SV = dyn_cast_or_null<StringInit>(V->getValue()))
        Value = SV->getValue();
      else if (auto *CV = dyn_cast_or_null<CodeInit>(V->getValue()))
        Value = CV->getValue();
      if (!Value.empty())
        return Field == Primary ? Value.str() : escapeRST(Value);
    }
  }
  return StringRef();
}

void emitOptionWithArgs(StringRef Prefix, const Record *Option,
                        ArrayRef<StringRef> Args, raw_ostream &OS) {
  OS << Prefix << escapeRST(Option->getValueAsString("Name"));

  std::pair<StringRef, StringRef> Separators =
      getSeparatorsForKind(Option->getValueAsDef("Kind"));

  StringRef Separator = Separators.first;
  for (auto Arg : Args) {
    OS << Separator << escapeRST(Arg);
    Separator = Separators.second;
  }
}

void emitOptionName(StringRef Prefix, const Record *Option, raw_ostream &OS) {
  // Find the arguments to list after the option.
  unsigned NumArgs = getNumArgsForKind(Option->getValueAsDef("Kind"), Option);

  std::vector<std::string> Args;
  if (!Option->isValueUnset("MetaVarName"))
    Args.push_back(Option->getValueAsString("MetaVarName"));
  else if (NumArgs == 1)
    Args.push_back("<arg>");

  while (Args.size() < NumArgs) {
    Args.push_back(("<arg" + Twine(Args.size() + 1) + ">").str());
    // Use '--args <arg1> <arg2>...' if any number of args are allowed.
    if (Args.size() == 2 && NumArgs == UnlimitedArgs) {
      Args.back() += "...";
      break;
    }
  }

  emitOptionWithArgs(Prefix, Option, std::vector<StringRef>(Args.begin(), Args.end()), OS);

  auto AliasArgs = Option->getValueAsListOfStrings("AliasArgs");
  if (!AliasArgs.empty()) {
    Record *Alias = Option->getValueAsDef("Alias");
    OS << " (equivalent to ";
    emitOptionWithArgs(
        Alias->getValueAsListOfStrings("Prefixes").front(), Alias,
        AliasArgs, OS);
    OS << ")";
  }
}

bool emitOptionNames(const Record *Option, raw_ostream &OS, bool EmittedAny) {
  for (auto &Prefix : Option->getValueAsListOfStrings("Prefixes")) {
    if (EmittedAny)
      OS << ", ";
    emitOptionName(Prefix, Option, OS);
    EmittedAny = true;
  }
  return EmittedAny;
}

template <typename Fn>
void forEachOptionName(const DocumentedOption &Option, const Record *DocInfo,
                       Fn F) {
  F(Option.Option);

  for (auto *Alias : Option.Aliases)
    if (!isExcluded(Alias, DocInfo) && canSphinxCopeWithOption(Option.Option))
      F(Alias);
}

void emitOption(const DocumentedOption &Option, const Record *DocInfo,
                raw_ostream &OS) {
  if (isExcluded(Option.Option, DocInfo))
    return;
  if (Option.Option->getValueAsDef("Kind")->getName() == "KIND_UNKNOWN" ||
      Option.Option->getValueAsDef("Kind")->getName() == "KIND_INPUT")
    return;
  if (!canSphinxCopeWithOption(Option.Option))
    return;

  // HACK: Emit a different program name with each option to work around
  // sphinx's inability to cope with options that differ only by punctuation
  // (eg -ObjC vs -ObjC++, -G vs -G=).
  std::vector<std::string> SphinxOptionIDs;
  forEachOptionName(Option, DocInfo, [&](const Record *Option) {
    for (auto &Prefix : Option->getValueAsListOfStrings("Prefixes"))
      SphinxOptionIDs.push_back(
          getSphinxOptionID((Prefix + Option->getValueAsString("Name")).str()));
  });
  assert(!SphinxOptionIDs.empty() && "no flags for option");
  static std::map<std::string, int> NextSuffix;
  int SphinxWorkaroundSuffix = NextSuffix[*std::max_element(
      SphinxOptionIDs.begin(), SphinxOptionIDs.end(),
      [&](const std::string &A, const std::string &B) {
        return NextSuffix[A] < NextSuffix[B];
      })];
  for (auto &S : SphinxOptionIDs)
    NextSuffix[S] = SphinxWorkaroundSuffix + 1;
  if (SphinxWorkaroundSuffix)
    OS << ".. program:: " << DocInfo->getValueAsString("Program")
       << SphinxWorkaroundSuffix << "\n";

  // Emit the names of the option.
  OS << ".. option:: ";
  bool EmittedAny = false;
  forEachOptionName(Option, DocInfo, [&](const Record *Option) {
    EmittedAny = emitOptionNames(Option, OS, EmittedAny);
  });
  if (SphinxWorkaroundSuffix)
    OS << "\n.. program:: " << DocInfo->getValueAsString("Program");
  OS << "\n\n";

  // Emit the description, if we have one.
  std::string Description =
      getRSTStringWithTextFallback(Option.Option, "DocBrief", "HelpText");
  if (!Description.empty())
    OS << Description << "\n\n";
}

void emitDocumentation(int Depth, const Documentation &Doc,
                       const Record *DocInfo, raw_ostream &OS);

void emitGroup(int Depth, const DocumentedGroup &Group, const Record *DocInfo,
               raw_ostream &OS) {
  if (isExcluded(Group.Group, DocInfo))
    return;

  emitHeading(Depth,
              getRSTStringWithTextFallback(Group.Group, "DocName", "Name"), OS);

  // Emit the description, if we have one.
  std::string Description =
      getRSTStringWithTextFallback(Group.Group, "DocBrief", "HelpText");
  if (!Description.empty())
    OS << Description << "\n\n";

  // Emit contained options and groups.
  emitDocumentation(Depth + 1, Group, DocInfo, OS);
}

void emitDocumentation(int Depth, const Documentation &Doc,
                       const Record *DocInfo, raw_ostream &OS) {
  for (auto &O : Doc.Options)
    emitOption(O, DocInfo, OS);
  for (auto &G : Doc.Groups)
    emitGroup(Depth, G, DocInfo, OS);
}

}  // namespace
}  // namespace docs

void EmitClangOptDocs(RecordKeeper &Records, raw_ostream &OS) {
  using namespace docs;

  const Record *DocInfo = Records.getDef("GlobalDocumentation");
  if (!DocInfo) {
    PrintFatalError("The GlobalDocumentation top-level definition is missing, "
                    "no documentation will be generated.");
    return;
  }
  OS << DocInfo->getValueAsString("Intro") << "\n";
  OS << ".. program:: " << DocInfo->getValueAsString("Program") << "\n";

  emitDocumentation(0, extractDocumentation(Records), DocInfo, OS);
}
} // end namespace clang
