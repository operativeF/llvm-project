//===--- P1689ModuleDependencyCollector.cpp - Generate dependency file ----===//
//
// Part of the LLVM Project, under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
//
//===----------------------------------------------------------------------===//
//
// This code generates dependency files.
//
//===----------------------------------------------------------------------===//

#include "clang/Frontend/Utils.h"
#include "clang/Lex/PPCallbacks.h"
#include "clang/Lex/Preprocessor.h"
#include "clang/Serialization/ASTReader.h"
#include "llvm/Support/raw_ostream.h"

#include <iostream>
#include <sstream>

using namespace clang;

namespace {
/// Private implementations for P1689ModuleDependencyCollector
class P1689ModuleDependencyListener : public ASTReaderListener {
  P1689ModuleDependencyCollector &Collector;
public:
  P1689ModuleDependencyListener(P1689ModuleDependencyCollector &Collector)
      : Collector(Collector) {}
  bool needsInputFileVisitation() override { return true; }
  bool needsSystemInputFileVisitation() override { return true; }
  bool visitInputFile(StringRef Filename, bool IsSystem, bool IsOverridden,
                      bool IsExplicitModule) override {
    std::cerr << "Visiting '" << Filename.str() << "'";
    std::cerr << "  IsSystem: " << IsSystem;
    std::cerr << "  IsOverridden: " << IsOverridden;
    std::cerr << "  IsExplicitModule: " << IsExplicitModule;
    std::cerr << std::endl;
    return true;
  }
};

struct P1689ModuleDependencyPPCallbacks : public PPCallbacks {
  P1689ModuleDependencyCollector &Collector;
  Preprocessor &PP;
  P1689ModuleDependencyPPCallbacks(P1689ModuleDependencyCollector &Collector,
                              Preprocessor &PP)
      : Collector(Collector), PP(PP) {}

  void InclusionDirective(SourceLocation HashLoc, const Token &IncludeTok,
                          StringRef FileName, bool IsAngled,
                          CharSourceRange FilenameRange, const FileEntry *File,
                          StringRef SearchPath, StringRef RelativePath,
                          const Module *Imported,
                          SrcMgr::CharacteristicKind FileType) override {
    if (!File)
      return;
    std::cerr << "Including '" << FileName.str() << "'";
    std::cerr << "  IsAngled: " << IsAngled;
    std::cerr << "  SearchPath: " << SearchPath.str();
    std::cerr << "  RelativePath: " << RelativePath.str();
    std::cerr << "  Module: " << Imported;
    std::cerr << std::endl;
  }
  void moduleImport(SourceLocation ImportLoc,
                            ModuleIdPath Path,
                            const Module *Imported) override {
    std::stringstream FullName;
    bool started = false;
    for (auto PathPart : Path) {
      if (started)
        FullName << '.';
      started = true;
      FullName << PathPart.first->getName().str();
    }
    P1689ModuleDependencyCollector::ModuleInfo RequiredModule;
    RequiredModule.Name = FullName.str();
    RequiredModule.Type = P1689ModuleDependencyCollector::ModuleInfo::ModuleType::Named;
    Collector.Requires.push_back(RequiredModule);
  }
  void EndOfMainFile() override {
    auto &Map = PP.getHeaderSearchInfo().getModuleMap();
    for (auto Entry = Map.module_begin(); Entry != Map.module_end(); ++Entry) {
      auto* M = Entry->second;
      P1689ModuleDependencyCollector::ModuleInfo ProvidedModule;
      ProvidedModule.Name = M->Name;
      ProvidedModule.Type = P1689ModuleDependencyCollector::ModuleInfo::ModuleType::Named;
      Collector.Provides.push_back(ProvidedModule);
    }

    Collector.writeFile();
  }
};

struct P1689ModuleDependencyMMCallbacks : public ModuleMapCallbacks {
  P1689ModuleDependencyCollector &Collector;
  P1689ModuleDependencyMMCallbacks(P1689ModuleDependencyCollector &Collector)
      : Collector(Collector) {}

