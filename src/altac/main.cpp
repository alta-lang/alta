#include <cstring>
#include <cstdio>
#include <string>
#include <fstream>
#include <algorithm>
#include <iostream>
#include <set>
#include <chrono>

#include <altac/cli.hpp>
#include <alta/version.hpp>
#include <crossguid/guid.hpp>
#include <json.hpp>
#include <picosha2.h>
#include <altall/altall.hpp>

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
  AC_GENERAL_ATTRIBUTE("native", "link");
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
            << " ["
            << message.shortSubsystem()
            << '-'
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

// credit to @evan-teran on StackOverflow
// https://stackoverflow.com/a/217605
// <stackoverflow-code>
  // trim from start (copying)
  // trim from start (in place)
  static inline void ltrim(std::string &s) {
    s.erase(s.begin(), std::find_if(s.begin(), s.end(), [](int ch) {
      return !std::isspace(ch);
    }));
  };

  // trim from end (in place)
  static inline void rtrim(std::string &s) {
    s.erase(std::find_if(s.rbegin(), s.rend(), [](int ch) {
      return !std::isspace(ch);
    }).base(), s.end());
  };

  // trim from both ends (in place)
  static inline void trim(std::string &s) {
    ltrim(s);
    rtrim(s);
  };

  static inline std::string ltrimCopy(std::string s) {
    ltrim(s);
    return s;
  };

  // trim from end (copying)
  static inline std::string rtrimCopy(std::string s) {
    rtrim(s);
    return s;
  };

  // trim from both ends (copying)
  static inline std::string trimCopy(std::string s) {
    trim(s);
    return s;
  };
// </stackoverflow-code>

int main(int argc, char** argv) {
  using json = nlohmann::json;
  using Option = CLI::Option;
  using namespace CLI;

  if (!AltaLL::init()) {
    throw std::runtime_error("Failed to initialize compiler");
  }

  AltaCore::Logging::shortSubsystemNames.push_back(std::make_pair<std::string, std::string>("frontend", "CLI"));
  AltaCore::Logging::codeSummaryRepositories["frontend"] = {};

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
    auto buildForDebugSwitch = Option()
      .shortID("d")
      .longID("debug")
      .description("Builds the binary for debugging. This will set the CMake variable \"CMAKE_BUILD_TYPE\" to \"Debug\" (by default, it is set to \"Release\"). Note that some CMake generators ignore this variable. Running the Alta compiler with the `-c` switch (to compile the code after generating it) and with this switch will automatically handle those generators and build for debug.")
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
      .add(buildForDebugSwitch)
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
    bool buildForDebug = buildForDebugSwitch;

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
        targetInfo.type = AltaCore::Modules::OutputBinaryType::Exectuable;
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
          targetInfo.type = modInfo.outputBinary;
          targets.push_back(targetInfo);
        }
      } catch (AltaCore::Modules::PackageInformationNotFoundError&) {
        AltaCore::Modules::TargetInfo targetInfo;
        targetInfo.main = fn;
        targetInfo.name = fn.filename();
        targetInfo.type = AltaCore::Modules::OutputBinaryType::Exectuable;
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

    for (auto& target: targets) {
      fn = target.main;
      outDir = origOutDir / target.name;

      manifest["target-infos"].push_back(targetInfoToJSON(target));

      if (!fn.exists()) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": main module (\"" << fn.toString() << "\") for target \"" << target.name << "\" not found" << std::endl;
        return 8;
      }

      std::ifstream file(fn.toString());
      std::string line;
      ALTACORE_MAP<std::string, AltaCore::Parser::PrepoExpression> defs;

      std::string platform = "other";
      std::string compatability = "none";
      std::string arch = "unknown";
      std::string bitness = "0";
      std::string endianness = "unknown";

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

#if defined(__x86_64__) || defined(__x86_64) || defined(_M_AMD64) || defined(_M_X64)
      arch = "x86_64";
      bitness = "64";
#elif defined(__i386__) || defined(__i386) || defined(_M_IX86)
      arch = "i386";
      bitness = "32";
#elif defined(__aarch64__)
      arch = "aarch64";
      bitness = "64";
#elif defined(__arm__)
      arch = "arm";
      bitness = "32";
#endif

#if defined(__LITTLE_ENDIAN__) || defined(__LITTLE_ENDIAN)
      endianness = "little";
#elif defined(__BIG_ENDIAN__) || defined(__BIG_ENDIAN)
      endianness = "big";
#endif

      defs["platform"] = platform;
      defs["compatability"] = compatability;
      defs["arch"] = arch;
      defs["bitness"] = bitness;
      defs["endianness"] = endianness;

      AltaCore::Modules::parsingDefinitions = &defs;

      AltaCore::registerGlobalAttributes();
      registerAttributes(fn);
      AltaLL::registerAttributes(fn);

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

      if (lexer.tokens.size() > 0 && lexer.tokens.back().type == AltaCore::Lexer::TokenType::PreprocessorDirective && lexer.tokens.back().raw == "#") {
        lexer.tokens.pop_back();
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
        AltaLL::registerAttributes(path);

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

          if (lexer.tokens.size() > 0 && lexer.tokens.back().type == AltaCore::Lexer::TokenType::PreprocessorDirective && lexer.tokens.back().raw == "#") {
            lexer.tokens.pop_back();
          }

          AltaCore::Parser::Parser parser(lexer.tokens, *parsingDefinitions, modPath);
          parser.parse();
          auto root = std::dynamic_pointer_cast<AST::RootNode>(*parser.root);
          //root->detail(modPath);

          importCache[modPath.absolutify().toString()] = root;

          return root;
        } catch (AltaCore::Errors::Error& e) {
          logger(AltaCore::Logging::Message("frontend", "G0001", AltaCore::Logging::Severity::Error, e.position, e.what()));
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
        logger(AltaCore::Logging::Message("frontend", "G0001", AltaCore::Logging::Severity::Error, e.position, e.what()));
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
        logger(AltaCore::Logging::Message("frontend", "G0001", AltaCore::Logging::Severity::Error, e.position, e.what()));
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
        logger(AltaCore::Logging::Message("frontend", "G0001", AltaCore::Logging::Severity::Error, e.position, e.what()));
        return 11;
      }

      AltaCore::Filesystem::mkdirp(outDir); // ensure the output directory has been created

      try {
        AltaLL::compile(root, outDir / target.name);
      } catch (AltaCore::Errors::ValidationError& e) {
        std::cerr << CLI::COLOR_RED << "AST failed to compile" << CLI::COLOR_NORMAL << std::endl;
        logger(AltaCore::Logging::Message("frontend", "G0001", AltaCore::Logging::Severity::Error, e.position, e.what()));
        return 11;
      }
    }

    auto manifestOutpath = origOutDir / "manifest.json";
    std::ofstream manifestOutfile(manifestOutpath.toString());
    manifestOutfile << manifest;
    manifestOutfile.close();

    std::cout << CLI::COLOR_GREEN << "Successfully compiled input!" << CLI::COLOR_NORMAL << std::endl;

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
