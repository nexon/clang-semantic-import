// Clang Includes
#include <clang/AST/ASTConsumer.h>
#include <clang/AST/ASTContext.h>
#include <clang/AST/Expr.h>
#include <clang/ASTMatchers/ASTMatchFinder.h>
#include <clang/ASTMatchers/ASTMatchers.h>
#include <clang/Basic/Diagnostic.h>
#include <clang/Basic/SourceLocation.h>
#include <clang/Basic/SourceManager.h>
#include <clang/Frontend/CompilerInstance.h>
#include <clang/Frontend/FrontendAction.h>
#include <clang/Frontend/FrontendActions.h>
#include <clang/Lex/PPCallbacks.h>
#include <clang/Lex/Preprocessor.h>
#include <clang/Rewrite/Core/Rewriter.h>
#include <clang/Rewrite/Frontend/FixItRewriter.h>
#include <clang/Tooling/CommonOptionsParser.h>
#include <clang/Tooling/Tooling.h>

// LLVM Includes
#include <llvm/ADT/ArrayRef.h>
#include <llvm/ADT/StringRef.h>
#include <llvm/Support/CommandLine.h>
#include <llvm/Support/raw_ostream.h>

// Standard Includes
#include <memory>
#include <string>
#include <type_traits>
#include <iostream>

namespace mmi {
    class Import {
	public:
				
        Import(const std::string& Name, clang::SourceLocation& HashLocation, bool IsAngled)
        : Name(Name)
        , HashLocation(HashLocation)
        , IsAngled(IsAngled) {}
		
        std::string to_string() const {
            if (IsAngled) {
                return (llvm::Twine("#import") + " <" + Name + ">").str();
            } else {
                return (llvm::Twine("#import") + " \"" + Name + "\"").str();
            }
        }

        /// The name of the included file.
        std::string Name;

        /// The location of the import/include
        clang::SourceLocation HashLocation;

        /// Wether the file was included with angle brackets.
        bool IsAngled;
    };

    class SemanticImport {
	public:
		
        SemanticImport(const std::string& Name, clang::SourceLocation& HashLocation)
        : Name(Name)
        , HashLocation(HashLocation) {}
	
        std::string to_string() const {
            return token() + " " + Name + ";";
        }
        
        std::string token() const {
            return "@import";
        }

        /// The name of the included file.
        std::string Name;

        /// The location of the import/include
        clang::SourceLocation HashLocation;
    };

    /// Captures #include directives and sorts them after every block.
    ///
    /// The algorithm proceeds by collecting all included files into a vector and
    /// whenever the distance between two includes is more than one line, the files
    /// picked up until then are sorted and the source code rewritten.
    class SemanticImportPreprocessorCallback : public clang::PPCallbacks {
    public:
        /// Constructor.
        ///
        /// \param Rewriter The object to rewrite the source code
        /// \param DE The Diagnostics Engine in charge of emits warnings/error
        explicit SemanticImportPreprocessorCallback(clang::Rewriter& Rewriter,
                                                    bool OrderOption,
                                                    bool MoveImportOrder,
                                                    const std::vector<std::string>& RemoveImport,
                                                    clang::DiagnosticsEngine& DE)
        : SourceManager(Rewriter.getSourceMgr())
        , Rewriter(Rewriter)
        , OrderOption(OrderOption)
        , MoveImportOrder(MoveImportOrder)
        , RemoveImport(RemoveImport)
        , DiagnosticEngine(DE) {}

