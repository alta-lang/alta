#include <cstring>
#include <cstdio>
#include <string>
#include <fstream>
#include "../include/cli.hpp"
#include <crossguid/guid.hpp>
#include <algorithm>

int main(int argc, char** argv) {
  if (argc < 2) {
    fprintf(stderr, "Expected an argument.\n");
    return 1;
  }

  if (strcmp(argv[1], "-h") == 0 || strcmp(argv[1], "--help") == 0) {
    printf("AltaCore CLI v1.0.0\n");
    return 0;
  }

  const char* filename = argv[1];
  std::ifstream file(filename);
  std::string line;
  AltaCore::Lexer::Lexer lexer;
  
  if (!file.is_open()) {
    throw std::runtime_error("Couldn't open the input file!");
  }

  while (std::getline(file, line)) {
    if (file.peek() != EOF) {
      line += "\n";
    }
    lexer.feed(line);
  }

  file.close();

  /*
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
  */

  for (const auto& absence: lexer.absences) {
    auto [line, column] = absence;
    fprintf(stderr, "%sERROR%s: Failed to lex input at %zu:%zu\n", CLI::COLOR_RED, CLI::COLOR_NORMAL, line, column);
  }

  AltaCore::Parser::Parser parser(lexer.tokens);

  parser.parse();

  if (parser.root == nullptr) throw std::runtime_error("AHHHH. NO PARSINGS.");

  /*
  printf("\n%sAST:%s\n", CLI::COLOR_BLUE, CLI::COLOR_NORMAL);
  CLI::Printers::printAST(parser.root);
  printf("\n");
  */

  auto inputFile = AltaCore::Filesystem::Path(filename).absolutify();
  parser.root->detail(inputFile);

  /*
  printf("\n%sDET:%s\n", CLI::COLOR_BLUE, CLI::COLOR_NORMAL);
  CLI::Printers::printDET(parser.root->$module);
  printf("\n");
  */

  auto outDir = inputFile.dirname() / "alta-build";
  auto transpiledModules = Talta::recursivelyTranspileToC(parser.root);
  auto indexHeaderPath = outDir / "index.h";
  std::ofstream outfileIndex(indexHeaderPath.toString());
  std::stringstream uuidStream;
  uuidStream << xg::newGuid();
  std::string indexUUID = uuidStream.str();
  std::replace(indexUUID.begin(), indexUUID.end(), '-', '_');

  outfileIndex << "#ifndef _ALTA_INDEX_" << indexUUID << "\n";
  outfileIndex << "#define _ALTA_INDEX_" << indexUUID << "\n";

  for (auto& [moduleName, info]: transpiledModules) {
    auto& [cRoot, hRoot, mod] = info;
    auto base = outDir / moduleName;
    auto cOut = base + ".c";
    auto hOut = base + ".h";

    auto ok = AltaCore::Filesystem::mkdirp(base.dirname());
    if (!ok) {
      auto str = base.dirname().toString();
      fprintf(stderr, "%sError:%s failed to create output directory (\"%s\")\n", CLI::COLOR_RED, CLI::COLOR_NORMAL, str.c_str());
      return 1;
    }
    std::ofstream outfileC(cOut.toString());
    std::ofstream outfileH(hOut.toString());

    outfileC << "#include \"" << hOut.relativeTo(cOut).toString('/') << "\"\n";
    outfileC << cRoot->toString();
    // include the index in the module's header, otherwise we won't be able to find our dependencies
    outfileH << "#include \"" << indexHeaderPath.relativeTo(hOut).toString('/') << "\"\n";
    outfileH << hRoot->toString();

    // finally, add our header to the index for all our dependents
    for (auto& dependent: mod->dependents) {
      outfileIndex << "#define _ALTA_MODULE_" << Talta::mangleName(dependent.get()) << "_0_INCLUDE_" << Talta::mangleName(mod.get()) << " \"" << hOut.relativeTo(outDir / dependent->name).toString('/') << "\"\n";
    }
  }

  outfileIndex << "#endif // _ALTA_INDEX_" << indexUUID << "\n";

  printf("%sSuccessfully transpiled input!%s", CLI::COLOR_GREEN, CLI::COLOR_NORMAL);

  return 0;
};
