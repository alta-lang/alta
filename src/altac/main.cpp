#include <cstring>
#include <cstdio>
#include <string>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <set>
#include <chrono>

#include "../include/cli.hpp"
#include "alta/version.hpp"
#include <crossguid/guid.hpp>
#include <json.hpp>
#include <picosha2.h>

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

nlohmann::json targetInfoToJSON(const AltaCore::Modules::TargetInfo info) {
  return {
    {"name", info.name},
    {"main", info.main.absolutify().normalize().toString()},
    {"type", info.type == AltaCore::Modules::OutputBinaryType::Library ? "library" : "executable"},
  };
};

nlohmann::json versionToJSON(const semver_t version) {
  nlohmann::json result = {
    {"major", version.major},
    {"minor", version.minor},
    {"patch", version.patch},
  };
  if (version.metadata) {
    result["metadata"] = version.metadata;
  }
  if (version.prerelease) {
    result["prerelease"] = version.prerelease;
  }
  return result;
};

nlohmann::json packageInfoToJSON(const AltaCore::Modules::PackageInfo info) {
  nlohmann::json result = {
    {"name", info.name},
    {"version", versionToJSON(info.version)},
    {"root", info.root.absolutify().normalize().toString()},
    {"main", info.main.absolutify().normalize().toString()},
    {"type", info.outputBinary == AltaCore::Modules::OutputBinaryType::Library ? "library" : "executable"},
    {"entry", info.isEntryPackage}
  };

  for (size_t i = 0; i < info.targets.size(); i++) {
    result["targets"][i] = targetInfoToJSON(info.targets[i]);
  }

  return result;
};

bool checkHashes(const AltaCore::Filesystem::Path filepath, const std::stringstream& otherContent) {
  std::string line;
  std::ifstream file(filepath.toString());
  picosha2::hash256_one_by_one fileHasher;
  picosha2::hash256_one_by_one stringHasher;

  while (std::getline(file, line)) {
    fileHasher.process(line.begin(), line.end());
  }
  fileHasher.finish();

  std::string asString = otherContent.str();
  std::istringstream stringStream(asString);

  while (std::getline(stringStream, line)) {
    stringHasher.process(line.begin(), line.end());
  }
  stringHasher.finish();

  return picosha2::get_hash_hex_string(fileHasher) == picosha2::get_hash_hex_string(stringHasher);
};

void logger(AltaCore::Logging::Message message) {
  std::string severityColor;
  std::string severityName;
  switch (message.severity()) {
    case AltaCore::Logging::Severity::Error: {
      severityColor = CLI::COLOR_RED;
      severityName = "Error";
    } break;
    case AltaCore::Logging::Severity::Warning: {
      severityColor = CLI::COLOR_YELLOW;
      severityName = "Warning";
    } break;
    case AltaCore::Logging::Severity::Information: {
      severityColor = CLI::COLOR_BLUE;
      severityName = "Information";
    } break;
    case AltaCore::Logging::Severity::Verbose: {
      severityColor = CLI::COLOR_GREEN;
      severityName = "Verbose";
    } break;
    default: {
      severityColor = CLI::COLOR_NORMAL;
      severityName = "";
    } break;
  }
  std::cout << severityColor
            << severityName
            << CLI::COLOR_NORMAL
            <<" ["
            << message.code()
            << ']';
  if (message.location().file) {
    std::cout << " at "
              << message.location().file.toString()
              << ":"
              << message.location().line
              << ":"
              << message.location().column
              ;
  }
  std::cout << ": " << message.summary() << std::endl;
  if (message.location().file) {
    std::ifstream fileSource(message.location().file.toString());
    // seek to the beginning of the line
    fileSource.seekg(message.location().filePosition - message.location().column + 1);
    std::string line;
    std::getline(fileSource, line);
    std::cout << line << std::endl;
    for (size_t i = 1; i < message.location().column; i++) {
      std::cout << ' ';
    }
    std::cout << '^' << std::endl;
  }
  if (!message.description().empty()) {
    auto formattedDescription = message.description();
    size_t offset = 0;
    while ((offset = formattedDescription.find('\n', offset)) != std::string::npos) {
      formattedDescription.replace(offset, 1, "\n>> ");
      offset += 4;
    }
    std::cout << ">> " << formattedDescription << std::endl;
  }
};

