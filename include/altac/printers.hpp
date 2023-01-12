#ifndef ALTAC_PRINTERS_HPP
#define ALTAC_PRINTERS_HPP

#include <altacore.hpp>

namespace CLI {
  static const char* const COLOR_NORMAL = "\x1b[0m";
  static const char* const COLOR_RED = "\x1b[31m";
  static const char* const COLOR_GREEN = "\x1b[32m";
  static const char* const COLOR_YELLOW = "\x1b[33m";
  static const char* const COLOR_BLUE = "\x1b[34m";
  static const char* const COLOR_MAGENTA = "\x1b[35m";
  static const char* const COLOR_CYAN = "\x1b[36m";
  static const char* const COLOR_WHITE = "\x1b[37m";

  namespace Printers {
    void printAST(AltaCore::AST::Node* node, std::string indent = "", bool indentName = true, bool autoPrint = true);
    void printDET(AltaCore::DET::Node* node, std::string indent = "", bool indentName = true, bool autoPrint = true);
  };
};

#endif // ALTAC_PRINTERS_HPP
