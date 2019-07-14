#ifndef ALTA_CLI_CLI_HPP
#define ALTA_CLI_CLI_HPP

#include <altacore.hpp>
#include <talta.hpp>

#include "printers.hpp"
#include "cli-parser.hpp"

namespace CLI {
  static const std::string knownWarnings[] = {
    "msvc-crt-secure",
  };
  static const size_t knownWarnings_count = 1;
};

#endif // ALTA_CLI_CLI_HPP
