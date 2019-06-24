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

#if defined(_WIN32) || defined(_WIN64)
#include <Windows.h>
#include <direct.h>
#define changeDir _chdir
#else
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <limits.h>
#define changeDir chdir
#endif

ALTACORE_MAP<std::string, std::vector<std::string>> libsToLink;

#define AC_ATTRIBUTE_FUNC [=](std::shared_ptr<AST::Node> _target, std::shared_ptr<DH::Node> _info, std::vector<Attributes::AttributeArgument> args) -> void
#define AC_ATTRIBUTE_CAST(x) auto target = std::dynamic_pointer_cast<AST::x>(_target);\
  auto info = std::dynamic_pointer_cast<DH::x>(_info);\
  if (!target || !info) throw std::runtime_error("this isn't supposed to happen");
#define AC_ATTRIBUTE(x, ...) Attributes::registerAttribute({ __VA_ARGS__ }, { AST::NodeType::x }, AC_ATTRIBUTE_FUNC {\
  AC_ATTRIBUTE_CAST(x);
#define AC_GENERAL_ATTRIBUTE(...) Attributes::registerAttribute({ __VA_ARGS__ }, {}, AC_ATTRIBUTE_FUNC {
#define AC_END_ATTRIBUTE }, file.toString())

void registerAttributes(AltaCore::Filesystem::Path file) {
  using namespace AltaCore;
  AC_GENERAL_ATTRIBUTE("CTranspiler", "link");
    if (args.size() < 1) return;
    if (!args[0].isString) return;
    auto ast = std::dynamic_pointer_cast<AST::RootNode>(_target);
    libsToLink[ast->info->module->packageInfo.name].push_back(args[0].string);
  AC_END_ATTRIBUTE;
};

#undef AC_END_ATTRIBUTE
#undef AC_ATTIBUTE
#undef AC_ATTRIBUTE_CAST
#undef AC_ATTRIBUTE_FUNC

AltaCore::Filesystem::Path findProgramPath(const std::string arg) {
  using Path = AltaCore::Filesystem::Path;
  auto asPath = Path(arg);
  auto proc = Path("/proc");
  bool doDefault = false;
  std::string sep = "";

  if (asPath.isAbsolute()) {
    return asPath;
  }

#if defined(_WIN32) || defined(_WIN64)
  sep = ";";

  size_t bufferSize = MAX_PATH;
  LPSTR buffer = NULL;
  bool ok = false;
  bool possible = true;

  while (!ok && possible) {
    if (buffer != NULL) {
      delete[] buffer;
    }
    buffer = new TCHAR[bufferSize + 1]();
    auto result = GetModuleFileName(NULL, buffer, bufferSize);
    if (result == bufferSize && GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      possible = true;
      ok = false;
      continue;
    }
    if (result == 0) {
      possible = false;
      ok = false;
      delete[] buffer;
      break;
    }
    ok = true;
    possible = true;
    auto asString = std::string(buffer);
    delete[] buffer;
    return asString;
  }

  if (arg.find('\\') != std::string::npos) {
    return asPath.absolutify();
  }
#else
  sep = ":";

  if (proc.exists()) {
    auto link = proc / "self" / "exe";
    if (!link.exists()) {
      link = proc / "curproc" / "file";
      if (!link.exists()) {
        link = proc / "self" / "path" / "a.out";
        if (!link.exists()) {
          doDefault = true;
        }
      }
    }
    if (!doDefault) {
      auto linkStr = link.toString();
      struct stat info;
      size_t bufferSize = PATH_MAX;
      if (lstat(linkStr.c_str(), &info) < 0) {
        bufferSize = info.st_size;
      }
      char* buffer = new char[bufferSize + 1]();
      if (readlink(linkStr.c_str(), buffer, bufferSize) < 0) {
        doDefault = true;
      }
      std::string result = doDefault ? std::string() : std::string(buffer);
      delete[] buffer;
      if (!doDefault) {
        return result;
      }
    }
  } else {
    doDefault = true;
  }

  if (arg.find('/') != std::string::npos) {
    return asPath.absolutify();
  }
#endif

  auto pathVar = getenv("PATH");
  auto paths = Path(pathVar, sep);

  for (auto path: paths.components) {
    auto pathPath = Path(path);
    if (!pathPath.exists()) continue;
    if (!pathPath.isDirectory()) continue;

    auto result = pathPath / asPath;
    if (result.exists()) {
      return result;
    }
  }

  return Path();
};

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

    auto programPath = findProgramPath(argv[0]);
    if (!programPath) {
      std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to determine the path of this executable" << std::endl;
      return 14;
    }

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
      ALTACORE_MAP<std::string, AltaCore::Parser::PrepoExpression> defs;

      std::string platform = "other";
      std::string compatability = "none";

