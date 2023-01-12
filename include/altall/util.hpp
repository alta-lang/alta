#ifndef ALTALL_UTIL_HPP
#define ALTALL_UTIL_HPP

#include <llvm-c/Core.h>
#include <llvm-c/TargetMachine.h>
#include <memory>
#include <string>

namespace AltaLL {

	#define ALTALL_LLWRAP_DEF(_type, _dispose) \
		static inline std::shared_ptr<std::remove_pointer_t<_type>> llwrap(_type val) { \
			return std::shared_ptr<std::remove_pointer_t<_type>>(val, _dispose); \
		};

	// using a template doesn't work properly (the compiler doesn't use the explicit instantiations in some cases)
	ALTALL_LLWRAP_DEF(LLVMContextRef, LLVMContextDispose);
	ALTALL_LLWRAP_DEF(LLVMModuleRef, LLVMDisposeModule);
	ALTALL_LLWRAP_DEF(LLVMBuilderRef, LLVMDisposeBuilder);
	ALTALL_LLWRAP_DEF(LLVMTargetMachineRef, LLVMDisposeTargetMachine);
	ALTALL_LLWRAP_DEF(LLVMTargetDataRef, LLVMDisposeTargetData);
	ALTALL_LLWRAP_DEF(LLVMPassManagerRef, LLVMDisposePassManager);

	static inline std::string wrapMessage(char* msg) {
		std::string str(msg);
		LLVMDisposeMessage(msg);
		return str;
	};
};

#endif // ALTALL_UTIL_HPP
