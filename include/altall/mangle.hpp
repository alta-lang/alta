#ifndef ALTALL_MANGLE_HPP
#define ALTALL_MANGLE_HPP

#include <string>
#include <altacore.hpp>

namespace AltaLL {
	extern ALTACORE_MAP<std::string, std::string> mangledToFullMapping;

	std::string cTypeNameify(std::shared_ptr<AltaCore::DET::Type> type, bool mangled = true);
	std::string mangleType(std::shared_ptr<AltaCore::DET::Type> type);
	std::shared_ptr<AltaCore::DET::ScopeItem> followAlias(std::shared_ptr<AltaCore::DET::ScopeItem> maybeAlias);
	std::string escapeName(std::string name);
	std::string mangleName(std::shared_ptr<AltaCore::DET::Module> mod, bool fullName = true);
	std::string mangleName(std::shared_ptr<AltaCore::DET::Scope> scope, bool fullName = true);
	std::string mangleName(std::shared_ptr<AltaCore::DET::ScopeItem> item, bool fullName = true, bool root = true);
};

#endif // ALTALL_MANGLE_HPP
