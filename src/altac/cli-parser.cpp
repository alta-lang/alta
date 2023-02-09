#include <altac/cli-parser.hpp>
#include <stdexcept>
#include <iostream>

const CLI::Parser& CLI::Parser::parse(int argCount, char** argArray) const {
  Option* waiting = nullptr;
  bool canParse = true;

  for (int i = 1; i < argCount; i++) {
    const auto argStr = argArray[i];
    const std::string arg = argStr;

    if (canParse && arg[0] == separator()) {
      if (waiting) {
        std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": no value found for supplied flag \"" << waiting->longID() << '"' << std::endl;
        exit(110);
      }

      bool includeShortID = true;
      auto search = arg.substr(1);
      std::string value;

      if (repeatSeparator() && search[0] == separator()) {
        includeShortID = false;
        search = search.substr(1);

        if (search.empty()) {
          canParse = false;
          continue;
        }
      }

      auto pos = search.find('=');
      if (pos != search.npos) {
        value = search.substr(pos + 1);
        search = search.substr(0, pos);
      }

      bool found = false;
      for (auto& opt: options) {
        if (includeShortID && search == opt->shortID()) {
          found = true;
        } else if (search == opt->longID()) {
          found = true;
        } else {
          continue;
        }
        if (!opt->isSwitch()) {
          if (value.empty()) {
            waiting = opt;
          } else {
            opt->addValue(value);
          }
        } else {
          opt->addValue("1");
        }
        break;
      }

      if (!found) {
        std::cout << CLI::COLOR_YELLOW << "Warning" << CLI::COLOR_NORMAL << ": no option found for supplied flag \"" << search << '"' << std::endl;
      }
    } else if (waiting) {
      waiting->addValue(arg);
      waiting = nullptr;
    } else {
      bool found = false;
      for (auto& opt: options) {
        if (opt->isUnnamed()) {
          opt->addValue(arg);
          found = true;
          break;
        }
      }
      if (!found) {
        std::cout << CLI::COLOR_YELLOW << "Warning" << CLI::COLOR_NORMAL << ": no option found for supplied argument \"" << arg << '"' << std::endl;
      }
    }
  }

  if (waiting) {
    std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": no value found for supplied flag \"" << waiting->longID() << '"' << std::endl;
    exit(110);
  }

  for (auto& opt: options) {
    if (opt->required() && !opt) {
      std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": no value found for required flag \"" << waiting->longID() << '"' << std::endl;
      exit(111);
    }
    if (!opt->canRepeat() && opt->values().size() > 1) {
      std::cerr << CLI::COLOR_RED << "Error" << CLI::COLOR_NORMAL << ": multiple values found for flag \"" << waiting->longID() << "\" (which doesn't accept multiple values)" << std::endl;
      exit(112);
    }

    if (*opt && opt->longID() == "help") {
      const auto tab = [](size_t times = 1) {
        std::string result = "";
        for (size_t i = 0; i < times; i++) {
          result += "  ";
        }
        return result;
      };
      std::cout << programName << " - " << programDescription << " (" << programVersion << ')' << std::endl;
      std::cout << std::endl;
      std::cout << "USAGE:" << std::endl;
      std::cout << std::endl;
      std::cout << tab(1) << argArray[0];
      Option* unnamed = nullptr;
      for (auto& option: options) {
        if (option->isUnnamed()) {
          unnamed = option;
          continue;
        }
        std::cout << ' ';
        if (!option->required()) std::cout << '[';
        if (!option->shortID().empty()) {
          std::cout << separator() << option->shortID();
        } else {
          if (repeatSeparator()) {
            std::cout << separator();
          }
          std::cout << separator() << option->longID();
        }
        if (!option->isSwitch()) {
          std::cout << " <" << option->valueDescription() << '>';
        }
        if (!option->required()) std::cout << ']';
        if (option->canRepeat()) std::cout << " ...";
      }
      if (unnamed) {
        std::cout << " [--]";
        std::cout << " <" << unnamed->valueDescription() << '>';
      }
      std::cout << std::endl;
      std::cout << std::endl;
      std::cout << std::endl;
      std::cout << "All options:" << std::endl;
      for (auto& option: options) {
        if (option->isUnnamed() || option->hidden()) continue;
        std::cout << tab(1);
        if (!option->shortID().empty()) {
          std::cout << separator() << option->shortID();
          if (!option->isSwitch()) {
            std::cout << " <" << option->valueDescription() << '>';
          }
        }
        if (!option->longID().empty()) {
          if (!option->shortID().empty()) {
            std::cout << ", ";
          }
          if (repeatSeparator()) {
            std::cout << separator();
          }
          std::cout << separator() << option->longID();
          if (!option->isSwitch()) {
            std::cout << " <" << option->valueDescription() << '>';
          }
        }
        if (option->canRepeat()) {
          std::cout << std::endl;
          std::cout << tab(2) << COLOR_BLUE << "Accepts multiple values" << COLOR_NORMAL;
        }
        if (option->required()) {
          std::cout << std::endl;
          std::cout << tab(2) << COLOR_BLUE << "Is required" << COLOR_NORMAL;
        }
        if (!option->defaultValue().empty()) {
          std::cout << std::endl;
          std::cout << tab(2) << COLOR_BLUE << "Defaults to " << COLOR_NORMAL << '"' << option->defaultValue() << '"';
        }
        std::cout << std::endl;
        std::cout << tab(2) << option->description() << std::endl;
      }
      if (unnamed) {
        std::cout << tab(1) << '<' << unnamed->valueDescription() << '>';
        if (unnamed->canRepeat()) {
          std::cout << std::endl;
          std::cout << tab(2) << COLOR_BLUE << "Accepts multiple values" << COLOR_NORMAL;
        }
        if (unnamed->required()) {
          std::cout << std::endl;
          std::cout << tab(2) << COLOR_BLUE << "Is required" << COLOR_NORMAL;
        }
        if (!unnamed->defaultValue().empty()) {
          std::cout << std::endl;
          std::cout << tab(2) << COLOR_BLUE << "Defaults to " << COLOR_NORMAL << '"' << unnamed->defaultValue() << '"';
        }
        std::cout << std::endl;
        std::cout << tab(2) << unnamed->description() << std::endl;
      }
      exit(0);
    } else if (*opt && opt->longID() == "version") {
      std::cout << programName << " - " << programDescription << " (" << programVersion << ')' << std::endl;
      exit(0);
    }
  }

  return *this;
};
