#include <cstring>
#include <cstdio>
#include <string>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <set>
#include <chrono>

#include "../include/cli.hpp"
#include <crossguid/guid.hpp>
#include <tclap/CmdLine.h>

int main(int argc, char** argv) {
  try {
    auto defaultOutDir = "alta-build";

    TCLAP::CmdLine cmd("altac, a C transpiler for the Alta language", ' ', "0.3.1");

    TCLAP::SwitchArg compileSwitch("c", "compile", "Whether to compile the generated C code with CMake after transpilation");
    TCLAP::ValueArg<std::string> generatorArg("g", "cmake-generator", "The generator to use with CMake when compiling code. Only has meaning when `-c` is specificed", false, "", "cmake-generator");
    TCLAP::SwitchArg verboseSwitch("v", "verbose", "Whether to output extra information");
    TCLAP::ValueArg<std::string> outdirPath("o", "out-dir", "The directory in which to put the generated C files. Will be created if it doesn't exist", false, defaultOutDir, "folder");
    TCLAP::UnlabeledValueArg<std::string> filenameString("module-or-package-path", "A path to a file (for modules) or folder (for packages)", true, "", "file-or-folder");
    TCLAP::SwitchArg benchmarkSwitch("", "benchmark", "Whether to time each section of code processing and report the result");

    cmd.add(generatorArg);
    cmd.add(compileSwitch);
    cmd.add(verboseSwitch);
    cmd.add(benchmarkSwitch);
    cmd.add(outdirPath);
    cmd.add(filenameString);
    cmd.parse(argc, argv);

    bool isVerbose = verboseSwitch.getValue();
    bool doTime = benchmarkSwitch.getValue() || isVerbose;

    auto programPath = AltaCore::Filesystem::Path(argv[0]).absolutify();

    // set our standard library path
#ifndef NDEBUG
  // we're in a debug build
    auto debugStdlibPath = AltaCore::Filesystem::Path(ALTA_DEBUG_STDLIB_PATH);
    if (debugStdlibPath.exists()) {
      AltaCore::Modules::standardLibraryPath = debugStdlibPath;
    } else {
      // fallback to relative, platform-dependent stdlib directory
#if defined(_WIN32) || defined(_WIN64)
      AltaCore::Modules::standardLibraryPath = programPath.dirname() / "stdlib";
#else
      AltaCore::Modules::standardLibraryPath = programPath.dirname().dirname() / "lib" / "alta-stdlib";
#endif
    }
#else
  // we're in a release build
#if defined(_WIN32) || defined(_WIN64)
    AltaCore::Modules::standardLibraryPath = programPath.dirname() / "stdlib";
#else
    AltaCore::Modules::standardLibraryPath = programPath.dirname().dirname() / "lib" / "alta-stdlib";
#endif
#endif

    // set our runtime path
    AltaCore::Filesystem::Path runtimePath;
#ifndef NDEBUG
    auto debugRuntimePath = AltaCore::Filesystem::Path(ALTA_DEBUG_RUNTIME_PATH);
    if (debugRuntimePath.exists()) {
      runtimePath = debugRuntimePath;
    } else {
#if defined(_WIN32) || defined(_WIN64)
      runtimePath = programPath.dirname() / "runtime";
#else
      runtimePath = programPath.dirname().dirname() / "lib" / "alta-runtime";
#endif
    }
#else
    // we're in a release build
#if defined(_WIN32) || defined(_WIN64)
    runtimePath = programPath.dirname() / "runtime";
#else
    runtimePath = programPath.dirname().dirname() / "lib" / "alta-runtime";
#endif
#endif

    bool givenPackage = false;
    std::string filename = filenameString.getValue();
    auto fn = AltaCore::Filesystem::Path(filename).absolutify();
    auto origFn = fn;
    auto outDir = AltaCore::Filesystem::Path(outdirPath.getValue()).absolutify();
    auto origOutDir = outDir;
    std::vector<AltaCore::Modules::TargetInfo> targets;

    if (!fn.exists()) {
      std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to find the given file/folder" << std::endl;
      return 4;
    }

    if (fn.isDirectory()) {
      givenPackage = true;
      AltaCore::Filesystem::Path mainPackageModulePath;
      try {
        auto infoPath = fn / "package.alta.yaml";
        if (!infoPath.exists()) {
          std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": given path (\"" << fn.toString() << "\") points to a folder but does not contain a `package.alta.yaml`" << std::endl;
          try {
            auto nearestInfo = AltaCore::Modules::findInfo(fn);
            std::cout << CLI::COLOR_BLUE << "Info" << CLI::COLOR_NORMAL << ": nearest package found is located at \"" << nearestInfo.toString() << '"' << std::endl;
          } catch (AltaCore::Modules::PackageInformationNotFoundError&) {
            // ...
          }
          return 7;
        }
        auto modInfo = AltaCore::Modules::getInfo(infoPath, false);
        if (modInfo.main) {
          mainPackageModulePath = modInfo.main;
        } else {
          mainPackageModulePath = fn / "main.alta";
          std::cout << CLI::COLOR_YELLOW << "Warning" << CLI::COLOR_NORMAL << ": failed to find a main module for \"" << fn.toString() << "\". Using default main of \"" << mainPackageModulePath.toString() << '"' << std::endl;
        }
        targets = modInfo.targets;
      } catch (AltaCore::Modules::ModuleError&) {
        mainPackageModulePath = fn / "main.alta";
        AltaCore::Modules::TargetInfo targetInfo;
        targetInfo.main = fn;
        targetInfo.name = fn.filename();
        targets.push_back(targetInfo);
        std::cout << CLI::COLOR_YELLOW << "Warning" << CLI::COLOR_NORMAL << ": failed to parse package information for \"" << fn.toString() << "\". Using default main of \"" << mainPackageModulePath.toString() << '"' << std::endl;
      }
      if (!mainPackageModulePath.exists() && targets.size() < 1) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": no main module and no targets found for the given package" << std::endl;
        return 5;
      }
      fn = mainPackageModulePath;
    } else {
      try {
        auto modInfo = AltaCore::Modules::getInfo(fn);
        bool found = false;
        for (auto& target: modInfo.targets) {
          if (target.main == fn) {
            targets.push_back(target);
            found = true;
            break;
          }
        }
        if (!found) {
          AltaCore::Modules::TargetInfo targetInfo;
          targetInfo.main = fn;
          targetInfo.name = fn.filename();
          targets.push_back(targetInfo);
        }
      } catch (AltaCore::Modules::PackageInformationNotFoundError&) {
        AltaCore::Modules::TargetInfo targetInfo;
        targetInfo.main = fn;
        targetInfo.name = fn.filename();
        targets.push_back(targetInfo);
      }
    }

    AltaCore::registerGlobalAttributes();

    AltaCore::Filesystem::mkdirp(outDir);

    auto outdirRuntime = outDir / "_runtime";
    AltaCore::Filesystem::copyDirectory(runtimePath, outdirRuntime);

    std::ofstream runtimeCmakeLists((outDir / "_runtime" / "CMakeLists.txt").absolutify().toString());
    runtimeCmakeLists << "cmake_minimum_required(VERSION 3.10)\n";
    runtimeCmakeLists << "project(alta-global-runtime)\n";
    runtimeCmakeLists << "if (NOT TARGET alta-global-runtime)\n";
    runtimeCmakeLists << "  add_library(alta-global-runtime\n";
    auto runtimeFiles = AltaCore::Filesystem::getDirectoryListing(outdirRuntime, true);
    for (auto& runtimeFile: runtimeFiles) {
      if (runtimeFile.extname() == "c") {
        runtimeCmakeLists << "    \"${PROJECT_SOURCE_DIR}/" << runtimeFile.relativeTo(outdirRuntime).toString('/') << "\"\n";
      }
    }
    runtimeCmakeLists << "  )\n";
    runtimeCmakeLists << "endif()\n";
    runtimeCmakeLists.close();

    std::ofstream rootCmakeLists((outDir / "CMakeLists.txt").toString());

    rootCmakeLists << "cmake_minimum_required(VERSION 3.10)\n";
    rootCmakeLists << "if (WIN32)\n";
    rootCmakeLists << "  set(ALTA_EXECUTABLE_EXTENSION \".exe\")\n";
    rootCmakeLists << "  set(ALTA_DYLIB_EXTENSION \".dll\")\n";
    rootCmakeLists << "  set(ALTA_STATLIB_EXTENSION \".lib\")\n";
    rootCmakeLists << "else()\n";
    rootCmakeLists << "  set(ALTA_EXECUTABLE_EXTENSION \"\")\n";
    rootCmakeLists << "  set(ALTA_STATLIB_EXTENSION \".a\")\n";
    rootCmakeLists << "  if (APPLE)\n";
    rootCmakeLists << "    set(ALTA_DYLIB_EXTENSION \".dylib\")\n";
    rootCmakeLists << "  else()\n";
    rootCmakeLists << "    set(ALTA_DYLIB_EXTENSION \".so\")\n";
    rootCmakeLists << "  endif()\n";
    rootCmakeLists << "endif()\n";
    rootCmakeLists << "project(ALTA_TOPLEVEL_PROJECT-" << fn.dirname().basename() << ")\n";

    for (auto& target: targets) {
      fn = target.main;
      outDir = origOutDir / target.name;

      if (!fn.exists()) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": main module (\"" << fn.toString() << "\") for target \"" << target.name << "\" not found" << std::endl;
        return 8;
      }

      rootCmakeLists << "set(ALTA_CURRENT_PROJECT_BINARY_DIR \"${CMAKE_BINARY_DIR}/" << target.name << "\")\n";
      rootCmakeLists << "add_subdirectory(\"${PROJECT_SOURCE_DIR}/" << target.name << "\")\n";

      std::ifstream file(fn.toString());
      std::string line;
      ALTACORE_MAP<std::string, std::string> originalSources;
      ALTACORE_MAP<std::string, AltaCore::Preprocessor::Expression> defs;
      ALTACORE_MAP<std::string, std::string> results;
      ALTACORE_MAP<std::string, std::vector<AltaCore::Preprocessor::Location>> locationMaps;
      AltaCore::Preprocessor::Preprocessor prepo(fn, defs, results, locationMaps, AltaCore::Preprocessor::defaultFileResolver,
        [&](AltaCore::Preprocessor::Preprocessor& orig, AltaCore::Preprocessor::Preprocessor& newPre, AltaCore::Filesystem::Path path) {
          std::ifstream file(path.toString());
          std::string line;

          if (!file.is_open()) {
            throw std::runtime_error("Couldn't open the input file!");
          }

          while (std::getline(file, line)) {
            if (file.peek() != EOF) {
              line += "\n";
            }
            originalSources[path.toString()] += line;
            newPre.feed(line);
          }

          file.close();
          newPre.done();
        }
      );

      Talta::registerAttributes(fn);

      if (!file.is_open()) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to open the input file (\"" << fn.toString() << '"' << std::endl;
        return 2;
      }

      while (std::getline(file, line)) {
        if (file.peek() != EOF) {
          line += "\n";
        }
        originalSources[fn.toString()] += line;
        prepo.feed(line);
      }

      file.close();
      prepo.done();
      
      if (doTime) {
        for (auto& [path, timer]: AltaCore::Timing::preprocessTimes) {
          auto duration = AltaCore::Timing::toMilliseconds(timer.total());
          std::cout << CLI::COLOR_BLUE << "Info" << ": preprocessed \"" << path.toString() << "\" in " << duration.count() << "ms" << std::endl;
        }
      }

      auto locationMapper = [&](AltaCore::Filesystem::Path& filePath) {
        return [&](size_t prepoLine, size_t prepoColumn) -> std::pair<size_t, size_t> {
          using AltaCore::Preprocessor::Location;

          auto& locations = locationMaps[fn.absolutify().toString()];
          if (locations.size() == 0) {
            return std::make_pair(prepoLine, prepoColumn);
          }
          
          auto prevLocation = Location(0, 0, 0, 0, 1);
          auto defaultLocation = Location(1, 1, 1, 1, 0);
          auto* prevLast = &prevLocation;
          auto* last = &defaultLocation;

          bool even = true;
          for (auto& location: locations) {
            even = !even;
            if (
              location.newLine > prepoLine ||
              (
                location.newLine == prepoLine &&
                location.newColumn > prepoColumn
              )
            ) {
              if (even) {
                prevLast = last;
                last = &location;
              }
              break;
            }
            prevLast = last;
            last = &location;
          }

          if (
            prevLast->newLine <= prepoLine && last->newLine >= prepoLine &&
            prevLast->newColumn <= prepoColumn && last->newColumn >= prepoColumn &&
            prevLast != &prevLocation &&
            last != &defaultLocation
          ) {
            return std::make_pair(prevLast->newLine, prevLast->newColumn);
          } else {
            if (last->newLine == prepoLine) {
              return std::make_pair(last->originalLine, prepoColumn - last->newColumn + last->originalColumn);
            } else {
              return std::make_pair(prepoLine - last->newLine + last->originalLine, prepoColumn);
            }
          }
        };
      };

      AltaCore::Lexer::Lexer lexer(fn, locationMapper(fn));
      lexer.feed(results[fn.toString()]);

      if (doTime) {
        for (auto& [path, timer]: AltaCore::Timing::lexTimes) {
          auto duration = AltaCore::Timing::toMilliseconds(timer.total());
          std::cout << CLI::COLOR_BLUE << "Info" << ": lexed \"" << path.toString() << "\" in " << duration.count() << "ms" << std::endl;
        }
      }

      if (verboseSwitch.getValue()) {
        printf("%sTokens:%s\n", CLI::COLOR_BLUE, CLI::COLOR_NORMAL);
        for (const auto& token: lexer.tokens) {
          printf(
            "%s token (with type %d) found at %zu:%zu with content \"%s\"\n",
            AltaCore::Lexer::TokenType_names[(int)token.type],
            (int)token.type,
            token.line,
            token.column,
            token.raw.c_str()
          );
        }

        if (lexer.absences.size() > 0) printf("\n");
      }

      for (const auto& absence: lexer.absences) {
        auto[line, column] = absence;
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to lex input at " << line << ":" << column << std::endl;
      }

      if (lexer.absences.size() > 0) {
        return 3;
      }

      auto logError = [&](AltaCore::Errors::Error& e) {
        std::cerr << "Error at " << e.position.file.toString() << ":" << e.position.line << ":" << e.position.column << std::endl;
        if (originalSources.find(e.position.file.toString()) != originalSources.end()) {
          auto& source = originalSources[e.position.file.toString()];
          size_t firstNewline = -1;
          for (size_t i = 1; i < e.position.line; i++) {
            firstNewline = source.find('\n', firstNewline + 1);
          }
          auto secondNewline = source.find('\n', firstNewline + 1);
          auto line = (secondNewline == -1) ? source.substr(firstNewline + 1) : source.substr(firstNewline + 1, secondNewline - firstNewline - 1);
          std::cerr << line << std::endl;
          for (size_t i = 1; i < e.position.column; i++) {
            std::cerr << " ";
          }
          std::cerr << "^" << std::endl;
          for (size_t i = 1; i < e.position.column; i++) {
            std::cerr << " ";
          }
          std::cerr << e.what() << std::endl;
        }
      };

      ALTACORE_MAP<std::string, std::shared_ptr<AltaCore::AST::RootNode>> importCache;

      AltaCore::Modules::parseModule = [&](std::string importRequest, AltaCore::Filesystem::Path requestingPath) -> std::shared_ptr<AltaCore::AST::RootNode> {
        auto path = AltaCore::Modules::resolve(importRequest, requestingPath);
        if (auto& imp = importCache[path.absolutify().toString()]) {
          return imp;
        }
        if (results.find(path.absolutify().toString()) == results.end()) {
          throw std::runtime_error("its not here.");
        }

        Talta::registerAttributes(path);

        AltaCore::Lexer::Lexer otherLexer(path, locationMapper(path));

        otherLexer.feed(results[path.absolutify().toString()]);

        AltaCore::Parser::Parser otherParser(otherLexer.tokens, path);
        try {
          otherParser.parse();
        } catch (AltaCore::Errors::Error& e) {
          logError(e);
          exit(13);
        }
        auto root = std::dynamic_pointer_cast<AltaCore::AST::RootNode>(*otherParser.root);
        root->detail(path);

        importCache[path.absolutify().toString()] = root;

        return root;
      };

      auto parseTimeStart = std::chrono::high_resolution_clock::now();
      AltaCore::Parser::Parser parser(lexer.tokens, fn);
      try {
        parser.parse();
      } catch (AltaCore::Errors::Error& e) {
        logError(e);
        exit(13);
      }

      if (!parser.root || !(*parser.root)) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to parse \"" << fn.toString() << '"' << std::endl;
        return 6;
      }

      if (doTime) {
        auto duration = AltaCore::Timing::toMilliseconds(AltaCore::Timing::parseTimes[fn].total());
        std::cout << CLI::COLOR_BLUE << "Info" << ": parsed \"" << fn.toString() << "\" in " << duration.count() << "ms" << std::endl;
      }

      if (verboseSwitch.getValue()) {
        /*
        printf("\n%sAST:%s\n", CLI::COLOR_BLUE, CLI::COLOR_NORMAL);
        CLI::Printers::printAST(parser.root);
        printf("\n");
        */
      }

      auto root = std::dynamic_pointer_cast<AltaCore::AST::RootNode>(*parser.root);

      if (!root) {
        std::cerr << CLI::COLOR_RED << "Internal error" << CLI::COLOR_NORMAL << ": root node returned by parser was not an `AltaCore::AST::RootNode`" << std::endl;
      }

      try {
        root->detail(fn);
        root->info->module->packageInfo.isEntryPackage = true;
      } catch (AltaCore::Errors::DetailingError& e) {
        std::cerr << CLI::COLOR_RED << "AST failed detailing" << CLI::COLOR_NORMAL << std::endl;
        logError(e);
        return 12;
      }

      if (doTime) {
        for (auto& [path, timer]: AltaCore::Timing::parseTimes) {
          if (path == fn) continue;
          auto duration = AltaCore::Timing::toMilliseconds(timer.total());
          std::cout << CLI::COLOR_BLUE << "Info" << ": parsed \"" << path.toString() << "\" in " << duration.count() << "ms" << std::endl;
        }
      }

      auto entryPackageInfo = root->info->module->packageInfo;

      if (verboseSwitch.getValue()) {
        /*
        printf("\n%sDET:%s\n", CLI::COLOR_BLUE, CLI::COLOR_NORMAL);
        CLI::Printers::printDET(parser.root->$module);
        printf("\n");
        */
      }

      try {
        AltaCore::Validator::validate(root);
      } catch (AltaCore::Errors::ValidationError& e) {
        std::cerr << CLI::COLOR_RED << "AST failed semantic validation" << CLI::COLOR_NORMAL << std::endl;
        logError(e);
        return 11;
      }

      AltaCore::Filesystem::mkdirp(outDir); // ensure the output directory has been created

      auto transpiledModules = Talta::recursivelyTranspileToC(root);

      // TL;DR: reset the attributes, otherwise you'll end up with dangling
      //        references in the attribute callbacks for attributes in the
      //        `CTranspiler` domain
      //
      // reseting the attributes is necessary because otherwise
      // any modules used in for this target that are reused for any
      // other targets retain the old attribute callbacks;
      // each target uses a different transpiler instance,
      // so that would leave attribute callbacks created for
      // with a reference to a transpiler instance that has
      // already been disposed of, leading to LOTS of dangling references
      AltaCore::Attributes::clearAllAttributes();

      auto indexHeaderPath = outDir / "index.h";
      std::ofstream outfileIndex(indexHeaderPath.toString());

      auto generalCmakeListsPath = outDir / "CMakeLists.txt";
      std::ofstream generalCmakeLists(generalCmakeListsPath.toString());

      ALTACORE_MAP<std::string, std::pair<std::ofstream, AltaCore::Modules::PackageInfo>> cmakeListsCollection;
      ALTACORE_MAP<std::string, std::set<std::string>> packageDependencies;

      std::stringstream uuidStream;
      uuidStream << xg::newGuid();

      std::string indexUUID = uuidStream.str();
      std::replace(indexUUID.begin(), indexUUID.end(), '-', '_');

      // setup index header
      outfileIndex << "#ifndef _ALTA_INDEX_" << indexUUID << '\n';
      outfileIndex << "#define _ALTA_INDEX_" << indexUUID << '\n';

      // setup root cmakelists
      generalCmakeLists << "cmake_minimum_required(VERSION 3.10)\n";
      generalCmakeLists << "project(" << indexUUID << ")\n";
      generalCmakeLists << "add_subdirectory(\"${PROJECT_SOURCE_DIR}/../_runtime\" \"${ALTA_CURRENT_PROJECT_BINARY_DIR}/_runtime\")\n";
      generalCmakeLists << "add_subdirectory(\"${PROJECT_SOURCE_DIR}/" << root->info->module->packageInfo.name << "\")\n";
      generalCmakeLists.close();

      for (auto& [moduleName, info]: transpiledModules) {
        auto& [cRoot, hRoot, mod] = info;

        if (packageDependencies.find(mod->packageInfo.name) == packageDependencies.end()) {
          packageDependencies[mod->packageInfo.name] = std::set<std::string>();
        }

        for (auto& dep: mod->dependencies) {
          if (dep->packageInfo.name == mod->packageInfo.name) continue;
          packageDependencies[mod->packageInfo.name].insert(dep->packageInfo.name);
        }
      }

      for (auto& [moduleName, info]: transpiledModules) {
        auto& [cRoot, hRoot, mod] = info;
        auto base = outDir / moduleName;
        auto modOutDir = outDir / mod->packageInfo.name;
        auto cOut = base + ".c";
        auto hOut = base + ".h";
        auto cmakeOut = modOutDir / "CMakeLists.txt";

        auto ok = AltaCore::Filesystem::mkdirp(base.dirname());
        if (!ok) {
          auto str = base.dirname().toString();
          std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to create output directory (\"" << str.c_str() << "\")" << std::endl;
          return 1;
        }

        std::ofstream outfileC(cOut.toString());
        std::ofstream outfileH(hOut.toString());

        if (cmakeListsCollection.find(mod->packageInfo.name) == cmakeListsCollection.end()) {
          // set up the cmakelists for this package
          cmakeListsCollection[mod->packageInfo.name] = { std::ofstream(cmakeOut.toString()), mod->packageInfo };
          auto& lists = cmakeListsCollection[mod->packageInfo.name].first;
          lists << "cmake_minimum_required(VERSION 3.10)\n";
          lists << "project(" << mod->packageInfo.name << ")\n";
          lists << "add_custom_target(alta-dummy-target-" << mod->packageInfo.name << '-' << target.name << ")\n";
          //lists << "set(ALTA_PACKAGE_DEFINITION_CHECK_" << mod->packageInfo.name << " \"TRUE\" CACHE INTERNAL \"package definition check for " << mod->packageInfo.name << ". do not touch or modify.\" FORCE)\n";

          for (auto& item: packageDependencies[mod->packageInfo.name]) {
            lists << "if (NOT TARGET alta-dummy-target-" << item << '-' << target.name << ")\n";
            lists << "  add_subdirectory(\"${PROJECT_SOURCE_DIR}/../" << item << "\" \"${ALTA_CURRENT_PROJECT_BINARY_DIR}/" << item << "\")\n";
            lists << "endif()\n";
          }

          if (mod->packageInfo.name == entryPackageInfo.name) {
            if (target.type == AltaCore::Modules::OutputBinaryType::Exectuable) {
              lists << "add_executable(";
            } else {
              lists << "add_library(";
            }
          } else {
            lists << "add_library(";
          }

          lists << mod->packageInfo.name << '-' << target.name << "\n";
        }

        auto& outfileCmake = cmakeListsCollection[mod->packageInfo.name].first;

        outfileC << "#define _ALTA_MODULE_ALL_" << Talta::mangleName(mod.get()) << "\n";
        outfileC << "#include \"" << hOut.relativeTo(cOut).toString('/') << "\"\n";
        outfileC << cRoot->toString();

        auto mangledModuleName = Talta::mangleName(mod.get());

        // include the Alta common runtime header
        outfileH << "#define _ALTA_RUNTIME_COMMON_HEADER_" << mangledModuleName << " \"" << outdirRuntime.relativeTo(hOut).toString('/') << "/common.h\"\n";

        // include the index in the module's header, otherwise we won't be able to find our dependencies
        outfileH << "#include \"" << indexHeaderPath.relativeTo(hOut).toString('/') << "\"\n";

        outfileH << hRoot->toString();

        outfileCmake << "  \"${PROJECT_SOURCE_DIR}/" << cOut.relativeTo(modOutDir).toString('/') << "\"\n";

        // finally, add our header to the index for all our dependents
        for (auto& dependent: mod->dependents) {
          outfileIndex << "#define _ALTA_MODULE_" << Talta::mangleName(dependent.get()) << "_0_INCLUDE_" << mangledModuleName << " \"" << hOut.relativeTo(outDir / dependent->name).toString('/') << "\"\n";
        }
      }

      outfileIndex << "#endif // _ALTA_INDEX_" << indexUUID << "\n";
      outfileIndex.close();

      for (auto& [packageName, cmakeInfo]: cmakeListsCollection) {
        auto& [cmakeLists, modInfo] = cmakeInfo;

        cmakeLists << ")\n";

        if (modInfo.isEntryPackage) {
          cmakeLists << "set_target_properties(" << packageName << '-' << target.name << " PROPERTIES\n";
          cmakeLists << "  OUTPUT_NAME " << target.name << '\n';
          cmakeLists << ")\n";
        }

        cmakeLists << "target_link_libraries(" << packageName << '-' << target.name << " PUBLIC\n";

        cmakeLists << "  alta-global-runtime" << '\n';

        for (auto& dep: packageDependencies[packageName]) {
          cmakeLists << "  " << dep << '-' << target.name << '\n';
        }

        cmakeLists << ")\n";

        if (modInfo.isEntryPackage) {
          std::string extensionVar;
          if (target.type == AltaCore::Modules::OutputBinaryType::Exectuable) {
            extensionVar = "${ALTA_EXECUTABLE_EXTENSION}";
          } else {
            extensionVar = "${ALTA_STATLIB_EXTENSION}";
          }
          cmakeLists << "add_custom_command(TARGET " << packageName << '-' << target.name << " POST_BUILD\n";
          std::string outFileDir;
          if (target.type == AltaCore::Modules::OutputBinaryType::Exectuable) {
            outFileDir = "bin";
          } else {
            outFileDir = "lib";
          }
          cmakeLists << "  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:" << packageName << '-' << target.name << "> \"${CMAKE_BINARY_DIR}/" << outFileDir << '/' << target.name << extensionVar << "\"\n";
          cmakeLists << ")\n";
        }

        cmakeLists.close();
      }
    }

    rootCmakeLists.close();

    std::cout << CLI::COLOR_GREEN << "Successfully transpiled input!" << CLI::COLOR_NORMAL << std::endl;

    if (compileSwitch.getValue()) {
      if (system(NULL) == 0) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to compile the C code - no command processor is available." << std::endl;
        return 11;
      }

      AltaCore::Filesystem::mkdirp(origOutDir / "_build");

      std::string configureCommand = "cmake -S \"" + origOutDir.toString() + "\" -B \"" + (origOutDir / "_build").toString() + '"';
      std::string compileCommand = "cmake --build \"" + (origOutDir / "_build").toString() + '"';

      if (!generatorArg.getValue().empty()) {
        configureCommand += " -G \"" + generatorArg.getValue() + "\"";
      }

      auto configureResult = system(configureCommand.c_str());

      if (configureResult != 0) {
        std::cerr << CLI::COLOR_RED << "Failed to configure the C code for building." << CLI::COLOR_NORMAL << std::endl;
        std::cout << CLI::COLOR_BLUE << "Info" << CLI::COLOR_NORMAL << ": The command that was executed was `" << configureCommand << '`' << std::endl;
        std::cout << CLI::COLOR_BLUE << "Info" << CLI::COLOR_NORMAL << ": CMake exited with status code " << configureResult << std::endl;
        return 9;
      }

      auto compileResult = system(compileCommand.c_str());

      if (compileResult != 0) {
        std::cerr << CLI::COLOR_RED << "Failed to compile the C code." << CLI::COLOR_NORMAL << std::endl;
        std::cout << CLI::COLOR_BLUE << "Info" << CLI::COLOR_NORMAL << ": The command that was executed was `" << compileCommand << '`' << std::endl;
        std::cout << CLI::COLOR_BLUE << "Info" << CLI::COLOR_NORMAL << ": CMake exited with status code " << compileResult << std::endl;
        return 10;
      }

      std::cout << CLI::COLOR_GREEN << "Successfully compiled the generated C code!" << CLI::COLOR_NORMAL << std::endl;
    }

    return 0;
  } catch (TCLAP::ArgException& e) {
    std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": " << e.error() << " for argument \"" << e.argId() << "\"" << std::endl;

    return 1;
  } catch (std::runtime_error& e) {
    std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": " << e.what() << std::endl;
    return 100;
  }
};
