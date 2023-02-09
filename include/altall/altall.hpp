#ifndef ALTALL_ALTALL_HPP
#define ALTALL_ALTALL_HPP

#include <altacore.hpp>

namespace AltaLL {
	bool init();
	void finit();

	void registerAttributes(AltaCore::Filesystem::Path modulePath);

	void compile(std::shared_ptr<AltaCore::AST::RootNode> root, AltaCore::Filesystem::Path binaryOutputPath, bool debug);
};

#endif // ALTALL_ALTALL_HPP