  void moduleMapFileRead(SourceLocation FileStart,
                                 const FileEntry &File, bool IsSystem) override {
    std::cerr << "ModuleMap file read";
    std::cerr << std::endl;
  }
  void moduleMapAddHeader(StringRef HeaderPath) override {
    std::cerr << "ModuleMap header '" << HeaderPath.str() << "'";
    std::cerr << std::endl;
  }
  void moduleMapAddUmbrellaHeader(FileManager *FileMgr,
                                  const FileEntry *Header) override {
    std::cerr << "ModuleMap unbrella header";
    std::cerr << std::endl;
  }
};
} // end anonymous namespace

P1689ModuleDependencyCollector::P1689ModuleDependencyCollector(const DependencyOutputOptions &Opts)
  : ModuleDepFormat(Opts.ModuleDepFormat),
  ModuleDepFile(Opts.ModuleDepFile), ModuleDepOutput(Opts.ModuleDepOutput) {}

void P1689ModuleDependencyCollector::attachToPreprocessor(Preprocessor &PP) {
  PP.addPPCallbacks(std::make_unique<P1689ModuleDependencyPPCallbacks>(
      *this, PP));
  PP.getHeaderSearchInfo().getModuleMap().addModuleMapCallbacks(
      std::make_unique<P1689ModuleDependencyMMCallbacks>(*this));
}

void P1689ModuleDependencyCollector::attachToASTReader(ASTReader &R) {
  R.addListener(std::make_unique<P1689ModuleDependencyListener>(*this));

  DependencyCollector::attachToASTReader(R);
}

void P1689ModuleDependencyCollector::writeFile() {
  std::error_code EC;
  llvm::raw_fd_ostream OS(ModuleDepFile, EC, llvm::sys::fs::OF_Text);
  if (EC) {
    /* Diags.Report(diag::err_fe_error_opening) << ModuleDepFile << EC.message(); */
    return;
  }

  OS << "{\n";
  OS << "  \"version\": 1,\n";
  OS << "  \"revision\": 0,\n";
  OS << "  \"rules\": [\n";
  OS << "    {\n";
  OS << "      \"primary-output\": \"" << ModuleDepOutput << "\"";

  if (!Provides.empty()) {
    OS << ",\n      \"provides\": [";
    bool first = true;
    for (auto const& Provide : Provides) {
      if (!first)
        OS << ',';
      first = false;
      OS << "\n        {";

      OS << "\n          \"logical-name\": \"" << Provide.Name << "\"";
      if (!Provide.SourcePath.empty()) {
        OS << ",\n          \"unique-on-source-path\": true"
              ",\n          \"source-path\": \"" << Provide.SourcePath << "\"";
      }
      OS << "\n        }";
    }
    OS << "\n      ]";
  }

  if (!Requires.empty()) {
    OS << ",\n      \"requires\": [";
    bool first = true;
    for (auto const& Require : Requires) {
      if (!first)
        OS << ',';
      first = false;
      OS << "\n        {";

      OS << "\n          \"logical-name\": \"" << Require.Name << "\"";
      if (!Require.SourcePath.empty()) {
        OS << ",\n          \"unique-on-source-path\": true"
              ",\n          \"source-path\": \"" << Require.SourcePath << "\"";
      }
      const char* TypeStr = nullptr;
      switch (Require.Type) {
        case ModuleInfo::ModuleType::AngleHeader:
          TypeStr = "include-angle";
          break;
        case ModuleInfo::ModuleType::QuoteHeader:
          TypeStr = "include-quote";
          break;
        case ModuleInfo::ModuleType::Named:
          break;
      }
      if (TypeStr)
        OS << ",\n        \"lookup-method\": \"" << TypeStr << "\"";

      OS << "\n        }";
    }
    OS << "\n      ]";
  }

  OS << "\n    }\n";
  OS << "  ]\n";
  OS << "}\n";
}