        /// Callback invoked whenever an inclusion directive of any kind (#include, #import, etc.) has been processed, regardless of whether the inclusion will actually result in an inclusion.
        ///
        /// \param HashLoc The location of the '#' that starts the inclusion directive.
        /// \param IncludeTok  The token that indicates the kind of inclusion directive, e.g., 'include' or 'import'.
        /// \param FileName The name of the file being included, as written in the source code.
        /// \param IsAngled Whether the file name was enclosed in angle brackets; otherwise, it was enclosed in quotes.
        /// \param FilenameRange The character range of the quotes or angle brackets for the written file name.
        /// \param File The actual file that may be included by this inclusion directive.
        /// \param SearchPath Contains the search path which was used to find the file in the file system. If the file was found via an absolute include path, SearchPath will be empty. For framework includes, the SearchPath and RelativePath will be split up. For example, if an include of "Some/Some.h" is found via the framework path "path/to/Frameworks/Some.framework/Headers/Some.h", SearchPath will be "path/to/Frameworks/Some.framework/Headers" and RelativePath will be "Some.h".
        /// \param RelativePath The path relative to SearchPath, at which the include file was found. This is equal to FileName except for framework includes.
        /// \param Imported The module, whenever an inclusion directive was automatically turned into a module import or null otherwise.
        /// \param FileType   The characteristic kind, indicates whether a file or directory holds normal user code, system code, or system code which is implicitly 'extern "C"' in C++ mode.
        void InclusionDirective(clang::SourceLocation HashLoc,
                                [[maybe_unused]] const clang::Token&  IncludeTok,
                                llvm::StringRef FileName,
                                bool  IsAngled,
                                clang::CharSourceRange FilenameRange,
                                [[maybe_unused]]  const clang::FileEntry *File,
                                [[maybe_unused]]  llvm::StringRef SearchPath,
                                [[maybe_unused]]  llvm::StringRef RelativePath,
                                const clang::Module *Imported,
                                [[maybe_unused]]  clang::SrcMgr::CharacteristicKind FileType) override {
            if (!SourceManager.isInMainFile(HashLoc)) return;

            if (Imports.empty() && SemanticImports.empty()) {
                FirstLocation = HashLoc;
            }
            
            Import lastInclude = { FileName, HashLoc, IsAngled };
            
            if (!SemanticImports.empty()) {
                SemanticImport& firstModule = SemanticImports.front();

                // we get the line numbers
                const unsigned SemanticImportLineNumber = getLineNumber(firstModule.HashLocation);
                const unsigned CurrentImportLineNumber = getLineNumber(HashLoc);
                
                if (CurrentImportLineNumber > SemanticImportLineNumber) {
                    ShouldCorrectImports = true;
                    
                    const unsigned ID = DiagnosticEngine.getCustomDiagID(clang::DiagnosticsEngine::Warning, "Move all #include/#import directives at the very top of the file, before any code. ('%0')");
                    llvm::StringRef messageValue(std::move(lastInclude.to_string()));
                    DiagnosticEngine.Report(lastInclude.HashLocation, ID).AddString(std::move(messageValue));
                }

            }
        
            LastLocation = FilenameRange.getEnd();
            
            if (Imported != nullptr) {
                std::string moduleName;
                Imports.emplace_back(std::move(lastInclude));
            } else {
                Imports.emplace_back(std::move(lastInclude));
            }
        }

        /// Callback invoked whenever there was an explicit module-import syntax.
        ///
        /// \param ImportLoc The location of import directive token.
        /// \param Path  The identifiers (and their locations) of the module "path", e.g., "std.vector" would be split into "std" and "vector".
        /// \param Imported  The imported module; can be null if importing failed.
        void moduleImport(clang::SourceLocation ImportLoc, clang::ModuleIdPath Path, [[maybe_unused]] const clang::Module *Imported) override {
            if (!SourceManager.isInMainFile(ImportLoc)) return;
            
            if (SemanticImports.empty() && Imports.empty()) {
                FirstLocation = ImportLoc;
            }
            
            std::string moduleName;

			// add 2 spaces to compensate for the @ and ;
            std::string token("import  "); 
            
            for (const auto& pair : Path) {
                auto Info = pair.first;
                
                moduleName = Info->getName().str();
                LastLocation = ImportLoc.getLocWithOffset(token.length() + moduleName.length());
            }
            
            if (!RemoveImport.empty() && (std::find(RemoveImport.begin(), RemoveImport.end(), moduleName) != end(RemoveImport))) {
                clang::Rewriter::RewriteOptions Options;
                Options.RemoveLineIfEmpty = true;
                Rewriter.RemoveText({ImportLoc.getLocWithOffset(-1), LastLocation}, Options);
            } else {
                SemanticImport newElement = {moduleName, ImportLoc};
                SemanticImports.emplace_back(std::move(newElement));
            }
        }