#if defined(_WIN32) || defined(_WIN32)
      platform = "windows";
      compatability = "nt";
#if defined(__CYGWIN__)
      compatability += ";posix";
#endif
#elif defined(__linux__)
      compatability = "posix";
#if defined(__ANDROID__)
      platform = "android";
#else
      platform = "linux";
#endif
#elif defined(__FreeBSD__) || defined(__NetBSD__) || defined(__OpenBSD__) || defined(__bsdi__) || defined(__DragonFly__) || defined(__FreeBSD_kernel__)
      compatability = "posix";
      platform = "bsd";
#elif defined(__APPLE__)
      compatability = "posix";
      platform = "darwin";
#elif defined(macintosh) || defined(Macintosh)
      platform = "oldmac";
#elif defined(sun) || defined(__sun)
      compatability = "posix";
      platform = "solaris";
#endif

      defs["platform"] = platform;
      defs["compatability"] = compatability;

      AltaCore::Modules::parsingDefinitions = &defs;

      registerAttributes(fn);
      Talta::registerAttributes(fn);

      AltaCore::Lexer::Lexer lexer(fn);

      if (!file.is_open()) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to open the input file (\"" << fn.toString() << '"' << std::endl;
        return 2;
      }

      while (std::getline(file, line)) {
        if (file.peek() != EOF) {
          line += "\n";
        }
        lexer.feed(line);
      }

      file.close();

      if (doTime) {
        auto timer = AltaCore::Timing::lexTimes[fn];
        auto duration = AltaCore::Timing::toMilliseconds(timer.total());
        std::cout << CLI::COLOR_BLUE << "Info" << CLI::COLOR_NORMAL << ": lexed \"" << fn.toString() << "\" in " << duration.count() << "ms" << std::endl;
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

      auto logError = [&](AltaCore::Errors::Error& e) {
        std::cerr << "Error at " << e.position.file.toString() << ":" << e.position.line << ":" << e.position.column << std::endl;
        if (e.position.file) {
          std::ifstream fileSource(e.position.file.toString());
          // seek to the beginning of the line
          fileSource.seekg(e.position.filePosition - e.position.column);
          std::string line;
          std::getline(fileSource, line);
          std::cerr << line << std::endl;
          for (size_t i = 1; i < e.position.column; i++) {
            std::cerr << ' ';
          }
          std::cerr << '^' << std::endl;
          for (size_t i = 1; i < e.position.column; i++) {
            std::cerr << ' ';
          }
        }
        std::cerr << e.what() << std::endl;
      };

      ALTACORE_MAP<std::string, std::shared_ptr<AltaCore::AST::RootNode>> importCache;

      auto defaultParseModule = AltaCore::Modules::parseModule;
      AltaCore::Modules::parseModule = [&](std::string importRequest, AltaCore::Filesystem::Path requestingPath) -> std::shared_ptr<AltaCore::AST::RootNode> {
        auto path = AltaCore::Modules::resolve(importRequest, requestingPath);

        registerAttributes(path);
        Talta::registerAttributes(path);

        try {
          return defaultParseModule(importRequest, requestingPath);
        } catch (AltaCore::Errors::Error& e) {
          logError(e);
          exit(13);
        }
      };

      AltaCore::Parser::Parser parser(lexer.tokens, defs, fn);
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
        std::cout << CLI::COLOR_BLUE << "Info" << CLI::COLOR_NORMAL << ": preprocessed and parsed \"" << fn.toString() << "\" in " << duration.count() << "ms" << std::endl;
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
        for (auto& [path, timer]: AltaCore::Timing::lexTimes) {
          if (path == fn) continue;
          auto duration = AltaCore::Timing::toMilliseconds(timer.total());
          std::cout << CLI::COLOR_BLUE << "Info" << CLI::COLOR_NORMAL << ": lexed \"" << path.toString() << "\" in " << duration.count() << "ms" << std::endl;
        }
        for (auto& [path, timer]: AltaCore::Timing::parseTimes) {
          if (path == fn) continue;
          auto duration = AltaCore::Timing::toMilliseconds(timer.total());
          std::cout << CLI::COLOR_BLUE << "Info" << CLI::COLOR_NORMAL << ": preprocessed and parsed \"" << path.toString() << "\" in " << duration.count() << "ms" << std::endl;
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

      auto dummySourcePath = outDir / "dummy.c";
      std::ofstream dummySource(dummySourcePath.toString());

      dummySource << "\n";
      dummySource.close();

      ALTACORE_MAP<std::string, std::pair<std::ofstream, AltaCore::Modules::PackageInfo>> cmakeListsCollection;
      ALTACORE_MAP<std::string, std::set<std::string>> packageDependencies;
      ALTACORE_MAP<std::string, std::vector<std::shared_ptr<AltaCore::DET::ScopeItem>>> genericsUsed;

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
        auto& [cRoot, hRoot, dRoot, gRoots, gItems, mod] = info;

        if (packageDependencies.find(mod->packageInfo.name) == packageDependencies.end()) {
          packageDependencies[mod->packageInfo.name] = std::set<std::string>();
        }

        for (auto& dep: mod->dependencies) {
          if (dep->packageInfo.name == mod->packageInfo.name) continue;
          packageDependencies[mod->packageInfo.name].insert(dep->packageInfo.name);
        }

        for (auto& generic: mod->genericsUsed) {
          auto gMod = AltaCore::Util::getModule(generic->parentScope.lock().get()).lock();
          if (gMod->packageInfo.name == mod->packageInfo.name) continue;
          genericsUsed[mod->packageInfo.name].push_back(generic);
        }
      }

      for (auto& [moduleName, info]: transpiledModules) {
        auto& [cRoot, hRoot, dRoot, gRoots, gItems, mod] = info;
        auto base = outDir / moduleName;
        auto modOutDir = outDir / mod->packageInfo.name;
        auto cOut = base + ".c";
        auto hOut = base + ".h";
        auto dOut = base + ".d.h";
        std::vector<AltaCore::Filesystem::Path> gOuts;
        auto cmakeOut = modOutDir / "CMakeLists.txt";

        auto ok = AltaCore::Filesystem::mkdirp(base.dirname());
        if (!ok) {
          auto str = base.dirname().toString();
          std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to create output directory (\"" << str.c_str() << "\")" << std::endl;
          return 1;
        }

        std::ofstream outfileC(cOut.toString());
        std::ofstream outfileH(hOut.toString());
        std::ofstream outfileD(dOut.toString());
        std::vector<std::ofstream> outfileGs;

        for (size_t i = 0; i < gRoots.size(); i++) {
          auto gOut = base + "-" + std::to_string(i) + ".c";
          gOuts.push_back(gOut);
          outfileGs.push_back(std::ofstream(gOut.toString()));
        }

        if (cmakeListsCollection.find(mod->packageInfo.name) == cmakeListsCollection.end()) {
          // set up the cmakelists for this package
          cmakeListsCollection[mod->packageInfo.name] = { std::ofstream(cmakeOut.toString()), mod->packageInfo };
          auto& lists = cmakeListsCollection[mod->packageInfo.name].first;
          lists << "cmake_minimum_required(VERSION 3.10)\n";
          lists << "project(" << mod->packageInfo.name << ")\n";
          lists << "add_custom_target(alta-dummy-target-" << mod->packageInfo.name << '-' << target.name << ")\n";

          for (auto& item: packageDependencies[mod->packageInfo.name]) {
            lists << "if (NOT TARGET alta-dummy-target-" << item << '-' << target.name << ")\n";
            lists << "  add_subdirectory(\"${PROJECT_SOURCE_DIR}/../" << item << "\" \"${ALTA_CURRENT_PROJECT_BINARY_DIR}/" << item << "\")\n";
            lists << "endif()\n";
          }

          lists << "add_library(" << mod->packageInfo.name << "-core-" << target.name << "\n";
        }

        auto& outfileCmake = cmakeListsCollection[mod->packageInfo.name].first;

        outfileC << "#define _ALTA_MODULE_ALL_" << Talta::mangleName(mod.get()) << "\n";
        outfileC << "#include \"" << hOut.relativeTo(cOut).toString('/') << "\"\n";
        outfileC << cRoot->toString();

        for (size_t i = 0; i < gRoots.size(); i++) {
          auto& gRoot = gRoots[i];
          auto& outfileG = outfileGs[i];
          auto& gOut = gOuts[i];
          auto& gItem = gItems[i];
          auto gName = Talta::headerMangle(gItem.get());
          outfileG << "#define _ALTA_MODULE_ALL_" << Talta::mangleName(mod.get()) << "\n";
          outfileG << "#define " + gName + "\n";
          outfileG << "#include \"" << hOut.relativeTo(gOut).toString('/') << "\"\n";
          outfileG << gRoot->toString();
        }

        auto mangledModuleName = Talta::mangleName(mod.get());

        // include the Alta common runtime header
        outfileH << "#define _ALTA_RUNTIME_COMMON_HEADER_" << mangledModuleName << " \"" << outdirRuntime.relativeTo(hOut).toString('/') << "/common.h\"\n";

        // define the definition header include path
        outfileH << "#define _ALTA_DEF_HEADER_" << mangledModuleName << " \"" << dOut.relativeTo(hOut).toString('/') << "\"\n";

        // include the index in the module's header, otherwise we won't be able to find our dependencies
        outfileH << "#include \"" << indexHeaderPath.relativeTo(hOut).toString('/') << "\"\n";

        outfileH << hRoot->toString();

        outfileCmake << "  \"${PROJECT_SOURCE_DIR}/" << cOut.relativeTo(modOutDir).toString('/') << "\"\n";

        outfileD << dRoot->toString();
        outfileD.close();

        // finally, add our header to the index for all our dependents
        for (auto& dependent: mod->dependents) {
          outfileIndex << "#define _ALTA_MODULE_" << Talta::mangleName(dependent.get()) << "_0_INCLUDE_" << mangledModuleName << " \"" << hOut.relativeTo(outDir / dependent->name).toString('/') << "\"\n";
        }

        // add our header to index for ourselves (in case we have any generics that we use)
        outfileIndex << "#define _ALTA_MODULE_" << mangledModuleName << "_0_INCLUDE_" << mangledModuleName << " \"" << hOut.relativeTo(cOut).toString('/') << "\"\n";
      }

      for (auto& [packageName, cmakeInfo]: cmakeListsCollection) {
        auto& [cmakeLists, modInfo] = cmakeInfo;

        cmakeLists << ")\n";

        cmakeLists << "set_target_properties(" << packageName << "-core-" << target.name << " PROPERTIES\n";
        cmakeLists << "  C_STANDARD 99\n";
        cmakeLists << ")\n";

        cmakeLists << "target_link_libraries(" << packageName << "-core-" << target.name << " PUBLIC\n";

        cmakeLists << "  alta-global-runtime" << '\n';

        for (auto& dep: packageDependencies[packageName]) {
          cmakeLists << "  " << dep << '-' << target.name << '\n';
        }

        for (auto& generic: genericsUsed[packageName]) {
          cmakeLists << "  " << Talta::mangleName(generic->parentScope.lock()->parentModule.lock().get()) << '-' << generic->moduleIndex << '-' << target.name << '\n';
        }

        for (auto& lib: libsToLink[packageName]) {
          cmakeLists << "  " << lib << '\n';
        }

        cmakeLists << ")\n";
      }

      for (auto& [moduleName, info]: transpiledModules) {
        auto& [cRoot, hRoot, dRoot, gRoots, gItems, mod] = info;
        auto base = outDir / moduleName;
        auto modOutDir = outDir / mod->packageInfo.name;
        auto& outfileCmake = cmakeListsCollection[mod->packageInfo.name].first;
        auto mangledModuleName = Talta::mangleName(mod.get());
        auto moduleNamePath = AltaCore::Filesystem::Path(moduleName);

        for (size_t i = 0; i < gRoots.size(); i++) {
          auto gOut = base + "-" + std::to_string(i) + ".c";
          auto cOut = base + ".c";
          auto& gItem = gItems[i];
          auto targetName = Talta::mangleName(gItem->parentScope.lock()->parentModule.lock().get()) + '-' + std::to_string(gItem->moduleIndex) + '-' + target.name;

          for (auto& item: mod->genericDependencies[gItem->id]) {
            auto& name = item->packageInfo.name;
            auto mangledImportName = Talta::mangleName(item.get());
            auto depBase = outDir / item->name;
            auto depHOut = depBase + ".h";
            outfileCmake << "if (NOT TARGET alta-dummy-target-" << name << '-' << target.name << ")\n";
            outfileCmake << "  add_subdirectory(\"${PROJECT_SOURCE_DIR}/../" << name << "\" \"${ALTA_CURRENT_PROJECT_BINARY_DIR}/" << name << "\")\n";
            outfileCmake << "endif()\n";
            outfileIndex << "#define _ALTA_MODULE_" << mangledModuleName << "_0_INCLUDE_" << mangledImportName << " \"" << depHOut.relativeTo(cOut).toString('/') << "\"\n";
          }

          outfileCmake << "add_library(" << targetName << '\n';
          if (!gItem->instantiatedFromSamePackage) {
            outfileCmake << "  \"${PROJECT_SOURCE_DIR}/" << gOut.relativeTo(modOutDir).toString('/') << "\"\n";
          } else {
            outfileCmake << "  \"${PROJECT_SOURCE_DIR}/../" << dummySourcePath.relativeTo(modOutDir).toString('/') << "\"\n";
          }
          outfileCmake << ")\n";
          outfileCmake << "set_target_properties(" << targetName << " PROPERTIES\n";
          auto dashName = (moduleNamePath + "-" + std::to_string(i)).toString('-');
          outfileCmake << "  OUTPUT_NAME " << dashName << '\n';
          outfileCmake << "  COMPILE_PDB_NAME " << dashName << '\n';
          outfileCmake << "  C_STANDARD 99\n";
          outfileCmake << ")\n";
          if (gItem->instantiatedFromSamePackage) {
            outfileCmake << "target_sources(" << mod->packageInfo.name << "-core-" << target.name << " PUBLIC\n";
            outfileCmake << "  \"${PROJECT_SOURCE_DIR}/" << gOut.relativeTo(modOutDir).toString('/') << "\"\n";
            outfileCmake << ")\n";
          }
          outfileCmake << "target_link_libraries(" << targetName << " PUBLIC\n";
          if (gItem->instantiatedFromSamePackage || !mod->packageInfo.isEntryPackage || (mod->packageInfo.isEntryPackage && target.type != AltaCore::Modules::OutputBinaryType::Exectuable)) {
            outfileCmake << "  " << mod->packageInfo.name << "-core-" << target.name << '\n';
          }

          if (gItem->instantiatedFromSamePackage) {
            outfileCmake << ")\n";
            outfileCmake << "target_link_libraries(" << mod->packageInfo.name << "-core-" << target.name << " PUBLIC\n";
          }

          for (auto& item: mod->genericDependencies[gItem->id]) {
            outfileCmake << "  " << item->packageInfo.name << '-' << target.name << '\n';
          }

          if (auto klass = std::dynamic_pointer_cast<AltaCore::DET::Class>(gItem)) {
            for (auto& generic: klass->genericArguments) {
              if (generic->klass && generic->klass->genericParameterCount > 0) {
                auto genMod = AltaCore::Util::getModule(generic->klass->parentScope.lock().get()).lock();
                outfileCmake << "  " << Talta::mangleName(genMod.get()) << '-' << generic->klass->moduleIndex << '-' << target.name << '\n';
              }
            }
          } else if (auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(gItem)) {
            for (auto& generic: func->genericArguments) {
              if (generic->klass && generic->klass->genericParameterCount > 0) {
                auto genMod = AltaCore::Util::getModule(generic->klass->parentScope.lock().get()).lock();
                outfileCmake << "  " << Talta::mangleName(genMod.get()) << '-' << generic->klass->moduleIndex << '-' << target.name << '\n';
              }
            }
          } else {
            throw std::runtime_error("wth");
          }

          outfileCmake << ")\n";
        }
      }

      for (auto& [moduleName, info]: transpiledModules) {
        auto& [cRoot, hRoot, dRoot, gRoots, gItems, mod] = info;
        auto base = outDir / moduleName;
        auto modOutDir = outDir / mod->packageInfo.name;
        auto& outfileCmake = cmakeListsCollection[mod->packageInfo.name].first;

        if (mod->packageInfo.name == entryPackageInfo.name) {
          if (target.type == AltaCore::Modules::OutputBinaryType::Exectuable) {
            outfileCmake << "add_executable(";
          } else {
            outfileCmake << "add_library(";
          }
        } else {
          outfileCmake << "add_library(";
        }

        outfileCmake << mod->packageInfo.name << '-' << target.name << '\n';
        outfileCmake << "  \"${PROJECT_SOURCE_DIR}/../" << dummySourcePath.relativeTo(modOutDir).toString('/') << "\"\n";
        outfileCmake << ")\n";
        outfileCmake << "target_link_libraries(" << mod->packageInfo.name << '-' << target.name << " PUBLIC\n";
        outfileCmake << "  " << mod->packageInfo.name << "-core-" << target.name << '\n';
        outfileCmake << ")\n";

        if (mod->packageInfo.isEntryPackage) {
          std::string extensionVar;
          if (target.type == AltaCore::Modules::OutputBinaryType::Exectuable) {
            extensionVar = "${ALTA_EXECUTABLE_EXTENSION}";
          } else {
            extensionVar = "${ALTA_STATLIB_EXTENSION}";
          }
          outfileCmake << "add_custom_command(TARGET " << mod->packageInfo.name << '-' << target.name << " POST_BUILD\n";
          std::string outFileDir;
          if (target.type == AltaCore::Modules::OutputBinaryType::Exectuable) {
            outFileDir = "bin";
          } else {
            outFileDir = "lib";
          }
          outfileCmake << "  COMMAND ${CMAKE_COMMAND} -E copy $<TARGET_FILE:" << mod->packageInfo.name << '-' << target.name << "> \"${CMAKE_BINARY_DIR}/" << outFileDir << '/' << target.name << extensionVar << "\"\n";
          outfileCmake << ")\n";

          outfileCmake << "set_target_properties(" << mod->packageInfo.name << '-' << target.name << " PROPERTIES\n";
          outfileCmake << "  OUTPUT_NAME " << target.name << '\n';
          outfileCmake << "  C_STANDARD 99\n";
          outfileCmake << ")\n";
        }

        outfileCmake.close();
      }

      outfileIndex << "#endif // _ALTA_INDEX_" << indexUUID << "\n";
      outfileIndex.close();
    }

    rootCmakeLists.close();

    std::cout << CLI::COLOR_GREEN << "Successfully transpiled input!" << CLI::COLOR_NORMAL << std::endl;

    if (compileSwitch.getValue()) {
      if (system(NULL) == 0) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to compile the C code - no command processor is available." << std::endl;
        return 11;
      }

      auto rootBuildDir = origOutDir / "_build";
      auto rbdAsString = rootBuildDir.toString();

      AltaCore::Filesystem::mkdirp(rootBuildDir);

      std::string configureCommand = "cmake \"" + origOutDir.toString() + '"';
      std::string compileCommand = "cmake --build \"" + rootBuildDir.toString() + '"';

      if (!generatorArg.getValue().empty()) {
        configureCommand += " -G \"" + generatorArg.getValue() + "\"";
      }

      auto currDir = AltaCore::Filesystem::cwd();

      changeDir(rbdAsString.c_str());
      auto configureResult = system(configureCommand.c_str());
      changeDir(currDir.c_str());

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
  } catch (std::exception& e) {
    std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": " << e.what() << std::endl;
    return 100;
  } catch (...) {
    std::cerr << CLI::COLOR_RED << "Unknown error" << CLI::COLOR_NORMAL << std::endl;
    return 101;
  }
};
