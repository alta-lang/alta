#ifndef ALTALL_ALTALL_HPP
#define ALTALL_ALTALL_HPP

#include <altacore.hpp>

namespace AltaLL {
	bool init();
	void finit();

	typedef std::function<void(AltaCore::Errors::ValidationError&)> ValidationErrorHandler;
	extern ValidationErrorHandler validationErrorHandler;

	void registerAttributes(AltaCore::Filesystem::Path modulePath);

	void compile(std::shared_ptr<AltaCore::AST::RootNode> root, AltaCore::Filesystem::Path binaryOutputPath, bool debug, std::string targetName, std::string cpu = "", std::string cpuFeatures = "");
};

#endif // ALTALL_ALTALL_HPP