        /// Callback invoked when the end of the main file is reached. 
        void EndOfMainFile() override {
            if (ShouldCorrectImports && FirstLocation.isValid() && LastLocation.isValid()) {
                if (OrderOption) {
                    SortImports();
                }
                
                if (MoveImportOrder) {
                    MoveImports();
                }

            } else if (FirstLocation.isInvalid() || LastLocation.isInvalid()) {
                llvm::outs() << "File Invalid" << "\n";
            }
        }

        /// Callback invoked whenever an inclusion directive results in a file-not-found error.
        ///
        /// \param FileName The name of the file being included, as written in the source code.
        /// \param RecoveryPAth  If this client indicates that it can recover from this missing file, the client should set this as an additional header search patch.
        bool FileNotFound(llvm::StringRef FileName, [[maybe_unused]] llvm::SmallVectorImpl<char>& RecoveryPath) override {
            llvm::outs() << "File not Found: " << FileName << "\n";
            return false;
        }
        
    private:
        unsigned FirstLocationOffset() const {
            auto LineNumber = SourceManager.getDecomposedLoc(FirstLocation).second;
            auto Offset = 0;
            if (LineNumber > 1) {
                Offset = -1;
            }
            
            return Offset;
        }
        
        /// We sort the includes 
        void SortImports() {
            std::sort(Imports.begin(), Imports.end(), [](Import& a, Import& b) {
                return a.Name < b.Name;
            });
            
            std::sort(SemanticImports.begin(), SemanticImports.end(), [](SemanticImport& a, SemanticImport& b) {
                return a.Name < b.Name;
            });
        }
        
        /// We Move the semantic imports (@import) below of the normal imports (#import)
        /// And then we replace the text
        void MoveImports() {
            std::string CorrectedImports;
            
            for (const auto& Import : Imports) {
                CorrectedImports += Import.to_string() + "\n";
            }
            
            CorrectedImports += "\n";
            
            for (const auto& SemanticImport : SemanticImports) {
                CorrectedImports += SemanticImport.to_string() + "\n";
            }
            
            CorrectedImports.pop_back(); // Remove last string.
            
            clang::SourceRange Range = {FirstLocation.getLocWithOffset(FirstLocationOffset()), LastLocation};
            Rewriter.RemoveText(Range);
            Rewriter.ReplaceText(Range, CorrectedImports);
        }
        
        /// Given the hashLocation, get the LineNumber
        int getLineNumber(clang::SourceLocation& HashLocation) const {
            auto [FileID, Offset] = SourceManager.getDecomposedLoc(HashLocation);
            
            return SourceManager.getLineNumber(FileID, Offset);
        }
        
        /// The Includes for the given files
        llvm::SmallVector<Import, 16> Imports;

        /// All the semantic imports for the files.
        llvm::SmallVector<SemanticImport, 16> SemanticImports;

        /// The first location of the current block.
        clang::SourceLocation FirstLocation;

        /// The last location of the current block.
        clang::SourceLocation LastLocation;

        /// The `SourceManager` to rewrite text.
        const clang::SourceManager& SourceManager;

        /// The `Rewriter` to rewrite text.
        clang::Rewriter& Rewriter;

        /// If there is any import to correct, we set it to true
        bool ShouldCorrectImports;
        
        /// If we want to order the Includes and semantic imports
        bool OrderOption;
        
        /// Set to true if we want to change the order of the #imports to be above @imports
        bool MoveImportOrder;
        
        /// A list of all the imports that we need to remove.
        const std::vector<std::string> RemoveImport;
        
        /// Emits Diagnostic (warnings) about the misras
        clang::DiagnosticsEngine& DiagnosticEngine;
    };

    /// The action that registers the preprocessor callbacks.
    ///
    /// Note that we can skip the consumer in this case.
    class PreprocessorAction : public clang::PreprocessOnlyAction {
    public:
        /// Constructor.
        explicit PreprocessorAction(bool RewriteOption, bool OrderOption, bool MoveImportOrder, const std::vector<std::string>& RemoveImport)
        : RewriteOption(RewriteOption)
        , OrderOption(OrderOption)
        , MoveImportOrder(MoveImportOrder)
        , RemoveImport(RemoveImport) {}

        /// Called before any file is even touched. Allows us to register a rewriter.
        bool BeginInvocation(clang::CompilerInstance& Compiler) override {
            Rewriter.setSourceMgr(Compiler.getSourceManager(), Compiler.getLangOpts());
            return true;
        }

