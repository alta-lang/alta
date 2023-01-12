#ifndef ALTALL_MANGLE_HPP
#define ALTALL_MANGLE_HPP

#include <string>
#include <altacore.hpp>

namespace AltaLL {
	std::string cTypeNameify(AltaCore::DET::Type* type, bool mangled = true);
	std::string mangleType(AltaCore::DET::Type* type);
	AltaCore::DET::ScopeItem* followAlias(AltaCore::DET::ScopeItem* maybeAlias);
	std::string escapeName(std::string name);
	std::string mangleName(AltaCore::DET::Module* mod, bool fullName = true);
	std::string mangleName(AltaCore::DET::Scope* scope, bool fullName = true);
	std::string mangleName(AltaCore::DET::ScopeItem* item, bool fullName = true);
};

#endif // ALTALL_MANGLE_HPP
