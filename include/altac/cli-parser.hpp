#ifndef ALTAC_PARSER_HPP
#define ALTAC_PARSER_HPP

#include <string>
#include <vector>
#include "printers.hpp"

namespace CLI {
  #define CLI_PROPERTY_DEFAULT(name, type, def) \
    private:\
      type _##name = def;\
    public:\
      inline This& name(type name) { _##name = name; return *this; }\
      inline type name() const { return _##name; }
  #define CLI_PROPERTY(name, type) CLI_PROPERTY_DEFAULT(name, type, {})

  class Parser;

  class Option {
    friend class Parser;

    private:
      using This = Option;

      std::vector<std::string> _value;

      inline This& addValue(std::string value) {
        _value.push_back(value);
        return *this;
      };
    public:
      Option() {};

      CLI_PROPERTY(shortID, std::string);
      CLI_PROPERTY(longID, std::string);
      CLI_PROPERTY(description, std::string);
      CLI_PROPERTY(valueDescription, std::string);
      CLI_PROPERTY(defaultValue, std::string);
      CLI_PROPERTY_DEFAULT(canRepeat, bool, false);
      CLI_PROPERTY_DEFAULT(required, bool, false);

      inline This& shortID(char id) {
        _shortID = id;
        return *this;
      };

      inline bool isSwitch() const {
        return _valueDescription.empty() && _defaultValue.empty();
      };

      inline bool isUnnamed() const {
        return _shortID.empty() && _longID.empty();
      };

      inline std::string value() const {
        return *this ? _value.front() : _defaultValue;
      };
      inline std::vector<std::string> values() const {
        return _value;
      };

      inline operator bool() const {
        return _value.size() > 0 && !_value.front().empty();
      };
  };

  class Parser {
    private:
      using This = Parser;

      Option help;
      Option version;

      std::string programName;
      std::string programDescription;
      std::string programVersion;
      std::vector<Option*> options;
    public:
      Parser(
        std::string _programName,
        std::string _programDescription,
        std::string _programVersion
      ):
        programName(_programName),
        programDescription(_programDescription),
        programVersion(_programVersion)
      {
        help
          .shortID("h")
          .longID("help")
          .description("Prints this help prompt and exits")
          ;
        version
          .longID("version")
          .description("Prints the version and exits")
          ;
        add(help);
        add(version);
      };

      CLI_PROPERTY_DEFAULT(separator, char, '-');
      CLI_PROPERTY_DEFAULT(repeatSeparator, bool, true);

      inline This& add(Option& option) {
        if (option.isUnnamed()) {
          for (auto& opt: options) {
            if (opt->isUnnamed()) {
              throw std::runtime_error("can't have multiple unnamed options");
            }
          }
        }
        options.emplace_back(&option);
        return *this;
      };

      const This& parse(int argCount, char** argArray) const;
  };
};

#endif // ALTAC_PARSER_HPP