        /// Adds our preprocessor callback to the compiler instance.
        bool BeginSourceFileAction(clang::CompilerInstance& Compiler) override {
            auto hooks = llvm::make_unique<SemanticImportPreprocessorCallback>(Rewriter, OrderOption, MoveImportOrder, RemoveImport, Compiler.getDiagnostics());
            Compiler.getPreprocessor().addPPCallbacks(std::move(hooks));
            return true;
        }

        /// Writes the rewritten source code back out to disk.
        void EndSourceFileAction() override {
            if (RewriteOption) {
                const auto FileID = Rewriter.getSourceMgr().getMainFileID();
                const auto fullPath = Rewriter.getSourceMgr().getFileEntryForID(FileID)->getName().str();

                std::error_code error_code;
                llvm::raw_fd_ostream outFile(fullPath, error_code, llvm::sys::fs::F_None);
                Rewriter.getEditBuffer(FileID).write(outFile);
                outFile.close();
            }
        }

    private:
        /// The rewriter to rewrite source code. Forwarded to the callback.
        clang::Rewriter Rewriter;

        /// Tells us if we want to override the code or if we only want to print stuff.
        bool RewriteOption;
        
        /// Tells uf if we want to order the includes and semantic imports alphabetically (in its own blocks)
        bool OrderOption;
        
        /// Tells uf if we want to order the includes and semantic imports alphabetically (in its own blocks)
        bool MoveImportOrder;
        
        /// Tells uf if we want to order the includes and semantic imports alphabetically (in its own blocks)
        const std::vector<std::string> RemoveImport;
    };
}

/// Namespace to contain (and for ordering only) the options that we can pass to the tool
namespace  {
    llvm::cl::OptionCategory SemanticImportToolCategory("Semantic Import Tools");
    llvm::cl::extrahelp SemanticImportToolCategoryHelp("Move all #include/#import directives at the very top of the file, before any code");

    /// Option for Rewrite
    llvm::cl::opt<bool> RewriteOption("rewrite", llvm::cl::desc("Rewrite the source code so the #/@import are in the right order."), llvm::cl::cat(SemanticImportToolCategory));
    llvm::cl::alias RewriteShortOption("r", llvm::cl::desc("Alias for -rewrite option"), llvm::cl::aliasopt(RewriteOption));
    
    /// Option for ordering by Alph Order
    llvm::cl::opt<bool> OrderOption("order", llvm::cl::desc("Order Imports & Semantic Imports by Alph Order"), llvm::cl::cat(SemanticImportToolCategory));
    llvm::cl::alias OrderShortOption("o", llvm::cl::desc("Alias for -order option"), llvm::cl::aliasopt(OrderOption));
    
    /// Option for removing imports
    llvm::cl::list<std::string> RemoveOption("remove-import", llvm::cl::desc("Remove the given imports"), llvm::cl::cat(SemanticImportToolCategory));
    llvm::cl::alias RemoveShortOption("ri", llvm::cl::desc("Alias for -remove-import option"), llvm::cl::aliasopt(RemoveOption));
    
    /// Move the #import order on top of any @import
    llvm::cl::opt<bool> MoveImportOrderOption("move-import-order", llvm::cl::desc("Move the #import order on top of any @import"), llvm::cl::cat(SemanticImportToolCategory));
    llvm::cl::alias MoveImportOrderShortOption("mio", llvm::cl::desc("Alias for -move-import-order"), llvm::cl::aliasopt(MoveImportOrderOption));
}

/// A custom `FrontendActionFactory` so that we can pass the options
/// to the constructor of the tool.
class SemanticModuleToolFactory : public clang::tooling::FrontendActionFactory {
  clang::FrontendAction* create() override {
    return new mmi::PreprocessorAction(RewriteOption, OrderOption, MoveImportOrderOption, RemoveOption);
  }
};

auto main(int argc, const char* argv[]) -> int {
    using namespace clang::tooling;

    CommonOptionsParser OptionsParser(argc, argv, SemanticImportToolCategory);
    ClangTool Tool(OptionsParser.getCompilations(),
                   OptionsParser.getSourcePathList());

    return Tool.run(new SemanticModuleToolFactory());
}