int main(int argc, char** argv) {
  using json = nlohmann::json;
  using Option = CLI::Option;

  try {
    auto defaultOutDir = "alta-build";

    CLI::Parser parser("altac", "The Alta compiler", Alta::Version::version);

    auto compileSwitch = Option()
      .shortID('c')
      .longID("compile")
      .description("Whether to compile the generated C code with CMake after transpilation")
      ;
    auto generatorArg = Option()
      .shortID('g')
      .longID("cmake-generator")
      .description("The generator to use with CMake when compiling code. Only has meaning when `-c` is specified")
      .valueDescription("CMake Generator ID string")
      .defaultValue("")
      ;
    auto verboseSwitch = Option()
      .shortID('v')
      .longID("verbose")
      .description("Whether to output extra information")
      ;
    auto outdirPath = Option()
      .shortID('o')
      .longID("out-dir")
      .description("The directory in which to put the generated C files. Will be created if it doesn't exist")
      .valueDescription("Folder path")
      .defaultValue(defaultOutDir)
      ;
    auto filenameString = Option()
      .description("A path to a file (for modules) or folder (for packages)")
      .valueDescription("File or folder path")
      .defaultValue(".")
      ;
    auto benchmarkSwitch = Option()
      .shortID("bm")
      .longID("benchmark")
      .description("Whether to time each section of code processing and report the result")
      ;
    auto runtimeInitializer = Option()
      .shortID("ri")
      .longID("runtime-init")
      .description("A module to use to setup the environment necessary for the runtime")
      .valueDescription("Alta module import path")
      ;
    auto freestandingSwitch = Option()
      .shortID("fs")
      .longID("freestanding")
      .description("Indicates that the runtime shouldn't try to use the C standard library")
      ;
    auto searchFlag = Option()
      .shortID('s')
      .longID("search")
      .description("Adds an additional path to search when looking for modules")
      .valueDescription("Folder path")
      .canRepeat(true)
      ;
    auto prioritySearchFlag = Option()
      .shortID("ps")
      .longID("priority-search")
      .description("Adds an additional path to search when looking for modules. This will take precendence over other paths (including the standard library)")
      .valueDescription("Folder path")
      .canRepeat(true)
      ;
    auto hideWarningFlag = Option()
      .shortID("nw")
      .longID("no-warn")
      .description("Disable the specified compiler warning")
      .valueDescription("Warning ID")
      .canRepeat(true)
      ;
    auto runtimeOverride = Option()
      .shortID("ro")
      .longID("runtime-override")
      .description("Specifies a custom runtime directory to use when building modules")
      .valueDescription("Folder path")
      ;
    auto stdlibOverride = Option()
      .shortID("so")
      .longID("stdlib-override")
      .description("Specifies a custom standard library directory to use when building modules")
      .valueDescription("Folder path")
      ;
    auto outputTypeOption = Option()
      .shortID("ot")
      .longID("output-type")
      .description("Specifies a custom output type (executable, library, etc.) for the target")
      .valueDescription("Output type (`exe` or `lib`)")
      .defaultValue("")
      ;

    parser
      .add(compileSwitch)
      .add(generatorArg)
      .add(verboseSwitch)
      .add(outdirPath)
      .add(filenameString)
      .add(benchmarkSwitch)
      .add(runtimeInitializer)
      .add(freestandingSwitch)
      .add(searchFlag)
      .add(prioritySearchFlag)
      .add(hideWarningFlag)
      .add(runtimeOverride)
      .add(stdlibOverride)
      .add(outputTypeOption)
      .parse(argc, argv)
      ;

    bool isVerbose = verboseSwitch;
    bool doTime = benchmarkSwitch || isVerbose;
    std::string runtimeInit = runtimeInitializer.value();
    bool freestanding = freestandingSwitch;
    auto searchDirs = searchFlag.values();
    auto prioritySearchDirs = prioritySearchFlag.values();
    auto hiddenWarnings = hideWarningFlag.values();
    auto outputType = outputTypeOption.value();

    for (auto warning: hiddenWarnings) {
      if (std::find(CLI::knownWarnings, CLI::knownWarnings + CLI::knownWarnings_count, warning) == CLI::knownWarnings + CLI::knownWarnings_count) {
        std::cout << CLI::COLOR_YELLOW << "Warning" << CLI::COLOR_NORMAL << ": unknown compiler warning \"" << warning << '"' << std::endl;
      }
    }

    auto programPath = findProgramPath(argv[0]);
    if (!programPath) {
      std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to determine the path of this executable" << std::endl;
      return 14;
    }

    AltaCore::Logging::registerListener(logger);

    AltaCore::Modules::searchPaths.insert(AltaCore::Modules::searchPaths.end(), searchDirs.begin(), searchDirs.end());
    AltaCore::Modules::prioritySearchPaths.insert(AltaCore::Modules::prioritySearchPaths.end(), prioritySearchDirs.begin(), prioritySearchDirs.end());

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

    if (stdlibOverride) {
      AltaCore::Modules::standardLibraryPath = stdlibOverride.value();
      AltaCore::Modules::standardLibraryPath = AltaCore::Modules::standardLibraryPath.absolutify().normalize();
    }

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

    if (runtimeOverride) {
      runtimePath = runtimeOverride.value();
      runtimePath = runtimePath.absolutify().normalize();
    }

    bool givenPackage = false;
    std::string filename = filenameString.value();
    auto fn = AltaCore::Filesystem::Path(filename).absolutify();
    auto origFn = fn;
    auto outDir = AltaCore::Filesystem::Path(outdirPath.value()).absolutify();
    auto origOutDir = outDir;
    std::vector<AltaCore::Modules::TargetInfo> targets;
    AltaCore::Filesystem::Path runtimeInitPath;
    AltaCore::Modules::PackageInfo runtimeInitInfo;
    json manifest;

    if (!runtimeInit.empty()) {
      AltaCore::Filesystem::Path modPath(runtimeInit);
      runtimeInitPath = AltaCore::Modules::resolve(runtimeInit, fn.isDirectory() ? fn / "main.alta" : fn).absolutify().normalize();
      runtimeInitInfo = AltaCore::Modules::getInfo(runtimeInitPath);
      AltaCore::Modules::TargetInfo info;
      AltaCore::Filesystem::Path modFile;
      if (modPath.isDirectory()) {
        modFile = info.main;
      } else {
        modFile = runtimeInitInfo.root / (modPath.extname() == "alta" ? modPath : modPath + ".alta");
      }
      info.main = modFile.absolutify().normalize();
      info.type = AltaCore::Modules::OutputBinaryType::Library;
      info.name = "_runtime_init";
      targets.push_back(info);
    }

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
        if (modInfo.targets.size() == 0) {
          AltaCore::Modules::TargetInfo targetInfo;
          targetInfo.main = modInfo.main;
          targetInfo.name = modInfo.name;
          targetInfo.type = modInfo.outputBinary;
          targets.push_back(targetInfo);
        } else {
          targets.insert(targets.end(), modInfo.targets.begin(), modInfo.targets.end());
        }
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
      if (!outputType.empty()) {
        if (outputType.substr(0, 3) == "exe") {
          targets.front().type = AltaCore::Modules::OutputBinaryType::Exectuable;
        } else if (outputType.substr(0, 3) == "lib") {
          targets.front().type = AltaCore::Modules::OutputBinaryType::Library;
        } else {
          std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": invalid output type provided" << std::endl;
          return 15;
        }
      }
    }

    AltaCore::Filesystem::mkdirp(outDir);

    auto outdirRuntime = outDir / "_runtime";
    auto runtimeFiles = getDirectoryListing(runtimePath, true);
    mkdirp(outdirRuntime);
    for (auto& file: runtimeFiles) {
      auto relPath = file.relativeTo(runtimePath);
      if (file.isDirectory()) {
        mkdirp(outdirRuntime / relPath);
      } else {
        mkdirp(file.dirname());
        bool output = true;
        auto outpath = outdirRuntime / relPath;
        if (outpath.exists()) {
          std::ifstream fileIn(file.toString(), std::ios::in | std::ios::binary);
          std::ifstream fileOut(outpath.toString(), std::ios::in | std::ios::binary);
          std::vector<unsigned char> hashIn(picosha2::k_digest_size);
          std::vector<unsigned char> hashOut(picosha2::k_digest_size);
          picosha2::hash256(fileIn, hashIn.begin(), hashIn.end());
          picosha2::hash256(fileOut, hashOut.begin(), hashOut.end());
          auto hashInString = picosha2::bytes_to_hex_string(hashIn.begin(), hashIn.end());
          auto hashOutString = picosha2::bytes_to_hex_string(hashOut.begin(), hashOut.end());
          if (hashInString == hashOutString) {
            output = false;
          }
        }
        if (output) {
          copyFile(file, outpath);
        }
      }
    }

    manifest["runtime"] = {
      {"path", outdirRuntime.absolutify().normalize().toString()},
      {"initializer", nullptr},
    };

    if (!runtimeInit.empty()) {
      manifest["runtime"]["initializer"] = packageInfoToJSON(runtimeInitInfo);
    }

    std::ofstream runtimeCmakeLists((outDir / "_runtime" / "CMakeLists.txt").absolutify().toString());
    runtimeCmakeLists << "cmake_minimum_required(VERSION 3.10)\n";
    runtimeCmakeLists << "project(alta-global-runtime)\n";
    if (!runtimeInit.empty()) {
      runtimeCmakeLists << "if (NOT TARGET alta-dummy-target-" << runtimeInitInfo.name << "-_runtime_init)\n";
      runtimeCmakeLists << "  add_subdirectory(\"${PROJECT_SOURCE_DIR}/../_runtime_init/" << runtimeInitInfo.name << "\" \"${PROJECT_BINARY_DIR}/_runtime_init/" << runtimeInitInfo.name << "\")\n";
      runtimeCmakeLists << "endif()\n";
    }
    runtimeCmakeLists << "if (NOT TARGET alta-global-runtime)\n";
    runtimeCmakeLists << "  add_library(alta-global-runtime\n";
    runtimeFiles = AltaCore::Filesystem::getDirectoryListing(outdirRuntime, true);
    for (auto& runtimeFile: runtimeFiles) {
      if (runtimeFile.extname() == "c") {
        runtimeCmakeLists << "    \"${PROJECT_SOURCE_DIR}/" << runtimeFile.relativeTo(outdirRuntime).toString('/') << "\"\n";
        manifest["runtime"]["sources"].push_back(runtimeFile.absolutify().normalize().toString());
      }
    }
    runtimeCmakeLists << "  )\n";
    runtimeCmakeLists << "alta_add_properties(alta-global-runtime)\n";
    if (!runtimeInit.empty()) {
      auto rel = targets[0].main.relativeTo(runtimeInitInfo.root);
      auto baseModPath = outDir / targets[0].name / runtimeInitInfo.name / rel.dirname() / rel.filename();
      runtimeCmakeLists << "  target_link_libraries(alta-global-runtime PUBLIC " << runtimeInitInfo.name << "-_runtime_init)\n";
      runtimeCmakeLists << "  target_compile_definitions(alta-global-runtime PUBLIC ALTA_RUNTIME_INITIALIZER=\"";
      runtimeCmakeLists << (baseModPath + ".h").absolutify().normalize().toString('/');
      runtimeCmakeLists << "\")\n";
    }
    if (freestanding) {
      runtimeCmakeLists << "  target_compile_definitions(alta-global-runtime PUBLIC ALTA_RUNTIME_FREESTANDING)\n";
    }
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

    // modified from code by Austin Lasher (https://medium.com/@alasher/colored-c-compiler-output-with-ninja-clang-gcc-10bfe7f2b949)
    rootCmakeLists << "option(FORCE_COLORED_OUTPUT \"Always produce ANSI-colored output (GNU/Clang only).\" TRUE)\n";
    rootCmakeLists << "macro(alta_add_properties target)\n"; // 
    rootCmakeLists << "  set(COMPILER_IS_GNU_LIKE \"${CMAKE_CXX_COMPILER_ID}\" STREQUAL \"GNU\" OR \"${CMAKE_CXX_COMPILER_ID}\" STREQUAL \"Clang\" OR \"${CMAKE_CXX_COMPILER_ID}\" STREQUAL \"AppleClang\")\n";
    rootCmakeLists << "  if(${FORCE_COLORED_OUTPUT})\n";
    rootCmakeLists << "    if(\"${CMAKE_CXX_COMPILER_ID}\" STREQUAL \"GNU\")\n";
    rootCmakeLists << "      target_compile_options(${target} PRIVATE -fdiagnostics-color=always)\n";
    rootCmakeLists << "    elseif(\"${CMAKE_CXX_COMPILER_ID}\" STREQUAL \"Clang\" OR \"${CMAKE_CXX_COMPILER_ID}\" STREQUAL \"AppleClang\")\n";
    rootCmakeLists << "      target_compile_options(${target} PRIVATE -fcolor-diagnostics)\n";
    rootCmakeLists << "    endif()\n";
    rootCmakeLists << "  endif()\n";
    rootCmakeLists << "  if(${COMPILER_IS_GNU_LIKE})\n";
    rootCmakeLists << "    target_compile_options(${target} PRIVATE -Wno-parentheses-equality -Wno-unused-value)\n";
    rootCmakeLists << "  endif()\n";
    rootCmakeLists << "endmacro()\n";

    rootCmakeLists << "project(ALTA_TOPLEVEL_PROJECT-" << fn.dirname().basename() << ")\n";

    if (std::find(hiddenWarnings.begin(), hiddenWarnings.end(), "msvc-crt-secure") != hiddenWarnings.end()) {
      // silence MSVC warnings about insecure C functions
      rootCmakeLists << "add_compile_definitions(_CRT_SECURE_NO_WARNINGS=1)\n";
    }

    for (auto& target: targets) {
      fn = target.main;
      outDir = origOutDir / target.name;

      manifest["target-infos"].push_back(targetInfoToJSON(target));

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

      AltaCore::registerGlobalAttributes();
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

      if (verboseSwitch) {
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

      ALTACORE_MAP<std::string, std::shared_ptr<AltaCore::AST::RootNode>> importCache;

      auto defaultParseModule = AltaCore::Modules::parseModule;
      AltaCore::Modules::parseModule = [&](std::string importRequest, AltaCore::Filesystem::Path requestingModulePath) -> std::shared_ptr<AltaCore::AST::RootNode> {
        auto path = AltaCore::Modules::resolve(importRequest, requestingModulePath);

        registerAttributes(path);
        Talta::registerAttributes(path);

        try {
          using namespace AltaCore;
          using namespace AltaCore::Modules;
          auto modPath = resolve(importRequest, requestingModulePath);
          if (importCache.find(modPath.absolutify().toString()) != importCache.end()) {
            return importCache[modPath.absolutify().toString()];
          }
          std::ifstream file(modPath.absolutify().toString());
          std::string line;
          Lexer::Lexer lexer(modPath);

          if (!file.is_open()) {
            throw std::runtime_error("oh no.");
          }

          while (std::getline(file, line)) {
            if (file.peek() != EOF) {
              line += "\n";
            }
            lexer.feed(line);
          }

          file.close();

          Parser::Parser parser(lexer.tokens, *parsingDefinitions, modPath);
          parser.parse();
          auto root = std::dynamic_pointer_cast<AST::RootNode>(*parser.root);
          //root->detail(modPath);

          importCache[modPath.absolutify().toString()] = root;

          return root;
        } catch (AltaCore::Errors::Error& e) {
          logger(AltaCore::Logging::Message("G0001", AltaCore::Logging::Severity::Error, e.position, e.what()));
          exit(13);
        } catch (AltaCore::Logging::Message& e) {
          logger(e);
          exit(14);
        }
      };

      AltaCore::Parser::Parser parser(lexer.tokens, defs, fn);
      try {
        parser.parse();
      } catch (AltaCore::Errors::Error& e) {
        logger(AltaCore::Logging::Message("G0001", AltaCore::Logging::Severity::Error, e.position, e.what()));
        exit(13);
      } catch (AltaCore::Logging::Message& e) {
        logger(e);
        exit(14);
      }

      if (!parser.root || !(*parser.root)) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to parse \"" << fn.toString() << '"' << std::endl;
        return 6;
      }

      if (doTime) {
        auto duration = AltaCore::Timing::toMilliseconds(AltaCore::Timing::parseTimes[fn].total());
        std::cout << CLI::COLOR_BLUE << "Info" << CLI::COLOR_NORMAL << ": preprocessed and parsed \"" << fn.toString() << "\" in " << duration.count() << "ms" << std::endl;
      }

      if (verboseSwitch) {
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
        manifest["target-infos"].back()["package"] = root->info->module->packageInfo.name;
      } catch (AltaCore::Errors::DetailingError& e) {
        std::cerr << CLI::COLOR_RED << "AST failed detailing" << CLI::COLOR_NORMAL << std::endl;
        logger(AltaCore::Logging::Message("G0001", AltaCore::Logging::Severity::Error, e.position, e.what()));
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

      if (verboseSwitch) {
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
        logger(AltaCore::Logging::Message("G0001", AltaCore::Logging::Severity::Error, e.position, e.what()));
        return 11;
      }

      AltaCore::Filesystem::mkdirp(outDir); // ensure the output directory has been created

      ALTACORE_MAP<std::string, std::tuple<std::shared_ptr<Ceetah::AST::RootNode>, std::shared_ptr<Ceetah::AST::RootNode>, std::vector<std::shared_ptr<Ceetah::AST::RootNode>>, std::vector<std::shared_ptr<AltaCore::DET::ScopeItem>>, std::vector<bool>, std::shared_ptr<AltaCore::DET::Module>>> transpiledModules;

      try {
        transpiledModules = Talta::recursivelyTranspileToC(root);
      } catch (AltaCore::Errors::ValidationError& e) {
        std::cerr << CLI::COLOR_RED << "AST failed to transpile" << CLI::COLOR_NORMAL << std::endl;
        logger(AltaCore::Logging::Message("G0001", AltaCore::Logging::Severity::Error, e.position, e.what()));
        return 11;
      }

      Ceetah::AST::newlineOnExpressions = false;

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

      auto generalCmakeListsPath = outDir / "CMakeLists.txt";
      std::ofstream generalCmakeLists(generalCmakeListsPath.toString());

      auto dummySourcePath = outDir / "dummy.c";

      if (!dummySourcePath.exists()) {
        std::ofstream dummySource(dummySourcePath.toString());
        dummySource << "\n";
        dummySource.close();
      }

      ALTACORE_MAP<std::string, std::pair<std::ofstream, AltaCore::Modules::PackageInfo>> cmakeListsCollection;
      ALTACORE_MAP<std::string, std::set<std::string>> packageDependencies;
      ALTACORE_MAP<std::string, std::vector<std::shared_ptr<AltaCore::DET::ScopeItem>>> genericsUsed;

      std::string indexUUID = picosha2::hash256_hex_string(target.main.toString() + ':' + target.name + ':' + std::to_string((size_t)target.type));

      // setup root cmakelists
      generalCmakeLists << "cmake_minimum_required(VERSION 3.10)\n";
      generalCmakeLists << "project(" << indexUUID << ")\n";
      if (target.name != "_runtime_init") {
        generalCmakeLists << "add_subdirectory(\"${PROJECT_SOURCE_DIR}/../_runtime\" \"${ALTA_CURRENT_PROJECT_BINARY_DIR}/_runtime\")\n";
      }
      generalCmakeLists << "add_subdirectory(\"${PROJECT_SOURCE_DIR}/" << root->info->module->packageInfo.name << "\")\n";
      generalCmakeLists.close();

      for (auto& [moduleName, info]: transpiledModules) {
        auto& [hRoot, dRoot, gRoots, gItems, gBools, mod] = info;

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

      ALTACORE_MAP<std::string, std::unordered_set<std::string>> depIndex;

      for (auto& [moduleName, info]: transpiledModules) {
        auto& [hRoot, dRoot, gRoots, gItems, gBools, mod] = info;
        auto base = outDir / moduleName;
        auto hOut = base + ".h";
        auto mangledModuleName = Talta::mangleName(mod.get());
        for (auto& dependent: mod->dependents) {
          depIndex[dependent->id].insert("#define _ALTA_MODULE_" + Talta::mangleName(dependent.get()) + "_0_INCLUDE_" + mangledModuleName + " \"" + hOut.relativeTo(outDir / dependent->name).toString('/') + "\"");
        }
      }

      for (auto& [moduleName, info]: transpiledModules) {
        auto& [hRoot, dRoot, gRoots, gItems, gBools, mod] = info;
        auto base = outDir / moduleName;
        auto modOutDir = outDir / mod->packageInfo.name;
        auto cOut = base + ".c";
        auto hOut = base + ".h";
        auto dOut = base + ".d.h";
        std::vector<AltaCore::Filesystem::Path> gOuts;
        auto cmakeOut = modOutDir / "CMakeLists.txt";

        manifest["targets"][target.name][mod->packageInfo.name]["module-names"].push_back(mod->name);
        manifest["targets"][target.name][mod->packageInfo.name]["original-sources"].push_back(mod->path.absolutify().normalize().toString());

        auto ok = AltaCore::Filesystem::mkdirp(base.dirname());
        if (!ok) {
          auto str = base.dirname().toString();
          std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to create output directory (\"" << str.c_str() << "\")" << std::endl;
          return 1;
        }

        std::stringstream outstringH;
        std::stringstream outstringD;
        std::vector<std::stringstream> outstringGs;

        for (size_t i = 0; i < gRoots.size(); i++) {
          auto gOut = i == 0 ? cOut : base + "-" + std::to_string(i - 1) + ".c";
          gOuts.push_back(gOut);
          outstringGs.push_back(std::stringstream());
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

          manifest["targets"][target.name][mod->packageInfo.name]["info"] = packageInfoToJSON(mod->packageInfo);
        }

        auto& outfileCmake = cmakeListsCollection[mod->packageInfo.name].first;

        for (size_t i = 0; i < gRoots.size(); i++) {
          auto& gRoot = gRoots[i];
          auto& outstringG = outstringGs[i];
          auto& gOut = gOuts[i];
          auto& gItem = gItems[i];
          if (gItem) {
            auto gName = Talta::headerMangle(gItem.get());
            outstringG << "#define " + gName + "\n";
          }
          outstringG << "#include \"" << hOut.relativeTo(gOut).toString('/') << "\"\n";
          outstringG << gRoot->toString();
        }

        auto mangledModuleName = Talta::mangleName(mod.get());

        // include the Alta common runtime header
        outstringH << "#define _ALTA_RUNTIME_COMMON_HEADER_" << mangledModuleName << " \"" << outdirRuntime.relativeTo(hOut).toString('/') << "/common.h\"\n";
        outstringH << "#define _ALTA_RUNTIME_DEFINITIONS_HEADER_" << mangledModuleName << " \"" << outdirRuntime.relativeTo(hOut).toString('/') << "/definitions.h\"\n";

        outstringH << "#ifndef _ALTA_HEADER_DEFS_" << mangledModuleName << '\n';
        outstringH << "#define _ALTA_HEADER_DEFS_" << mangledModuleName << '\n';
        // define the definition header include path
        outstringH << "#define _ALTA_DEF_HEADER_" << mangledModuleName << " \"" << dOut.relativeTo(hOut).toString('/') << "\"\n";

        // add dependency header path definitions to our header
        for (auto& dep: depIndex[mod->id]) {
          outstringH << dep << '\n';
        }

        // add a header path definition to our header for ourselves (in case we have any generics that we use)
        outstringH << "#define _ALTA_MODULE_" << mangledModuleName << "_0_INCLUDE_" << mangledModuleName << " \"" << hOut.relativeTo(cOut).toString('/') << "\"\n";

        for (size_t i = 0; i < gRoots.size(); i++) {
          auto gBool = gBools[i];
          if (!gBool) continue;
          auto& gItem = gItems[i];
          for (auto& item: mod->genericDependencies[gItem->id]) {
            auto mangledImportName = Talta::mangleName(item.get());
            auto depBase = outDir / item->name;
            auto depHOut = depBase + ".h";
            outstringH << "#define _ALTA_MODULE_" << mangledModuleName << "_0_INCLUDE_" << mangledImportName << " \"" << depHOut.relativeTo(cOut).toString('/') << "\"\n";
          }
        }
        outstringH << "#endif /* _ALTA_HEADER_DEFS_" << mangledModuleName << " */\n";

        outstringH << hRoot->toString();

        for (size_t i = 0; i < gRoots.size(); i++) {
          auto gBool = gBools[i];
          if (gBool) continue;
          auto gOut = i == 0 ? cOut : base + "-" + std::to_string(i - 1) + ".c";
          outfileCmake << "  \"${PROJECT_SOURCE_DIR}/" << gOut.relativeTo(modOutDir).toString('/') << "\"\n";
          manifest["targets"][target.name][mod->packageInfo.name]["sources"].push_back(gOut.absolutify().normalize().toString());
        }

        manifest["targets"][target.name][mod->packageInfo.name]["headers"].push_back(hOut.absolutify().normalize().toString());
        manifest["targets"][target.name][mod->packageInfo.name]["definition-headers"].push_back(dOut.absolutify().normalize().toString());

        outstringD << dRoot->toString();

        bool outputH = true;
        if (hOut.exists()) {
          if (checkHashes(hOut, outstringH)) {
            outputH = false;
          }
        }
        if (outputH) {
          std::ofstream outfileH(hOut.toString());
          outfileH << outstringH.rdbuf();
          outfileH.close();
        }

        bool outputD = true;
        if (dOut.exists()) {
          if (checkHashes(dOut, outstringD)) {
            outputD = false;
          }
        }
        if (outputD) {
          std::ofstream outfileD(dOut.toString());
          outfileD << outstringD.rdbuf();
          outfileD.close();
        }

        for (size_t i = 0; i < gRoots.size(); i++) {
          auto& outstringG = outstringGs[i];
          auto& gOut = gOuts[i];

          bool outputG = true;
          if (gOut.exists()) {
            if (checkHashes(gOut, outstringG)) {
              outputG = false;
            }
          }
          if (outputG) {
            std::ofstream outfileG(gOut.toString());
            outfileG << outstringG.rdbuf();
            outfileG.close();
          }
        }
      }

      for (auto& [packageName, cmakeInfo]: cmakeListsCollection) {
        auto& [cmakeLists, modInfo] = cmakeInfo;

        cmakeLists << ")\n";
        cmakeLists << "alta_add_properties(" << packageName << "-core-" << target.name << ")\n";

        cmakeLists << "set_target_properties(" << packageName << "-core-" << target.name << " PROPERTIES\n";
        cmakeLists << "  C_STANDARD 99\n";
        cmakeLists << ")\n";

        if (
          target.name != "_runtime_init"      ||
          packageDependencies.size() > 0 ||
          genericsUsed.size() > 0        ||
          libsToLink.size() > 0
        ) {
          cmakeLists << "target_link_libraries(" << packageName << "-core-" << target.name << " PUBLIC\n";

          manifest["targets"][target.name][packageName]["uses-runtime"] = target.name != "_runtime_init";

          if (target.name != "_runtime_init") {
            cmakeLists << "  alta-global-runtime" << '\n';
          }

          std::unordered_set<std::string> depsLinked;

          for (auto& dep: packageDependencies[packageName]) {
            auto name = dep + '-' + target.name;
            if (depsLinked.find(name) == depsLinked.end()) {
              cmakeLists << "  " << name << '\n';
              manifest["targets"][target.name][packageName]["dependencies"].push_back(dep);
              depsLinked.insert(name);
            }
          }

          for (auto& generic: genericsUsed[packageName]) {
            auto genMod = generic->parentScope.lock()->parentModule.lock();
            auto pkgName = Talta::mangleName(genMod.get());
            auto name = pkgName + '-' + std::to_string(generic->moduleIndex) + '-' + target.name;
            if (depsLinked.find(name) == depsLinked.end()) {
              cmakeLists << "  " << name << '\n';
              manifest["targets"][target.name][packageName]["generics-used"].push_back({
                {"module", genMod->name},
                {"id", std::to_string(generic->moduleIndex)},
              });
              depsLinked.insert(name);
            }
          }

          for (auto& lib: libsToLink[packageName]) {
            cmakeLists << "  " << lib << '\n';
            manifest["targets"][target.name][packageName]["extra-links"].push_back(lib);
          }

          cmakeLists << ")\n";
        }
      }

      for (auto& [moduleName, info]: transpiledModules) {
        auto& [hRoot, dRoot, gRoots, gItems, gBools, mod] = info;
        auto base = outDir / moduleName;
        auto modOutDir = outDir / mod->packageInfo.name;
        auto& outfileCmake = cmakeListsCollection[mod->packageInfo.name].first;
        auto mangledModuleName = Talta::mangleName(mod.get());
        auto moduleNamePath = AltaCore::Filesystem::Path(moduleName);

        for (size_t i = 0; i < gRoots.size(); i++) {
          auto gBool = gBools[i];
          if (!gBool) continue;
          auto gOut = base + "-" + std::to_string(i - 1) + ".c";
          auto cOut = base + ".c";
          auto hOut = base + ".h";
          auto& gItem = gItems[i];
          auto targetName = Talta::mangleName(gItem->parentScope.lock()->parentModule.lock().get()) + '-' + std::to_string(gItem->moduleIndex) + '-' + target.name;
          auto& genEntry = manifest["targets"][target.name][mod->packageInfo.name]["generics"][i];
          genEntry["id"] = gItem->moduleIndex;

          for (auto& item: mod->genericDependencies[gItem->id]) {
            auto& name = item->packageInfo.name;
            auto mangledImportName = Talta::mangleName(item.get());
            auto depBase = outDir / item->name;
            auto depHOut = depBase + ".h";
            genEntry["generic-dependencies"].push_back(item->name);
            outfileCmake << "if (NOT TARGET alta-dummy-target-" << name << '-' << target.name << ")\n";
            outfileCmake << "  add_subdirectory(\"${PROJECT_SOURCE_DIR}/../" << name << "\" \"${ALTA_CURRENT_PROJECT_BINARY_DIR}/" << name << "\")\n";
            outfileCmake << "endif()\n";
          }

          genEntry["source"] = gOut.absolutify().normalize().toString();

          outfileCmake << "add_library(" << targetName << '\n';
          if (!gItem->instantiatedFromSamePackage) {
            outfileCmake << "  \"${PROJECT_SOURCE_DIR}/" << gOut.relativeTo(modOutDir).toString('/') << "\"\n";
          } else {
            outfileCmake << "  \"${PROJECT_SOURCE_DIR}/../" << dummySourcePath.relativeTo(modOutDir).toString('/') << "\"\n";
          }
          outfileCmake << ")\n";
          outfileCmake << "alta_add_properties(" << targetName << ")\n";
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
            genEntry["used-in-core"] = true;
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
                genEntry["generic-arguments"].push_back({
                  {"info", packageInfoToJSON(genMod->packageInfo)},
                  {"id", generic->klass->moduleIndex},
                });
              }
            }
          } else if (auto func = std::dynamic_pointer_cast<AltaCore::DET::Function>(gItem)) {
            for (auto& generic: func->genericArguments) {
              if (generic->klass && generic->klass->genericParameterCount > 0) {
                auto genMod = AltaCore::Util::getModule(generic->klass->parentScope.lock().get()).lock();
                outfileCmake << "  " << Talta::mangleName(genMod.get()) << '-' << generic->klass->moduleIndex << '-' << target.name << '\n';
                genEntry["generic-arguments"].push_back({
                  {"info", packageInfoToJSON(genMod->packageInfo)},
                  {"id", generic->klass->moduleIndex},
                });
              }
            }
          } else {
            throw std::runtime_error("wth");
          }

          outfileCmake << ")\n";
        }
      }

      for (auto& [moduleName, info]: transpiledModules) {
        auto& [hRoot, dRoot, gRoots, gItems, gBools, mod] = info;
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
        outfileCmake << "alta_add_properties(" << mod->packageInfo.name << '-' << target.name << ")\n";
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
    }

    rootCmakeLists.close();

    auto manifestOutpath = origOutDir / "manifest.json";
    std::ofstream manifestOutfile(manifestOutpath.toString());
    manifestOutfile << manifest;
    manifestOutfile.close();

    std::cout << CLI::COLOR_GREEN << "Successfully transpiled input!" << CLI::COLOR_NORMAL << std::endl;

    if (compileSwitch) {
      if (system(NULL) == 0) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": failed to compile the C code - no command processor is available." << std::endl;
        return 11;
      }

      auto rootBuildDir = origOutDir / "_build";
      auto rbdAsString = rootBuildDir.toString();

      AltaCore::Filesystem::mkdirp(rootBuildDir);

      std::string configureCommand = "cmake \"" + origOutDir.toString() + '"';
      std::string compileCommand = "cmake --build \"" + rootBuildDir.toString() + '"';

      if (!generatorArg.value().empty()) {
        configureCommand += " -G \"" + generatorArg.value() + "\"";
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
  }  catch (AltaCore::Logging::Message& e) {
    logger(e);
    return 102;
  } catch (std::exception& e) {
    std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": " << e.what() << std::endl;
    return 100;
  } catch (...) {
    std::cerr << CLI::COLOR_RED << "Unknown error" << CLI::COLOR_NORMAL << std::endl;
    return 101;
  }
};
