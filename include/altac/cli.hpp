#ifndef ALTAC_CLI_HPP
#define ALTAC_CLI_HPP

#include <altacore.hpp>

#include "printers.hpp"
#include "cli-parser.hpp"

namespace CLI {
  static const std::string knownWarnings[] = {
    "msvc-crt-secure",
  };
  static const size_t knownWarnings_count = 1;
};

#endif // ALTAC_CLI_HPP
